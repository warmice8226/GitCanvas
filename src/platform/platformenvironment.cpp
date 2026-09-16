#include "platformenvironment.h"
#include <QStringList>

QString PlatformEnvironment::macSearchPath(const QString &existing){
    // Preserve explicit launcher/terminal paths. Finder normally supplies only
    // system paths; insert package-manager tools ahead of those system defaults.
    const QStringList defaults{"/usr/bin","/bin","/usr/sbin","/sbin"};
    QStringList custom,system;
    for(const auto &path:existing.split(':',Qt::SkipEmptyParts)){
        if(!path.startsWith('/'))continue; // Never add cwd to executable lookup.
        auto &target=defaults.contains(path)?system:custom;if(!target.contains(path))target.append(path);
    }
    for(const auto &path:QStringList{"/opt/homebrew/bin","/usr/local/bin"})if(!custom.contains(path))custom.append(path);
    for(const auto &path:defaults)if(!system.contains(path))system.append(path);
    return (custom+system).join(':');
}
void PlatformEnvironment::initialize(){
#ifdef Q_OS_MACOS
    qputenv("PATH",macSearchPath(qEnvironmentVariable("PATH")).toUtf8());
#endif
}
