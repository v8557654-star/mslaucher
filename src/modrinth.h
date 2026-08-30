#pragma once

#include <QObject>
#include <QSet>
#include <QString>

namespace mcl {

class ModrinthSearchWorker final : public QObject {
    Q_OBJECT
public:
    ModrinthSearchWorker(QString query, QString gameVersion, QString loader, QString projectType);

public slots:
    void run();

signals:
    void loaded(const QByteArray &json);
    void failed(const QString &message);

private:
    QString query_;
    QString gameVersion_;
    QString loader_;
    QString projectType_;
};

class ModrinthInstallWorker final : public QObject {
    Q_OBJECT
public:
    ModrinthInstallWorker(QString projectId,
                          QString gameVersion,
                          QString loader,
                          QString projectType,
                          QString gameDir);

public slots:
    void run();

signals:
    void progress(qint64 current, qint64 total, const QString &label);
    void logMessage(const QString &message);
    void finished(const QString &projectId);
    void failed(const QString &message);

private:
    bool installProject(const QString &projectId,
                        QSet<QString> *installed,
                        QString *error);
    bool installModpack(QString *error);
    bool downloadFile(const QString &url,
                      const QString &destination,
                      const QString &sha1,
                      const QString &label,
                      QString *error);

    QString projectId_;
    QString gameVersion_;
    QString loader_;
    QString projectType_;
    QString gameDir_;
};

} // namespace mcl
