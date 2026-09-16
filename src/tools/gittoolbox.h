#pragma once
#include "git/gitclient.h"
#include <QMap>
#include <QWidget>
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QCheckBox;
class QFormLayout;
class QLabel;
class QTableWidget;
struct ToolField {QString key,label,initial;QStringList choices;};
struct GitTool {QString id,group,title,description;QList<ToolField> fields;bool query=false,clean=false;};
struct ToolReview {QString repository,fingerprint;QStringList arguments;QString description;QString id;QString inputFile,inputHash;bool query=false,clean=false;QByteArray standardInput;QString outputFile;};
class ToolboxController final:public QObject {
    Q_OBJECT
public:
    explicit ToolboxController(GitClient *git,QObject *parent=nullptr):QObject(parent),git_(git){}
    static QList<GitTool> catalog();
    static QList<GitTool> advancedCatalog();
    static QStringList advancedArguments(const QString &id,const QMap<QString,QString> &values,QString *error);
    static QStringList arguments(const QString &id,const QMap<QString,QString> &values,QString *error);
    void review(const QString &id,const QMap<QString,QString> &values,std::function<void(bool,ToolReview,QString)> callback);
    void execute(const ToolReview &review,GitClient::CommandCallback callback);
private:
    void snapshot(std::function<void(bool,QString,QString)> callback);
    GitClient *git_;
};
class GitToolbox final:public QWidget {
    Q_OBJECT
public:
    explicit GitToolbox(GitClient *git,QWidget *parent=nullptr);
signals:
    void repositoryChanged();
    void openWorktree(const QString &path);
private:
    void selectTool();void update();void preview();void execute();void renderResult(const QByteArray &bytes);
    GitClient *git_;ToolboxController controller_;QList<GitTool> catalog_;ToolReview review_;
    QComboBox *tools_;QLineEdit *search_;QFormLayout *form_;QLabel *description_;QPlainTextEdit *preview_,*result_;
    QTableWidget *table_;QPushButton *reviewButton_,*executeButton_,*openButton_;QCheckBox *expert_;
    QMap<QString,QWidget*> fields_;bool loading_=false;QString repository_;
};
