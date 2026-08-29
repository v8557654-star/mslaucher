#include "zip_extract.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <cstring>

#include "miniz.h"

namespace mcl {

bool extractZip(const QString &archivePath,
                const QString &destination,
                const QStringList &excludedPrefixes,
                QString *error)
{
    mz_zip_archive archive;
    std::memset(&archive, 0, sizeof(archive));

    const QByteArray archiveName = archivePath.toUtf8();
    if (!mz_zip_reader_init_file(&archive, archiveName.constData(), 0)) {
        if (error) {
            *error = QStringLiteral("Не удалось открыть ZIP-архив %1").arg(archivePath);
        }
        return false;
    }

    const mz_uint count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&archive, index, &stat)) {
            mz_zip_reader_end(&archive);
            if (error) {
                *error = QStringLiteral("Не удалось прочитать запись ZIP-архива %1").arg(archivePath);
            }
            return false;
        }

        const QString entry = QString::fromUtf8(stat.m_filename);
        if (stat.m_is_directory || entry.endsWith(QLatin1Char('/'))) {
            continue;
        }

        const QString cleanEntry = QDir::cleanPath(entry);
        if (cleanEntry.startsWith(QStringLiteral("../"))
            || cleanEntry.contains(QStringLiteral("/../"))
            || QDir::isAbsolutePath(cleanEntry)) {
            continue;
        }

        bool excluded = false;
        for (const QString &prefix : excludedPrefixes) {
            if (cleanEntry.startsWith(prefix)) {
                excluded = true;
                break;
            }
        }
        if (excluded || cleanEntry.startsWith(QStringLiteral("META-INF/"))) {
            continue;
        }

        const QString output = QDir(destination).filePath(cleanEntry);
        if (!QDir().mkpath(QFileInfo(output).absolutePath())) {
            mz_zip_reader_end(&archive);
            if (error) {
                *error = QStringLiteral("Не удалось создать каталог для %1").arg(output);
            }
            return false;
        }

        const QByteArray outputName = output.toUtf8();
        if (!mz_zip_reader_extract_to_file(&archive, index, outputName.constData(), 0)) {
            mz_zip_reader_end(&archive);
            if (error) {
                *error = QStringLiteral("Не удалось распаковать %1").arg(entry);
            }
            return false;
        }
    }

    mz_zip_reader_end(&archive);
    return true;
}

} // namespace mcl
