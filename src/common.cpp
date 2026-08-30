#include "common.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSettings>
#include <QSysInfo>

namespace mcl {

bool Account::isAuthenticated() const
{
    return !name.isEmpty() && !uuid.isEmpty() && !accessToken.isEmpty();
}

QJsonObject accountToJson(const Account &account)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), account.id);
    object.insert(QStringLiteral("name"), account.name);
    object.insert(QStringLiteral("uuid"), account.uuid);
    object.insert(QStringLiteral("xuid"), account.xuid);
    object.insert(QStringLiteral("userHash"), account.userHash);
    object.insert(QStringLiteral("userType"), account.userType);
    object.insert(QStringLiteral("expiresAt"), account.expiresAt);
    return object;
}

Account accountFromJson(const QJsonObject &object)
{
    Account account;
    account.id = object.value(QStringLiteral("id")).toString();
    account.name = object.value(QStringLiteral("name")).toString();
    account.uuid = object.value(QStringLiteral("uuid")).toString();
    account.xuid = object.value(QStringLiteral("xuid")).toString();
    account.userHash = object.value(QStringLiteral("userHash")).toString();
    account.userType = object.value(QStringLiteral("userType")).toString(QStringLiteral("msa"));
    account.expiresAt = object.value(QStringLiteral("expiresAt")).toInteger();
    return account;
}

QString fileSha1(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    while (!file.atEnd()) {
        hash.addData(file.read(1024 * 1024));
    }
    return QString::fromLatin1(hash.result().toHex()).toLower();
}

bool writeJsonFile(const QString &path, const QJsonDocument &document, QString *error)
{
    QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) {
            *error = QStringLiteral("Не удалось создать каталог %1").arg(info.absolutePath());
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Не удалось записать %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    file.write(document.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QJsonDocument readJsonFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Не удалось открыть %1: %2").arg(path, file.errorString());
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("Ошибка JSON в %1: %2").arg(path, parseError.errorString());
        }
        return {};
    }
    return document;
}

QStringList splitCommandLine(const QString &value)
{
    QStringList result;
    const QRegularExpression expression(QStringLiteral("(?:\"([^\"]*)\")|([^\\s]+)"));
    QRegularExpressionMatchIterator iterator = expression.globalMatch(value);
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        result.append(match.captured(1).isNull() ? match.captured(2) : match.captured(1));
    }
    return result;
}

QString replaceVariables(QString value, const QHash<QString, QString> &variables)
{
    for (auto iterator = variables.constBegin(); iterator != variables.constEnd(); ++iterator) {
        value.replace(QStringLiteral("${%1}").arg(iterator.key()), iterator.value());
    }
    return value;
}

QString defaultOAuthClientId()
{
    const QString fromEnvironment = qEnvironmentVariable("MCL_OAUTH_CLIENT_ID").trimmed();
    if (!fromEnvironment.isEmpty()) {
        return fromEnvironment;
    }

    QSettings settings;
    const QString configured = settings.value(QStringLiteral("auth/clientId")).toString().trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    // Public client id used by the standard Minecraft/Xbox sign-in flow.
    // It is overridable through MCL_OAUTH_CLIENT_ID or settings.
    return QStringLiteral("00000000402b5328");
}

namespace Rules {

QString currentOsName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("osx");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("unknown");
#endif
}

QString currentOsArch()
{
    const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
    if (architecture == QStringLiteral("x86_64") || architecture == QStringLiteral("amd64")
        || architecture == QStringLiteral("x86-64")) {
        return QStringLiteral("x86_64");
    }
    if (architecture == QStringLiteral("i386") || architecture == QStringLiteral("i686")
        || architecture == QStringLiteral("x86")) {
        return QStringLiteral("x86");
    }
    return architecture;
}

QString nativeArch()
{
    return currentOsArch() == QStringLiteral("x86_64") ? QStringLiteral("64") : QStringLiteral("32");
}

static bool ruleMatches(const QJsonObject &rule, const QHash<QString, bool> &features)
{
    const QJsonObject os = rule.value(QStringLiteral("os")).toObject();
    if (!os.isEmpty()) {
        if (os.contains(QStringLiteral("name"))
            && os.value(QStringLiteral("name")).toString() != currentOsName()) {
            return false;
        }
        if (os.contains(QStringLiteral("arch"))
            && os.value(QStringLiteral("arch")).toString().toLower() != currentOsArch()) {
            return false;
        }
        if (os.contains(QStringLiteral("version"))) {
            const QRegularExpression versionExpression(os.value(QStringLiteral("version")).toString());
            if (!versionExpression.match(QSysInfo::kernelVersion()).hasMatch()) {
                return false;
            }
        }
    }

    const QJsonObject requiredFeatures = rule.value(QStringLiteral("features")).toObject();
    for (auto iterator = requiredFeatures.constBegin(); iterator != requiredFeatures.constEnd(); ++iterator) {
        const bool actual = features.value(iterator.key(), false);
        if (actual != iterator.value().toBool()) {
            return false;
        }
    }
    return true;
}

bool allowed(const QJsonArray &rules, const QHash<QString, bool> &features)
{
    if (rules.isEmpty()) {
        return true;
    }

    // Mojang rules are evaluated in order; the last matching rule wins.
    bool result = false;
    for (const QJsonValue &value : rules) {
        const QJsonObject rule = value.toObject();
        if (rule.isEmpty() || !ruleMatches(rule, features)) {
            continue;
        }
        result = rule.value(QStringLiteral("action")).toString() == QStringLiteral("allow");
    }
    return result;
}

} // namespace Rules

} // namespace mcl
