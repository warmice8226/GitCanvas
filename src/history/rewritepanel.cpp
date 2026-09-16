#include "rewritepanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include "ui/flatsections.h"
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
bool ask(QWidget *parent,const QString &text){QMessageBox box(QMessageBox::Warning,QObject::tr("이력 작업 확인"),text,QMessageBox::Yes|QMessageBox::No,parent,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);box.setTextFormat(Qt::PlainText);box.setDefaultButton(QMessageBox::No);return box.exec()==QMessageBox::Yes;}
QPlainTextEdit *view(){auto *w=new QPlainTextEdit;w->setReadOnly(true);w->setLineWrapMode(QPlainTextEdit::NoWrap);return w;}
QTableWidget *table(const QStringList &headers){auto *w=new QTableWidget(0,headers.size());w->setHorizontalHeaderLabels(headers);w->setSelectionBehavior(QAbstractItemView::SelectRows);w->setSelectionMode(QAbstractItemView::SingleSelection);w->setEditTriggers(QAbstractItemView::NoEditTriggers);w->verticalHeader()->hide();w->horizontalHeader()->setStretchLastSection(true);return w;}
}
RewritePanel::RewritePanel(GitClient *git,QWidget *parent):QWidget(parent),git_(git),controller_(git,this){
    setObjectName("rewritePanel");auto *layout=new QVBoxLayout(this);tabs_=new FlatSections;layout->addWidget(tabs_,1);
    auto *rebase=new QWidget;auto *rl=new QVBoxLayout(rebase);tabs_->addSection(rebase,tr("Interactive rebase"));
    auto *hint=new QLabel(tr("기준 커밋은 유지하고 그 이후 커밋을 오래된 순서로 편집합니다. 이력 ID가 바뀌므로 공유한 커밋은 주의하세요. 병합 없는 1~200개 구간을 지원합니다."));hint->setWordWrap(true);rl->addWidget(hint);
    auto *row=new QHBoxLayout;base_=new QLineEdit("HEAD~1");base_->setObjectName("rebaseBase");base_->setToolTip(tr("수정하지 않고 유지할 기준입니다. History 우클릭으로 기준을 선택할 수도 있습니다."));row->addWidget(base_,1);root_=new QCheckBox(tr("첫 커밋부터"));root_->setObjectName("rebaseRoot");row->addWidget(root_);prepare_=new QPushButton(tr("계획 읽기"));prepare_->setObjectName("prepareRebase");row->addWidget(prepare_);rl->addLayout(row);
    planTable_=table({tr("동작"),tr("원래 커밋"),tr("원래 제목")});planTable_->setObjectName("rebasePlan");planTable_->setColumnWidth(0,150);planTable_->setColumnWidth(1,120);rl->addWidget(planTable_,1);
    auto *moves=new QHBoxLayout;up_=new QPushButton(tr("↑ 위로"));down_=new QPushButton(tr("↓ 아래로"));up_->setObjectName("rebaseMoveUp");down_->setObjectName("rebaseMoveDown");moves->addWidget(up_);moves->addWidget(down_);start_=new QPushButton(tr("계획 확인 후 실행"));start_->setObjectName("startRebase");moves->addWidget(start_,1);rl->addLayout(moves);
    auto *help=new QLabel(tr("Pick 유지 · Reword 메시지 수정 · Edit 멈춰서 파일/커밋 수정 · Squash 이전 커밋과 메시지까지 합치기 · Fixup 이전 커밋에 내용만 합치기 · Drop 제외"));help->setWordWrap(true);rl->addWidget(help);
    message_=new QPlainTextEdit;message_->setObjectName("rebaseMessage");message_->setMaximumHeight(115);message_->setPlaceholderText(tr("Reword를 선택한 커밋의 제목과 본문"));rl->addWidget(message_);
    connect(prepare_,&QPushButton::clicked,this,&RewritePanel::prepare);
    connect(base_,&QLineEdit::textChanged,this,[this]{update();});
    connect(root_,&QCheckBox::toggled,this,[this]{update();});
    connect(planTable_,&QTableWidget::itemSelectionChanged,this,[this]{const QSignalBlocker block(message_);const int row=planTable_->currentRow();message_->setPlainText(row>=0&&row<plan_.entries.size()?plan_.entries[row].message:QString());update();});
    connect(message_,&QPlainTextEdit::textChanged,this,[this]{const int row=planTable_->currentRow();if(row>=0&&row<plan_.entries.size()){plan_.entries[row].message=message_->toPlainText();dirty_=true;}});
    for(auto pair:{qMakePair(up_,-1),qMakePair(down_,1)})connect(pair.first,&QPushButton::clicked,this,[this,pair]{const int row=planTable_->currentRow(),next=row+pair.second;if(next<0||next>=plan_.entries.size())return;plan_.entries.swapItemsAt(row,next);dirty_=true;renderPlan();planTable_->selectRow(next);});
    connect(start_,&QPushButton::clicked,this,[this]{
        QStringList summary;for(const auto &entry:plan_.entries)summary.append(entry.action+" "+entry.oid.left(12)+" "+entry.subject);
        QMessageBox box(QMessageBox::Warning,tr("Rebase 실행"),tr("현재 HEAD %1의 이력을 아래 계획으로 다시 만듭니다. 제외·합치기·순서 변경은 커밋 ID와 결과를 바꿀 수 있습니다. 기존 HEAD와 계획을 백업합니다. 자동 Push는 하지 않습니다.\n\n%2").arg(plan_.head.left(12),summary.mid(0,8).join('\n')),QMessageBox::Yes|QMessageBox::No,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);box.setTextFormat(Qt::PlainText);box.setDetailedText(summary.join('\n'));box.setDefaultButton(QMessageBox::No);if(box.exec()!=QMessageBox::Yes)return;
        loading_=true;update();controller_.start(plan_,[this](bool ok,const QByteArray &out,const QString &error){loading_=false;if(ok||git_->operationState().operation=="rebase"){dirty_=false;plan_={};renderPlan();}status_->setPlainText(ok?tr("명령 완료. Edit 정지 또는 추가 충돌이 있는지 아래 작업 상태를 확인하세요.\n")+QString::fromUtf8(out):error);update();emit repositoryChanged();});
    });
    auto *recovery=new QWidget;auto *cl=new QVBoxLayout(recovery);tabs_->addSection(recovery,tr("Reflog · 복구 참조"));
    auto *controls=new QHBoxLayout;refresh_=new QPushButton(tr("복구 기록 읽기"));refresh_->setObjectName("refreshRecovery");controls->addWidget(refresh_);limit_=new QSpinBox;limit_->setRange(200,2000);limit_->setSingleStep(200);limit_->setSuffix(tr("개 HEAD reflog"));controls->addWidget(limit_);filter_=new QLineEdit;filter_->setPlaceholderText(tr("참조·메시지·커밋 검색"));controls->addWidget(filter_,1);cl->addLayout(controls);
    auto *split=new QSplitter(Qt::Vertical);recoveryTable_=table({tr("종류"),tr("참조 / 기록"),tr("날짜"),tr("제목"),tr("커밋")});recoveryTable_->setObjectName("recoveryEntries");recoveryTable_->setColumnWidth(1,300);recoveryTable_->setColumnWidth(3,220);split->addWidget(recoveryTable_);details_=view();details_->setObjectName("recoveryDetails");split->addWidget(details_);cl->addWidget(split,1);
    branch_=new QLineEdit;branch_->setObjectName("recoveryBranchName");branch_->setPlaceholderText(tr("복구할 새 브랜치 이름 (기존 브랜치를 덮어쓰지 않음)"));cl->addWidget(branch_);
    auto *actions=new QHBoxLayout;
    for(const auto &action:QStringList{"branch","reset","stash","delete-ref"}){const auto label=action=="branch"?tr("새 브랜치로 보존"):action=="reset"?tr("현재 브랜치 되돌리기"):action=="stash"?tr("Stash 목록으로 복원"):tr("복구 참조 삭제");auto *button=new QPushButton(label);button->setObjectName("recovery_"+action);button->setProperty("action",action);actions->addWidget(button);recoveryButtons_.append(button);connect(button,&QPushButton::clicked,this,[this,action]{recover(action);});}cl->addLayout(actions);
    auto *folders=new QHBoxLayout;for(const auto &folder:QStringList{"worktree-backups","conflict-backups","rebase-plans"}){auto *button=new QPushButton(folder=="worktree-backups"?tr("파일 정리 백업 폴더"):folder=="conflict-backups"?tr("충돌 백업 폴더"):tr("Rebase 계획 폴더"));folders->addWidget(button);connect(button,&QPushButton::clicked,this,[this,folder]{const auto directory=QDir(git_->operationState().gitDirectory).filePath("gitcanvas/"+folder);if(git_->operationState().gitDirectory.isEmpty()||!QDir(directory).exists()){status_->setPlainText(tr("아직 해당 백업 폴더가 없습니다."));return;}QDesktopServices::openUrl(QUrl::fromLocalFile(directory));});}cl->addLayout(folders);
    connect(refresh_,&QPushButton::clicked,this,&RewritePanel::refreshRecovery);
    connect(filter_,&QLineEdit::textChanged,this,[this](const QString &text){for(int row=0;row<recoveryTable_->rowCount();++row){QString all;for(int col=0;col<5;++col)all+=recoveryTable_->item(row,col)->text()+" ";recoveryTable_->setRowHidden(row,!all.contains(text,Qt::CaseInsensitive));}});
    connect(recoveryTable_,&QTableWidget::itemSelectionChanged,this,[this]{update();const int row=recoveryTable_->currentRow();if(loading_||git_->isBusy()||row<0||row>=entries_.size())return;const auto oid=entries_[row].oid;git_->inspectLimited({"show","--no-ext-diff","--no-textconv","--no-color","--format=fuller","--stat","--patch",oid},[this,oid](bool ok,const QByteArray &out,const QString &error){const int row=recoveryTable_->currentRow();if(row>=0&&row<entries_.size()&&entries_[row].oid==oid)details_->setPlainText(ok?QString::fromUtf8(out):error);});});
    state_=new QLabel;state_->setWordWrap(true);state_->setTextFormat(Qt::PlainText);state_->setObjectName("rewriteState");layout->addWidget(state_);
    auto *operation=new QHBoxLayout;continue_=new QPushButton(tr("Continue"));continue_->setObjectName("rewriteContinue");abort_=new QPushButton(tr("Abort"));abort_->setObjectName("rewriteAbort");edit_=new QPushButton(tr("Edit: 변경 사항에서 Amend"));edit_->setObjectName("editRebaseCommit");auto *conflict=new QPushButton(tr("3-way 충돌 편집기"));operation->addWidget(continue_);operation->addWidget(abort_);operation->addWidget(edit_);operation->addWidget(conflict);layout->addLayout(operation);
    connect(continue_,&QPushButton::clicked,this,[this]{continueOperation(false);});connect(abort_,&QPushButton::clicked,this,[this]{continueOperation(true);});connect(edit_,&QPushButton::clicked,this,&RewritePanel::editCommitRequested);connect(conflict,&QPushButton::clicked,this,&RewritePanel::conflictEditorRequested);
    status_=view();status_->setObjectName("rewriteResult");status_->setMaximumHeight(85);layout->addWidget(status_);
    connect(git,&GitClient::busyChanged,this,&RewritePanel::update);connect(git,&GitClient::operationStateChanged,this,&RewritePanel::update);
    auto *timer=new QTimer(this);timer->setInterval(250);connect(timer,&QTimer::timeout,this,[this]{if(pending_&&!loading_&&!git_->isBusy()){pending_=false;prepare();}});timer->start();update();
}
void RewritePanel::openBase(const QString &base){if(!confirmLeave())return;base_->setText(base);root_->setChecked(false);tabs_->scrollToSection(0);pending_=true;}
bool RewritePanel::confirmLeave(){if(!dirty_)return true;if(!ask(this,tr("편집한 Rebase 계획을 버릴까요? 아직 Git 이력에는 적용되지 않았습니다.")))return false;dirty_=false;return true;}
void RewritePanel::update(){
    if(repository_!=git_->repositoryPath()){repository_=git_->repositoryPath();plan_={};entries_.clear();planTable_->setRowCount(0);recoveryTable_->setRowCount(0);details_->clear();dirty_=false;}
    const auto &state=git_->operationState();const bool ready=!loading_&&!git_->isBusy()&&!repository_.isEmpty();const bool clean=state.known&&state.operation.isEmpty()&&state.conflicts.isEmpty()&&state.locks.isEmpty();
    prepare_->setEnabled(ready&&clean);start_->setEnabled(ready&&clean&&!plan_.entries.isEmpty()&&preparedBase_==base_->text().trimmed()&&preparedRoot_==root_->isChecked());base_->setEnabled(ready&&!root_->isChecked());root_->setEnabled(ready);planTable_->setEnabled(ready);refresh_->setEnabled(ready);recoveryTable_->setEnabled(ready);branch_->setEnabled(ready);
    const int row=planTable_->currentRow();up_->setEnabled(ready&&row>0);down_->setEnabled(ready&&row>=0&&row+1<plan_.entries.size());message_->setEnabled(ready&&row>=0&&row<plan_.entries.size()&&plan_.entries[row].action=="reword");
    const int selected=recoveryTable_->currentRow();const bool valid=selected>=0&&selected<entries_.size();for(auto *button:recoveryButtons_){const auto action=button->property("action").toString();button->setEnabled(ready&&clean&&valid&&!recoveryFingerprint_.isEmpty()&&((action!="reset"&&action!="branch")||!entries_[selected].ref.startsWith("refs/gitcanvas/stash-backups/"))&&(action!="delete-ref"||entries_[selected].backup)&&(action!="stash"||entries_[selected].ref.startsWith("refs/gitcanvas/stash-backups/")));}
    const bool rebasing=state.operation=="rebase"&&state.locks.isEmpty();continue_->setEnabled(ready&&rebasing&&state.conflicts.isEmpty());abort_->setEnabled(ready&&rebasing);edit_->setEnabled(ready&&rebasing&&state.editStop&&state.conflicts.isEmpty());
    state_->setText(state.editStop?tr("Edit에서 일시 정지했습니다. 파일을 수정·Stage하고 Amend한 다음 Continue하세요. 수정하지 않고 Continue할 수도 있습니다."):state.operation.isEmpty()?tr("진행 작업 없음 · 복구는 원격에 자동 반영하지 않습니다."):tr("진행 작업: %1 · 충돌 %2개. 충돌 해결·Stage 후 Continue 또는 Abort하세요.").arg(state.operation).arg(state.conflicts.size()));
}
void RewritePanel::renderPlan(){const QSignalBlocker block(planTable_);planTable_->setRowCount(0);for(int row=0;row<plan_.entries.size();++row){const auto &entry=plan_.entries[row];planTable_->insertRow(row);auto *action=new QComboBox;action->addItems({"pick","reword","edit","squash","fixup","drop"});action->setCurrentText(entry.action);planTable_->setCellWidget(row,0,action);planTable_->setItem(row,1,new QTableWidgetItem(entry.oid.left(12)));planTable_->setItem(row,2,new QTableWidgetItem(entry.subject));connect(action,&QComboBox::currentTextChanged,this,[this,row](const QString &text){plan_.entries[row].action=text;dirty_=true;planTable_->selectRow(row);update();});}const QSignalBlocker messageBlock(message_);message_->clear();update();}
void RewritePanel::prepare(){if(git_->isBusy()||loading_||!confirmLeave())return;loading_=true;update();controller_.plan(base_->text().trimmed(),root_->isChecked(),[this](bool ok,RebasePlan plan,QString error){loading_=false;plan_=ok?plan:RebasePlan{};preparedBase_=base_->text().trimmed();preparedRoot_=root_->isChecked();renderPlan();status_->setPlainText(ok?tr("계획을 읽었습니다. 순서·동작·Reword 메시지를 편집하세요."):error);update();});}
void RewritePanel::refreshRecovery(){if(git_->isBusy()||loading_)return;loading_=true;update();git_->loadOperationState([this](bool ok,const QString &error){if(!ok){loading_=false;status_->setPlainText(error);update();return;}recoveryFingerprint_=git_->operationState().fingerprint;recoveryRepository_=git_->repositoryPath();controller_.recoveryLog(limit_->value(),[this](bool ok,QList<RecoveryEntry> entries,QString error){loading_=false;const QSignalBlocker block(recoveryTable_);entries_=entries;recoveryTable_->setRowCount(0);for(const auto &entry:entries){int row=recoveryTable_->rowCount();recoveryTable_->insertRow(row);const QStringList values{entry.backup?tr("복구 참조"):"HEAD reflog",entry.ref,entry.date,entry.subject,entry.oid};for(int col=0;col<values.size();++col)recoveryTable_->setItem(row,col,new QTableWidgetItem(values[col]));recoveryTable_->setRowHidden(row,!values.join(" ").contains(filter_->text(),Qt::CaseInsensitive));}details_->clear();status_->setPlainText(ok?tr("%1개 기록. Reflog는 만료되거나 GC로 커밋이 제거되면 복구할 수 없습니다.").arg(entries.size())+"\n"+error:error);update();});});}
void RewritePanel::recover(const QString &action){const int row=recoveryTable_->currentRow();if(row<0||row>=entries_.size()||git_->isBusy()||loading_)return;const auto entry=entries_[row];
    QString effect=action=="branch"?tr("선택한 커밋에서 새 브랜치를 만듭니다. 현재 브랜치와 파일은 유지합니다."):action=="reset"?tr("현재 브랜치를 선택한 커밋으로 되돌립니다. 변경이 있으면 차단하며 복구 직전 HEAD를 백업합니다. 공유한 이력과 갈라질 수 있습니다."):action=="stash"?tr("이 보관본을 Stash 목록에 다시 등록합니다. 파일은 변경하지 않습니다."):tr("이 복구 참조를 삭제합니다. 다른 참조가 없으면 이후 GC로 해당 이력을 잃을 수 있습니다. 자동 Undo는 없습니다.");
    if(!ask(this,effect+"\n\n"+entry.ref+"\n"+entry.oid))return;loading_=true;update();controller_.recover(entry,action,branch_->text().trimmed(),recoveryRepository_,recoveryFingerprint_,[this](bool ok,const QByteArray &out,const QString &error){loading_=false;status_->setPlainText(ok?tr("복구 명령 완료\n")+QString::fromUtf8(out):error);recoveryFingerprint_.clear();update();emit repositoryChanged();});
}
void RewritePanel::continueOperation(bool abort){const auto state=git_->operationState();if(!ask(this,abort?tr("Rebase를 Abort합니다. 진행 중 수정이 사라질 수 있습니다. 원래 이력과 계획 백업은 유지됩니다."):tr("현재 해결·수정한 내용을 사용하여 Rebase를 계속합니다. 이후 Edit나 충돌에서 다시 멈출 수 있습니다.")))return;loading_=true;update();git_->recoverOperation(state.fingerprint,abort,[this](bool ok,const QByteArray &,const QString &error){loading_=false;status_->setPlainText(ok?tr("작업 상태를 갱신했습니다."):error);update();emit repositoryChanged();});}
