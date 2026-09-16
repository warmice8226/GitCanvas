#include "applanguage.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
AppLanguage::AppLanguage(QObject *parent):QTranslator(parent){QFile file(":/i18n/en.json");if(file.open(QIODevice::ReadOnly)){const auto values=QJsonDocument::fromJson(file.readAll()).object();for(auto it=values.begin();it!=values.end();++it)english_.insert(it.key(),it.value().toString());}}
QString AppLanguage::translate(const char *,const char *source,const char *,int)const{return english_.value(QString::fromUtf8(source));}
QString AppLanguage::effectiveLanguage(const QString &choice){if(choice=="ko"||choice=="en")return choice;return QLocale::system().language()==QLocale::Korean?"ko":"en";}
