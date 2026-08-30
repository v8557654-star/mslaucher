#pragma once

#include <QDir>
#include <QString>

namespace mcl::Paths {

QString launcherRoot();
QString defaultGameDir();
QString versionDir(const QString &gameDir, const QString &versionId);
QString librariesDir(const QString &gameDir);
QString assetsDir(const QString &gameDir);
QString assetIndexesDir(const QString &gameDir);
QString assetObjectsDir(const QString &gameDir);
QString nativesDir(const QString &gameDir, const QString &versionId);
QString launcherCacheDir();

bool ensureGameLayout(const QString &gameDir, QString *error = nullptr);

} // namespace mcl::Paths
