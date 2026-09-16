#pragma once
#include <QStringList>
class QProcessEnvironment;
// Headless mode used only by Git's editor hooks, in the app and test executable.
int runRebaseEditor(const QStringList &arguments);
void configureRebaseEditor(QProcessEnvironment &environment,const QString &gitDirectory,bool starting);
void finishRebaseEditor(const QString &gitDirectory);
