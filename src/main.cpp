#include "mainwindow.h"
#include "settings/applanguage.h"
#include "platform/platformenvironment.h"
#include <QSettings>
#include <QLocale>

#include <QApplication>
#include <QIcon>
#include <QCoreApplication>
#include <QTimer>
#include "history/rebaseeditor.h"

int main(int argc, char *argv[])
{
    PlatformEnvironment::initialize();
    if(argc>1&&qstrcmp(argv[1],"--gitcanvas-rebase-editor")==0){QCoreApplication app(argc,argv);const int result=runRebaseEditor(app.arguments());if(result)fprintf(stderr,"GitCanvas editor rejected plan (%d)\n",result);return result;}
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/app/gitcanvas-scroll.png"));
    QCoreApplication::setApplicationName("GitCanvas");
    QCoreApplication::setOrganizationName("GitCanvas");
    QCoreApplication::setApplicationVersion("0.1.0");
    if(app.arguments().contains("--smoke-test"))QCoreApplication::setApplicationName("GitCanvasSmoke");
    AppLanguage language;
    const auto chosen=AppLanguage::effectiveLanguage(QSettings().value("ui/language","system").toString());
    QLocale::setDefault(QLocale(chosen=="ko"?"ko_KR":"en_US"));
    if(chosen=="en")app.installTranslator(&language);

    MainWindow window;
    window.show();
    const auto args = app.arguments();
    if(args.contains("--smoke-test"))QTimer::singleShot(1500,&app,&QCoreApplication::quit);
    const int repoOption = args.indexOf("--repository");
    if (repoOption >= 0 && repoOption + 1 < args.size()) {
        QTimer::singleShot(0, &window, [&window, args, repoOption] { window.openRepository(args.at(repoOption + 1)); });
    }

    return app.exec();
}

