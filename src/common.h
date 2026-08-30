#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QHash>

namespace mcl {

struct Account {
    QString id;
    QString name;
    QString uuid;
    QString xuid;
    QString userHash;
    QString accessToken;
    QString userType = QStringLiteral("msa");
    qint64 expiresAt = 0;

    bool isAuthenticated() const;
};

QJsonObject accountToJson(const Account &account);
Account accountFromJson(const QJsonObject &object);

QString fileSha1(const QString &path);
bool writeJsonFile(const QString &path, const QJsonDocument &document, QString *error = nullptr);
QJsonDocument readJsonFile(const QString &path, QString *error = nullptr);
QStringList splitCommandLine(const QString &value);
QString replaceVariables(QString value, const QHash<QString, QString> &variables);
QString defaultOAuthClientId();

namespace Rules {
    QString currentOsName();
    QString currentOsArch();
    QString nativeArch();
    bool allowed(const QJsonArray &rules, const QHash<QString, bool> &features = {});
}

} // namespace mcl

Q_DECLARE_METATYPE(mcl::Account)
