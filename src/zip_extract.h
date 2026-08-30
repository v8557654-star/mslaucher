#pragma once

#include <QString>
#include <QStringList>

namespace mcl {

bool extractZip(const QString &archivePath,
                const QString &destination,
                const QStringList &excludedPrefixes = {},
                QString *error = nullptr);

} // namespace mcl
