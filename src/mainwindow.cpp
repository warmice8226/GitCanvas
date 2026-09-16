#include "mainwindow.h"
#include "ui/actionlogger.h"
#include "ui/windowchrome.h"
#include "ui/apptheme.h"
#include "github/githubpanel.h"
#include <QScrollArea>
#include "history/rewritepanel.h"
#include "tools/gittoolbox.h"
#include <QApplication>
#include "settings/diagnostics.h"
#include "ssh/sshworkspace.h"
#include <QFormLayout>
#include <QJsonDocument>
#include <QDialogButtonBox>
#include <QLocale>
#include "history/historywidget.h"
#include "diff/diffwidget.h"
#include "sync/synccontroller.h"
#include "settings/gitsettingsdialog.h"
#include "settings/repositorydialogs.h"
#include "operations/operationpanel.h"
#include "worktree/worktreepanel.h"
#include "merge/mergepanel.h"
#include <QComboBox>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QMenu>
#include <QTimer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QSyntaxHighlighter>
#include <algorithm>

namespace {
class DiffHighlighter : public QSyntaxHighlighter {
public:
    explicit DiffHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}
protected:
    void highlightBlock(const QString &text) override {
        QTextCharFormat format;
        if (text.startsWith('+')) { format.setForeground(AppTheme::color("#92edc8")); format.setBackground(AppTheme::color("#19332e")); }
        else if (text.startsWith('-')) { format.setForeground(AppTheme::color("#ffabb7")); format.setBackground(AppTheme::color("#36232c")); }
        else if (text.startsWith("@@")) format.setForeground(AppTheme::color("#8cbcff"));
        else return;
        setFormat(0,text.size(),format);
    }
};
constexpr int PathRole = Qt::UserRole + 1;
QLabel *label(const QString &text, const char *name) {
    auto *w = new QLabel(text); w->setObjectName(name); return w;
}
QPushButton *button(const QString &text, const QString &tip) {
    auto *w = new QPushButton(text); w->setToolTip(tip); w->setCursor(Qt::PointingHandCursor); return w;
}
QPlainTextEdit *viewer() {
    auto *w = new QPlainTextEdit; w->setReadOnly(true); w->setLineWrapMode(QPlainTextEdit::NoWrap);
    w->setFont(QFont("Consolas", 10)); return w;
}
}
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), git_(this) {
    sync_ = new SyncController(&git_,this);
    diagnostics_=new Diagnostics(&git_,this);
    refreshTimer_=new QTimer(this);refreshTimer_->setInterval(200);
    connect(refreshTimer_,&QTimer::timeout,this,[this]{
        auto *github=findChild<GithubPanel*>();
        if(git_.isBusy()||(github&&github->busy())||!pendingCreatedRepository_.isEmpty())return;
        refreshTimer_->stop();refreshStatus();
    });
    creationOpenTimer_ = new QTimer(this); creationOpenTimer_->setInterval(100);
    connect(creationOpenTimer_, &QTimer::timeout, this, [this] {
        auto *github = findChild<GithubPanel*>();
        if (git_.isBusy() || (github && github->busy())) return;
        creationOpenTimer_->stop();
        const auto path = pendingCreatedRepository_; pendingCreatedRepository_.clear();
        if (!path.isEmpty()) openRepository(path);
    });
    buildUi();
    new ActionLogger(this,[this](const QString &action,const QString &id){ appendLog(QDateTime::currentDateTime().toString("HH:mm:ss  ")+"[UI] "+action);diagnostics_->recordAction(id); });
    connect(sync_,&SyncController::changed,this,&MainWindow::updateActions);
    connect(sync_,&SyncController::failed,this,&MainWindow::showError);
    connect(sync_,&SyncController::completed,this,&MainWindow::refreshStatus);
    connect(&git_, &GitClient::busyChanged, this, [this](bool) {
        updateActions();
    });
    connect(&git_, &GitClient::commandStarted, this, [this](const QString &command) {
        const QString safe = command.startsWith("git commit") ? "git commit -m <message>" : command;
        appendLog(QDateTime::currentDateTime().toString("HH:mm:ss  ") + safe);
    });
    connect(&git_, &GitClient::operationStateChanged, this, &MainWindow::updateActions);
    connect(&git_,&GitClient::recoveryRequired,this,[this]{
        // Git emits recoveryRequired before the command's completion callback.
        // Let that callback finish before starting another Git process.
        sync_->invalidate();
        if(!refreshTimer_->isActive())refreshTimer_->start();
    });
    loadRepositories();
    updateActions();
    const auto previous = qApp->arguments().contains("--repository") ? QString() : QSettings().value("repositoryPath").toString();
    if (!previous.isEmpty() && QDir(previous).exists()) openRepository(previous);
}
void MainWindow::buildUi() {
    AppTheme::initialize();
    installWindowChrome();
    setWindowTitle("GitCanvas · Desktop Preview");
    resize(1360, 860); setMinimumSize(900, 600);
    qApp->setStyle("Fusion");
    setStyleSheet(R"(
        QWidget { background: #11151d; color: #dce3ef; font-family: 'Segoe UI', 'Malgun Gothic'; font-size: 13px; }
        QMainWindow, QStatusBar { background: #0c1017; }
        QLabel { background: transparent; }
        QWidget#sidebar { background: #0c1017; border-right: 1px solid #273040; }
        QLabel#brand { color: #f6f8fc; font-size: 24px; font-weight: 700; padding: 12px 0; }
        QPushButton#themeToggle { font-size: 21px; font-weight: 700; padding: 10px 0; }
        QLabel#eyebrow { color: #77869b; font-size: 11px; font-weight: 600; padding-top: 12px; }
        QLabel#heading { font-size: 22px; font-weight: 600; }
        QLabel#muted { color: #92a0b5; }
        QLabel#badge { background: #123b37; color: #76e2c4; border-radius: 5px; padding: 6px 10px; }
        QPushButton { background: #202938; border: 1px solid #354257; border-radius: 6px; padding: 8px 13px; }
        QPushButton:hover { background: #2c3c51; border-color: #6c8daf; }
        QPushButton:disabled { color: #5c687b; background: #171d27; border-color: #252d3a; }
        QPushButton#primary { background: #6ae0bd; color: #10251e; border: none; font-weight: 700; }
        QPushButton#primary:disabled { background: #24483e; color: #7b968d; }
        QListWidget, QPlainTextEdit, QLineEdit, QComboBox { background: #151c27; border: 1px solid #2a3445; border-radius: 6px; padding: 6px; }
        QListWidget::item { padding: 8px 4px; }
        QListWidget::item:selected { background: #27463f; color: #a2f5db; }
        QCheckBox::indicator, QListView::indicator { width: 14px; height: 14px; }
        QCheckBox::indicator:unchecked, QListView::indicator:unchecked { border: 1px solid #71839b; border-radius: 3px; background: #192331; }
        QTabWidget::pane { border: none; padding-top: 12px; }
        QTabBar::tab { padding: 11px 20px; color: #91a0b7; border-bottom: 2px solid #273040; }
        QTabBar::tab:selected { color: #81e5c6; border-bottom: 2px solid #6ae0bd; }
        QSplitter::handle { background: #273040; }
        QStatusBar { color: #8d9bb0; }
        QToolTip { color: #e2e8f0; background: #243044; border: 1px solid #53627a; }
    )");
    auto *central = new QWidget; auto *root = new QHBoxLayout(central); root->setContentsMargins(0,0,0,0); root->setSpacing(0);
    auto *sidebar = new QWidget; sidebar->setObjectName("sidebar"); sidebar->setFixedWidth(245);
    auto *side = new QVBoxLayout(sidebar); side->setContentsMargins(20,20,20,20); side->setSpacing(14);
    auto *themeButton = new QPushButton; themeButton->setObjectName("themeToggle");
    auto updateThemeButton = [themeButton] { themeButton->setText(AppTheme::isLight() ? QString::fromUtf8("☀️ GitCanvas") : QString::fromUtf8("🌙 GitCanvas")); };
    updateThemeButton(); themeButton->setToolTip(tr("다크 / 라이트 모드 전환")); side->addWidget(themeButton);
    connect(themeButton, &QPushButton::clicked, this, [updateThemeButton] { AppTheme::setLight(!AppTheme::isLight()); updateThemeButton(); });
    side->addWidget(label("WORKSPACE", "eyebrow"));
    side->addWidget(label(tr("LOCAL  ·  내 컴퓨터"), "badge"));
    openButton_ = button(tr("+  저장소 추가"), tr("기존 Git 저장소 폴더를 목록에 추가하고 엽니다. 추가한 저장소는 다음 실행에도 남아 클릭 한 번으로 열 수 있습니다.")); side->addWidget(openButton_);
    auto *creationRow = new QHBoxLayout;
    initButton_ = button(tr("새 저장소"), tr("새 폴더나 기존 프로젝트 폴더에 파일을 유지하면서 Git 저장소와 첫 브랜치를 만듭니다.")); initButton_->setObjectName("initRepositoryButton");
    cloneButton_ = button(tr("Clone"), tr("원격 저장소를 새 폴더로 복제하고 Workspace에 등록합니다.")); cloneButton_->setObjectName("cloneRepositoryButton");
    creationRow->addWidget(initButton_); creationRow->addWidget(cloneButton_); side->addLayout(creationRow);
    connect(initButton_, &QPushButton::clicked, this, [this] { createRepository(false); });
    connect(cloneButton_, &QPushButton::clicked, this, [this] { createRepository(true); });
    repositoryList_ = new QListWidget; repositoryList_->setObjectName("repositoryList");
    repositoryList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);repositoryList_->setTextElideMode(Qt::ElideMiddle);
    repositoryList_->setToolTip(tr("저장소를 클릭하면 바로 엽니다. 별표는 즐겨찾기이며 항상 목록 위에 표시됩니다."));
    side->addWidget(repositoryList_,1);
    repositoryList_->setDragDropMode(QAbstractItemView::InternalMove); repositoryList_->setDefaultDropAction(Qt::MoveAction);
    repositoryList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(repositoryList_, &QWidget::customContextMenuRequested, this, &MainWindow::repositoryMenu);
    connect(repositoryList_->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        QTimer::singleShot(0, this, [this] {
            QList<SavedRepository> ordered;
            for (int row = 0; row < repositoryList_->count(); ++row) {
                const auto path = repositoryList_->item(row)->data(PathRole).toString();
                for (const auto &repo : repositories_) if (repo.path == path) { ordered.append(repo); break; }
            }
            if (ordered.size() != repositories_.size()) return;
            const auto selected = repositoryList_->currentItem() ? repositoryList_->currentItem()->data(PathRole).toString() : QString();
            repositories_ = ordered;
            std::stable_partition(repositories_.begin(), repositories_.end(), [](const auto &repo) { return repo.favorite; });
            saveRepositories(); renderRepositories(selected);
        });
    });
    favoriteButton_ = button(tr("☆ 즐겨찾기"),tr("선택한 저장소를 즐겨찾기로 지정하거나 해제합니다. 즐겨찾기는 항상 일반 저장소보다 위에 표시됩니다."));
    favoriteButton_->setObjectName("favoriteRepository");side->addWidget(favoriteButton_);
    auto *orderRow=new QHBoxLayout;
    moveUpButton_=button(tr("↑ 위로"),tr("선택한 저장소를 한 칸 위로 옮깁니다. 즐겨찾기끼리, 일반 저장소끼리 순서를 바꿀 수 있습니다."));
    moveDownButton_=button(tr("↓ 아래로"),tr("선택한 저장소를 한 칸 아래로 옮깁니다. 변경한 순서는 다음 실행에도 유지됩니다."));
    moveUpButton_->setObjectName("repositoryUp");moveDownButton_->setObjectName("repositoryDown");
    orderRow->addWidget(moveUpButton_);orderRow->addWidget(moveDownButton_);side->addLayout(orderRow);
    gitSettingsButton_ = button(tr("Git 환경 · 작성자 설정"), tr("Git 실행 파일과 버전을 확인하고 새 커밋에 기록할 이름·이메일을 설정합니다. 저장소를 열기 전에도 전역 설정을 저장할 수 있습니다."));
    gitSettingsButton_->setObjectName("gitSettingsButton");
    side->addWidget(gitSettingsButton_);
    connect(gitSettingsButton_, &QPushButton::clicked, this, &MainWindow::showGitSettings);
    remoteButton_ = button(tr("원격 · 브랜치 연결"), tr("원격 주소와 현재 브랜치가 비교할 upstream을 설정합니다. 최초 Push 대상도 여기서 지정합니다."));
    remoteButton_->setObjectName("remoteSettingsButton"); side->addWidget(remoteButton_);
    connect(remoteButton_, &QPushButton::clicked, this, [this] {
        RemoteDialog dialog(&git_, this); connect(&dialog, &RemoteDialog::configured, sync_, &SyncController::invalidate);
        dialog.exec(); refreshStatus();
    });
    connect(repositoryList_,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){openRepository(item->data(PathRole).toString());});
    connect(repositoryList_,&QListWidget::itemActivated,this,[this](QListWidgetItem *item){openRepository(item->data(PathRole).toString());});
    connect(repositoryList_,&QListWidget::itemSelectionChanged,this,&MainWindow::updateActions);
    connect(favoriteButton_,&QPushButton::clicked,this,[this]{
        int row=repositoryList_->currentRow();if(row<0)return;
        auto path=repositories_[row].path;repositories_[row].favorite=!repositories_[row].favorite;
        std::stable_partition(repositories_.begin(),repositories_.end(),[](const auto &r){return r.favorite;});
        saveRepositories();renderRepositories(path);
    });
    connect(moveUpButton_,&QPushButton::clicked,this,[this]{moveRepository(-1);});
    connect(moveDownButton_,&QPushButton::clicked,this,[this]{moveRepository(1);});
    branches_ = new QComboBox; branches_->setObjectName("branchSelector"); branches_->setMinimumContentsLength(10);branches_->setMaximumWidth(200);
    branches_->setToolTip(tr("브랜치는 서로 다른 작업을 나누어 기록하는 공간입니다. 전환할 브랜치를 선택하세요."));
    renameBranchButton_=button(tr("이름 변경"),tr("현재 브랜치의 이름을 바꿉니다. 원격 브랜치 이름은 바뀌지 않습니다."));
    deleteBranchButton_=button(tr("삭제"),tr("main 또는 master로 자동 이동한 뒤 현재 브랜치를 삭제합니다. 강제 삭제·원격 삭제 여부는 삭제창에서 선택합니다."));
    deleteBranchButton_->setObjectName("deleteBranchButton");
    newBranchButton_ = button(tr("+  새 브랜치"), tr("현재 커밋을 출발점으로 별도의 작업 이력을 만들고 그 브랜치로 이동합니다. 기존 브랜치와 구분하여 새 기능이나 수정을 작업할 때 사용합니다."));
    auto *githubButton=button(tr("GitHub 계정 · 저장소"),tr("GitHub 로그인과 계정 전환, 접근 가능한 저장소 조회 및 Clone을 엽니다."));githubButton->setObjectName("openGithub");side->addWidget(githubButton);
    auto *appSettings=button(tr("앱 설정 · 진단"),tr("언어, 로그 보관과 진단 보고서를 설정합니다."));appSettings->setObjectName("applicationSettings");side->addWidget(appSettings);connect(appSettings,&QPushButton::clicked,this,&MainWindow::showAppSettings);
    auto *sshButton=button(tr("SSH 원격 작업공간"),tr("다른 컴퓨터의 Git과 파일을 별도의 SSH 작업창에서 관리합니다."));sshButton->setObjectName("openSshWorkspace");side->addWidget(sshButton);connect(sshButton,&QPushButton::clicked,this,[this]{SshWorkspace workspace(this);workspace.exec();});
    root->addWidget(sidebar);
    auto *body = new QVBoxLayout; body->setContentsMargins(24,22,24,12); body->setSpacing(16); root->addLayout(body,1);
    auto *top=new QHBoxLayout;top->setSpacing(10);
    auto panel=[](const char *name){auto *w=new QWidget;w->setObjectName(name);w->setStyleSheet("QWidget#"+QString(name)+" { border: 1px solid #354257; border-radius: 7px; background: #151c27; }");w->setFixedHeight(92);return w;};
    auto *repoPanel=panel("repositoryHeaderPanel");repoPanel->setMinimumWidth(160);auto *titles=new QVBoxLayout(repoPanel);titles->setContentsMargins(12,10,12,10);
    auto *repoRow=new QHBoxLayout;
    repositoryLabel_=label(tr("저장소를 추가하세요"),"heading");repositoryLabel_->setTextFormat(Qt::PlainText);repositoryLabel_->setMinimumWidth(0);repositoryLabel_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    branchLabel_=label(tr("브랜치 없음"),"badge");branchLabel_->setTextFormat(Qt::PlainText);branchLabel_->setMaximumWidth(150);
    pathLabel_=label(tr("폴더 경로"),"muted");pathLabel_->setTextFormat(Qt::PlainText);pathLabel_->setStyleSheet("font-size: 11px; color: #92a0b5;");pathLabel_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    repoRow->addWidget(repositoryLabel_,1);repoRow->addWidget(branchLabel_);titles->addLayout(repoRow);titles->addWidget(pathLabel_);
    top->addWidget(repoPanel,1);
    auto *branchPanel=panel("branchControlsPanel");branchPanel->setFixedWidth(300);auto *branchLayout=new QVBoxLayout(branchPanel);branchLayout->setContentsMargins(10,8,10,8);branchLayout->setSpacing(5);
    branches_->setMaximumWidth(280);branches_->setToolTip(tr("브랜치를 선택하면 바로 해당 브랜치로 전환합니다. 현재 브랜치는 왼쪽 폴더명 옆에 표시됩니다."));branchLayout->addWidget(branches_);
    auto *branchActions=new QHBoxLayout;branchActions->setSpacing(4);branchActions->addWidget(newBranchButton_);branchActions->addWidget(renameBranchButton_);branchActions->addWidget(deleteBranchButton_);branchLayout->addLayout(branchActions);top->addWidget(branchPanel);
    auto *syncPanel=panel("syncControlsPanel");syncPanel->setFixedWidth(360);auto *syncRow=new QHBoxLayout(syncPanel);syncRow->setContentsMargins(10,8,10,8);syncRow->setSpacing(8);auto *syncLayout=new QVBoxLayout;syncLayout->setSpacing(5);syncRow->addLayout(syncLayout,1);
    refreshButton_=button(tr("로컬파일 새로고침"),tr("내 컴퓨터의 변경 파일, 브랜치와 커밋 기록을 다시 읽습니다. 원격 서버에서 새 커밋을 내려받지는 않습니다."));syncLayout->addWidget(refreshButton_);
    syncButton_=button(tr("Fetch · 원격 확인"),QString());syncButton_->setObjectName("syncButton");syncLayout->addWidget(syncButton_);top->addWidget(syncPanel);
    auto *stop=button(tr("현재작업 중단"),tr("현재 실행 중인 조회·Fetch·Clone 등 중단 가능한 작업을 멈춥니다. 이미 반영된 변경을 되돌리지는 않습니다. 커밋·병합 같은 쓰기 작업은 강제로 중단하지 않습니다."));stop->setObjectName("stopCurrentOperation");stop->setEnabled(false);syncRow->addWidget(stop);
    connect(stop,&QPushButton::clicked,&git_,&GitClient::cancelActive);
    connect(&git_,&GitClient::activityChanged,stop,[this,stop]{stop->setEnabled(git_.canCancel());});
    connect(syncButton_,&QPushButton::clicked,this,[this]{sync_->execute();});body->addLayout(top);
    creationNoticePanel_ = new QWidget;
    auto *noticeRow = new QHBoxLayout(creationNoticePanel_);
    noticeRow->setContentsMargins(8, 6, 8, 6);
    creationNotice_ = new QLabel; creationNotice_->setObjectName("repositoryCreationNotice");
    creationNotice_->setTextFormat(Qt::PlainText); creationNotice_->setWordWrap(true);
    creationNotice_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *dismissNotice = new QPushButton(tr("닫기")); dismissNotice->setObjectName("dismissCreationNotice");
    noticeRow->addWidget(creationNotice_, 1); noticeRow->addWidget(dismissNotice);
    connect(dismissNotice, &QPushButton::clicked, creationNoticePanel_, &QWidget::hide);
    body->addWidget(creationNoticePanel_); creationNoticePanel_->hide();
    tabs_ = new QTabWidget; tabs_->setObjectName("mainTabs"); body->addWidget(tabs_,1);
    auto *introduction=new QLabel;introduction->setObjectName("featureIntroduction");introduction->setWordWrap(true);
    introduction->setStyleSheet("color: #aab8cd; padding: 8px; background: #18202c; border-radius: 6px;");body->insertWidget(1,introduction);
    const QStringList introductions{
        tr("변경 사항 · 파일을 체크해 Stage하거나 제외하고, 제목과 설명을 작성해 커밋합니다. 오른쪽에서 변경 내용을 비교하고 구간·줄 단위로 Stage할 수 있습니다."),
        tr("커밋 기록 · 그래프로 이력을 탐색하고 메시지 또는 상세 조건으로 검색합니다. 행을 더블 클릭하면 파일별 변경을 비교하며, 우클릭으로 브랜치·태그·이력 작업을 실행합니다."),
        tr("작업 로그 · 클릭한 기능과 이어서 실행된 Git 명령을 시간순으로 확인합니다. [UI]는 사용자 행동이며 자동 새로고침 명령도 함께 기록됩니다."),
        tr("작업 상태 · 복구 · 진행 중인 작업, 충돌과 잠금 상태를 확인하고 Continue·Abort·복구를 실행합니다. 중단 가능한 조회 작업은 상단 현재작업 중단 버튼으로 멈춥니다."),
        tr("Stash · 작업 트리 · 변경을 임시 보관하거나 복원하고, 파일 정리·이동·삭제 및 .gitignore 규칙을 관리합니다. 각 기능은 아래 구역에서 바로 사용합니다."),
        tr("병합 · 충돌 해결 · 다른 브랜치와 비교한 뒤 병합합니다. 충돌 파일을 선택하면 별도 편집창에서 기준 브랜치와 병합 브랜치를 보며 최종 결과를 저장할 수 있습니다."),
        tr("이력 수정 · 복구 · 커밋 순서·메시지를 수정하거나 합치고 제외합니다. Reflog와 복구 참조에서 이전 상태를 확인하고 복원할 수 있습니다."),
        tr("Git Tools · 태그, Worktree, Submodule, Bisect, 패치 등 Git 기능을 검색해 실행합니다. 실행할 명령과 변경 대상을 검토한 뒤 적용합니다."),
        tr("GitHub · 로그인하고 저장소를 가져오거나 Pull Requests에서 PR 생성·리뷰·CI·병합을 진행합니다. 계정과 인증 옵션은 고급 설정에서 변경합니다.")};
    introduction->setText(introductions.first());connect(tabs_,&QTabWidget::currentChanged,introduction,[introduction,introductions](int index){introduction->setText(introductions.value(index));});
    auto *changes = new QWidget; auto *changesLayout = new QVBoxLayout(changes); changesLayout->setContentsMargins(0,0,0,0);
    countsLabel_ = label(tr("변경 사항  ·  저장소를 선택하세요"), "muted"); changesLayout->addWidget(countsLabel_);
    auto *split = new QSplitter; split->setObjectName("changesSplitter");
    auto *files = new QWidget; auto *fileLayout = new QVBoxLayout(files); fileLayout->setContentsMargins(0,0,12,0);
    fileLayout->setAlignment(Qt::AlignTop);
    unstagedList_ = new QListWidget; stagedList_ = new QListWidget;
    unstagedList_->setObjectName("unstagedFiles"); stagedList_->setObjectName("stagedFiles");
    for(auto *list:{unstagedList_,stagedList_}){list->setMinimumHeight(75);list->setMaximumHeight(160);}
    unstagedList_->setSelectionMode(QAbstractItemView::ExtendedSelection); stagedList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    stageButton_ = button(tr("체크한 파일 Stage  ↓"), tr("Stage는 다음 커밋에 담을 변경을 미리 골라두는 작업입니다. 위 목록에서 체크한 파일의 현재 변경을 아래 '커밋할 파일' 목록에 담습니다. 아직 커밋이나 원격 업로드는 하지 않습니다."));
    unstageButton_ = button(tr("체크한 파일 Unstage  ↑"), tr("Unstage는 커밋할 변경을 골라둔 상태를 해제하는 작업입니다. 아래 목록에서 체크한 파일을 커밋 대상에서 빼고 위 목록으로 돌려놓습니다. 직접 수정한 파일 내용은 그대로 유지됩니다."));
    unstagedAll_ = new QCheckBox(tr("작업 디렉터리 · 전체 체크"));
    stagedAll_ = new QCheckBox(tr("커밋할 파일 · 전체 체크"));
    unstagedAll_->setObjectName("unstagedAll"); stagedAll_->setObjectName("stagedAll");
    unstagedAll_->setToolTip(tr("작업 디렉터리는 내 컴퓨터에서 파일을 수정하는 폴더입니다. 이 목록은 아직 커밋 대상으로 담지 않은 변경입니다. 체크하면 Stage할 파일을 한 번에 고를 수 있습니다."));
    stagedAll_->setToolTip(tr("다음 커밋에 기록할 변경을 미리 담아둔 목록입니다. 여기의 체크박스는 Unstage로 뺄 파일을 고릅니다. 커밋 버튼은 체크 여부와 관계없이 이 목록의 모든 변경을 기록합니다."));
    auto listHeader = [this, fileLayout](QListWidget *list, QCheckBox *all, const QString &id) {
        auto *row = new QHBoxLayout;
        auto *fold = new QPushButton(QStringLiteral("▸")); fold->setObjectName(id + "Toggle"); fold->setCheckable(true); fold->setFixedWidth(38); fold->setToolTip(tr("파일 목록 펼치기 / 접기"));
        auto *expand = new QPushButton(QStringLiteral("⤢")); expand->setObjectName(id + "Expand"); expand->setCheckable(true); expand->setFixedWidth(38); expand->setToolTip(tr("파일 목록 크게 보기 / 기본 크기"));
        row->addWidget(fold); row->addWidget(all, 1); row->addWidget(expand); fileLayout->addLayout(row);
        list->hide(); fileLayout->addWidget(list, 1);
        connect(fold, &QPushButton::toggled, list, [list, fold, expand](bool open) { list->setVisible(open); fold->setText(open ? QStringLiteral("▾") : QStringLiteral("▸")); if (!open) expand->setChecked(false); });
        connect(expand, &QPushButton::toggled, list, [list, fold](bool large) { if (large) fold->setChecked(true); list->setMinimumHeight(large ? 320 : 75); list->setMaximumHeight(large ? QWIDGETSIZE_MAX : 160); });
    };
    listHeader(unstagedList_, unstagedAll_, "unstaged");
    auto *stageRow=new QHBoxLayout;stageRow->addWidget(stageButton_);stageAllButton_=button(tr("전체파일 Stage ↓"),tr("체크 여부와 관계없이 작업 디렉터리 목록의 모든 파일 변경을 커밋 대상으로 담습니다."));stageAllButton_->setObjectName("stageAllButton");stageRow->addWidget(stageAllButton_);fileLayout->addLayout(stageRow);
    auto *unstageRow=new QHBoxLayout;unstageRow->addWidget(unstageButton_);unstageAllButton_=button(tr("전체파일 Unstage ↑"),tr("체크 여부와 관계없이 커밋할 파일 목록의 모든 변경을 커밋 대상에서 뺍니다. 파일 내용은 유지됩니다."));unstageAllButton_->setObjectName("unstageAllButton");unstageRow->addWidget(unstageAllButton_);fileLayout->addLayout(unstageRow);
    listHeader(stagedList_, stagedAll_, "staged");
    auto *fileScroll=new QScrollArea;fileScroll->setWidgetResizable(true);fileScroll->setFrameShape(QFrame::NoFrame);fileScroll->setWidget(files);split->addWidget(fileScroll);
    auto *details = new QWidget; auto *detailLayout = new QVBoxLayout(details); detailLayout->setContentsMargins(12,0,0,0);
    diffTitle_ = label(tr("변경 내용  /  파일을 선택하세요"),"muted"); diffTitle_->setTextFormat(Qt::PlainText); detailLayout->addWidget(diffTitle_);
    diffPanel_=new DiffWidget(&git_);diff_=diffPanel_->unified();diff_->setObjectName("diffViewer");detailLayout->addWidget(diffPanel_,1);
    connect(diffPanel_,&DiffWidget::indexChanged,this,&MainWindow::refreshStatus);
    fileLayout->addWidget(label(tr("새 커밋"),"eyebrow"));
    fileLayout->addWidget(label(tr("제목 · 필수"),"muted"));
    commitTitle_ = new QLineEdit; commitTitle_->setObjectName("commitTitle");
    commitTitle_->setPlaceholderText(tr("예: 로그인 화면의 버튼 위치 수정"));
    commitTitle_->setToolTip(tr("이번 변경을 한 줄로 요약하세요. 커밋 기록 목록에 표시되는 제목입니다.")); fileLayout->addWidget(commitTitle_);
    fileLayout->addWidget(label(tr("상세 설명 · 선택"),"muted"));
    commitMessage_ = new QPlainTextEdit; commitMessage_->setPlaceholderText(tr("무엇을 왜 바꿨는지 자세히 적어주세요.")); commitMessage_->setMaximumHeight(85); fileLayout->addWidget(commitMessage_);
    commitMessage_->setToolTip(tr("변경 이유와 구체적인 내용을 여러 줄로 작성할 수 있습니다. 비워두면 제목만 기록됩니다."));
    commitMessage_->setObjectName("commitMessage");
    auto *commitOptions=new QHBoxLayout;
    amend_=new QCheckBox(tr("마지막 커밋 수정 · Amend"));amend_->setObjectName("amendCommit");
    amend_->setToolTip(tr("마지막 커밋의 메시지와 파일을 교체합니다. 커밋 ID가 바뀌므로 이미 공유한 커밋은 주의하세요."));
    signOff_=new QCheckBox(tr("Signed-off-by 추가"));signOff_->setObjectName("signOffCommit");
    signOff_->setToolTip(tr("커밋 메시지 끝에 현재 커미터의 이름·이메일로 Signed-off-by를 추가합니다. 암호학적 서명은 아닙니다."));
    commitOptions->addWidget(amend_);commitOptions->addWidget(signOff_);fileLayout->addLayout(commitOptions);
    connect(amend_,&QCheckBox::toggled,this,[this](bool enabled){
        amendHead_.clear();updateActions();if(!enabled||git_.isBusy())return;
        git_.inspect({"log","-1","--format=%H%x00%B"},[this](bool ok,const QByteArray &out,const QString &error){
            if(!ok){amend_->setChecked(false);showError(error);return;}
            const auto split=out.indexOf('\0');amendHead_=QString::fromUtf8(out.left(split));
            if(commitTitle_->text().isEmpty()&&commitMessage_->toPlainText().isEmpty()){
                const auto message=QString::fromUtf8(out.mid(split+1)).trimmed();commitTitle_->setText(message.section('\n',0,0));commitMessage_->setPlainText(message.section('\n',1).trimmed());
            }updateActions();
        });
    });
    commitButton_ = button(tr("커밋 만들기"),tr("커밋은 변경 내용을 나중에 확인하거나 되돌아볼 수 있도록 기록하는 작업입니다. '커밋할 파일' 목록의 모든 변경을 제목·상세 설명과 함께 현재 브랜치에 기록합니다. 체크박스는 Unstage 대상 선택용이며, 원격에 공유하려면 이후 Push를 사용하세요.")); commitButton_->setObjectName("primary"); fileLayout->addWidget(commitButton_);
    split->addWidget(details); split->setSizes({350,700}); changesLayout->addWidget(split,1);
    tabs_->addTab(changes,tr("변경 사항"));
    history_ = new HistoryWidget(&git_); tabs_->addTab(history_,tr("커밋 기록"));
    connect(history_,&HistoryWidget::repositoryChanged,this,&MainWindow::repositoryChanged);
    log_ = viewer(); log_->setObjectName("actionLog");log_->setMaximumBlockCount(2000); tabs_->addTab(log_,tr("작업 로그"));
    auto *operations=new OperationPanel(&git_);tabs_->addTab(operations,tr("작업 상태 · 복구"));
    worktreePanel_=new WorktreePanel(&git_);tabs_->addTab(worktreePanel_,tr("Stash · 작업 트리"));
    connect(worktreePanel_,&WorktreePanel::repositoryChanged,this,&MainWindow::repositoryChanged);
    auto *mergePanel=new MergePanel(&git_);tabs_->addTab(mergePanel,tr("병합 · 충돌 해결"));
    auto *rewrite=new RewritePanel(&git_);tabs_->addTab(rewrite,tr("이력 수정 · 복구"));
    auto *toolbox=new GitToolbox(&git_);tabs_->addTab(toolbox,tr("Git Tools"));
    auto *github=new GithubPanel(&git_);tabs_->addTab(github,tr("GitHub"));
    connect(githubButton,&QPushButton::clicked,this,[this,github]{tabs_->setCurrentWidget(github);});
    connect(github,&GithubPanel::busyChanged,this,[this,github](bool busy){for(int i=0;i<tabs_->count();++i)if(tabs_->widget(i)!=github)tabs_->setTabEnabled(i,!busy);updateActions();});
    connect(github,&GithubPanel::repositoryCreated,this,&MainWindow::openRepository);
    connect(github,&GithubPanel::repositoryCloned,this,&MainWindow::addCreatedRepository);
    connect(github,&GithubPanel::repositoryChanged,this,&MainWindow::repositoryChanged);
    connect(github,&GithubPanel::commandStarted,this,[this](const QString &command){appendLog(QDateTime::currentDateTime().toString("HH:mm:ss  ")+command);});
    auto *toolShortcut=new QShortcut(QKeySequence("Ctrl+Shift+P"),this);connect(toolShortcut,&QShortcut::activated,this,[this,toolbox]{tabs_->setCurrentWidget(toolbox);toolbox->findChild<QLineEdit*>("gitToolSearch")->setFocus();});
    connect(toolbox,&GitToolbox::repositoryChanged,this,&MainWindow::repositoryChanged);
    connect(toolbox,&GitToolbox::openWorktree,this,&MainWindow::openRepository);
    connect(rewrite,&RewritePanel::repositoryChanged,this,&MainWindow::repositoryChanged);
    connect(rewrite,&RewritePanel::conflictEditorRequested,this,[this,mergePanel]{tabs_->setCurrentWidget(mergePanel);mergePanel->openConflicts();});
    connect(rewrite,&RewritePanel::editCommitRequested,this,[this]{tabs_->setCurrentIndex(0);amend_->setChecked(false);amend_->setChecked(true);});
    connect(history_,&HistoryWidget::rebaseRequested,this,[this,rewrite](const QString &base){tabs_->setCurrentWidget(rewrite);rewrite->openBase(base);});
    connect(mergePanel,&MergePanel::repositoryChanged,this,&MainWindow::repositoryChanged);
    connect(sync_,&SyncController::mergeRequested,this,[this,mergePanel](const QString &ref){tabs_->setCurrentWidget(mergePanel);mergePanel->openTarget(ref);});
    auto *resolveButton=new QPushButton(tr("3-way 충돌 편집기 열기"));resolveButton->setObjectName("openConflictEditor");operations->layout()->addWidget(resolveButton);connect(resolveButton,&QPushButton::clicked,this,[this,mergePanel]{tabs_->setCurrentWidget(mergePanel);mergePanel->openConflicts();});
    connect(operations,&OperationPanel::refreshRequested,this,&MainWindow::repositoryChanged);
    setCentralWidget(central);
    connect(openButton_, &QPushButton::clicked,this,[this] {
        QFileDialog dialog(this,tr("저장소 추가"),validRepository_);dialog.setOption(QFileDialog::DontUseNativeDialog);dialog.setFileMode(QFileDialog::Directory);dialog.setOption(QFileDialog::ShowDirsOnly);dialog.setWindowFlag(Qt::WindowTitleHint);
        if(dialog.exec()==QDialog::Accepted&&!dialog.selectedFiles().isEmpty())openRepository(dialog.selectedFiles().first());
    });
    connect(refreshButton_, &QPushButton::clicked,this,&MainWindow::refreshStatus);
    connect(stageAllButton_,&QPushButton::clicked,this,[this]{stageAll(false);});
    connect(unstageAllButton_,&QPushButton::clicked,this,[this]{stageAll(true);});
    connect(stageButton_, &QPushButton::clicked,this,[this]{ stageSelected(false); });
    connect(unstageButton_, &QPushButton::clicked,this,[this]{ stageSelected(true); });
    connect(commitButton_, &QPushButton::clicked,this,&MainWindow::commitChanges);
    connect(commitMessage_, &QPlainTextEdit::textChanged,this,&MainWindow::updateActions);
    connect(commitTitle_, &QLineEdit::textChanged,this,&MainWindow::updateActions);
    connect(commitTitle_, &QLineEdit::textChanged, this, &MainWindow::saveDrafts);
    connect(commitMessage_, &QPlainTextEdit::textChanged, this, &MainWindow::saveDrafts);
    for (auto pair : {qMakePair(unstagedAll_,unstagedList_), qMakePair(stagedAll_,stagedList_)}) {
        connect(pair.first, &QCheckBox::clicked, this, [this,pair](bool checked) {
            const QSignalBlocker blocker(pair.second);
            for(int i=0;i<pair.second->count();++i) pair.second->item(i)->setCheckState(checked?Qt::Checked:Qt::Unchecked);
            updateActions();
        });
        connect(pair.second, &QListWidget::itemChanged, this, [this]{updateActions();});
    }
    connect(unstagedList_, &QListWidget::itemSelectionChanged,this,[this]{updateActions(); loadDiff(false);});
    connect(stagedList_, &QListWidget::itemSelectionChanged,this,[this]{updateActions(); loadDiff(true);});
    connect(branches_,&QComboBox::activated,this,[this](int index){
        if(index<0||git_.isBusy()||branches_->itemText(index)==branchLabel_->text())return;
        appendLog(QDateTime::currentDateTime().toString("HH:mm:ss  ")+tr("[UI] 브랜치 %1 선택").arg(branches_->itemText(index)));
        git_.switchBranch(branches_->itemText(index),false,[this](bool ok,const QByteArray &,const QString &error){if(!ok)showError(error);repositoryChanged();});
    });
    connect(newBranchButton_,&QPushButton::clicked,this,[this]{
        QInputDialog dialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setLabelText(tr("새 브랜치\n현재 브랜치 %1의 마지막 커밋에서 분리합니다.\n새 이름:").arg(branchLabel_->text()));dialog.setOkButtonText(tr("만들기"));dialog.setCancelButtonText(tr("취소"));
        if(dialog.exec()!=QDialog::Accepted||dialog.textValue().trimmed().isEmpty())return;
        git_.switchBranch(dialog.textValue().trimmed(),true,[this](bool ok,const QByteArray &,const QString &error){if(!ok)showError(error);repositoryChanged();});
    });
    connect(renameBranchButton_,&QPushButton::clicked,this,[this]{
        QInputDialog dialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setLabelText(tr("현재 브랜치 이름 변경\n새 이름:"));dialog.setTextValue(branches_->currentText());dialog.setOkButtonText(tr("변경"));dialog.setCancelButtonText(tr("취소"));
        if(dialog.exec()!=QDialog::Accepted||dialog.textValue().trimmed().isEmpty())return;
        git_.renameBranch(branches_->currentText(),dialog.textValue().trimmed(),[this](bool ok,const QByteArray &,const QString &error){if(!ok)showError(error);repositoryChanged();});
    });
    connect(deleteBranchButton_,&QPushButton::clicked,this,&MainWindow::deleteCurrentBranch);
}
void MainWindow::updateActions() {
    auto *github=findChild<GithubPanel*>();
    bool idle = !git_.isBusy()&&(!github||!github->busy()), ready = idle && !validRepository_.isEmpty();
    const auto &state=git_.operationState();
    const bool blocked=state.repository==validRepository_&&state.known&&(!state.operation.isEmpty()||!state.conflicts.isEmpty()||!state.locks.isEmpty());
    repositoryList_->setEnabled(idle);
    int row=repositoryList_->currentRow();bool selected=row>=0 && row<repositories_.size();
    favoriteButton_->setEnabled(idle && selected);
    favoriteButton_->setText(selected && repositories_[row].favorite?tr("★ 즐겨찾기 해제"):tr("☆ 즐겨찾기"));
    moveUpButton_->setEnabled(idle && selected && row>0 && repositories_[row].favorite==repositories_[row-1].favorite);
    moveDownButton_->setEnabled(idle && selected && row+1<repositories_.size() && repositories_[row].favorite==repositories_[row+1].favorite);
    openButton_->setEnabled(idle); refreshButton_->setEnabled(ready);
    gitSettingsButton_->setEnabled(idle);
    // Opening the form is safe while Git is busy; the form gates execution.
    const bool canOpenCreation = !github || !github->busy();
    initButton_->setEnabled(canOpenCreation); cloneButton_->setEnabled(canOpenCreation); remoteButton_->setEnabled(ready);
    stageAllButton_->setEnabled(ready && unstagedList_->count()>0);unstageAllButton_->setEnabled(ready && stagedList_->count()>0);
    stageButton_->setEnabled(ready && !selectedPaths(unstagedList_).isEmpty());
    unstageButton_->setEnabled(ready && !selectedPaths(stagedList_).isEmpty());
    for (auto pair : {qMakePair(unstagedAll_,unstagedList_), qMakePair(stagedAll_,stagedList_)}) {
        const auto count = selectedPaths(pair.second).count();
        const QSignalBlocker blocker(pair.first);
        pair.first->setCheckState(count == 0 ? Qt::Unchecked : count == pair.second->count() ? Qt::Checked : Qt::PartiallyChecked);
        pair.first->setEnabled(ready && pair.second->count()>0);
    }
    const bool editStop=state.known&&state.operation=="rebase"&&state.editStop&&state.conflicts.isEmpty()&&state.locks.isEmpty();
    amend_->setEnabled(ready&&(!blocked||editStop));signOff_->setEnabled(ready&&(!blocked||editStop));
    commitButton_->setText(amend_->isChecked()?tr("마지막 커밋 수정"):tr("커밋 만들기"));
    commitButton_->setEnabled(ready && (!blocked||(editStop&&amend_->isChecked())) && (amend_->isChecked()?!amendHead_.isEmpty():stagedList_->count()>0) && !commitTitle_->text().trimmed().isEmpty());
    branches_->setEnabled(ready&&!blocked);newBranchButton_->setEnabled(ready && !blocked && branches_->currentIndex()>=0);renameBranchButton_->setEnabled(ready && !blocked && branches_->currentIndex()>=0);deleteBranchButton_->setEnabled(ready && !blocked && branches_->currentIndex()>=0 && branches_->count()>1);
    syncButton_->setText(sync_->label());syncButton_->setToolTip(sync_->explanation());syncButton_->setEnabled(ready && !sync_->working());
    if(blocked) {syncButton_->setEnabled(false);syncButton_->setToolTip(tr("진행 중인 작업·충돌·잠금을 작업 상태 · 복구 탭에서 확인하세요."));}
    if(tabs_->count()>3)tabs_->setTabText(3,blocked?tr("작업 상태 · 복구 (!)"):tr("작업 상태 · 복구"));
    unstagedList_->setEnabled(idle); stagedList_->setEnabled(idle);
}
void MainWindow::openRepository(const QString &path) {
    if(auto *github=findChild<GithubPanel*>();github&&github->busy())return;
    if (git_.isBusy()) return;
    if(path!=validRepository_&&(!findChild<MergePanel*>()->confirmLeave()||!findChild<RewritePanel*>()->confirmLeave())){
        renderRepositories(validRepository_);
        if (path == creationNoticePath_)
            showCreationNotice(tr("저장소 생성과 Workspace 추가는 완료했습니다. 화면 전환을 취소했으므로 목록에서 직접 열 수 있습니다.\n%1").arg(path));
        return;
    }
    git_.setRepositoryPath(path);
    git_.checkRepository([this, path](bool ok,const QByteArray &out,const QString &error){
        if(!ok) {
            if (path == creationNoticePath_)
                showCreationNotice(tr("저장소 생성과 Workspace 추가는 완료했지만 자동으로 열지 못했습니다. 목록에서 다시 열어주세요.\n%1\n%2").arg(path, error));
            git_.setRepositoryPath(validRepository_); renderRepositories(validRepository_); showError(error); updateActions(); return;
        }
        sync_->invalidate();
        if(!validRepository_.isEmpty())commitDrafts_.insert(validRepository_,{commitTitle_->text(),commitMessage_->toPlainText()});
        validRepository_ = QString::fromUtf8(out).trimmed(); git_.setRepositoryPath(validRepository_);
        amend_->setChecked(false);signOff_->setChecked(false);
        history_->setVisible(tabs_->currentWidget()==history_);
        const auto canonical=QFileInfo(validRepository_).canonicalFilePath();
        if(!canonical.isEmpty()){validRepository_=canonical;git_.setRepositoryPath(validRepository_);}
        auto found=std::find_if(repositories_.begin(),repositories_.end(),[this](const auto &r){
#ifdef Q_OS_WIN
            return r.path.compare(validRepository_,Qt::CaseInsensitive)==0;
#else
            return r.path==validRepository_;
#endif
        });
        if(found==repositories_.end())repositories_.append({validRepository_,false});
        saveRepositories();renderRepositories(validRepository_);
        repositoryLabel_->setText(QDir(validRepository_).dirName()); repositoryLabel_->setToolTip(validRepository_);pathLabel_->setText(QDir::toNativeSeparators(validRepository_));pathLabel_->setToolTip(validRepository_);
        QSettings().setValue("repositoryPath",validRepository_);
        if (path == creationNoticePath_) {
            showCreationNotice(tr("저장소 생성이 완료되었습니다. Workspace에 추가하고 열었습니다.\n%1").arg(validRepository_));
            creationNoticePath_.clear();
        }
        const auto draft=commitDrafts_.value(validRepository_);commitTitle_->setText(draft.value(0));commitMessage_->setPlainText(draft.value(1));refreshStatus();
    });
}
void MainWindow::repositoryChanged() {
    sync_->invalidate();
    refreshStatus();
}
void MainWindow::refreshStatus() {
    if(validRepository_.isEmpty())return;
    auto *github=findChild<GithubPanel*>();
    if(git_.isBusy()||(github&&github->busy())){if(!refreshTimer_->isActive())refreshTimer_->start();return;}
    refreshTimer_->stop();
    worktreePanel_->invalidate();
    git_.loadStatus([this](bool ok,const QList<GitStatusEntry> &entries,const QString &error){
        if(!ok) {showError(error);return;}
        const QSignalBlocker a(unstagedList_), b(stagedList_);
        unstagedList_->clear(); stagedList_->clear(); diffPanel_->clear();
        diffTitle_->setText(tr("변경 내용  /  파일을 선택하세요"));
        for(const auto &entry:entries) {
            auto add = [&entry](QListWidget *list,const QString &status){auto *item = new QListWidgetItem(status+"   "+entry.path,list);item->setData(PathRole,entry.path);item->setCheckState(Qt::Unchecked);item->setToolTip(entry.path + tr("\n파일명을 클릭하면 변경 내용을 봅니다. 체크박스로 Stage / Unstage할 파일을 고르세요."));};
            if(entry.isUnstaged()) add(unstagedList_,entry.workTreeStatus == " " ? entry.indexStatus : entry.workTreeStatus);
            if(entry.isStaged()) add(stagedList_,entry.indexStatus);
        }
        countsLabel_->setText(tr("작업 디렉터리 %1개   ·   Staged %2개").arg(unstagedList_->count()).arg(stagedList_->count()));
        updateActions(); refreshMetadata();
    });
}
void MainWindow::refreshMetadata() {
    git_.inspect({"symbolic-ref","--quiet","--short","HEAD"},[this](bool ok,const QByteArray &out,const QString &){
        branchLabel_->setText(ok ? QString::fromUtf8(out).trimmed() : tr("detached HEAD"));
        git_.inspect({"branch","--format=%(refname:short)"},[this](bool ok,const QByteArray &out,const QString &error){
            if(!ok){showError(error);return;} branches_->clear(); branches_->addItems(QString::fromUtf8(out).trimmed().split('\n',Qt::SkipEmptyParts));
            auto current = branchLabel_->text().section("...",0,0).section(" [",0,0); branches_->setCurrentIndex(branches_->findText(current));branchLabel_->setText(current);branchLabel_->setToolTip(current);
            sync_->review([this]{git_.loadOperationState([this](bool,const QString &){history_->reload();});});
        });
    });
}
QStringList MainWindow::selectedPaths(QListWidget *list) const {
    QStringList paths; for(int i=0;i<list->count();++i) { auto *item=list->item(i); if(item->checkState()==Qt::Checked) paths.append(item->data(PathRole).toString()); } return paths;
}
void MainWindow::loadDiff(bool staged) {
    if(git_.isBusy())return;
    auto *list = staged ? stagedList_ : unstagedList_; auto *item = list->currentItem(); if(!item)return;
    const auto path = item->data(PathRole).toString(); diffTitle_->setText((staged ? tr("Staged  /  ") : tr("작업 디렉터리  /  "))+path);
    diffPanel_->loadWorking(path,staged);
}
void MainWindow::stageSelected(bool unstage) {
    const auto paths=selectedPaths(unstage?stagedList_:unstagedList_); if(paths.isEmpty())return;
    auto done=[this](bool ok,const QByteArray &,const QString &error){if(!ok)showError(error);refreshStatus();};
    if(unstage)git_.unstage(paths,done);else git_.stage(paths,done);
}
void MainWindow::commitChanges() {
    const auto title=commitTitle_->text().trimmed(); if(title.isEmpty())return;
    const auto body=commitMessage_->toPlainText().trimmed();
    const auto message=title+(body.isEmpty()?QString():"\n\n"+body);
    const bool amend=amend_->isChecked(),signOff=signOff_->isChecked();const auto expectedHead=amendHead_;
    if(amend){QMessageBox confirm(QMessageBox::Warning,tr("마지막 커밋 수정"),tr("%1 커밋을 현재 Stage 전체와 입력한 메시지로 교체합니다. 원래 작성자는 유지되고 커밋 ID는 바뀝니다. 이미 공유한 커밋이면 다른 사람의 이력과 갈라질 수 있습니다. 실행 전 복구 참조를 남깁니다.").arg(expectedHead.left(12)),QMessageBox::Yes|QMessageBox::No,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);confirm.setDefaultButton(QMessageBox::No);if(confirm.exec()!=QMessageBox::Yes)return;}
    git_.inspect({"var", "GIT_AUTHOR_IDENT"}, [this, message,amend,signOff,expectedHead](bool ok, const QByteArray &, const QString &) {
        if (!ok) { showGitSettings(); return; }
        git_.inspect({"var", "GIT_COMMITTER_IDENT"}, [this, message,amend,signOff,expectedHead](bool ok, const QByteArray &, const QString &) {
            if (!ok) { showGitSettings(); return; }
            git_.commitWithOptions(message,amend,signOff,expectedHead,[this](bool ok,const QByteArray &,const QString &error){if(ok){amend_->setChecked(false);commitTitle_->clear();commitMessage_->clear();}else showError(error);repositoryChanged();});
        });
    });
}
void MainWindow::showGitSettings() {
    if (git_.isBusy()) return;
    GitSettingsDialog dialog(&git_, this);
    dialog.exec();
    updateActions();
}
void MainWindow::createRepository(bool clone) {
    RepositoryDialog dialog(&git_, clone, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.createdPath().isEmpty()) addCreatedRepository(dialog.createdPath());
}
void MainWindow::showCreationNotice(const QString &message) {
    creationNotice_->setText(message); creationNoticePanel_->show();
    appendLog(message);
}
void MainWindow::addCreatedRepository(const QString &path) {
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const auto destination = canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
    const auto found = std::find_if(repositories_.cbegin(), repositories_.cend(), [&](const auto &repo) {
#ifdef Q_OS_WIN
        return repo.path.compare(destination, Qt::CaseInsensitive) == 0;
#else
        return repo.path == destination;
#endif
    });
    if (found == repositories_.cend()) repositories_.append({destination, false});
    // Persist completion independently of switching repositories or refreshing Git.
    saveRepositories(); renderRepositories(validRepository_);
    creationNoticePath_ = destination;
    showCreationNotice(tr("저장소 생성이 완료되어 Workspace에 추가했습니다. 진행 중인 작업이 끝나면 자동으로 엽니다.\n%1").arg(destination));
    pendingCreatedRepository_ = destination;
    creationOpenTimer_->start();
}
void MainWindow::saveDrafts() {
    if (!validRepository_.isEmpty()) commitDrafts_.insert(validRepository_, {commitTitle_->text(), commitMessage_->toPlainText()});
    QVariantMap values;
    for (auto it = commitDrafts_.cbegin(); it != commitDrafts_.cend(); ++it) values.insert(it.key(), it.value());
    QSettings().setValue("workspace/commitDrafts", values);
}
void MainWindow::repositoryMenu(const QPoint &point) {
    if (git_.isBusy()) return;
    auto *item = repositoryList_->itemAt(point); if (!item) return;
    const int row = repositoryList_->row(item); const auto repo = repositories_[row];
    QMenu menu(this);
    auto *rename = menu.addAction(tr("표시 이름 변경")); rename->setObjectName("renameRepositoryEntry");
    auto *relocate = menu.addAction(tr("저장소 경로 다시 지정")); relocate->setObjectName("relocateRepositoryEntry");
    auto *remove = menu.addAction(tr("목록에서 제거")); remove->setObjectName("removeRepositoryEntry");
    const auto chosen = menu.exec(repositoryList_->viewport()->mapToGlobal(point));
    if (chosen == rename) {
        QInputDialog dialog(this, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint); dialog.setLabelText(tr("Workspace 표시 이름 (비우면 실제 폴더명)")); dialog.setTextValue(repo.displayName);
        if (dialog.exec() == QDialog::Accepted) { repositories_[row].displayName = dialog.textValue().trimmed(); saveRepositories(); renderRepositories(repo.path); }
    } else if (chosen == relocate) {
        QFileDialog dialog(this, tr("옮겨진 저장소 폴더"), repo.path); dialog.setFileMode(QFileDialog::Directory); dialog.setOption(QFileDialog::DontUseNativeDialog); dialog.setWindowFlag(Qt::WindowTitleHint);
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) relocateRepository(row, dialog.selectedFiles().first());
    } else if (chosen == remove) {
        QMessageBox confirm(QMessageBox::Question, tr("Workspace 목록에서 제거"), tr("이 저장소의 목록 등록과 저장된 커밋 초안을 제거합니다. 실제 폴더, 파일과 Git 기록은 삭제하지 않습니다."), QMessageBox::Yes | QMessageBox::No, this, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        confirm.setDefaultButton(QMessageBox::No); if (confirm.exec() != QMessageBox::Yes) return;
        repositories_.removeAt(row); commitDrafts_.remove(repo.path);
        if (validRepository_ == repo.path) {
            validRepository_.clear(); git_.setRepositoryPath({}); sync_->invalidate(); QSettings().remove("repositoryPath");
            commitTitle_->clear(); commitMessage_->clear(); unstagedList_->clear(); stagedList_->clear(); branches_->clear();
            diffPanel_->clear(); history_->hide(); tabs_->setCurrentIndex(0);
            repositoryLabel_->setText(tr("저장소를 추가하세요")); branchLabel_->setText(tr("브랜치 없음")); pathLabel_->setText(tr("폴더 경로"));
            countsLabel_->setText(tr("변경 사항 · 저장소를 선택하세요"));
        }
        saveRepositories(); saveDrafts(); renderRepositories(validRepository_);
        if (validRepository_.isEmpty() && !repositories_.isEmpty()) openRepository(repositories_.first().path);
    }
}
void MainWindow::relocateRepository(int row, const QString &path) {
    if (git_.isBusy() || row < 0 || row >= repositories_.size()) return;
    const auto oldPath = repositories_[row].path, previous = git_.repositoryPath();
    git_.setRepositoryPath(path);
    git_.checkRepository([this, row, oldPath, previous](bool ok, const QByteArray &out, const QString &error) {
        git_.setRepositoryPath(previous);
        if (!ok) { showError(error); return; }
        const auto root = QFileInfo(QString::fromUtf8(out).trimmed()).canonicalFilePath();
        if (root.isEmpty()) { showError(tr("저장소 경로를 확인할 수 없습니다.")); return; }
        for (int i = 0; i < repositories_.size(); ++i) if (i != row && repositories_[i].path.compare(root,
#ifdef Q_OS_WIN
            Qt::CaseInsensitive
#else
            Qt::CaseSensitive
#endif
        ) == 0) { showError(tr("이미 Workspace에 등록된 저장소입니다.")); return; }
        saveDrafts(); repositories_[row].path = root;
        if (commitDrafts_.contains(oldPath)) { const auto draft = commitDrafts_.take(oldPath); commitDrafts_.insert(root, draft); }
        const bool active = validRepository_ == oldPath;
        if (active) validRepository_.clear();
        saveRepositories(); saveDrafts(); renderRepositories(root);
        if (active) openRepository(root);
    });
}
void MainWindow::showError(const QString &message) {
    QMessageBox dialog(QMessageBox::Warning,tr("Git 작업 오류"),message.isEmpty()?tr("작업 로그와 저장소 상태를 확인하세요."):message,QMessageBox::Ok,this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setTextFormat(Qt::PlainText);dialog.exec();
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if(auto *ssh=findChild<SshWorkspace*>();ssh&&ssh->isVisible()){event->ignore();ssh->raise();return;}
    if(auto *github=findChild<GithubPanel*>();github&&github->busy()){event->ignore();tabs_->setCurrentWidget(github);return;}
    if (git_.isBusy()) {
        event->ignore();
        appendLog(tr("Git 작업이 끝난 뒤 창을 닫아주세요. 중단 가능한 작업은 상단의 현재작업 중단 버튼으로 멈출 수 있습니다."));
        tabs_->setCurrentWidget(log_);
    } else if((!findChild<MergePanel*>()->confirmLeave()||!findChild<RewritePanel*>()->confirmLeave())) {event->ignore();}
    else { saveDrafts(); event->accept(); }
}

void MainWindow::loadRepositories() {
    QSettings settings;
    const auto saved=settings.value("workspace/repositories").toList();
    for(const auto &value:saved){
        const auto item=value.toMap();const auto path=item.value("path").toString();
        if(!path.isEmpty())repositories_.append({path,item.value("favorite").toBool(),item.value("displayName").toString()});
    }
    if(repositories_.isEmpty()){
        const auto previous=settings.value("repositoryPath").toString();
        if(!previous.isEmpty())repositories_.append({previous,false});
    }
    std::stable_partition(repositories_.begin(),repositories_.end(),[](const auto &r){return r.favorite;});
    const auto drafts = settings.value("workspace/commitDrafts").toMap();
    for (auto it = drafts.cbegin(); it != drafts.cend(); ++it) commitDrafts_.insert(it.key(), it.value().toStringList());
    renderRepositories();
}
void MainWindow::saveRepositories() {
    QVariantList values;
    for(const auto &repo:repositories_){QVariantMap entry;entry.insert("path",repo.path);entry.insert("favorite",repo.favorite);entry.insert("displayName",repo.displayName);values.append(entry);}
    QSettings().setValue("workspace/repositories",values);
}
void MainWindow::renderRepositories(const QString &selected) {
    const QSignalBlocker blocker(repositoryList_);repositoryList_->clear();
    for(const auto &repo:repositories_){
        auto *item=new QListWidgetItem((repo.favorite?QStringLiteral("★ "):QStringLiteral("◇ "))+(repo.displayName.isEmpty()?QDir(repo.path).dirName():repo.displayName),repositoryList_);
        item->setData(PathRole,repo.path);item->setToolTip(QDir::toNativeSeparators(repo.path));
        if(repo.favorite)item->setForeground(AppTheme::color("#f0c879"));
        if(repo.path==validRepository_){QFont font=item->font();font.setBold(true);item->setFont(font);}
        if(repo.path==selected)repositoryList_->setCurrentItem(item);
    }
    updateActions();
}
void MainWindow::moveRepository(int direction) {
    int from=repositoryList_->currentRow(),to=from+direction;
    if(from<0 || to<0 || to>=repositories_.size() || repositories_[from].favorite!=repositories_[to].favorite)return;
    const auto path=repositories_[from].path;repositories_.swapItemsAt(from,to);
    saveRepositories();renderRepositories(path);
}
void MainWindow::stageAll(bool unstage) {
    auto *list=unstage?stagedList_:unstagedList_;QStringList paths;
    for(int i=0;i<list->count();++i)paths.append(list->item(i)->data(PathRole).toString());
    if(paths.isEmpty()||git_.isBusy())return;
    auto done=[this](bool ok,const QByteArray &,const QString &error){if(!ok)showError(error);refreshStatus();};
    if(unstage)git_.unstage(paths,done);else git_.stage(paths,done);
}
void MainWindow::deleteCurrentBranch() {
    if(branches_->count()<=1)return;
    if(git_.isBusy()||branches_->currentIndex()<0)return;
    const auto name=branches_->currentText();
    git_.inspect({"for-each-ref","--format=%(refname)%00%(upstream:remotename)%00%(upstream:remoteref)","refs/heads/"+name},[this,name](bool ok,const QByteArray &out,const QString &error){
        if(!ok){showError(error);return;}
        QString remote,ref;
        for(const auto &line:out.split('\n')){const auto fields=line.split('\0');if(fields.size()==3&&QString::fromUtf8(fields[0])=="refs/heads/"+name){remote=QString::fromUtf8(fields[1]);ref=QString::fromUtf8(fields[2]);}}
        QDialog dialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setObjectName("deleteCurrentBranchDialog");dialog.setMinimumWidth(480);
        auto *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(24,20,24,20);layout->setSpacing(14);
        auto *heading=new QLabel(tr("현재 브랜치 삭제: %1").arg(name));heading->setTextFormat(Qt::PlainText);layout->addWidget(heading);
        QStringList remaining;
        for(int i=0;i<branches_->count();++i)if(branches_->itemText(i)!=name)remaining.append(branches_->itemText(i));
        const QString destination=remaining.contains("main")?QString("main"):remaining.contains("master")?QString("master"):remaining.value(0);
        auto *description=new QLabel(destination.isEmpty()
            ?tr("다른 브랜치가 없어 현재 커밋을 유지한 채 브랜치 없는 상태로 전환합니다.")
            :tr("%1로 자동 이동한 뒤 삭제합니다.\n작업 파일과 충돌하면 전환 및 삭제를 중단합니다.").arg(destination));
        description->setTextFormat(Qt::PlainText);description->setObjectName("deleteDestinationInfo");layout->addWidget(description);
        auto *force=new QCheckBox(tr("강제 삭제 · 병합되지 않은 로컬 브랜치도 삭제"));force->setObjectName("forceDeleteBranch");layout->addWidget(force);
        connect(force,&QCheckBox::toggled,&dialog,[&dialog,force](bool checked){
            if(!checked)return;
            QMessageBox warning(QMessageBox::Warning,tr("강제 삭제 경고"),tr("병합되지 않은 커밋을 가리키는 브랜치가 삭제되어 해당 작업을 찾거나 복구하기 어려워질 수 있습니다.\n강제 삭제 옵션을 켜시겠습니까?"),QMessageBox::Yes|QMessageBox::No,&dialog,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);
            warning.setDefaultButton(QMessageBox::No);
            if(warning.exec()!=QMessageBox::Yes)force->setChecked(false);
        });
        auto *removeRemote=new QCheckBox(tr("연결된 원격 브랜치도 삭제"));removeRemote->setObjectName("deleteRemoteBranch");
        bool connected=!remote.isEmpty()&&remote!="."&&ref.startsWith("refs/heads/");removeRemote->setEnabled(connected);
        removeRemote->setToolTip(connected?tr("원격 %1의 %2를 삭제합니다. 공유 브랜치이므로 다른 사용자에게 영향을 줄 수 있습니다.").arg(remote,ref):tr("현재 브랜치에 연결된 원격 브랜치(upstream)가 없습니다."));
        layout->addWidget(removeRemote);
        auto *remoteInfo=new QLabel(connected?tr("원격 삭제 대상: %1 / %2").arg(remote,ref):tr("원격 브랜치 연결 없음"));remoteInfo->setTextFormat(Qt::PlainText);layout->addWidget(remoteInfo);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText(tr("현재 브랜치 삭제"));buttons->button(QDialogButtonBox::Cancel)->setText(tr("취소"));buttons->button(QDialogButtonBox::Cancel)->setDefault(true);layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return;
        git_.deleteCurrentBranch(name,destination,force->isChecked(),removeRemote->isChecked()?remote:QString(),removeRemote->isChecked()?ref:QString(),[this](bool ok,const QByteArray &,const QString &error){
            sync_->invalidate();if(!ok)showError(error);refreshStatus();
        });
    });
}
