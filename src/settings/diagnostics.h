#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
class GitClient;
class Diagnostics final : public QObject {
    Q_OBJECT
public:
    explicit Diagnostics(GitClient *git,QObject *parent=nullptr,const QString &storageDirectory={});
    static QString redact(QString text);
    static QJsonObject classify(const QString &error,int exitCode,bool changed);
    QJsonObject report() const;
    bool exportReport(const QString &path,QString *error=nullptr) const;
    void recordAction(const QString &identifier);
    void clear();
    void setRetention(bool enabled);
    QString storageError() const { return storageError_; }
    QJsonArray entries() const { return entries_; }
signals:
    void changed();
    void storageErrorChanged();
private:
    void append(QJsonObject entry);
    void persist();
    void removeStored();
    QJsonArray entries_;
    QString directory_;
    QString storageError_;
};
