#include "skin.h"

#include "http.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace mcl {

namespace {

constexpr char kSkinUrl[] = "https://api.minecraftservices.com/minecraft/profile/skins";
constexpr char kResetSkinUrl[] = "https://api.minecraftservices.com/minecraft/profile/skins/active";

QString responseMessage(const HttpResponse &response)
{
    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    if (document.isObject()) {
        const QJsonObject object = document.object();
        const QString message = object.value(QStringLiteral("errorMessage")).toString();
        if (!message.isEmpty()) {
            return message;
        }
        const QString detail = object.value(QStringLiteral("message")).toString();
        if (!detail.isEmpty()) {
            return detail;
        }
    }
    return response.error.isEmpty() ? QStringLiteral("HTTP %1").arg(response.status) : response.error;
}

} // namespace

SkinWorker::SkinWorker(SkinOperation operation,
                       Account account,
                       QString filePath,
                       QString variant)
    : operation_(operation), account_(std::move(account)), filePath_(std::move(filePath)),
      variant_(std::move(variant))
{
}

void SkinWorker::run()
{
    if (!account_.isAuthenticated()) {
        emit failed(QStringLiteral("Сначала войдите в Minecraft через Microsoft."));
        return;
    }

    const QMap<QString, QString> headers {
        {QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(account_.accessToken)}
    };

    if (operation_ == SkinOperation::Reset) {
        const HttpResponse response = Http::deleteRequest(QString::fromLatin1(kResetSkinUrl), headers);
        if (!response.ok()) {
            emit failed(QStringLiteral("Не удалось сбросить скин: %1").arg(responseMessage(response)));
            return;
        }
        emit finished(QStringLiteral("Скин сброшен до стандартного."));
        return;
    }

    const QFileInfo file(filePath_);
    if (!file.exists() || !file.isFile()) {
        emit failed(QStringLiteral("Файл скина не найден."));
        return;
    }
    if (file.suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
        emit failed(QStringLiteral("Скин должен быть PNG-файлом."));
        return;
    }
    if (file.size() > 4 * 1024 * 1024) {
        emit failed(QStringLiteral("PNG-файл скина слишком большой."));
        return;
    }
    const QString variant = variant_ == QStringLiteral("slim") ? QStringLiteral("slim")
                                                                  : QStringLiteral("classic");
    const HttpResponse response = Http::uploadMultipart(
        QString::fromLatin1(kSkinUrl),
        {{QStringLiteral("variant"), variant}},
        QStringLiteral("file"), file.absoluteFilePath(), headers);
    if (!response.ok()) {
        emit failed(QStringLiteral("Не удалось загрузить скин: %1").arg(responseMessage(response)));
        return;
    }
    emit finished(QStringLiteral("Скин загружен на аккаунт Minecraft."));
}

} // namespace mcl
