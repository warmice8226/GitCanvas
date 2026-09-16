#pragma once
#include "gittoolbox.h"
#include <QDialog>

struct GitCommandOption {
    QString name,syntax,description;
    bool value=false,optional=false,equals=false,attached=false;
    QStringList choices;
};
struct GitCommandManual {
    QList<GitCommandOption> options;
    QStringList subcommands,unparsed;
    QString text,summary,description;
    static GitCommandManual parse(const QString &html);
};
class GitCommandForm final:public QDialog {
    Q_OBJECT
public:
    explicit GitCommandForm(GitClient *git,QWidget *parent=nullptr);
    void reject() override;
    QStringList arguments(QString *error) const;
signals:
    void repositoryChanged();
protected:
    void closeEvent(QCloseEvent *) override;
private:
    void loadManual();void filterOptions();void addRow(const QString &name,const QString &syntax,const QString &value,int mode);
    void invalidate();void updateButtons();void preview();void execute();void moveRow(int delta);
    QString rowValue(int row) const; void compose();
    void refreshDescriptions(); void refreshCommands(); QString commandLabel(const QString &name) const;
    QString translated(const QString &text) const;
    GitClient *git_;ToolboxController controller_;QString docs_;bool loading_=false;
    GitCommandManual manual_;ToolReview review_;
    QComboBox *commands_,*subcommands_;QLineEdit *commandSearch_,*optionSearch_;
    QTableWidget *options_,*selected_;QPlainTextEdit *output_,*stdin_;
    QLabel *summary_;QPushButton *previewButton_,*executeButton_;QWidget *inputs_;
    QPlainTextEdit *sentence_=nullptr; QStringList assembled_;
    QComboBox *descriptionLanguage_=nullptr; QPlainTextEdit *commandDescription_=nullptr,*commandOriginal_=nullptr;
};
