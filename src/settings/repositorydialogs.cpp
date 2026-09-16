#include "repositorydialogs.h"
#include "git/gitclient.h"
#include "operations/operationpanel.h"
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QLabel *plainLabel(const QString &text = {}) {
    auto *label = new QLabel(text); label->setTextFormat(Qt::PlainText); label->setWordWrap(true); return label;
}
QString chooseFolder(QWidget *parent, const QString &initial) {
    QFileDialog dialog(parent, QObject::tr("대상 폴더"), initial);
    dialog.setOption(QFileDialog::DontUseNativeDialog); dialog.setFileMode(QFileDialog::Directory);
    dialog.setWindowFlag(Qt::WindowTitleHint);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
}
}
RepositoryDialog::RepositoryDialog(GitClient *git, bool clone, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint), git_(git), clone_(clone) {
    setObjectName("repositoryCreationDialog"); resize(740, 560);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(plainLabel(clone ? tr("원격 저장소 Clone") : tr("새 Git 저장소 만들기")));
    layout->addWidget(plainLabel(clone
        ? tr("새 폴더 또는 비어 있는 폴더에 생성합니다. 성공하면 Workspace에 추가하고 바로 엽니다. 실패한 폴더는 자동 삭제하지 않습니다.")
        : tr("새 폴더나 파일이 있는 기존 프로젝트 폴더에 Git 저장소를 만듭니다. 기존 파일은 유지되며 자동으로 Stage하거나 커밋하지 않습니다. 성공하면 Workspace에 추가하고 바로 엽니다.")));
    auto *form = new QFormLayout;
    url_ = new QLineEdit(this); url_->setObjectName("cloneUrl"); url_->setPlaceholderText("https://host/owner/repository.git");
    if (clone) form->addRow(tr("원격 URL / 로컬 저장소 경로"), url_); else url_->hide();
    auto *pathRow = new QHBoxLayout;
    path_ = new QLineEdit; path_->setObjectName("creationPath"); path_->setPlaceholderText(tr("예: C:/Projects/my-project"));
    browse_ = new QPushButton(tr("폴더 선택")); pathRow->addWidget(path_); pathRow->addWidget(browse_);
    form->addRow(tr("대상 폴더 · 전체 경로"), pathRow);
    branch_ = new QLineEdit(clone ? QString() : "main"); branch_->setObjectName("creationBranch");
    branch_->setPlaceholderText(tr("비우면 원격의 기본 브랜치"));
    form->addRow(clone ? tr("가져올 브랜치 / 태그 · 선택") : tr("첫 브랜치 이름"), branch_);
    mode_ = new QComboBox(this); mode_->setObjectName("cloneMode");
    mode_->addItem(tr("전체 · 모든 이력과 파일"), "full");
    mode_->addItem(tr("Shallow · 최근 이력만"), "shallow");
    mode_->addItem(tr("Partial · 파일 내용은 필요할 때"), "partial");
    depth_ = new QSpinBox(this); depth_->setObjectName("cloneDepth"); depth_->setRange(1, 1000000); depth_->setValue(100);
    if (clone) { form->addRow(tr("Clone 방식"), mode_); form->addRow(tr("Shallow 커밋 수"), depth_); }
    else { mode_->hide(); depth_->hide(); }
    depth_->setEnabled(false);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { depth_->setEnabled(mode_->currentData() == "shallow"); });
    layout->addLayout(form);
    if (clone) layout->addWidget(plainLabel(tr("Shallow는 선택 브랜치의 최근 이력만 가져옵니다. Partial은 서버 지원이 필요하며 현재 파일 checkout 때 내용이 내려올 수 있습니다. 인증은 기존 Git credential helper/SSH 설정을 사용합니다.")));
    progress_ = new QPlainTextEdit; progress_->setObjectName("creationProgress"); progress_->setReadOnly(true); progress_->setMaximumBlockCount(300); layout->addWidget(progress_, 1);
    layout->addWidget(new ProcessControls(git_));
    auto *buttons = new QHBoxLayout; run_ = new QPushButton(clone ? tr("Clone 시작") : tr("저장소 만들기")); run_->setObjectName("createRepository");
    close_ = new QPushButton(tr("닫기")); buttons->addStretch(); buttons->addWidget(run_); buttons->addWidget(close_); layout->addLayout(buttons);
    auto *waiting = plainLabel(tr("다른 Git 작업이 진행 중입니다. 완료되면 생성 버튼이 활성화됩니다."));
    waiting->setObjectName("creationWaiting"); layout->addWidget(waiting);
    auto updateAvailability = [this, waiting] {
        const bool busy = git_->isBusy();
        run_->setEnabled(!working_ && !busy);
        waiting->setVisible(!working_ && busy);
    };
    connect(git_, &GitClient::busyChanged, this, updateAvailability);
    updateAvailability();
    connect(browse_, &QPushButton::clicked, this, [this] { const auto path = chooseFolder(this, path_->text()); if (!path.isEmpty()) path_->setText(path); });
    connect(close_, &QPushButton::clicked, this, &RepositoryDialog::reject);
    connect(run_, &QPushButton::clicked, this, &RepositoryDialog::execute);
    connect(git_, &GitClient::cloneProgress, this, [this](const QString &text) { if (working_ && clone_) progress_->appendPlainText(text); });
}
void RepositoryDialog::setWorking(bool busy) {
    working_ = busy;
    for (auto *widget : QList<QWidget *>{path_, url_, branch_, mode_, run_, browse_, close_}) widget->setEnabled(!busy);
    run_->setEnabled(!busy && !git_->isBusy());
    depth_->setEnabled(!busy && mode_->currentData() == "shallow");
}
void RepositoryDialog::execute() {
    if (working_) return;
    if (git_->isBusy()) {
        progress_->setPlainText(tr("다른 Git 작업이 진행 중입니다. 완료되면 생성 버튼이 활성화됩니다."));
        return;
    }
    const auto destination = QDir::cleanPath(path_->text().trimmed());
    if(path_->text().trimmed().isEmpty()) { progress_->setPlainText(tr("대상 폴더의 전체 경로를 입력하세요.")); return; }
    setWorking(true); progress_->setPlainText(tr("Git 작업을 시작합니다…"));
    auto done = [this, destination](bool ok, const QByteArray &, const QString &error) {
        setWorking(false);
        if (!ok) { progress_->appendPlainText(error + tr("\n실패 후 대상 폴더를 확인하세요. 기존 파일은 자동으로 삭제하지 않습니다.")); return; }
        createdPath_ = destination; accept();
    };
    if (clone_) git_->cloneRepository(url_->text().trimmed(), destination, mode_->currentData().toString(), depth_->value(), branch_->text().trimmed(), done);
    else git_->createRepository(destination, branch_->text().trimmed(), done);
}
void RepositoryDialog::reject() { if (!working_) QDialog::reject(); }

RemoteDialog::RemoteDialog(GitClient *git, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint), git_(git) {
    setObjectName("remoteSettingsDialog"); resize(760, 640);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(plainLabel(tr("원격 저장소 · 브랜치 연결")));
    layout->addWidget(plainLabel(tr("remote는 이 저장소가 주고받을 서버 주소의 별명입니다. upstream은 현재 브랜치가 비교하고 올릴 원격 브랜치입니다.")));
    auto *form = new QFormLayout;
    remotes_ = new QComboBox; remotes_->setObjectName("remoteList"); form->addRow(tr("등록된 원격"), remotes_);
    name_ = new QLineEdit; name_->setObjectName("remoteName"); form->addRow(tr("원격 이름"), name_);
    url_ = new QLineEdit; url_->setObjectName("remoteUrl"); form->addRow(tr("가져오기 주소"), url_);
    layout->addLayout(form);
    auto *remoteButtons = new QHBoxLayout;
    save_ = new QPushButton(tr("원격 저장")); save_->setObjectName("saveRemote");
    remove_ = new QPushButton(tr("원격 연결 제거")); remove_->setObjectName("removeRemote");
    sameUrl_ = new QPushButton(tr("Push 주소 통일"));
    remoteButtons->addWidget(save_); remoteButtons->addWidget(remove_); remoteButtons->addWidget(sameUrl_); layout->addLayout(remoteButtons);
    addresses_ = plainLabel(); layout->addWidget(addresses_);
    branch_ = plainLabel(); branch_->setObjectName("upstreamStatus"); layout->addWidget(branch_);
    auto *branchForm = new QFormLayout;
    target_ = new QComboBox; target_->setEditable(true); target_->setObjectName("upstreamTarget"); branchForm->addRow(tr("대상 원격 브랜치"), target_); layout->addLayout(branchForm);
    layout->addWidget(plainLabel(tr("기존 원격 브랜치를 선택하거나 새 이름을 입력하세요. 연결 저장은 서버를 바꾸지 않습니다. 이후 상단 Fetch로 비교한 다음, 원격에 브랜치가 없으면 최초 Push가 활성화됩니다. 먼저 로컬 커밋을 만들어야 합니다.")));
    auto *branchButtons = new QHBoxLayout;
    fetch_ = new QPushButton(tr("원격 브랜치 목록 가져오기")); fetch_->setObjectName("fetchRemoteBranches");
    connect_ = new QPushButton(tr("현재 브랜치 연결 저장")); connect_->setObjectName("saveUpstream");
    branchButtons->addWidget(fetch_); branchButtons->addWidget(connect_); layout->addLayout(branchButtons);
    status_ = plainLabel(); status_->setObjectName("remoteStatus"); layout->addWidget(status_, 1);
    layout->addWidget(new ProcessControls(git_));
    close_ = new QPushButton(tr("닫기")); layout->addWidget(close_);
    connect(remotes_, &QComboBox::activated, this, [this] { selectRemote(); });
    connect(save_, &QPushButton::clicked, this, &RemoteDialog::save);
    connect(remove_, &QPushButton::clicked, this, &RemoteDialog::remove);
    connect(connect_, &QPushButton::clicked, this, &RemoteDialog::connectBranch);
    connect(close_, &QPushButton::clicked, this, &RemoteDialog::reject);
    connect(fetch_, &QPushButton::clicked, this, [this] {
        setWorking(true); const auto name = remotes_->currentData().toString();
        git_->inspect({"fetch", "--no-tags", name, "+refs/heads/*:refs/remotes/" + name + "/*"}, [this](bool ok, const QByteArray &, const QString &error) { finish(ok, error); });
    });
    connect(sameUrl_, &QPushButton::clicked, this, [this] {
        setWorking(true);
        const auto name = remotes_->currentData().toString();
        git_->inspect({"remote", "get-url", name}, [this, name](bool ok, const QByteArray &out, const QString &error) {
            if (!ok) { finish(false, error, false); return; }
            git_->inspect({"config", "--local", "--replace-all", "remote." + name + ".pushurl", QString::fromUtf8(out).trimmed()},
                [this](bool ok, const QByteArray &, const QString &error) { finish(ok, error); });
        });
    });
    setWorking(true); QTimer::singleShot(0, this, &RemoteDialog::reload);
}
void RemoteDialog::setWorking(bool busy) {
    working_ = busy; const bool existing = !remotes_->currentData().toString().isEmpty();
    remotes_->setEnabled(!busy); name_->setEnabled(!busy && !existing); url_->setEnabled(!busy); save_->setEnabled(!busy); close_->setEnabled(!busy);
    remove_->setEnabled(!busy && existing); fetch_->setEnabled(!busy && existing); sameUrl_->setEnabled(!busy && existing);
    target_->setEnabled(!busy && existing); connect_->setEnabled(!busy && existing && !branchName_.isEmpty());
}
void RemoteDialog::reload() {
    const auto previous = remotes_->currentData().toString(); setWorking(true);
    git_->inspect({"remote"}, [this, previous](bool ok, const QByteArray &out, const QString &error) {
        if (!ok) { finish(false, error, false); return; }
        { const QSignalBlocker block(remotes_); remotes_->clear();
          for (const auto &name : QString::fromUtf8(out).trimmed().split('\n', Qt::SkipEmptyParts)) remotes_->addItem(name, name);
          remotes_->addItem(tr("＋ 새 원격 추가"), QString());
          const int index = previous.isEmpty() ? 0 : remotes_->findData(previous); remotes_->setCurrentIndex(index < 0 ? 0 : index); }
        git_->inspect({"symbolic-ref", "--quiet", "--short", "HEAD"}, [this](bool ok, const QByteArray &out, const QString &) {
            branchName_ = ok ? QString::fromUtf8(out).trimmed() : QString();
            branch_->setText(branchName_.isEmpty() ? tr("브랜치 없는 상태입니다. 먼저 로컬 브랜치로 전환하세요.") : tr("현재 로컬 브랜치: %1").arg(branchName_));
            selectRemote();
        });
    });
}
void RemoteDialog::selectRemote() {
    setWorking(true); target_->clear(); target_->setEditText(branchName_);
    const auto name = remotes_->currentData().toString(); name_->setText(name.isEmpty() ? "origin" : name);
    if (name.isEmpty()) { url_->clear(); addresses_->clear(); setWorking(false); return; }
    git_->inspect({"remote", "get-url", name}, [this, name](bool ok, const QByteArray &out, const QString &error) {
        if (!ok) { finish(false, error, false); return; } url_->setText(QString::fromUtf8(out).trimmed());
        git_->inspect({"remote", "get-url", "--push", "--all", name}, [this, name](bool ok, const QByteArray &out, const QString &error) {
            if (!ok) { finish(false, error, false); return; }
            addresses_->setText(tr("현재 Push 주소: %1\n동기화하려면 가져오기 주소와 Push 주소가 같고 단일 대상이어야 합니다.").arg(QString::fromUtf8(out).trimmed()));
            git_->inspect({"for-each-ref", "--format=%(refname)%00%(symref)", "refs/remotes/" + name + "/"}, [this, name](bool ok, const QByteArray &out, const QString &error) {
                if (!ok) { finish(false, error, false); return; }
                for (const auto &line : out.split('\n')) { const auto fields = line.split('\0'); if (fields.size() == 2 && fields[1].isEmpty()) target_->addItem(QString::fromUtf8(fields[0]).mid(14 + name.size())); }
                target_->setEditText(branchName_);
                git_->inspect({"for-each-ref", "--format=%(refname)%00%(upstream)", "refs/heads/" + branchName_}, [this](bool ok, const QByteArray &out, const QString &) {
                    if (ok) for (const auto &line : out.split('\n')) { const auto fields = line.split('\0'); if (fields.size() == 2 && QString::fromUtf8(fields[0]) == "refs/heads/" + branchName_) {
                        const auto upstream = QString::fromUtf8(fields[1]);
                        branch_->setText(tr("현재 브랜치: %1\n현재 연결: %2").arg(branchName_, upstream.isEmpty() ? tr("없음") : upstream));
                        const auto prefix = "refs/remotes/" + remotes_->currentData().toString() + "/";
                        if (upstream.startsWith(prefix)) target_->setEditText(upstream.mid(prefix.size()));
                    }}
                    setWorking(false);
                });
            });
        });
    });
}
void RemoteDialog::finish(bool ok, const QString &error, bool reloadList) {
    status_->setText(ok ? tr("완료했습니다. 상단 동기화 버튼에서 Fetch로 다시 비교하세요.") : error);
    if (reloadList) { emit configured(); reload(); } else setWorking(false);
}
void RemoteDialog::save() {
    const auto name = name_->text().trimmed();
    setWorking(true); git_->saveRemote(name, url_->text().trimmed(), remotes_->currentData().toString().isEmpty(),
        [this, name](bool ok, const QByteArray &, const QString &error) {
            if (ok && remotes_->findData(name) < 0) { remotes_->addItem(name, name); remotes_->setCurrentIndex(remotes_->count()-1); }
            finish(ok, error);
        });
}
void RemoteDialog::remove() {
    const auto name = remotes_->currentData().toString();
    QMessageBox confirm(QMessageBox::Warning, tr("원격 연결 제거"), tr("%1의 주소와 로컬 원격 추적 정보·연결 설정을 제거합니다. 서버의 저장소나 브랜치는 삭제하지 않습니다.").arg(name), QMessageBox::Yes | QMessageBox::No, this, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    confirm.setDefaultButton(QMessageBox::No); if (confirm.exec() != QMessageBox::Yes) return;
    setWorking(true); git_->removeRemote(name, [this](bool ok, const QByteArray &, const QString &error) { finish(ok, error); });
}
void RemoteDialog::connectBranch() {
    setWorking(true); git_->setUpstream(branchName_, remotes_->currentData().toString(), target_->currentText().trimmed(),
        [this](bool ok, const QByteArray &, const QString &error) { finish(ok, error); });
}
void RemoteDialog::reject() { if (!working_) QDialog::reject(); }
