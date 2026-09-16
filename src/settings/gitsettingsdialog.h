#pragma once

#include <QDialog>
#include <QMap>

class GitClient;
class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QPlainTextEdit;

class GitSettingsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit GitSettingsDialog(GitClient *git, QWidget *parent = nullptr);
    void reject() override;

signals:
    void settingsSaved();

private:
    struct Value { QString text, scope, origin; };
    static QMap<QString, Value> parseConfig(const QByteArray &output);
    void reload();
    void loadScope();
    void loadEffective(const QString &notice = {});
    void loadIdentity(const QString &notice);
    void save();
    void setWorking(bool working);
    void showFailure(const QString &message);
    void closeEvent(QCloseEvent *event) override;

    GitClient *git_;
    QLabel *version_, *scopeHelp_, *status_;
    QPlainTextEdit *effective_;
    QLineEdit *name_, *email_;
    QComboBox *scope_;
    QPushButton *save_, *reload_, *close_;
    bool working_ = false, available_ = false, scopeLoaded_ = false;
};
