#pragma once

#include "common.h"
#include "skin.h"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPropertyAnimation;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTextBrowser;
class QThread;
class QLabel;

namespace mcl {

class Authenticator;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void loadCatalog();
    void catalogLoaded(const QByteArray &manifestJson);
    void catalogFailed(const QString &message);
    void showDeviceCode(const QString &message,
                        const QString &userCode,
                        const QString &verificationUrl);
    void authenticated(const mcl::Account &account);
    void loggedOut();
    void authFailed(const QString &message);
    void installSelectedVersion();
    void installFabric();
    void installForge();
    void installQuilt();
    void fabricProgress(qint64 current, qint64 total, const QString &label);
    void fabricFinished(const QString &profileId, const QString &profileJsonPath);
    void fabricFailed(const QString &message);
    void forgeProgress(qint64 current, qint64 total, const QString &label);
    void forgeFinished(const QString &profileId, const QString &profileJsonPath);
    void forgeFailed(const QString &message);
    void quiltProgress(qint64 current, qint64 total, const QString &label);
    void quiltFinished(const QString &profileId, const QString &profileJsonPath);
    void quiltFailed(const QString &message);
    void loadNews();
    void newsLoaded(const QString &html);
    void newsFailed(const QString &message);
    void searchModrinth();
    void modrinthSearchLoaded(const QByteArray &json);
    void modrinthSearchFailed(const QString &message);
    void installSelectedModrinth();
    void modrinthInstallProgress(qint64 current, qint64 total, const QString &label);
    void modrinthInstallFinished(const QString &projectId);
    void modrinthInstallFailed(const QString &message);
    void browseSkinFile();
    void uploadSkin();
    void resetSkin();
    void skinFinished(const QString &message);
    void skinFailed(const QString &message);
    void installProgress(qint64 current, qint64 total, const QString &label);
    void installFinished(const QString &versionId, const QString &versionJsonPath);
    void installFailed(const QString &message);
    void launchSelectedVersion();
    void gameOutput();
    void gameErrorOutput();
    void gameFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void gameError(QProcess::ProcessError error);
    void loginClicked();
    void browseGameDirectory();
    void browseJava();
    void openGameDirectory();
    void openModsDirectory();
    void refreshMods();
    void refreshCrashReports();
    void showCrashReport(int row);
    void openCrashReportsDirectory();
    void serverSelectionChanged(int row);
    void addOrUpdateServer();
    void removeServer();
    void useSelectedServer();
    void instanceChanged(int index);
    void addInstance();
    void removeInstance();

private:
    void buildUi();
    void restoreSettings();
    void saveSettings();
    void loadInstances();
    void saveInstances() const;
    void loadServers();
    void saveServers() const;
    void syncCurrentInstance();
    void appendLog(const QString &message);
    void setBusy(bool busy);
    QString selectedVersionId() const;
    QString selectedVersionUrl() const;
    bool isVersionInstalled(const QString &versionId) const;
    void updatePlayButton();
    void loadLocalProfiles();
    void runSkinOperation(SkinOperation operation);

    Authenticator *authenticator_ = nullptr;
    QPointer<QThread> catalogThread_;
    QPointer<QThread> installThread_;
    QPointer<QThread> fabricThread_;
    QPointer<QThread> forgeThread_;
    QPointer<QThread> quiltThread_;
    QPointer<QThread> newsThread_;
    QPointer<QThread> modrinthSearchThread_;
    QPointer<QThread> modrinthInstallThread_;
    QPointer<QThread> skinThread_;
    QPointer<QPropertyAnimation> tabAnimation_;
    QProcess *gameProcess_ = nullptr;

    QTabWidget *tabs_ = nullptr;
    QComboBox *instanceCombo_ = nullptr;
    QComboBox *versionCombo_ = nullptr;
    QLineEdit *gameDirectoryEdit_ = nullptr;
    QLineEdit *javaEdit_ = nullptr;
    QSpinBox *minimumMemorySpin_ = nullptr;
    QSpinBox *maximumMemorySpin_ = nullptr;
    QCheckBox *customResolutionCheck_ = nullptr;
    QSpinBox *widthSpin_ = nullptr;
    QSpinBox *heightSpin_ = nullptr;
    QListWidget *modsList_ = nullptr;
    QLineEdit *modrinthSearchEdit_ = nullptr;
    QComboBox *modSourceCombo_ = nullptr;
    QComboBox *modrinthTypeCombo_ = nullptr;
    QComboBox *modrinthLoaderCombo_ = nullptr;
    QLineEdit *curseForgeApiKeyEdit_ = nullptr;
    QListWidget *modrinthResults_ = nullptr;
    QListWidget *serversList_ = nullptr;
    QLineEdit *serverNameEdit_ = nullptr;
    QLineEdit *serverAddressBookEdit_ = nullptr;
    QSpinBox *serverPortBookSpin_ = nullptr;
    QListWidget *crashReportsList_ = nullptr;
    QPlainTextEdit *crashReportEdit_ = nullptr;
    QLineEdit *skinFileEdit_ = nullptr;
    QComboBox *skinVariantCombo_ = nullptr;
    QPlainTextEdit *logEdit_ = nullptr;
    QProgressBar *progressBar_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *accountLabel_ = nullptr;
    QPushButton *loginButton_ = nullptr;
    QPushButton *installButton_ = nullptr;
    QPushButton *fabricButton_ = nullptr;
    QPushButton *forgeButton_ = nullptr;
    QPushButton *quiltButton_ = nullptr;
    QPushButton *playButton_ = nullptr;
    QCheckBox *connectServerCheck_ = nullptr;
    QLineEdit *serverAddressEdit_ = nullptr;
    QSpinBox *serverPortSpin_ = nullptr;
    QTextBrowser *newsBrowser_ = nullptr;

    QHash<QString, QString> versionUrls_;
    QStringList instanceNames_;
    QStringList instanceDirectories_;
    QStringList serverNames_;
    QStringList serverAddresses_;
    QList<int> serverPorts_;
    QStringList crashReportPaths_;
    int activeInstanceIndex_ = -1;
    QString installedVersion_;
};

} // namespace mcl
