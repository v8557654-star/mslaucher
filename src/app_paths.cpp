#include "app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace mcl::Paths {

QString launcherRoot()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) {
        root = QDir::homePath() + QStringLiteral("/.mclauncher");
    }
    return QDir::cleanPath(root);
}

QString defaultGameDir()
{
#ifdef Q_OS_WIN
    const QString roaming = qEnvironmentVariable("APPDATA");
    if (!roaming.isEmpty()) {
        return QDir::cleanPath(QDir(roaming).filePath(QStringLiteral(".minecraft")));
    }
    return QDir::cleanPath(QDir::homePath() + QStringLiteral("/AppData/Roaming/.minecraft"));
#elif defined(Q_OS_MACOS)
    return QDir::cleanPath(QDir::homePath()
                           + QStringLiteral("/Library/Application Support/minecraft"));
#else
    return QDir::cleanPath(QDir::homePath() + QStringLiteral("/.minecraft"));
#endif
}

QString versionDir(const QString &gameDir, const QString &versionId)
{
    return QDir(gameDir).filePath(QStringLiteral("versions/%1").arg(versionId));
}

QString librariesDir(const QString &gameDir)
{
    return QDir(gameDir).filePath(QStringLiteral("libraries"));
}

QString assetsDir(const QString &gameDir)
{
    return QDir(gameDir).filePath(QStringLiteral("assets"));
}

QString assetIndexesDir(const QString &gameDir)
{
    return QDir(assetsDir(gameDir)).filePath(QStringLiteral("indexes"));
}

QString assetObjectsDir(const QString &gameDir)
{
    return QDir(assetsDir(gameDir)).filePath(QStringLiteral("objects"));
}

QString nativesDir(const QString &gameDir, const QString &versionId)
{
    return QDir(versionDir(gameDir, versionId)).filePath(QStringLiteral("natives"));
}

QString launcherCacheDir()
{
    return QDir(launcherRoot()).filePath(QStringLiteral("cache"));
}

bool ensureGameLayout(const QString &gameDir, QString *error)
{
    const QStringList directories {
        gameDir,
        QDir(gameDir).filePath(QStringLiteral("versions")),
        librariesDir(gameDir),
        assetsDir(gameDir),
        assetIndexesDir(gameDir),
        assetObjectsDir(gameDir),
        QDir(gameDir).filePath(QStringLiteral("mods")),
        QDir(gameDir).filePath(QStringLiteral("resourcepacks")),
        QDir(gameDir).filePath(QStringLiteral("shaderpacks")),
        launcherRoot(),
        launcherCacheDir()
    };

    for (const QString &directory : directories) {
        if (!QDir().mkpath(directory)) {
            if (error) {
                *error = QStringLiteral("Не удалось создать каталог %1").arg(directory);
            }
            return false;
        }
    }
    return true;
}

} // namespace mcl::Paths
