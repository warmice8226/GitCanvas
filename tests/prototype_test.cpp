#include "mainwindow.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QDesktopServices>
#include "history/historywidget.h"
#include "history/historytable.h"
#include "settings/applanguage.h"
#include "settings/diagnostics.h"
#include "ssh/sshworkspace.h"
#include <QStandardPaths>
#include <QAbstractItemModelTester>
#include "sync/synccontroller.h"
#include "diff/diffwidget.h"
#include "settings/gitsettingsdialog.h"
#include "settings/repositorydialogs.h"
#include "operations/operationpanel.h"
#include "worktree/worktreecontroller.h"
#include "worktree/worktreepanel.h"
#include "merge/mergecontroller.h"
#include "merge/mergepanel.h"
#include "history/rebaseeditor.h"
#include "history/rewritepanel.h"
#include "tools/gittoolbox.h"
#include "tools/gitcommandform.h"
#include "platform/platformenvironment.h"
#include "github/githubpanel.h"
#include "github/pullrequests.h"
#include "github/pullrequestdialog.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QFile>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QFileDialog>
#include <QTimer>
#include <QTabWidget>
#include <QSplitter>
#include <QCalendarWidget>
#include <QScrollBar>
#include <QtTest>

// Isolate configuration writes from the developer's real global Git configuration.
class TestEnvironment {
public:
    void set(const QByteArray &key, const QByteArray &value = {}) {
        if (!saved_.contains(key)) saved_.insert(key, {qEnvironmentVariableIsSet(key.constData()), qgetenv(key.constData())});
        if (value.isNull()) qunsetenv(key.constData()); else qputenv(key.constData(), value);
    }
    ~TestEnvironment() {
        for (auto it = saved_.cbegin(); it != saved_.cend(); ++it)
            if (it.value().first) qputenv(it.key().constData(), it.value().second); else qunsetenv(it.key().constData());
    }
private:
    QMap<QByteArray, QPair<bool, QByteArray>> saved_;
};

class BrowserCapture : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void open(const QUrl &url){urls.append(url);}
};
class PrototypeTest : public QObject {
    Q_OBJECT
private slots:
    void repositoryChangeWhileBusy() {
        QSettings().clear();
        QTemporaryDir repo; QVERIFY(repo.isValid());
        QProcess setup; setup.setWorkingDirectory(repo.path());
        setup.start("git", {"init", "-b", "main"});
        QVERIFY(setup.waitForFinished(15000)); QCOMPARE(setup.exitCode(), 0);
        MainWindow window; window.openRepository(repo.path());
        auto *client = window.findChild<GitClient*>();
        auto *files = window.findChild<QListWidget*>("unstagedFiles");
        auto *panel = window.findChild<GitToolbox*>();
        QVERIFY(client && files && panel);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(), 15000);
        QCOMPARE(files->count(), 0);
        QFile file(repo.filePath("pending.txt")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("pending refresh\n"); file.close();
        client->inspect({"status", "--porcelain"}, [](bool, const QByteArray &, const QString &) {});
        QVERIFY(client->isBusy());
        // A notification arriving during a command must survive the busy guard.
        QVERIFY(QMetaObject::invokeMethod(panel, "repositoryChanged", Qt::DirectConnection));
        QTRY_COMPARE_WITH_TIMEOUT(files->count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(), 15000);
    }
    void themeAndFileSections() {
        MainWindow window; window.show();
        auto *theme=window.findChild<QPushButton*>("themeToggle");QVERIFY(theme);
        if(QSettings().value("ui/theme").toString()=="light")theme->click();
        QTRY_COMPARE(window.palette().color(QPalette::Window),QColor("#0c1017"));
        theme->click();QTRY_COMPARE(window.palette().color(QPalette::Window),QColor("#eef1f5"));
        QDialog popup(&window);auto *layout=new QVBoxLayout(&popup);layout->addWidget(new QLabel("Theme preview"));popup.show();
        QTRY_COMPARE(popup.palette().color(QPalette::Window),QColor("#f5f7fa"));
        for(const auto &name:{QString("unstaged"),QString("staged")}){
            auto *list=window.findChild<QListWidget*>(name+"Files");QVERIFY(list->isHidden());
            window.findChild<QPushButton*>(name+"Toggle")->click();QVERIFY(!list->isHidden());
            window.findChild<QPushButton*>(name+"Expand")->click();QCOMPARE(list->minimumHeight(),320);
            window.findChild<QPushButton*>(name+"Toggle")->click();QVERIFY(list->isHidden());QCOMPARE(list->minimumHeight(),75);
        }
        window.grab().save("light-theme-screenshot.png");popup.grab().save("light-dialog-screenshot.png");
        popup.close();theme->click();QTRY_COMPARE(window.palette().color(QPalette::Window),QColor("#0c1017"));
        const auto before=window.size();window.resize(before+QSize(80,60));QCOMPARE(window.size(),before+QSize(80,60));
    }
    void macLauncherEnvironment() {
        const auto finder=PlatformEnvironment::macSearchPath("/usr/bin:/bin:/usr/sbin:/sbin");
        QVERIFY(finder.startsWith("/opt/homebrew/bin:/usr/local/bin:"));
        const auto custom=PlatformEnvironment::macSearchPath("/custom tools/bin:/usr/bin:/custom tools/bin:.:relative::/usr/local/bin");
        QVERIFY(custom.startsWith("/custom tools/bin:/usr/local/bin:"));QCOMPARE(custom.count("/custom tools/bin"),1);QVERIFY(!custom.split(':').contains("."));QVERIFY(!custom.split(':').contains("relative"));
        QCOMPARE(PlatformEnvironment::macSearchPath(custom),custom);
#ifndef Q_OS_MACOS
        const auto previous=qgetenv("PATH");PlatformEnvironment::initialize();QCOMPARE(qgetenv("PATH"),previous);
#endif
    }
    void commandOptionForms() {
        const auto parsed=GitCommandManual::parse("<h2>COMMANDS</h2><dl><dt>add</dt><dd>Create a thing.</dd></dl><h2>OPTIONS</h2><dl><dt>--[no-]color</dt><dd>Color.</dd><dt>--format=&lt;format&gt;</dt><dd>Format text.</dd><dt>--count[=&lt;n&gt;]</dt><dd>Optional count.</dd><dt>-m &lt;message&gt;</dt><dd>Message.</dd><dt>--complex &lt;a&gt; --other &lt;b&gt;</dt><dd>Composite.</dd></dl>");
        QCOMPARE(parsed.options.size(),5);QVERIFY(parsed.subcommands.contains("add"));QCOMPARE(parsed.unparsed.size(),1);QVERIFY(parsed.options[2].equals);QVERIFY(parsed.options[3].optional);QVERIFY(parsed.options[4].value);QVERIFY(!parsed.options[4].equals);
        const auto attached=GitCommandManual::parse("<dl><dt>-S[&lt;key&gt;]</dt><dd>Sign.</dd><dt>-&lt;number&gt;</dt><dd>Count.</dd><dt>-L:&lt;function&gt;:&lt;file&gt;</dt><dd>Lines.</dd></dl>");QCOMPARE(attached.options.size(),3);QVERIFY(attached.options[0].attached);QVERIFY(attached.options[0].optional);QVERIFY(attached.options[1].attached);QVERIFY(attached.options[2].attached);
        QTemporaryDir repo;QVERIFY(repo.isValid());auto run=[&](QStringList args){QProcess p;p.setWorkingDirectory(repo.path());p.start("git",args);p.closeWriteChannel();p.waitForFinished(15000);return p.readAllStandardOutput();};run({"init","-b","main"});run({"config","user.name","Form Test"});run({"config","user.email","form@example.invalid"});run({"commit","--allow-empty","-m","base"});
        GitClient client;client.setRepositoryPath(repo.path());MainWindow main;GitCommandForm form(&client,&main);form.show();auto *commands=form.findChild<QComboBox*>("formCommand");QTRY_VERIFY_WITH_TIMEOUT(commands->count()>50,15000);QTRY_VERIFY(!client.isBusy());
        const auto htmlPath=QString::fromUtf8(run({"--html-path"})).trimmed();if(!QFileInfo::exists(htmlPath+"/git-log.html"))QSKIP("Installed Git HTML manuals required for dynamic form integration");
        commands->setCurrentIndex(commands->findData("log"));auto *options=form.findChild<QTableWidget*>("formOptions");QVERIFY(options->rowCount()>20);
        auto addOption=[&](const QString &name){for(int i=0;i<options->rowCount();++i)if(options->item(i,0)->text().startsWith(name)){options->selectRow(i);form.findChild<QPushButton*>("formAddOption")->click();return true;}return false;};
        QVERIFY(addOption("--format="));auto *selected=form.findChild<QTableWidget*>("formArguments");auto *value=qobject_cast<QPlainTextEdit*>(selected->cellWidget(0,1));value->setPlainText("%s\n$(echo literal); value");QString error;QCOMPARE(form.arguments(&error),QStringList({"log","--format=%s\n$(echo literal); value"}));QVERIFY(error.isEmpty());
        form.findChild<QPushButton*>("formAssemble")->click();form.findChild<QPushButton*>("formPreview")->click();auto *execute=form.findChild<QPushButton*>("formExecute");QTRY_VERIFY_WITH_TIMEOUT(execute->isEnabled(),15000);value->setPlainText("%H");QVERIFY(!execute->isEnabled());form.findChild<QPushButton*>("formAssemble")->click();form.findChild<QPushButton*>("formPreview")->click();QTRY_VERIFY_WITH_TIMEOUT(execute->isEnabled(),15000);
        QTimer::singleShot(100,&form,[&]{for(auto *box:form.findChildren<QMessageBox*>())box->done(QMessageBox::Yes);});execute->click();QTRY_VERIFY_WITH_TIMEOUT(form.findChild<QPlainTextEdit*>("formOutput")->toPlainText().contains("Backup:"),15000);QVERIFY(form.findChild<QPlainTextEdit*>("formOutput")->toPlainText().contains(QString::fromLatin1(run({"rev-parse","HEAD"})).trimmed()));
        commands->setCurrentIndex(commands->findData("diff"));QVERIFY(addOption("--diff-algorithm="));
        auto *algorithm=qobject_cast<QComboBox*>(selected->cellWidget(0,1));QVERIFY(algorithm);QVERIFY(!algorithm->isEditable());QVERIFY(algorithm->findText("default")>=0);
        algorithm->setCurrentText("patience");form.findChild<QPushButton*>("formAssemble")->click();
        QCOMPARE(form.findChild<QPlainTextEdit*>("formSentence")->toPlainText(),QString("git diff --diff-algorithm=patience"));
        algorithm->setCurrentText("histogram");QVERIFY(form.findChild<QPlainTextEdit*>("formSentence")->toPlainText().isEmpty());QVERIFY(!form.findChild<QPushButton*>("formPreview")->isEnabled());
        form.findChild<QPushButton*>("formAssemble")->click();
        auto *language=form.findChild<QComboBox*>("formDescriptionLanguage");QVERIFY(language);
        options->setCurrentCell(0,0);const auto before=form.arguments(&error);const auto sentence=form.findChild<QPlainTextEdit*>("formSentence")->toPlainText();
        language->setCurrentIndex(language->findData("ko"));
        QVERIFY(commands->currentText().contains(QRegularExpression("[가-힣]")));
        auto *search=form.findChild<QLineEdit*>("formCommandSearch");search->setText("변경사항 비교");
        QVERIFY(commands->findData("diff")>=0);QCOMPARE(commands->currentData().toString(),QString("diff"));
        QVERIFY(form.findChild<QPlainTextEdit*>("formCommandDescription")->toPlainText().contains(QRegularExpression("[가-힣]")));
        QVERIFY(options->item(0,1)->text().contains(QRegularExpression("[가-힣]")));
        QFile original(htmlPath+"/git-diff.html");QVERIFY(original.open(QIODevice::ReadOnly));const auto manual=GitCommandManual::parse(QString::fromUtf8(original.readAll()));
        QCOMPARE(form.findChild<QPlainTextEdit*>("formCommandOriginal")->toPlainText(),manual.text);
        QVERIFY(!form.findChild<QPlainTextEdit*>("formOptionOriginal"));QVERIFY(!form.findChild<QPlainTextEdit*>("formManual"));
        QVERIFY(!form.findChild<QLineEdit*>("formStdinFile"));QVERIFY(!form.findChild<QLineEdit*>("formStdoutFile"));
        language->setCurrentIndex(language->findData("en"));
        QVERIFY(search->text().isEmpty());QVERIFY(commands->count()>50);
        QCOMPARE(options->item(0,1)->text(),manual.options.first().description);
        QCOMPARE(form.findChild<QPlainTextEdit*>("formCommandDescription")->toPlainText(),manual.description);
        QCOMPARE(form.arguments(&error),before);QCOMPARE(form.findChild<QPlainTextEdit*>("formSentence")->toPlainText(),sentence);
        language->setCurrentIndex(language->findData("ko"));
        auto *chooser=form.findChild<QTabWidget*>("formChooser");chooser->setCurrentIndex(0);form.findChild<QPushButton*>("formUseCommand")->click();QCOMPARE(chooser->currentIndex(),1);
        QVERIFY(addOption("--output="));auto *pathEdit=qobject_cast<QPlainTextEdit*>(selected->cellWidget(1,1));QVERIFY(pathEdit);
        auto *browse=qobject_cast<QPushButton*>(selected->cellWidget(1,3));QVERIFY(browse&&browse->isEnabled());
        const auto folder=repo.filePath("경로 test");QVERIFY(QDir().mkpath(folder));const auto file=folder+"/new output.patch";
        for(int kind=0;kind<4;++kind){
            const auto path=kind>=2?folder:file;
            QTimer::singleShot(100,&form,[&form,path]{auto *dialog=form.findChild<QFileDialog*>("formValuePathDialog");QVERIFY(dialog);QTimer::singleShot(3000,dialog,&QDialog::reject);dialog->setDirectory(QFileInfo(path).absolutePath());QTimer::singleShot(250,dialog,[dialog,path]{dialog->selectFile(path);auto *filename=dialog->findChild<QLineEdit*>("fileNameEdit");QVERIFY(filename);filename->setText(QDir::toNativeSeparators(path));QMetaObject::invokeMethod(dialog,"accept");});});
            browse->findChild<QAction*>(QString("formBrowsePath%1").arg(kind))->trigger();
            QCOMPARE(pathEdit->toPlainText(),kind%2?QDir(repo.path()).relativeFilePath(path):path);
            QCOMPARE(form.arguments(&error).last(),"--output="+pathEdit->toPlainText());QVERIFY(error.isEmpty());
        }
        form.findChild<QPushButton*>("formAssemble")->click();const auto savedSentence=form.findChild<QPlainTextEdit*>("formSentence")->toPlainText();
        QTimer::singleShot(100,&form,[&form]{form.findChild<QFileDialog*>("formValuePathDialog")->reject();});browse->findChild<QAction*>("formBrowsePath0")->trigger();
        QCOMPARE(form.findChild<QPlainTextEdit*>("formSentence")->toPlainText(),savedSentence);
        form.grab().save("git-command-form-screenshot.png");
        QJsonArray coverage;int count=0;for(int i=0;i<commands->count();++i){const auto name=commands->itemData(i).toString();QFile doc(htmlPath+"/git-"+name+".html");if(!doc.open(QIODevice::ReadOnly))continue;auto manual=GitCommandManual::parse(QString::fromUtf8(doc.readAll()));count+=manual.options.size();coverage.append(QJsonObject{{"command",name},{"options",manual.options.size()},{"unparsed",QJsonArray::fromStringList(manual.unparsed)}});}
        QVERIFY(count>500);QFile report("git-command-coverage.json");QVERIFY(report.open(QIODevice::WriteOnly));report.write(QJsonDocument(QJsonObject{{"git",QString::fromUtf8(run({"--version"})).trimmed()},{"options",count},{"commands",coverage}}).toJson());
        QCOMPARE(ToolboxController::arguments("expert",{{"argumentsJson",QString::fromUtf8(QJsonDocument(QJsonArray{"log","--format=one\ntwo"}).toJson())}},&error),QStringList({"log","--format=one\ntwo"}));
        QVERIFY(ToolboxController::arguments("expert",{{"argumentsJson","[\"push\",\"origin\"]"}},&error).isEmpty());form.close();
    }
    void advancedToolFamilies() {
        QTemporaryDir root;QVERIFY(root.isValid());const auto repo=root.filePath("repo");QDir().mkpath(repo);
        auto git=[&](QStringList args){QProcess p;p.setWorkingDirectory(repo);p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode())return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput();};
        auto write=[&](QString path,QByteArray bytes){QFile file(repo+"/"+path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();};
        git({"init","-b","main"});git({"config","user.name","Advanced Test"});git({"config","user.email","advanced@example.invalid"});QDir().mkpath(repo+"/one");QDir().mkpath(repo+"/two");QVERIFY(write("one/file","one\n"));QVERIFY(write("two/file","two\n"));git({"add","."});git({"commit","-m","base"});const auto base=QString::fromLatin1(git({"rev-parse","HEAD"}).trimmed());git({"commit","--allow-empty","-m","second"});
        GitClient client;client.setRepositoryPath(repo);ToolboxController controller(&client);ToolReview review;bool done=false,success=false;QString error;QByteArray output;
        auto prepare=[&](QString id,QMap<QString,QString> values){done=false;controller.review(id,values,[&](bool ok,ToolReview r,QString e){success=ok;review=r;error=e;done=true;});};
        auto execute=[&]{done=false;controller.execute(review,[&](bool ok,const QByteArray &out,const QString &e){success=ok;output=out;error=e;done=true;});};
        auto perform=[&](QString id,QMap<QString,QString> values){prepare(id,values);if(!QTest::qWaitFor([&]{return done;},15000)||!success)return false;execute();return QTest::qWaitFor([&]{return done;},15000)&&success;};
        QVERIFY2(perform("notes.add",{{"notes","refs/notes/commits"},{"ref","HEAD"},{"message","memo\nsecond line"}}),qPrintable(error));QVERIFY(git({"notes","show","HEAD"}).contains("second line"));
        QVERIFY(perform("notes.append",{{"notes","refs/notes/commits"},{"ref","HEAD"},{"message","append"}}));const auto notes=git({"rev-parse","refs/notes/commits"}).trimmed();QVERIFY(perform("notes.copy",{{"notes","refs/notes/commits"},{"ref","HEAD"},{"target",base}}));QVERIFY(git({"notes","show",base}).contains("append"));
        QVERIFY(perform("notes.remove",{{"notes","refs/notes/commits"},{"ref","HEAD"}}));QVERIFY(git({"notes","show","HEAD"}).startsWith("ERROR"));QVERIFY(git({"for-each-ref","--format=%(objectname)","refs/gitcanvas/tool-backups"}).contains(notes));
        prepare("notes.add",{{"notes","refs/heads/main"},{"ref","HEAD"},{"message","invalid"}});QTRY_VERIFY(done);QVERIFY(!success);
        QVERIFY(perform("config.add",{{"scope","local"},{"key","gitcanvas.values"},{"value","a.b"}}));QVERIFY(perform("config.add",{{"scope","local"},{"key","gitcanvas.values"},{"value","axb"}}));QVERIFY(perform("config.remove-value",{{"scope","local"},{"key","gitcanvas.values"},{"value","a.b"}}));QCOMPARE(git({"config","--get-all","gitcanvas.values"}).trimmed(),QByteArray("axb"));
        QVERIFY2(perform("sparse.set",{{"paths","one"}}),qPrintable(error));QVERIFY(QFileInfo::exists(repo+"/one/file"));QVERIFY(!QFileInfo::exists(repo+"/two/file"));QVERIFY(perform("sparse.add",{{"paths","two"}}));QVERIFY(QFileInfo::exists(repo+"/two/file"));QVERIFY(perform("sparse.disable",{}));
        prepare("sparse.set",{{"paths","../outside"}});QTRY_VERIFY(done);QVERIFY(!success);
        QVERIFY(perform("replace.create",{{"ref","HEAD"},{"target",base}}));QCOMPARE(git({"log","-1","--format=%s"}).trimmed(),QByteArray("base"));QVERIFY2(perform("replace.delete",{{"ref","HEAD"}}),qPrintable(error));QCOMPARE(git({"log","-1","--format=%s"}).trimmed(),QByteArray("second"));
        QVERIFY2(perform("worktree.detached",{{"path",root.filePath("detached")},{"ref","HEAD"}}),qPrintable(error));QVERIFY(QFileInfo::exists(root.filePath("detached/one/file")));
        const QByteArray binary=QByteArray::fromHex("00ff0d0a800001");const auto inputPath=root.filePath("input.bin"),oidPath=root.filePath("object.txt"),binaryPath=root.filePath("output.bin");QFile input(inputPath);QVERIFY(input.open(QIODevice::WriteOnly));input.write(binary);input.close();
        QVERIFY2(perform("expert",{{"arguments","hash-object\n-w\n--stdin"},{"stdinFile",inputPath},{"stdoutFile",oidPath}}),qPrintable(error));QFile oidFile(oidPath);QVERIFY(oidFile.open(QIODevice::ReadOnly));const auto oid=QString::fromLatin1(oidFile.readAll()).trimmed();oidFile.close();
        QVERIFY2(perform("expert",{{"arguments","cat-file\nblob\n"+oid},{"stdoutFile",binaryPath}}),qPrintable(error));QFile binaryFile(binaryPath);QVERIFY(binaryFile.open(QIODevice::ReadOnly));QCOMPARE(binaryFile.readAll(),binary);binaryFile.close();
        prepare("expert",{{"arguments","hash-object\n--stdin"},{"stdinFile",inputPath}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);QVERIFY(input.open(QIODevice::Append));input.write("changed");input.close();execute();QTRY_VERIFY(done);QVERIFY(!success);
        QVERIFY(!perform("expert",{{"arguments","status"},{"stdoutFile",binaryPath}}));QVERIFY(binaryFile.open(QIODevice::ReadOnly));QCOMPARE(binaryFile.readAll(),binary);binaryFile.close();
        for(const auto &id:QStringList{"health.graph-write","health.graph-verify","health.pack-refs","notes.list","replace.list","rerere.status","sparse.disable"})QVERIFY2(perform(id,id=="notes.list"?QMap<QString,QString>{{"notes","refs/notes/commits"}}:QMap<QString,QString>{}),qPrintable(id+": "+error));
        QVERIFY(!git({"fsck","--full"}).startsWith("ERROR"));
    }
    void advancedSubmoduleProtection() {
        QTemporaryDir root;QVERIFY(root.isValid());const auto repo=root.filePath("parent"),child=root.filePath("child");QDir().mkpath(repo);QDir().mkpath(child);TestEnvironment env;env.set("GIT_ALLOW_PROTOCOL","file");
        auto git=[&](QString cwd,QStringList args){QProcess p;p.setWorkingDirectory(cwd);p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode())return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput();};
        for(const auto &path:QStringList{repo,child}){git(path,{"init","-b","main"});git(path,{"config","user.name","Submodule Test"});git(path,{"config","user.email","submodule@example.invalid"});git(path,{"commit","--allow-empty","-m","base"});}
        GitClient client;client.setRepositoryPath(repo);ToolboxController controller(&client);ToolReview review;bool done=false,success=false;QString error;
        auto perform=[&](QString id,QMap<QString,QString> values){done=false;controller.review(id,values,[&](bool ok,ToolReview r,QString e){success=ok;review=r;error=e;done=true;});if(!QTest::qWaitFor([&]{return done;},15000)||!success)return false;done=false;controller.execute(review,[&](bool ok,const QByteArray &,const QString &e){success=ok;error=e;done=true;});return QTest::qWaitFor([&]{return done;},15000)&&success;};
        QVERIFY2(perform("submodule.add",{{"url",child},{"path","nested"}}),qPrintable(error));QVERIFY(QFileInfo::exists(repo+"/nested/.git"));git(repo,{"commit","-m","add child"});
        QVERIFY2(perform("submodule.set-branch",{{"path","nested"},{"name","main"}}),qPrintable(error));QCOMPARE(git(repo,{"config","-f",".gitmodules","submodule.nested.branch"}).trimmed(),QByteArray("main"));
        QVERIFY2(perform("submodule.set-url",{{"path","nested"},{"url",child}}),qPrintable(error));git(repo,{"add",".gitmodules"});git(repo,{"commit","-m","tracking branch"});
        QFile precious(repo+"/nested/precious");QVERIFY(precious.open(QIODevice::WriteOnly));precious.write("keep me");precious.close();QVERIFY(!perform("submodule.deinit",{{"path","nested"}}));QVERIFY(precious.exists());
        const auto ignore=root.filePath("ignore");QFile ignored(ignore);QVERIFY(ignored.open(QIODevice::WriteOnly));ignored.write("precious\n");ignored.close();git(repo+"/nested",{"config","core.excludesFile",ignore});QVERIFY(!perform("submodule.deinit",{{"path","nested"}}));QVERIFY(precious.exists());QVERIFY(precious.remove());
        QVERIFY2(perform("submodule.deinit",{{"path","nested"}}),qPrintable(error));QVERIFY(!QFileInfo::exists(repo+"/nested/.git"));QVERIFY(QFileInfo::exists(repo+"/.git/modules/nested/HEAD"));
    }
    void sshWorkspaceSafety() {
        QSettings().clear();QTemporaryDir dir;QVERIFY(dir.isValid());
        SshProfile profile{QString(32,'a'),"Test server","example.invalid","dev",{},"/srv",2222};QVERIFY(profile.valid());
        auto invalid=profile;invalid.host="host; echo wrong";QVERIFY(!invalid.valid());invalid=profile;invalid.user="-option";QVERIFY(!invalid.valid());invalid=profile;invalid.root="relative";QVERIFY(!invalid.valid());
        const QByteArray blob=QByteArray::fromHex("0000000b7373682d6564323535313900000020")+QByteArray(32,'x');const QString key="example.invalid ssh-ed25519 "+QString::fromLatin1(blob.toBase64());
        QVERIFY(SshExecutor::fingerprint(key).startsWith("SHA256:"));QVERIFY(SshExecutor::fingerprint("host ssh-ed25519 aaaa").isEmpty());
        SshExecutor executor(nullptr,dir.path());QString error;QVERIFY(!executor.trusted(profile));QVERIFY(executor.trust(profile,key,&error));QVERIFY(executor.trusted(profile));QVERIFY(!executor.trust(profile,key,&error));
        const auto args=SshExecutor::arguments(profile,dir.filePath("known hosts"));QVERIFY(args.contains("StrictHostKeyChecking=yes"));QVERIFY(args.contains("ForwardAgent=no"));QVERIFY(args.contains("BatchMode=yes"));QVERIFY(args.contains("none"));QCOMPARE(args.last(),profile.host);
#ifdef Q_OS_WIN
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),dir.filePath("ssh.exe")));QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),dir.filePath("ssh-keyscan.exe")));
#else
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),dir.filePath("ssh")));QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),dir.filePath("ssh-keyscan")));
#endif
        TestEnvironment env;env.set("PATH",dir.path().toUtf8()+QDir::listSeparator().toLatin1()+qgetenv("PATH"));env.set("GITCANVAS_FAKE_SSH",dir.path().toUtf8());env.set("GH_TOKEN","do-not-forward");
        bool done=false,success=false;QJsonObject result;auto callback=[&](bool ok,const QJsonObject &value,const QString &message){done=true;success=ok;result=value;error=message;};
        executor.scan(profile,callback);QTRY_VERIFY_WITH_TIMEOUT(done,10000);QVERIFY(success);QCOMPARE(result.value("fingerprint").toString(),SshExecutor::fingerprint(key));
        done=false;executor.request(profile,{{"action","probe"}},callback);QTRY_VERIFY_WITH_TIMEOUT(done,10000);QVERIFY2(success,qPrintable(error));
        QFile captured(dir.filePath("request.json"));QVERIFY(captured.open(QIODevice::ReadOnly));const auto sent=QJsonDocument::fromJson(captured.readAll()).object();captured.close();QCOMPARE(sent.value("input").toObject().value("version").toInt(),1);QVERIFY(!sent.value("command").toString().contains(profile.root));QVERIFY(sent.value("command").toString().size()<30000);
        env.set("GITCANVAS_SSH_FAILURE","hostkey");done=false;executor.request(profile,{{"action","probe"}},callback);QTRY_VERIFY_WITH_TIMEOUT(done,10000);QVERIFY(!success);
        env.set("GITCANVAS_SSH_FAILURE","hang");done=false;executor.request(profile,{{"action","commit"}},callback);QVERIFY(executor.writing());executor.disconnectTransport();QTRY_VERIFY_WITH_TIMEOUT(done,10000);QVERIFY(!success);QVERIFY(!executor.busy());env.set("GITCANVAS_SSH_FAILURE",{});
        QSettings().setValue("ssh/profiles",QJsonDocument(QJsonArray{profile.json()}).toJson());MainWindow main;SshWorkspace window(&main,dir.path());window.show();window.findChild<QComboBox*>("sshProfiles")->setCurrentIndex(1);window.findChild<QPushButton*>("connectSsh")->click();
        auto *client=window.findChild<SshExecutor*>();QTRY_VERIFY_WITH_TIMEOUT(!client->busy(),10000);window.findChild<QLineEdit*>("sshRepositoryPath")->setText("/srv/repo");window.findChild<QPushButton*>("refreshSshRepository")->click();
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QListWidget*>("sshFiles")->count()==1,10000);QVERIFY(window.findChild<QPushButton*>("ssh_stage")->isEnabled());QVERIFY(window.windowFlags().testFlag(Qt::WindowTitleHint));QCOMPARE(window.findChild<QListWidget*>("sshRepositories")->count(),1);window.grab().save("ssh-workspace-screenshot.png");
        main.show();QVERIFY(!main.close());QVERIFY(main.isVisible());
        env.set("GITCANVAS_SSH_FAILURE","hang");client->request(profile,{{"action","status"}},[](bool,const QJsonObject &,const QString &){});
        QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(window.isVisible());QVERIFY(!window.close());
        client->disconnectTransport();QTRY_VERIFY_WITH_TIMEOUT(!client->busy(),10000);env.set("GITCANVAS_SSH_FAILURE",{});
        QVERIFY(window.close());QVERIFY(main.close());QSettings().clear();
    }
    void diagnosticsPrivacyAndRetention() {
        QTemporaryDir storage;QSettings().setValue("diagnostics/retain",false);GitClient git;Diagnostics diagnostics(&git,nullptr,storage.path());diagnostics.clear();
        const QString secret="ghp_1234567890abcdef";
        const auto text=Diagnostics::redact("Authorization: Bearer private-value\nhttps://alice:secret@example.org/repo password=hidden token=opaque "+secret+" alice@example.org "+QDir::homePath()+"/file");
        for(const auto &value:QStringList{"private-value","alice:secret","hidden","opaque",secret,"alice@example.org",QDir::homePath()})QVERIFY(!text.contains(value));
        for(int i=0;i<510;++i)diagnostics.recordAction("stageAllFiles");QCOMPARE(diagnostics.entries().size(),500);
        git.commandFinished(secret,false,128,35,"fatal: authentication failed "+secret,true);
        auto entry=diagnostics.entries().last().toObject();QCOMPARE(entry.value("code").toString(),QString("authentication_or_permission"));QVERIFY(entry.value("data_may_have_changed").toBool());QCOMPARE(entry.value("command").toString(),QString("other"));
        git.commandFinished("diff",true,1,5,"warning: informational stderr",false);QCOMPARE(diagnostics.entries().last().toObject().value("code").toString(),QString("success"));QCOMPARE(diagnostics.entries().last().toObject().value("exit_code").toInt(),1);
        diagnostics.recordAction(secret);QVERIFY(!QJsonDocument(diagnostics.report()).toJson().contains(secret.toUtf8()));
        QTemporaryDir dir;QString error;QVERIFY(diagnostics.exportReport(dir.filePath("report.json"),&error));QFile report(dir.filePath("report.json"));QVERIFY(report.open(QIODevice::ReadOnly));QCOMPARE(QJsonDocument::fromJson(report.readAll()).object().value("entries").toArray().size(),500);
        diagnostics.setRetention(true);const auto path=storage.filePath("recent.json");QVERIFY(QFile::exists(path));
        QJsonArray injected{QJsonObject{{"time",QDateTime::currentDateTimeUtc().addDays(-8).toString(Qt::ISODateWithMs)},{"kind","ui"},{"action",secret}},QJsonObject{{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"kind","git"},{"command",secret},{"exit_code",128},{"recovery",secret},{"raw",secret}}};
        QFile retained(path);QVERIFY(retained.open(QIODevice::WriteOnly));retained.write(QJsonDocument(QJsonObject{{"entries",injected}}).toJson());retained.close();
        Diagnostics restored(&git,nullptr,storage.path());QCOMPARE(restored.entries().size(),1);QVERIFY(!QJsonDocument(restored.report()).toJson().contains(secret.toUtf8()));
        restored.setRetention(false);QVERIFY(!QFile::exists(path));restored.clear();diagnostics.clear();
        QSignalSpy progress(&git,&GitClient::outputProgress);bool finished=false;QByteArray output;
        git.inspectEnvironment({"--version"},[&](bool ok,const QByteArray &bytes,const QString &){finished=ok;output=bytes;});QTRY_VERIFY_WITH_TIMEOUT(finished,10000);QVERIFY(!progress.isEmpty());QCOMPARE(progress.last()[0].toLongLong(),qint64(output.size()));
    }
    void languageAndSettings() {
        QSettings().clear();QCOMPARE(AppLanguage::effectiveLanguage("ko"),QString("ko"));QCOMPARE(AppLanguage::effectiveLanguage("en"),QString("en"));
        QCOMPARE(AppLanguage::effectiveLanguage("system"),QLocale::system().language()==QLocale::Korean?QString("ko"):QString("en"));
        AppLanguage translator;QVERIFY(qApp->installTranslator(&translator));
        MainWindow window;window.show();auto *tabs=window.findChild<QTabWidget*>("mainTabs");QCOMPARE(tabs->tabText(1),QString("History"));
        auto *settings=window.findChild<QPushButton*>("applicationSettings");QVERIFY(settings);QCOMPARE(settings->text(),QString("App settings / diagnostics"));
        bool inspected=false;QTimer::singleShot(100,&window,[&]{auto *dialog=window.findChild<QDialog*>("appSettingsDialog");if(!dialog)return;auto *choice=dialog->findChild<QComboBox*>("applicationLanguage");if(!choice)return;choice->setCurrentIndex(choice->findData("en"));inspected=dialog->windowFlags().testFlag(Qt::WindowTitleHint)&&dialog->findChild<QPlainTextEdit*>("diagnosticPreview")->isReadOnly();dialog->accept();});
        settings->click();QVERIFY(inspected);QCOMPARE(QSettings().value("ui/language").toString(),QString("en"));
        window.grab().save("language-settings-screenshot.png");qApp->removeTranslator(&translator);QSettings().clear();
    }
    void historyModelScale() {
        HistoryTable table;QAbstractItemModelTester tester(table.model(),QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVector<HistoryCommit> rows(10000);for(int i=0;i<rows.size();++i){rows[i].hash=QString::number(i);rows[i].subject="Commit "+QString::number(i);rows[i].body="Details";rows[i].date="2026-09-14T10:00:00+09:00";}
        table.replace(rows);QCOMPARE(table.rowCount(),10000);QCOMPARE(table.cellText(9999,1),QString("Commit 9999"));QCOMPARE(table.model()->index(9999,1).data(Qt::ToolTipRole).toString(),QString("Commit 9999\n\nDetails"));
        const auto previousLocale=QLocale();QLocale::setDefault(QLocale("ko_KR"));const auto koreanDate=table.cellText(0,4);QLocale::setDefault(QLocale("en_US"));const auto englishDate=table.cellText(0,4);QLocale::setDefault(previousLocale);QVERIFY(koreanDate!=englishDate);QVERIFY(!englishDate.isEmpty());
        table.resize(1000,500);table.show();table.setCurrentCell(9999,1);table.scrollTo(table.currentIndex());QCOMPARE(table.currentRow(),9999);QVERIFY(table.verticalScrollBar()->maximum()>0);
        table.replace({});QCOMPARE(table.rowCount(),0);QVERIFY(!table.currentIndex().isValid());
    }
    void mergeDropdownReview() {
        QSettings().clear();QTemporaryDir repo;
        auto git=[&](QStringList args){QProcess p;p.setWorkingDirectory(repo.path());p.start("git",args);if(!p.waitForFinished(15000))return false;return p.exitCode()==0;};
        QVERIFY(git({"init","-b","main"}));QVERIFY(git({"config","user.name","Dropdown Test"}));QVERIFY(git({"config","user.email","test@example.invalid"}));QVERIFY(git({"commit","--allow-empty","-m","base"}));QVERIFY(git({"branch","other"}));
        MainWindow window;window.show();window.openRepository(repo.path());auto *client=window.findChild<GitClient*>();QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);
        auto *panel=window.findChild<MergePanel*>();window.findChild<QTabWidget*>("mainTabs")->setCurrentWidget(panel);panel->openTarget("refs/heads/other");
        auto *target=panel->findChild<QComboBox*>("mergeTarget");QVERIFY(!target->isEditable());
        QTRY_VERIFY_WITH_TIMEOUT(target->currentData().toString()=="refs/heads/other"&&panel->findChild<QPushButton*>("executeMerge")->isEnabled(),15000);
        const auto selected=target->currentText();target->setCurrentText("not-a-branch");QCOMPARE(target->currentText(),selected);
        QCOMPARE(window.findChild<QTabWidget*>("mainTabs")->widget(0)->findChild<DiffWidget*>()->findChild<QComboBox*>("diffViewMode")->count(),4);
        TestEnvironment missingGit;missingGit.set("PATH",repo.path().toUtf8());panel->openTarget("refs/heads/main");
        QTRY_COMPARE_WITH_TIMEOUT(target->count(),0,15000);QVERIFY(!panel->findChild<QPushButton*>("executeMerge")->isEnabled());
    }
    void githubAccountsAndRepositories() {
        QTemporaryDir state;TestEnvironment env;env.set("GITCANVAS_FAKE_GH",state.path().toUtf8());env.set("GH_TOKEN","secret-environment-token");
        GithubClient client;client.setExecutable(QCoreApplication::applicationFilePath());
        QSignalSpy accounts(&client,&GithubClient::accountsReady),repos(&client,&GithubClient::repositoriesReady),changed(&client,&GithubClient::authChanged),messages(&client,&GithubClient::message),commands(&client,&GithubClient::commandStarted);
        client.accounts("github.com");QTRY_COMPARE_WITH_TIMEOUT(accounts.count(),1,15000);
        const auto values=qvariant_cast<QJsonArray>(accounts.last()[0]);QCOMPARE(values.size(),2);QVERIFY(values[0].toObject().value("active").toBool());QVERIFY(!QJsonDocument(values).toJson().contains("secret"));
        client.repositories("github.com","alice",1);QTRY_COMPARE_WITH_TIMEOUT(repos.count(),1,15000);QCOMPARE(qvariant_cast<QJsonArray>(repos.last()[0]).size(),100);
        client.repositories("github.com","alice",2);QTRY_COMPARE_WITH_TIMEOUT(repos.count(),2,15000);QCOMPARE(qvariant_cast<QJsonArray>(repos.last()[0]).size(),1);QCOMPARE(repos.last()[1].toInt(),2);
        client.switchAccount("github.com","bob");QTRY_COMPARE_WITH_TIMEOUT(changed.count(),1,15000);
        client.repositories("github.com","alice",1);QTRY_VERIFY_WITH_TIMEOUT(!client.busy()&&repos.count()==3,15000);QVERIFY(qvariant_cast<QJsonArray>(repos.last()[0]).isEmpty());
        client.repositories("github.com","bob",1);QTRY_COMPARE_WITH_TIMEOUT(repos.count(),4,15000);QVERIFY(qvariant_cast<QJsonArray>(repos.last()[0])[0].toObject().value("full_name").toString().startsWith("bob/"));
        const int before=commands.count();client.accounts("https://evil/path");QCOMPARE(commands.count(),before);
        client.setupGit("github.com");QCOMPARE(commands.count(),before); // inherited tokens cannot silently override Git credentials.
        QSignalSpy code(&client,&GithubClient::deviceCode),url(&client,&GithubClient::authorizationUrl);
        client.login("github.com");QTRY_VERIFY_WITH_TIMEOUT(!client.busy()&&changed.count()==2,15000);QVERIFY(!code.isEmpty());QCOMPARE(url.first()[0].toString(),QString("https://github.com/login/device"));
        env.set("GITCANVAS_GH_FAILURE","401");client.repositories("github.com","bob",1);QTRY_VERIFY_WITH_TIMEOUT(!client.busy(),15000);QVERIFY(!messages.isEmpty());
        for(const auto &row:messages)QVERIFY(!row[0].toString().contains("secret"));for(const auto &row:commands)QVERIFY(!row[0].toString().contains("secret"));
        env.set("GITCANVAS_GH_FAILURE","hang");client.accounts("github.com");QVERIFY(client.busy());client.cancel();QTRY_VERIFY_WITH_TIMEOUT(!client.busy(),5000);
        env.set("GITCANVAS_GH_FAILURE",{});client.setExecutable(state.filePath("missing-gh.exe"));client.check();QTRY_VERIFY_WITH_TIMEOUT(!client.busy(),5000);
        QSettings().remove("github/ghExecutable");
    }
    void pullRequestCollaboration() {
        QTemporaryDir state;TestEnvironment env;env.set("GITCANVAS_FAKE_GH",state.path().toUtf8());
        GithubClient client;client.setExecutable(QCoreApplication::applicationFilePath());GitClient git;
        PullRequests prs(&client,&git,"github.com","alice","alice/repo");
        QSignalSpy listed(&prs,&PullRequests::listed),loaded(&prs,&PullRequests::loaded),done(&prs,&PullRequests::completed),commands(&client,&GithubClient::commandStarted),branches(&prs,&PullRequests::branchesLoaded);
        auto detail=[&]{prs.detail(1);};
        auto writes=[&]{QFile f(state.filePath("writes.jsonl"));QList<QJsonObject> rows;if(f.open(QIODevice::ReadOnly))for(const auto &line:f.readAll().split('\n'))if(!line.isEmpty())rows.append(QJsonDocument::fromJson(line).object());return rows;};
        prs.list("open",1);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QCOMPARE(listed.count(),1);
        detail();QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QCOMPARE(loaded.count(),1);auto pr=loaded.last()[0].toJsonObject();QCOMPARE(pr.value("files").toArray().size(),101);QVERIFY(PullRequests::canMerge(pr));
        prs.branches("alice/repo");QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QCOMPARE(branches.count(),1);
        for(const auto &action:QStringList{"comment","approve","request_changes","reviewers"}){
            detail();QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);pr=loaded.last()[0].toJsonObject();
            prs.act(action,pr,action=="reviewers"?"carol, team:reviewers":"private-note\nreview body");QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(done.last()[0].toBool());
        }
        QCOMPARE(writes().size(),4);QCOMPARE(writes()[0].value("body").toObject().value("body").toString(),QString("private-note\nreview body"));QCOMPARE(writes()[1].value("body").toObject().value("commit_id").toString(),QString(40,'a'));QCOMPARE(writes()[2].value("body").toObject().value("event").toString(),QString("REQUEST_CHANGES"));QCOMPARE(writes()[3].value("body").toObject().value("team_reviewers").toArray().first().toString(),QString("reviewers"));
        for(const auto &row:commands)QVERIFY(!row[0].toString().contains("private-note"));
        detail();QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);pr=loaded.last()[0].toJsonObject();env.set("GITCANVAS_PR_HEAD",QByteArray(40,'c'));
        prs.act("merge",pr);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(!done.last()[0].toBool());QCOMPARE(writes().size(),4);env.set("GITCANVAS_PR_HEAD",{});
        for(const auto &check:QList<QByteArray>{"pending","failure"}){env.set("GITCANVAS_PR_CHECK",check);prs.act("merge",pr);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(!done.last()[0].toBool());QCOMPARE(writes().size(),4);}env.set("GITCANVAS_PR_CHECK",{});
        env.set("GITCANVAS_PR_MERGE_STATE","blocked");prs.act("merge",pr);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(!done.last()[0].toBool());QCOMPARE(writes().size(),4);env.set("GITCANVAS_PR_MERGE_STATE",{});
        prs.act("close",pr);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(done.last()[0].toBool());
        detail();QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);pr=loaded.last()[0].toJsonObject();QCOMPARE(pr.value("state").toString(),QString("closed"));
        prs.act("reopen",pr);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(done.last()[0].toBool());detail();QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);pr=loaded.last()[0].toJsonObject();
        env.set("GITCANVAS_PR_FAILURE","not-merged");prs.act("squash",pr);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(!done.last()[0].toBool());env.set("GITCANVAS_PR_FAILURE",{});
        prs.act("squash",pr);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(done.last()[0].toBool());QCOMPARE(writes().last().value("body").toObject().value("sha").toString(),QString(40,'a'));QCOMPARE(writes().last().value("body").toObject().value("merge_method").toString(),QString("squash"));
        prs.create("alice/repo","topic","main","New PR","Description",true);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(done.last()[0].toBool());QVERIFY(writes().last().value("body").toObject().value("draft").toBool());
        QSignalSpy preview(&prs,&PullRequests::previewReady),templates(&prs,&PullRequests::templateReady);
        prs.preview("bob/repo","topic","main");QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QCOMPARE(preview.count(),1);
        prs.loadTemplate(".github/pull_request_template.md");QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QCOMPARE(templates.count(),1);QCOMPARE(templates.first()[0].toString(),QString("## Summary\n"));
        const int beforeDuplicate=writes().size();env.set("GITCANVAS_PR_DUPLICATE","1");prs.create("alice/repo","topic","main","Duplicate","",false);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(!done.last()[0].toBool());QCOMPARE(writes().size(),beforeDuplicate);env.set("GITCANVAS_PR_DUPLICATE",{});
        for(const auto &action:QStringList{"ready","remove_reviewers","update_branch"}){
            prs.detail(2);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);pr=loaded.last()[0].toJsonObject();prs.act(action,pr,"carol");QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY2(done.last()[0].toBool(),qPrintable(action));
        }
        QCOMPARE(writes().last().value("body").toObject().value("expected_head_sha").toString(),QString(40,'a'));
        client.switchAccount("github.com","bob");QTRY_VERIFY_WITH_TIMEOUT(!client.busy(),15000);const int before=writes().size();prs.act("comment",pr,"do not post");QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(!done.last()[0].toBool());QCOMPARE(writes().size(),before);
        env.set("GITCANVAS_GH_FAILURE","hang");prs.list("open",1);QVERIFY(prs.busy());prs.cancel();QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),5000);QVERIFY(!done.last()[0].toBool());
        QSettings().remove("github/ghExecutable");
    }
    void pullRequestCheckout() {
        QTemporaryDir root;const auto folder=root.filePath("local"),remote=root.filePath("remote.git");QDir().mkpath(folder);QDir().mkpath(remote);
        auto run=[](const QString &cwd,const QStringList &args){QProcess p;p.setWorkingDirectory(cwd);p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR:")+p.readAllStandardError();return p.readAllStandardOutput();};
        auto write=[](const QString &path,const QByteArray &bytes){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
        QVERIFY(!run(folder,{"init","-b","main"}).startsWith("ERROR:"));run(folder,{"config","user.name","PR Test"});run(folder,{"config","user.email","pr@example.invalid"});
        QVERIFY(write(folder+"/base.txt","base\n"));run(folder,{"add","."});run(folder,{"commit","-m","base"});run(folder,{"switch","-c","topic"});QVERIFY(write(folder+"/incoming.txt","remote change\n"));run(folder,{"add","."});run(folder,{"commit","-m","topic"});const auto head=run(folder,{"rev-parse","HEAD"}).trimmed();QCOMPARE(head.size(),40);
        run(remote,{"init","--bare","-b","main"});QVERIFY(!run(folder,{"push",remote,"HEAD:refs/pull/1/head"}).startsWith("ERROR:"));run(folder,{"switch","main"});
        run(folder,{"remote","add","origin","https://github.com/alice/repo.git"});run(folder,{"config","url."+QUrl::fromLocalFile(remote).toString()+".insteadOf","https://github.com/alice/repo.git"});
        TestEnvironment env;env.set("GITCANVAS_FAKE_GH",root.path().toUtf8());env.set("GITCANVAS_PR_HEAD",head);
        GithubClient client;client.setExecutable(QCoreApplication::applicationFilePath());GitClient git;git.setRepositoryPath(folder);PullRequests prs(&client,&git,"github.com","alice","alice/repo");QSignalSpy loaded(&prs,&PullRequests::loaded),done(&prs,&PullRequests::completed),checked(&prs,&PullRequests::checkedOut);
        prs.detail(1);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QCOMPARE(loaded.count(),1);const auto pr=loaded.last()[0].toJsonObject();
        QVERIFY(write(folder+"/base.txt","local edit\n"));prs.checkout(pr);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(!done.last()[0].toBool());QCOMPARE(run(folder,{"branch","--show-current"}).trimmed(),QByteArray("main"));QVERIFY(write(folder+"/base.txt","base\n"));
        QVERIFY(write(folder+"/.git/info/exclude","incoming.txt\n"));QVERIFY(write(folder+"/incoming.txt","valuable ignored data\n"));
        prs.checkout(pr);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(!done.last()[0].toBool());QFile ignored(folder+"/incoming.txt");QVERIFY(ignored.open(QIODevice::ReadOnly));QCOMPARE(ignored.readAll(),QByteArray("valuable ignored data\n"));ignored.close();QVERIFY(ignored.remove());
        prs.checkout(pr);QTRY_VERIFY_WITH_TIMEOUT(!prs.busy(),15000);QVERIFY(done.last()[0].toBool());QCOMPARE(checked.count(),1);QCOMPARE(run(folder,{"rev-parse","HEAD"}).trimmed(),head);QVERIFY(run(folder,{"branch","--show-current"}).startsWith("pr/1-"));
        QSettings().remove("github/ghExecutable");
    }
    void pullRequestGui() {
        QTemporaryDir state;TestEnvironment env;env.set("GITCANVAS_FAKE_GH",state.path().toUtf8());GithubClient client;client.setExecutable(QCoreApplication::applicationFilePath());GitClient git;
        MainWindow window;window.show();PullRequestDialog dialog(&client,&git,"github.com","alice","alice/repo",&window);dialog.show();auto *list=dialog.findChild<QTableWidget*>("pullRequests");
        QTRY_VERIFY_WITH_TIMEOUT(list->rowCount()==1&&!client.busy(),15000);list->selectRow(0);auto *files=dialog.findChild<QTableWidget*>("prFiles");QTRY_VERIFY_WITH_TIMEOUT(files->rowCount()==101&&!client.busy(),15000);
        QVERIFY(dialog.findChild<QPushButton*>("mergePullRequest")->isEnabled());QCOMPARE(dialog.findChild<DiffWidget*>("prDiff")->findChild<QComboBox*>("diffViewMode")->count(),3);
        QVERIFY(dialog.findChild<QPlainTextEdit*>("prDescription")->toPlainText().contains("Existing comment"));
        QTimer::singleShot(0,[&]{auto *create=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(create);auto *base=create->findChild<QComboBox*>("prBaseBranch");QTRY_VERIFY_WITH_TIMEOUT(base->count()==2&&!client.busy(),15000);QVERIFY(!base->isEditable());env.set("GITCANVAS_GH_FAILURE","hang");create->findChild<QPushButton*>("loadPrBranches")->click();QVERIFY(client.busy());create->findChild<QPushButton*>("cancelNewPullRequest")->click();QTRY_VERIFY_WITH_TIMEOUT(!client.busy()&&create->findChild<QPushButton*>("loadPrBranches")->isEnabled(),5000);env.set("GITCANVAS_GH_FAILURE",{});create->reject();});dialog.findChild<QPushButton*>("createPullRequest")->click();
        dialog.findChild<QPushButton*>("reloadPullRequest")->click();QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QPushButton*>("mergePullRequest")->isEnabled(),15000);
        QVERIFY(dialog.grab().save("pull-request-screenshot.png"));dialog.reject();QSettings().remove("github/ghExecutable");
    }
    void githubSimpleLogin() {
        QTemporaryDir state;TestEnvironment env;env.set("GITCANVAS_FAKE_GH",state.path().toUtf8());
        for(const auto *key:{"GH_TOKEN","GITHUB_TOKEN","GH_ENTERPRISE_TOKEN","GITHUB_ENTERPRISE_TOKEN"})env.set(key,{});
        QSettings().clear();QSettings().setValue("github/ghExecutable",QCoreApplication::applicationFilePath());
        BrowserCapture browser;QDesktopServices::setUrlHandler("https",&browser,"open");
        struct RestoreHandler {~RestoreHandler(){QDesktopServices::unsetUrlHandler("https");}} restore;
        GitClient git;GithubPanel panel(&git);panel.show();
        auto *table=panel.findChild<QTableWidget*>("githubRepositories");
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),100,15000);
        QVERIFY(!panel.findChild<QLineEdit*>("githubExecutable")->isVisible());
        QVERIFY(!panel.findChild<QComboBox*>("githubAccounts")->isVisible());
        QSignalSpy commands(&panel,&GithubPanel::commandStarted);
        panel.findChild<QPushButton*>("loginGithub")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!panel.busy()&&table->rowCount()==100,15000);
        QCOMPARE(browser.urls.size(),1);QCOMPARE(browser.urls.first(),QUrl("https://github.com/login/device"));
        QStringList calls;for(const auto &row:commands)calls.append(row[0].toString());
        QVERIFY(calls[0].startsWith("gh auth login "));QVERIFY(calls[1].startsWith("gh auth setup-git "));QVERIFY(calls[2].startsWith("gh auth status "));
        QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);QVERIFY(dialog->windowFlags().testFlag(Qt::WindowTitleHint));QVERIFY(panel.findChild<QLineEdit*>("githubExecutable")->isVisible());panel.findChild<QCheckBox*>("githubAutoSetupGit")->setChecked(false);dialog->accept();});
        panel.findChild<QPushButton*>("githubAdvancedSettings")->click();
        commands.clear();panel.findChild<QPushButton*>("loginGithub")->click();QTRY_VERIFY_WITH_TIMEOUT(!panel.busy()&&table->rowCount()==100,15000);
        for(const auto &row:commands)QVERIFY(!row[0].toString().contains("setup-git"));
        panel.findChild<QCheckBox*>("githubAutoSetupGit")->setChecked(true);
        env.set("GH_TOKEN","fake-token");panel.findChild<QPushButton*>("loginGithub")->click();QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),15000);
        QCOMPARE(table->rowCount(),0);QVERIFY(!panel.findChild<QPushButton*>("cloneGithubRepo")->isEnabled());
        QSettings().clear();
    }
    void githubGui() {
        QTemporaryDir state;TestEnvironment env;env.set("GITCANVAS_FAKE_GH",state.path().toUtf8());
        QSettings().setValue("github/ghExecutable",QCoreApplication::applicationFilePath());
        MainWindow window;window.show();auto &panel=*window.findChild<GithubPanel*>();window.findChild<QTabWidget*>("mainTabs")->setCurrentWidget(&panel);
        auto *accounts=panel.findChild<QComboBox*>("githubAccounts");QTRY_COMPARE_WITH_TIMEOUT(accounts->count(),2,15000);
        auto *table=panel.findChild<QTableWidget*>("githubRepositories");QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),100,15000);
        panel.findChild<QPushButton*>("moreGithubRepos")->click();QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),101,15000);
        table->selectRow(0);QTimer::singleShot(0,[]{auto *dialog=qobject_cast<PullRequestDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);auto *list=dialog->findChild<QTableWidget*>("pullRequests");QTRY_VERIFY_WITH_TIMEOUT(list->rowCount()==1,15000);dialog->reject();});panel.findChild<QPushButton*>("openPullRequests")->click();
        table->selectRow(0);QTimer::singleShot(0,[]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);QVERIFY(dialog->findChild<QLineEdit*>("cloneUrl")->text().startsWith("https://github.com/alice/"));dialog->reject();});panel.findChild<QPushButton*>("cloneGithubRepo")->click();
        accounts->setCurrentIndex(1);QTimer::singleShot(0,[]{auto *dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());QVERIFY(dialog);dialog->done(QMessageBox::Yes);});panel.findChild<QPushButton*>("switchGithub")->click();QCOMPARE(table->rowCount(),0);
        QTRY_VERIFY_WITH_TIMEOUT(!panel.busy()&&accounts->currentData().toJsonObject().value("login").toString()=="bob",15000);
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),100,15000);QVERIFY(table->item(0,0)->text().startsWith("bob/"));
        QVERIFY(window.grab().save("github-screenshot.png"));
        panel.findChild<QLineEdit*>("githubHost")->setText("github.example.com");QCOMPARE(table->rowCount(),0);QCOMPARE(accounts->count(),0);
        QSettings().remove("github/ghExecutable");
    }
    void toolboxTagsWorktreesAndExpert() {
        QTemporaryDir root;QVERIFY(root.isValid());const auto repo=root.filePath("repo"),remote=root.filePath("remote.git");QDir().mkpath(repo);QDir().mkpath(remote);
        auto git=[&](const QString &cwd,QStringList args){QProcess p;p.setWorkingDirectory(cwd);p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput();};
        auto write=[&](const QString &path,const QByteArray &data){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();};
        git(repo,{"init","-b","main"});git(remote,{"init","--bare","-b","main"});git(repo,{"config","user.name","Tool Test"});git(repo,{"config","user.email","tools@example.invalid"});QVERIFY(write(repo+"/file.txt","base\n"));git(repo,{"add","."});git(repo,{"commit","-m","base"});git(repo,{"remote","add","origin",remote});
        GitClient client;client.setRepositoryPath(repo);ToolboxController controller(&client);ToolReview review;QString error;QByteArray output;bool done=false,success=false;
        auto prepare=[&](QString id,QMap<QString,QString> values){done=false;controller.review(id,values,[&](bool ok,ToolReview r,QString e){success=ok;review=r;error=e;done=true;});};
        auto execute=[&]{done=false;controller.execute(review,[&](bool ok,const QByteArray &out,const QString &e){success=ok;output=out;error=e;done=true;});};
        prepare("tag.annotate",{{"name","v1"},{"ref","HEAD"},{"message","release\n\ndetails"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(git(repo,{"cat-file","-t","v1"}).trimmed(),QByteArray("tag"));const auto tagOid=git(repo,{"rev-parse","v1"}).trimmed();
        prepare("tag.publish",{{"remote","origin"},{"name","v1"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(git(remote,{"rev-parse","refs/tags/v1"}).trimmed(),tagOid);
        prepare("tag.publish",{{"remote","origin"},{"name","v1"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);
        prepare("tag.remote-delete",{{"remote","origin"},{"name","v1"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));const auto head=git(repo,{"rev-parse","HEAD"}).trimmed();git(remote,{"update-ref","refs/tags/v1",QString::fromLatin1(head)});execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);QCOMPARE(git(remote,{"rev-parse","refs/tags/v1"}).trimmed(),head);
        prepare("tag.remote-delete",{{"remote","origin"},{"name","v1"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QVERIFY(git(remote,{"show-ref","--verify","refs/tags/v1"}).startsWith("ERROR"));
        prepare("tag.delete",{{"name","v1"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);QVERIFY(git(repo,{"for-each-ref","--format=%(objectname)","refs/gitcanvas/tool-backups"}).contains(tagOid));
        const auto worktree=root.filePath("linked tree");prepare("worktree.add",{{"path",worktree},{"name","linked"},{"ref","HEAD"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(git(worktree,{"branch","--show-current"}).trimmed(),QByteArray("linked"));
        QVERIFY(write(worktree+"/untracked","valuable"));prepare("worktree.remove",{{"path",worktree}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);QVERIFY(QFileInfo::exists(worktree+"/untracked"));QVERIFY(QFile::remove(worktree+"/untracked"));
        const auto excludes=root.filePath("ignore-patterns");QVERIFY(write(excludes,"ignored-cache\n"));git(repo,{"config","core.excludesFile",excludes});QVERIFY(write(worktree+"/ignored-cache","valuable ignored file"));prepare("worktree.remove",{{"path",worktree}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);QVERIFY(QFile::remove(worktree+"/ignored-cache"));git(repo,{"config","--unset","core.excludesFile"});
        prepare("worktree.lock",{{"path",worktree},{"message","offline disk"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);QVERIFY(git(repo,{"worktree","list","--porcelain"}).contains("locked offline disk"));
        prepare("worktree.unlock",{{"path",worktree}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);
        prepare("worktree.move",{{"path",worktree},{"destination",root.filePath("moved")}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QVERIFY(QFileInfo::exists(root.filePath("moved/file.txt")));
        prepare("worktree.remove",{{"path",repo}});QTRY_VERIFY(done);QVERIFY(!success);
        prepare("worktree.remove",{{"path",root.filePath("moved")}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QVERIFY(!QFileInfo::exists(root.filePath("moved")));
        const auto literal=QString("$(echo not-a-shell); & literal value");prepare("expert",{{"arguments","config\n--local\n--replace-all\ngitcanvas.literal\n"+literal}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(QString::fromUtf8(git(repo,{"config","gitcanvas.literal"})).trimmed(),literal);
        prepare("config.set",{{"scope","local"},{"key","gitcanvas.literal"},{"value","new"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);git(repo,{"config","gitcanvas.changed","true"});execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);
        prepare("expert",{{"arguments","push\norigin\nmain"}});QTRY_VERIFY(done);QVERIFY(!success);
        const auto logFile=root.filePath("log.txt");bool conservative=false;connect(&client,&GitClient::commandStarted,this,[&](const QString &command){if(command.startsWith("git log <reviewed"))conservative=!client.canCancel();});prepare("expert",{{"arguments","log\n--oneline\n--output="+logFile}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);QVERIFY(conservative);QVERIFY(QFileInfo(logFile).size()>0);
        prepare("expert",{{"arguments","hash-object\n-w\n--stdin"},{"stdin","standard input\n"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));const auto object=QString::fromLatin1(output.split('\n').first());QCOMPARE(git(repo,{"cat-file","-p",object}),QByteArray("standard input\n"));
        prepare("config.set",{{"scope","worktree"},{"key","gitcanvas.test"},{"value","must not fall back"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);QVERIFY(git(repo,{"config","--local","--get","gitcanvas.test"}).startsWith("ERROR"));
        prepare("expert",{{"arguments","log\n--oneline\nHEAD"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);
        GitToolbox panel(&client);panel.resize(1100,760);panel.show();auto *choice=panel.findChild<QComboBox*>("gitToolChoice");choice->setCurrentIndex(choice->findData("worktree.list"));auto *preview=panel.findChild<QPushButton*>("previewGitTool");QVERIFY(preview->isEnabled());preview->click();auto *table=panel.findChild<QTableWidget*>("gitToolTable");QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),1,15000);QSignalSpy open(&panel,&GitToolbox::openWorktree);table->selectRow(0);panel.findChild<QPushButton*>("openToolWorktree")->click();QCOMPARE(open.count(),1);QCOMPARE(open.at(0).at(0).toString(),QDir::fromNativeSeparators(repo));QVERIFY(panel.grab().save("toolbox-screenshot.png"));
        choice->setCurrentIndex(choice->findData("config.set"));panel.findChild<QLineEdit*>("gitTool_key")->setText("gitcanvas.gui");panel.findChild<QLineEdit*>("gitTool_value")->setText("from form");preview->click();auto *executeButton=panel.findChild<QPushButton*>("executeGitTool");QTRY_VERIFY_WITH_TIMEOUT(executeButton->isEnabled(),15000);bool titled=false;QTimer accept;accept.setInterval(10);connect(&accept,&QTimer::timeout,&panel,[&]{if(auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){titled=box->windowFlags().testFlag(Qt::WindowTitleHint);box->done(QMessageBox::Yes);}});accept.start();QSignalSpy changed(&panel,&GitToolbox::repositoryChanged);executeButton->click();QTRY_COMPARE_WITH_TIMEOUT(changed.count(),1,15000);accept.stop();QVERIFY(titled);QCOMPARE(git(repo,{"config","--get","gitcanvas.gui"}).trimmed(),QByteArray("from form"));
    }

    void toolboxBisectExportsAndMaintenance() {
        QTemporaryDir root;QVERIFY(root.isValid());const auto repo=root.filePath("repo");QDir().mkpath(repo);
        auto git=[&](QStringList args){QProcess p;p.setWorkingDirectory(repo);p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput();};
        auto write=[&](const QString &path,const QByteArray &data){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size();};
        git({"init","-b","main"});git({"config","user.name","Export Test"});git({"config","user.email","export@example.invalid"});git({"config","core.autocrlf","false"});for(int i=0;i<4;++i){QVERIFY(write(repo+"/file.txt",QByteArray::number(i)+"\n"));git({"add","."});git({"commit","-m",QString::number(i)});}
        GitClient client;client.setRepositoryPath(repo);ToolboxController controller(&client);ToolReview review;bool done=false,success=false;QString error;QByteArray output;
        auto prepare=[&](QString id,QMap<QString,QString> values=QMap<QString,QString>{}){done=false;controller.review(id,values,[&](bool ok,ToolReview r,QString e){success=ok;review=r;error=e;done=true;});};
        auto execute=[&]{done=false;controller.execute(review,[&](bool ok,const QByteArray &out,const QString &e){success=ok;output=out;error=e;done=true;});};
        const auto head=git({"rev-parse","HEAD"});prepare("bisect.start",{{"bad","HEAD"},{"good","HEAD~3"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"rev-parse","HEAD"}),head);QVERIFY(!git({"rev-parse","BISECT_HEAD"}).startsWith("ERROR"));
        prepare("export.archive",{{"path",root.filePath("bisect-candidate.zip")},{"ref","BISECT_HEAD"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"rev-parse","HEAD"}),head);
        prepare("config.set",{{"scope","local"},{"key","gitcanvas.test"},{"value","blocked"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);
        prepare("bisect.good");QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));prepare("bisect.reset");QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);QCOMPARE(git({"rev-parse","HEAD"}),head);
        const auto zip=root.filePath("snapshot.zip"),bundle=root.filePath("backup.bundle");prepare("export.archive",{{"ref","HEAD"},{"path",zip}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QFile archive(zip);QVERIFY(archive.open(QIODevice::ReadOnly));QCOMPARE(archive.read(2),QByteArray("PK"));archive.close();prepare("export.archive",{{"ref","HEAD"},{"path",zip}});QTRY_VERIFY(done);QVERIFY(!success);
        prepare("export.bundle",{{"path",bundle}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));prepare("bundle.verify",{{"path",bundle}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));
        const auto patchDir=root.filePath("patches");prepare("export.patch",{{"range","HEAD~1..HEAD"},{"path",patchDir}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(QDir(patchDir).entryList({"*.patch"},QDir::Files).size(),1);
        const auto patch=root.filePath("change.patch");QVERIFY(write(repo+"/file.txt","changed\n"));QVERIFY(write(patch,git({"diff","--no-color"})));git({"restore","file.txt"});prepare("patch.apply",{{"path",patch}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);QFile file(patch);QVERIFY(file.open(QIODevice::Append));file.write("\n");file.close();execute();QTRY_VERIFY(done);QVERIFY(!success);
        prepare("patch.apply",{{"path",patch}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"show",":file.txt"}),QByteArray("changed\n"));git({"commit","-m","applied"});
        for(const auto &id:QStringList{"health.objects","health.fsck","health.repack","health.gc","health.maintenance","worktree.prune-check","commands","submodule.status"}){prepare(id,id=="health.maintenance"?QMap<QString,QString>{{"task","commit-graph"}}:QMap<QString,QString>{});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));execute();QTRY_VERIFY_WITH_TIMEOUT(done,20000);QVERIFY2(success,qPrintable(id+": "+error));}
        git({"switch","-c","mail-import","HEAD~2"});const auto mail=QDir(patchDir).filePath(QDir(patchDir).entryList({"*.patch"},QDir::Files).first());prepare("patch.am",{{"path",mail}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"log","-1","--format=%s"}).trimmed(),QByteArray("3"));git({"switch","main"});
        if(!git({"lfs","version"}).startsWith("ERROR")){prepare("lfs.track",{{"pattern","*.psd"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QFile attributes(repo+"/.gitattributes");QVERIFY(attributes.open(QIODevice::ReadOnly));QVERIFY(attributes.readAll().contains("*.psd filter=lfs"));attributes.close();prepare("lfs.untrack",{{"pattern","*.psd"}});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);execute();QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));}
        QVERIFY(!git({"fsck","--full"}).startsWith("ERROR"));
    }

    void rewriteAndRecovery() {
        QTemporaryDir repo;QVERIFY(repo.isValid());
        auto git=[&](QStringList args){QProcess p;p.setWorkingDirectory(repo.path());p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput();};
        auto write=[&](QString path,QByteArray data){QFile f(repo.filePath(path));return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();};
        git({"init","-b","main"});git({"config","core.autocrlf","false"});git({"config","user.name","Rewrite Test"});git({"config","user.email","rewrite@example.invalid"});
        for(const auto &name:QStringList{"base","a","b","c","d"}){QVERIFY(write(name,name.toUtf8()+"\n"));git({"add","."});QVERIFY(!git({"commit","-m",name}).startsWith("ERROR"));}
        const auto original=QString::fromUtf8(git({"rev-parse","HEAD"}).trimmed());
        GitClient client;client.setRepositoryPath(repo.path());RewriteController controller(&client);RebasePlan plan;bool done=false,success=false;QString error;
        auto planned=[&](bool ok,RebasePlan p,QString e){done=true;success=ok;plan=p;error=e;};auto complete=[&](bool ok,const QByteArray &,const QString &e){done=true;success=ok;error=e;};
        controller.plan("HEAD~4",false,planned);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(plan.entries.size(),4);
        auto invalid=plan;invalid.entries[0].action="fixup";done=false;controller.start(invalid,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);
        plan.entries.swapItemsAt(0,1);plan.entries[0].action="reword";plan.entries[0].message="renamed b\n\nnew body";plan.entries[1].action="fixup";plan.entries[3].action="drop";
        done=false;controller.start(plan,complete);QTRY_VERIFY_WITH_TIMEOUT(done,20000);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"rev-list","--count","HEAD"}).trimmed(),QByteArray("3"));QCOMPARE(git({"log","-1","--format=%s","HEAD~1"}).trimmed(),QByteArray("renamed b"));QVERIFY(!QFileInfo::exists(repo.filePath("d")));QVERIFY(QFileInfo::exists(repo.filePath("a")));
        QVERIFY(git({"for-each-ref","--format=%(objectname)","refs/gitcanvas/rebase-backups"}).contains(original.toUtf8()));
        done=false;controller.plan("HEAD~2",false,planned);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));plan.entries[0].action="edit";plan.entries[1].action="reword";plan.entries[1].message="after edit";
        done=false;controller.start(plan,complete);QTRY_VERIFY_WITH_TIMEOUT(done,20000);QVERIFY2(success,qPrintable(error));QVERIFY(client.operationState().editStop);QCOMPARE(client.operationState().operation,QString("rebase"));
        const auto editingHead=QString::fromUtf8(git({"rev-parse","HEAD"}).trimmed());done=false;client.commitWithOptions("edited b",true,false,editingHead,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));
        done=false;client.loadOperationState([&](bool ok,const QString &e){success=ok;error=e;done=true;});QTRY_VERIFY(done);QVERIFY(success);done=false;client.recoverOperation(client.operationState().fingerprint,false,complete);QTRY_VERIFY_WITH_TIMEOUT(done,20000);QVERIFY2(success,qPrintable(error));QVERIFY(client.operationState().operation.isEmpty());QCOMPARE(git({"log","-1","--format=%s"}).trimmed(),QByteArray("after edit"));
        QList<RecoveryEntry> entries;done=false;controller.recoveryLog(200,[&](bool ok,QList<RecoveryEntry> result,QString e){done=true;success=ok;entries=result;error=e;});QTRY_VERIFY(done);QVERIFY(success);QVERIFY(entries.size()>4);
        RecoveryEntry originalEntry;for(const auto &entry:entries)if(entry.backup&&entry.oid==original)originalEntry=entry;QVERIFY(originalEntry.backup);
        auto fingerprint=client.operationState().fingerprint;done=false;controller.recover(originalEntry,"branch","rescued",repo.path(),fingerprint,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"rev-parse","rescued"}).trimmed(),original.toUtf8());
        done=false;controller.recover(originalEntry,"reset",{},repo.path(),fingerprint,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"rev-parse","HEAD"}).trimmed(),original.toUtf8());
        done=false;controller.recover(originalEntry,"delete-ref",{},repo.path(),fingerprint,complete);QTRY_VERIFY(done);QVERIFY(!success); // stale state
        done=false;controller.plan({},true,planned);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);plan.entries[1].action="squash";done=false;controller.start(plan,complete);QTRY_VERIFY_WITH_TIMEOUT(done,20000);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"rev-list","--count","HEAD"}).trimmed(),QByteArray("4"));QVERIFY(git({"log","--format=%B"}).contains("base\n\na"));
        done=false;controller.plan("HEAD~1",false,planned);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);QVERIFY(write("dirty","keep"));done=false;controller.start(plan,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);QVERIFY(QFile::remove(repo.filePath("dirty")));
        done=false;controller.plan("HEAD~1",false,planned);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);plan.entries[0].action="edit";const auto beforeAbort=git({"rev-parse","HEAD"});done=false;controller.start(plan,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);done=false;client.recoverOperation(client.operationState().fingerprint,true,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"rev-parse","HEAD"}),beforeAbort);
        QVERIFY(write("a","stashed data\n"));git({"stash","push","-m","recover me"});const auto stashOid=QString::fromUtf8(git({"rev-parse","refs/stash"}).trimmed());const QString stashRef="refs/gitcanvas/stash-backups/test";git({"update-ref",stashRef,stashOid});git({"stash","drop"});
        done=false;client.loadOperationState([&](bool ok,const QString &){done=true;success=ok;});QTRY_VERIFY(done);QVERIFY(success);fingerprint=client.operationState().fingerprint;RecoveryEntry stashEntry{stashRef,stashOid,{},{},true};done=false;controller.recover(stashEntry,"stash",{},repo.path(),fingerprint,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"rev-parse","refs/stash"}).trimmed(),stashOid.toUtf8());
        done=false;controller.recover(stashEntry,"delete-ref",{},repo.path(),fingerprint,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QVERIFY(git({"show-ref","--verify",stashRef}).startsWith("ERROR"));
        git({"rm","d"});git({"commit","-m","remove d"});QVERIFY(write(".git/info/exclude","d\n"));QVERIFY(write("d","ignored valuable data\n"));done=false;controller.plan("HEAD~2",false,planned);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);done=false;controller.start(plan,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);RecoveryEntry restoreD{"HEAD",original,{},{},false};done=false;controller.recover(restoreD,"reset",{},repo.path(),client.operationState().fingerprint,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);QFile ignored(repo.filePath("d"));QVERIFY(ignored.open(QIODevice::ReadOnly));QCOMPARE(ignored.readAll(),QByteArray("ignored valuable data\n"));ignored.close();
        RewritePanel panel(&client);panel.resize(1100,780);panel.show();panel.openBase("HEAD~2");auto *table=panel.findChild<QTableWidget*>("rebasePlan");QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),2,15000);table->selectRow(1);panel.findChild<QPushButton*>("rebaseMoveUp")->click();QCOMPARE(table->currentRow(),0);QVERIFY(panel.grab().save("rewrite-screenshot.png"));
    }

    void advancedCommitAndLines() {
        QTemporaryDir repo;QVERIFY(repo.isValid());
        auto git=[&](QStringList args){QProcess p;p.setWorkingDirectory(repo.path());p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput();};
        auto write=[&](QString path,QByteArray data){QFile f(repo.filePath(path));return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();};
        git({"init","-b","main"});git({"config","core.autocrlf","false"});git({"config","user.name","Original"});git({"config","user.email","original@example.invalid"});
        QVERIFY(write("file.txt","one\ntwo\nthree\nfour\n"));git({"add","."});git({"commit","-m","original"});const auto oldHead=QString::fromUtf8(git({"rev-parse","HEAD"}).trimmed());
        git({"config","user.name","New Committer"});git({"config","user.email","new@example.invalid"});
        GitClient client;client.setRepositoryPath(repo.path());bool done=false,success=false;QString error;
        auto complete=[&](bool ok,const QByteArray &,const QString &e){success=ok;error=e;done=true;};
        client.commitWithOptions("amended\n\nbody",true,true,oldHead,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));
        QCOMPARE(git({"log","-1","--format=%an"}).trimmed(),QByteArray("Original"));QVERIFY(git({"log","-1","--format=%B"}).contains("Signed-off-by: New Committer <new@example.invalid>"));QCOMPARE(git({"rev-list","--count","HEAD"}).trimmed(),QByteArray("1"));QVERIFY(git({"for-each-ref","--format=%(objectname)","refs/gitcanvas/amend-backups"}).contains(oldHead.toUtf8()));
        done=false;client.commitWithOptions("stale",true,false,oldHead,complete);QTRY_VERIFY(done);QVERIFY(!success);QCOMPARE(git({"log","-1","--format=%s"}).trimmed(),QByteArray("amended"));
        QVERIFY(write("file.txt","ONE\ntwo\nTHREE\nfour\n"));const auto patch=git({"diff","--no-color","--","file.txt"});QSet<int> selected;const auto rows=patch.split('\n');for(int i=0;i<rows.size();++i)if(rows[i]=="-one"||rows[i]=="+ONE")selected.insert(i);
        const auto partial=DiffWidget::selectedLinesPatch(patch,selected,false);QVERIFY(!partial.isEmpty());done=false;client.applyPatch(partial,false,complete);QTRY_VERIFY(done);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"show",":file.txt"}),QByteArray("ONE\ntwo\nthree\nfour\n"));
        const auto staged=git({"diff","--cached","--","file.txt"});selected.clear();const auto stagedRows=staged.split('\n');for(int i=0;i<stagedRows.size();++i)if(stagedRows[i]=="-one"||stagedRows[i]=="+ONE")selected.insert(i);done=false;client.applyPatch(DiffWidget::selectedLinesPatch(staged,selected,true),false,complete);QTRY_VERIFY(done);QVERIFY2(success,qPrintable(error));QVERIFY(git({"diff","--cached"}).isEmpty());
        // New files get a real diff without adding intent-to-add entries to the index.
        QVERIFY(write("new.txt","alpha\nbeta\ngamma\n"));DiffWidget diff(&client);diff.show();QSignalSpy applied(&diff,&DiffWidget::indexChanged);diff.loadWorking("new.txt",false);QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy(),15000);
        auto *lines=diff.findChild<QListWidget*>("diffLineSelection");auto *apply=diff.findChild<QPushButton*>("applyDiffLines");QVERIFY(apply->isEnabled());for(int i=0;i<lines->count();++i)if(lines->item(i)->text()=="+beta")lines->item(i)->setCheckState(Qt::Checked);apply->click();QTRY_COMPARE_WITH_TIMEOUT(applied.count(),1,15000);QCOMPARE(git({"show",":new.txt"}),QByteArray("beta\n"));
        diff.loadWorking("new.txt",true);QTRY_VERIFY(!client.isBusy());for(int i=0;i<lines->count();++i)if(lines->item(i)->text()=="+beta")lines->item(i)->setCheckState(Qt::Checked);apply->click();QTRY_COMPARE_WITH_TIMEOUT(applied.count(),2,15000);QVERIFY(git({"ls-files","new.txt"}).isEmpty());
        QVERIFY(QFile::remove(repo.filePath("file.txt")));const auto deleted=git({"diff","--","file.txt"});selected.clear();const auto deletedRows=deleted.split('\n');for(int i=0;i<deletedRows.size();++i)if(deletedRows[i]=="-two")selected.insert(i);done=false;client.applyPatch(DiffWidget::selectedLinesPatch(deleted,selected,false),false,complete);QTRY_VERIFY(done);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"show",":file.txt"}),QByteArray("one\nthree\nfour\n"));QVERIFY(!QFileInfo::exists(repo.filePath("file.txt")));
        QSettings().clear();MainWindow window;window.show();window.openRepository(repo.path());auto *amend=window.findChild<QCheckBox*>("amendCommit");QTRY_VERIFY_WITH_TIMEOUT(amend->isEnabled(),15000);amend->setChecked(true);auto *commit=window.findChild<QPushButton*>("primary");QTRY_VERIFY_WITH_TIMEOUT(commit->isEnabled(),15000);QCOMPARE(window.findChild<QLineEdit*>("commitTitle")->text(),QString("amended"));window.findChild<QLineEdit*>("commitTitle")->setText("GUI amend");window.findChild<QCheckBox*>("signOffCommit")->setChecked(true);
        QTimer confirmations;confirmations.setInterval(10);connect(&confirmations,&QTimer::timeout,&window,[]{if(auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->done(QMessageBox::Yes);});confirmations.start();commit->click();QTRY_VERIFY_WITH_TIMEOUT(!amend->isChecked(),15000);QCOMPARE(git({"log","-1","--format=%s"}).trimmed(),QByteArray("GUI amend"));QVERIFY(git({"log","-1","--format=%B"}).contains("Signed-off-by: New Committer"));confirmations.stop();QTRY_VERIFY_WITH_TIMEOUT(!window.findChild<GitClient*>()->isBusy(),15000);
    }

    void mergeAndConflictResolution() {
        QTemporaryDir repo;QVERIFY(repo.isValid());
        auto git=[&](QStringList args){QProcess p;p.setWorkingDirectory(repo.path());p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput();};
        auto write=[&](const QByteArray &data){QFile f(repo.filePath("conflict.txt"));return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();};
        git({"init","-b","main"});git({"config","core.autocrlf","false"});git({"config","user.name","Merge Test"});git({"config","user.email","merge@example.invalid"});QVERIFY(write("base\n"));git({"add","."});git({"commit","-m","base"});git({"switch","-c","other"});QVERIFY(write("theirs\n"));git({"commit","-am","theirs"});git({"switch","main"});QVERIFY(write("ours\n"));git({"commit","-am","ours"});
        GitClient client;client.setRepositoryPath(repo.path());MergeController controller(&client);MergePreview preview;ConflictFile conflict;QString error;bool done=false,success=false;auto complete=[&](bool ok,const QByteArray &,const QString &e){done=true;success=ok;error=e;};
        controller.preview("other",[&](bool ok,MergePreview p,QString e){success=ok;preview=p;error=e;done=true;});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));QVERIFY(preview.comparison.contains("theirs"));
        QVERIFY(write("external\n"));done=false;controller.merge(preview,complete);QTRY_VERIFY(done);QVERIFY(!success);QVERIFY(!QFileInfo::exists(repo.filePath(".git/MERGE_HEAD")));QVERIFY(write("ours\n"));
        done=false;controller.preview("other",[&](bool ok,MergePreview p,QString e){success=ok;preview=p;error=e;done=true;});QTRY_VERIFY(done);QVERIFY(success);done=false;controller.merge(preview,complete);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);QVERIFY(client.operationState().conflicts.contains("conflict.txt"));
        done=false;controller.loadConflict("conflict.txt",[&](bool ok,ConflictFile f,QString e){success=ok;conflict=f;error=e;done=true;});QTRY_VERIFY(done);QVERIFY2(success,qPrintable(error));QVERIFY(conflict.text);QCOMPARE(conflict.sides[0],QByteArray("base\n"));QCOMPARE(conflict.sides[1],QByteArray("ours\n"));QCOMPARE(conflict.sides[2],QByteArray("theirs\n"));
        done=false;controller.resolve(conflict,"edit",conflict.working,complete);QTRY_VERIFY(done);QVERIFY(!success);QVERIFY(write("external resolution\n"));done=false;controller.resolve(conflict,"ours",{},complete);QTRY_VERIFY(done);QVERIFY(!success);QVERIFY(write(conflict.working));
        // The GUI chooses one conflict block, saves only this path, then continues the merge.
        {
        QSettings().clear();MainWindow window;window.show();window.openRepository(repo.path());auto *uiClient=window.findChild<GitClient*>();QTRY_VERIFY_WITH_TIMEOUT(!uiClient->isBusy(),15000);auto &panel=*window.findChild<MergePanel*>();window.findChild<QTabWidget*>("mainTabs")->setCurrentIndex(3);window.findChild<QPushButton*>("openConflictEditor")->click();QCOMPARE(window.findChild<QTabWidget*>("mainTabs")->currentWidget(),&panel);QVERIFY(panel.findChildren<QTabWidget*>().isEmpty());auto *files=panel.findChild<QListWidget*>("mergeConflictFiles");QCOMPARE(files->count(),1);files->setCurrentRow(0);
        auto *save=panel.findChild<QPushButton*>("saveConflictResult");QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(),15000);auto *result=panel.findChild<QPlainTextEdit*>("conflictResult");auto *editor=panel.findChild<QDialog*>("mergeEditorDialog");QVERIFY(editor);QVERIFY(editor->isVisible());QVERIFY(editor->windowFlags().testFlag(Qt::WindowTitleHint));
        QVERIFY(!panel.findChild<QPushButton*>("resolve_base"));
        auto *columns=panel.findChild<QSplitter*>("conflictThreeWay");QVERIFY(columns);QCOMPARE(columns->count(),3);
        QVERIFY(columns->widget(0)->isAncestorOf(panel.findChild<QPlainTextEdit*>("conflictOurs")));
        QVERIFY(columns->widget(1)->isAncestorOf(result));QVERIFY(columns->widget(2)->isAncestorOf(panel.findChild<QPlainTextEdit*>("conflictTheirs")));
        result->setPlainText("ours\n");result->document()->setModified(true);
        QTimer::singleShot(0,[]{auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());QVERIFY(box);box->done(QMessageBox::No);});
        panel.findChild<QPushButton*>("closeConflictEditor")->click();QVERIFY(editor->isVisible());QVERIFY(result->document()->isModified());
        editor->grab().save("merge-screenshot.png");
        QTimer confirmations;confirmations.setInterval(10);bool titled=false;connect(&confirmations,&QTimer::timeout,&panel,[&]{if(auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){titled=box->windowFlags().testFlag(Qt::WindowTitleHint);box->done(QMessageBox::Yes);}});confirmations.start();
        save->click();QTRY_VERIFY_WITH_TIMEOUT(uiClient->operationState().conflicts.isEmpty()&&!uiClient->isBusy(),15000);QVERIFY(titled);QCOMPARE(git({"show",":conflict.txt"}),QByteArray("ours\n"));QVERIFY(!QDir(repo.filePath(".git/gitcanvas/conflict-backups")).entryList(QDir::Dirs|QDir::NoDotAndDotDot).isEmpty());
        auto *continueButton=panel.findChild<QPushButton*>("mergeContinue");QTRY_VERIFY(continueButton->isEnabled());continueButton->click();QTRY_VERIFY_WITH_TIMEOUT(uiClient->operationState().operation.isEmpty()&&!uiClient->isBusy(),15000);QCOMPARE(git({"rev-list","--parents","-1","HEAD"}).trimmed().split(' ').size(),3);confirmations.stop();window.hide();
        }
        // Delete/modify conflict: an absent side means deletion, with backup retained.
        git({"switch","-c","delete-side"});git({"rm","conflict.txt"});git({"commit","-m","delete"});git({"switch","main"});QVERIFY(write("modified\n"));git({"commit","-am","modify"});git({"merge","--no-edit","delete-side"});
        done=false;controller.loadConflict("conflict.txt",[&](bool ok,ConflictFile f,QString e){success=ok;conflict=f;error=e;done=true;});QTRY_VERIFY(done);QVERIFY(success);QVERIFY(!conflict.present[2]);done=false;controller.resolve(conflict,"theirs",{},complete);QTRY_VERIFY(done);QVERIFY2(success,qPrintable(error));QVERIFY(!QFileInfo::exists(repo.filePath("conflict.txt")));QVERIFY(client.operationState().conflicts.isEmpty());
        git({"merge","--abort"});QVERIFY(write(QByteArray("base\0data",9)));git({"commit","-am","binary base"});git({"branch","binary-other"});QVERIFY(write(QByteArray("ours\0data",9)));git({"commit","-am","binary ours"});git({"switch","binary-other"});QVERIFY(write(QByteArray("theirs\0data",11)));git({"commit","-am","binary theirs"});git({"switch","main"});git({"merge","--no-edit","binary-other"});
        done=false;controller.loadConflict("conflict.txt",[&](bool ok,ConflictFile f,QString e){success=ok;conflict=f;error=e;done=true;});QTRY_VERIFY(done);QVERIFY2(success,qPrintable(error));QVERIFY(!conflict.text);done=false;controller.resolve(conflict,"edit","replacement",complete);QTRY_VERIFY(done);QVERIFY(!success);done=false;controller.resolve(conflict,"theirs",{},complete);QTRY_VERIFY(done);QVERIFY2(success,qPrintable(error));QCOMPARE(git({"show",":conflict.txt"}),QByteArray("theirs\0data",11));
    }

    void stashAndWorktree() {
        QTemporaryDir repo;QVERIFY(repo.isValid());
        auto git=[&](QStringList args) {QProcess p;p.setWorkingDirectory(repo.path());p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput();};
        auto write=[&](const QString &path,const QByteArray &data){QFile f(repo.filePath(path));return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();};
        auto read=[&](const QString &path){QFile f(repo.filePath(path));if(!f.open(QIODevice::ReadOnly))return QByteArray();return f.readAll();};
        QVERIFY(!git({"init","-b","main"}).startsWith("ERROR"));git({"config","user.name","Stash Test"});git({"config","user.email","test@example.invalid"});git({"config","core.autocrlf","false"});
        QVERIFY(write("tracked.txt","base\n"));QVERIFY(write(".gitignore","*.ignored\n"));git({"add","."});git({"commit","-m","base"});
        GitClient client;client.setRepositoryPath(repo.path());WorktreeController controller(&client);WorktreeReview review;QString error;QByteArray output;
        auto wait=[](bool &done){QElapsedTimer timer;timer.start();while(!done&&timer.elapsed()<15000)QTest::qWait(10);return done;};
        auto snapshot=[&]{bool done=false,success=false;controller.review([&](bool ok,WorktreeReview value,QString e){success=ok;review=value;error=e;done=true;});return wait(done)&&success;};
        auto run=[&](const QString &action,QStringList paths=QStringList{},QString value=QString{},QString oid=QString{}){bool done=false,success=false;controller.execute(review,action,paths,value,oid,[&](bool ok,const QByteArray &out,const QString &e){success=ok;output=out;error=e;done=true;});return wait(done)&&success;};
        auto oid=[&]{return QString::fromUtf8(git({"rev-parse","refs/stash"}).trimmed());};
        QVERIFY(write("tracked.txt","staged\n"));git({"add","tracked.txt"});QVERIFY(write("tracked.txt","working\n"));
        QVERIFY(write(QString::fromUtf8("새 파일.txt"),"new\n"));QVERIFY(write("keep.ignored","secret\n"));
        QVERIFY(snapshot());QVERIFY2(run("save-untracked",{},"checkpoint"),qPrintable(error));const auto first=oid();
        QCOMPARE(read("tracked.txt"),QByteArray("base\n"));QVERIFY(!QFileInfo::exists(repo.filePath(QString::fromUtf8("새 파일.txt"))));QCOMPARE(read("keep.ignored"),QByteArray("secret\n"));
        QVERIFY(snapshot());QVERIFY2(run("apply-index",{},{},first),qPrintable(error));QCOMPARE(read("tracked.txt"),QByteArray("working\n"));QCOMPARE(git({"show",":tracked.txt"}),QByteArray("staged\n"));QCOMPARE(read(QString::fromUtf8("새 파일.txt")),QByteArray("new\n"));QCOMPARE(oid(),first);
        QVERIFY(snapshot());QVERIFY(run("restore",{"tracked.txt"}));QCOMPARE(read("tracked.txt"),QByteArray("staged\n"));QCOMPARE(git({"show",":tracked.txt"}),QByteArray("staged\n"));
        const auto backup=QString::fromUtf8(output).section(QString::fromUtf8("파일 복구 폴더: "),-1);QVERIFY(QFileInfo::exists(QDir(backup).filePath("manifest.json")));QFile saved(QDir(backup).filePath("0.bin"));QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.readAll(),QByteArray("working\n"));QVERIFY(QFileInfo::exists(QDir(backup).filePath("index")));
        QFile stagedBackup(QDir(backup).filePath("staged/tracked.txt"));QVERIFY(stagedBackup.open(QIODevice::ReadOnly));QCOMPARE(stagedBackup.readAll(),QByteArray("staged\n"));
        QVERIFY(snapshot());QVERIFY(write("tracked.txt","external edit\n"));QVERIFY(!run("discard",{"tracked.txt"}));QCOMPARE(read("tracked.txt"),QByteArray("external edit\n"));QVERIFY(error.contains(QString::fromUtf8("바뀌")));
        QVERIFY(snapshot());QVERIFY2(run("discard",{"tracked.txt"}),qPrintable(error));QCOMPARE(read("tracked.txt"),QByteArray("base\n"));QCOMPARE(git({"diff","--cached"}),QByteArray());
        QVERIFY(write("[abc].tmp","literal\n"));QVERIFY(write("a.tmp","other\n"));
        QVERIFY(snapshot());QVERIFY2(run("clean",{"[abc].tmp",QString::fromUtf8("새 파일.txt")}),qPrintable(error));QVERIFY(!QFileInfo::exists(repo.filePath("[abc].tmp")));QCOMPARE(read("a.tmp"),QByteArray("other\n"));QCOMPARE(read("keep.ignored"),QByteArray("secret\n"));
        QVERIFY(snapshot());QVERIFY(!run("clean",{"keep.ignored"}));QVERIFY(!run("clean",{"../outside"}));QCOMPARE(read("keep.ignored"),QByteArray("secret\n"));
        QVERIFY(!run("move",{"tracked.txt"},".git./forbidden"));QCOMPARE(read("tracked.txt"),QByteArray("base\n"));
        QVERIFY(snapshot());QVERIFY2(run("move",{"tracked.txt"},"renamed.txt"),qPrintable(error));QVERIFY(!QFileInfo::exists(repo.filePath("tracked.txt")));QCOMPARE(read("renamed.txt"),QByteArray("base\n"));
        QVERIFY(snapshot());QVERIFY2(run("discard",{"renamed.txt"}),qPrintable(error));QCOMPARE(read("tracked.txt"),QByteArray("base\n"));QVERIFY(!QFileInfo::exists(repo.filePath("renamed.txt")));
        QVERIFY(write("tracked.txt","must keep\n"));QVERIFY(snapshot());QVERIFY(!run("remove",{"tracked.txt"}));QCOMPARE(read("tracked.txt"),QByteArray("must keep\n"));
        QVERIFY(snapshot());QVERIFY(run("discard",{"tracked.txt"}));QVERIFY(snapshot());QVERIFY2(run("remove",{"tracked.txt"}),qPrintable(error));QVERIFY(!QFileInfo::exists(repo.filePath("tracked.txt")));QVERIFY(snapshot());QVERIFY(run("discard",{"tracked.txt"}));
        QVERIFY(snapshot());QVERIFY2(run("ignore",{},"*.ignored\na.tmp\n"),qPrintable(error));QCOMPARE(read(".gitignore"),QByteArray("*.ignored\na.tmp\n"));
        QVERIFY(snapshot());QVERIFY(run("discard",{".gitignore"}));QVERIFY(snapshot());QVERIFY(run("clean",{"a.tmp"}));
        QVERIFY(snapshot());QVERIFY2(run("pop",{},{},first),qPrintable(error));QVERIFY(git({"stash","list"}).isEmpty());QCOMPARE(read("tracked.txt"),QByteArray("working\n"));QVERIFY(!git({"for-each-ref","--format=%(objectname)","refs/gitcanvas/stash-backups"}).isEmpty());
        QVERIFY(snapshot());QVERIFY(run("save-untracked",{},"branch stash"));const auto branchStash=oid();QVERIFY(snapshot());QVERIFY2(run("branch",{},"from-stash",branchStash),qPrintable(error));QCOMPARE(git({"branch","--show-current"}).trimmed(),QByteArray("from-stash"));QCOMPARE(read("tracked.txt"),QByteArray("working\n"));QVERIFY(git({"stash","list"}).isEmpty());
        QVERIFY(snapshot());QVERIFY(run("save-untracked",{},"one"));const auto dropOid=oid();QVERIFY(snapshot());QVERIFY(run("drop",{},{},dropOid));QVERIFY(git({"stash","list"}).isEmpty());
        QVERIFY(write("tracked.txt","one\n"));QVERIFY(snapshot());QVERIFY(run("save",{},"one"));QVERIFY(write("tracked.txt","two\n"));QVERIFY(snapshot());QVERIFY(run("save",{},"two"));
        QVERIFY(snapshot());QVERIFY2(run("clear"),qPrintable(error));QVERIFY(git({"stash","list"}).isEmpty());QCOMPARE(read("tracked.txt"),QByteArray("base\n"));
        // Failed Pop must keep the stash and expose unmerged paths for manual resolution.
        QVERIFY(write("tracked.txt","stash conflict\n"));QVERIFY(snapshot());QVERIFY(run("save",{},"conflict"));const auto conflictOid=oid();
        QVERIFY(write("tracked.txt","committed conflict\n"));git({"add","tracked.txt"});git({"commit","-m","conflict"});
        QVERIFY(snapshot());QVERIFY(!run("pop",{},{},conflictOid));QCOMPARE(oid(),conflictOid);QVERIFY(read("tracked.txt").contains("<<<<<<<"));
        QVERIFY(snapshot());QVERIFY(!client.operationState().conflicts.isEmpty());QVERIFY(!run("save"));QCOMPARE(oid(),conflictOid);
    }

    void worktreeGui() {
        QSettings().clear();QTemporaryDir repo;QVERIFY(repo.isValid());
        auto git=[&](QStringList args){QProcess p;p.setWorkingDirectory(repo.path());p.start("git",args);p.waitForFinished(15000);return p.readAllStandardOutput();};
        auto write=[&](const QByteArray &data){QFile f(repo.filePath("demo.txt"));return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();};
        git({"init","-b","main"});git({"config","user.name","GUI Test"});git({"config","user.email","gui@example.invalid"});git({"config","core.autocrlf","false"});QVERIFY(write("base\n"));git({"add","."});git({"commit","-m","base"});QVERIFY(write("GUI stash\n"));
        MainWindow window;window.show();window.openRepository(repo.path());auto *tabs=window.findChild<QTabWidget*>("mainTabs");auto *panel=window.findChild<WorktreePanel*>("worktreePanel");QVERIFY(panel);tabs->setCurrentWidget(panel);
        auto *save=panel->findChild<QPushButton*>("worktree_save");auto *list=panel->findChild<QListWidget*>("stashList");QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(),15000);
        panel->findChild<QLineEdit*>("stashMessage")->setText("GUI checkpoint");
        auto *confirmTimer=new QTimer(&window);confirmTimer->setInterval(10);bool titled=false;connect(confirmTimer,&QTimer::timeout,&window,[&]{if(auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){titled=box->windowFlags().testFlag(Qt::WindowTitleHint);box->done(QMessageBox::Yes);}});confirmTimer->start();
        QTest::mouseClick(save,Qt::LeftButton);QTRY_COMPARE_WITH_TIMEOUT(list->count(),1,15000);QVERIFY(titled);list->setCurrentRow(0);
        auto *detail=panel->findChild<QPlainTextEdit*>("stashDetail");QTRY_VERIFY_WITH_TIMEOUT(detail->toPlainText().contains("GUI stash"),10000);
        window.grab().save("build/stash-screenshot.png");
        auto *apply=panel->findChild<QPushButton*>("worktree_apply");QTRY_VERIFY(apply->isEnabled());QTest::mouseClick(apply,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(panel->findChild<QPlainTextEdit*>("worktreeResult")->toPlainText().startsWith(QString::fromUtf8("완료")),15000);
        QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(),15000);QCOMPARE(list->count(),1);QFile f(repo.filePath("demo.txt"));QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),QByteArray("GUI stash\n"));confirmTimer->stop();
    }

    void operationRecovery() {
        auto git=[](const QString &path,QStringList args) {QProcess p;p.setWorkingDirectory(path);p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput();};
        for(const auto &operation:QStringList{"merge","rebase","cherry-pick","revert"}) {
            QTemporaryDir repo;QVERIFY(repo.isValid());
            auto write=[&](const QByteArray &text){QFile f(repo.filePath("conflict.txt"));if(!f.open(QIODevice::WriteOnly))return false;return f.write(text)==text.size();};
            git(repo.path(),{"init","-b","main"});git(repo.path(),{"config","user.name","Recovery Test"});git(repo.path(),{"config","user.email","test@example.invalid"});
            QVERIFY(write("base\n"));git(repo.path(),{"add","."});git(repo.path(),{"commit","-m","base"});
            git(repo.path(),{"switch","-c","feature"});QVERIFY(write("feature\n"));git(repo.path(),{"commit","-am","feature"});
            const auto feature=QString::fromUtf8(git(repo.path(),{"rev-parse","HEAD"}).trimmed());
            git(repo.path(),{"switch","main"});QVERIFY(write("main\n"));git(repo.path(),{"commit","-am","main"});
            const auto before=git(repo.path(),{"rev-parse","HEAD"}).trimmed();
            const QStringList command=operation=="merge"?QStringList{"merge","--no-edit","feature"}:operation=="rebase"?QStringList{"rebase","feature"}:QStringList{operation,"--no-edit",feature};
            QVERIFY(git(repo.path(),command).startsWith("ERROR:"));
            GitClient client;client.setRepositoryPath(repo.path());bool done=false,success=false;QString error;
            auto stateDone=[&](bool ok,const QString &message){done=true;success=ok;error=message;};
            auto completed=[&](bool ok,const QByteArray &,const QString &message){done=true;success=ok;error=message;};
            client.loadOperationState(stateDone);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));
            QCOMPARE(client.operationState().operation,operation);QCOMPARE(client.operationState().conflicts,QStringList{"conflict.txt"});
            const auto stale=client.operationState().fingerprint;
            done=false;client.commit("must not commit unresolved operation",completed);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);
            QCOMPARE(client.operationState().operation,operation);
            if(operation=="merge") {
                OperationPanel panel(&client);panel.show();
                QCOMPARE(panel.findChild<QListWidget *>("conflictFiles")->count(),1);
                QVERIFY(!panel.findChild<QPushButton *>("continueGitOperation")->isEnabled());
                QTimer::singleShot(0,[]{auto *confirm=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());QVERIFY(confirm);confirm->done(QMessageBox::Yes);});
                panel.findChild<QPushButton *>("abortGitOperation")->click();
                QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy()&&client.operationState().operation.isEmpty(),15000);
                QCOMPARE(git(repo.path(),{"rev-parse","HEAD"}).trimmed(),before);
                QVERIFY(git(repo.path(),command).startsWith("ERROR:"));
            }
            QVERIFY(write("resolved\n"));done=false;client.stage({"conflict.txt"},completed);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);
            done=false;client.loadOperationState(stateDone);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);QVERIFY(client.operationState().conflicts.isEmpty());
            done=false;client.recoverOperation(stale,false,completed);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);
            done=false;client.recoverOperation(client.operationState().fingerprint,false,completed);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));
            QVERIFY(client.operationState().operation.isEmpty());QVERIFY(client.operationState().conflicts.isEmpty());
            QVERIFY(!git(repo.path(),{"for-each-ref","--format=%(refname)","refs/gitcanvas/recovery"}).isEmpty());
            QFile lock(repo.filePath(".git/index.lock"));QVERIFY(lock.open(QIODevice::WriteOnly));lock.close();
            done=false;client.loadOperationState(stateDone);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);QCOMPARE(client.operationState().locks.size(),1);
            done=false;client.switchBranch("feature",false,completed);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);QVERIFY(lock.exists());QVERIFY(lock.remove());
        }
    }
    void processCancellationAndTimeout() {
#ifdef Q_OS_WIN
        QTemporaryDir helper;QVERIFY(helper.isValid());
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),helper.filePath("git.exe")));
        TestEnvironment environment;environment.set("PATH",helper.path().toUtf8()+";"+qgetenv("PATH"));
        environment.set("GITCANVAS_FAKE_GIT","1");environment.set("GITCANVAS_CHILD_PID",helper.filePath("child.pid").toUtf8());
        GitClient client;client.setRepositoryPath(helper.path());client.setTimeoutMilliseconds(15000);
        int callbacks=0;bool success=true;QString error;
        auto completed=[&](bool ok,const QByteArray &,const QString &message){++callbacks;success=ok;error=message;};
        client.inspect({"status"},completed);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(helper.filePath("child.pid")),10000);
        QFile pid(helper.filePath("child.pid"));QVERIFY(pid.open(QIODevice::ReadOnly));const auto childPid=pid.readAll().trimmed().toULong();pid.close();
        HANDLE child=OpenProcess(SYNCHRONIZE,FALSE,childPid);QVERIFY(child);
        QVERIFY(client.canCancel());client.cancelActive();QTRY_COMPARE_WITH_TIMEOUT(callbacks,1,10000);QVERIFY(!success);QVERIFY(error.contains(QString::fromUtf8("중단")));
        const auto stopped=WaitForSingleObject(child,5000);CloseHandle(child);QCOMPARE(stopped,DWORD(WAIT_OBJECT_0));
        QVERIFY(pid.remove());client.setTimeoutMilliseconds(1500);client.inspect({"status"},completed);
        QTRY_COMPARE_WITH_TIMEOUT(callbacks,2,10000);QVERIFY(!success);QVERIFY(error.contains(QString::fromUtf8("시간 제한")));
        // Local writes are deliberately not force-killed when the deadline expires.
        client.setTimeoutMilliseconds(100);client.inspectEnvironment({"update-ref","test"},completed);
        QTRY_VERIFY_WITH_TIMEOUT(client.activityText().contains(QString::fromUtf8("시간 제한 초과")),3000);QVERIFY(!client.canCancel());client.cancelActive();
        QTRY_COMPARE_WITH_TIMEOUT(callbacks,3,5000);QVERIFY(success);QVERIFY(client.lastDiagnostic().contains(QString::fromUtf8("명령이 완료")));
        QTest::qWait(200);QCOMPARE(callbacks,3);QVERIFY(!client.isBusy());
#else
        QSKIP("Windows process tree cancellation test");
#endif
    }
    void remoteSetupAndFirstPush() {
        QTemporaryDir root;
        const auto local = root.filePath("local"), remote = root.filePath("remote.git");
        auto git = [](const QString &path, QStringList args) {
            QProcess p; p.setWorkingDirectory(path); p.start("git", args); p.closeWriteChannel();
            if (!p.waitForFinished(15000) || p.exitCode()!=0) return QByteArray("ERROR: ") + p.readAllStandardError();
            return p.readAllStandardOutput();
        };
        QVERIFY(!git(root.path(), {"init", "--bare", "-b", "main", remote}).startsWith("ERROR:"));
        GitClient client; bool done=false, succeeded=false; QString error;
        auto completed = [&](bool ok, const QByteArray &, const QString &message) { done=true; succeeded=ok; error=message; };
        client.createRepository(local, "main", completed); QTRY_VERIFY_WITH_TIMEOUT(done,15000); QVERIFY2(succeeded,qPrintable(error));
        client.setRepositoryPath(local);
        git(local,{"config","user.name","Remote Test"}); git(local,{"config","user.email","test@example.invalid"});
        QVERIFY(!git(local,{"commit","--allow-empty","-m","initial"}).startsWith("ERROR:"));
        {
            RemoteDialog dialog(&client); dialog.show();
            auto *save=dialog.findChild<QPushButton *>("saveRemote"); QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(),15000);
            dialog.findChild<QLineEdit *>("remoteUrl")->setText(remote); save->click();
            QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(),15000);
            QCOMPARE(git(local,{"remote","get-url","origin"}).trimmed(),remote.toUtf8());
            auto *connect=dialog.findChild<QPushButton *>("saveUpstream"); QVERIFY(connect->isEnabled());
            dialog.findChild<QComboBox *>("upstreamTarget")->setEditText("main"); connect->click();
            QTRY_VERIFY_WITH_TIMEOUT(connect->isEnabled(),15000);
            QCOMPARE(git(local,{"config","branch.main.merge"}).trimmed(),QByteArray("refs/heads/main"));
            QVERIFY(dialog.grab().save("remote-settings-screenshot.png"));
        }
        SyncController sync(&client); QSignalSpy complete(&sync,&SyncController::completed), failed(&sync,&SyncController::failed);
        QCOMPARE(sync.action(),SyncController::Action::Fetch);
        sync.execute(); QTRY_COMPARE_WITH_TIMEOUT(complete.count(),1,15000); QCOMPARE(failed.count(),0);
        QCOMPARE(sync.action(),SyncController::Action::Publish);
        QVERIFY(git(remote,{"rev-parse","--verify","refs/heads/main"}).startsWith("ERROR:"));
        sync.execute(); QTRY_COMPARE_WITH_TIMEOUT(complete.count(),2,15000); QCOMPARE(failed.count(),0);
        const auto base=git(local,{"rev-parse","HEAD"}).trimmed();
        QCOMPARE(git(remote,{"rev-parse","refs/heads/main"}).trimmed(),base);
        QCOMPARE(git(local,{"rev-parse","--abbrev-ref","@{upstream}"}).trimmed(),QByteArray("origin/main"));
        git(local,{"switch","-c","feature"}); git(local,{"commit","--allow-empty","-m","feature"});
        done=false; client.setUpstream("feature","origin","new-feature",completed); QTRY_VERIFY_WITH_TIMEOUT(done,15000); QVERIFY(succeeded);
        sync.invalidate(); sync.execute(); QTRY_COMPARE_WITH_TIMEOUT(complete.count(),3,15000); QCOMPARE(sync.action(),SyncController::Action::Publish);
        // A branch created after Fetch must not be overwritten, even by a fast-forward.
        QVERIFY(!git(remote,{"update-ref","refs/heads/new-feature",QString::fromUtf8(base)}).startsWith("ERROR:"));
        sync.execute(); QTRY_COMPARE_WITH_TIMEOUT(complete.count(),4,15000); QCOMPARE(failed.count(),1);
        QCOMPARE(git(remote,{"rev-parse","refs/heads/new-feature"}).trimmed(),base);
        QCOMPARE(sync.action(),SyncController::Action::Fetch);
        sync.execute(); QTRY_COMPARE_WITH_TIMEOUT(complete.count(),5,15000); QCOMPARE(sync.action(),SyncController::Action::Push);
        sync.execute(); QTRY_COMPARE_WITH_TIMEOUT(complete.count(),6,15000); QCOMPARE(failed.count(),1);
        QCOMPARE(git(remote,{"rev-parse","refs/heads/new-feature"}).trimmed(),git(local,{"rev-parse","HEAD"}).trimmed());
        done=false; client.setUpstream("wrong-branch","origin","main",completed); QTRY_VERIFY_WITH_TIMEOUT(done,15000); QVERIFY(!succeeded);
        QCOMPARE(git(local,{"config","branch.feature.merge"}).trimmed(),QByteArray("refs/heads/new-feature"));
        done=false; client.saveRemote("origin",root.filePath("missing.git"),false,completed); QTRY_VERIFY_WITH_TIMEOUT(done,15000); QVERIFY(succeeded);
        sync.invalidate(); sync.execute(); QTRY_COMPARE_WITH_TIMEOUT(complete.count(),7,15000); QCOMPARE(sync.action(),SyncController::Action::Fetch);
        done=false; client.removeRemote("origin",completed); QTRY_VERIFY_WITH_TIMEOUT(done,15000); QVERIFY(succeeded);
        QVERIFY(git(local,{"remote"}).isEmpty()); QVERIFY(QFileInfo::exists(remote));
    }
    void repositoryCreationWhileBusy() {
        QSettings().clear();
        QTemporaryDir root; QVERIFY(root.isValid());
        MainWindow window; window.show();
        auto *client = window.findChild<GitClient *>(); QVERIFY(client);
        // A pending status/environment query must not swallow the sidebar click.
        for (const auto &name : {"initRepositoryButton", "cloneRepositoryButton"}) {
            client->inspectEnvironment({"--version"}, [](bool, const QByteArray &, const QString &) {});
            QVERIFY(client->isBusy());
            auto *button = window.findChild<QPushButton *>(name); QVERIFY(button);
            QVERIFY(button->isEnabled());
            bool opened = false;
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = qobject_cast<RepositoryDialog *>(QApplication::activeModalWidget());
                opened = dialog != nullptr;
                if (dialog) dialog->reject();
            });
            QTest::mouseClick(button, Qt::LeftButton);
            QVERIFY(opened);
            QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(), 15000);
        }
        client->inspectEnvironment({"--version"}, [](bool, const QByteArray &, const QString &) {});
        QVERIFY(client->isBusy());
        RepositoryDialog dialog(client, false, &window);
        auto *run = dialog.findChild<QPushButton *>("createRepository");
        auto *waiting = dialog.findChild<QLabel *>("creationWaiting");
        QVERIFY(run && waiting); QVERIFY(!run->isEnabled()); QVERIFY(!waiting->isHidden());
        dialog.show();
        QTRY_VERIFY_WITH_TIMEOUT(run->isEnabled(), 15000);
        QVERIFY(waiting->isHidden());
        const auto destination = root.filePath(QString::fromUtf8("새 저장소 with spaces"));
        dialog.findChild<QLineEdit *>("creationPath")->setText(destination);
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        QTest::mouseClick(run, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 15000);
        QVERIFY(QFileInfo::exists(destination + "/.git/HEAD"));
        QCOMPARE(dialog.createdPath(), destination);
        QSettings().clear();
    }
    void repositoryCreationWithExistingFiles() {
        QTemporaryDir root; QVERIFY(root.isValid());
        const auto destination = root.filePath(QString::fromUtf8("기존 프로젝트 with files"));
        QVERIFY(QDir().mkpath(destination + "/src"));
        const QMap<QString, QByteArray> files{{"src/main.cpp", "int main() { return 0; }\n"},
                                            {".gitignore", "/build/\n"}, {"README.md", "Existing project\n"}};
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            QFile file(destination + "/" + it.key()); QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(it.value()), it.value().size());
        }
        GitClient client;
        RepositoryDialog dialog(&client, false); dialog.show();
        dialog.findChild<QLineEdit *>("creationPath")->setText(destination);
        auto *run = dialog.findChild<QPushButton *>("createRepository");
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        QTest::mouseClick(run, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(run->isEnabled(), 15000);
        QCOMPARE(accepted.count(), 1);
        QFile head(destination + "/.git/HEAD"); QVERIFY(head.open(QIODevice::ReadOnly));
        const auto originalHead = head.readAll(); QCOMPARE(originalHead.trimmed(), QByteArray("ref: refs/heads/main")); head.close();
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            QFile file(destination + "/" + it.key()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), it.value());
        }
        QVERIFY(!QFileInfo::exists(destination + "/.git/index")); // No automatic staging.
        bool done = false, success = true; QString error;
        auto result = [&](bool ok, const QByteArray &, const QString &message) { done = true; success = ok; error = message; };
        client.createRepository(destination, "other", result);
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000); QVERIFY(!success); QVERIFY(!error.isEmpty());
        QVERIFY(head.open(QIODevice::ReadOnly)); QCOMPARE(head.readAll(), originalHead); head.close();
        const auto occupied = root.filePath("occupied"); QVERIFY(QDir().mkpath(occupied));
        QFile existing(occupied + "/keep.txt"); QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("keep\n"); existing.close();
        done = false; client.cloneRepository(destination, occupied, "full", 1, {}, result);
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000); QVERIFY(!success);
        QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(), QByteArray("keep\n")); existing.close();
        done = false; client.createRepository(occupied, "invalid..branch", result);
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000); QVERIFY(!success); QVERIFY(!QFileInfo::exists(occupied + "/.git"));
        QFile marker(occupied + "/.git"); QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("gitdir: missing\n"); marker.close();
        done = false; client.createRepository(occupied, "main", result);
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000); QVERIFY(!success);
        QVERIFY(marker.open(QIODevice::ReadOnly)); QCOMPARE(marker.readAll(), QByteArray("gitdir: missing\n"));
    }
    void repositoryCreationAndClone() {
        QTemporaryDir root; QSettings().clear();
        const auto source=root.filePath("source");
        {
            MainWindow window; window.show(); auto *client=window.findChild<GitClient *>();
            QTimer::singleShot(0,[&] {
                auto *dialog=qobject_cast<RepositoryDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
                QVERIFY(dialog->windowFlags().testFlag(Qt::WindowTitleHint));
                // Simulate a background query taking the client just as creation completes.
                connect(dialog, &QDialog::accepted, &window, [client] {
                    client->inspectEnvironment({"--version"}, [](bool, const QByteArray &, const QString &) {});
                });
                dialog->findChild<QLineEdit *>("creationPath")->setText(source);
                dialog->findChild<QPushButton *>("createRepository")->click();
            });
            window.findChild<QPushButton *>("initRepositoryButton")->click();
            QVERIFY(client->isBusy());
            QCOMPARE(window.findChild<QListWidget *>("repositoryList")->count(),1);
            QCOMPARE(QSettings().value("workspace/repositories").toList().size(), 1);
            auto *notice = window.findChild<QLabel *>("repositoryCreationNotice"); QVERIFY(notice);
            QVERIFY(notice->isVisible()); QVERIFY(notice->text().contains(QString::fromUtf8("추가했습니다")));
            QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && client->repositoryPath()==QFileInfo(source).canonicalFilePath(),15000);
            QCOMPARE(window.findChild<QListWidget *>("repositoryList")->count(),1);
            QVERIFY(notice->text().contains(QString::fromUtf8("열었습니다")));
            QVERIFY(notice->text().contains(QFileInfo(source).canonicalFilePath()));
            window.findChild<QPushButton *>("dismissCreationNotice")->click(); QVERIFY(!notice->isVisible());
            bool initialBranchShown=false; for(auto *label:window.findChildren<QLabel *>()) if(label->text()=="main") initialBranchShown=true;
            QVERIFY(initialBranchShown);
        }
        auto git=[](const QString &path,QStringList args) { QProcess p;p.setWorkingDirectory(path);p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput(); };
        git(source,{"config","user.name","Clone Test"});git(source,{"config","user.email","test@example.invalid"});
        QFile file(source+"/hello.txt");QVERIFY(file.open(QIODevice::WriteOnly));file.write("hello\n");file.close();
        git(source,{"add","."});git(source,{"commit","-m","initial"});git(source,{"commit","--allow-empty","-m","second"});
        git(source,{"config","uploadpack.allowFilter","true"});
        GitClient client;client.setRepositoryPath(source);bool done=false,success=false;QString error;
        auto callback=[&](bool ok,const QByteArray &,const QString &message){done=true;success=ok;error=message;};
        QSignalSpy progress(&client,&GitClient::cloneProgress);
        for(const auto &mode:QStringList{"full","shallow","partial"}) {
            const auto destination=root.filePath(mode);done=false;
            client.cloneRepository(source,destination,mode,1,{},callback);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(success,qPrintable(error));
            QVERIFY(QFileInfo::exists(destination+"/hello.txt"));QCOMPARE(client.repositoryPath(),source);
            if(mode=="shallow") {QCOMPARE(git(destination,{"rev-parse","--is-shallow-repository"}).trimmed(),QByteArray("true"));QCOMPARE(git(destination,{"rev-list","--count","HEAD"}).trimmed(),QByteArray("1"));}
            if(mode=="partial") QCOMPARE(git(destination,{"config","remote.origin.partialclonefilter"}).trimmed(),QByteArray("blob:none"));
        }
        QVERIFY(progress.count()>0);
        done=false;client.createRepository(source,"main",callback);QVERIFY(done);QVERIFY(!success);
        done=false;client.cloneRepository(source,source,"full",1,{},callback);QVERIFY(done);QVERIFY(!success);QVERIFY(QFileInfo::exists(source+"/hello.txt"));
        done=false;client.cloneRepository(root.filePath("missing"),root.filePath("failed"),"full",1,{},callback);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);
        {
            MainWindow window; window.show(); auto *windowClient=window.findChild<GitClient *>();QTRY_VERIFY_WITH_TIMEOUT(!windowClient->isBusy(),15000);
            const auto destination=root.filePath("gui-clone");
            QTimer::singleShot(0,[&] {auto *dialog=qobject_cast<RepositoryDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);dialog->findChild<QLineEdit *>("cloneUrl")->setText(source);dialog->findChild<QLineEdit *>("creationPath")->setText(destination);dialog->findChild<QPushButton *>("createRepository")->click();});
            window.findChild<QPushButton *>("cloneRepositoryButton")->click();
            QTRY_VERIFY_WITH_TIMEOUT(!windowClient->isBusy()&&windowClient->repositoryPath()==QFileInfo(destination).canonicalFilePath(),15000);
            QCOMPARE(window.findChild<QListWidget *>("repositoryList")->count(),2);
            auto *notice = window.findChild<QLabel *>("repositoryCreationNotice");
            QVERIFY(notice->isVisible()); QVERIFY(notice->text().contains(QString::fromUtf8("열었습니다")));
            QCOMPARE(QSettings().value("workspace/repositories").toList().size(), 2);
        }
        {
            MainWindow restored;
            QCOMPARE(restored.findChild<QListWidget *>("repositoryList")->count(), 2);
            auto *restoredClient = restored.findChild<GitClient *>();
            QTRY_VERIFY_WITH_TIMEOUT(!restoredClient->isBusy(), 15000);
            QCOMPARE(restoredClient->repositoryPath(), QFileInfo(root.filePath("gui-clone")).canonicalFilePath());
        }
        QSettings().clear();
    }
    void gitEnvironmentAndIdentity() {
        QTemporaryDir repo, isolated;
        QVERIFY(repo.isValid() && isolated.isValid());
        TestEnvironment environment;
        environment.set("GIT_CONFIG_GLOBAL", isolated.filePath("global.gitconfig").toUtf8());
        environment.set("GIT_CONFIG_NOSYSTEM", "1");
        for (const auto *key : {"GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL", "EMAIL", "GIT_CONFIG_COUNT", "GIT_CONFIG_PARAMETERS"}) environment.set(key);
        auto git = [&](QStringList args) {
            QProcess process; process.setWorkingDirectory(repo.path()); process.start("git", args); process.closeWriteChannel();
            if (!process.waitForFinished(15000) || process.exitCode() != 0) return QByteArray("ERROR: ") + process.readAllStandardError();
            return process.readAllStandardOutput();
        };
        GitClient client;
        QSignalSpy commands(&client, &GitClient::commandStarted);
        {
            GitSettingsDialog dialog(&client); dialog.show();
            auto *save = dialog.findChild<QPushButton *>("saveIdentity");
            QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 15000);
            QVERIFY(dialog.windowFlags().testFlag(Qt::WindowTitleHint));
            QVERIFY(dialog.findChild<QLabel *>("gitEnvironment")->text().contains("git version"));
            QCOMPARE(dialog.findChild<QComboBox *>("identityScope")->currentIndex(), 1);
            auto *name = dialog.findChild<QLineEdit *>("identityName");
            auto *email = dialog.findChild<QLineEdit *>("identityEmail");
            QVERIFY(name->text().isEmpty());
            name->setText(QString::fromUtf8("전역 작성자")); email->setText("invalid email"); save->click();
            QVERIFY(!QFile::exists(isolated.filePath("global.gitconfig")));
            email->setText("global@example.invalid"); save->click();
            QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 15000);
            QCOMPARE(git({"config", "--global", "user.name"}).trimmed(), QString::fromUtf8("전역 작성자").toUtf8());
            QVERIFY(dialog.findChild<QPlainTextEdit *>("effectiveIdentity")->toPlainText().contains("global@example.invalid"));
        }
        QVERIFY(!git({"init", "-b", "main"}).startsWith("ERROR:"));
        client.setRepositoryPath(repo.path());
        {
            GitSettingsDialog dialog(&client); dialog.show();
            auto *save = dialog.findChild<QPushButton *>("saveIdentity");
            auto *scope = dialog.findChild<QComboBox *>("identityScope");
            auto *name = dialog.findChild<QLineEdit *>("identityName");
            auto *email = dialog.findChild<QLineEdit *>("identityEmail");
            auto *effective = dialog.findChild<QPlainTextEdit *>("effectiveIdentity");
            QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 15000);
            QCOMPARE(scope->currentIndex(), 0); QVERIFY(name->text().isEmpty());
            QVERIFY(effective->toPlainText().contains("global@example.invalid"));
            name->setText(QString::fromUtf8("저장소 작성자")); email->setText("local@example.invalid"); save->click();
            QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 15000);
            QCOMPARE(git({"config", "--local", "user.email"}).trimmed(), QByteArray("local@example.invalid"));
            QCOMPARE(git({"config", "--global", "user.email"}).trimmed(), QByteArray("global@example.invalid"));
            QVERIFY(effective->toPlainText().contains("local"));
            QVERIFY(!git({"commit", "--allow-empty", "-m", "configured identity"}).startsWith("ERROR:"));
            QCOMPARE(git({"log", "-1", "--format=%an <%ae>"}).trimmed(), QString::fromUtf8("저장소 작성자 <local@example.invalid>").toUtf8());
            scope->setCurrentIndex(1); scope->activated(1);
            QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 15000);
            QCOMPARE(email->text(), QString("global@example.invalid"));
            email->setText("new-global@example.invalid"); save->click();
            QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 15000);
            QVERIFY(effective->toPlainText().contains("local@example.invalid"));
            QCOMPARE(git({"config", "--global", "user.email"}).trimmed(), QByteArray("new-global@example.invalid"));
            environment.set("GIT_AUTHOR_NAME", "Environment Author"); environment.set("GIT_AUTHOR_EMAIL", "env@example.invalid");
            dialog.findChild<QPushButton *>("reloadGitEnvironment")->click();
            QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 15000);
            QVERIFY(effective->toPlainText().contains("Environment Author <env@example.invalid>"));
            scope->setCurrentIndex(0); scope->activated(0);
            QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 15000);
            QFile lock(repo.filePath(".git/config.lock")); QVERIFY(lock.open(QIODevice::WriteOnly)); lock.close();
            name->setText("Must not be saved"); save->click();
            QTRY_VERIFY_WITH_TIMEOUT(save->isEnabled(), 15000);
            QVERIFY(dialog.findChild<QLabel *>("identityStatus")->text().contains(QString::fromUtf8("저장하지 못했습니다")));
            QCOMPARE(git({"config", "--local", "user.name"}).trimmed(), QString::fromUtf8("저장소 작성자").toUtf8());
            QVERIFY(dialog.grab().save("git-settings-screenshot.png"));
        }
        for (const auto &command : commands) {
            QVERIFY(!command[0].toString().contains("global@example.invalid"));
            QVERIFY(!command[0].toString().contains("local@example.invalid"));
        }
    }
    void commitAfterIdentitySetup() {
        QTemporaryDir repo, isolated;
        TestEnvironment environment;
        environment.set("GIT_CONFIG_GLOBAL", isolated.filePath("global.gitconfig").toUtf8());
        environment.set("GIT_CONFIG_NOSYSTEM", "1");
        for (const auto *key : {"GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL", "EMAIL", "GIT_CONFIG_COUNT", "GIT_CONFIG_PARAMETERS"}) environment.set(key);
        auto git = [&](QStringList args) {
            QProcess process; process.setWorkingDirectory(repo.path()); process.start("git", args); process.closeWriteChannel();
            if (!process.waitForFinished(15000) || process.exitCode() != 0) return QByteArray("ERROR: ") + process.readAllStandardError();
            return process.readAllStandardOutput();
        };
        QVERIFY(!git({"init", "-b", "main"}).startsWith("ERROR:"));
        git({"config", "user.useConfigOnly", "true"});
        QFile file(repo.filePath("first.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("first\n"); file.close();
        QVERIFY(!git({"add", "first.txt"}).startsWith("ERROR:"));
        QSettings().clear();
        MainWindow window; window.show(); window.openRepository(repo.path());
        auto *client = window.findChild<GitClient *>();
        auto *title = window.findChild<QLineEdit *>("commitTitle");
        auto *body = window.findChild<QPlainTextEdit *>("commitMessage");
        auto *commit = window.findChild<QPushButton *>("primary");
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(), 15000);
        title->setText("first configured commit"); body->setPlainText("draft remains during setup");
        QVERIFY(commit->isEnabled());
        int phase = 0;
        QTimer setup; setup.setInterval(10);
        connect(&setup, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<GitSettingsDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *save = dialog->findChild<QPushButton *>("saveIdentity");
            if (!save->isEnabled()) return;
            if (phase == 0) {
                phase = 1;
                dialog->findChild<QLineEdit *>("identityName")->setText("First Author");
                dialog->findChild<QLineEdit *>("identityEmail")->setText("first@example.invalid");
                save->click();
            } else {
                setup.stop(); phase = 2;
                QVERIFY(dialog->grab().save("git-settings-screenshot.png"));
                dialog->reject();
            }
        });
        setup.start(); commit->click();
        QTRY_COMPARE_WITH_TIMEOUT(phase, 2, 15000);
        QCOMPARE(title->text(), QString("first configured commit"));
        QCOMPARE(body->toPlainText(), QString("draft remains during setup"));
        QVERIFY(git({"rev-parse", "--verify", "HEAD"}).startsWith("ERROR:"));
        commit->click();
        QTRY_VERIFY_WITH_TIMEOUT(title->text().isEmpty() && !client->isBusy(), 15000);
        QCOMPARE(git({"log", "-1", "--format=%an <%ae>"}).trimmed(), QByteArray("First Author <first@example.invalid>"));
        QCOMPARE(git({"log", "-1", "--format=%s"}).trimmed(), QByteArray("first configured commit"));
    }
    void gitEnvironmentMissingExecutable() {
        QTemporaryDir isolated;
        TestEnvironment environment; environment.set("PATH", isolated.path().toUtf8());
        GitClient client;
        GitSettingsDialog dialog(&client); dialog.show();
        auto *reload = dialog.findChild<QPushButton *>("reloadGitEnvironment");
        QTRY_VERIFY_WITH_TIMEOUT(reload->isEnabled(), 15000);
        QVERIFY(!dialog.findChild<QPushButton *>("saveIdentity")->isEnabled());
#ifdef Q_OS_MACOS
        QVERIFY(dialog.findChild<QLabel *>("identityStatus")->text().contains("Command Line Tools"));
#else
        QVERIFY(dialog.findChild<QLabel *>("identityStatus")->text().contains("Git for Windows"));
#endif
        QVERIFY(!client.isBusy());
    }
    void workingTreeFlow() {
        QTemporaryDir repo;
        QVERIFY(repo.isValid());
        auto git = [&repo](QStringList args) {
            QProcess p; p.setWorkingDirectory(repo.path()); p.start("git",args);
            if(!p.waitForFinished(15000) || p.exitCode()!=0) return QByteArray("ERROR: ")+p.readAllStandardError();
            return p.readAllStandardOutput();
        };
        QVERIFY(!git({"init","-b","main"}).startsWith("ERROR:"));
        git({"config","user.name","Prototype Test"}); git({"config","user.email","test@example.invalid"});
        QFile file(repo.filePath(QString::fromUtf8("한글 file.txt"))); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("first line\n"); file.close();
        MainWindow window; window.show(); window.openRepository(repo.path());
        auto *client=window.findChild<GitClient *>(); QVERIFY(client);
        auto *unstaged=window.findChild<QListWidget *>("unstagedFiles");
        auto *staged=window.findChild<QListWidget *>("stagedFiles");
        auto *message=window.findChild<QPlainTextEdit *>("commitMessage");
        auto *title=window.findChild<QLineEdit *>("commitTitle");
        auto *diff=window.findChild<QPlainTextEdit *>("diffViewer");
        auto *history=window.findChild<HistoryTable *>("historyTable");
        auto findButton=[&window](const QString &contains) -> QPushButton * {
            for(auto *b:window.findChildren<QPushButton *>()) if(b->text().contains(contains)) return b;
            return nullptr;
        };
        auto *stage=findButton(QString::fromUtf8("체크한 파일 Stage")), *unstage=findButton(QString::fromUtf8("체크한 파일 Unstage")), *commit=findButton(QString::fromUtf8("커밋 만들기"));
        QVERIFY(stage && unstage && commit);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && unstaged->count()==1,15000);
        QVERIFY(window.findChild<QLabel *>("historySummary")->text().contains(QString::fromUtf8("아직 커밋이 없습니다")));
        unstaged->setCurrentRow(0);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && diff->toPlainText().contains("first line"),15000);
        QVERIFY(!stage->isEnabled());
        unstaged->item(0)->setCheckState(Qt::Checked);
        QTest::mouseClick(stage,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && staged->count()==1,15000);
        staged->setCurrentRow(0); QTRY_VERIFY(!client->isBusy());
        staged->item(0)->setCheckState(Qt::Checked);
        QTest::mouseClick(unstage,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && staged->count()==0 && unstaged->count()==1,15000);
        QVERIFY(file.exists());
        unstaged->setCurrentRow(0); QTRY_VERIFY(!client->isBusy()); unstaged->item(0)->setCheckState(Qt::Checked); QTest::mouseClick(stage,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && staged->count()==1,15000);
        message->setPlainText("Detailed reason\nSecond detail line"); QVERIFY(!commit->isEnabled());
        title->setText("Prototype initial commit"); QVERIFY(commit->isEnabled()); QTest::mouseClick(commit,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && history->rowCount()==1,15000);
        QCOMPARE(history->cellText(0,1),QString("Prototype initial commit"));
        QVERIFY(!window.findChild<QPushButton*>("deleteBranchButton")->isEnabled());
        QSignalSpy branchCommands(client,&GitClient::commandStarted);
        auto *sameBranch=window.findChild<QComboBox*>("branchSelector");
        sameBranch->activated(sameBranch->currentIndex());QCOMPARE(branchCommands.count(),0);
        QCOMPARE(window.findChildren<QTabWidget*>().size(),1);
        QVERIFY(!window.findChild<QComboBox*>("changesLayoutMode"));auto *changesSplit=window.findChild<QSplitter*>("changesSplitter");
        QCOMPARE(changesSplit->orientation(),Qt::Horizontal);
        QVERIFY(changesSplit->widget(0)->isAncestorOf(title));QVERIFY(changesSplit->widget(1)->isAncestorOf(diff));
        QCOMPARE(staged->count(),0); QCOMPARE(unstaged->count(),0);
        QCOMPARE(git({"log","-1","--format=%B"}).trimmed(),QByteArray("Prototype initial commit\n\nDetailed reason\nSecond detail line"));
        QVERIFY(title->text().isEmpty()); QVERIFY(message->toPlainText().isEmpty());
        QVERIFY(file.open(QIODevice::Append)); file.write("second line\n"); file.close();
        QTest::mouseClick(findButton(QString::fromUtf8("새로고침")),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && unstaged->count()==1,15000);
        unstaged->setCurrentRow(0);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && diff->toPlainText().contains("+second line"),15000);
        bool done=false, succeeded=false;
        client->switchBranch("feature/prototype",true,[&](bool ok,const QByteArray &,const QString &){succeeded=ok;done=true;});
        QTRY_VERIFY_WITH_TIMEOUT(done,15000); QVERIFY(succeeded);
        QCOMPARE(git({"branch","--show-current"}).trimmed(),QByteArray("feature/prototype"));
        QTest::mouseClick(findButton(QString::fromUtf8("새로고침")),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && window.findChild<QComboBox *>("branchSelector")->count()==2,15000);
        window.findChild<QComboBox *>("branchSelector")->setCurrentText("main");
        auto *branchCombo=window.findChild<QComboBox *>("branchSelector");
        QMetaObject::invokeMethod(branchCombo,"activated",Qt::DirectConnection,Q_ARG(int,branchCombo->currentIndex()));
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);
        QCOMPARE(git({"branch","--show-current"}).trimmed(),QByteArray("main"));
        const auto startHead=git({"rev-parse","HEAD"}).trimmed();
        QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QInputDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);QVERIFY(dialog->windowFlags().testFlag(Qt::WindowTitleHint));dialog->setTextValue("new-from-main");dialog->accept();});
        QTest::mouseClick(findButton(QString::fromUtf8("새 브랜치")),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);
        QCOMPARE(git({"branch","--show-current"}).trimmed(),QByteArray("new-from-main"));QCOMPARE(git({"rev-parse","HEAD"}).trimmed(),startHead);
        QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QInputDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);QVERIFY(dialog->windowFlags().testFlag(Qt::WindowTitleHint));dialog->setTextValue("renamed-branch");dialog->accept();});
        QTest::mouseClick(findButton(QString::fromUtf8("이름 변경")),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);QCOMPARE(git({"branch","--show-current"}).trimmed(),QByteArray("renamed-branch"));
        QTimer deleteWatcher;deleteWatcher.setInterval(10);
        int warningCount=0;
        connect(&deleteWatcher,&QTimer::timeout,&window,[&]{
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if(!dialog||dialog->objectName()!="deleteCurrentBranchDialog")return;
            deleteWatcher.stop();QVERIFY(dialog->windowFlags().testFlag(Qt::WindowTitleHint));
            QVERIFY(!dialog->findChild<QCheckBox *>("deleteRemoteBranch")->isEnabled());
            auto *force=dialog->findChild<QCheckBox *>("forceDeleteBranch");
            for(int i=0;i<2;++i){QTimer::singleShot(0,[&]{auto *warning=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());QVERIFY(warning);++warningCount;warning->done(QMessageBox::Yes);});force->setChecked(true);force->setChecked(false);}
            QVERIFY(!dialog->findChild<QComboBox *>("deleteDestination"));
            QVERIFY(dialog->findChild<QLabel *>("deleteDestinationInfo")->text().contains("main"));dialog->accept();
        });deleteWatcher.start();
        QTest::mouseClick(findButton(QString::fromUtf8("삭제")),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);QVERIFY(!git({"branch","--list","renamed-branch"}).contains("renamed-branch"));QCOMPARE(warningCount,2);QCOMPARE(git({"branch","--show-current"}).trimmed(),QByteArray("main"));
        const auto unmerged=git({"commit-tree","HEAD^{tree}","-p","HEAD","-m","unmerged change"}).trimmed();
        QVERIFY(!git({"branch","protected-branch",QString::fromUtf8(unmerged)}).startsWith("ERROR:"));
        done=false;client->deleteBranch("protected-branch",[&](bool ok,const QByteArray &,const QString &){done=true;succeeded=ok;});
        QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!succeeded);QVERIFY(git({"branch","--list","protected-branch"}).contains("protected-branch"));
        QFile other(repo.filePath("another.txt")); QVERIFY(other.open(QIODevice::WriteOnly)); other.write("another file\n"); other.close();
        QTest::mouseClick(findButton(QString::fromUtf8("새로고침")),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && unstaged->count()==2,15000);
        auto *all=window.findChild<QCheckBox *>("unstagedAll"); QVERIFY(all);
        QTest::mouseClick(all,Qt::LeftButton,Qt::NoModifier,QPoint(8,all->height()/2));
        QCOMPARE(unstaged->item(0)->checkState(),Qt::Checked); QCOMPARE(unstaged->item(1)->checkState(),Qt::Checked);
        unstaged->item(1)->setCheckState(Qt::Unchecked);
        const auto checkedPath=unstaged->item(0)->data(Qt::UserRole+1).toString().toUtf8();
        QCOMPARE(all->checkState(),Qt::PartiallyChecked);
        QTest::mouseClick(stage,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && staged->count()==1 && unstaged->count()==1,15000);
        QCOMPARE(git({"diff","--cached","--name-only","-z"}),checkedPath+QByteArray(1,'\0'));
        QTest::mouseClick(window.findChild<QPushButton *>("stageAllButton"),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy()&&staged->count()==2&&unstaged->count()==0,15000);
        QTest::mouseClick(window.findChild<QPushButton *>("unstageAllButton"),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy()&&staged->count()==0&&unstaged->count()==2,15000);
        const auto actionLog=window.findChild<QPlainTextEdit*>("actionLog")->toPlainText();
        const auto actionAt=actionLog.lastIndexOf(QString::fromUtf8("[UI] 전체파일 Unstage ↑ 클릭"));QVERIFY(actionAt>=0);
        QVERIFY(actionLog.indexOf("git ",actionAt)>actionAt);
        QVERIFY(file.exists());QVERIFY(other.exists());
        unstaged->item(0)->setCheckState(Qt::Checked);QTest::mouseClick(stage,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy()&&staged->count()==1,15000);
        // A title alone is valid, and the body remains optional.
        title->setText(QString::fromUtf8("새 파일 추가")); QVERIFY(commit->isEnabled());
        message->setPlainText(QString::fromUtf8("파일별 체크로 이번 커밋에 담을 변경을 골랐습니다."));
        unstaged->setCurrentRow(0); QTRY_VERIFY(!client->isBusy());
        QVERIFY(window.grab().save("prototype-screenshot.png"));
    }
    void graphicalHistory() {
        QSettings().clear();
        QTemporaryDir repo; QVERIFY(repo.isValid());
        auto git=[&](QStringList args) {
            QProcess p; p.setWorkingDirectory(repo.path()); p.start("git",args);
            if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR: ")+p.readAllStandardError();
            return p.readAllStandardOutput();
        };
        auto write=[&](const QString &path,const QByteArray &contents){QFile f(repo.filePath(path));if(!f.open(QIODevice::WriteOnly))return false;return f.write(contents)==contents.size();};
        git({"init","-b","main"});git({"config","user.name","GitCanvas Developer"});git({"config","user.email","developer@example.invalid"});
        QVERIFY(write("README.md","GitCanvas\n"));git({"add","."});git({"commit","-m",QString::fromUtf8("프로젝트 시작")});
        const auto root=git({"rev-parse","HEAD"}).trimmed();
        git({"switch","-c","feature/history"});
        const auto specialPath=QString::fromUtf8("커밋 기록.txt");
        QVERIFY(write(specialPath,"history view\n"));git({"add","."});git({"commit","-m",QString::fromUtf8("커밋 그래프 화면 추가"),"-m","Detailed body\nwith separators \x1e and \x1f preserved"});
        git({"switch","main"});QVERIFY(write("theme.txt","dark theme\n"));git({"add","."});git({"commit","-m",QString::fromUtf8("어두운 테마 색상 개선")});
        QVERIFY(!git({"merge","--no-ff","feature/history","-m",QString::fromUtf8("커밋 기록 GUI 병합")}).startsWith("ERROR:"));
        git({"tag","v0.1-preview"});
        const auto data=git({"log","--all","--topo-order","-100","-z","--format=%H%x00%P%x00%an%x00%ae%x00%aI%x00%D%x00%s%x00%b%x00%cI"});
        const auto commits=HistoryWidget::parseLog(data);QCOMPARE(commits.size(),4);
        QCOMPARE(commits[0].parents.size(),2);QCOMPARE(commits.last().hash.toUtf8(),root);
        const auto graph=HistoryWidget::buildGraph(commits);QCOMPARE(graph.size(),4);QCOMPARE(graph[0].parents.size(),2);
        QVERIFY(graph[0].parents[0].to!=graph[0].parents[1].to);QVERIFY(graph.last().parents.isEmpty());
        // Every edge leaving a row must arrive at the next row's node or continuing lane.
        for(int i=0;i+1<graph.size();++i){
            QVector<int> destinations;for(const auto &e:graph[i].parents)destinations.append(e.to);for(const auto &e:graph[i].through)destinations.append(e.to);
            for(int lane:destinations){bool connected=graph[i+1].incoming&&graph[i+1].lane==lane;for(const auto &e:graph[i+1].through)connected|=e.from==lane;QVERIFY(connected);}
        }
        MainWindow window;window.show();window.openRepository(repo.path());
        auto *client=window.findChild<GitClient *>();auto *table=window.findChild<HistoryTable *>("historyTable");
        auto *details=window.findChild<QPlainTextEdit *>("historyDetails");auto *files=window.findChild<QListWidget *>("historyFiles");
        window.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && table->rowCount()==4 && files->count()==1,15000);
        QCOMPARE(table->cellText(0,1),QString::fromUtf8("커밋 기록 GUI 병합"));
        QVERIFY(table->cellText(0,2).contains("v0.1-preview"));QCOMPARE(files->item(0)->text(),specialPath);
        QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(table->model()->index(0,1)).center());
        QTest::mouseDClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(table->model()->index(0,1)).center());
        auto *commitDialog=window.findChild<QDialog*>("historyCommitDialog");QTRY_VERIFY(commitDialog->isVisible());
        commitDialog->hide();auto *detailButton=window.findChild<QPushButton*>("historyOpenDetail");QVERIFY(detailButton->isEnabled());detailButton->click();QTRY_VERIFY(commitDialog->isVisible());
        QVERIFY(commitDialog->windowFlags().testFlag(Qt::WindowTitleHint));
        QCOMPARE(table->columnCount(),10);QVERIFY(table->horizontalScrollBar()->maximum()>0);
        QTest::mouseClick(files->viewport(),Qt::LeftButton,Qt::NoModifier,files->visualItemRect(files->item(0)).center());
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);
        QVERIFY(window.findChild<QPlainTextEdit *>("historyDiff")->toPlainText().contains("+history view"));
        auto *viewMode=commitDialog->findChild<QComboBox*>("diffViewMode");
        QCOMPARE(viewMode->count(),3);
        viewMode->setCurrentIndex(1);QVERIFY(commitDialog->grab().save("history-commit-screenshot.png"));viewMode->setCurrentIndex(2);viewMode->setCurrentIndex(0);
        auto *parents=window.findChild<QComboBox *>("historyParent");QCOMPARE(parents->count(),2);
        parents->setCurrentIndex(1);QMetaObject::invokeMethod(parents,"activated",Qt::DirectConnection,Q_ARG(int,1));
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);QCOMPARE(files->count(),1);QCOMPARE(files->item(0)->text(),QString("theme.txt"));
        commitDialog->hide();QVERIFY(window.grab().save("history-screenshot.png"));
        int featureRow=-1;for(int i=0;i<commits.size();++i)if(commits[i].subject==QString::fromUtf8("커밋 그래프 화면 추가"))featureRow=i;
        QVERIFY(featureRow>=0);table->setCurrentCell(featureRow,1);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);
        QVERIFY(details->toPlainText().contains("Detailed body\nwith separators \x1e and \x1f preserved"));
        table->setCurrentCell(3,1);QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);
        QCOMPARE(files->count(),1);QCOMPARE(files->item(0)->text(),QString("README.md"));
        window.findChild<QPushButton *>("historyCompareBase")->click();table->setCurrentCell(0,1);
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);QCOMPARE(files->count(),2);
        QTimer confirm;confirm.setInterval(10);int confirmations=0;
        connect(&confirm,&QTimer::timeout,&window,[&]{if(auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())){++confirmations;box->done(box->standardButtons().testFlag(QMessageBox::Yes)?QMessageBox::Yes:QMessageBox::Ok);}});confirm.start();
        auto *historyWidget=window.findChild<HistoryWidget *>();const auto before=git({"rev-parse","HEAD"}).trimmed();
        historyWidget->runHistoryAction({"reset","--soft",QString::fromUtf8(root)});
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy()&&confirmations>=2,15000);confirm.stop();
        QCOMPARE(git({"rev-parse","HEAD"}).trimmed(),root);
        QVERIFY(git({"for-each-ref","--format=%(objectname)","refs/gitcanvas/backups"}).contains(before));
        QVERIFY(!git({"diff","--cached","--name-only"}).isEmpty());
    }
    void partialDiffAndHistoryPaging() {
        QTemporaryDir repo;
        auto git=[&](QStringList args){QProcess p;p.setWorkingDirectory(repo.path());p.start("git",args);p.closeWriteChannel();if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput();};
        git({"init","-b","main"});git({"config","user.name","Diff Tester"});git({"config","user.email","test@example.invalid"});
        QByteArray original;for(int i=0;i<40;++i)original+="line "+QByteArray::number(i)+"\n";
        QFile file(repo.filePath(QString::fromUtf8("구간 file.txt")));QVERIFY(file.open(QIODevice::WriteOnly));file.write(original);file.close();git({"add","."});git({"commit","-m","oldest-search-target"});
        auto modified=original;modified.replace("line 2\n","changed 2\n");modified.replace("line 35\n","changed 35\n");
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(modified);file.close();
        GitClient client;client.setRepositoryPath(repo.path());DiffWidget diff(&client);diff.show();QSignalSpy applied(&diff,&DiffWidget::indexChanged);
        diff.loadWorking(QString::fromUtf8("구간 file.txt"),false);QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy(),15000);
        auto *hunks=diff.findChild<QComboBox *>("diffHunks");auto *apply=diff.findChild<QPushButton *>("applyHunk");QCOMPARE(hunks->count(),2);QVERIFY(apply->isEnabled());
        apply->click();QTRY_COMPARE_WITH_TIMEOUT(applied.count(),1,15000);
        auto staged=git({"diff","--cached"});QVERIFY(staged.contains("+changed 2"));QVERIFY(!staged.contains("+changed 35"));
        diff.loadWorking(QString::fromUtf8("구간 file.txt"),true);QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy(),15000);apply->click();QTRY_COMPARE_WITH_TIMEOUT(applied.count(),2,15000);QVERIFY(git({"diff","--cached"}).isEmpty());
        diff.loadWorking(QString::fromUtf8("구간 file.txt"),false);QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy(),15000);
        QVERIFY(file.open(QIODevice::Append));file.write("external edit\n");file.close();apply->click();QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy(),15000);QCOMPARE(applied.count(),2);QVERIFY(git({"diff","--cached"}).isEmpty());
        bool done=false,ok=true;client.inspectLimited({"show","HEAD:"+QString::fromUtf8("구간 file.txt")},[&](bool success,const QByteArray &,const QString &){done=true;ok=success;},10);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!ok);
        QFile encoded(repo.filePath("utf16.txt"));QVERIFY(encoded.open(QIODevice::WriteOnly));encoded.write(QByteArray::fromHex("fffe41004200"));encoded.close();
        diff.loadWorking("utf16.txt",false);QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy(),15000);diff.findChild<QComboBox *>("diffEncoding")->setCurrentIndex(1);QVERIFY(diff.unified()->toPlainText().contains("AB"));QVERIFY(!apply->isEnabled());
        auto parent=git({"rev-parse","HEAD"}).trimmed();const auto tree=git({"rev-parse","HEAD^{tree}"}).trimmed();
        for(int i=0;i<104;++i){parent=git({"commit-tree",QString::fromUtf8(tree),"-p",QString::fromUtf8(parent),"-m","page commit "+QString::number(i)}).trimmed();QVERIFY(!parent.startsWith("ERROR:"));}
        git({"update-ref","refs/heads/main",QString::fromUtf8(parent)});
        HistoryWidget history(&client);history.show();history.reload();auto *table=history.findChild<HistoryTable *>("historyTable");auto *more=history.findChild<QPushButton *>("historyMore");
        QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy()&&table->rowCount()==100,15000);QVERIFY(more->isEnabled());more->click();QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy()&&table->rowCount()==105,15000);QVERIFY(!more->isEnabled());
        auto *search=history.findChild<QLineEdit *>("historySearch");search->setText("oldest-search-target");history.findChild<QPushButton *>("historySearchButton")->click();QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy()&&table->rowCount()==1,15000);QCOMPARE(table->cellText(0,1),QString("oldest-search-target"));QVERIFY(table->isColumnHidden(0));
        search->clear();history.findChild<QLineEdit *>("historyAuthor")->setText("nonexistent-author");history.findChild<QPushButton *>("historySearchButton")->click();QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy(),15000);QCOMPARE(table->rowCount(),0);
        history.findChild<QLineEdit *>("historyAuthor")->clear();history.findChild<QLineEdit *>("historyPath")->setText(QString::fromUtf8("구간 file.txt"));history.findChild<QPushButton *>("historySearchButton")->click();QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy(),15000);QCOMPARE(table->rowCount(),1);
        auto *advanced=history.findChild<QPushButton*>("historyAdvancedSearch");
        auto *author=history.findChild<QLineEdit*>("historyAuthor");auto *since=history.findChild<QLineEdit*>("historySince");
        QVERIFY(!author->isVisible());
        QTimer::singleShot(0,[&]{
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);QCOMPARE(dialog->objectName(),QString("historySearchDialog"));
            author->setText("cancelled author");dialog->reject();
        });advanced->click();QVERIFY(author->text().isEmpty());
        QTimer::singleShot(0,[&]{
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);
            dialog->findChild<QPushButton*>("historyResetFilters")->click();
            since->setText("invalid date");dialog->findChild<QPushButton*>("historyApplyFilters")->click();QVERIFY(dialog->isVisible());
            QTimer::singleShot(0,[&]{
                auto *calendarDialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(calendarDialog);
                auto *calendar=calendarDialog->findChild<QCalendarWidget*>();QVERIFY(calendar);calendar->setSelectedDate(QDate(2000,1,2));calendarDialog->accept();
            });dialog->findChild<QPushButton*>("historySinceCalendar")->click();QCOMPARE(since->text(),QString("2000-01-02"));
            dialog->findChild<QPushButton*>("historyApplyFilters")->click();
        });advanced->click();QTRY_VERIFY_WITH_TIMEOUT(!client.isBusy()&&table->rowCount()==100,15000);
        QVERIFY(history.findChild<QLineEdit*>("historyPath")->text().isEmpty());
    }
    void workspaceEditing() {
        QSettings().clear(); QTemporaryDir a,b,c;
        for (const auto &path : {a.path(),b.path(),c.path()}) {
            QProcess p; p.setWorkingDirectory(path); p.start("git", {"init","-b","main"}); QVERIFY(p.waitForFinished(15000)); QCOMPARE(p.exitCode(),0);
        }
        MainWindow window;window.show(); auto *client=window.findChild<GitClient *>();
        auto *list=window.findChild<QListWidget *>("repositoryList");
        window.openRepository(a.path());QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);
        window.findChild<QLineEdit *>("commitTitle")->setText("persistent draft");
        window.findChild<QPlainTextEdit *>("commitMessage")->setPlainText("persistent body");
        auto menuAction=[&](const QString &actionName, std::function<void()> answer) {
            QTimer::singleShot(0,[actionName,answer] {
                auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());QVERIFY(menu);
                QAction *chosen=nullptr;for(auto *action:menu->actions())if(action->objectName()==actionName)chosen=action;
                QVERIFY(chosen);QTimer::singleShot(0,answer);QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(chosen).center());
            });
            list->customContextMenuRequested(list->visualItemRect(list->currentItem()).center());
        };
        menuAction("renameRepositoryEntry",[] {auto *dialog=qobject_cast<QInputDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);dialog->setTextValue("My repository");dialog->accept();});
        QVERIFY(list->currentItem()->text().contains("My repository"));
        menuAction("relocateRepositoryEntry",[&] {auto *dialog=qobject_cast<QFileDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);dialog->setDirectory(c.path());dialog->selectFile(c.path());QMetaObject::invokeMethod(dialog,"accept");});
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy()&&client->repositoryPath()==QFileInfo(c.path()).canonicalFilePath(),15000);
        QCOMPARE(list->count(),1);QVERIFY(list->currentItem()->text().contains("My repository"));
        QCOMPARE(window.findChild<QLineEdit *>("commitTitle")->text(),QString("persistent draft"));
        QCOMPARE(window.findChild<QPlainTextEdit *>("commitMessage")->toPlainText(),QString("persistent body"));
        window.openRepository(b.path());QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);
        QVERIFY(list->model()->moveRow({},1,{},0));
        QTRY_COMPARE(list->item(0)->data(Qt::UserRole+1).toString(),QFileInfo(b.path()).canonicalFilePath());
        QTRY_COMPARE(QSettings().value("workspace/repositories").toList().value(0).toMap().value("path").toString(),QFileInfo(b.path()).canonicalFilePath());
        menuAction("removeRepositoryEntry",[] {auto *dialog=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());QVERIFY(dialog);dialog->done(QMessageBox::Yes);});
        QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy()&&list->count()==1,15000);
        QVERIFY(QFileInfo::exists(b.filePath(".git")));
        QCOMPARE(client->repositoryPath(),QFileInfo(c.path()).canonicalFilePath());
        window.close();
        {
            MainWindow restored; auto *restoredClient=restored.findChild<GitClient *>();QTRY_VERIFY_WITH_TIMEOUT(!restoredClient->isBusy(),15000);
            QVERIFY(restored.findChild<QListWidget *>("repositoryList")->item(0)->text().contains("My repository"));
            QCOMPARE(restored.findChild<QLineEdit *>("commitTitle")->text(),QString("persistent draft"));
        }
        QSettings().clear();
    }
    void workspacePersistence() {
        QSettings().clear();QTemporaryDir a,b,c;
        for(const auto &path:{a.path(),b.path(),c.path()}){
            QProcess p;p.setWorkingDirectory(path);p.start("git",{"init","-b","main"});QVERIFY(p.waitForFinished(15000));QCOMPARE(p.exitCode(),0);
        }
        auto canonical=[](const QString &path){return QFileInfo(path).canonicalFilePath();};
        QSettings().setValue("repositoryPath",a.path());
        {
            MainWindow window;window.show();auto *client=window.findChild<GitClient *>();
            auto *list=window.findChild<QListWidget *>("repositoryList");
            auto *favorite=window.findChild<QPushButton *>("favoriteRepository");
            auto *up=window.findChild<QPushButton *>("repositoryUp");
            QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && list->count()==1,15000);
            window.findChild<QLineEdit *>("commitTitle")->setText("Draft for A");
            window.openRepository(b.path());QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && list->count()==2,15000);
            window.openRepository(c.path());QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && list->count()==3,15000);
            QVERIFY(QDir(c.path()).mkdir("subfolder"));window.openRepository(c.filePath("subfolder"));QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);QCOMPARE(list->count(),3);
            QTest::mouseClick(favorite,Qt::LeftButton);QCOMPARE(list->item(0)->data(Qt::UserRole+1).toString(),canonical(c.path()));
            list->setCurrentRow(2);QTest::mouseClick(favorite,Qt::LeftButton);
            QCOMPARE(list->item(1)->data(Qt::UserRole+1).toString(),canonical(b.path()));
            QTest::mouseClick(up,Qt::LeftButton);QCOMPARE(list->item(0)->data(Qt::UserRole+1).toString(),canonical(b.path()));
            QVERIFY(!up->isEnabled());
            QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(list->item(2)).center());
            QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy() && client->repositoryPath()==canonical(a.path()),15000);
            QCOMPARE(window.findChild<QLineEdit *>("commitTitle")->text(),QString("Draft for A"));
            QVERIFY(window.grab().save("workspace-screenshot.png"));
        }
        {
            MainWindow window;auto *client=window.findChild<GitClient *>();auto *list=window.findChild<QListWidget *>("repositoryList");
            QTRY_VERIFY_WITH_TIMEOUT(!client->isBusy(),15000);QCOMPARE(list->count(),3);
            QCOMPARE(list->item(0)->data(Qt::UserRole+1).toString(),canonical(b.path()));
            QCOMPARE(list->item(1)->data(Qt::UserRole+1).toString(),canonical(c.path()));
            QVERIFY(list->item(0)->text().startsWith(QString::fromUtf8("★")));
              QCOMPARE(client->repositoryPath(),canonical(a.path()));QCOMPARE(list->currentRow(),2);
              QCOMPARE(window.findChild<QLineEdit *>("commitTitle")->text(),QString("Draft for A"));
            QVERIFY(!window.findChild<QPushButton *>("repositoryUp")->isEnabled());
        }
        QSettings().clear();
    }
    void remoteOperations() {
        QTemporaryDir local, remote, peer;
        QVERIFY(local.isValid() && remote.isValid() && peer.isValid());
        auto git=[](const QString &path,QStringList args) {
            QProcess p; p.setWorkingDirectory(path); p.start("git",args);
            if(!p.waitForFinished(15000) || p.exitCode()!=0) return QByteArray("ERROR: ")+p.readAllStandardError();
            return p.readAllStandardOutput();
        };
        git(remote.path(),{"init","--bare","-b","main"});
        git(local.path(),{"init","-b","main"});
        git(local.path(),{"config","user.name","Test"}); git(local.path(),{"config","user.email","test@example.invalid"});
        git(local.path(),{"commit","--allow-empty","-m","initial"});
        git(local.path(),{"remote","add","origin",remote.path()});
        git(local.path(),{"config","branch.main.remote","origin"});
        git(local.path(),{"config","branch.main.merge","refs/heads/main"});
        GitClient client; client.setRepositoryPath(local.path());
        bool done=false, succeeded=false;
        auto completed=[&](bool ok,const QByteArray &,const QString &){done=true;succeeded=ok;};
        client.sync("push",completed); QTRY_VERIFY_WITH_TIMEOUT(done,15000); QVERIFY(succeeded);
        QVERIFY(!git(remote.path(),{"rev-parse","refs/heads/main"}).startsWith("ERROR:"));
        QVERIFY(!git(peer.path(),{"clone",remote.path(),"."}).startsWith("ERROR:"));
        git(peer.path(),{"config","user.name","Peer"}); git(peer.path(),{"config","user.email","peer@example.invalid"});
        git(peer.path(),{"commit","--allow-empty","-m","peer change"});
        QVERIFY(!git(peer.path(),{"push"}).startsWith("ERROR:"));
        done=false; client.sync("fetch",completed); QTRY_VERIFY_WITH_TIMEOUT(done,15000); QVERIFY(succeeded);
        done=false; client.sync("pull",completed); QTRY_VERIFY_WITH_TIMEOUT(done,15000); QVERIFY(succeeded);
        QCOMPARE(git(local.path(),{"log","-1","--format=%s"}).trimmed(),QByteArray("peer change"));
        SyncController sync(&client);
        QSignalSpy syncCompleted(&sync,&SyncController::completed),errors(&sync,&SyncController::failed);
        QCOMPARE(sync.action(),SyncController::Action::Fetch);
        git(local.path(),{"commit","--allow-empty","-m","local ahead"});
        sync.execute();QTRY_VERIFY_WITH_TIMEOUT(!sync.working()&&!client.isBusy()&&syncCompleted.count()==1,15000);
        QCOMPARE(errors.count(),0);QCOMPARE(sync.action(),SyncController::Action::Push);
        // Fetch alone must never publish the pending local commit.
        QCOMPARE(git(remote.path(),{"log","-1","--format=%s"}).trimmed(),QByteArray("peer change"));
        sync.execute();QTRY_COMPARE_WITH_TIMEOUT(syncCompleted.count(),2,15000);
        QCOMPARE(errors.count(),0);QCOMPARE(sync.action(),SyncController::Action::Fetch);
        QCOMPARE(git(remote.path(),{"log","-1","--format=%s"}).trimmed(),QByteArray("local ahead"));
        git(peer.path(),{"pull","--ff-only"});git(peer.path(),{"commit","--allow-empty","-m","remote ahead"});git(peer.path(),{"push"});
        sync.execute();QTRY_COMPARE_WITH_TIMEOUT(syncCompleted.count(),3,15000);QCOMPARE(sync.action(),SyncController::Action::Pull);
        sync.execute();QTRY_COMPARE_WITH_TIMEOUT(syncCompleted.count(),4,15000);QCOMPARE(errors.count(),0);
        QCOMPARE(git(local.path(),{"log","-1","--format=%s"}).trimmed(),QByteArray("remote ahead"));
        git(peer.path(),{"commit","--allow-empty","-m","diverged remote"});git(peer.path(),{"push"});
        git(local.path(),{"commit","--allow-empty","-m","diverged local"});
        sync.execute();QTRY_COMPARE_WITH_TIMEOUT(syncCompleted.count(),5,15000);QCOMPARE(sync.action(),SyncController::Action::Diverged);
        QSignalSpy mergeRequested(&sync,&SyncController::mergeRequested);sync.execute();QTRY_COMPARE_WITH_TIMEOUT(mergeRequested.count(),1,15000);QCOMPARE(errors.count(),0);QCOMPARE(mergeRequested.first().first().toString(),QString("refs/remotes/origin/main"));
        QCOMPARE(git(remote.path(),{"log","-1","--format=%s"}).trimmed(),QByteArray("diverged remote"));
        MergeController merger(&client);MergePreview preview;done=false;merger.preview(mergeRequested.first().first().toString(),[&](bool ok,MergePreview value,QString){done=true;succeeded=ok;preview=value;});QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(succeeded);done=false;merger.merge(preview,completed);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(succeeded);
        sync.invalidate();sync.execute();QTRY_COMPARE_WITH_TIMEOUT(syncCompleted.count(),6,15000);QCOMPARE(sync.action(),SyncController::Action::Push);sync.execute();QTRY_COMPARE_WITH_TIMEOUT(syncCompleted.count(),7,15000);QCOMPARE(git(remote.path(),{"rev-parse","HEAD"}),git(local.path(),{"rev-parse","HEAD"}));
        // A failed fetch must discard any permission to pull or push.
        sync.invalidate();git(local.path(),{"remote","set-url","origin",local.filePath("missing-remote")});
        sync.execute();QTRY_COMPARE_WITH_TIMEOUT(syncCompleted.count(),8,15000);QCOMPARE(sync.action(),SyncController::Action::Fetch);QCOMPARE(errors.count(),1);
    }
    void deleteCurrentAndRemoteBranch() {
        QTemporaryDir repo,remote;
        auto git=[](const QString &path,QStringList args){QProcess p;p.setWorkingDirectory(path);p.start("git",args);if(!p.waitForFinished(15000)||p.exitCode()!=0)return QByteArray("ERROR: ")+p.readAllStandardError();return p.readAllStandardOutput();};
        git(remote.path(),{"init","--bare","-b","main"});git(repo.path(),{"init","-b","main"});
        git(repo.path(),{"config","user.name","Delete test"});git(repo.path(),{"config","user.email","test@example.invalid"});
        git(repo.path(),{"commit","--allow-empty","-m","base"});git(repo.path(),{"remote","add","origin",remote.path()});git(repo.path(),{"push","-u","origin","main"});
        git(repo.path(),{"switch","-c","feature-delete"});git(repo.path(),{"commit","--allow-empty","-m","feature"});git(repo.path(),{"push","-u","origin","feature-delete"});
        GitClient client;client.setRepositoryPath(repo.path());bool done=false,success=false;
        auto callback=[&](bool ok,const QByteArray &,const QString &){done=true;success=ok;};
        client.deleteCurrentBranch("feature-delete","main",true,"origin","refs/heads/feature-delete",callback);
        QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);
        QCOMPARE(git(repo.path(),{"branch","--show-current"}).trimmed(),QByteArray("main"));
        QVERIFY(git(repo.path(),{"branch","--list","feature-delete"}).isEmpty());
        QVERIFY(git(remote.path(),{"show-ref","--verify","refs/heads/feature-delete"}).startsWith("ERROR:"));
        git(repo.path(),{"switch","-c","unmerged"});git(repo.path(),{"commit","--allow-empty","-m","unmerged"});
        done=false;client.deleteCurrentBranch("unmerged","main",false,{},{},callback);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(!success);
        QVERIFY(git(repo.path(),{"branch","--list","unmerged"}).contains("unmerged"));
        git(repo.path(),{"switch","unmerged"});done=false;client.deleteCurrentBranch("unmerged","main",true,{},{},callback);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);
        QVERIFY(git(repo.path(),{"branch","--list","unmerged"}).isEmpty());
        // The last local branch can be removed while retaining its commit in detached HEAD.
        done=false;client.deleteCurrentBranch("main",{},false,{},{},callback);QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY(success);
        QVERIFY(!git(repo.path(),{"rev-parse","HEAD"}).startsWith("ERROR:"));QVERIFY(git(repo.path(),{"branch","--list","main"}).isEmpty());
    }
    void missingGitReportsError() {
        GitClient client; QTemporaryDir repo; client.setRepositoryPath(repo.path());
        const auto oldPath=qgetenv("PATH"); qputenv("PATH",repo.path().toUtf8());
        bool done=false, succeeded=true; QString error;
        client.checkRepository([&](bool ok,const QByteArray &,const QString &e){done=true;succeeded=ok;error=e;});
        QTRY_VERIFY_WITH_TIMEOUT(done,15000);
        qputenv("PATH",oldPath);
        QVERIFY(!succeeded); QVERIFY(!error.isEmpty()); QVERIFY(!client.isBusy());
    }
};
int main(int argc,char **argv) {
    if(qEnvironmentVariableIsSet("GITCANVAS_FAKE_SSH")) {
        QCoreApplication app(argc,argv);const auto failure=qgetenv("GITCANVAS_SSH_FAILURE");if(failure=="hang"){QThread::msleep(30000);return 1;}if(failure=="hostkey"){fprintf(stderr,"REMOTE HOST IDENTIFICATION HAS CHANGED!\n");return 255;}
        if(qEnvironmentVariableIsSet("GH_TOKEN")){fprintf(stderr,"Local token leaked\n");return 1;}
        if(QFileInfo(app.applicationFilePath()).baseName()=="ssh-keyscan"){const auto blob=QByteArray::fromHex("0000000b7373682d6564323535313900000020")+QByteArray(32,'x');printf("example.invalid ssh-ed25519 %s\n",blob.toBase64().constData());return 0;}
        QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 2;const auto request=QJsonDocument::fromJson(input.readAll()).object();QFile capture(QDir(qEnvironmentVariable("GITCANVAS_FAKE_SSH")).filePath("request.json"));if(capture.open(QIODevice::WriteOnly))capture.write(QJsonDocument(QJsonObject{{"input",request},{"command",app.arguments().last()}}).toJson());
        QJsonObject result;if(request.value("action")=="probe")result={{"os","posix"},{"python",QJsonArray{3,11,0}},{"platform","Linux"},{"user","dev"},{"git","git version 2.45.1"},{"home","/home/dev"}};
        else if(request.value("action")=="status")result={{"repository",request.value("repository")},{"common_dir","/srv/repo/.git"},{"fingerprint","test"},{"branch","main"},{"branches",QJsonArray{"main"}},{"files",QJsonArray{QJsonObject{{"path","file.txt"},{"status"," M"}}}}};
        const auto bytes=QJsonDocument(QJsonObject{{"type","result"},{"version",1},{"ok",true},{"result",result}}).toJson(QJsonDocument::Compact)+"\n";fwrite(bytes.constData(),1,bytes.size(),stdout);fflush(stdout);return 0;
    }
    if(qEnvironmentVariableIsSet("GITCANVAS_FAKE_GH")) {
        QCoreApplication app(argc,argv);const auto args=app.arguments();const auto failure=qgetenv("GITCANVAS_GH_FAILURE");
        if(failure=="hang"){QThread::msleep(30000);return 1;}
        if(!failure.isEmpty()){fprintf(stderr,"HTTP 401 secret-from-stderr\n");return 1;}
        if(qEnvironmentVariableIsSet("GH_TOKEN")){fprintf(stderr,"environment token unexpectedly inherited\n");return 1;}
        QFile active(QDir(QString::fromUtf8(qgetenv("GITCANVAS_FAKE_GH"))).filePath("account"));QString login="alice";if(active.open(QIODevice::ReadOnly)){login=QString::fromUtf8(active.readAll());active.close();}
        auto json=[](const QJsonDocument &doc){const auto bytes=doc.toJson(QJsonDocument::Compact);fwrite(bytes.constData(),1,bytes.size(),stdout);fflush(stdout);return 0;};
        if(args.contains("--version")){printf("gh version 2.80.0\n");return 0;}
        if(args.contains("switch")){if(!active.open(QIODevice::WriteOnly))return 1;active.write(args.value(args.indexOf("--user")+1).toUtf8());return 0;}
        if(args.contains("login")){fprintf(stderr,"First copy your one-time code: ABCD-1234\nOpen this URL to continue in your web browser: https://github.com/login/device\n");return 0;}
        if(args.contains("status")){QJsonArray accounts;const auto host=args.value(args.indexOf("--hostname")+1);for(const auto &name:QStringList{"alice","bob"})accounts.append(QJsonObject{{"login",name},{"active",name==login},{"host",host},{"state","success"},{"scopes","repo, read:org"},{"tokenSource","keyring"},{"token","secret-from-json"}});return json(QJsonDocument(QJsonObject{{"hosts",QJsonObject{{host,accounts}}}}));}
        if(args.last()=="user")return json(QJsonDocument(QJsonObject{{"login",login},{"id",123}}));
        if(args.last().startsWith("repos/")||args.last()=="graphql") {
            const auto endpoint=args.last();const auto repo=endpoint=="graphql"?QString("alice/repo"):endpoint.section('/',1,2);const auto root="repos/"+repo;
            const auto method=args.value(args.indexOf("--method")+1,"GET");
            QFile state(QDir(QString::fromUtf8(qgetenv("GITCANVAS_FAKE_GH"))).filePath("pr.json"));
            QJsonObject pr{{"node_id","PR_mock"},{"number",1},{"title","Review changes"},{"body","PR description"},{"user",QJsonObject{{"login","bob"}}},{"state","open"},{"merged",false},{"draft",false},{"mergeable",true},{"mergeable_state","clean"},{"updated_at","initial"},{"changed_files",101},{"additions",101},{"deletions",101},
                {"head",QJsonObject{{"sha",QString(40,'a')},{"ref","topic"},{"label","bob:topic"},{"repo",QJsonObject{{"full_name","bob/repo"}}}}},
                {"base",QJsonObject{{"sha",QString(40,'b')},{"ref","main"},{"label","alice:main"},{"repo",QJsonObject{{"full_name",repo}}}}}};
            if(state.open(QIODevice::ReadOnly)){pr=QJsonDocument::fromJson(state.readAll()).object();state.close();}
            if(qEnvironmentVariableIsSet("GITCANVAS_PR_HEAD")){auto head=pr.value("head").toObject();head["sha"]=qEnvironmentVariable("GITCANVAS_PR_HEAD");pr["head"]=head;}
            if(qEnvironmentVariableIsSet("GITCANVAS_PR_MERGE_STATE"))pr["mergeable_state"]=qEnvironmentVariable("GITCANVAS_PR_MERGE_STATE");
            if(method!="GET") {
                QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 1;const auto body=QJsonDocument::fromJson(input.readAll()).object();
                QFile record(QDir(QString::fromUtf8(qgetenv("GITCANVAS_FAKE_GH"))).filePath("writes.jsonl"));if(!record.open(QIODevice::Append))return 1;record.write(QJsonDocument(QJsonObject{{"method",method},{"endpoint",endpoint},{"body",body}}).toJson(QJsonDocument::Compact)+"\n");record.close();
                if(qEnvironmentVariable("GITCANVAS_PR_FAILURE")=="409"){fprintf(stderr,"HTTP 409 changed head\n");return 1;}
                if(endpoint=="graphql")pr["draft"]=false;
                else if(endpoint.endsWith("/merge")) {
                    if(qEnvironmentVariable("GITCANVAS_PR_FAILURE")=="not-merged")return json(QJsonDocument(QJsonObject{{"merged",false}}));
                    if(body.value("sha")!=pr.value("head").toObject().value("sha")){fprintf(stderr,"HTTP 409\n");return 1;}pr["merged"]=true;pr["state"]="closed";
                } else if(method=="PATCH")pr["state"]=body.value("state");
                else if(endpoint.endsWith("/pulls")){pr["number"]=2;pr["state"]="open";pr["merged"]=false;pr["title"]=body.value("title");pr["body"]=body.value("body");pr["draft"]=body.value("draft");}
                pr["updated_at"]=pr.value("updated_at").toString()+"-write";
                if(!state.open(QIODevice::WriteOnly))return 1;state.write(QJsonDocument(pr).toJson());state.close();
                if(endpoint=="graphql")return json(QJsonDocument(QJsonObject{{"data",QJsonObject{{"markPullRequestReadyForReview",QJsonObject{{"pullRequest",QJsonObject{{"isDraft",false}}}}}}}}));
                return json(QJsonDocument(pr));
            }
            if(endpoint.contains("/compare/"))return json(QJsonDocument(QJsonObject{{"status","ahead"},{"total_commits",1},{"commits",QJsonArray{QJsonObject{{"sha",QString(40,'a')},{"commit",QJsonObject{{"message","Topic commit"}}}}}},{"files",QJsonArray{QJsonObject{{"filename","file.txt"},{"patch","@@ -1 +1 @@\n-old\n+new"}}}}}));
            if(endpoint.contains("/contents/"))return json(QJsonDocument(QJsonObject{{"encoding","base64"},{"content",QString::fromLatin1(QByteArray("## Summary\n").toBase64())}}));
            if(endpoint==root)return json(QJsonDocument(QJsonObject{{"permissions",QJsonObject{{"push",true}}},{"allow_merge_commit",true},{"allow_squash_merge",true},{"allow_rebase_merge",true}}));
            if(endpoint.contains("/commits?"))return json(QJsonDocument(QJsonArray{QJsonObject{{"sha",QString(40,'a')},{"commit",QJsonObject{{"message","Topic commit"}}}}}));
            if(endpoint.contains("?state=open&head="))return json(QJsonDocument(qEnvironmentVariableIsSet("GITCANVAS_PR_DUPLICATE")?QJsonArray{pr}:QJsonArray()));
            if(endpoint.startsWith(root+"/branches/"))return json(QJsonDocument(QJsonObject{{"name",endpoint.section('/',-1)},{"commit",QJsonObject{{"sha",QString(40,'a')}}}}));
            if(endpoint.startsWith(root+"/branches?"))return json(QJsonDocument(QJsonArray{QJsonObject{{"name","main"}},QJsonObject{{"name","topic"}}}));
            if(endpoint.startsWith(root+"/pulls?"))return json(QJsonDocument(QJsonArray{pr}));
            if(endpoint.contains("/files?")){QJsonArray files;const bool second=endpoint.contains("page=2");for(int i=0;i<(second?1:100);++i)files.append(QJsonObject{{"filename",QString("file%1.txt").arg(second?100:i)},{"status","modified"},{"additions",1},{"deletions",1},{"patch","@@ -1 +1 @@\n-before\n+after"}});return json(QJsonDocument(files));}
            if(endpoint.contains("/check-runs?")){const auto check=qEnvironmentVariable("GITCANVAS_PR_CHECK","success");return json(QJsonDocument(QJsonObject{{"total_count",1},{"check_runs",QJsonArray{QJsonObject{{"name","build"},{"status",check=="pending"?"in_progress":"completed"},{"conclusion",check}}}}}));}
            if(endpoint.endsWith("/status"))return json(QJsonDocument(QJsonObject{{"state","success"},{"total_count",1},{"statuses",QJsonArray{QJsonObject{{"context","test"},{"state","success"}}}}}));
            if(endpoint.contains("/comments?"))return json(QJsonDocument(QJsonArray{QJsonObject{{"body","Existing comment"},{"user",QJsonObject{{"login","carol"}}}}}));
            if(endpoint.contains("/reviews?"))return json(QJsonDocument(QJsonArray{QJsonObject{{"body","Reviewed"},{"state","APPROVED"},{"user",QJsonObject{{"login","carol"}}}}}));
            return json(QJsonDocument(pr));
        }
        if(args.last().startsWith("user/repos?")){QJsonArray repos;int count=args.last().contains("page=2&")?1:100;for(int i=0;i<count;++i)repos.append(QJsonObject{{"full_name",login+"/repo"+QString::number(i)},{"owner",QJsonObject{{"type",i%2?"Organization":"User"}}},{"visibility","private"},{"permissions",QJsonObject{{"pull",true},{"push",true}}},{"updated_at","2026-09-14T00:00:00Z"},{"clone_url","https://github.com/"+login+"/repo.git"},{"html_url","https://github.com/"+login+"/repo"}});return json(QJsonDocument(repos));}
        return 0;
    }
    if(argc>1&&qstrcmp(argv[1],"--gitcanvas-rebase-editor")==0){QCoreApplication app(argc,argv);const int result=runRebaseEditor(app.arguments());if(result)fprintf(stderr,"GitCanvas editor rejected plan (%d)\n",result);return result;}
    if(qEnvironmentVariableIsSet("GITCANVAS_FAKE_GIT")) {
        QCoreApplication app(argc,argv);
        if(app.arguments().contains("--child")){QThread::msleep(30000);return 0;}
        if(app.arguments().contains("update-ref")){QThread::msleep(900);return 0;}
        QProcess child;child.start(app.applicationFilePath(),{"--child"});if(!child.waitForStarted(5000))return 1;
        QFile pid(QString::fromUtf8(qgetenv("GITCANVAS_CHILD_PID")));if(!pid.open(QIODevice::WriteOnly))return 2;pid.write(QByteArray::number(child.processId()));pid.close();
        QThread::msleep(30000);return 0;
    }
    QApplication app(argc,argv); app.setOrganizationName("GitCanvasTests"); app.setApplicationName("Prototype");
    if(app.arguments().contains("--export-git-manuals")){
        auto run=[](QStringList args){QProcess p;p.start("git",args);p.closeWriteChannel();p.waitForFinished(15000);return QString::fromUtf8(p.readAllStandardOutput()).trimmed();};
        const auto docs=run({"--html-path"});QJsonObject commands;
        for(const auto &name:run({"--list-cmds=main"}).split('\n',Qt::SkipEmptyParts)){
            QFile file(QDir(docs).filePath("git-"+name+".html"));if(!file.open(QIODevice::ReadOnly))continue;
            const auto manual=GitCommandManual::parse(QString::fromUtf8(file.readAll()));QJsonArray options;
            for(const auto &option:manual.options)options.append(QJsonObject{{"name",option.name},{"syntax",option.syntax},{"description",option.description}});
            commands.insert(name,QJsonObject{{"summary",manual.summary},{"description",manual.description},{"options",options}});
        }
        QFile output("git-manual-source.json");if(!output.open(QIODevice::WriteOnly))return 1;output.write(QJsonDocument(commands).toJson());return 0;
    }
    QTemporaryDir settings; QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    PrototypeTest tests; return QTest::qExec(&tests,argc,argv);
}
#include "prototype_test.moc"
