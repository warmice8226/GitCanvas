#include "githubpanel.h"
#include "pullrequestdialog.h"
#include "git/gitclient.h"
#include "settings/repositorydialogs.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QShowEvent>
#include <QTimer>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
bool confirm(QWidget *parent,const QString &text){QMessageBox box(QMessageBox::Question,QObject::tr("GitHub 계정 적용 범위"),text,QMessageBox::Yes|QMessageBox::No,parent,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);box.setDefaultButton(QMessageBox::No);return box.exec()==QMessageBox::Yes;}
QLabel *plain(const QString &text){auto *label=new QLabel(text);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);return label;}
}
GithubPanel::GithubPanel(GitClient *git,QWidget *parent):QWidget(parent),git_(git),client_(this){
    setObjectName("githubPanel");auto *layout=new QVBoxLayout(this);
    layout->addWidget(plain(tr("GitHub에 로그인하면 내 저장소와 참여 중인 저장소를 불러옵니다. 저장소를 선택해 컴퓨터로 가져오거나 GitHub에서 열 수 있습니다.")));
    advanced_=new QDialog(this,Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);advanced_->setObjectName("githubAdvancedDialog");advanced_->resize(760,400);
    auto *advancedLayout=new QVBoxLayout(advanced_);advancedLayout->addWidget(plain(tr("GitHub 고급 설정")));
    auto *advancedAuthRow=new QHBoxLayout;advancedLayout->addLayout(advancedAuthRow);
    auto add=[this](QHBoxLayout *row,const QString &text,const char *name){auto *button=new QPushButton(text);button->setObjectName(name);row->addWidget(button);controls_.append(button);return button;};
    auto *summaryRow=new QHBoxLayout;accountSummary_=plain(tr("GitHub"));accountSummary_->setObjectName("githubAccountSummary");summaryRow->addWidget(accountSummary_,1);
    auto *advancedButton=add(summaryRow,tr("고급 설정"),"githubAdvancedSettings");layout->addLayout(summaryRow);
    connect(advancedButton,&QPushButton::clicked,this,[this]{advanced_->exec();});
    auto *installRow=new QHBoxLayout;path_=new QLineEdit(client_.executable());path_->setObjectName("githubExecutable");path_->setPlaceholderText(tr("gh 실행 파일 경로 · 비우면 PATH에서 검색"));installRow->addWidget(path_,1);
    auto *choose=add(installRow,tr("실행 파일 선택"),"chooseGh");auto *check=add(installRow,tr("gh 확인"),"checkGh");auto *install=add(installRow,tr("설치 안내"),"installGh");advancedLayout->addLayout(installRow);
    connect(choose,&QPushButton::clicked,this,[this]{QFileDialog dialog(this,tr("GitHub CLI 실행 파일"));dialog.setWindowFlags(Qt::Dialog|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);dialog.setOption(QFileDialog::DontUseNativeDialog);dialog.setFileMode(QFileDialog::ExistingFile);if(dialog.exec()==QDialog::Accepted){path_->setText(dialog.selectedFiles().first());client_.setExecutable(path_->text());clearRepositories();accounts_->clear();update();}});
    connect(path_,&QLineEdit::editingFinished,this,[this]{if(path_->text().trimmed()==client_.executable())return;client_.setExecutable(path_->text().trimmed());clearRepositories();accounts_->clear();update();});
    connect(check,&QPushButton::clicked,&client_,&GithubClient::check);
    connect(install,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl("https://github.com/cli/cli/releases/latest"));});
    auto *authRow=new QHBoxLayout;host_=new QLineEdit(QSettings().value("github/host","github.com").toString());host_->setObjectName("githubHost");host_->setMaximumWidth(230);advancedLayout->addWidget(host_);auto *refreshButton=add(advancedAuthRow,tr("인증 상태 다시 확인"),"refreshGithub");auto *login=add(authRow,tr("GitHub 로그인"),"loginGithub");cancel_=add(authRow,tr("요청 중단"),"cancelGithub");authRow->addStretch();layout->addLayout(authRow);
    layout->addWidget(plain(tr("브라우저에서 로그인하고 표시된 코드를 입력해 연결을 허용하세요. 기본 설정에서는 이 계정을 GitHub 저장소의 가져오기·올리기에도 연결합니다.")));
    autoSetup_=new QCheckBox(tr("로그인 후 Git HTTPS 인증도 자동 연결 (이 호스트의 모든 로컬 저장소에 적용)"));autoSetup_->setObjectName("githubAutoSetupGit");autoSetup_->setChecked(QSettings().value("github/autoSetupGit",true).toBool());advancedLayout->addWidget(autoSetup_);
    connect(autoSetup_,&QCheckBox::toggled,this,[](bool checked){QSettings().setValue("github/autoSetupGit",checked);});
    connect(host_,&QLineEdit::textChanged,this,[this]{accounts_->clear();clearRepositories();scope_->clear();update();});
    connect(refreshButton,&QPushButton::clicked,this,&GithubPanel::refresh);
    connect(login,&QPushButton::clicked,this,[this]{
        if(!GithubClient::validHost(host())){refresh();return;}
        if(client_.executable().isEmpty()){status_->setText(tr("로그인 연결 도구가 필요합니다. 고급 설정의 설치 안내에서 운영체제에 맞는 GitHub CLI를 준비하고 실행 파일을 선택하세요."));advanced_->open();return;}
        setupAfterLogin_=autoSetup_->isChecked();device_->setText(tr("브라우저에서 인증을 완료하세요. 일회용 코드가 발급되면 여기에 표시됩니다."));clearRepositories();accounts_->clear();client_.login(host());
    });
    connect(cancel_,&QPushButton::clicked,&client_,&GithubClient::cancel);
    device_=plain({});device_->setObjectName("githubDeviceCode");device_->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(device_);
    auto *authBrowser=new QPushButton(tr("인증 브라우저 열기"));authBrowser->setObjectName("githubAuthBrowser");authBrowser->hide();layout->addWidget(authBrowser);
    connect(&client_,&GithubClient::authorizationUrl,authBrowser,[authBrowser](const QString &url){authBrowser->setProperty("authorizationUrl",url);authBrowser->show();QDesktopServices::openUrl(QUrl(url));});
    connect(authBrowser,&QPushButton::clicked,this,[authBrowser]{QDesktopServices::openUrl(QUrl(authBrowser->property("authorizationUrl").toString()));});
    connect(&client_,&GithubClient::busyChanged,authBrowser,[authBrowser](bool busy){if(!busy){authBrowser->hide();authBrowser->setProperty("authorizationUrl",{});}});
    connect(&client_,&GithubClient::deviceCode,device_,[this](const QString &code){device_->setText(tr("브라우저 인증용 일회용 코드: %1 · https://%2/login/device").arg(code,host()));});
    auto *accountRow=new QHBoxLayout;accounts_=new QComboBox;accounts_->setObjectName("githubAccounts");accountRow->addWidget(accounts_,1);switch_=add(accountRow,tr("선택 계정으로 전환"),"switchGithub");logout_=add(accountRow,tr("선택 계정 로그아웃"),"logoutGithub");setup_=add(accountRow,tr("Git HTTPS 인증 연결"),"setupGithubGit");advancedLayout->addLayout(accountRow);
    scope_=plain({});scope_->setObjectName("githubAccountScope");advancedLayout->addWidget(scope_);
    advancedLayout->addWidget(plain(tr("인증 정보는 gh가 운영체제의 자격 증명 저장소에 보관합니다. 저장소를 사용할 수 없으면 gh 설정 파일에 평문으로 저장될 수 있으므로 아래 계정의 저장 위치를 확인하세요. Git 작성자와 SSH 인증은 별도입니다.")));
    auto *closeSettings=new QPushButton(tr("닫기"));closeSettings->setObjectName("closeGithubSettings");advancedLayout->addWidget(closeSettings);connect(closeSettings,&QPushButton::clicked,advanced_,&QDialog::accept);
    connect(accounts_,&QComboBox::currentIndexChanged,this,[this]{const auto account=accounts_->currentData().toJsonObject();scope_->setText(tr("선택 계정: %1 · 활성: %2 · 상태: %3\n권한(scope): %4 · 저장 위치: %5\n계정 전환은 이 호스트의 gh 전체에 적용됩니다. 원격 URL과 Git 작성자는 유지되며 기존 SSH/다른 credential helper는 별도입니다.").arg(account.value("login").toString(),account.value("active").toBool()?tr("예"):tr("아니요"),account.value("state").toString(),account.value("scopes").toString(tr("서버에서 제공하지 않음 · 저장소별 권한 확인")),account.value("tokenSource").toString()));update();});
    connect(switch_,&QPushButton::clicked,this,[this]{const auto login=accounts_->currentData().toJsonObject().value("login").toString();if(!confirm(this,tr("%1의 활성 gh 계정을 %2로 바꿉니다. 진행 중인 다른 앱의 gh 작업에도 영향을 줄 수 있습니다. 이 화면의 저장소 목록은 다시 읽습니다.").arg(host(),login)))return;clearRepositories();client_.switchAccount(host(),login);});
    connect(logout_,&QPushButton::clicked,this,[this]{const auto login=accounts_->currentData().toJsonObject().value("login").toString();if(!confirm(this,tr("%1의 %2 계정을 gh에서 제거합니다. 서버 토큰 자체를 폐기하지 않으며 로컬 파일과 Git 작성자 설정은 유지합니다.").arg(host(),login)))return;clearRepositories();client_.logout(host(),login);});
    connect(setup_,&QPushButton::clicked,this,[this]{if(confirm(this,tr("%1 호스트의 전역 Git HTTPS credential helper를 gh에 연결합니다. 이 호스트의 다른 로컬 저장소에도 적용됩니다. SSH 인증과 원격 주소는 변경하지 않습니다.").arg(host())))client_.setupGit(host());});
    auto *filters=new QHBoxLayout;search_=new QLineEdit;search_->setObjectName("githubRepositorySearch");search_->setPlaceholderText(tr("불러온 저장소 이름 / 소유자 검색"));filters->addWidget(search_,1);owner_=new QComboBox;owner_->addItems({tr("모든 소유자"),tr("개인"),tr("조직")});filters->addWidget(owner_);visibility_=new QComboBox;visibility_->addItems({tr("모든 공개 범위"),"public","private","internal"});filters->addWidget(visibility_);list_=add(filters,tr("활성 계정 저장소 조회"),"listGithubRepos");layout->addLayout(filters);
    repos_=new QTableWidget(0,5);repos_->setObjectName("githubRepositories");repos_->setHorizontalHeaderLabels({tr("저장소"),tr("소유자 유형"),tr("공개 범위"),tr("내 권한"),tr("최근 갱신")});repos_->setEditTriggers(QAbstractItemView::NoEditTriggers);repos_->setSelectionBehavior(QAbstractItemView::SelectRows);repos_->setSelectionMode(QAbstractItemView::SingleSelection);repos_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);repos_->verticalHeader()->hide();layout->addWidget(repos_,1);
    auto *actions=new QHBoxLayout;clone_=add(actions,tr("선택 저장소 Clone"),"cloneGithubRepo");browser_=add(actions,tr("GitHub에서 열기"),"browseGithubRepo");more_=add(actions,tr("다음 100개"),"moreGithubRepos");pullRequests_=add(actions,tr("Pull Requests"),"openPullRequests");layout->addLayout(actions);
    connect(pullRequests_,&QPushButton::clicked,this,[this]{
        const int row=repos_->currentRow();if(row<0)return;
        const auto repository=repos_->item(row,0)->data(Qt::UserRole).toJsonObject().value("full_name").toString();
        PullRequestDialog dialog(&client_,git_,host(),activeLogin(),repository,this);
        connect(&dialog,&PullRequestDialog::busyChanged,this,[this](bool active){prBusy_=active;emit busyChanged(busy());update();});
        connect(&dialog,&PullRequestDialog::checkedOut,this,&GithubPanel::repositoryCreated);
        dialog.exec();emit repositoryChanged();update();
    });
    status_=plain(tr("GitHub에 로그인해 시작하세요. 이미 연결한 계정은 자동으로 불러옵니다."));status_->setObjectName("githubStatus");layout->addWidget(status_);
    connect(search_,&QLineEdit::textChanged,this,&GithubPanel::filter);connect(owner_,&QComboBox::currentIndexChanged,this,&GithubPanel::filter);connect(visibility_,&QComboBox::currentIndexChanged,this,&GithubPanel::filter);
    connect(list_,&QPushButton::clicked,this,[this]{clearRepositories();client_.repositories(host(),activeLogin(),1);});connect(more_,&QPushButton::clicked,this,[this]{client_.repositories(host(),activeLogin(),page_+1);});
    connect(repos_,&QTableWidget::itemSelectionChanged,this,&GithubPanel::update);
    auto selectedUrl=[this](const char *key){const int row=repos_->currentRow();if(row<0)return QUrl();const auto repo=repos_->item(row,0)->data(Qt::UserRole).toJsonObject();const QUrl url(repo.value(key).toString());return url.scheme()=="https"&&url.host().compare(host(),Qt::CaseInsensitive)==0&&url.userInfo().isEmpty()?url:QUrl();};
    connect(browser_,&QPushButton::clicked,this,[selectedUrl]{const auto url=selectedUrl("html_url");if(url.isValid()&&!url.isEmpty())QDesktopServices::openUrl(url);});
    connect(clone_,&QPushButton::clicked,this,[this,selectedUrl]{const auto url=selectedUrl("clone_url");if(url.isEmpty())return;RepositoryDialog dialog(git_,true,this);if(auto *field=dialog.findChild<QLineEdit*>("cloneUrl"))field->setText(url.toString());if(dialog.exec()==QDialog::Accepted)emit repositoryCloned(dialog.createdPath());});
    connect(&client_,&GithubClient::commandStarted,this,&GithubPanel::commandStarted);connect(&client_,&GithubClient::message,status_,&QLabel::setText);
    auto *advancedStatus=plain({});advancedLayout->insertWidget(advancedLayout->count()-1,advancedStatus);connect(&client_,&GithubClient::message,advancedStatus,&QLabel::setText);
    connect(&client_,&GithubClient::busyChanged,this,[this]{emit busyChanged(busy());});
    connect(&client_,&GithubClient::requestFailed,this,[this]{if(prBusy_)return;clearRepositories();accounts_->clear();setupAfterLogin_=false;update();});
    connect(&client_,&GithubClient::busyChanged,this,[this](bool busy){if(!busy)device_->clear();update();});connect(git_,&GitClient::busyChanged,this,[this]{update();if(isVisible()&&!restored_&&!git_->isBusy()&&!client_.busy()){restored_=true;refresh();}});
    connect(&client_,&GithubClient::authChanged,this,[this]{
        if(setupAfterLogin_){client_.setupGit(host());return;}
        refresh();
    });
    connect(&client_,&GithubClient::gitSetupFinished,this,[this](bool ok){
        const bool continueLogin=setupAfterLogin_;setupAfterLogin_=false;
        if(continueLogin&&ok)refresh();
    });
    connect(&client_,&GithubClient::accountsReady,this,[this](const QJsonArray &accounts){accounts_->clear();int active=-1;for(const auto &value:accounts){auto a=value.toObject();accounts_->addItem(a.value("login").toString()+(a.value("active").toBool()?tr(" · 활성"):QString()),a);if(a.value("active").toBool())active=accounts_->count()-1;}accounts_->setCurrentIndex(active>=0?active:0);update();if(!activeLogin().isEmpty())client_.repositories(host(),activeLogin(),1);});
    connect(&client_,&GithubClient::repositoriesReady,this,[this](const QJsonArray &repos,int page){if(page==1)clearRepositories();page_=page;moreAvailable_=repos.size()==100;for(const auto &value:repos){const auto repo=value.toObject();const auto permission=repo.value("permissions").toObject();QStringList rights;for(const auto *key:{"pull","push","admin","maintain","triage"})if(permission.value(key).toBool())rights.append(key);QStringList columns{repo.value("full_name").toString(),repo.value("owner").toObject().value("type").toString(),repo.value("visibility").toString(repo.value("private").toBool()?"private":"public"),rights.isEmpty()?tr("권한 정보 없음"):rights.join(", "),repo.value("updated_at").toString()};int row=repos_->rowCount();repos_->insertRow(row);for(int col=0;col<columns.size();++col){auto *item=new QTableWidgetItem(columns[col]);item->setToolTip(columns[col]);repos_->setItem(row,col,item);}repos_->item(row,0)->setData(Qt::UserRole,repo);}status_->setText(tr("%1 / %2 · %3개 조회. 개인·협업·조직 접근 범위이며 보호 브랜치 쓰기 권한을 보장하지 않습니다.").arg(host(),activeLogin()).arg(repos_->rowCount()));filter();update();});
    update();
}
void GithubPanel::showEvent(QShowEvent *event){
    QWidget::showEvent(event);
    QTimer::singleShot(0,this,[this]{
        if(restored_||git_->isBusy()||client_.busy())return;
        restored_=true;
        if(!client_.executable().isEmpty())refresh();
    });
}
QString GithubPanel::host()const{return host_->text().trimmed().toLower();}
QString GithubPanel::activeLogin()const{for(int i=0;i<accounts_->count();++i){auto a=accounts_->itemData(i).toJsonObject();if(a.value("active").toBool()&&a.value("state").toString()=="success")return a.value("login").toString();}return {};}
void GithubPanel::clearRepositories(){repos_->setRowCount(0);page_=0;moreAvailable_=false;}
void GithubPanel::refresh(){clearRepositories();accounts_->clear();if(GithubClient::validHost(host()))QSettings().setValue("github/host",host());client_.accounts(host());}
void GithubPanel::filter(){for(int row=0;row<repos_->rowCount();++row){const bool name=repos_->item(row,0)->text().contains(search_->text(),Qt::CaseInsensitive);const auto owner=repos_->item(row,1)->text();repos_->setRowHidden(row,!name||(owner_->currentIndex()==1&&owner!="User")||(owner_->currentIndex()==2&&owner!="Organization")||(visibility_->currentIndex()>0&&repos_->item(row,2)->text()!=visibility_->currentText()));}}
void GithubPanel::update(){
    const bool idle=!busy()&&!git_->isBusy();for(auto *button:controls_)button->setEnabled(idle);cancel_->setEnabled(client_.busy());autoSetup_->setEnabled(idle);path_->setEnabled(idle);host_->setEnabled(idle);accounts_->setEnabled(idle);repos_->setEnabled(idle);
    const auto account=accounts_->currentData().toJsonObject();const bool active=!activeLogin().isEmpty();accountSummary_->setText(active?tr("%1 / %2").arg(activeLogin(),host()):tr("GitHub"));switch_->setEnabled(idle&&!account.isEmpty()&&!account.value("active").toBool());logout_->setEnabled(idle&&!account.isEmpty());setup_->setEnabled(idle&&active);list_->setEnabled(idle&&active);more_->setEnabled(idle&&active&&moreAvailable_);clone_->setEnabled(idle&&active&&repos_->currentRow()>=0);browser_->setEnabled(idle&&active&&repos_->currentRow()>=0);pullRequests_->setEnabled(idle&&active&&repos_->currentRow()>=0);
}
