#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

#include <functional>

namespace mcl {

struct HttpResponse {
    QByteArray body;
    int status = 0;
    QString error;

    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
};

class Http final {
public:
    using ProgressCallback = std::function<void(qint64 current, qint64 total)>;

    static HttpResponse get(const QString &url,
                            const QMap<QString, QString> &headers = {});
    static HttpResponse postForm(const QString &url,
                                 const QMap<QString, QString> &fields,
                                 const QMap<QString, QString> &headers = {});
    static HttpResponse postJson(const QString &url,
                                 const QByteArray &json,
                                 const QMap<QString, QString> &headers = {});
    static HttpResponse uploadMultipart(const QString &url,
                                        const QMap<QString, QString> &fields,
                                        const QString &fileField,
                                        const QString &filePath,
                                        const QMap<QString, QString> &headers = {});
    static HttpResponse deleteRequest(const QString &url,
                                     const QMap<QString, QString> &headers = {});

    static bool download(const QString &url,
                         const QString &destination,
                         const QString &expectedSha1,
                         const ProgressCallback &progress,
                         QString *error = nullptr);

private:
    static HttpResponse request(const QString &method,
                                const QString &url,
                                const QByteArray &body,
                                const QMap<QString, QString> &headers);
};

} // namespace mcl
