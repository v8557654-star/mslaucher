#include "modrinth.h"

#include "app_paths.h"
#include "common.h"
#include "http.h"
#include "zip_extract.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

#include <utility>

namespace mcl {

namespace {

constexpr char kApiBase[] = "https://api.modrinth.com/v2";

QJsonObject parseObject(const QByteArray &body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    return document.isObject() ? document.object() : QJsonObject();
}

QString jsonArrayQuery(const QString &value)
{
    QJsonArray array;
    array.append(value);
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString responseMessage(const HttpResponse &response)
{
    if (!response.error.isEmpty()) {
        return response.error;
    }
    const QJsonObject object = parseObject(response.body);
    const QString description = object.value(QStringLiteral("description")).toString();
    return description.isEmpty() ? QStringLiteral("HTTP %1").arg(response.status) : description;
}

bool safeRelativePath(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    return !clean.isEmpty() && !clean.startsWith(QStringLiteral("../"))
           && !clean.contains(QStringLiteral("/../")) && !QDir::isAbsolutePath(clean);
}

bool copyTree(const QString &source, const QString &destination, QString *error)
{
    const QDir sourceDirectory(source);
    if (!sourceDirectory.exists()) {
        return true;
    }
    if (!QDir().mkpath(destination)) {
        if (error) {
            *error = QStringLiteral("Не удалось создать каталог %1").arg(destination);
        }
        return false;
    }
    const QFileInfoList entries = sourceDirectory.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString target = QDir(destination).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyTree(entry.absoluteFilePath(), target, error)) {
                return false;
            }
        } else {
            QFile::remove(target);
            if (!QFile::copy(entry.absoluteFilePath(), target)) {
                if (error) {
                    *error = QStringLiteral("Не удалось скопировать %1").arg(entry.absoluteFilePath());
                }
                return false;
            }
        }
    }
    return true;
}

} // namespace

ModrinthSearchWorker::ModrinthSearchWorker(QString query,
                                           QString gameVersion,
                                           QString loader,
                                           QString projectType)
    : query_(std::move(query)), gameVersion_(std::move(gameVersion)), loader_(std::move(loader)),
      projectType_(std::move(projectType))
{
}

void ModrinthSearchWorker::run()
{
    QUrl url(QString::fromLatin1(kApiBase) + QStringLiteral("/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("query"), query_);
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("30"));
    query.addQueryItem(QStringLiteral("index"), QStringLiteral("relevance"));

    QJsonArray facets;
    QJsonArray projectFacet;
    projectFacet.append(QStringLiteral("project_type:%1").arg(
        projectType_.isEmpty() ? QStringLiteral("mod") : projectType_));
    facets.append(projectFacet);
    if (!gameVersion_.isEmpty()) {
        QJsonArray versionFacet;
        versionFacet.append(QStringLiteral("versions:%1").arg(gameVersion_));
        facets.append(versionFacet);
    }
    if (!loader_.isEmpty() && loader_ != QStringLiteral("vanilla")) {
        QJsonArray loaderFacet;
        loaderFacet.append(QStringLiteral("categories:%1").arg(loader_));
        facets.append(loaderFacet);
    }
    query.addQueryItem(QStringLiteral("facets"),
                       QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));
    url.setQuery(query);

    const HttpResponse response = Http::get(url.toString(),
                                             {{QStringLiteral("User-Agent"),
                                               QStringLiteral("MCLauncher/0.1 (Modrinth client)")}});
    if (!response.ok()) {
        emit failed(QStringLiteral("Modrinth недоступен: %1").arg(responseMessage(response)));
        return;
    }
    if (!parseObject(response.body).contains(QStringLiteral("hits"))) {
        emit failed(QStringLiteral("Modrinth вернул некорректный ответ."));
        return;
    }
    emit loaded(response.body);
}

ModrinthInstallWorker::ModrinthInstallWorker(QString projectId,
                                             QString gameVersion,
                                             QString loader,
                                             QString projectType,
                                             QString gameDir)
    : projectId_(std::move(projectId)), gameVersion_(std::move(gameVersion)),
      loader_(std::move(loader)), projectType_(std::move(projectType)), gameDir_(std::move(gameDir))
{
}

bool ModrinthInstallWorker::downloadFile(const QString &url,
                                         const QString &destination,
                                         const QString &sha1,
                                         const QString &label,
                                         QString *error)
{
    emit logMessage(QStringLiteral("Modrinth: %1").arg(label));
    return Http::download(url, destination, sha1,
                          [this, label](qint64 current, qint64 total) {
                              emit progress(current, total, label);
                          }, error);
}

bool ModrinthInstallWorker::installProject(const QString &projectId,
                                           QSet<QString> *installed,
                                           QString *error)
{
    if (installed->contains(projectId)) {
        return true;
    }
    if (installed->size() >= 100) {
        *error = QStringLiteral("Слишком много зависимостей Modrinth; установка остановлена.");
        return false;
    }
    installed->insert(projectId);

    QUrl url(QString::fromLatin1(kApiBase) + QStringLiteral("/project/")
             + QString::fromUtf8(QUrl::toPercentEncoding(projectId)) + QStringLiteral("/version"));
    QUrlQuery query;
    if (!gameVersion_.isEmpty()) {
        query.addQueryItem(QStringLiteral("game_versions"), jsonArrayQuery(gameVersion_));
    }
    if (!loader_.isEmpty() && loader_ != QStringLiteral("vanilla")) {
        query.addQueryItem(QStringLiteral("loaders"), jsonArrayQuery(loader_));
    }
    url.setQuery(query);

    const HttpResponse response = Http::get(url.toString(),
                                             {{QStringLiteral("User-Agent"),
                                               QStringLiteral("MCLauncher/0.1 (Modrinth client)")}});
    if (!response.ok()) {
        *error = QStringLiteral("Не удалось получить версию Modrinth %1: %2")
                     .arg(projectId, responseMessage(response));
        return false;
    }

    const QJsonDocument versionsDocument = QJsonDocument::fromJson(response.body);
    if (!versionsDocument.isArray() || versionsDocument.array().isEmpty()) {
        *error = QStringLiteral("Для проекта %1 нет версии под Minecraft %2 и загрузчик %3.")
                     .arg(projectId, gameVersion_, loader_);
        return false;
    }

    const QJsonObject version = versionsDocument.array().first().toObject();
    const QJsonArray dependencies = version.value(QStringLiteral("dependencies")).toArray();
    for (const QJsonValue &dependencyValue : dependencies) {
        const QJsonObject dependency = dependencyValue.toObject();
        if (dependency.value(QStringLiteral("dependency_type")).toString() != QStringLiteral("required")) {
            continue;
        }
        const QString dependencyProject = dependency.value(QStringLiteral("project_id")).toString();
        if (!dependencyProject.isEmpty() && !installProject(dependencyProject, installed, error)) {
            return false;
        }
    }

    const QJsonArray files = version.value(QStringLiteral("files")).toArray();
    QJsonObject selectedFile;
    for (const QJsonValue &fileValue : files) {
        const QJsonObject file = fileValue.toObject();
        if (file.value(QStringLiteral("primary")).toBool()) {
            selectedFile = file;
            break;
        }
    }
    if (selectedFile.isEmpty()) {
        for (const QJsonValue &fileValue : files) {
            const QJsonObject file = fileValue.toObject();
            if (file.value(QStringLiteral("filename")).toString().endsWith(QStringLiteral(".jar"),
                                                                             Qt::CaseInsensitive)) {
                selectedFile = file;
                break;
            }
        }
    }
    if (selectedFile.isEmpty()) {
        *error = QStringLiteral("У проекта %1 нет JAR-файла.").arg(projectId);
        return false;
    }

    const QString downloadUrl = selectedFile.value(QStringLiteral("url")).toString();
    QString filename = QFileInfo(selectedFile.value(QStringLiteral("filename")).toString()).fileName();
    if (filename.isEmpty()) {
        filename = projectId + QStringLiteral(".jar");
    }
    const QString sha1 = selectedFile.value(QStringLiteral("hashes")).toObject()
                             .value(QStringLiteral("sha1")).toString();
    if (downloadUrl.isEmpty()) {
        *error = QStringLiteral("У файла проекта %1 отсутствует download URL.").arg(projectId);
        return false;
    }

    const QString modsDirectory = QDir(gameDir_).filePath(QStringLiteral("mods"));
    if (!QDir().mkpath(modsDirectory)) {
        *error = QStringLiteral("Не удалось создать папку mods.");
        return false;
    }
    const QString destination = QDir(modsDirectory).filePath(filename);
    return downloadFile(downloadUrl, destination, sha1,
                        QStringLiteral("%1 → %2").arg(projectId, filename), error);
}

bool ModrinthInstallWorker::installModpack(QString *error)
{
    QUrl url(QString::fromLatin1(kApiBase) + QStringLiteral("/project/")
             + QString::fromUtf8(QUrl::toPercentEncoding(projectId_)) + QStringLiteral("/version"));
    QUrlQuery query;
    if (!gameVersion_.isEmpty()) {
        query.addQueryItem(QStringLiteral("game_versions"), jsonArrayQuery(gameVersion_));
    }
    if (!loader_.isEmpty() && loader_ != QStringLiteral("vanilla")) {
        query.addQueryItem(QStringLiteral("loaders"), jsonArrayQuery(loader_));
    }
    url.setQuery(query);

    const HttpResponse response = Http::get(url.toString(),
                                             {{QStringLiteral("User-Agent"),
                                               QStringLiteral("MCLauncher/0.1 (Modrinth client)")}});
    if (!response.ok()) {
        *error = QStringLiteral("Не удалось получить модпак %1: %2")
                     .arg(projectId_, responseMessage(response));
        return false;
    }
    const QJsonDocument versionsDocument = QJsonDocument::fromJson(response.body);
    if (!versionsDocument.isArray() || versionsDocument.array().isEmpty()) {
        *error = QStringLiteral("Для модпака %1 нет совместимой версии.").arg(projectId_);
        return false;
    }

    const QJsonObject version = versionsDocument.array().first().toObject();
    const QJsonArray files = version.value(QStringLiteral("files")).toArray();
    QJsonObject selectedFile;
    for (const QJsonValue &fileValue : files) {
        const QJsonObject file = fileValue.toObject();
        if (file.value(QStringLiteral("primary")).toBool()
            || file.value(QStringLiteral("filename")).toString().endsWith(QStringLiteral(".mrpack"),
                                                                            Qt::CaseInsensitive)) {
            selectedFile = file;
            if (file.value(QStringLiteral("primary")).toBool()) {
                break;
            }
        }
    }
    if (selectedFile.isEmpty()) {
        *error = QStringLiteral("У модпака %1 нет .mrpack-файла.").arg(projectId_);
        return false;
    }

    const QString archiveUrl = selectedFile.value(QStringLiteral("url")).toString();
    const QString archiveSha1 = selectedFile.value(QStringLiteral("hashes")).toObject()
                                    .value(QStringLiteral("sha1")).toString();
    QString archiveName = QFileInfo(selectedFile.value(QStringLiteral("filename")).toString()).fileName();
    if (archiveName.isEmpty()) {
        archiveName = projectId_ + QStringLiteral(".mrpack");
    }
    const QString archivePath = QDir(Paths::launcherCacheDir()).filePath(archiveName);
    if (!downloadFile(archiveUrl, archivePath, archiveSha1,
                      QStringLiteral("модпак %1").arg(projectId_), error)) {
        return false;
    }

    const QString unpackDirectory = QDir(Paths::launcherCacheDir()).filePath(
        QStringLiteral("modpack-%1").arg(projectId_));
    QDir(unpackDirectory).removeRecursively();
    if (!QDir().mkpath(unpackDirectory)) {
        *error = QStringLiteral("Не удалось создать временный каталог модпака.");
        return false;
    }
    if (!extractZip(archivePath, unpackDirectory, {}, error)) {
        QDir(unpackDirectory).removeRecursively();
        return false;
    }

    const QString indexPath = QDir(unpackDirectory).filePath(QStringLiteral("modrinth.index.json"));
    const QJsonDocument indexDocument = readJsonFile(indexPath, error);
    if (!indexDocument.isObject()) {
        QDir(unpackDirectory).removeRecursively();
        return false;
    }
    const QJsonObject index = indexDocument.object();
    const QJsonArray packFiles = index.value(QStringLiteral("files")).toArray();
    int completed = 0;
    for (const QJsonValue &fileValue : packFiles) {
        ++completed;
        const QJsonObject packFile = fileValue.toObject();
        const QString relativePath = packFile.value(QStringLiteral("path")).toString();
        if (!safeRelativePath(relativePath)) {
            *error = QStringLiteral("Модпак содержит небезопасный путь: %1").arg(relativePath);
            QDir(unpackDirectory).removeRecursively();
            return false;
        }
        const QJsonArray downloads = packFile.value(QStringLiteral("downloads")).toArray();
        if (downloads.isEmpty()) {
            *error = QStringLiteral("Для файла модпака нет download URL: %1").arg(relativePath);
            QDir(unpackDirectory).removeRecursively();
            return false;
        }
        const QString sha1 = packFile.value(QStringLiteral("hashes")).toObject()
                                 .value(QStringLiteral("sha1")).toString();
        const QString destination = QDir(gameDir_).filePath(QDir::cleanPath(relativePath));
        if (!downloadFile(downloads.first().toString(), destination, sha1,
                          QStringLiteral("файл модпака %1/%2").arg(completed).arg(packFiles.size()),
                          error)) {
            QDir(unpackDirectory).removeRecursively();
            return false;
        }
    }

    const QString overrides = QDir(unpackDirectory).filePath(QStringLiteral("overrides"));
    if (!copyTree(overrides, gameDir_, error)) {
        QDir(unpackDirectory).removeRecursively();
        return false;
    }
    const QString clientOverrides = QDir(unpackDirectory).filePath(QStringLiteral("client-overrides"));
    if (!copyTree(clientOverrides, gameDir_, error)) {
        QDir(unpackDirectory).removeRecursively();
        return false;
    }
    QDir(unpackDirectory).removeRecursively();
    return true;
}

void ModrinthInstallWorker::run()
{
    if (projectId_.isEmpty()) {
        emit failed(QStringLiteral("Проект Modrinth не выбран."));
        return;
    }
    if (!QDir().mkpath(QDir(gameDir_).filePath(QStringLiteral("mods")))) {
        emit failed(QStringLiteral("Не удалось создать папку mods."));
        return;
    }

    QString error;
    if (projectType_ == QStringLiteral("modpack")) {
        if (!installModpack(&error)) {
            emit failed(error);
            return;
        }
        emit progress(1, 1, QStringLiteral("Modrinth модпак готов"));
        emit logMessage(QStringLiteral("Modrinth модпак %1 установлен в выбранную папку игры.")
                            .arg(projectId_));
    } else {
        QSet<QString> installed;
        if (!installProject(projectId_, &installed, &error)) {
            emit failed(error);
            return;
        }
        emit progress(1, 1, QStringLiteral("Modrinth готов"));
        emit logMessage(QStringLiteral("Установлены проект и обязательные зависимости: %1")
                            .arg(projectId_));
    }
    emit finished(projectId_);
}

} // namespace mcl
