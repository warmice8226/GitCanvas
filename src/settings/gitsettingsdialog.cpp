#include "gitsettingsdialog.h"
#include "git/gitclient.h"
#include "operations/operationpanel.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

GitSettingsDialog::GitSettingsDialog(GitClient *git, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint), git_(git)
{
    setObjectName("gitSettingsDialog");
    resize(760, 680);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    auto *heading = new QLabel(tr("Git 환경 · 작성자 설정"));
    heading->setStyleSheet("font-size: 20px; font-weight: 600;");
    layout->addWidget(heading);
    version_ = new QLabel;
    version_->setObjectName("gitEnvironment");
    version_->setTextFormat(Qt::PlainText);
    version_->setWordWrap(true);
    version_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(version_);
    auto *explanation = new QLabel(tr("작성자는 커밋에 기록할 이름과 이메일입니다. GitHub 로그인 정보가 아니며, 저장해도 기존 커밋은 바뀌지 않습니다."));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto *form = new QFormLayout;
    scope_ = new QComboBox;
    scope_->setObjectName("identityScope");
    scope_->addItem(tr("이 저장소만 · Local"), "--local");
    scope_->addItem(tr("현재 사용자 기본값 · Global"), "--global");
    if (git_->repositoryPath().isEmpty()) {
        static_cast<QStandardItemModel *>(scope_->model())->item(0)->setEnabled(false);
        scope_->setCurrentIndex(1);
    }
    form->addRow(tr("저장 범위"), scope_);
    name_ = new QLineEdit;
    name_->setObjectName("identityName");
    name_->setPlaceholderText(tr("커밋에 표시할 이름"));
    email_ = new QLineEdit;
    email_->setObjectName("identityEmail");
    email_->setPlaceholderText("name@example.com");
    form->addRow(tr("이름"), name_);
    form->addRow(tr("이메일"), email_);
    layout->addLayout(form);
    scopeHelp_ = new QLabel;
    scopeHelp_->setWordWrap(true);
    scopeHelp_->setTextFormat(Qt::PlainText);
    layout->addWidget(scopeHelp_);
    layout->addWidget(new QLabel(tr("현재 유효한 설정과 실제 커밋 신원")));
    effective_ = new QPlainTextEdit;
    effective_->setObjectName("effectiveIdentity");
    effective_->setReadOnly(true);
    layout->addWidget(effective_, 1);
    status_ = new QLabel;
    status_->setObjectName("identityStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    layout->addWidget(new ProcessControls(git_));
    auto *buttons = new QHBoxLayout;
    reload_ = new QPushButton(tr("다시 확인"));
    reload_->setObjectName("reloadGitEnvironment");
    save_ = new QPushButton(tr("작성자 설정 저장"));
    save_->setObjectName("saveIdentity");
    close_ = new QPushButton(tr("닫기"));
    buttons->addWidget(reload_);
    buttons->addStretch();
    buttons->addWidget(save_);
    buttons->addWidget(close_);
    layout->addLayout(buttons);
    connect(reload_, &QPushButton::clicked, this, &GitSettingsDialog::reload);
    connect(save_, &QPushButton::clicked, this, &GitSettingsDialog::save);
    connect(close_, &QPushButton::clicked, this, &GitSettingsDialog::reject);
    connect(scope_, &QComboBox::activated, this, [this] { loadScope(); });
    // Delay until the dialog event loop starts; callbacks never outlive a closed dialog.
    setWorking(true);
    QTimer::singleShot(0, this, &GitSettingsDialog::reload);
}

QMap<QString, GitSettingsDialog::Value> GitSettingsDialog::parseConfig(const QByteArray &output)
{
    QMap<QString, Value> values;
    const auto fields = output.split('\0');
    // --null --show-scope --show-origin: scope NUL origin NUL key LF value NUL.
    for (qsizetype i = 0; i + 2 < fields.size(); i += 3) {
        const auto separator = fields[i + 2].indexOf('\n');
        const auto key = QString::fromUtf8(fields[i + 2].left(separator));
        if (key != "user.name" && key != "user.email") continue;
        values[key] = {separator < 0 ? QString() : QString::fromUtf8(fields[i + 2].mid(separator + 1)),
                       QString::fromUtf8(fields[i]), QString::fromUtf8(fields[i + 1])};
    }
    return values;
}

void GitSettingsDialog::setWorking(bool working)
{
    working_ = working;
    scope_->setEnabled(!working && available_);
    name_->setEnabled(!working && available_ && scopeLoaded_);
    email_->setEnabled(!working && available_ && scopeLoaded_);
    save_->setEnabled(!working && available_ && scopeLoaded_);
    reload_->setEnabled(!working);
    close_->setEnabled(!working);
}

void GitSettingsDialog::showFailure(const QString &message)
{
    status_->setText(message);
    setWorking(false);
}

void GitSettingsDialog::reload()
{
    if (git_->isBusy()) { showFailure(tr("다른 Git 작업이 끝난 뒤 다시 확인하세요.")); return; }
    setWorking(true);
    available_ = false;
    scopeLoaded_ = false;
    status_->setText(tr("Git 환경을 확인하고 있습니다…"));
    effective_->clear();
    git_->inspectEnvironment({"--version"}, [this](bool ok, const QByteArray &out, const QString &error) {
        const auto path = QStandardPaths::findExecutable("git");
        if (!ok) {
            version_->setText(tr("Git 실행 불가\n%1").arg(error));
#ifdef Q_OS_MACOS
            showFailure(tr("macOS에서는 Git 또는 Xcode Command Line Tools가 필요합니다. Homebrew Git도 사용할 수 있습니다. 설치 후 앱을 다시 실행하고 Git 경로와 실행 권한을 확인하세요."));
#else
            showFailure(tr("Windows에서는 Git for Windows를 설치하고 Git 명령을 PATH에서 사용할 수 있도록 선택하세요. 설치 후 GitCanvas를 다시 실행하세요. 이미 설치했다면 PATH 설정과 실행 권한을 확인하세요."));
#endif
            return;
        }
        available_ = true;
        version_->setText(tr("%1\n실행 파일: %2\n조회 대상: %3")
            .arg(QString::fromUtf8(out).trimmed(), QDir::toNativeSeparators(path),
                 git_->repositoryPath().isEmpty() ? tr("저장소 없음 · 전역 설정 가능") : QDir::toNativeSeparators(git_->repositoryPath())));
        loadScope();
    });
}

void GitSettingsDialog::loadScope()
{
    if (git_->isBusy()) return;
    setWorking(true);
    scopeLoaded_ = false;
    scopeHelp_->setText(scope_->currentIndex() == 0
        ? tr("이 저장소의 user.name / user.email만 저장합니다. 다른 저장소에는 영향을 주지 않습니다.")
        : tr("현재 사용자의 Git 기본값을 저장합니다. 별도 작성자 설정이 없는 다른 저장소에도 적용됩니다. 저장소·worktree·조건부 설정이나 환경 변수가 있으면 그 값이 우선할 수 있습니다."));
    // Missing global config files are allowed when listing all scopes. Select the target scope
    // from that read, so a first-time user can still create their global identity.
    git_->inspectEnvironment({"config", "--null", "--list", "--show-origin", "--show-scope"},
        [this](bool ok, const QByteArray &out, const QString &error) {
            if (!ok) { showFailure(error); return; }
            const auto wanted = scope_->currentIndex() == 0 ? QByteArray("local") : QByteArray("global");
            QByteArray selected;
            const auto fields = out.split('\0');
            for (qsizetype i = 0; i + 2 < fields.size(); i += 3)
                if (fields[i] == wanted) selected += fields[i] + '\0' + fields[i+1] + '\0' + fields[i+2] + '\0';
            const auto values = parseConfig(selected);
            name_->setText(values.value("user.name").text);
            email_->setText(values.value("user.email").text);
            scopeLoaded_ = true;
            loadEffective();
        });
}

void GitSettingsDialog::loadEffective(const QString &notice)
{
    git_->inspectEnvironment({"config", "--null", "--list", "--show-origin", "--show-scope"},
        [this, notice](bool ok, const QByteArray &out, const QString &error) {
            if (!ok) { scopeLoaded_ = false; showFailure(notice + "\n" + error); return; }
            const auto values = parseConfig(out);
            QStringList lines;
            for (const auto &key : {QString("user.name"), QString("user.email")}) {
                const auto value = values.value(key);
                lines.append(key + ": " + (values.contains(key) ? value.text : tr("설정 없음")));
                if (values.contains(key)) lines.append(tr("  출처: %1 · %2").arg(value.scope, value.origin));
            }
            lines.append(tr("\n환경 변수가 있으면 아래 실제 신원에 우선 반영됩니다."));
            effective_->setPlainText(lines.join('\n'));
            loadIdentity(notice);
        });
}

void GitSettingsDialog::loadIdentity(const QString &notice)
{
    git_->inspectEnvironment({"var", "GIT_AUTHOR_IDENT"},
        [this, notice](bool authorOk, const QByteArray &out, const QString &error) {
            auto identity = QString::fromUtf8(out).trimmed();
            identity.remove(QRegularExpression(" \\d+ [+-]\\d{4}$"));
            effective_->appendPlainText(tr("실제 작성자: %1").arg(authorOk ? identity : error));
            git_->inspectEnvironment({"var", "GIT_COMMITTER_IDENT"},
                [this, notice, authorOk](bool ok, const QByteArray &out, const QString &error) {
                    auto identity = QString::fromUtf8(out).trimmed();
                    identity.remove(QRegularExpression(" \\d+ [+-]\\d{4}$"));
                    effective_->appendPlainText(tr("실제 기록자: %1").arg(ok ? identity : error));
                    status_->setText(notice + (notice.isEmpty() ? QString() : "\n") +
                        (ok && authorOk ? tr("작성자·기록자 확인 완료. 위 신원이 새 커밋에 사용됩니다.")
                                        : tr("커밋 신원을 확인하지 못했습니다. 이름과 이메일을 저장한 뒤 다시 확인하세요.")));
                    setWorking(false);
                });
        });
}

void GitSettingsDialog::save()
{
    if (working_ || git_->isBusy() || !scopeLoaded_) return;
    const auto name = name_->text().trimmed(), email = email_->text().trimmed();
    const QRegularExpression forbidden("[<>\\x00-\\x1f\\x7f]");
    const QRegularExpression emailShape("^[^\\s<>@]+@[^\\s<>@]+$");
    if (name.isEmpty() || forbidden.match(name).hasMatch() ||
        forbidden.match(email).hasMatch() || !emailShape.match(email).hasMatch()) {
        status_->setText(tr("이름과 이메일을 모두 입력하세요. 이름에는 줄바꿈·꺾쇠를 쓸 수 없고 이메일은 name@example.com 형태여야 합니다."));
        return;
    }
    const auto scope = scope_->currentData().toString();
    setWorking(true);
    status_->setText(tr("선택한 범위에 작성자 설정을 저장하고 있습니다…"));
    git_->inspectEnvironment({"config", scope, "--replace-all", "user.name", name},
        [this, scope, email](bool ok, const QByteArray &, const QString &error) {
            if (!ok) { showFailure(tr("이름을 저장하지 못했습니다. 이메일은 변경하지 않았습니다.\n") + error); return; }
            git_->inspectEnvironment({"config", scope, "--replace-all", "user.email", email},
                [this](bool ok, const QByteArray &, const QString &error) {
                    emit settingsSaved();
                    loadEffective(ok ? tr("선택한 범위에 이름과 이메일을 저장했습니다. 현재 유효한 값과 실제 신원을 아래에서 확인하세요.")
                                     : tr("이름은 저장했지만 이메일 저장은 실패했습니다. 입력을 확인하고 다시 저장하세요.\n") + error);
                });
        });
}

void GitSettingsDialog::reject()
{
    if (!working_) QDialog::reject();
}

void GitSettingsDialog::closeEvent(QCloseEvent *event)
{
    if (working_) event->ignore(); else QDialog::closeEvent(event);
}
