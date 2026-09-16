#include "mergepanel.h"
#include "operations/operationpanel.h"
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include "ui/flatsections.h"
#include "ui/guardeddialog.h"
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QPlainTextEdit *editor(bool readOnly){auto *w=new QPlainTextEdit;w->setReadOnly(readOnly);w->setLineWrapMode(QPlainTextEdit::NoWrap);w->setFont(QFont("Consolas",10));return w;}
bool confirm(QWidget *parent,const QString &message){QMessageBox box(QMessageBox::Warning,QObject::tr("작업 확인"),message,QMessageBox::Yes|QMessageBox::No,parent,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);box.setTextFormat(Qt::PlainText);box.setDefaultButton(QMessageBox::No);return box.exec()==QMessageBox::Yes;}
}
MergePanel::MergePanel(GitClient *git,QWidget *parent):QWidget(parent),git_(git),controller_(git,this){
    setObjectName("mergePanel");auto *layout=new QVBoxLayout(this);tabs_=new FlatSections;layout->addWidget(tabs_,1);
    auto *mergeTab=new QWidget;auto *mergeLayout=new QVBoxLayout(mergeTab);tabs_->addSection(mergeTab,tr("브랜치 비교 · 병합"));
    auto *intro=new QLabel(tr("드롭다운에서 기존 로컬 또는 원격 추적 브랜치를 선택해 현재 브랜치에 합칩니다. 원격의 최신 내용은 상단 Fetch로 가져온 뒤 비교하세요."));intro->setWordWrap(true);mergeLayout->addWidget(intro);
    auto *row=new QHBoxLayout;target_=new QComboBox;target_->setEditable(false);target_->setObjectName("mergeTarget");target_->setToolTip(tr("목록에서 병합할 로컬 또는 원격 추적 브랜치를 선택하세요."));row->addWidget(target_,1);compare_=new QPushButton(tr("대상과 비교"));compare_->setObjectName("compareMerge");merge_=new QPushButton(tr("비교한 커밋 병합"));merge_->setObjectName("executeMerge");row->addWidget(compare_);row->addWidget(merge_);mergeLayout->addLayout(row);
    comparison_=editor(true);comparison_->setObjectName("mergeComparison");mergeLayout->addWidget(comparison_,1);
    auto *refreshRefs=new QPushButton(tr("브랜치 목록 갱신"));row->addWidget(refreshRefs);connect(refreshRefs,&QPushButton::clicked,this,[this]{pendingRefs_=true;});
    connect(compare_,&QPushButton::clicked,this,&MergePanel::compare);
    connect(target_,&QComboBox::currentTextChanged,this,[this]{preview_={};update();});
    connect(merge_,&QPushButton::clicked,this,[this]{
        if(!confirm(this,tr("현재 %1에 %2 (%3)를 병합합니다. 빨리 감기가 가능하면 바로 이동하고, 이력이 갈라졌으면 병합 커밋을 만듭니다. 충돌 시 편집기에서 해결한 뒤 계속하세요. 병합 전 HEAD의 복구 참조를 남깁니다.").arg(preview_.head.left(12),preview_.ref,preview_.target.left(12))))return;
        loading_=true;update();controller_.merge(preview_,[this](bool ok,const QByteArray &out,const QString &error){loading_=false;preview_={};status_->setPlainText(ok?tr("병합 완료\n")+QString::fromUtf8(out):error);if(!git_->operationState().conflicts.isEmpty())tabs_->scrollToSection(1);update();emit repositoryChanged();});
    });
    auto *conflicts=new QWidget;auto *conflictLayout=new QVBoxLayout(conflicts);tabs_->addSection(conflicts,tr("3-way 충돌 해결"));
    hint_=new QLabel(tr("충돌 파일을 선택하면 기준·Ours·Theirs와 현재 해결 결과를 읽습니다."));hint_->setWordWrap(true);hint_->setTextFormat(Qt::PlainText);conflictLayout->addWidget(hint_);
    hint_->setText(tr("충돌 파일을 선택하면 큰 편집창이 열립니다. 왼쪽 Base와 오른쪽 Merge를 참고해 가운데 결과를 편집하세요."));
    comparison_->setMinimumHeight(130);comparison_->setMaximumHeight(240);
    files_=new QListWidget;files_->setObjectName("mergeConflictFiles");files_->setMinimumHeight(160);conflictLayout->addWidget(files_,1);
    auto *openEditor=new QPushButton(tr("선택 파일 충돌 편집"));openEditor->setObjectName("editSelectedConflict");conflictLayout->addWidget(openEditor);connect(openEditor,&QPushButton::clicked,this,&MergePanel::load);
    editorDialog_=new GuardedDialog(this,[this]{return !loading_&&!git_->isBusy()&&confirmLeave();});editorDialog_->setObjectName("mergeEditorDialog");editorDialog_->resize(1400,820);editorDialog_->setWindowModality(Qt::WindowModal);
    auto *workLayout=new QVBoxLayout(editorDialog_);editorHint_=new QLabel;editorHint_->setTextFormat(Qt::PlainText);editorHint_->setWordWrap(true);workLayout->addWidget(editorHint_);
    auto *sourceButtons=new QHBoxLayout;
    for(const auto &choice:QStringList{"ours","theirs","delete"}){
        auto *button=new QPushButton(choice=="ours"?tr("Base 파일 사용"):choice=="theirs"?tr("Merge 파일 사용"):tr("해당 파일 삭제"));button->setObjectName("resolve_"+choice);sourceButtons->addWidget(button);resolutionButtons_.append(button);connect(button,&QPushButton::clicked,this,[this,choice]{save(choice);});
    }
    sourceButtons->addStretch();workLayout->addLayout(sourceButtons);
    auto *sources=new QSplitter; sources->setObjectName("conflictThreeWay");workLayout->addWidget(sources,1);
    // Stage 1 is the common ancestor, retained for backup; the visible Base is stage 2.
    sides_[0]=editor(true);sides_[0]->setObjectName("conflictBase");sides_[0]->setParent(editorDialog_);sides_[0]->hide();
    sides_[1]=editor(true);sides_[1]->setObjectName("conflictOurs");sides_[2]=editor(true);sides_[2]->setObjectName("conflictTheirs");
    result_=editor(false);result_->setObjectName("conflictResult");result_->setPlaceholderText(tr("두 버전을 참고해 최종 내용을 편집하고 저장 + Stage하세요."));
    const QList<QPlainTextEdit*> editors{sides_[1],result_,sides_[2]};
    const QStringList titles{tr("Base branch · 기준 브랜치"),tr("최종 결과 · 직접 편집"),tr("Merge branch · 병합할 브랜치")};
    for(int i=0;i<editors.size();++i){auto *column=new QWidget;auto *columnLayout=new QVBoxLayout(column);columnLayout->setContentsMargins(0,0,0,0);columnLayout->addWidget(new QLabel(titles[i]));columnLayout->addWidget(editors[i],1);sources->addWidget(column);sources->setStretchFactor(i,1);}
    sources->setSizes({450,500,450});
    auto *editorActions=new QHBoxLayout;save_=new QPushButton(tr("결과 저장 + Stage"));save_->setObjectName("saveConflictResult");editorActions->addStretch();editorActions->addWidget(save_);auto *closeEditor=new QPushButton(tr("닫기"));closeEditor->setObjectName("closeConflictEditor");editorActions->addWidget(closeEditor);workLayout->addLayout(editorActions);
    editorDialog_->setSizeGripEnabled(true);
    auto *editorStatus=editor(true);editorStatus->setObjectName("conflictEditorStatus");editorStatus->setMaximumHeight(85);editorStatus->hide();workLayout->addWidget(editorStatus);
    connect(closeEditor,&QPushButton::clicked,editorDialog_,&GuardedDialog::reject);connect(save_,&QPushButton::clicked,this,[this]{save("edit");});
    connect(files_,&QListWidget::currentRowChanged,this,[this]{load();});
    auto *actions=new QHBoxLayout;auto *refresh=new QPushButton(tr("충돌 다시 읽기"));refresh->setObjectName("reloadConflict");continue_=new QPushButton(tr("해결 후 계속 · Continue"));continue_->setObjectName("mergeContinue");abort_=new QPushButton(tr("진행 작업 취소 · Abort"));abort_->setObjectName("mergeAbort");actions->addWidget(refresh);actions->addWidget(continue_);actions->addWidget(abort_);layout->addLayout(actions);
    connect(refresh,&QPushButton::clicked,this,[this]{load();});connect(continue_,&QPushButton::clicked,this,[this]{recover(false);});connect(abort_,&QPushButton::clicked,this,[this]{recover(true);});
    status_=editor(true);status_->setObjectName("mergeResult");status_->setMaximumHeight(60);layout->addWidget(status_);status_->hide();connect(status_,&QPlainTextEdit::textChanged,this,[this]{status_->setVisible(!status_->toPlainText().isEmpty());});
    connect(status_,&QPlainTextEdit::textChanged,editorStatus,[this,editorStatus]{editorStatus->setPlainText(status_->toPlainText());editorStatus->setVisible(!status_->toPlainText().isEmpty());});
    connect(git,&GitClient::busyChanged,this,[this,refresh](bool busy){refresh->setEnabled(!busy&&!loading_);update();});
    connect(git,&GitClient::operationStateChanged,this,&MergePanel::update);
    auto *timer=new QTimer(this);timer->setInterval(250);connect(timer,&QTimer::timeout,this,[this]{
        if(!git_->isBusy()&&!loading_&&pendingCompare_&&!pendingRefs_){pendingCompare_=false;compare();}
        else if(isVisible()&&!git_->isBusy()&&!loading_&&pendingRefs_&&!repository_.isEmpty()){
            pendingRefs_=false;loading_=true;update();git_->inspect({"for-each-ref","--format=%(refname)%00%(refname:short)%00%(symref)","refs/heads","refs/remotes"},[this](bool ok,const QByteArray &out,const QString &error){loading_=false;if(ok){const QSignalBlocker blocker(target_);const auto current=target_->currentData().toString();target_->clear();for(const auto &line:out.split('\n')){const auto fields=line.split('\0');if(fields.size()==3&&fields[2].trimmed().isEmpty())target_->addItem(QString::fromUtf8(fields[1]),QString::fromUtf8(fields[0]));}if(!requestedRef_.isEmpty()){target_->setCurrentIndex(target_->findData(requestedRef_));requestedRef_.clear();}else target_->setCurrentIndex(qMax(0,target_->findData(current)));preview_={};}else {const QSignalBlocker blocker(target_);target_->clear();preview_={};requestedRef_.clear();pendingCompare_=false;status_->setPlainText(error);}update();});
        }
    });timer->start();update();
}
void MergePanel::openTarget(const QString &ref){requestedRef_=ref;pendingRefs_=true;tabs_->scrollToSection(0);pendingCompare_=true;}
void MergePanel::openConflicts(){tabs_->scrollToSection(1);}
bool MergePanel::confirmLeave(){if(!result_->document()->isModified())return true;if(!confirm(this,tr("저장하지 않은 충돌 해결 결과를 버릴까요? 디스크에 저장한 파일과 백업은 유지됩니다.")))return false;result_->document()->setModified(false);return true;}
void MergePanel::update(){
    const auto &state=git_->operationState();if(repository_!=git_->repositoryPath()){repository_=git_->repositoryPath();pendingRefs_=true;preview_={};conflict_={};comparison_->clear();result_->clear();for(auto *side:sides_)side->clear();}
    QStringList listed;for(int i=0;i<files_->count();++i)listed.append(files_->item(i)->text());
    if(listed!=state.conflicts){const QSignalBlocker block(files_);files_->clear();files_->addItems(state.conflicts);}
    const bool ready=!loading_&&!git_->isBusy()&&!repository_.isEmpty();const bool clean=state.known&&state.locks.isEmpty()&&state.operation.isEmpty()&&state.conflicts.isEmpty();
    if(auto *refresh=findChild<QPushButton*>("reloadConflict"))refresh->setEnabled(ready&&files_->currentItem());
    compare_->setEnabled(ready&&clean&&target_->currentIndex()>=0);target_->setEnabled(ready);merge_->setEnabled(ready&&clean&&!preview_.target.isEmpty());files_->setEnabled(ready);
    const bool selected=ready&&!conflict_.path.isEmpty()&&state.conflicts.contains(conflict_.path)&&state.locks.isEmpty();
    save_->setEnabled(selected&&conflict_.text);result_->setReadOnly(!selected||!conflict_.text);for(auto *button:resolutionButtons_)button->setEnabled(selected);
    const bool recovery=QStringList{"merge","rebase","cherry-pick","revert","am"}.contains(state.operation)&&state.locks.isEmpty();continue_->setEnabled(ready&&recovery&&state.conflicts.isEmpty());abort_->setEnabled(ready&&recovery);
}
void MergePanel::compare(){if(git_->isBusy()||loading_||target_->currentIndex()<0)return;loading_=true;update();controller_.preview(target_->currentData().toString(),[this](bool ok,MergePreview preview,QString error){loading_=false;preview_=ok?preview:MergePreview{};comparison_->setPlainText(ok?QString::fromUtf8(preview.comparison):error);update();});}
void MergePanel::load(){
    if(git_->isBusy()||loading_||!files_->currentItem())return;
    if(result_->document()->isModified()&&!confirm(this,tr("저장하지 않은 해결 결과를 버리고 충돌 파일을 다시 읽을까요?"))){const QSignalBlocker block(files_);const auto old=files_->findItems(conflict_.path,Qt::MatchExactly);if(!old.isEmpty())files_->setCurrentItem(old.first());return;}
    status_->clear();loading_=true;update();controller_.loadConflict(files_->currentItem()->text(),[this](bool ok,ConflictFile file,QString error){
        loading_=false;conflict_=ok?file:ConflictFile{};if(!ok){status_->setPlainText(error);update();return;}
        for(int i=0;i<3;++i)sides_[i]->setPlainText(!file.present[i]?tr("이 버전에는 파일이 없습니다."):file.text?QString::fromUtf8(file.sides[i]):tr("바이너리 / 비 UTF-8 · %1 바이트\n파일 전체 선택으로 해결하세요.").arg(file.sides[i].size()));
        result_->setPlainText(file.text?QString::fromUtf8(file.working):tr("텍스트 편집을 지원하지 않는 파일입니다. Ours/Theirs 전체 사용 또는 삭제를 선택하세요."));result_->document()->setModified(false);
        editorHint_->setText(file.path+"\n"+(file.operation=="rebase"?tr("Rebase: 왼쪽 Base는 재배치 기준 쪽(Ours), 오른쪽 Merge는 지금 재적용하는 커밋(Theirs)입니다."):tr("왼쪽 Base는 병합을 받는 현재 쪽(Ours), 오른쪽 Merge는 합칠 쪽(Theirs)입니다. 선택한 쪽에 파일이 없으면 파일을 삭제합니다.")));update();editorDialog_->show();editorDialog_->raise();
    });
}
void MergePanel::save(const QString &choice){
    if(git_->isBusy()||loading_||conflict_.path.isEmpty())return;
    if(!confirm(this,tr("%1\n해결 방식: %2\n원본과 세 버전을 백업한 뒤 파일을 저장/삭제하고 이 파일만 Stage합니다. 다른 Stage 파일은 유지됩니다. Continue는 Stage 전체를 포함할 수 있으니 확인하세요.").arg(conflict_.path,choice)))return;
    QByteArray data=result_->toPlainText().toUtf8();if(conflict_.working.contains("\r\n"))data.replace("\n","\r\n");if(conflict_.working.startsWith("\xEF\xBB\xBF")&&!data.startsWith("\xEF\xBB\xBF"))data.prepend("\xEF\xBB\xBF");
    loading_=true;update();controller_.resolve(conflict_,choice,data,[this](bool ok,const QByteArray &out,const QString &error){loading_=false;status_->setPlainText(ok?tr("해결 결과를 Stage했습니다. 남은 충돌을 해결한 뒤 Continue하세요.\n")+QString::fromUtf8(out):error);if(ok){conflict_={};result_->document()->setModified(false);editorDialog_->hide();}update();emit repositoryChanged();});
}
void MergePanel::recover(bool abort){
    const auto state=git_->operationState();
    if(!confirm(this,abort?tr("진행 작업을 Abort합니다. 충돌 해결 중인 변경을 버릴 수 있습니다. 파일별 저장 시 만든 백업은 유지됩니다."):tr("Stage된 전체 내용을 사용하여 진행 작업을 계속합니다. 이후 추가 충돌이 나타날 수 있습니다.")))return;
    loading_=true;update();git_->recoverOperation(state.fingerprint,abort,[this](bool ok,const QByteArray &,const QString &error){loading_=false;status_->setPlainText(ok?tr("작업 상태를 갱신했습니다."):error);conflict_={};result_->document()->setModified(false);update();emit repositoryChanged();});
}
