#pragma once
#include <QTranslator>
#include <QHash>
class AppLanguage final : public QTranslator {
public:
    explicit AppLanguage(QObject *parent=nullptr);
    bool isEmpty() const override { return false; }
    QString translate(const char *,const char *source,const char * = nullptr,int = -1) const override;
    static QString effectiveLanguage(const QString &choice);
private:
    QHash<QString,QString> english_;
};
