#pragma once

#include "common.h"

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace mcl {

struct LaunchOptions {
    QString gameDir;
    QString versionId;
    QString javaPath;
    Account account;
    int minimumMemoryMb = 1024;
    int maximumMemoryMb = 4096;
    bool customResolution = false;
    int width = 1280;
    int height = 720;
    bool connectToServer = false;
    QString serverAddress;
    int serverPort = 25565;
};

struct LaunchSpec {
    bool ok = false;
    QString error;
    QString program;
    QStringList arguments;
    QString workingDirectory;
    int javaMajor = 0;
    int requiredJavaMajor = 0;
};

class LauncherBuilder final {
public:
    static LaunchSpec build(const LaunchOptions &options);
    static QString findJava(const QString &configuredPath = {}, int requiredMajor = 0);
    static int javaMajorVersion(const QString &javaPath);
};

} // namespace mcl
