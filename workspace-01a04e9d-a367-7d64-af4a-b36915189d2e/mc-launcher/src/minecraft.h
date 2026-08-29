#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace mcl {

class CatalogWorker final : public QObject {
    Q_OBJECT
public slots:
    void run();

signals:
    void loaded(const QByteArray &manifestJson);
    void failed(const QString &message);
};

class InstallerWorker final : public QObject {
    Q_OBJECT
public:
    InstallerWorker(QString versionId, QString versionUrl, QString gameDir);

public slots:
    void run();

signals:
    void progress(qint64 current, qint64 total, const QString &label);
    void logMessage(const QString &message);
    void finished(const QString &versionId, const QString &versionJsonPath);
    void failed(const QString &message);

private:
    bool downloadFile(const QString &url,
                      const QString &destination,
                      const QString &sha1,
                      const QString &label,
                      QString *error);
    bool installLibraries(const QJsonArray &libraries, const QString &gameDir, QString *error);
    bool installAssets(const QJsonObject &version, const QString &gameDir, QString *error);
    bool installLogging(const QJsonObject &version, const QString &gameDir, QString *error);

    QString versionId_;
    QString versionUrl_;
    QString gameDir_;
};

class FabricInstallerWorker final : public QObject {
    Q_OBJECT
public:
    FabricInstallerWorker(QString minecraftVersion, QString gameDir);

public slots:
    void run();

signals:
    void progress(qint64 current, qint64 total, const QString &label);
    void logMessage(const QString &message);
    void finished(const QString &profileId, const QString &profileJsonPath);
    void failed(const QString &message);

private:
    QString minecraftVersion_;
    QString gameDir_;
};

class QuiltInstallerWorker final : public QObject {
    Q_OBJECT
public:
    QuiltInstallerWorker(QString minecraftVersion, QString gameDir);

public slots:
    void run();

signals:
    void progress(qint64 current, qint64 total, const QString &label);
    void logMessage(const QString &message);
    void finished(const QString &profileId, const QString &profileJsonPath);
    void failed(const QString &message);

private:
    QString minecraftVersion_;
    QString gameDir_;
};

class ForgeInstallerWorker final : public QObject {
    Q_OBJECT
public:
    ForgeInstallerWorker(QString minecraftVersion, QString gameDir, QString javaPath);

public slots:
    void run();

signals:
    void progress(qint64 current, qint64 total, const QString &label);
    void logMessage(const QString &message);
    void finished(const QString &profileId, const QString &profileJsonPath);
    void failed(const QString &message);

private:
    QString minecraftVersion_;
    QString gameDir_;
    QString javaPath_;
};

class NewsWorker final : public QObject {
    Q_OBJECT
public slots:
    void run();

signals:
    void loaded(const QString &html);
    void failed(const QString &message);
};

} // namespace mcl
