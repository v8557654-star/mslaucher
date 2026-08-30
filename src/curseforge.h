#pragma once

#include <QObject>
#include <QString>

namespace mcl {

class CurseForgeSearchWorker final : public QObject {
    Q_OBJECT
public:
    CurseForgeSearchWorker(QString query,
                           QString gameVersion,
                           QString loader,
                           QString apiKey);

public slots:
    void run();

signals:
    void loaded(const QByteArray &json);
    void failed(const QString &message);

private:
    QString query_;
    QString gameVersion_;
    QString loader_;
    QString apiKey_;
};

class CurseForgeInstallWorker final : public QObject {
    Q_OBJECT
public:
    CurseForgeInstallWorker(QString modId,
                            QString gameVersion,
                            QString loader,
                            QString gameDir,
                            QString apiKey);

public slots:
    void run();

signals:
    void progress(qint64 current, qint64 total, const QString &label);
    void logMessage(const QString &message);
    void finished(const QString &modId);
    void failed(const QString &message);

private:
    int loaderType() const;
    QString apiUrl(const QString &path) const;

    QString modId_;
    QString gameVersion_;
    QString loader_;
    QString gameDir_;
    QString apiKey_;
};

QString curseForgeApiKey();

} // namespace mcl
