#include "http.h"

#include "common.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace mcl {

namespace {

constexpr int kTimeoutMs = 60 * 1000;
constexpr char kUserAgent[] = "MCLauncher/0.1 (Qt; official Minecraft Java launcher client)";

void applyHeaders(QNetworkRequest &request, const QMap<QString, QString> &headers)
{
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    for (auto iterator = headers.constBegin(); iterator != headers.constEnd(); ++iterator) {
        request.setRawHeader(iterator.key().toUtf8(), iterator.value().toUtf8());
    }
}

} // namespace

HttpResponse Http::request(const QString &method,
                           const QString &url,
                           const QByteArray &body,
                           const QMap<QString, QString> &headers)
{
    HttpResponse response;
    QNetworkAccessManager manager;
    QNetworkRequest request { QUrl(url) };
    applyHeaders(request, headers);

    QNetworkReply *reply = nullptr;
    if (method == QStringLiteral("POST")) {
        reply = manager.post(request, body);
    } else if (method == QStringLiteral("DELETE")) {
        reply = manager.deleteResource(request);
    } else {
        reply = manager.get(request);
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout.start(kTimeoutMs);
    loop.exec();

    response.body = reply->readAll();
    response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        response.error = reply->errorString();
    }
    reply->deleteLater();
    return response;
}

HttpResponse Http::get(const QString &url, const QMap<QString, QString> &headers)
{
    return request(QStringLiteral("GET"), url, {}, headers);
}

HttpResponse Http::postForm(const QString &url,
                            const QMap<QString, QString> &fields,
                            const QMap<QString, QString> &headers)
{
    QUrlQuery query;
    for (auto iterator = fields.constBegin(); iterator != fields.constEnd(); ++iterator) {
        query.addQueryItem(iterator.key(), iterator.value());
    }

    QMap<QString, QString> merged = headers;
    merged.insert(QStringLiteral("Content-Type"),
                  QStringLiteral("application/x-www-form-urlencoded"));
    return request(QStringLiteral("POST"), url, query.toString(QUrl::FullyEncoded).toUtf8(), merged);
}

HttpResponse Http::postJson(const QString &url,
                            const QByteArray &json,
                            const QMap<QString, QString> &headers)
{
    QMap<QString, QString> merged = headers;
    merged.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
    merged.insert(QStringLiteral("Accept"), QStringLiteral("application/json"));
    return request(QStringLiteral("POST"), url, json, merged);
}

HttpResponse Http::uploadMultipart(const QString &url,
                                   const QMap<QString, QString> &fields,
                                   const QString &fileField,
                                   const QString &filePath,
                                   const QMap<QString, QString> &headers)
{
    HttpResponse response;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        response.error = QStringLiteral("Не удалось открыть файл %1: %2")
                             .arg(filePath, file.errorString());
        return response;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request { QUrl(url) };
    applyHeaders(request, headers);
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for (auto iterator = fields.constBegin(); iterator != fields.constEnd(); ++iterator) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(iterator.key()));
        part.setBody(iterator.value().toUtf8());
        multipart->append(part);
    }

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"")
                           .arg(fileField, QFileInfo(filePath).fileName()));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
    filePart.setBody(file.readAll());
    multipart->append(filePart);

    QNetworkReply *reply = manager.post(request, multipart);
    multipart->setParent(reply);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout.start(kTimeoutMs);
    loop.exec();

    response.body = reply->readAll();
    response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        response.error = reply->errorString();
    }
    reply->deleteLater();
    return response;
}

HttpResponse Http::deleteRequest(const QString &url,
                                 const QMap<QString, QString> &headers)
{
    return request(QStringLiteral("DELETE"), url, {}, headers);
}

bool Http::download(const QString &url,
                    const QString &destination,
                    const QString &expectedSha1,
                    const ProgressCallback &progress,
                    QString *error)
{
    const QString expected = expectedSha1.trimmed().toLower();
    if (QFileInfo::exists(destination)
        && ((!expected.isEmpty() && fileSha1(destination) == expected)
            || (expected.isEmpty() && QFileInfo(destination).size() > 0))) {
        if (progress) {
            progress(QFileInfo(destination).size(), QFileInfo(destination).size());
        }
        return true;
    }

    const QFileInfo destinationInfo(destination);
    if (!QDir().mkpath(destinationInfo.absolutePath())) {
        if (error) {
            *error = QStringLiteral("Не удалось создать каталог %1").arg(destinationInfo.absolutePath());
        }
        return false;
    }

    const QString partPath = destination + QStringLiteral(".part");
    qint64 offset = QFileInfo::exists(partPath) ? QFileInfo(partPath).size() : 0;

    QNetworkAccessManager manager;
    QNetworkRequest request { QUrl(url) };
    applyHeaders(request, {});
    if (offset > 0) {
        request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(offset) + "-");
    }

    QNetworkReply *reply = manager.get(request);
    QFile partFile(partPath);
    bool append = offset > 0;
    if (!partFile.open(append ? QIODevice::Append : QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("Не удалось открыть временный файл %1: %2")
                         .arg(partPath, partFile.errorString());
        }
        reply->abort();
        reply->deleteLater();
        return false;
    }

    QObject::connect(reply, &QNetworkReply::metaDataChanged, [&]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (append && status != 206) {
            partFile.close();
            partFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
            append = false;
            offset = 0;
        }
    });

    QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
        partFile.write(reply->readAll());
    });

    QObject::connect(reply, &QNetworkReply::downloadProgress,
                     [&](qint64 current, qint64 total) {
                         if (progress) {
                             progress(offset + current, total > 0 ? offset + total : total);
                         }
                     });

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout.start(kTimeoutMs);
    loop.exec();

    partFile.write(reply->readAll());
    partFile.flush();
    partFile.close();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString networkError = reply->error() == QNetworkReply::NoError
                                     ? QString()
                                     : reply->errorString();
    reply->deleteLater();

    if (!networkError.isEmpty() || (status < 200 || status >= 300)) {
        if (error) {
            *error = QStringLiteral("Загрузка %1 не удалась (HTTP %2): %3")
                         .arg(url).arg(status).arg(networkError);
        }
        return false;
    }

    if (!expected.isEmpty() && fileSha1(partPath) != expected) {
        if (error) {
            *error = QStringLiteral("Проверка SHA-1 не пройдена для %1").arg(destination);
        }
        return false;
    }

    QFile::remove(destination);
    if (!QFile::rename(partPath, destination)) {
        if (error) {
            *error = QStringLiteral("Не удалось переместить %1 в %2").arg(partPath, destination);
        }
        return false;
    }
    return true;
}

} // namespace mcl
