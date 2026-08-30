#include "launcher.h"

#include "app_paths.h"
#include "common.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>

namespace mcl {

namespace {

QJsonArray libraryArray(const QJsonValue &value)
{
    if (value.isArray()) {
        return value.toArray();
    }
    QJsonArray result;
    const QJsonObject object = value.toObject();
    // Fabric's launcher metadata groups libraries by side; a regular
    // version JSON stores the same entries directly in an array.
    for (const QString &key : {QStringLiteral("common"), QStringLiteral("client"),
                               QStringLiteral("server"), QStringLiteral("development")}) {
        for (const QJsonValue &entry : object.value(key).toArray()) {
            result.append(entry);
        }
    }
    return result;
}

QJsonArray appendArgumentValues(const QJsonValue &base, const QJsonValue &extra)
{
    QJsonArray result;
    if (base.isArray()) {
        result = base.toArray();
    } else if (base.isString()) {
        result.append(base);
    }
    if (extra.isArray()) {
        for (const QJsonValue &value : extra.toArray()) {
            result.append(value);
        }
    } else if (extra.isString()) {
        result.append(extra);
    }
    return result;
}

QJsonObject mergeVersion(const QJsonObject &parent, const QJsonObject &child)
{
    QJsonObject merged = parent;
    for (auto iterator = child.constBegin(); iterator != child.constEnd(); ++iterator) {
        const QString key = iterator.key();
        if (key == QStringLiteral("inheritsFrom")) {
            continue;
        }
        if (key == QStringLiteral("libraries")) {
            QJsonArray libraries = libraryArray(merged.value(key));
            for (const QJsonValue &library : libraryArray(iterator.value())) {
                libraries.append(library);
            }
            merged.insert(key, libraries);
            continue;
        }
        if (key == QStringLiteral("arguments")) {
            QJsonObject arguments = merged.value(key).toObject();
            const QJsonObject childArguments = iterator.value().toObject();
            for (const QString &argumentType : {QStringLiteral("jvm"), QStringLiteral("game")}) {
                if (childArguments.contains(argumentType)) {
                    arguments.insert(argumentType,
                                     appendArgumentValues(arguments.value(argumentType),
                                                          childArguments.value(argumentType)));
                }
            }
            merged.insert(key, arguments);
            continue;
        }
        merged.insert(key, iterator.value());
    }
    return merged;
}

QJsonObject loadMergedVersion(const QString &gameDir,
                              const QString &versionId,
                              QString *baseVersionId,
                              QSet<QString> *visited,
                              QString *error)
{
    if (visited->contains(versionId)) {
        if (error) {
            *error = QStringLiteral("Циклическая ссылка inheritsFrom в профиле %1.").arg(versionId);
        }
        return {};
    }
    visited->insert(versionId);

    const QString path = QDir(Paths::versionDir(gameDir, versionId))
                             .filePath(versionId + QStringLiteral(".json"));
    QString readError;
    const QJsonDocument document = readJsonFile(path, &readError);
    if (!document.isObject()) {
        if (error) {
            *error = readError;
        }
        return {};
    }
    const QJsonObject current = document.object();
    const QString parentId = current.value(QStringLiteral("inheritsFrom")).toString();
    if (parentId.isEmpty()) {
        *baseVersionId = versionId;
        return current;
    }

    const QJsonObject parent = loadMergedVersion(gameDir, parentId, baseVersionId, visited, error);
    if (parent.isEmpty()) {
        return {};
    }
    return mergeVersion(parent, current);
}

QHash<QString, bool> launchFeatures(const LaunchOptions &options)
{
    return {
        {QStringLiteral("is_demo_user"), false},
        {QStringLiteral("has_custom_resolution"), options.customResolution},
        {QStringLiteral("has_quick_plays_support"), true},
        {QStringLiteral("is_quick_play_singleplayer"), false},
        {QStringLiteral("is_quick_play_multiplayer"), false},
        {QStringLiteral("is_quick_play_realms"), false}
    };
}

QStringList parseArguments(const QJsonValue &value,
                           const QHash<QString, bool> &features,
                           const QHash<QString, QString> &variables)
{
    QStringList result;
    if (value.isString()) {
        const QStringList tokens = splitCommandLine(value.toString());
        for (const QString &token : tokens) {
            result.append(replaceVariables(token, variables));
        }
        return result;
    }

    for (const QJsonValue &entry : value.toArray()) {
        if (entry.isString()) {
            result.append(replaceVariables(entry.toString(), variables));
            continue;
        }
        const QJsonObject object = entry.toObject();
        if (object.isEmpty() || !Rules::allowed(object.value(QStringLiteral("rules")).toArray(), features)) {
            continue;
        }
        const QJsonValue argumentValue = object.value(QStringLiteral("value"));
        if (argumentValue.isString()) {
            result.append(replaceVariables(argumentValue.toString(), variables));
        } else {
            for (const QJsonValue &nested : argumentValue.toArray()) {
                result.append(replaceVariables(nested.toString(), variables));
            }
        }
    }
    return result;
}

QString artifactPathFromName(const QString &coordinate)
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
    QString classifier;
    if (parts.size() >= 4) {
        classifier = parts.at(3);
    }
    if (parts.size() >= 5) {
        extension = parts.at(4);
    }
    QString group = parts.at(0);
    group.replace(QLatin1Char('.'), QLatin1Char('/'));
    const QString artifact = parts.at(1);
    const QString fileName = artifact + QLatin1Char('-') + version
                             + (classifier.isEmpty() ? QString() : QLatin1Char('-') + classifier)
                             + QLatin1Char('.') + extension;
    return group + QLatin1Char('/') + artifact + QLatin1Char('/') + version
           + QLatin1Char('/') + fileName;
}

QStringList classpathFor(const QJsonArray &libraries,
                         const QString &gameDir,
                         const QHash<QString, bool> &features,
                         QString *missing)
{
    QStringList classpath;
    QStringList missingFiles;
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (library.isEmpty() || !Rules::allowed(library.value(QStringLiteral("rules")).toArray(), features)) {
            continue;
        }

        const QJsonObject artifact = library.value(QStringLiteral("downloads")).toObject()
                                         .value(QStringLiteral("artifact")).toObject();
        QString path = artifact.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            path = artifactPathFromName(library.value(QStringLiteral("name")).toString());
        }
        if (path.isEmpty()) {
            continue;
        }
        const QString fullPath = QDir(Paths::librariesDir(gameDir)).filePath(path);
        if (!QFileInfo::exists(fullPath)) {
            missingFiles.append(fullPath);
        } else {
            classpath.append(QDir::cleanPath(fullPath));
        }
    }

    if (missing) {
        *missing = missingFiles.join(QStringLiteral("\n"));
    }
    return classpath;
}

bool hasClassPathArgument(const QStringList &arguments)
{
    for (const QString &argument : arguments) {
        if (argument == QStringLiteral("-cp") || argument == QStringLiteral("-classpath")
            || argument == QStringLiteral("--class-path")) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace {

void appendJavaCandidate(QStringList *candidates, const QString &candidate)
{
    if (candidate.trimmed().isEmpty()) {
        return;
    }
    const QString clean = QDir::cleanPath(candidate.trimmed());
    if (!candidates->contains(clean)) {
        candidates->append(clean);
    }
}

void appendJavaHome(QStringList *candidates, const QString &home)
{
    if (home.trimmed().isEmpty()) {
        return;
    }
#ifdef Q_OS_WIN
    appendJavaCandidate(candidates, QDir(home).filePath(QStringLiteral("bin/java.exe")));
#else
    appendJavaCandidate(candidates, QDir(home).filePath(QStringLiteral("bin/java")));
#endif
}

void appendJdkRoot(QStringList *candidates, const QString &root)
{
    const QDir directory(root);
    if (!directory.exists()) {
        return;
    }
    const QFileInfoList entries = directory.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        appendJavaHome(candidates, entry.absoluteFilePath());
    }
}

int inferredJavaMajor(const QString &versionId)
{
    const QStringList parts = versionId.split(QLatin1Char('.'));
    if (parts.size() >= 2 && parts.at(0) == QStringLiteral("1")) {
        bool ok = false;
        const int minor = parts.at(1).toInt(&ok);
        if (!ok) {
            return 0;
        }
        int patch = 0;
        if (parts.size() >= 3) {
            patch = parts.at(2).section(QLatin1Char('-'), 0, 0).toInt();
        }
        if (minor > 20 || (minor == 20 && patch >= 5)) {
            return 21;
        }
        if (minor >= 18) {
            return 17;
        }
        return 8;
    }
    return 0;
}

} // namespace

int LauncherBuilder::javaMajorVersion(const QString &javaPath)
{
    if (javaPath.trimmed().isEmpty() || !QFileInfo::exists(javaPath)) {
        return 0;
    }
    QProcess process;
    process.setProgram(javaPath);
    process.setArguments({QStringLiteral("-version")});
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(2500)) {
        return 0;
    }
    if (!process.waitForFinished(2500)) {
        process.kill();
        process.waitForFinished(1000);
        return 0;
    }

    const QString output = QString::fromLocal8Bit(process.readAll());
    const QRegularExpression versionExpression(QStringLiteral("version\\s+\"(?:1\\.)?(\\d+)"));
    const QRegularExpressionMatch match = versionExpression.match(output);
    if (!match.hasMatch()) {
        return 0;
    }
    return match.captured(1).toInt();
}

QString LauncherBuilder::findJava(const QString &configuredPath, int requiredMajor)
{
    QStringList candidates;
    const QFileInfo configuredInfo(configuredPath.trimmed());
    if (configuredInfo.isDir()) {
        appendJavaHome(&candidates, configuredInfo.absoluteFilePath());
    } else {
        appendJavaCandidate(&candidates, configuredPath);
    }

    appendJavaHome(&candidates, qEnvironmentVariable("JAVA_HOME"));

    const QString runtimeBase = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime"));
#ifdef Q_OS_WIN
    const QString runtimeSuffix = QStringLiteral("-windows-x64");
    const QString executable = QStringLiteral("java.exe");
#else
#if defined(Q_OS_MACOS)
    const QString runtimeSuffix = QStringLiteral("-mac-os");
#else
    const QString runtimeSuffix = QStringLiteral("-linux");
#endif
    const QString executable = QStringLiteral("java");
#endif
    for (const QString &component : {QStringLiteral("java-runtime-alpha"),
                                     QStringLiteral("java-runtime-beta"),
                                     QStringLiteral("java-runtime-gamma"),
                                     QStringLiteral("java-runtime-delta"),
                                     QStringLiteral("java-runtime-epsilon"),
                                     QStringLiteral("jre-legacy")}) {
        appendJavaCandidate(&candidates,
                            QDir(runtimeBase).filePath(component + runtimeSuffix
                                                        + QStringLiteral("/bin/") + executable));
    }

#ifdef Q_OS_WIN
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    for (const QString &root : {QDir(programFiles).filePath(QStringLiteral("Java")),
                                QDir(programFiles).filePath(QStringLiteral("Eclipse Adoptium")),
                                QDir(programFiles).filePath(QStringLiteral("Microsoft")),
                                QDir(programFiles).filePath(QStringLiteral("Amazon Corretto")),
                                QDir(programFiles).filePath(QStringLiteral("Zulu")),
                                QDir(programFiles).filePath(QStringLiteral("BellSoft")),
                                QDir(programFilesX86).filePath(QStringLiteral("Java")),
                                QDir(localAppData).filePath(QStringLiteral("Programs/Eclipse Adoptium"))}) {
        appendJdkRoot(&candidates, root);
    }
#else
    appendJdkRoot(&candidates, QStringLiteral("/usr/lib/jvm"));
    appendJdkRoot(&candidates, QDir::home().filePath(QStringLiteral(".sdkman/candidates/java")));
#endif

    appendJavaCandidate(&candidates, QStandardPaths::findExecutable(executable));
    if (executable != QStringLiteral("java")) {
        appendJavaCandidate(&candidates, QStandardPaths::findExecutable(QStringLiteral("java")));
    }

    for (const QString &candidate : candidates) {
        QFileInfo info(candidate);
        if (!info.exists() || !info.isFile()) {
            continue;
        }
        const QString absolutePath = info.absoluteFilePath();
        if (requiredMajor <= 0 || javaMajorVersion(absolutePath) == requiredMajor) {
            return absolutePath;
        }
    }
    return {};
}

LaunchSpec LauncherBuilder::build(const LaunchOptions &options)
{
    LaunchSpec spec;
    if (!options.account.isAuthenticated()) {
        spec.error = QStringLiteral("Сначала войдите через официальный аккаунт Microsoft.");
        return spec;
    }
    if (options.versionId.isEmpty() || options.gameDir.isEmpty()) {
        spec.error = QStringLiteral("Не выбрана версия или папка игры.");
        return spec;
    }

    QString jsonError;
    QString baseVersionId;
    QSet<QString> visited;
    const QJsonObject version = loadMergedVersion(options.gameDir, options.versionId,
                                                   &baseVersionId, &visited, &jsonError);
    if (version.isEmpty()) {
        spec.error = jsonError.isEmpty() ? QStringLiteral("Описание установленной версии не найдено.") : jsonError;
        return spec;
    }
    if (baseVersionId.isEmpty()) {
        baseVersionId = options.versionId;
    }

    const QJsonObject javaVersion = version.value(QStringLiteral("javaVersion")).toObject();
    const int requiredJavaMajor = javaVersion.value(QStringLiteral("majorVersion")).toInt(
        inferredJavaMajor(baseVersionId));
    spec.requiredJavaMajor = requiredJavaMajor;
    const QString java = findJava(options.javaPath, requiredJavaMajor);
    if (java.isEmpty()) {
        if (requiredJavaMajor > 0) {
            spec.error = QStringLiteral(
                "Для Minecraft %1 требуется Java %2. Установите JDK этой версии или укажите путь к подходящему java.exe.")
                             .arg(baseVersionId)
                             .arg(requiredJavaMajor);
        } else {
            spec.error = QStringLiteral("Java не найдена. Укажите путь к java.exe или установите совместимый JDK.");
        }
        return spec;
    }
    spec.javaMajor = javaMajorVersion(java);

    const QString clientJar = QDir(Paths::versionDir(options.gameDir, baseVersionId))
                                  .filePath(baseVersionId + QStringLiteral(".jar"));
    if (!QFileInfo::exists(clientJar)) {
        spec.error = QStringLiteral("Клиент %1.jar не найден. Установите базовую версию.")
                         .arg(baseVersionId);
        return spec;
    }

    QString missing;
    const QHash<QString, bool> features = launchFeatures(options);
    QStringList classpath = classpathFor(version.value(QStringLiteral("libraries")).toArray(),
                                         options.gameDir, features, &missing);
    if (!missing.isEmpty()) {
        spec.error = QStringLiteral("Не хватает библиотек. Нажмите «Установить/обновить».\n%1")
                         .arg(missing.split(QLatin1Char('\n')).mid(0, 5).join(QLatin1Char('\n')));
        return spec;
    }
    classpath.append(clientJar);
    const QString classpathValue = classpath.join(QDir::listSeparator());

    const QJsonObject assetIndex = version.value(QStringLiteral("assetIndex")).toObject();
    const QString assetsIndexName = assetIndex.value(QStringLiteral("id")).toString();
    const QString nativeDirectory = Paths::nativesDir(options.gameDir, options.versionId);
    const QString gameDirectory = QDir::cleanPath(options.gameDir);
    const QString assetsRoot = Paths::assetsDir(gameDirectory);

    QHash<QString, QString> variables {
        {QStringLiteral("auth_player_name"), options.account.name},
        {QStringLiteral("version_name"), options.versionId},
        {QStringLiteral("game_directory"), gameDirectory},
        {QStringLiteral("assets_root"), assetsRoot},
        {QStringLiteral("assets_index_name"), assetsIndexName},
        {QStringLiteral("auth_uuid"), options.account.uuid},
        {QStringLiteral("auth_access_token"), options.account.accessToken},
        {QStringLiteral("user_type"), options.account.userType.isEmpty()
                                            ? QStringLiteral("msa") : options.account.userType},
        {QStringLiteral("version_type"), version.value(QStringLiteral("type")).toString(
                                               QStringLiteral("release"))},
        {QStringLiteral("clientid"), QStringLiteral("MCLauncher")},
        {QStringLiteral("auth_xuid"), options.account.xuid},
        {QStringLiteral("user_properties"), QStringLiteral("{}")},
        {QStringLiteral("launcher_name"), QStringLiteral("MCLauncher")},
        {QStringLiteral("launcher_version"), QStringLiteral("0.1.0")},
        {QStringLiteral("natives_directory"), nativeDirectory},
        {QStringLiteral("library_directory"), Paths::librariesDir(gameDirectory)},
        {QStringLiteral("classpath_separator"), QString(QDir::listSeparator())},
        {QStringLiteral("classpath"), classpathValue},
        {QStringLiteral("resolution_width"), QString::number(options.width)},
        {QStringLiteral("resolution_height"), QString::number(options.height)},
        {QStringLiteral("quickPlayPath"), QString()}
    };

    QStringList arguments;
    const QJsonObject argumentObject = version.value(QStringLiteral("arguments")).toObject();
    if (!argumentObject.isEmpty()) {
        arguments = parseArguments(argumentObject.value(QStringLiteral("jvm")), features, variables);
        if (!hasClassPathArgument(arguments)) {
            arguments << QStringLiteral("-cp") << classpathValue;
        }

        bool hasNativePath = false;
        for (const QString &argument : arguments) {
            if (argument.startsWith(QStringLiteral("-Djava.library.path="))) {
                hasNativePath = true;
                break;
            }
        }
        if (!hasNativePath) {
            arguments << QStringLiteral("-Djava.library.path=%1").arg(nativeDirectory);
        }

        arguments << QStringLiteral("-Xms%1M").arg(qMax(256, options.minimumMemoryMb));
        arguments << QStringLiteral("-Xmx%1M").arg(qMax(options.minimumMemoryMb, options.maximumMemoryMb));
        arguments << version.value(QStringLiteral("mainClass")).toString(
            QStringLiteral("net.minecraft.client.main.Main"));
        arguments += parseArguments(argumentObject.value(QStringLiteral("game")), features, variables);
    } else {
        arguments << QStringLiteral("-Xms%1M").arg(qMax(256, options.minimumMemoryMb));
        arguments << QStringLiteral("-Xmx%1M").arg(qMax(options.minimumMemoryMb, options.maximumMemoryMb));
        arguments << QStringLiteral("-Djava.library.path=%1").arg(nativeDirectory);
        arguments << QStringLiteral("-cp") << classpathValue;
        arguments << version.value(QStringLiteral("mainClass")).toString(
            QStringLiteral("net.minecraft.client.main.Main"));

        QString oldArguments = version.value(QStringLiteral("minecraftArguments")).toString();
        if (oldArguments.isEmpty()) {
            oldArguments = QStringLiteral("--username ${auth_player_name} --version ${version_name} "
                                         "--gameDir ${game_directory} --assetsDir ${assets_root} "
                                         "--assetIndex ${assets_index_name} --uuid ${auth_uuid} "
                                         "--accessToken ${auth_access_token} --userType ${user_type}");
        }
        for (const QString &token : splitCommandLine(oldArguments)) {
            arguments.append(replaceVariables(token, variables));
        }
    }

    if (options.connectToServer && !options.serverAddress.trimmed().isEmpty()) {
        arguments << QStringLiteral("--server") << options.serverAddress.trimmed()
                  << QStringLiteral("--port") << QString::number(qBound(1, options.serverPort, 65535));
    }

    spec.ok = true;
    spec.program = java;
    spec.arguments = arguments;
    spec.workingDirectory = gameDirectory;
    return spec;
}

} // namespace mcl
