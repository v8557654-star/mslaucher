#include "auth.h"

#include "http.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#ifdef Q_OS_WIN
#    define SECURITY_WIN32
#    include <windows.h>
#    include <wincrypt.h>
#endif

namespace mcl {

namespace {

constexpr char kDeviceCodeUrl[] =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
constexpr char kTokenUrl[] =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
constexpr char kXboxLiveUrl[] =
    "https://user.auth.xboxlive.com/user/authenticate";
constexpr char kXstsUrl[] =
    "https://xsts.auth.xboxlive.com/xsts/authorize";
constexpr char kMinecraftLoginUrl[] =
    "https://api.minecraftservices.com/authentication/login_with_xbox";
constexpr char kMinecraftProfileUrl[] =
    "https://api.minecraftservices.com/minecraft/profile";

QJsonObject parseObject(const QByteArray &body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    return document.isObject() ? document.object() : QJsonObject();
}

QString responseError(const HttpResponse &response, const QString &fallback)
{
    const QJsonObject object = parseObject(response.body);
    const QString description = object.value(QStringLiteral("error_description")).toString();
    if (!description.isEmpty()) {
        return description;
    }
    const QString message = object.value(QStringLiteral("Message")).toString();
    if (!message.isEmpty()) {
        return message;
    }
    if (!response.error.isEmpty()) {
        return response.error;
    }
    return fallback;
}

#ifdef Q_OS_WIN
QByteArray protectToken(const QByteArray &value)
{
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(value.constData()));
    input.cbData = static_cast<DWORD>(value.size());

    DATA_BLOB output;
    if (!CryptProtectData(&input, L"MCLauncher refresh token", nullptr, nullptr, nullptr,
                          0, &output)) {
        return value.toBase64();
    }

    const QByteArray result = QByteArray(reinterpret_cast<const char *>(output.pbData),
                                         static_cast<int>(output.cbData)).toBase64();
    LocalFree(output.pbData);
    return result;
}

QByteArray unprotectToken(const QByteArray &value)
{
    const QByteArray encrypted = QByteArray::fromBase64(value);
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData()));
    input.cbData = static_cast<DWORD>(encrypted.size());

    DATA_BLOB output;
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output)) {
        // This also supports tokens written by the non-Windows fallback.
        return encrypted;
    }

    const QByteArray result(reinterpret_cast<const char *>(output.pbData),
                            static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return result;
}
#else
QByteArray protectToken(const QByteArray &value)
{
    return value.toBase64();
}

QByteArray unprotectToken(const QByteArray &value)
{
    return QByteArray::fromBase64(value);
}
#endif

} // namespace

Authenticator::Authenticator(QObject *parent)
    : QObject(parent), clientId_(defaultOAuthClientId())
{
    pollTimer_.setSingleShot(true);
    connect(&pollTimer_, &QTimer::timeout, this, &Authenticator::pollDeviceCode);
}

bool Authenticator::hasSavedSession() const
{
    return !loadRefreshToken().isEmpty();
}

void Authenticator::login()
{
    if (busy_) {
        return;
    }

    busy_ = true;
    const QString refreshToken = loadRefreshToken();
    if (!refreshToken.isEmpty()) {
        emit statusChanged(QStringLiteral("Восстановление сессии Microsoft…"));
        QTimer::singleShot(0, this, [this, refreshToken]() { requestRefresh(refreshToken); });
    } else {
        QTimer::singleShot(0, this, &Authenticator::requestDeviceCode);
    }
}

void Authenticator::logout()
{
    pollTimer_.stop();
    busy_ = false;
    account_ = {};
    clearSavedSession();
    emit loggedOut();
}

void Authenticator::requestDeviceCode()
{
    emit statusChanged(QStringLiteral("Получение кода входа Microsoft…"));
    const HttpResponse response = Http::postForm(
        QString::fromLatin1(kDeviceCodeUrl),
        {{QStringLiteral("client_id"), clientId_},
         {QStringLiteral("scope"), QStringLiteral("XboxLive.signin offline_access")}});

    const QJsonObject object = parseObject(response.body);
    if (!response.ok() || object.isEmpty() || object.value(QStringLiteral("device_code")).toString().isEmpty()) {
        finishError(QStringLiteral("Не удалось получить код Microsoft: %1")
                        .arg(responseError(response, QStringLiteral("неизвестная ошибка"))));
        return;
    }

    deviceCode_ = object.value(QStringLiteral("device_code")).toString();
    const QString userCode = object.value(QStringLiteral("user_code")).toString();
    const QString verificationUrl = object.value(QStringLiteral("verification_uri")).toString(
        object.value(QStringLiteral("verification_url")).toString());
    const QString message = object.value(QStringLiteral("message")).toString(
        QStringLiteral("Откройте страницу Microsoft и введите код."));
    pollIntervalSeconds_ = qMax(5, object.value(QStringLiteral("interval")).toInt(5));
    deviceExpiresAt_ = QDateTime::currentSecsSinceEpoch()
                       + object.value(QStringLiteral("expires_in")).toInt(900);

    emit deviceCodeReady(message, userCode, verificationUrl);
    pollTimer_.start(pollIntervalSeconds_ * 1000);
}

void Authenticator::pollDeviceCode()
{
    if (deviceCode_.isEmpty()) {
        finishError(QStringLiteral("Сессия входа Microsoft не найдена."));
        return;
    }
    if (QDateTime::currentSecsSinceEpoch() >= deviceExpiresAt_) {
        finishError(QStringLiteral("Время действия кода Microsoft истекло."));
        return;
    }

    emit statusChanged(QStringLiteral("Ожидание подтверждения входа Microsoft…"));
    const HttpResponse response = Http::postForm(
        QString::fromLatin1(kTokenUrl),
        {{QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code")},
         {QStringLiteral("client_id"), clientId_},
         {QStringLiteral("device_code"), deviceCode_}});
    const QJsonObject object = parseObject(response.body);

    const QString accessToken = object.value(QStringLiteral("access_token")).toString();
    if (!accessToken.isEmpty()) {
        exchangeMicrosoftToken(accessToken, object.value(QStringLiteral("refresh_token")).toString());
        return;
    }

    const QString errorCode = object.value(QStringLiteral("error")).toString();
    if (errorCode == QStringLiteral("authorization_pending")) {
        pollTimer_.start(pollIntervalSeconds_ * 1000);
        return;
    }
    if (errorCode == QStringLiteral("slow_down")) {
        pollIntervalSeconds_ += 5;
        pollTimer_.start(pollIntervalSeconds_ * 1000);
        return;
    }
    if (errorCode == QStringLiteral("expired_token")) {
        finishError(QStringLiteral("Время действия кода Microsoft истекло."));
        return;
    }
    if (errorCode == QStringLiteral("access_denied")) {
        finishError(QStringLiteral("Вход отменён пользователем."));
        return;
    }

    finishError(QStringLiteral("Ошибка входа Microsoft: %1")
                    .arg(responseError(response, QStringLiteral("неизвестная ошибка"))));
}

void Authenticator::requestRefresh(const QString &refreshToken)
{
    const HttpResponse response = Http::postForm(
        QString::fromLatin1(kTokenUrl),
        {{QStringLiteral("client_id"), clientId_},
         {QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
         {QStringLiteral("refresh_token"), refreshToken},
         {QStringLiteral("scope"), QStringLiteral("XboxLive.signin offline_access")}});
    const QJsonObject object = parseObject(response.body);
    const QString accessToken = object.value(QStringLiteral("access_token")).toString();
    if (accessToken.isEmpty()) {
        clearSavedSession();
        requestDeviceCode();
        return;
    }

    const QString newRefreshToken = object.value(QStringLiteral("refresh_token")).toString(refreshToken);
    exchangeMicrosoftToken(accessToken, newRefreshToken);
}

void Authenticator::exchangeMicrosoftToken(const QString &accessToken,
                                            const QString &refreshToken)
{
    emit statusChanged(QStringLiteral("Проверка Xbox Live…"));
    QJsonObject xblProperties;
    xblProperties.insert(QStringLiteral("AuthMethod"), QStringLiteral("RPS"));
    xblProperties.insert(QStringLiteral("SiteName"), QStringLiteral("user.auth.xboxlive.com"));
    xblProperties.insert(QStringLiteral("RpsTicket"), QStringLiteral("d=%1").arg(accessToken));

    QJsonObject xblRequest;
    xblRequest.insert(QStringLiteral("Properties"), xblProperties);
    xblRequest.insert(QStringLiteral("RelyingParty"), QStringLiteral("http://auth.xboxlive.com"));
    xblRequest.insert(QStringLiteral("TokenType"), QStringLiteral("JWT"));
    const HttpResponse xblResponse = Http::postJson(
        QString::fromLatin1(kXboxLiveUrl), QJsonDocument(xblRequest).toJson(QJsonDocument::Compact));
    const QJsonObject xbl = parseObject(xblResponse.body);
    const QString xblToken = xbl.value(QStringLiteral("Token")).toString();
    const QJsonArray xblClaims = xbl.value(QStringLiteral("DisplayClaims")).toObject()
                                     .value(QStringLiteral("xui")).toArray();
    const QJsonObject xui = xblClaims.isEmpty() ? QJsonObject() : xblClaims.first().toObject();
    const QString userHash = xui.value(QStringLiteral("uhs")).toString();
    if (!xblResponse.ok() || xblToken.isEmpty() || userHash.isEmpty()) {
        finishError(QStringLiteral("Xbox Live не принял учётную запись: %1")
                        .arg(responseError(xblResponse, QStringLiteral("проверьте аккаунт Microsoft"))));
        return;
    }

    emit statusChanged(QStringLiteral("Получение токена Minecraft…"));
    QJsonObject xstsProperties;
    xstsProperties.insert(QStringLiteral("SandboxId"), QStringLiteral("RETAIL"));
    QJsonArray userTokens;
    userTokens.append(xblToken);
    xstsProperties.insert(QStringLiteral("UserTokens"), userTokens);
    QJsonObject xstsRequest;
    xstsRequest.insert(QStringLiteral("Properties"), xstsProperties);
    xstsRequest.insert(QStringLiteral("RelyingParty"), QStringLiteral("rp://api.minecraftservices.com/"));
    xstsRequest.insert(QStringLiteral("TokenType"), QStringLiteral("JWT"));
    const HttpResponse xstsResponse = Http::postJson(
        QString::fromLatin1(kXstsUrl), QJsonDocument(xstsRequest).toJson(QJsonDocument::Compact));
    const QJsonObject xsts = parseObject(xstsResponse.body);
    const QString xstsToken = xsts.value(QStringLiteral("Token")).toString();
    if (!xstsResponse.ok() || xstsToken.isEmpty()) {
        const QJsonObject errorObject = parseObject(xstsResponse.body);
        const QString code = errorObject.value(QStringLiteral("XErr")).toVariant().toString();
        finishError(QStringLiteral("XSTS не выдал токен (%1): %2")
                        .arg(code.isEmpty() ? QStringLiteral("ошибка") : code,
                             responseError(xstsResponse, QStringLiteral("аккаунт не прошёл проверку Xbox Live"))));
        return;
    }

    emit statusChanged(QStringLiteral("Проверка лицензии Minecraft Java…"));
    const QString identityToken = QStringLiteral("XBL3.0 x=%1;%2").arg(userHash, xstsToken);
    QJsonObject minecraftRequest;
    minecraftRequest.insert(QStringLiteral("identityToken"), identityToken);
    const HttpResponse minecraftResponse = Http::postJson(
        QString::fromLatin1(kMinecraftLoginUrl),
        QJsonDocument(minecraftRequest).toJson(QJsonDocument::Compact));
    const QJsonObject minecraftTokenObject = parseObject(minecraftResponse.body);
    const QString minecraftToken = minecraftTokenObject.value(QStringLiteral("access_token")).toString();
    if (!minecraftResponse.ok() || minecraftToken.isEmpty()) {
        finishError(QStringLiteral("Minecraft Services не выдал токен: %1")
                        .arg(responseError(minecraftResponse,
                                            QStringLiteral("у аккаунта нет доступа к Minecraft Java Edition"))));
        return;
    }

    const HttpResponse profileResponse = Http::get(
        QString::fromLatin1(kMinecraftProfileUrl),
        {{QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(minecraftToken)}});
    const QJsonObject profile = parseObject(profileResponse.body);
    const QString profileId = profile.value(QStringLiteral("id")).toString();
    const QString profileName = profile.value(QStringLiteral("name")).toString();
    if (!profileResponse.ok() || profileId.isEmpty() || profileName.isEmpty()) {
        finishError(QStringLiteral("Профиль Minecraft не найден: аккаунт должен владеть Java Edition."));
        return;
    }

    Account account;
    account.id = profileId;
    account.name = profileName;
    account.uuid = profileId;
    account.xuid = xui.value(QStringLiteral("xid")).toString();
    account.userHash = userHash;
    account.accessToken = minecraftToken;
    account.userType = QStringLiteral("msa");
    account.expiresAt = QDateTime::currentSecsSinceEpoch()
                        + minecraftTokenObject.value(QStringLiteral("expires_in")).toInteger(86400);
    account_ = account;
    saveSession(account, refreshToken);
    busy_ = false;
    emit statusChanged(QStringLiteral("Вход выполнен: %1").arg(account.name));
    emit authenticated(account_);
}

void Authenticator::finishError(const QString &message)
{
    pollTimer_.stop();
    busy_ = false;
    emit statusChanged(message);
    emit failed(message);
}

void Authenticator::saveSession(const Account &account, const QString &refreshToken)
{
    QSettings settings;
    settings.setValue(QStringLiteral("auth/account"), accountToJson(account).toVariantMap());
    settings.setValue(QStringLiteral("auth/refreshToken"),
                      QString::fromLatin1(protectToken(refreshToken.toUtf8())));
    settings.sync();
}

QString Authenticator::loadRefreshToken() const
{
    QSettings settings;
    const QString encoded = settings.value(QStringLiteral("auth/refreshToken")).toString();
    if (encoded.isEmpty()) {
        return {};
    }
    return QString::fromUtf8(unprotectToken(encoded.toLatin1()));
}

void Authenticator::clearSavedSession()
{
    QSettings settings;
    settings.remove(QStringLiteral("auth/account"));
    settings.remove(QStringLiteral("auth/refreshToken"));
    settings.sync();
}

} // namespace mcl
