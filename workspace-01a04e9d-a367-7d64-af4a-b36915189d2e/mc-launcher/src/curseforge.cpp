#include "curseforge.h"

#include "http.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

#include <utility>

namespace mcl {

namespace {

constexpr char kApiBase[] = "https://api.curseforge.com/v1";

QJsonObject parseObject(const QByteArray &body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    return document.isObject() ? document.object() : QJsonObject();
}

QString responseMessage(const HttpResponse &response)
{
    const QJsonObject object = parseObject(response.body);
    const QString message = object.value(QStringLiteral("errorMessage")).toString();
    if (!message.isEmpty()) {
        return message;
    }
    if (!response.error.isEmpty()) {
        return response.error;
    }
    return QStringLiteral("HTTP %1").arg(response.status);
}

} // namespace

QString curseForgeApiKey()
{
    QString key = qEnvironmentVariable("MCL_CURSEFORGE_API_KEY").trimmed();
    if (key.isEmpty()) {
        key = qEnvironmentVariable("CURSEFORGE_API_KEY").trimmed();
    }
    if (key.isEmpty()) {
        QSettings settings;
        key = settings.value(QStringLiteral("curseforge/apiKey")).toString().trimmed();
    }
    return key;
}

CurseForgeSearchWorker::CurseForgeSearchWorker(QString query,
                                               QString gameVersion,
                                               QString loader,
                                               QString apiKey)
    : query_(std::move(query)), gameVersion_(std::move(gameVersion)), loader_(std::move(loader)),
      apiKey_(std::move(apiKey))
{
}

void CurseForgeSearchWorker::run()
{
    if (apiKey_.isEmpty()) {
        emit failed(QStringLiteral(
            "Для CurseForge нужен API key. Укажите MCL_CURSEFORGE_API_KEY или CURSEFORGE_API_KEY."));
        return;
    }

    QUrl url(QString::fromLatin1(kApiBase) + QStringLiteral("/mods/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("gameId"), QStringLiteral("432"));
    query.addQueryItem(QStringLiteral("classId"), QStringLiteral("6"));
    query.addQueryItem(QStringLiteral("searchFilter"), query_);
    query.addQueryItem(QStringLiteral("gameVersion"), gameVersion_);
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("30"));
    query.addQueryItem(QStringLiteral("sortField"), QStringLiteral("2"));
    query.addQueryItem(QStringLiteral("sortOrder"), QStringLiteral("desc"));
    const int type = loader_ == QStringLiteral("fabric") ? 4
                     : loader_ == QStringLiteral("forge") ? 1
                     : loader_ == QStringLiteral("neoforge") ? 6
                     : loader_ == QStringLiteral("quilt") ? 5 : 0;
    if (type != 0) {
        query.addQueryItem(QStringLiteral("modLoaderType"), QString::number(type));
    }
    url.setQuery(query);

    const HttpResponse response = Http::get(
        url.toString(),
        {{QStringLiteral("Accept"), QStringLiteral("application/json")},
         {QStringLiteral("x-api-key"), apiKey_}});
    if (!response.ok()) {
        emit failed(QStringLiteral("CurseForge недоступен: %1").arg(responseMessage(response)));
        return;
    }
    const QJsonObject object = parseObject(response.body);
    if (!object.contains(QStringLiteral("data"))) {
        emit failed(QStringLiteral("CurseForge вернул некорректный ответ."));
        return;
    }
    emit loaded(response.body);
}

CurseForgeInstallWorker::CurseForgeInstallWorker(QString modId,
                                                 QString gameVersion,
                                                 QString loader,
                                                 QString gameDir,
                                                 QString apiKey)
    : modId_(std::move(modId)), gameVersion_(std::move(gameVersion)), loader_(std::move(loader)),
      gameDir_(std::move(gameDir)), apiKey_(std::move(apiKey))
{
}

int CurseForgeInstallWorker::loaderType() const
{
    if (loader_ == QStringLiteral("fabric")) {
        return 4;
    }
    if (loader_ == QStringLiteral("forge")) {
        return 1;
    }
    if (loader_ == QStringLiteral("neoforge")) {
        return 6;
    }
    if (loader_ == QStringLiteral("quilt")) {
        return 5;
    }
    return 0;
}

QString CurseForgeInstallWorker::apiUrl(const QString &path) const
{
    return QString::fromLatin1(kApiBase) + path;
}

void CurseForgeInstallWorker::run()
{
    if (apiKey_.isEmpty()) {
        emit failed(QStringLiteral(
            "Для CurseForge нужен API key. Укажите MCL_CURSEFORGE_API_KEY или CURSEFORGE_API_KEY."));
        return;
    }
    if (modId_.isEmpty() || gameVersion_.isEmpty()) {
        emit failed(QStringLiteral("Не выбран проект CurseForge или версия Minecraft."));
        return;
    }

    QUrl url(apiUrl(QStringLiteral("/mods/%1/files").arg(modId_)));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("gameVersion"), gameVersion_);
    if (loaderType() != 0) {
        query.addQueryItem(QStringLiteral("modLoaderType"), QString::number(loaderType()));
    }
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("50"));
    query.addQueryItem(QStringLiteral("sortField"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("sortOrder"), QStringLiteral("desc"));
    url.setQuery(query);

    const QMap<QString, QString> headers {
        {QStringLiteral("Accept"), QStringLiteral("application/json")},
        {QStringLiteral("x-api-key"), apiKey_}
    };
    const HttpResponse filesResponse = Http::get(url.toString(), headers);
    if (!filesResponse.ok()) {
        emit failed(QStringLiteral("Не удалось получить файлы CurseForge: %1")
                        .arg(responseMessage(filesResponse)));
        return;
    }
    const QJsonArray files = parseObject(filesResponse.body).value(QStringLiteral("data")).toArray();
    QJsonObject selected;
    for (const QJsonValue &value : files) {
        const QJsonObject file = value.toObject();
        if (file.value(QStringLiteral("isAvailable")).toBool(true)
            && file.value(QStringLiteral("fileName")).toString().endsWith(QStringLiteral(".jar"),
                                                                            Qt::CaseInsensitive)) {
            selected = file;
            break;
        }
    }
    if (selected.isEmpty()) {
        emit failed(QStringLiteral("Для проекта %1 нет доступного JAR под %2.")
                        .arg(modId_, gameVersion_));
        return;
    }

    QString downloadUrl = selected.value(QStringLiteral("downloadUrl")).toString();
    if (downloadUrl.isEmpty()) {
        const QString fileId = QString::number(selected.value(QStringLiteral("id")).toInteger());
        const HttpResponse urlResponse = Http::get(apiUrl(
            QStringLiteral("/mods/%1/files/%2/download-url").arg(modId_, fileId)), headers);
        if (urlResponse.ok()) {
            downloadUrl = parseObject(urlResponse.body).value(QStringLiteral("data")).toString();
        }
    }
    if (downloadUrl.isEmpty()) {
        emit failed(QStringLiteral("CurseForge не предоставил ссылку на скачивание файла."));
        return;
    }

    QString sha1;
    for (const QJsonValue &hashValue : selected.value(QStringLiteral("hashes")).toArray()) {
        const QJsonObject hash = hashValue.toObject();
        const QString algorithm = hash.value(QStringLiteral("algo")).toVariant().toString().toLower();
        if (hash.value(QStringLiteral("algo")).toInt() == 1 || algorithm.contains(QStringLiteral("sha1"))) {
            sha1 = hash.value(QStringLiteral("value")).toString();
            break;
        }
    }

    QString fileName = QFileInfo(selected.value(QStringLiteral("fileName")).toString()).fileName();
    if (fileName.isEmpty()) {
        fileName = QStringLiteral("curseforge-%1.jar").arg(modId_);
    }
    const QString modsDirectory = QDir(gameDir_).filePath(QStringLiteral("mods"));
    if (!QDir().mkpath(modsDirectory)) {
        emit failed(QStringLiteral("Не удалось создать папку mods."));
        return;
    }
    const QString destination = QDir(modsDirectory).filePath(fileName);
    QString error;
    emit logMessage(QStringLiteral("CurseForge: загрузка %1").arg(fileName));
    if (!Http::download(downloadUrl, destination, sha1,
                        [this, fileName](qint64 current, qint64 total) {
                            emit progress(current, total, QStringLiteral("CurseForge: %1").arg(fileName));
                        }, &error)) {
        emit failed(error);
        return;
    }
    emit finished(modId_);
}

} // namespace mcl
