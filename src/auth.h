#pragma once

#include "common.h"

#include <QObject>
#include <QTimer>

namespace mcl {

class Authenticator final : public QObject {
    Q_OBJECT
public:
    explicit Authenticator(QObject *parent = nullptr);

    void login();
    void logout();
    bool isBusy() const { return busy_; }
    bool hasSavedSession() const;
    const Account &account() const { return account_; }

signals:
    void statusChanged(const QString &message);
    void deviceCodeReady(const QString &message,
                         const QString &userCode,
                         const QString &verificationUrl);
    void authenticated(const mcl::Account &account);
    void loggedOut();
    void failed(const QString &message);

private slots:
    void requestDeviceCode();
    void pollDeviceCode();

private:
    void requestRefresh(const QString &refreshToken);
    void exchangeMicrosoftToken(const QString &accessToken,
                                const QString &refreshToken);
    void finishError(const QString &message);
    void saveSession(const Account &account, const QString &refreshToken);
    QString loadRefreshToken() const;
    void clearSavedSession();

    QString clientId_;
    Account account_;
    QTimer pollTimer_;
    QString deviceCode_;
    int pollIntervalSeconds_ = 5;
    qint64 deviceExpiresAt_ = 0;
    bool busy_ = false;
};

} // namespace mcl
