#include "worktreepanel.h"
#include "operations/operationpanel.h"
#include <QCheckBox>
#include <QFile>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include "ui/flatsections.h"
#include <QTimer>
#include <QVBoxLayout>
#include <QStringDecoder>
#include <QSignalBlocker>

WorktreePanel::WorktreePanel(GitClient *git,QWidget *parent):QWidget(parent),git_(git),controller_(git,this) {
    setObjectName("worktreePanel");
    auto *layout=new QVBoxLayout(this);
    auto *intro=new QLabel(tr("변경을 임시 보관하거나 작업 파일을 정리합니다. 삭제 전에는 대상 확인과 복구용 백업을 수행합니다."));intro->setWordWrap(true);layout->addWidget(intro);
    auto *refresh=new QPushButton(tr("Stash · 파일 목록 새로고침"));refresh->setObjectName("refreshWorktree");layout->addWidget(refresh);
    connect(refresh,&QPushButton::clicked,this,[this]{
        if(ignoreDrafts_.contains(git_->repositoryPath())) {
            QMessageBox confirm(QMessageBox::Warning,tr("무시 규칙 초안"),tr("저장하지 않은 .gitignore 편집을 버리고 디스크에서 다시 읽을까요?"),QMessageBox::Yes|QMessageBox::No,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);
            confirm.setDefaultButton(QMessageBox::No);if(confirm.exec()!=QMessageBox::Yes)return;
            ignoreDrafts_.remove(git_->repositoryPath());
        }
        pending_=true;reload();
    });
    auto *tabs=new FlatSections;layout->addWidget(tabs,1);
    auto addButton=[this](QHBoxLayout *row,const QString &text,const QString &action,const QString &tip) {
        auto *button=new QPushButton(text);button->setObjectName("worktree_"+action);button->setProperty("action",action);button->setToolTip(tip);row->addWidget(button);buttons_.append(button);
        connect(button,&QPushButton::clicked,this,[this,action]{act(action);});
    };
    auto *stash=new QWidget;auto *stashLayout=new QVBoxLayout(stash);tabs->addSection(stash,tr("임시 보관 · Stash"));
    auto *saveRow=new QHBoxLayout;message_=new QLineEdit;message_->setObjectName("stashMessage");message_->setPlaceholderText(tr("보관할 변경의 설명"));saveRow->addWidget(message_,1);
    untracked_=new QCheckBox(tr("새 파일도 포함"));untracked_->setObjectName("stashIncludeUntracked");untracked_->setToolTip(tr("Git이 아직 추적하지 않는 파일도 보관합니다. .gitignore로 제외한 파일은 그대로 둡니다."));saveRow->addWidget(untracked_);
    addButton(saveRow,tr("변경 보관"),"save",tr("현재 Stage 상태와 수정 내용을 보관한 뒤 작업 파일을 마지막 커밋 상태로 돌립니다."));stashLayout->addLayout(saveRow);
    auto *split=new QSplitter;stashes_=new QListWidget;stashes_->setObjectName("stashList");detail_=new QPlainTextEdit;detail_->setObjectName("stashDetail");detail_->setReadOnly(true);detail_->setLineWrapMode(QPlainTextEdit::NoWrap);detail_->setFont(QFont("Consolas",10));split->addWidget(stashes_);split->addWidget(detail_);split->setStretchFactor(1,2);stashLayout->addWidget(split,1);
    index_=new QCheckBox(tr("Apply 시 보관했던 Stage 상태도 복원"));stashLayout->addWidget(index_);
    auto *stashActions=new QHBoxLayout;
    addButton(stashActions,tr("복원 · Apply"),"apply",tr("선택한 변경을 현재 작업 파일에 합칩니다. 보관본은 남깁니다."));
    addButton(stashActions,tr("복원 후 제거 · Pop"),"pop",tr("변경을 복원하고 성공한 경우에만 보관 목록에서 제거합니다. Stage 상태는 복원하지 않습니다."));
    addButton(stashActions,tr("선택 보관본 삭제"),"drop",tr("작업 파일을 건드리지 않고 선택 보관본만 목록에서 제거합니다. 복구 참조를 남깁니다."));
    addButton(stashActions,tr("보관본 전체 삭제"),"clear",tr("모든 Stash를 목록에서 제거합니다. 먼저 각 보관본의 복구 참조를 만듭니다."));stashLayout->addLayout(stashActions);
    auto *branchRow=new QHBoxLayout;destination_=new QLineEdit;destination_->setObjectName("worktreeDestination");destination_->setPlaceholderText(tr("새 브랜치 이름 / 파일 이동 시 새 상대 경로 (아래 정리 탭)"));
    // Separate inputs keep branch creation and file movement unambiguous.
    auto *branchName=new QLineEdit;branchName->setObjectName("stashBranchName");branchName->setPlaceholderText(tr("보관 당시 커밋에서 만들 새 브랜치 이름"));branchRow->addWidget(branchName,1);
    addButton(branchRow,tr("보관본에서 브랜치 생성"),"branch",tr("보관 당시 커밋에서 새 브랜치를 만들고 Stage 상태까지 복원합니다. 성공하면 보관 목록에서 제거합니다."));stashLayout->addLayout(branchRow);
    auto *tree=new QWidget;auto *treeLayout=new QVBoxLayout(tree);tabs->addSection(tree,tr("작업 트리 정리"));
    auto *hint=new QLabel(tr("체크한 파일에만 적용합니다. Clean은 ? 표시의 미추적 일반 파일만 삭제하며, 무시된 파일·폴더·중첩 저장소는 삭제하지 않습니다."));hint->setWordWrap(true);treeLayout->addWidget(hint);
    files_=new QListWidget;files_->setObjectName("cleanupFiles");treeLayout->addWidget(files_,1);
    auto *restoreRow=new QHBoxLayout;
    addButton(restoreRow,tr("작업 파일 변경 폐기"),"restore",tr("작업 파일을 Stage 내용으로 되돌립니다. Stage에 담은 변경은 유지됩니다."));
    addButton(restoreRow,tr("Stage 포함 변경 폐기"),"discard",tr("선택한 추적 파일의 Stage와 작업 파일을 마지막 커밋으로 되돌립니다. 새로 추가한 파일은 삭제되며 이름 변경은 원래 경로로 돌아갑니다."));
    addButton(restoreRow,tr("미추적 파일 삭제 · Clean"),"clean",tr("체크한 미추적 파일 목록을 확인한 뒤 백업하고 삭제합니다. 무시된 파일은 대상에 포함하지 않습니다."));treeLayout->addLayout(restoreRow);
    auto *moveRow=new QHBoxLayout;destination_->setPlaceholderText(tr("이동할 새 상대 경로 (예: src/new-name.cpp)"));moveRow->addWidget(destination_,1);
    addButton(moveRow,tr("이동 / 이름 변경"),"move",tr("체크한 추적 파일 하나를 git mv로 옮겨 Stage에 반영합니다. 기존 파일을 덮어쓰지 않습니다."));
    addButton(moveRow,tr("추적 파일 삭제 · rm"),"remove",tr("체크한 추적 파일을 디스크와 Stage에서 삭제합니다. 저장하지 않은 변경이 있으면 Git이 거부합니다."));treeLayout->addLayout(moveRow);
    auto *ignoreTab=new QWidget;auto *ignoreLayout=new QVBoxLayout(ignoreTab);tabs->addSection(ignoreTab,tr("무시 규칙 · .gitignore"));
    auto *ignoreHint=new QLabel(tr("저장소 최상위 .gitignore를 UTF-8로 편집합니다. 이미 추적하는 파일에는 적용되지 않습니다. 예: build/ 또는 *.log. 저장 후 변경 사항에서 커밋할 수 있습니다."));ignoreHint->setWordWrap(true);ignoreLayout->addWidget(ignoreHint);
    ignore_=new QPlainTextEdit;ignore_->setObjectName("ignoreEditor");ignoreLayout->addWidget(ignore_,1);auto *ignoreRow=new QHBoxLayout;addButton(ignoreRow,tr("무시 규칙 저장"),"ignore",tr("기존 파일을 백업한 뒤 .gitignore를 저장합니다. 전역 Git 설정은 변경하지 않습니다."));ignoreLayout->addLayout(ignoreRow);
    connect(ignore_,&QPlainTextEdit::textChanged,this,[this]{if(!ignoreReview_.repository.isEmpty())ignoreDrafts_[ignoreReview_.repository]={ignore_->toPlainText(),ignoreReview_};});
    result_=new QPlainTextEdit;result_->setObjectName("worktreeResult");result_->setReadOnly(true);result_->setMaximumHeight(95);layout->addWidget(result_);
    layout->addWidget(new ProcessControls(git));
    connect(stashes_,&QListWidget::currentRowChanged,this,[this]{updateActions();details();});
    connect(git,&GitClient::busyChanged,this,[this,refresh](bool busy){refresh->setEnabled(!busy&&!loading_);updateActions();});
    connect(git,&GitClient::operationStateChanged,this,&WorktreePanel::updateActions);
    // Wait for the shared client's entire refresh chain to become idle; never interrupt it.
    auto *timer=new QTimer(this);timer->setInterval(250);connect(timer,&QTimer::timeout,this,[this]{
        if(review_.repository!=git_->repositoryPath()&&!loading_){pending_=true;review_={};files_->clear();stashes_->clear();const QSignalBlocker blocker(ignore_);ignore_->clear();ignoreLoaded_=false;updateActions();}
        if(isVisible()&&pending_&&!git_->isBusy()&&!loading_&&!git_->repositoryPath().isEmpty())reload();
    });timer->start();updateActions();
}
void WorktreePanel::invalidate(){pending_=true;}
void WorktreePanel::reload() {
    if(git_->isBusy()||loading_||git_->repositoryPath().isEmpty())return;
    loading_=true;pending_=false;updateActions();
    controller_.review([this](bool ok,WorktreeReview review,QString error) {
        loading_=false;if(!ok){result_->setPlainText(error);updateActions();return;}review_=review;
        const QSignalBlocker blocker(stashes_);stashes_->clear();detail_->clear();files_->clear();
        for(const auto &line:review.stashes.split('\n')){const auto fields=line.split('\0');if(fields.size()<3)continue;auto *item=new QListWidgetItem(QString::fromUtf8(fields[0])+"  ·  "+QString::fromUtf8(fields[2]),stashes_);item->setData(Qt::UserRole,QString::fromUtf8(fields[1]));item->setToolTip(item->text()+"\n"+item->data(Qt::UserRole).toString());}
        for(const auto &entry:review.files){auto *item=new QListWidgetItem(entry.indexStatus+entry.workTreeStatus+"  "+entry.path,files_);item->setData(Qt::UserRole,entry.path);item->setCheckState(Qt::Unchecked);}
        const QSignalBlocker ignoreBlocker(ignore_);
        QFile file(QDir(review.repository).filePath(".gitignore"));ignoreLoaded_=true;ignoreReview_=review;
        if(file.exists()) {
            if(QFileInfo(file).isSymLink()||!file.open(QIODevice::ReadOnly)||file.size()>1024*1024){ignoreLoaded_=false;result_->setPlainText(tr(".gitignore는 읽을 수 있는 1 MiB 이하 UTF-8 일반 파일만 편집할 수 있습니다."));}
            else {QStringDecoder decoder(QStringDecoder::Utf8);const auto text=decoder(file.readAll());if(decoder.hasError()){ignoreLoaded_=false;result_->setPlainText(tr(".gitignore가 UTF-8이 아니므로 원본 보호를 위해 편집을 차단합니다."));}else ignore_->setPlainText(text);}
        }else ignore_->clear();
        if(ignoreLoaded_&&ignoreDrafts_.contains(review.repository)) {const auto draft=ignoreDrafts_.value(review.repository);ignore_->setPlainText(draft.first);ignoreReview_=draft.second;}
        ignore_->setEnabled(ignoreLoaded_);updateActions();
    });
}
void WorktreePanel::updateActions() {
    const auto &state=git_->operationState();const bool ready=!loading_&&!git_->isBusy()&&!review_.repository.isEmpty()&&review_.repository==git_->repositoryPath();
    for(QWidget *input:QList<QWidget*>{message_,untracked_,index_,destination_,findChild<QLineEdit*>("stashBranchName"),files_,stashes_})input->setEnabled(ready);
    ignore_->setEnabled(ready&&ignoreLoaded_);
    const bool safe=ready&&state.known&&state.operation.isEmpty()&&state.conflicts.isEmpty()&&state.locks.isEmpty();
    for(auto *button:buttons_) {const auto action=button->property("action").toString();const bool selected=QStringList{"apply","pop","drop","branch"}.contains(action);button->setEnabled(safe&&(!selected||stashes_->currentItem())&&(action!="ignore"||ignoreLoaded_)&&(action!="clear"||stashes_->count()>0));}
}
void WorktreePanel::details() {
    if(git_->isBusy()||loading_||!stashes_->currentItem())return;
    const auto oid=stashes_->currentItem()->data(Qt::UserRole).toString();
    git_->inspectLimited({"stash","show","--include-untracked","--stat","--patch","--no-ext-diff","--no-textconv",oid},[this,oid](bool ok,const QByteArray &out,const QString &error){if(stashes_->currentItem()&&stashes_->currentItem()->data(Qt::UserRole).toString()==oid)detail_->setPlainText(ok?QString::fromUtf8(out):error);});
}
void WorktreePanel::act(const QString &requested) {
    if(git_->isBusy()||loading_)return;
    QString action=requested;QStringList paths;
    for(int i=0;i<files_->count();++i)if(files_->item(i)->checkState()==Qt::Checked)paths.append(files_->item(i)->data(Qt::UserRole).toString());
    const auto oid=stashes_->currentItem()?stashes_->currentItem()->data(Qt::UserRole).toString():QString();
    QString value;
    if(action=="save"){value=message_->text();if(untracked_->isChecked())action="save-untracked";}
    if(action=="apply"&&index_->isChecked())action="apply-index";
    if(action=="branch")value=findChild<QLineEdit*>("stashBranchName")->text().trimmed();
    if(action=="move")value=destination_->text().trimmed();
    if(action=="ignore")value=ignore_->toPlainText();
    QString effect;
    if(action=="save"||action=="save-untracked")effect=tr("수정·Stage 변경을 보관하고 작업 파일을 되돌립니다. %1\n설명: %2").arg(action=="save-untracked"?tr("미추적 새 파일도 포함합니다. 무시된 파일은 유지됩니다."):tr("미추적·무시된 파일은 그대로 남깁니다."),value);
    else if(action=="apply"||action=="apply-index"||action=="pop")effect=tr("선택한 보관본을 현재 작업 파일에 복원합니다. 충돌하면 보관본은 남으며 일부 파일은 변경될 수 있습니다.\n%1\n%2").arg(stashes_->currentItem()?stashes_->currentItem()->text():QString(),action=="pop"?tr("성공하면 보관 목록에서 제거합니다."):tr("보관본을 목록에 유지합니다."));
    else if(action=="branch")effect=tr("보관 당시 커밋에서 '%1' 브랜치로 전환하고 Stage까지 복원합니다. 성공하면 보관 목록에서 제거합니다. 실패하면 새 브랜치나 복원된 파일이 남을 수 있습니다.").arg(value);
    else if(action=="clear"||action=="drop")effect=tr("%1\n작업 파일은 유지합니다. 삭제 전 refs/gitcanvas/stash-backups 아래 복구 참조를 남깁니다.").arg(action=="clear"?tr("보관본 %1개를 모두 삭제합니다.").arg(stashes_->count()):tr("선택한 보관본을 삭제합니다: ")+stashes_->currentItem()->text());
    else if(action=="ignore")effect=tr("최상위 .gitignore를 편집한 내용으로 저장합니다. 기존 파일을 복구 폴더에 백업합니다.");
    else {
        if(paths.isEmpty()){result_->setPlainText(tr("처리할 파일을 체크하세요."));return;}
        if(action=="restore")effect=tr("작업 파일을 Stage 내용으로 되돌립니다. Stage 내용은 유지합니다.");
        if(action=="discard")effect=tr("작업 파일과 Stage를 마지막 커밋으로 되돌립니다. 새로 Stage한 파일은 삭제되며 이름 변경은 원래 경로로 돌아갑니다.");
        if(action=="clean")effect=tr("아래 미추적 일반 파일을 디스크에서 삭제합니다. 무시된 파일·폴더·중첩 저장소는 제외합니다.");
        if(action=="remove")effect=tr("아래 파일을 디스크에서 삭제하고 삭제를 Stage합니다. 수정 내용이 있으면 Git이 거부합니다.");
        if(action=="move")effect=tr("아래 파일을 '%1'(으)로 이동하고 Stage합니다. 기존 대상은 덮어쓰지 않습니다.").arg(value);
        effect+=tr("\n실행 전 파일과 인덱스를 복구 폴더에 백업합니다.\n\n")+paths.join('\n');
        if(action=="discard")for(const auto &entry:review_.files)if(paths.contains(entry.path)&&!entry.originalPath.isEmpty())effect+=tr("\n이름 변경 복원: %1 → %2").arg(entry.path,entry.originalPath);
    }
    QMessageBox confirm(QMessageBox::Warning,tr("작업 영향 확인"),effect,QMessageBox::Yes|QMessageBox::No,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);
    if(effect.size()>1500){confirm.setText(effect.left(1000)+tr("\n… 전체 대상은 자세히 보기에서 확인하세요."));confirm.setDetailedText(effect);}
    confirm.setTextFormat(Qt::PlainText);confirm.setDefaultButton(QMessageBox::No);if(confirm.exec()!=QMessageBox::Yes)return;
    loading_=true;updateActions();
    controller_.execute(action=="ignore"?ignoreReview_:review_,action,paths,value,oid,[this,action](bool ok,const QByteArray &out,const QString &error){
        loading_=false;result_->setPlainText(ok?tr("완료\n")+QString::fromUtf8(out):error);
        if(ok&&action=="ignore")ignoreDrafts_.remove(git_->repositoryPath());
        if(ok&&action.startsWith("save"))message_->clear();pending_=true;updateActions();emit repositoryChanged();
    });
}
