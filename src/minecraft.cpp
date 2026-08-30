#include "minecraft.h"

#include "app_paths.h"
#include "common.h"
#include "http.h"
#include "zip_extract.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

#include <utility>

namespace mcl {

namespace {

constexpr char kManifestUrl[] =
    "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
constexpr char kResourcesUrl[] = "https://resources.download.minecraft.net/";
constexpr char kForgeMetadataUrl[] =
    "https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml";

QJsonObject parseObject(const QByteArray &body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    return document.isObject() ? document.object() : QJsonObject();
}

QString defaultLibraryPath(const QString &coordinate, const QString &classifierOverride = {})
{
    QStringList parts = coordinate.split(QLatin1Char(':'));
    if (parts.size() < 3) {
        return {};
    }

    QString version = parts.at(2);
    QString extension = QStringLiteral("jar");
    const int extensionIndex = version.indexOf(QLatin1Char('@'));
    if (extensionIndex >= 0) {
        extension = version.mid(extensionIndex + 1);
        version = version.left(extensionIndex);
    }

    QString classifier = classifierOverride;
    if (classifier.isEmpty() && parts.size() >= 4) {
        classifier = parts.at(3);
    }
    if (parts.size() >= 5) {
        extension = parts.at(4);
    }

    QString groupPath = parts.at(0);
    groupPath.replace(QLatin1Char('.'), QLatin1Char('/'));
    const QString artifact = parts.at(1);
    const QString fileName = artifact + QLatin1Char('-') + version
                             + (classifier.isEmpty() ? QString() : QLatin1Char('-') + classifier)
                             + QLatin1Char('.') + extension;
    return groupPath + QLatin1Char('/') + artifact + QLatin1Char('/') + version
           + QLatin1Char('/') + fileName;
}

QString libraryBaseUrl(const QJsonObject &library)
{
    QString url = library.value(QStringLiteral("url")).toString();
    if (url.isEmpty()) {
        url = QStringLiteral("https://libraries.minecraft.net/");
    }
    if (!url.endsWith(QLatin1Char('/'))) {
        url += QLatin1Char('/');
    }
    return url;
}

QString nativeClassifier(const QJsonObject &library)
{
    const QJsonObject natives = library.value(QStringLiteral("natives")).toObject();
    QString classifier = natives.value(Rules::currentOsName()).toString();
    classifier.replace(QStringLiteral("${arch}"), Rules::nativeArch());
    return classifier;
}

QHash<QString, bool> installerFeatures()
{
    return {
        {QStringLiteral("is_demo_user"), false},
        {QStringLiteral("has_custom_resolution"), false},
        {QStringLiteral("has_quick_plays_support"), true},
        {QStringLiteral("is_quick_play_singleplayer"), false},
        {QStringLiteral("is_quick_play_multiplayer"), false},
        {QStringLiteral("is_quick_play_realms"), false}
    };
}

bool isSafeAssetKey(const QString &key)
{
    const QString clean = QDir::cleanPath(key);
    return !clean.isEmpty() && !clean.startsWith(QStringLiteral("../"))
           && !clean.contains(QStringLiteral("/../")) && !QDir::isAbsolutePath(clean);
}

} // namespace

void CatalogWorker::run()
{
    const HttpResponse response = Http::get(QString::fromLatin1(kManifestUrl));
    if (response.ok()) {
        QString error;
        if (!writeJsonFile(QDir(Paths::launcherCacheDir()).filePath(QStringLiteral("version_manifest_v2.json")),
                           QJsonDocument::fromJson(response.body), &error)) {
            // A cache write failure should not prevent a normal online launch.
            Q_UNUSED(error);
        }
        emit loaded(response.body);
        return;
    }

    const QString cachePath = QDir(Paths::launcherCacheDir()).filePath(QStringLiteral("version_manifest_v2.json"));
    if (QFileInfo::exists(cachePath)) {
        QFile cache(cachePath);
        if (cache.open(QIODevice::ReadOnly)) {
            emit loaded(cache.readAll());
            return;
        }
    }

    emit failed(QStringLiteral("Не удалось получить список версий: %1").arg(
        response.error.isEmpty() ? QStringLiteral("HTTP %1").arg(response.status) : response.error));
}

InstallerWorker::InstallerWorker(QString versionId, QString versionUrl, QString gameDir)
    : versionId_(std::move(versionId)), versionUrl_(std::move(versionUrl)), gameDir_(std::move(gameDir))
{
}

bool InstallerWorker::downloadFile(const QString &url,
                                   const QString &destination,
                                   const QString &sha1,
                                   const QString &label,
                                   QString *error)
{
    emit logMessage(QStringLiteral("Загрузка: %1").arg(label));
    return Http::download(url, destination, sha1,
                          [this, label](qint64 current, qint64 total) {
                              emit progress(current, total, label);
                          }, error);
}

bool InstallerWorker::installLibraries(const QJsonArray &libraries,
                                       const QString &gameDir,
                                       QString *error)
{
    const QHash<QString, bool> features = installerFeatures();
    const QString nativeDirectory = Paths::nativesDir(gameDir, versionId_);
    QDir(nativeDirectory).removeRecursively();
    if (!QDir().mkpath(nativeDirectory)) {
        if (error) {
            *error = QStringLiteral("Не удалось создать каталог natives.");
        }
        return false;
    }

    int libraryIndex = 0;
    for (const QJsonValue &value : libraries) {
        ++libraryIndex;
        const QJsonObject library = value.toObject();
        if (library.isEmpty() || !Rules::allowed(library.value(QStringLiteral("rules")).toArray(), features)) {
            continue;
        }

        const QString coordinate = library.value(QStringLiteral("name")).toString();
        QJsonObject downloads = library.value(QStringLiteral("downloads")).toObject();
        QJsonObject artifact = downloads.value(QStringLiteral("artifact")).toObject();
        QString artifactPath = artifact.value(QStringLiteral("path")).toString();
        QString artifactUrl = artifact.value(QStringLiteral("url")).toString();
        const QString artifactSha1 = artifact.value(QStringLiteral("sha1")).toString();
        if (artifactPath.isEmpty()) {
            artifactPath = defaultLibraryPath(coordinate);
        }
        if (artifactUrl.isEmpty() && !artifactPath.isEmpty()) {
            artifactUrl = libraryBaseUrl(library) + artifactPath;
        }

        if (!artifactPath.isEmpty() && !artifactUrl.isEmpty()) {
            const QString destination = QDir(Paths::librariesDir(gameDir)).filePath(artifactPath);
            if (!downloadFile(artifactUrl, destination, artifactSha1,
                              QStringLiteral("библиотека %1 (%2)").arg(coordinate).arg(libraryIndex),
                              error)) {
                return false;
            }
        }

        const QString classifierName = nativeClassifier(library);
        if (classifierName.isEmpty()) {
            continue;
        }

        QJsonObject classifier = downloads.value(QStringLiteral("classifiers")).toObject()
                                     .value(classifierName).toObject();
        QString nativePath = classifier.value(QStringLiteral("path")).toString();
        QString nativeUrl = classifier.value(QStringLiteral("url")).toString();
        const QString nativeSha1 = classifier.value(QStringLiteral("sha1")).toString();
        if (nativePath.isEmpty()) {
            nativePath = defaultLibraryPath(coordinate, classifierName);
        }
        if (nativeUrl.isEmpty() && !nativePath.isEmpty()) {
            nativeUrl = libraryBaseUrl(library) + nativePath;
        }
        if (nativePath.isEmpty() || nativeUrl.isEmpty()) {
            continue;
        }

        const QString nativeJar = QDir(Paths::librariesDir(gameDir)).filePath(nativePath);
        if (!downloadFile(nativeUrl, nativeJar, nativeSha1,
                          QStringLiteral("нативная библиотека %1").arg(coordinate), error)) {
            return false;
        }

        QStringList excluded;
        const QJsonArray excludeArray = library.value(QStringLiteral("extract")).toObject()
                                            .value(QStringLiteral("exclude")).toArray();
        for (const QJsonValue &excludedValue : excludeArray) {
            excluded.append(excludedValue.toString());
        }

        QString extractionError;
        if (!extractZip(nativeJar, nativeDirectory, excluded, &extractionError)) {
            if (error) {
                *error = QStringLiteral("Не удалось распаковать natives %1: %2")
                             .arg(nativeJar, extractionError);
            }
            return false;
        }
    }
    return true;
}

bool InstallerWorker::installAssets(const QJsonObject &version,
                                    const QString &gameDir,
                                    QString *error)
{
    const QJsonObject assetIndex = version.value(QStringLiteral("assetIndex")).toObject();
    const QString indexId = assetIndex.value(QStringLiteral("id")).toString();
    const QString indexUrl = assetIndex.value(QStringLiteral("url")).toString();
    if (indexId.isEmpty() || indexUrl.isEmpty()) {
        emit logMessage(QStringLiteral("У версии нет отдельного asset index, пропуск."));
        return true;
    }

    const QString indexPath = QDir(Paths::assetIndexesDir(gameDir)).filePath(indexId + QStringLiteral(".json"));
    if (!downloadFile(indexUrl, indexPath, assetIndex.value(QStringLiteral("sha1")).toString(),
                      QStringLiteral("индекс ресурсов %1").arg(indexId), error)) {
        return false;
    }

    QString jsonError;
    const QJsonDocument indexDocument = readJsonFile(indexPath, &jsonError);
    if (!indexDocument.isObject()) {
        if (error) {
            *error = jsonError.isEmpty() ? QStringLiteral("Повреждён индекс ресурсов.") : jsonError;
        }
        return false;
    }

    const QJsonObject objects = indexDocument.object().value(QStringLiteral("objects")).toObject();
    int completed = 0;
    const int total = objects.size();
    for (auto iterator = objects.constBegin(); iterator != objects.constEnd(); ++iterator) {
        ++completed;
        const QString key = iterator.key();
        const QJsonObject object = iterator.value().toObject();
        const QString hash = object.value(QStringLiteral("hash")).toString().toLower();
        if (hash.size() < 2 || !isSafeAssetKey(key)) {
            continue;
        }

        const QString objectPath = QDir(Paths::assetObjectsDir(gameDir))
                                       .filePath(hash.left(2) + QLatin1Char('/') + hash);
        const QString objectUrl = QString::fromLatin1(kResourcesUrl) + hash.left(2) + QLatin1Char('/') + hash;
        if (!downloadFile(objectUrl, objectPath, hash,
                          QStringLiteral("ресурс %1/%2").arg(completed).arg(total), error)) {
            return false;
        }

        if (indexDocument.object().value(QStringLiteral("virtual")).toBool()) {
            const QString virtualPath = QDir(gameDir).filePath(
                QStringLiteral("assets/virtual/%1/%2").arg(indexId, QDir::cleanPath(key)));
            if (!QFileInfo::exists(virtualPath)) {
                if (!QDir().mkpath(QFileInfo(virtualPath).absolutePath())
                    || !QFile::copy(objectPath, virtualPath)) {
                    if (error) {
                        *error = QStringLiteral("Не удалось создать виртуальный ресурс %1").arg(key);
                    }
                    return false;
                }
            }
        }
    }
    return true;
}

bool InstallerWorker::installLogging(const QJsonObject &version,
                                      const QString &gameDir,
                                      QString *error)
{
    const QJsonObject clientLogging = version.value(QStringLiteral("logging")).toObject()
                                          .value(QStringLiteral("client")).toObject();
    const QJsonObject file = clientLogging.value(QStringLiteral("file")).toObject();
    const QString id = file.value(QStringLiteral("id")).toString();
    const QString url = file.value(QStringLiteral("url")).toString();
    if (id.isEmpty() || url.isEmpty()) {
        return true;
    }

    const QString destination = QDir(Paths::assetsDir(gameDir)).filePath(QStringLiteral("log_configs/%1").arg(id));
    return downloadFile(url, destination, file.value(QStringLiteral("sha1")).toString(),
                        QStringLiteral("конфигурация логирования"), error);
}

void InstallerWorker::run()
{
    QString error;
    if (!Paths::ensureGameLayout(gameDir_, &error)) {
        emit failed(error);
        return;
    }

    emit logMessage(QStringLiteral("Получение манифеста Minecraft…"));
    QString selectedUrl = versionUrl_;
    if (selectedUrl.isEmpty()) {
        const HttpResponse manifestResponse = Http::get(QString::fromLatin1(kManifestUrl));
        if (!manifestResponse.ok()) {
            emit failed(QStringLiteral("Манифест версий недоступен: %1").arg(manifestResponse.error));
            return;
        }
        const QJsonObject manifest = parseObject(manifestResponse.body);
        for (const QJsonValue &value : manifest.value(QStringLiteral("versions")).toArray()) {
            const QJsonObject candidate = value.toObject();
            if (candidate.value(QStringLiteral("id")).toString() == versionId_) {
                selectedUrl = candidate.value(QStringLiteral("url")).toString();
                break;
            }
        }
    }

    if (selectedUrl.isEmpty()) {
        emit failed(QStringLiteral("Версия %1 отсутствует в официальном манифесте.").arg(versionId_));
        return;
    }

    emit logMessage(QStringLiteral("Загрузка описания версии %1…").arg(versionId_));
    const HttpResponse versionResponse = Http::get(selectedUrl);
    if (!versionResponse.ok()) {
        emit failed(QStringLiteral("Не удалось получить описание %1: %2")
                        .arg(versionId_, versionResponse.error));
        return;
    }

    const QJsonDocument versionDocument = QJsonDocument::fromJson(versionResponse.body);
    if (!versionDocument.isObject()) {
        emit failed(QStringLiteral("Описание версии %1 содержит некорректный JSON.").arg(versionId_));
        return;
    }
    const QJsonObject version = versionDocument.object();

    const QString versionDirectory = Paths::versionDir(gameDir_, versionId_);
    if (!QDir().mkpath(versionDirectory)) {
        emit failed(QStringLiteral("Не удалось создать каталог версии %1.").arg(versionDirectory));
        return;
    }
    const QString versionJsonPath = QDir(versionDirectory).filePath(versionId_ + QStringLiteral(".json"));
    if (!writeJsonFile(versionJsonPath, versionDocument, &error)) {
        emit failed(error);
        return;
    }

    const QJsonObject client = version.value(QStringLiteral("downloads")).toObject()
                                   .value(QStringLiteral("client")).toObject();
    const QString clientUrl = client.value(QStringLiteral("url")).toString();
    if (clientUrl.isEmpty()) {
        emit failed(QStringLiteral("Официальный манифест не содержит client.jar для %1.").arg(versionId_));
        return;
    }
    const QString clientJar = QDir(versionDirectory).filePath(versionId_ + QStringLiteral(".jar"));
    if (!downloadFile(clientUrl, clientJar, client.value(QStringLiteral("sha1")).toString(),
                      QStringLiteral("клиент Minecraft %1").arg(versionId_), &error)) {
        emit failed(error);
        return;
    }

    emit logMessage(QStringLiteral("Установка библиотек…"));
    if (!installLibraries(version.value(QStringLiteral("libraries")).toArray(), gameDir_, &error)) {
        emit failed(error);
        return;
    }

    emit logMessage(QStringLiteral("Установка ресурсов…"));
    if (!installAssets(version, gameDir_, &error)) {
        emit failed(error);
        return;
    }

    if (!installLogging(version, gameDir_, &error)) {
        emit failed(error);
        return;
    }

    emit progress(1, 1, QStringLiteral("Готово"));
    emit logMessage(QStringLiteral("Версия %1 установлена.").arg(versionId_));
    emit finished(versionId_, versionJsonPath);
}

FabricInstallerWorker::FabricInstallerWorker(QString minecraftVersion, QString gameDir)
    : minecraftVersion_(std::move(minecraftVersion)), gameDir_(std::move(gameDir))
{
}

void FabricInstallerWorker::run()
{
    QString error;
    if (!Paths::ensureGameLayout(gameDir_, &error)) {
        emit failed(error);
        return;
    }

    const QString parentJson = QDir(Paths::versionDir(gameDir_, minecraftVersion_))
                                   .filePath(minecraftVersion_ + QStringLiteral(".json"));
    const QString parentJar = QDir(Paths::versionDir(gameDir_, minecraftVersion_))
                                  .filePath(minecraftVersion_ + QStringLiteral(".jar"));
    if (!QFileInfo::exists(parentJson) || !QFileInfo::exists(parentJar)) {
        emit failed(QStringLiteral("Сначала установите обычную версию Minecraft %1.").arg(minecraftVersion_));
        return;
    }

    const QString encodedVersion = QString::fromUtf8(QUrl::toPercentEncoding(minecraftVersion_));
    const QString versionsUrl = QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/%1")
                                    .arg(encodedVersion);
    emit logMessage(QStringLiteral("Получение списка Fabric Loader для %1…").arg(minecraftVersion_));
    const HttpResponse versionsResponse = Http::get(versionsUrl);
    if (!versionsResponse.ok()) {
        emit failed(QStringLiteral("Fabric Meta недоступен: %1").arg(versionsResponse.error));
        return;
    }

    const QJsonDocument versionsDocument = QJsonDocument::fromJson(versionsResponse.body);
    const QJsonArray loaders = versionsDocument.isArray() ? versionsDocument.array() : QJsonArray();
    QJsonObject selectedLoader;
    for (const QJsonValue &value : loaders) {
        const QJsonObject loader = value.toObject().value(QStringLiteral("loader")).toObject();
        if (loader.value(QStringLiteral("stable")).toBool()) {
            selectedLoader = loader;
            break;
        }
    }
    if (selectedLoader.isEmpty() && !loaders.isEmpty()) {
        selectedLoader = loaders.first().toObject().value(QStringLiteral("loader")).toObject();
    }
    const QString loaderVersion = selectedLoader.value(QStringLiteral("version")).toString();
    if (loaderVersion.isEmpty()) {
        emit failed(QStringLiteral("Для Minecraft %1 не найден Fabric Loader.").arg(minecraftVersion_));
        return;
    }

    const QString encodedLoader = QString::fromUtf8(QUrl::toPercentEncoding(loaderVersion));
    const QString profileUrl = QStringLiteral(
        "https://meta.fabricmc.net/v2/versions/loader/%1/%2/profile/json")
                                   .arg(encodedVersion, encodedLoader);
    const HttpResponse profileResponse = Http::get(profileUrl);
    if (!profileResponse.ok()) {
        emit failed(QStringLiteral("Не удалось получить профиль Fabric %1: %2")
                        .arg(loaderVersion, profileResponse.error));
        return;
    }
    const QJsonDocument profileDocument = QJsonDocument::fromJson(profileResponse.body);
    if (!profileDocument.isObject()) {
        emit failed(QStringLiteral("Fabric вернул некорректный профиль."));
        return;
    }
    const QJsonObject profile = profileDocument.object();
    const QString profileId = profile.value(QStringLiteral("id")).toString();
    if (profileId.isEmpty()) {
        emit failed(QStringLiteral("В профиле Fabric отсутствует id."));
        return;
    }

    const QJsonArray libraries = profile.value(QStringLiteral("libraries")).toArray();
    int completed = 0;
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (library.isEmpty() || !Rules::allowed(library.value(QStringLiteral("rules")).toArray())) {
            continue;
        }
        const QString coordinate = library.value(QStringLiteral("name")).toString();
        const QString path = defaultLibraryPath(coordinate);
        if (path.isEmpty()) {
            continue;
        }
        QString baseUrl = library.value(QStringLiteral("url")).toString();
        if (baseUrl.isEmpty()) {
            baseUrl = QStringLiteral("https://maven.fabricmc.net/");
        }
        if (!baseUrl.endsWith(QLatin1Char('/'))) {
            baseUrl += QLatin1Char('/');
        }
        const QString url = baseUrl + path;
        const QString destination = QDir(Paths::librariesDir(gameDir_)).filePath(path);
        emit logMessage(QStringLiteral("Fabric библиотека: %1").arg(coordinate));
        if (!Http::download(url, destination, library.value(QStringLiteral("sha1")).toString(),
                            [this, &completed, libraries](qint64 current, qint64 total) {
                                emit progress(current, total,
                                              QStringLiteral("Fabric: библиотека %1/%2")
                                                  .arg(completed).arg(libraries.size()));
                            }, &error)) {
            emit failed(error);
            return;
        }
        ++completed;
    }

    const QString profileDirectory = Paths::versionDir(gameDir_, profileId);
    if (!QDir().mkpath(profileDirectory)) {
        emit failed(QStringLiteral("Не удалось создать каталог профиля Fabric."));
        return;
    }
    const QString profilePath = QDir(profileDirectory).filePath(profileId + QStringLiteral(".json"));
    if (!writeJsonFile(profilePath, profileDocument, &error)) {
        emit failed(error);
        return;
    }
    emit progress(1, 1, QStringLiteral("Fabric установлен"));
    emit logMessage(QStringLiteral("Профиль Fabric %1 готов.").arg(profileId));
    emit finished(profileId, profilePath);
}

QuiltInstallerWorker::QuiltInstallerWorker(QString minecraftVersion, QString gameDir)
    : minecraftVersion_(std::move(minecraftVersion)), gameDir_(std::move(gameDir))
{
}

void QuiltInstallerWorker::run()
{
    QString error;
    if (!Paths::ensureGameLayout(gameDir_, &error)) {
        emit failed(error);
        return;
    }

    const QString parentJson = QDir(Paths::versionDir(gameDir_, minecraftVersion_))
                                   .filePath(minecraftVersion_ + QStringLiteral(".json"));
    const QString parentJar = QDir(Paths::versionDir(gameDir_, minecraftVersion_))
                                  .filePath(minecraftVersion_ + QStringLiteral(".jar"));
    if (!QFileInfo::exists(parentJson) || !QFileInfo::exists(parentJar)) {
        emit failed(QStringLiteral("Сначала установите обычную версию Minecraft %1.").arg(minecraftVersion_));
        return;
    }

    const QString encodedVersion = QString::fromUtf8(QUrl::toPercentEncoding(minecraftVersion_));
    const QString versionsUrl = QStringLiteral("https://meta.quiltmc.org/v3/versions/loader/%1")
                                    .arg(encodedVersion);
    emit logMessage(QStringLiteral("Получение списка Quilt Loader для %1…").arg(minecraftVersion_));
    const HttpResponse versionsResponse = Http::get(versionsUrl);
    if (!versionsResponse.ok()) {
        emit failed(QStringLiteral("Quilt Meta недоступен: %1").arg(versionsResponse.error));
        return;
    }

    const QJsonDocument versionsDocument = QJsonDocument::fromJson(versionsResponse.body);
    const QJsonArray loaders = versionsDocument.isArray() ? versionsDocument.array() : QJsonArray();
    if (loaders.isEmpty()) {
        emit failed(QStringLiteral("Для Minecraft %1 не найден Quilt Loader.").arg(minecraftVersion_));
        return;
    }
    const QString loaderVersion = loaders.first().toObject().value(QStringLiteral("loader"))
                                      .toObject().value(QStringLiteral("version")).toString();
    if (loaderVersion.isEmpty()) {
        emit failed(QStringLiteral("Quilt Meta вернул пустую версию загрузчика."));
        return;
    }

    const QString encodedLoader = QString::fromUtf8(QUrl::toPercentEncoding(loaderVersion));
    const QString profileUrl = QStringLiteral(
        "https://meta.quiltmc.org/v3/versions/loader/%1/%2/profile/json")
                                   .arg(encodedVersion, encodedLoader);
    const HttpResponse profileResponse = Http::get(profileUrl);
    if (!profileResponse.ok()) {
        emit failed(QStringLiteral("Не удалось получить профиль Quilt %1: %2")
                        .arg(loaderVersion, profileResponse.error));
        return;
    }
    const QJsonDocument profileDocument = QJsonDocument::fromJson(profileResponse.body);
    if (!profileDocument.isObject()) {
        emit failed(QStringLiteral("Quilt вернул некорректный профиль."));
        return;
    }
    const QJsonObject profile = profileDocument.object();
    const QString profileId = profile.value(QStringLiteral("id")).toString();
    if (profileId.isEmpty()) {
        emit failed(QStringLiteral("В профиле Quilt отсутствует id."));
        return;
    }

    const QJsonArray libraries = profile.value(QStringLiteral("libraries")).toArray();
    int completed = 0;
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (library.isEmpty() || !Rules::allowed(library.value(QStringLiteral("rules")).toArray())) {
            continue;
        }
        const QString coordinate = library.value(QStringLiteral("name")).toString();
        const QString path = defaultLibraryPath(coordinate);
        if (path.isEmpty()) {
            continue;
        }
        QString baseUrl = library.value(QStringLiteral("url")).toString();
        if (baseUrl.isEmpty()) {
            baseUrl = QStringLiteral("https://maven.quiltmc.org/repository/release/");
        }
        if (!baseUrl.endsWith(QLatin1Char('/'))) {
            baseUrl += QLatin1Char('/');
        }
        const QString destination = QDir(Paths::librariesDir(gameDir_)).filePath(path);
        emit logMessage(QStringLiteral("Quilt библиотека: %1").arg(coordinate));
        if (!Http::download(baseUrl + path, destination,
                            library.value(QStringLiteral("sha1")).toString(),
                            [this, &completed, libraries](qint64 current, qint64 total) {
                                emit progress(current, total,
                                              QStringLiteral("Quilt: библиотека %1/%2")
                                                  .arg(completed).arg(libraries.size()));
                            }, &error)) {
            emit failed(error);
            return;
        }
        ++completed;
    }

    const QString profileDirectory = Paths::versionDir(gameDir_, profileId);
    if (!QDir().mkpath(profileDirectory)) {
        emit failed(QStringLiteral("Не удалось создать каталог профиля Quilt."));
        return;
    }
    const QString profilePath = QDir(profileDirectory).filePath(profileId + QStringLiteral(".json"));
    if (!writeJsonFile(profilePath, profileDocument, &error)) {
        emit failed(error);
        return;
    }
    emit progress(1, 1, QStringLiteral("Quilt установлен"));
    emit logMessage(QStringLiteral("Профиль Quilt %1 готов.").arg(profileId));
    emit finished(profileId, profilePath);
}

ForgeInstallerWorker::ForgeInstallerWorker(QString minecraftVersion,
                                             QString gameDir,
                                             QString javaPath)
    : minecraftVersion_(std::move(minecraftVersion)), gameDir_(std::move(gameDir)),
      javaPath_(std::move(javaPath))
{
}

void ForgeInstallerWorker::run()
{
    QString error;
    if (!Paths::ensureGameLayout(gameDir_, &error)) {
        emit failed(error);
        return;
    }

    const QString vanillaJson = QDir(Paths::versionDir(gameDir_, minecraftVersion_))
                                    .filePath(minecraftVersion_ + QStringLiteral(".json"));
    const QString vanillaJar = QDir(Paths::versionDir(gameDir_, minecraftVersion_))
                                   .filePath(minecraftVersion_ + QStringLiteral(".jar"));
    if (!QFileInfo::exists(vanillaJson) || !QFileInfo::exists(vanillaJar)) {
        emit failed(QStringLiteral("Сначала установите обычную версию Minecraft %1.")
                        .arg(minecraftVersion_));
        return;
    }
    if (javaPath_.isEmpty() || !QFileInfo::exists(javaPath_)) {
        emit failed(QStringLiteral("Для запуска Forge installer укажите путь к java.exe."));
        return;
    }

    emit logMessage(QStringLiteral("Получение списка версий Forge для %1…").arg(minecraftVersion_));
    const HttpResponse metadataResponse = Http::get(
        QString::fromLatin1(kForgeMetadataUrl),
        {{QStringLiteral("User-Agent"), QStringLiteral("MCLauncher/0.1 (Forge installer client)")}});
    if (!metadataResponse.ok()) {
        emit failed(QStringLiteral("Не удалось получить список Forge: %1")
                        .arg(metadataResponse.error.isEmpty()
                                 ? QStringLiteral("HTTP %1").arg(metadataResponse.status)
                                 : metadataResponse.error));
        return;
    }

    QXmlStreamReader xml(metadataResponse.body);
    QString selectedVersion;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("version")) {
            continue;
        }
        const QString version = xml.readElementText();
        if (version.startsWith(minecraftVersion_ + QLatin1Char('-'))) {
            // Maven metadata is ordered from older to newer releases.
            selectedVersion = version;
        }
    }
    if (xml.hasError()) {
        emit failed(QStringLiteral("Forge metadata содержит некорректный XML."));
        return;
    }
    if (selectedVersion.isEmpty()) {
        emit failed(QStringLiteral("Для Minecraft %1 не найден Forge.").arg(minecraftVersion_));
        return;
    }

    const QString installerUrl = QStringLiteral(
        "https://maven.minecraftforge.net/net/minecraftforge/forge/%1/forge-%1-installer.jar")
                                     .arg(selectedVersion);
    const QString installerPath = QDir(Paths::launcherCacheDir()).filePath(
        QStringLiteral("forge-%1-installer.jar").arg(selectedVersion));
    emit logMessage(QStringLiteral("Загрузка Forge %1…").arg(selectedVersion));
    if (!Http::download(installerUrl, installerPath, {},
                        [this](qint64 current, qint64 total) {
                            emit progress(current, total, QStringLiteral("Forge installer"));
                        }, &error)) {
        emit failed(error);
        return;
    }

    emit progress(0, 0, QStringLiteral("Запуск Forge installer…"));
    QProcess installer;
    installer.setProgram(javaPath_);
    installer.setArguments({QStringLiteral("-jar"), installerPath,
                            QStringLiteral("--installClient"), gameDir_});
    installer.setWorkingDirectory(gameDir_);
    installer.setProcessChannelMode(QProcess::MergedChannels);
    installer.start();
    if (!installer.waitForStarted(10000)) {
        emit failed(QStringLiteral("Forge installer не запустился: %1").arg(installer.errorString()));
        return;
    }
    installer.waitForFinished(-1);
    const QString output = QString::fromLocal8Bit(installer.readAll()).trimmed();
    if (!output.isEmpty()) {
        emit logMessage(output.right(8000));
    }
    if (installer.exitStatus() != QProcess::NormalExit || installer.exitCode() != 0) {
        emit failed(QStringLiteral("Forge installer завершился с ошибкой (код %1).")
                        .arg(installer.exitCode()));
        return;
    }

    QString profileId;
    QDateTime newest;
    const QDir versionsDirectory(QDir(gameDir_).filePath(QStringLiteral("versions")));
    const QFileInfoList versionDirectories = versionsDirectory.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo &directory : versionDirectories) {
        const QString candidatePath = QDir(directory.absoluteFilePath())
                                          .filePath(directory.fileName() + QStringLiteral(".json"));
        const QJsonDocument profileDocument = readJsonFile(candidatePath);
        if (!profileDocument.isObject()) {
            continue;
        }
        const QJsonObject profile = profileDocument.object();
        if (!profile.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("forge-"))
            || profile.value(QStringLiteral("inheritsFrom")).toString() != minecraftVersion_) {
            continue;
        }
        if (profileId.isEmpty() || directory.lastModified() > newest) {
            profileId = profile.value(QStringLiteral("id")).toString();
            newest = directory.lastModified();
        }
    }
    if (profileId.isEmpty()) {
        emit failed(QStringLiteral("Forge installer завершился, но профиль Forge не найден."));
        return;
    }

    const QString profilePath = QDir(Paths::versionDir(gameDir_, profileId))
                                    .filePath(profileId + QStringLiteral(".json"));
    emit progress(1, 1, QStringLiteral("Forge установлен"));
    emit logMessage(QStringLiteral("Профиль Forge %1 готов.").arg(profileId));
    emit finished(profileId, profilePath);
}

void NewsWorker::run()
{
    constexpr char kNewsUrl[] = "https://launchercontent.mojang.com/news.json";
    const HttpResponse response = Http::get(QString::fromLatin1(kNewsUrl));
    QByteArray body = response.body;
    if (!response.ok()) {
        const QString cachePath = QDir(Paths::launcherCacheDir()).filePath(QStringLiteral("news.json"));
        QFile cache(cachePath);
        if (cache.open(QIODevice::ReadOnly)) {
            body = cache.readAll();
        } else {
            emit failed(QStringLiteral("Новости недоступны: %1").arg(response.error));
            return;
        }
    } else {
        QString cacheError;
        writeJsonFile(QDir(Paths::launcherCacheDir()).filePath(QStringLiteral("news.json")),
                      QJsonDocument::fromJson(body), &cacheError);
    }

    const QJsonObject root = parseObject(body);
    const QJsonArray entries = root.value(QStringLiteral("entries")).toArray();
    QString html = QStringLiteral("<html><body style='font-family:Segoe UI;color:#eef3fb;background:#121822;'>");
    if (entries.isEmpty()) {
        html += QStringLiteral("<p>Новых публикаций нет.</p>");
    }
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const QString title = entry.value(QStringLiteral("title")).toString();
        const QString date = entry.value(QStringLiteral("date")).toString();
        QString text = entry.value(QStringLiteral("text")).toString();
        if (text.isEmpty()) {
            text = entry.value(QStringLiteral("shortText")).toString();
        }
        text = text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>"));
        const QString link = entry.value(QStringLiteral("readMoreLink")).toString();
        html += QStringLiteral("<h2 style='color:#8db8ff;'>%1</h2><small style='color:#9fb0c7;'>%2</small><p>%3</p>")
                    .arg(title.toHtmlEscaped(), date.toHtmlEscaped(), text);
        if (!link.isEmpty()) {
            html += QStringLiteral("<p><a style='color:#75aaff;' href='%1'>Читать на minecraft.net</a></p>")
                        .arg(link.toHtmlEscaped());
        }
        html += QStringLiteral("<hr style='border:0;border-top:1px solid #2b3547;'>");
    }
    html += QStringLiteral("</body></html>");
    emit loaded(html);
}

} // namespace mcl
