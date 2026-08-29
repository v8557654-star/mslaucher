#include "main_window.h"

#include "app_paths.h"
#include "auth.h"
#include "curseforge.h"
#include "launcher.h"
#include "minecraft.h"
#include "modrinth.h"
#include "skin.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedLayout>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mcl {

namespace {

class AnimatedBackgroundWidget final : public QWidget {
public:
    explicit AnimatedBackgroundWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setPhase(qreal phase)
    {
        phase_ = phase;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal width = qMax(1, this->width());
        const qreal height = qMax(1, this->height());
        const qreal tau = 6.28318530717958647692;
        const qreal angle = phase_ * tau;

        QLinearGradient base(0, 0, width, height);
        base.setColorAt(0.0, QColor(QStringLiteral("#0c1220")));
        base.setColorAt(0.52, QColor(QStringLiteral("#0b111c")));
        base.setColorAt(1.0, QColor(QStringLiteral("#071018")));
        painter.fillRect(rect(), base);

        const QPointF first(width * (0.23 + 0.10 * std::sin(angle)),
                            height * (0.22 + 0.08 * std::cos(angle * 0.73)));
        const QPointF second(width * (0.82 + 0.09 * std::cos(angle * 0.67)),
                             height * (0.38 + 0.11 * std::sin(angle * 0.61)));
        const QPointF third(width * (0.48 + 0.12 * std::sin(angle * 0.47)),
                            height * (0.94 + 0.04 * std::cos(angle * 0.83)));

        auto paintGlow = [&painter](const QPointF &center, qreal radius, const QColor &color) {
            QRadialGradient glow(center, radius);
            QColor inner = color;
            inner.setAlpha(48);
            QColor outer = color;
            outer.setAlpha(0);
            glow.setColorAt(0.0, inner);
            glow.setColorAt(1.0, outer);
            painter.fillRect(painter.viewport(), glow);
        };
        paintGlow(first, width * 0.44, QColor(QStringLiteral("#2e75bd")));
        paintGlow(second, width * 0.34, QColor(QStringLiteral("#4961c7")));
        paintGlow(third, width * 0.40, QColor(QStringLiteral("#2b9d91")));

        painter.setPen(QPen(QColor(126, 169, 214, 13), 1.0));
        const int gridSize = 42;
        const int offsetX = static_cast<int>(std::sin(angle * 0.18) * 16.0);
        const int offsetY = static_cast<int>(std::cos(angle * 0.16) * 16.0);
        for (int x = -gridSize + offsetX; x < width; x += gridSize) {
            painter.drawLine(x, 0, x, static_cast<int>(height));
        }
        for (int y = -gridSize + offsetY; y < height; y += gridSize) {
            painter.drawLine(0, y, static_cast<int>(width), y);
        }

        QLinearGradient vignette(0, 0, 0, height);
        vignette.setColorAt(0.0, QColor(0, 0, 0, 35));
        vignette.setColorAt(0.48, QColor(0, 0, 0, 0));
        vignette.setColorAt(1.0, QColor(0, 0, 0, 78));
        painter.fillRect(rect(), vignette);
    }

private:
    qreal phase_ = 0.0;
};

class SmoothTabBar final : public QTabBar {
public:
    explicit SmoothTabBar(QWidget *parent = nullptr)
        : QTabBar(parent)
    {
        setExpanding(false);
        animation_.setDuration(360);
        animation_.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    indicator_ = value.toRectF();
                    update();
                });
        connect(this, &QTabBar::currentChanged, this,
                [this](int index) { animateIndicator(index); });
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QTabBar::paintEvent(event);
        if (indicator_.isNull()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QRectF glow = indicator_.adjusted(0, -2, 0, 2);
        painter.setPen(QPen(QColor(91, 156, 255, 55), 4.0));
        painter.drawRoundedRect(glow, 2.0, 2.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#65a7ff")));
        painter.drawRoundedRect(indicator_, 1.5, 1.5);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QTabBar::resizeEvent(event);
        const QRectF target = targetRect(currentIndex());
        if (animation_.state() != QAbstractAnimation::Running || indicator_.isNull()) {
            indicator_ = target;
        }
        update();
    }

private:
    QRectF targetRect(int index) const
    {
        if (index < 0 || index >= count()) {
            return {};
        }
        const QRect tab = tabRect(index);
        return QRectF(tab.left() + 8.0, height() - 3.0,
                      qMax(8.0, tab.width() - 16.0), 2.0);
    }

    void animateIndicator(int index)
    {
        const QRectF target = targetRect(index);
        if (target.isNull()) {
            return;
        }
        if (indicator_.isNull()) {
            indicator_ = target;
            update();
            return;
        }
        animation_.stop();
        animation_.setStartValue(indicator_);
        animation_.setEndValue(target);
        animation_.start();
    }

    QVariantAnimation animation_;
    QRectF indicator_;
};

class SmoothTabWidget final : public QTabWidget {
public:
    explicit SmoothTabWidget(QWidget *parent = nullptr)
        : QTabWidget(parent)
    {
    }

    void installSmoothTabBar()
    {
        setTabBar(new SmoothTabBar(this));
    }
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), authenticator_(new Authenticator(this))
{
    setWindowTitle(QStringLiteral("MCLauncher — Minecraft Java Edition"));
    resize(980, 720);
    setMinimumSize(800, 600);
    buildUi();
    restoreSettings();

    connect(authenticator_, &Authenticator::statusChanged, this,
            [this](const QString &message) { statusLabel_->setText(message); });
    connect(authenticator_, &Authenticator::deviceCodeReady, this, &MainWindow::showDeviceCode);
    connect(authenticator_, &Authenticator::authenticated, this, &MainWindow::authenticated);
    connect(authenticator_, &Authenticator::loggedOut, this, &MainWindow::loggedOut);
    connect(authenticator_, &Authenticator::failed, this, &MainWindow::authFailed);

    QTimer::singleShot(0, this, [this]() {
        loadCatalog();
        loadNews();
        if (authenticator_->hasSavedSession()) {
            authenticator_->login();
        }
    });
}

MainWindow::~MainWindow()
{
    if (gameProcess_ && gameProcess_->state() != QProcess::NotRunning) {
        gameProcess_->terminate();
        if (!gameProcess_->waitForFinished(1500)) {
            gameProcess_->kill();
        }
    }
    if (catalogThread_ && catalogThread_->isRunning()) {
        catalogThread_->quit();
        catalogThread_->wait(5000);
    }
    if (installThread_ && installThread_->isRunning()) {
        installThread_->quit();
        installThread_->wait(5000);
    }
    if (fabricThread_ && fabricThread_->isRunning()) {
        fabricThread_->quit();
        fabricThread_->wait(5000);
    }
    if (forgeThread_ && forgeThread_->isRunning()) {
        forgeThread_->quit();
        forgeThread_->wait(5000);
    }
    if (quiltThread_ && quiltThread_->isRunning()) {
        quiltThread_->quit();
        quiltThread_->wait(5000);
    }
    if (newsThread_ && newsThread_->isRunning()) {
        newsThread_->quit();
        newsThread_->wait(5000);
    }
    if (modrinthSearchThread_ && modrinthSearchThread_->isRunning()) {
        modrinthSearchThread_->quit();
        modrinthSearchThread_->wait(5000);
    }
    if (modrinthInstallThread_ && modrinthInstallThread_->isRunning()) {
        modrinthInstallThread_->quit();
        modrinthInstallThread_->wait(5000);
    }
    if (skinThread_ && skinThread_->isRunning()) {
        skinThread_->quit();
        skinThread_->wait(5000);
    }
}

void MainWindow::buildUi()
{
    setStyleSheet(QString::fromUtf8(R"(
        QMainWindow { background: #10141d; color: #eef3fb; }
        QWidget { color: #eef3fb; font-size: 10pt; }
        QGroupBox { border: 1px solid #2b3547; border-radius: 9px; margin-top: 13px; padding: 12px; background: #151b26; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #9fb6d8; }
        QLineEdit, QComboBox, QSpinBox { background: #0e131c; border: 1px solid #34435a; border-radius: 6px; padding: 7px; selection-background-color: #3b82f6; }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid #5b9cff; }
        QPushButton { background: #26344b; border: 1px solid #3d5271; border-radius: 7px; padding: 8px 14px; }
        QPushButton:hover { background: #314765; }
        QPushButton:pressed { background: #1e2a3d; }
        QPushButton:disabled { color: #6e7c91; background: #1a2230; border-color: #273244; }
        QPushButton#primary { background: #2f83f7; border-color: #5d9fff; font-weight: bold; }
        QPushButton#primary:hover { background: #4b94f8; }
        QTabWidget::pane { border: 1px solid #2b3547; border-radius: 8px; background: #121822; }
        QTabBar::tab { background: #151b26; border: 1px solid #2b3547; padding: 8px 18px; margin-right: 3px; border-top-left-radius: 6px; border-top-right-radius: 6px; }
        QTabBar::tab:selected { background: #26344b; color: #ffffff; }
        QPlainTextEdit, QListWidget { background: #0b1017; border: 1px solid #2b3547; border-radius: 7px; }
        QProgressBar { border: 1px solid #2b3547; border-radius: 5px; background: #0c1119; text-align: center; }
        QProgressBar::chunk { background: #2f83f7; border-radius: 4px; }
        QCheckBox::indicator { width: 16px; height: 16px; }
    )"));

    auto *central = new QWidget(this);
    auto *stack = new QStackedLayout(central);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setStackingMode(QStackedLayout::StackAll);
    auto *animatedBackground = new AnimatedBackgroundWidget(central);
    stack->addWidget(animatedBackground);
    auto *content = new QWidget(central);
    content->setAttribute(Qt::WA_TranslucentBackground);
    stack->addWidget(content);
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(12);

    auto *backgroundAnimation = new QVariantAnimation(animatedBackground);
    backgroundAnimation->setStartValue(0.0);
    backgroundAnimation->setEndValue(1.0);
    backgroundAnimation->setDuration(24000);
    backgroundAnimation->setLoopCount(-1);
    backgroundAnimation->setEasingCurve(QEasingCurve::Linear);
    connect(backgroundAnimation, &QVariantAnimation::valueChanged,
            animatedBackground, [animatedBackground](const QVariant &value) {
                animatedBackground->setPhase(value.toReal());
            });
    backgroundAnimation->start();

    auto *headerLayout = new QHBoxLayout;
    auto *titleLayout = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("MCLauncher"));
    title->setStyleSheet(QStringLiteral("font-size: 24pt; font-weight: 700; color: #ffffff;"));
    auto *subtitle = new QLabel(QStringLiteral("Minecraft Java Edition • официальный аккаунт Microsoft"));
    subtitle->setStyleSheet(QStringLiteral("color: #94a6c2;"));
    titleLayout->addWidget(title);
    titleLayout->addWidget(subtitle);
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();
    accountLabel_ = new QLabel(QStringLiteral("Вход не выполнен"));
    accountLabel_->setStyleSheet(QStringLiteral("color: #b6c6dd; padding-right: 8px;"));
    headerLayout->addWidget(accountLabel_);
    loginButton_ = new QPushButton(QStringLiteral("Войти через Microsoft"));
    headerLayout->addWidget(loginButton_);
    root->addLayout(headerLayout);
    connect(loginButton_, &QPushButton::clicked, this, &MainWindow::loginClicked);

    auto *tabs = new SmoothTabWidget(this);
    tabs->installSmoothTabBar();
    tabs_ = tabs;
    root->addWidget(tabs, 1);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (!tabs_ || index < 0) {
            return;
        }
        QWidget *page = tabs_->widget(index);
        if (!page) {
            return;
        }
        auto *effect = qobject_cast<QGraphicsOpacityEffect *>(page->graphicsEffect());
        if (!effect) {
            effect = new QGraphicsOpacityEffect(page);
            page->setGraphicsEffect(effect);
        }
        if (tabAnimation_) {
            tabAnimation_->stop();
        }
        effect->setOpacity(0.78);
        auto *animation = new QPropertyAnimation(effect, "opacity", effect);
        animation->setDuration(360);
        animation->setStartValue(0.78);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        tabAnimation_ = animation;
        connect(animation, &QPropertyAnimation::finished, this, [this, animation]() {
            if (tabAnimation_ == animation) {
                tabAnimation_ = nullptr;
            }
        });
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    });

    auto *playTab = new QWidget;
    auto *playLayout = new QVBoxLayout(playTab);
    playLayout->setContentsMargins(12, 12, 12, 12);

    auto *versionGroup = new QGroupBox(QStringLiteral("Инстанс, версия и файлы"));
    auto *versionForm = new QFormLayout(versionGroup);
    versionForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *instanceRow = new QWidget;
    auto *instanceLayout = new QHBoxLayout(instanceRow);
    instanceLayout->setContentsMargins(0, 0, 0, 0);
    instanceCombo_ = new QComboBox;
    auto *addInstanceButton = new QPushButton(QStringLiteral("+"));
    addInstanceButton->setToolTip(QStringLiteral("Создать отдельный инстанс"));
    auto *removeInstanceButton = new QPushButton(QStringLiteral("−"));
    removeInstanceButton->setToolTip(QStringLiteral("Удалить выбранный инстанс"));
    instanceLayout->addWidget(instanceCombo_, 1);
    instanceLayout->addWidget(addInstanceButton);
    instanceLayout->addWidget(removeInstanceButton);
    versionForm->addRow(QStringLiteral("Инстанс:"), instanceRow);
    connect(instanceCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::instanceChanged);
    connect(addInstanceButton, &QPushButton::clicked, this, &MainWindow::addInstance);
    connect(removeInstanceButton, &QPushButton::clicked, this, &MainWindow::removeInstance);

    auto *versionRow = new QWidget;
    auto *versionRowLayout = new QHBoxLayout(versionRow);
    versionRowLayout->setContentsMargins(0, 0, 0, 0);
    versionCombo_ = new QComboBox;
    versionCombo_->setMinimumWidth(270);
    versionCombo_->addItem(QStringLiteral("Загрузка списка версий…"));
    versionCombo_->setEnabled(false);
    connect(versionCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updatePlayButton();
        if (!installThread_ && !fabricThread_ && !forgeThread_ && !modrinthInstallThread_) {
            fabricButton_->setEnabled(!selectedVersionUrl().isEmpty());
            forgeButton_->setEnabled(!selectedVersionUrl().isEmpty());
            quiltButton_->setEnabled(!selectedVersionUrl().isEmpty());
        }
    });
    fabricButton_ = new QPushButton(QStringLiteral("Fabric"));
    fabricButton_->setEnabled(false);
    fabricButton_->setToolTip(QStringLiteral("Установить последний стабильный Fabric Loader для выбранной версии"));
    forgeButton_ = new QPushButton(QStringLiteral("Forge"));
    forgeButton_->setEnabled(false);
    forgeButton_->setToolTip(QStringLiteral("Установить последний Forge через официальный installer"));
    quiltButton_ = new QPushButton(QStringLiteral("Quilt"));
    quiltButton_->setEnabled(false);
    quiltButton_->setToolTip(QStringLiteral("Установить последний Quilt Loader через Quilt Meta"));
    auto *refreshVersionsButton = new QPushButton(QStringLiteral("Обновить"));
    versionRowLayout->addWidget(versionCombo_, 1);
    versionRowLayout->addWidget(fabricButton_);
    versionRowLayout->addWidget(forgeButton_);
    versionRowLayout->addWidget(quiltButton_);
    versionRowLayout->addWidget(refreshVersionsButton);
    versionForm->addRow(QStringLiteral("Версия:"), versionRow);
    connect(fabricButton_, &QPushButton::clicked, this, &MainWindow::installFabric);
    connect(forgeButton_, &QPushButton::clicked, this, &MainWindow::installForge);
    connect(quiltButton_, &QPushButton::clicked, this, &MainWindow::installQuilt);
    connect(refreshVersionsButton, &QPushButton::clicked, this, &MainWindow::loadCatalog);

    auto *gameDirRow = new QWidget;
    auto *gameDirLayout = new QHBoxLayout(gameDirRow);
    gameDirLayout->setContentsMargins(0, 0, 0, 0);
    gameDirectoryEdit_ = new QLineEdit;
    auto *browseGameButton = new QPushButton(QStringLiteral("Обзор…"));
    gameDirLayout->addWidget(gameDirectoryEdit_, 1);
    gameDirLayout->addWidget(browseGameButton);
    versionForm->addRow(QStringLiteral("Папка игры:"), gameDirRow);
    connect(browseGameButton, &QPushButton::clicked, this, &MainWindow::browseGameDirectory);

    playLayout->addWidget(versionGroup);

    auto *settingsGroup = new QGroupBox(QStringLiteral("Параметры запуска"));
    auto *settingsForm = new QFormLayout(settingsGroup);
    settingsForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *javaRow = new QWidget;
    auto *javaLayout = new QHBoxLayout(javaRow);
    javaLayout->setContentsMargins(0, 0, 0, 0);
    javaEdit_ = new QLineEdit;
    javaEdit_->setPlaceholderText(QStringLiteral("Авто: Java под выбранную версию Minecraft"));
    javaEdit_->setToolTip(QStringLiteral(
        "Оставьте пустым для автоматического выбора Java, совместимой с выбранной версией Minecraft."));
    auto *browseJavaButton = new QPushButton(QStringLiteral("Обзор…"));
    javaLayout->addWidget(javaEdit_, 1);
    javaLayout->addWidget(browseJavaButton);
    settingsForm->addRow(QStringLiteral("Java:"), javaRow);
    auto *javaHint = new QLabel(QStringLiteral(
        "Автоматический режим проверяет javaVersion выбранного профиля и выбирает JDK с нужной major-версией. Несовместимая Java не используется."));
    javaHint->setWordWrap(true);
    javaHint->setStyleSheet(QStringLiteral("color: #7f96b4; font-size: 9pt;"));
    settingsForm->addRow(QString(), javaHint);
    connect(browseJavaButton, &QPushButton::clicked, this, &MainWindow::browseJava);

    auto *memoryRow = new QWidget;
    auto *memoryLayout = new QHBoxLayout(memoryRow);
    memoryLayout->setContentsMargins(0, 0, 0, 0);
    minimumMemorySpin_ = new QSpinBox;
    maximumMemorySpin_ = new QSpinBox;
    minimumMemorySpin_->setRange(256, 32768);
    maximumMemorySpin_->setRange(512, 65536);
    minimumMemorySpin_->setSingleStep(256);
    maximumMemorySpin_->setSingleStep(512);
    minimumMemorySpin_->setSuffix(QStringLiteral(" МБ"));
    maximumMemorySpin_->setSuffix(QStringLiteral(" МБ"));
    memoryLayout->addWidget(new QLabel(QStringLiteral("от")));
    memoryLayout->addWidget(minimumMemorySpin_);
    memoryLayout->addWidget(new QLabel(QStringLiteral("до")));
    memoryLayout->addWidget(maximumMemorySpin_);
    memoryLayout->addStretch();
    settingsForm->addRow(QStringLiteral("Память:"), memoryRow);

    auto *resolutionRow = new QWidget;
    auto *resolutionLayout = new QHBoxLayout(resolutionRow);
    resolutionLayout->setContentsMargins(0, 0, 0, 0);
    customResolutionCheck_ = new QCheckBox(QStringLiteral("Задать разрешение"));
    widthSpin_ = new QSpinBox;
    heightSpin_ = new QSpinBox;
    widthSpin_->setRange(640, 7680);
    heightSpin_->setRange(480, 4320);
    widthSpin_->setSuffix(QStringLiteral(" px"));
    heightSpin_->setSuffix(QStringLiteral(" px"));
    resolutionLayout->addWidget(customResolutionCheck_);
    resolutionLayout->addWidget(widthSpin_);
    resolutionLayout->addWidget(new QLabel(QStringLiteral("×")));
    resolutionLayout->addWidget(heightSpin_);
    resolutionLayout->addStretch();
    settingsForm->addRow(QStringLiteral("Окно:"), resolutionRow);
    connect(customResolutionCheck_, &QCheckBox::toggled, widthSpin_, &QWidget::setEnabled);
    connect(customResolutionCheck_, &QCheckBox::toggled, heightSpin_, &QWidget::setEnabled);

    auto *serverRow = new QWidget;
    auto *serverLayout = new QHBoxLayout(serverRow);
    serverLayout->setContentsMargins(0, 0, 0, 0);
    connectServerCheck_ = new QCheckBox(QStringLiteral("Быстро подключиться"));
    serverAddressEdit_ = new QLineEdit;
    serverAddressEdit_->setPlaceholderText(QStringLiteral("адрес сервера"));
    serverPortSpin_ = new QSpinBox;
    serverPortSpin_->setRange(1, 65535);
    serverPortSpin_->setValue(25565);
    serverPortSpin_->setSuffix(QStringLiteral(" порт"));
    serverLayout->addWidget(connectServerCheck_);
    serverLayout->addWidget(serverAddressEdit_, 1);
    serverLayout->addWidget(serverPortSpin_);
    settingsForm->addRow(QStringLiteral("Сервер:"), serverRow);

    playLayout->addWidget(settingsGroup);

    auto *buttonsLayout = new QHBoxLayout;
    installButton_ = new QPushButton(QStringLiteral("Установить / обновить"));
    playButton_ = new QPushButton(QStringLiteral("Играть"));
    playButton_->setObjectName(QStringLiteral("primary"));
    buttonsLayout->addWidget(installButton_);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(playButton_);
    playLayout->addLayout(buttonsLayout);
    connect(installButton_, &QPushButton::clicked, this, &MainWindow::installSelectedVersion);
    connect(playButton_, &QPushButton::clicked, this, &MainWindow::launchSelectedVersion);

    auto *logGroup = new QGroupBox(QStringLiteral("Журнал"));
    auto *logLayout = new QVBoxLayout(logGroup);
    logEdit_ = new QPlainTextEdit;
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumBlockCount(4000);
    logLayout->addWidget(logEdit_);
    playLayout->addWidget(logGroup, 1);

    tabs->addTab(playTab, QStringLiteral("Играть"));

    auto *serversTab = new QWidget;
    auto *serversTabLayout = new QVBoxLayout(serversTab);
    auto *serversInfo = new QLabel(QStringLiteral(
        "Сохраняйте адреса серверов и быстро подставляйте выбранный сервер на вкладке «Играть». "
        "Адрес вводится без протокола, например play.example.net или 192.168.1.20."));
    serversInfo->setWordWrap(true);
    serversInfo->setStyleSheet(QStringLiteral("color: #9fb0c7;"));
    serversTabLayout->addWidget(serversInfo);

    auto *serverBookGroup = new QGroupBox(QStringLiteral("Адресная книга серверов"));
    auto *serverBookLayout = new QVBoxLayout(serverBookGroup);
    auto *serverBookForm = new QFormLayout;
    serverNameEdit_ = new QLineEdit;
    serverNameEdit_->setPlaceholderText(QStringLiteral("например: Выживание"));
    serverAddressBookEdit_ = new QLineEdit;
    serverAddressBookEdit_->setPlaceholderText(QStringLiteral("play.example.net"));
    serverPortBookSpin_ = new QSpinBox;
    serverPortBookSpin_->setRange(1, 65535);
    serverPortBookSpin_->setValue(25565);
    serverBookForm->addRow(QStringLiteral("Название:"), serverNameEdit_);
    serverBookForm->addRow(QStringLiteral("Адрес:"), serverAddressBookEdit_);
    serverBookForm->addRow(QStringLiteral("Порт:"), serverPortBookSpin_);
    serverBookLayout->addLayout(serverBookForm);

    serversList_ = new QListWidget;
    serversList_->setMinimumHeight(180);
    serverBookLayout->addWidget(serversList_, 1);
    auto *serverButtons = new QHBoxLayout;
    auto *addOrUpdateServerButton = new QPushButton(QStringLiteral("Добавить / обновить"));
    auto *removeServerButton = new QPushButton(QStringLiteral("Удалить"));
    auto *useServerButton = new QPushButton(QStringLiteral("Использовать для быстрого подключения"));
    serverButtons->addWidget(addOrUpdateServerButton);
    serverButtons->addWidget(removeServerButton);
    serverButtons->addStretch();
    serverButtons->addWidget(useServerButton);
    serverBookLayout->addLayout(serverButtons);
    serversTabLayout->addWidget(serverBookGroup, 1);
    connect(serversList_, &QListWidget::currentRowChanged,
            this, &MainWindow::serverSelectionChanged);
    connect(addOrUpdateServerButton, &QPushButton::clicked,
            this, &MainWindow::addOrUpdateServer);
    connect(removeServerButton, &QPushButton::clicked,
            this, &MainWindow::removeServer);
    connect(useServerButton, &QPushButton::clicked,
            this, &MainWindow::useSelectedServer);
    tabs->addTab(serversTab, QStringLiteral("Серверы"));

    auto *modsTab = new QWidget;
    auto *modsLayout = new QVBoxLayout(modsTab);
    auto *modsInfo = new QLabel(QStringLiteral(
        "Папка mods поддерживает .jar и .zip. Загрузчики Fabric/Forge устанавливаются отдельно, "
        "а этот лаунчер запускает их как обычные профили Minecraft."));
    modsInfo->setWordWrap(true);
    modsInfo->setStyleSheet(QStringLiteral("color: #9fb0c7;"));
    modsLayout->addWidget(modsInfo);
    modsList_ = new QListWidget;
    modsLayout->addWidget(modsList_, 1);
    auto *modsButtons = new QHBoxLayout;
    auto *refreshModsButton = new QPushButton(QStringLiteral("Обновить список"));
    auto *openModsButton = new QPushButton(QStringLiteral("Открыть mods"));
    auto *openGameButton = new QPushButton(QStringLiteral("Открыть папку игры"));
    modsButtons->addWidget(refreshModsButton);
    modsButtons->addWidget(openModsButton);
    modsButtons->addWidget(openGameButton);
    modsButtons->addStretch();
    modsLayout->addLayout(modsButtons);
    connect(refreshModsButton, &QPushButton::clicked, this, &MainWindow::refreshMods);
    connect(openModsButton, &QPushButton::clicked, this, &MainWindow::openModsDirectory);
    connect(openGameButton, &QPushButton::clicked, this, &MainWindow::openGameDirectory);

    auto *modrinthGroup = new QGroupBox(QStringLiteral("Поиск модов Modrinth / CurseForge"));
    auto *modrinthLayout = new QVBoxLayout(modrinthGroup);
    auto *modrinthSearchRow = new QHBoxLayout;
    modrinthSearchEdit_ = new QLineEdit;
    modrinthSearchEdit_->setPlaceholderText(QStringLiteral("например: sodium, minimap, jei"));
    modSourceCombo_ = new QComboBox;
    modSourceCombo_->addItem(QStringLiteral("Modrinth"), QStringLiteral("modrinth"));
    modSourceCombo_->addItem(QStringLiteral("CurseForge"), QStringLiteral("curseforge"));
    modrinthTypeCombo_ = new QComboBox;
    modrinthTypeCombo_->addItem(QStringLiteral("Моды"), QStringLiteral("mod"));
    modrinthTypeCombo_->addItem(QStringLiteral("Модпаки"), QStringLiteral("modpack"));
    modrinthLoaderCombo_ = new QComboBox;
    modrinthLoaderCombo_->addItems({QStringLiteral("fabric"), QStringLiteral("forge"),
                                    QStringLiteral("neoforge"), QStringLiteral("quilt"),
                                    QStringLiteral("vanilla")});
    auto *modrinthSearchButton = new QPushButton(QStringLiteral("Найти"));
    modrinthSearchRow->addWidget(modrinthSearchEdit_, 1);
    modrinthSearchRow->addWidget(modSourceCombo_);
    modrinthSearchRow->addWidget(modrinthTypeCombo_);
    modrinthSearchRow->addWidget(modrinthLoaderCombo_);
    modrinthSearchRow->addWidget(modrinthSearchButton);
    modrinthLayout->addLayout(modrinthSearchRow);
    auto *curseForgeKeyRow = new QHBoxLayout;
    auto *curseForgeKeyLabel = new QLabel(QStringLiteral("CurseForge API key:"));
    curseForgeApiKeyEdit_ = new QLineEdit;
    curseForgeApiKeyEdit_->setEchoMode(QLineEdit::Password);
    curseForgeApiKeyEdit_->setPlaceholderText(QStringLiteral("необязательно, можно задать через переменную окружения"));
    curseForgeKeyRow->addWidget(curseForgeKeyLabel);
    curseForgeKeyRow->addWidget(curseForgeApiKeyEdit_, 1);
    modrinthLayout->addLayout(curseForgeKeyRow);
    modrinthResults_ = new QListWidget;
    modrinthResults_->setMinimumHeight(130);
    modrinthLayout->addWidget(modrinthResults_);
    auto *modrinthInstallButton = new QPushButton(QStringLiteral("Установить выбранный мод"));
    modrinthLayout->addWidget(modrinthInstallButton, 0, Qt::AlignRight);
    connect(modrinthSearchButton, &QPushButton::clicked, this, &MainWindow::searchModrinth);
    connect(modrinthSearchEdit_, &QLineEdit::returnPressed, this, &MainWindow::searchModrinth);
    connect(modrinthInstallButton, &QPushButton::clicked, this, &MainWindow::installSelectedModrinth);
    connect(modSourceCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        const bool curseforge = modSourceCombo_->currentData().toString() == QStringLiteral("curseforge");
        if (curseforge && modrinthTypeCombo_->currentData().toString() == QStringLiteral("modpack")) {
            modrinthTypeCombo_->setCurrentIndex(0);
        }
        modrinthTypeCombo_->setEnabled(!curseforge);
    });
    connect(curseForgeApiKeyEdit_, &QLineEdit::editingFinished, this, &MainWindow::saveSettings);
    modsLayout->addWidget(modrinthGroup, 1);

    tabs->addTab(modsTab, QStringLiteral("Моды и папки"));

    auto *newsTab = new QWidget;
    auto *newsLayout = new QVBoxLayout(newsTab);
    auto *newsHeader = new QHBoxLayout;
    auto *newsTitle = new QLabel(QStringLiteral("Официальные новости Minecraft"));
    newsTitle->setStyleSheet(QStringLiteral("font-size: 14pt; font-weight: 600;"));
    auto *refreshNewsButton = new QPushButton(QStringLiteral("Обновить"));
    newsHeader->addWidget(newsTitle);
    newsHeader->addStretch();
    newsHeader->addWidget(refreshNewsButton);
    newsLayout->addLayout(newsHeader);
    newsBrowser_ = new QTextBrowser;
    newsBrowser_->setOpenExternalLinks(true);
    newsBrowser_->setOpenLinks(true);
    newsBrowser_->setHtml(QStringLiteral("<p style='color:#9fb0c7;'>Загрузка новостей…</p>"));
    newsLayout->addWidget(newsBrowser_, 1);
    connect(refreshNewsButton, &QPushButton::clicked, this, &MainWindow::loadNews);
    tabs->addTab(newsTab, QStringLiteral("Новости"));

    auto *diagnosticsTab = new QWidget;
    auto *diagnosticsLayout = new QVBoxLayout(diagnosticsTab);
    auto *diagnosticsInfo = new QLabel(QStringLiteral(
        "Здесь отображаются локальные crash reports выбранного инстанса. Отчёты читаются только с диска и не отправляются автоматически."));
    diagnosticsInfo->setWordWrap(true);
    diagnosticsInfo->setStyleSheet(QStringLiteral("color: #9fb0c7;"));
    diagnosticsLayout->addWidget(diagnosticsInfo);
    auto *diagnosticsSplitter = new QSplitter(Qt::Horizontal);
    crashReportsList_ = new QListWidget;
    crashReportsList_->setMinimumWidth(260);
    crashReportEdit_ = new QPlainTextEdit;
    crashReportEdit_->setReadOnly(true);
    crashReportEdit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    diagnosticsSplitter->addWidget(crashReportsList_);
    diagnosticsSplitter->addWidget(crashReportEdit_);
    diagnosticsSplitter->setStretchFactor(1, 1);
    diagnosticsLayout->addWidget(diagnosticsSplitter, 1);
    auto *diagnosticsButtons = new QHBoxLayout;
    auto *refreshCrashReportsButton = new QPushButton(QStringLiteral("Обновить отчёты"));
    auto *openCrashReportsButton = new QPushButton(QStringLiteral("Открыть папку"));
    diagnosticsButtons->addWidget(refreshCrashReportsButton);
    diagnosticsButtons->addWidget(openCrashReportsButton);
    diagnosticsButtons->addStretch();
    diagnosticsLayout->addLayout(diagnosticsButtons);
    connect(crashReportsList_, &QListWidget::currentRowChanged,
            this, &MainWindow::showCrashReport);
    connect(refreshCrashReportsButton, &QPushButton::clicked,
            this, &MainWindow::refreshCrashReports);
    connect(openCrashReportsButton, &QPushButton::clicked,
            this, &MainWindow::openCrashReportsDirectory);
    tabs->addTab(diagnosticsTab, QStringLiteral("Диагностика"));

    auto *accountTab = new QWidget;
    auto *accountLayout = new QVBoxLayout(accountTab);
    auto *accountInfo = new QLabel(QStringLiteral(
        "Скин изменяется через официальный Minecraft Services API. Для операций нужен выполненный вход в аккаунт, владеющий Java Edition."));
    accountInfo->setWordWrap(true);
    accountInfo->setStyleSheet(QStringLiteral("color:#9fb0c7;"));
    accountLayout->addWidget(accountInfo);
    auto *skinGroup = new QGroupBox(QStringLiteral("Скин Minecraft"));
    auto *skinForm = new QFormLayout(skinGroup);
    auto *skinFileRow = new QWidget;
    auto *skinFileLayout = new QHBoxLayout(skinFileRow);
    skinFileLayout->setContentsMargins(0, 0, 0, 0);
    skinFileEdit_ = new QLineEdit;
    skinFileEdit_->setPlaceholderText(QStringLiteral("PNG 64×64 или 64×32"));
    auto *skinBrowseButton = new QPushButton(QStringLiteral("Обзор…"));
    skinFileLayout->addWidget(skinFileEdit_, 1);
    skinFileLayout->addWidget(skinBrowseButton);
    skinForm->addRow(QStringLiteral("Файл:"), skinFileRow);
    skinVariantCombo_ = new QComboBox;
    skinVariantCombo_->addItem(QStringLiteral("Classic / Steve"), QStringLiteral("classic"));
    skinVariantCombo_->addItem(QStringLiteral("Slim / Alex"), QStringLiteral("slim"));
    skinForm->addRow(QStringLiteral("Модель:"), skinVariantCombo_);
    auto *skinButtons = new QHBoxLayout;
    auto *uploadSkinButton = new QPushButton(QStringLiteral("Загрузить скин"));
    auto *resetSkinButton = new QPushButton(QStringLiteral("Сбросить скин"));
    skinButtons->addStretch();
    skinButtons->addWidget(uploadSkinButton);
    skinButtons->addWidget(resetSkinButton);
    skinForm->addRow(QString(), skinButtons);
    accountLayout->addWidget(skinGroup);
    accountLayout->addStretch();
    connect(skinBrowseButton, &QPushButton::clicked, this, &MainWindow::browseSkinFile);
    connect(uploadSkinButton, &QPushButton::clicked, this, &MainWindow::uploadSkin);
    connect(resetSkinButton, &QPushButton::clicked, this, &MainWindow::resetSkin);
    tabs->addTab(accountTab, QStringLiteral("Аккаунт"));

    auto *footer = new QHBoxLayout;
    statusLabel_ = new QLabel(QStringLiteral("Готово"));
    statusLabel_->setStyleSheet(QStringLiteral("color: #9fb6d8;"));
    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setMaximumWidth(360);
    footer->addWidget(statusLabel_, 1);
    footer->addWidget(progressBar_);
    root->addLayout(footer);

    setCentralWidget(central);
}

void MainWindow::restoreSettings()
{
    QSettings settings;
    gameDirectoryEdit_->setText(settings.value(QStringLiteral("game/directory"),
                                                Paths::defaultGameDir()).toString());
    javaEdit_->setText(settings.value(QStringLiteral("java/path")).toString());
    minimumMemorySpin_->setValue(settings.value(QStringLiteral("java/minMemory"), 2048).toInt());
    maximumMemorySpin_->setValue(settings.value(QStringLiteral("java/maxMemory"), 4096).toInt());
    customResolutionCheck_->setChecked(settings.value(QStringLiteral("window/customResolution"), false).toBool());
    widthSpin_->setValue(settings.value(QStringLiteral("window/width"), 1280).toInt());
    heightSpin_->setValue(settings.value(QStringLiteral("window/height"), 720).toInt());
    widthSpin_->setEnabled(customResolutionCheck_->isChecked());
    heightSpin_->setEnabled(customResolutionCheck_->isChecked());
    connectServerCheck_->setChecked(settings.value(QStringLiteral("server/enabled"), false).toBool());
    serverAddressEdit_->setText(settings.value(QStringLiteral("server/address")).toString());
    serverPortSpin_->setValue(settings.value(QStringLiteral("server/port"), 25565).toInt());
    curseForgeApiKeyEdit_->setText(settings.value(QStringLiteral("curseforge/apiKey")).toString());
    skinFileEdit_->setText(settings.value(QStringLiteral("skin/file")).toString());
    skinVariantCombo_->setCurrentIndex(qMax(0, skinVariantCombo_->findData(
        settings.value(QStringLiteral("skin/variant"), QStringLiteral("classic")))));
    installedVersion_ = settings.value(QStringLiteral("game/installedVersion")).toString();
    loadServers();
    loadInstances();
    refreshMods();
    refreshCrashReports();
    updatePlayButton();
}

void MainWindow::saveSettings()
{
    syncCurrentInstance();
    saveInstances();
    saveServers();
    QSettings settings;
    settings.setValue(QStringLiteral("game/directory"), gameDirectoryEdit_->text().trimmed());
    settings.setValue(QStringLiteral("java/path"), javaEdit_->text().trimmed());
    settings.setValue(QStringLiteral("java/minMemory"), minimumMemorySpin_->value());
    settings.setValue(QStringLiteral("java/maxMemory"), maximumMemorySpin_->value());
    settings.setValue(QStringLiteral("window/customResolution"), customResolutionCheck_->isChecked());
    settings.setValue(QStringLiteral("window/width"), widthSpin_->value());
    settings.setValue(QStringLiteral("window/height"), heightSpin_->value());
    settings.setValue(QStringLiteral("server/enabled"), connectServerCheck_->isChecked());
    settings.setValue(QStringLiteral("server/address"), serverAddressEdit_->text().trimmed());
    settings.setValue(QStringLiteral("server/port"), serverPortSpin_->value());
    settings.setValue(QStringLiteral("curseforge/apiKey"), curseForgeApiKeyEdit_->text().trimmed());
    settings.setValue(QStringLiteral("skin/file"), skinFileEdit_->text().trimmed());
    settings.setValue(QStringLiteral("skin/variant"), skinVariantCombo_->currentData().toString());
    settings.setValue(QStringLiteral("game/installedVersion"), installedVersion_);
    settings.sync();
}

void MainWindow::loadCatalog()
{
    if (catalogThread_) {
        return;
    }
    statusLabel_->setText(QStringLiteral("Загрузка официального списка версий…"));
    versionCombo_->setEnabled(false);

    auto *thread = new QThread;
    auto *worker = new CatalogWorker;
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &CatalogWorker::run);
    connect(worker, &CatalogWorker::loaded, this, &MainWindow::catalogLoaded);
    connect(worker, &CatalogWorker::failed, this, &MainWindow::catalogFailed);
    connect(worker, &CatalogWorker::loaded, thread, &QThread::quit);
    connect(worker, &CatalogWorker::failed, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (catalogThread_ == thread) {
            catalogThread_ = nullptr;
        }
    });
    catalogThread_ = thread;
    thread->start();
}

void MainWindow::catalogLoaded(const QByteArray &manifestJson)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        catalogFailed(QStringLiteral("Официальный манифест версий содержит некорректный JSON."));
        return;
    }

    const QJsonObject manifest = document.object();
    const QJsonObject latest = manifest.value(QStringLiteral("latest")).toObject();
    const QString latestRelease = latest.value(QStringLiteral("release")).toString();
    versionUrls_.clear();
    versionCombo_->blockSignals(true);
    versionCombo_->clear();
    for (const QJsonValue &value : manifest.value(QStringLiteral("versions")).toArray()) {
        const QJsonObject version = value.toObject();
        const QString id = version.value(QStringLiteral("id")).toString();
        const QString type = version.value(QStringLiteral("type")).toString();
        const QString url = version.value(QStringLiteral("url")).toString();
        if (id.isEmpty() || url.isEmpty()) {
            continue;
        }
        versionUrls_.insert(id, url);
        versionCombo_->addItem(QStringLiteral("%1  ·  %2").arg(id, type), id);
    }
    int selectedIndex = versionCombo_->findData(latestRelease);
    if (selectedIndex < 0 && versionCombo_->count() > 0) {
        selectedIndex = 0;
    }
    if (selectedIndex >= 0) {
        versionCombo_->setCurrentIndex(selectedIndex);
    }
    versionCombo_->blockSignals(false);
    loadLocalProfiles();
    versionCombo_->setEnabled(versionCombo_->count() > 0);
    fabricButton_->setEnabled(versionCombo_->count() > 0 && !installThread_ && !fabricThread_ && !forgeThread_);
    forgeButton_->setEnabled(versionCombo_->count() > 0 && !installThread_ && !fabricThread_ && !forgeThread_);
    quiltButton_->setEnabled(versionCombo_->count() > 0 && !installThread_ && !fabricThread_ && !forgeThread_ && !quiltThread_);
    statusLabel_->setText(QStringLiteral("Список версий обновлён. Выбрана %1.")
                              .arg(selectedVersionId()));
    appendLog(QStringLiteral("Загружено официальных версий: %1").arg(versionCombo_->count()));
    updatePlayButton();
}

void MainWindow::catalogFailed(const QString &message)
{
    versionCombo_->setEnabled(versionCombo_->count() > 0);
    fabricButton_->setEnabled(versionCombo_->count() > 0 && !installThread_ && !fabricThread_ && !forgeThread_);
    forgeButton_->setEnabled(versionCombo_->count() > 0 && !installThread_ && !fabricThread_ && !forgeThread_);
    quiltButton_->setEnabled(versionCombo_->count() > 0 && !installThread_ && !fabricThread_ && !forgeThread_ && !quiltThread_);
    statusLabel_->setText(message);
    appendLog(message);
}

void MainWindow::installFabric()
{
    if (fabricThread_ || forgeThread_ || quiltThread_ || installThread_ || modrinthInstallThread_) {
        return;
    }
    const QString minecraftVersion = selectedVersionId();
    const QString gameDir = gameDirectoryEdit_->text().trimmed();
    if (minecraftVersion.isEmpty() || gameDir.isEmpty() || selectedVersionUrl().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Fabric"),
                             QStringLiteral("Выберите обычную версию Minecraft из официального списка."));
        return;
    }
    if (!isVersionInstalled(minecraftVersion)) {
        QMessageBox::information(this, QStringLiteral("Сначала установите Minecraft"),
                                 QStringLiteral("Fabric использует базовую версию. Сначала будет установлена обычная версия, затем нажмите Fabric ещё раз."));
        installSelectedVersion();
        return;
    }

    saveSettings();
    setBusy(true);
    fabricButton_->setEnabled(false);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_->setText(QStringLiteral("Установка Fabric для %1…").arg(minecraftVersion));
    appendLog(QStringLiteral("Начата установка Fabric для %1").arg(minecraftVersion));

    auto *thread = new QThread;
    auto *worker = new FabricInstallerWorker(minecraftVersion, gameDir);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &FabricInstallerWorker::run);
    connect(worker, &FabricInstallerWorker::progress, this, &MainWindow::fabricProgress);
    connect(worker, &FabricInstallerWorker::logMessage, this, &MainWindow::appendLog);
    connect(worker, &FabricInstallerWorker::finished, this, &MainWindow::fabricFinished);
    connect(worker, &FabricInstallerWorker::failed, this, &MainWindow::fabricFailed);
    connect(worker, &FabricInstallerWorker::finished, thread, &QThread::quit);
    connect(worker, &FabricInstallerWorker::failed, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (fabricThread_ == thread) {
            fabricThread_ = nullptr;
            setBusy(false);
        }
    });
    fabricThread_ = thread;
    thread->start();
}

void MainWindow::fabricProgress(qint64 current, qint64 total, const QString &label)
{
    installProgress(current, total, label);
}

void MainWindow::fabricFinished(const QString &profileId, const QString &profileJsonPath)
{
    Q_UNUSED(profileJsonPath);
    if (versionCombo_->findData(profileId) < 0) {
        versionCombo_->addItem(QStringLiteral("Fabric · %1").arg(profileId), profileId);
    }
    versionCombo_->setCurrentIndex(versionCombo_->findData(profileId));
    installedVersion_ = profileId;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(100);
    statusLabel_->setText(QStringLiteral("Fabric-профиль установлен."));
    appendLog(QStringLiteral("Fabric-профиль %1 добавлен в список версий.").arg(profileId));
    saveSettings();
    updatePlayButton();
}

void MainWindow::fabricFailed(const QString &message)
{
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_->setText(QStringLiteral("Ошибка установки Fabric."));
    appendLog(QStringLiteral("Ошибка Fabric: %1").arg(message));
    QMessageBox::critical(this, QStringLiteral("Fabric не установлен"), message);
}

void MainWindow::installForge()
{
    if (forgeThread_ || quiltThread_ || installThread_ || fabricThread_ || modrinthInstallThread_) {
        return;
    }
    const QString minecraftVersion = selectedVersionId();
    const QString gameDir = gameDirectoryEdit_->text().trimmed();
    if (minecraftVersion.isEmpty() || gameDir.isEmpty() || selectedVersionUrl().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Forge"),
                             QStringLiteral("Выберите обычную версию Minecraft из официального списка."));
        return;
    }
    if (!isVersionInstalled(minecraftVersion)) {
        QMessageBox::information(this, QStringLiteral("Сначала установите Minecraft"),
                                 QStringLiteral("Forge использует базовую версию. Сначала будет установлена обычная версия, затем нажмите Forge ещё раз."));
        installSelectedVersion();
        return;
    }

    const QString javaPath = LauncherBuilder::findJava(javaEdit_->text().trimmed());
    if (javaPath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Forge"),
                             QStringLiteral("Для Forge installer нужен установленный java.exe."));
        return;
    }

    saveSettings();
    setBusy(true);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_->setText(QStringLiteral("Установка Forge для %1…").arg(minecraftVersion));
    appendLog(QStringLiteral("Начата установка Forge для %1").arg(minecraftVersion));

    auto *thread = new QThread;
    auto *worker = new ForgeInstallerWorker(minecraftVersion, gameDir, javaPath);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &ForgeInstallerWorker::run);
    connect(worker, &ForgeInstallerWorker::progress, this, &MainWindow::forgeProgress);
    connect(worker, &ForgeInstallerWorker::logMessage, this, &MainWindow::appendLog);
    connect(worker, &ForgeInstallerWorker::finished, this, &MainWindow::forgeFinished);
    connect(worker, &ForgeInstallerWorker::failed, this, &MainWindow::forgeFailed);
    connect(worker, &ForgeInstallerWorker::finished, thread, &QThread::quit);
    connect(worker, &ForgeInstallerWorker::failed, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (forgeThread_ == thread) {
            forgeThread_ = nullptr;
            setBusy(false);
        }
    });
    forgeThread_ = thread;
    thread->start();
}

void MainWindow::forgeProgress(qint64 current, qint64 total, const QString &label)
{
    installProgress(current, total, label);
}

void MainWindow::forgeFinished(const QString &profileId, const QString &profileJsonPath)
{
    Q_UNUSED(profileJsonPath);
    if (versionCombo_->findData(profileId) < 0) {
        versionCombo_->addItem(QStringLiteral("Forge · %1").arg(profileId), profileId);
    }
    versionCombo_->setCurrentIndex(versionCombo_->findData(profileId));
    installedVersion_ = profileId;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(100);
    statusLabel_->setText(QStringLiteral("Forge-профиль установлен."));
    appendLog(QStringLiteral("Forge-профиль %1 добавлен в список версий.").arg(profileId));
    saveSettings();
    updatePlayButton();
}

void MainWindow::forgeFailed(const QString &message)
{
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_->setText(QStringLiteral("Ошибка установки Forge."));
    appendLog(QStringLiteral("Ошибка Forge: %1").arg(message));
    QMessageBox::critical(this, QStringLiteral("Forge не установлен"), message);
}

void MainWindow::installQuilt()
{
    if (quiltThread_ || installThread_ || fabricThread_ || forgeThread_ || modrinthInstallThread_) {
        return;
    }
    const QString minecraftVersion = selectedVersionId();
    const QString gameDir = gameDirectoryEdit_->text().trimmed();
    if (minecraftVersion.isEmpty() || gameDir.isEmpty() || selectedVersionUrl().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Quilt"),
                             QStringLiteral("Выберите обычную версию Minecraft из официального списка."));
        return;
    }
    if (!isVersionInstalled(minecraftVersion)) {
        QMessageBox::information(this, QStringLiteral("Сначала установите Minecraft"),
                                 QStringLiteral("Quilt использует базовую версию. Сначала будет установлена обычная версия, затем нажмите Quilt ещё раз."));
        installSelectedVersion();
        return;
    }

    saveSettings();
    setBusy(true);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_->setText(QStringLiteral("Установка Quilt для %1…").arg(minecraftVersion));
    appendLog(QStringLiteral("Начата установка Quilt для %1").arg(minecraftVersion));

    auto *thread = new QThread;
    auto *worker = new QuiltInstallerWorker(minecraftVersion, gameDir);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &QuiltInstallerWorker::run);
    connect(worker, &QuiltInstallerWorker::progress, this, &MainWindow::quiltProgress);
    connect(worker, &QuiltInstallerWorker::logMessage, this, &MainWindow::appendLog);
    connect(worker, &QuiltInstallerWorker::finished, this, &MainWindow::quiltFinished);
    connect(worker, &QuiltInstallerWorker::failed, this, &MainWindow::quiltFailed);
    connect(worker, &QuiltInstallerWorker::finished, thread, &QThread::quit);
    connect(worker, &QuiltInstallerWorker::failed, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (quiltThread_ == thread) {
            quiltThread_ = nullptr;
            setBusy(false);
        }
    });
    quiltThread_ = thread;
    thread->start();
}

void MainWindow::quiltProgress(qint64 current, qint64 total, const QString &label)
{
    installProgress(current, total, label);
}

void MainWindow::quiltFinished(const QString &profileId, const QString &profileJsonPath)
{
    Q_UNUSED(profileJsonPath);
    if (versionCombo_->findData(profileId) < 0) {
        versionCombo_->addItem(QStringLiteral("Quilt · %1").arg(profileId), profileId);
    }
    versionCombo_->setCurrentIndex(versionCombo_->findData(profileId));
    installedVersion_ = profileId;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(100);
    statusLabel_->setText(QStringLiteral("Quilt-профиль установлен."));
    appendLog(QStringLiteral("Quilt-профиль %1 добавлен в список версий.").arg(profileId));
    saveSettings();
    updatePlayButton();
}

void MainWindow::quiltFailed(const QString &message)
{
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_->setText(QStringLiteral("Ошибка установки Quilt."));
    appendLog(QStringLiteral("Ошибка Quilt: %1").arg(message));
    QMessageBox::critical(this, QStringLiteral("Quilt не установлен"), message);
}

void MainWindow::loadNews()
{
    if (newsThread_) {
        return;
    }
    auto *thread = new QThread;
    auto *worker = new NewsWorker;
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &NewsWorker::run);
    connect(worker, &NewsWorker::loaded, this, &MainWindow::newsLoaded);
    connect(worker, &NewsWorker::failed, this, &MainWindow::newsFailed);
    connect(worker, &NewsWorker::loaded, thread, &QThread::quit);
    connect(worker, &NewsWorker::failed, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (newsThread_ == thread) {
            newsThread_ = nullptr;
        }
    });
    newsThread_ = thread;
    thread->start();
}

void MainWindow::newsLoaded(const QString &html)
{
    newsBrowser_->setHtml(html);
}

void MainWindow::newsFailed(const QString &message)
{
    newsBrowser_->setHtml(QStringLiteral("<p style='color:#ff9b9b;'>%1</p>").arg(message.toHtmlEscaped()));
    appendLog(message);
}

void MainWindow::searchModrinth()
{
    if (modrinthSearchThread_) {
        return;
    }
    const QString query = modrinthSearchEdit_->text().trimmed();
    if (query.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Modrinth"),
                                 QStringLiteral("Введите название мода для поиска."));
        return;
    }

    QString gameVersion = selectedVersionId();
    QString profileError;
    const QJsonDocument selectedProfile = readJsonFile(
        QDir(Paths::versionDir(gameDirectoryEdit_->text().trimmed(), gameVersion))
            .filePath(gameVersion + QStringLiteral(".json")), &profileError);
    if (selectedProfile.isObject()) {
        const QString parent = selectedProfile.object().value(QStringLiteral("inheritsFrom")).toString();
        if (!parent.isEmpty()) {
            gameVersion = parent;
        }
    }
    if (gameVersion.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Modrinth"),
                             QStringLiteral("Сначала выберите версию Minecraft."));
        return;
    }

    const QString loader = modrinthLoaderCombo_->currentText();
    const QString source = modSourceCombo_->currentData().toString();
    const QString projectType = modrinthTypeCombo_->currentData().toString();
    saveSettings();
    const QString apiKey = source == QStringLiteral("curseforge") ? curseForgeApiKey() : QString();
    if (source == QStringLiteral("curseforge") && apiKey.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("CurseForge"),
                             QStringLiteral("Укажите API key в поле выше или в переменной MCL_CURSEFORGE_API_KEY / CURSEFORGE_API_KEY."));
        return;
    }
    statusLabel_->setText(QStringLiteral("Поиск в %1…")
                              .arg(source == QStringLiteral("curseforge") ? QStringLiteral("CurseForge")
                                                                          : QStringLiteral("Modrinth")));
    modrinthResults_->clear();
    modSourceCombo_->setEnabled(false);
    auto *thread = new QThread;
    QObject *worker = nullptr;
    if (source == QStringLiteral("curseforge")) {
        auto *curseWorker = new CurseForgeSearchWorker(query, gameVersion, loader, apiKey);
        worker = curseWorker;
        connect(thread, &QThread::started, curseWorker, &CurseForgeSearchWorker::run);
        connect(curseWorker, &CurseForgeSearchWorker::loaded, this, &MainWindow::modrinthSearchLoaded);
        connect(curseWorker, &CurseForgeSearchWorker::failed, this, &MainWindow::modrinthSearchFailed);
        connect(curseWorker, &CurseForgeSearchWorker::loaded, thread, &QThread::quit);
        connect(curseWorker, &CurseForgeSearchWorker::failed, thread, &QThread::quit);
    } else {
        auto *modrinthWorker = new ModrinthSearchWorker(query, gameVersion, loader, projectType);
        worker = modrinthWorker;
        connect(thread, &QThread::started, modrinthWorker, &ModrinthSearchWorker::run);
        connect(modrinthWorker, &ModrinthSearchWorker::loaded, this, &MainWindow::modrinthSearchLoaded);
        connect(modrinthWorker, &ModrinthSearchWorker::failed, this, &MainWindow::modrinthSearchFailed);
        connect(modrinthWorker, &ModrinthSearchWorker::loaded, thread, &QThread::quit);
        connect(modrinthWorker, &ModrinthSearchWorker::failed, thread, &QThread::quit);
    }
    worker->moveToThread(thread);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (modrinthSearchThread_ == thread) {
            modrinthSearchThread_ = nullptr;
            modSourceCombo_->setEnabled(true);
        }
    });
    modrinthSearchThread_ = thread;
    thread->start();
}

void MainWindow::modrinthSearchLoaded(const QByteArray &json)
{
    const bool curseforge = modSourceCombo_->currentData().toString() == QStringLiteral("curseforge");
    const QJsonDocument document = QJsonDocument::fromJson(json);
    const QJsonArray hits = document.isObject()
                                ? document.object().value(curseforge ? QStringLiteral("data")
                                                                       : QStringLiteral("hits")).toArray()
                                : QJsonArray();
    modrinthResults_->clear();
    for (const QJsonValue &value : hits) {
        const QJsonObject project = value.toObject();
        const QString id = curseforge
                               ? QString::number(project.value(QStringLiteral("id")).toInteger())
                               : project.value(QStringLiteral("project_id")).toString();
        const QString title = curseforge
                                  ? project.value(QStringLiteral("name")).toString()
                                  : project.value(QStringLiteral("title")).toString();
        const QString description = curseforge
                                        ? project.value(QStringLiteral("summary")).toString()
                                        : project.value(QStringLiteral("description")).toString();
        if (id.isEmpty() || title.isEmpty()) {
            continue;
        }
        auto *item = new QListWidgetItem(QStringLiteral("%1  —  %2").arg(title, description));
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, curseforge ? QStringLiteral("curseforge")
                                                   : QStringLiteral("modrinth"));
        item->setToolTip(QStringLiteral("%1\n%2\nID: %3")
                             .arg(title, description, id));
        modrinthResults_->addItem(item);
    }
    if (modrinthResults_->count() == 0) {
        modrinthResults_->addItem(QStringLiteral("Совместимые проекты не найдены."));
    }
    statusLabel_->setText(QStringLiteral("Результатов %1: %2")
                              .arg(curseforge ? QStringLiteral("CurseForge") : QStringLiteral("Modrinth"))
                              .arg(hits.size()));
}

void MainWindow::modrinthSearchFailed(const QString &message)
{
    statusLabel_->setText(message);
    appendLog(message);
    modrinthResults_->addItem(QStringLiteral("Ошибка поиска: %1").arg(message));
}

void MainWindow::installSelectedModrinth()
{
    if (modrinthInstallThread_) {
        return;
    }
    const QListWidgetItem *item = modrinthResults_->currentItem();
    const QString projectId = item ? item->data(Qt::UserRole).toString() : QString();
    if (projectId.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Моды"),
                             QStringLiteral("Выберите проект из результатов поиска."));
        return;
    }

    const QString source = item->data(Qt::UserRole + 1).toString().isEmpty()
                               ? modSourceCombo_->currentData().toString()
                               : item->data(Qt::UserRole + 1).toString();
    const bool curseforge = source == QStringLiteral("curseforge");
    QString gameVersion = selectedVersionId();
    QString profileError;
    const QJsonDocument selectedProfile = readJsonFile(
        QDir(Paths::versionDir(gameDirectoryEdit_->text().trimmed(), gameVersion))
            .filePath(gameVersion + QStringLiteral(".json")), &profileError);
    if (selectedProfile.isObject()) {
        const QString parent = selectedProfile.object().value(QStringLiteral("inheritsFrom")).toString();
        if (!parent.isEmpty()) {
            gameVersion = parent;
        }
    }
    if (gameVersion.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Моды"),
                             QStringLiteral("Не удалось определить версию Minecraft."));
        return;
    }

    const QString gameDir = gameDirectoryEdit_->text().trimmed();
    const QString loader = modrinthLoaderCombo_->currentText();
    saveSettings();
    const QString apiKey = curseforge ? curseForgeApiKey() : QString();
    if (curseforge && apiKey.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("CurseForge"),
                             QStringLiteral("Укажите API key в поле выше или в переменной MCL_CURSEFORGE_API_KEY / CURSEFORGE_API_KEY."));
        return;
    }

    setBusy(true);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_->setText(QStringLiteral("Установка мода %1…")
                              .arg(curseforge ? QStringLiteral("CurseForge") : QStringLiteral("Modrinth")));
    modSourceCombo_->setEnabled(false);

    auto *thread = new QThread;
    if (curseforge) {
        auto *worker = new CurseForgeInstallWorker(projectId, gameVersion, loader, gameDir, apiKey);
        worker->moveToThread(thread);
        connect(thread, &QThread::started, worker, &CurseForgeInstallWorker::run);
        connect(worker, &CurseForgeInstallWorker::progress, this, &MainWindow::modrinthInstallProgress);
        connect(worker, &CurseForgeInstallWorker::logMessage, this, &MainWindow::appendLog);
        connect(worker, &CurseForgeInstallWorker::finished, this, &MainWindow::modrinthInstallFinished);
        connect(worker, &CurseForgeInstallWorker::failed, this, &MainWindow::modrinthInstallFailed);
        connect(worker, &CurseForgeInstallWorker::finished, thread, &QThread::quit);
        connect(worker, &CurseForgeInstallWorker::failed, thread, &QThread::quit);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    } else {
        auto *worker = new ModrinthInstallWorker(projectId, gameVersion, loader,
                                                 modrinthTypeCombo_->currentData().toString(), gameDir);
        worker->moveToThread(thread);
        connect(thread, &QThread::started, worker, &ModrinthInstallWorker::run);
        connect(worker, &ModrinthInstallWorker::progress, this, &MainWindow::modrinthInstallProgress);
        connect(worker, &ModrinthInstallWorker::logMessage, this, &MainWindow::appendLog);
        connect(worker, &ModrinthInstallWorker::finished, this, &MainWindow::modrinthInstallFinished);
        connect(worker, &ModrinthInstallWorker::failed, this, &MainWindow::modrinthInstallFailed);
        connect(worker, &ModrinthInstallWorker::finished, thread, &QThread::quit);
        connect(worker, &ModrinthInstallWorker::failed, thread, &QThread::quit);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    }
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (modrinthInstallThread_ == thread) {
            modrinthInstallThread_ = nullptr;
            modSourceCombo_->setEnabled(true);
            setBusy(false);
        }
    });
    modrinthInstallThread_ = thread;
    thread->start();
}

void MainWindow::modrinthInstallProgress(qint64 current, qint64 total, const QString &label)
{
    installProgress(current, total, label);
}

void MainWindow::modrinthInstallFinished(const QString &projectId)
{
    const bool curseforge = modSourceCombo_->currentData().toString() == QStringLiteral("curseforge");
    const QString source = curseforge ? QStringLiteral("CurseForge") : QStringLiteral("Modrinth");
    progressBar_->setRange(0, 100);
    progressBar_->setValue(100);
    statusLabel_->setText(QStringLiteral("Мод установлен: %1").arg(projectId));
    appendLog(QStringLiteral("%1 проект %2 установлен в mods.").arg(source, projectId));
    refreshMods();
}

void MainWindow::modrinthInstallFailed(const QString &message)
{
    const bool curseforge = modSourceCombo_->currentData().toString() == QStringLiteral("curseforge");
    const QString source = curseforge ? QStringLiteral("CurseForge") : QStringLiteral("Modrinth");
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_->setText(QStringLiteral("Ошибка установки мода."));
    appendLog(QStringLiteral("Ошибка %1: %2").arg(source, message));
    QMessageBox::critical(this, source, message);
}

void MainWindow::browseSkinFile()
{
    const QString selected = QFileDialog::getOpenFileName(
        this, QStringLiteral("Выберите PNG-скин"), skinFileEdit_->text(),
        QStringLiteral("PNG skin (*.png);;Все файлы (*.*)"));
    if (!selected.isEmpty()) {
        skinFileEdit_->setText(selected);
        saveSettings();
    }
}

void MainWindow::uploadSkin()
{
    runSkinOperation(SkinOperation::Upload);
}

void MainWindow::resetSkin()
{
    if (QMessageBox::question(this, QStringLiteral("Сбросить скин?"),
                              QStringLiteral("Текущий скин будет заменён стандартным."))
        == QMessageBox::Yes) {
        runSkinOperation(SkinOperation::Reset);
    }
}

void MainWindow::runSkinOperation(SkinOperation operation)
{
    if (skinThread_) {
        return;
    }
    if (!authenticator_->account().isAuthenticated()) {
        QMessageBox::warning(this, QStringLiteral("Нужен вход"),
                             QStringLiteral("Сначала войдите через Microsoft."));
        return;
    }
    if (operation == SkinOperation::Upload && skinFileEdit_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Скин"),
                             QStringLiteral("Выберите PNG-файл скина."));
        return;
    }

    saveSettings();
    statusLabel_->setText(operation == SkinOperation::Upload
                              ? QStringLiteral("Загрузка скина…")
                              : QStringLiteral("Сброс скина…"));
    auto *thread = new QThread;
    auto *worker = new SkinWorker(operation, authenticator_->account(),
                                  skinFileEdit_->text().trimmed(),
                                  skinVariantCombo_->currentData().toString());
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &SkinWorker::run);
    connect(worker, &SkinWorker::finished, this, &MainWindow::skinFinished);
    connect(worker, &SkinWorker::failed, this, &MainWindow::skinFailed);
    connect(worker, &SkinWorker::finished, thread, &QThread::quit);
    connect(worker, &SkinWorker::failed, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (skinThread_ == thread) {
            skinThread_ = nullptr;
        }
    });
    skinThread_ = thread;
    thread->start();
}

void MainWindow::skinFinished(const QString &message)
{
    statusLabel_->setText(message);
    appendLog(message);
}

void MainWindow::skinFailed(const QString &message)
{
    statusLabel_->setText(QStringLiteral("Ошибка операции со скином."));
    appendLog(message);
    QMessageBox::critical(this, QStringLiteral("Скин"), message);
}

void MainWindow::showDeviceCode(const QString &message,
                                const QString &userCode,
                                const QString &verificationUrl)
{
    const QString url = verificationUrl.isEmpty()
                            ? QStringLiteral("https://microsoft.com/devicelogin")
                            : verificationUrl;
    if (QApplication::clipboard()) {
        QApplication::clipboard()->setText(userCode);
    }
    QDesktopServices::openUrl(QUrl(url));

    QMessageBox box(QMessageBox::Information,
                    QStringLiteral("Вход через Microsoft"),
                    QStringLiteral("%1\n\nКод: %2\n\nСсылка уже открыта в браузере. Код скопирован в буфер обмена.")
                        .arg(message, userCode),
                    QMessageBox::Ok,
                    this);
    box.exec();
}

void MainWindow::authenticated(const mcl::Account &account)
{
    accountLabel_->setText(QStringLiteral("Аккаунт: %1").arg(account.name));
    loginButton_->setText(QStringLiteral("Выйти"));
    statusLabel_->setText(QStringLiteral("Вход выполнен: %1").arg(account.name));
    appendLog(QStringLiteral("Авторизован профиль Minecraft: %1").arg(account.name));
    updatePlayButton();
}

void MainWindow::loggedOut()
{
    accountLabel_->setText(QStringLiteral("Вход не выполнен"));
    loginButton_->setText(QStringLiteral("Войти через Microsoft"));
    statusLabel_->setText(QStringLiteral("Сессия завершена."));
    updatePlayButton();
}

void MainWindow::authFailed(const QString &message)
{
    appendLog(QStringLiteral("Ошибка авторизации: %1").arg(message));
    statusLabel_->setText(message);
    updatePlayButton();
}

void MainWindow::installSelectedVersion()
{
    if (installThread_ || fabricThread_ || forgeThread_ || quiltThread_ || modrinthInstallThread_) {
        return;
    }
    const QString versionId = selectedVersionId();
    const QString versionUrl = selectedVersionUrl();
    const QString gameDir = gameDirectoryEdit_->text().trimmed();
    if (versionId.isEmpty() || versionUrl.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Версия не выбрана"),
                             QStringLiteral("Сначала дождитесь загрузки списка версий."));
        return;
    }
    if (gameDir.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Папка игры не указана"),
                             QStringLiteral("Выберите папку для Minecraft."));
        return;
    }

    saveSettings();
    setBusy(true);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_->setText(QStringLiteral("Подготовка установки %1…").arg(versionId));
    appendLog(QStringLiteral("Начата установка версии %1 в %2").arg(versionId, gameDir));

    auto *thread = new QThread;
    auto *worker = new InstallerWorker(versionId, versionUrl, gameDir);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &InstallerWorker::run);
    connect(worker, &InstallerWorker::progress, this, &MainWindow::installProgress);
    connect(worker, &InstallerWorker::logMessage, this, &MainWindow::appendLog);
    connect(worker, &InstallerWorker::finished, this, &MainWindow::installFinished);
    connect(worker, &InstallerWorker::failed, this, &MainWindow::installFailed);
    connect(worker, &InstallerWorker::finished, thread, &QThread::quit);
    connect(worker, &InstallerWorker::failed, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (installThread_ == thread) {
            installThread_ = nullptr;
            setBusy(false);
        }
    });
    installThread_ = thread;
    thread->start();
}

void MainWindow::installProgress(qint64 current, qint64 total, const QString &label)
{
    if (total > 0) {
        progressBar_->setRange(0, 100);
        progressBar_->setValue(static_cast<int>(qBound<qint64>(0LL, current * 100 / total, 100LL)));
    } else {
        progressBar_->setRange(0, 0);
    }
    statusLabel_->setText(label);
}

void MainWindow::installFinished(const QString &versionId, const QString &versionJsonPath)
{
    Q_UNUSED(versionJsonPath);
    installedVersion_ = versionId;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(100);
    statusLabel_->setText(QStringLiteral("Установка %1 завершена.").arg(versionId));
    appendLog(QStringLiteral("Установка завершена успешно."));
    saveSettings();
    refreshMods();
    updatePlayButton();
}

void MainWindow::installFailed(const QString &message)
{
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_->setText(QStringLiteral("Ошибка установки."));
    appendLog(QStringLiteral("Ошибка установки: %1").arg(message));
    QMessageBox::critical(this, QStringLiteral("Установка не выполнена"), message);
}

void MainWindow::launchSelectedVersion()
{
    if (gameProcess_ && gameProcess_->state() != QProcess::NotRunning) {
        QMessageBox::information(this, QStringLiteral("Minecraft уже запущен"),
                                 QStringLiteral("Сначала закройте текущий экземпляр игры."));
        return;
    }
    if (!authenticator_->account().isAuthenticated()) {
        QMessageBox::warning(this, QStringLiteral("Нужен вход"),
                             QStringLiteral("Для запуска войдите через официальный аккаунт Microsoft, владеющий Minecraft Java Edition."));
        return;
    }

    const QString versionId = selectedVersionId();
    if (!isVersionInstalled(versionId)) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, QStringLiteral("Версия не установлена"),
            QStringLiteral("Установить выбранную версию сейчас?"));
        if (answer == QMessageBox::Yes) {
            installSelectedVersion();
        }
        return;
    }

    saveSettings();
    LaunchOptions options;
    options.gameDir = gameDirectoryEdit_->text().trimmed();
    options.versionId = versionId;
    options.javaPath = javaEdit_->text().trimmed();
    options.account = authenticator_->account();
    options.minimumMemoryMb = minimumMemorySpin_->value();
    options.maximumMemoryMb = maximumMemorySpin_->value();
    options.customResolution = customResolutionCheck_->isChecked();
    options.width = widthSpin_->value();
    options.height = heightSpin_->value();
    options.connectToServer = connectServerCheck_->isChecked();
    options.serverAddress = serverAddressEdit_->text().trimmed();
    options.serverPort = serverPortSpin_->value();

    const LaunchSpec spec = LauncherBuilder::build(options);
    if (!spec.ok) {
        appendLog(QStringLiteral("Запуск отменён: %1").arg(spec.error));
        QMessageBox::critical(this, QStringLiteral("Не удалось запустить Minecraft"), spec.error);
        return;
    }

    const QString javaDisplayVersion = spec.javaMajor > 0
                                           ? QString::number(spec.javaMajor)
                                           : QStringLiteral("неизвестная");
    appendLog(QStringLiteral("Для Minecraft %1 требуется Java %2; выбрана Java %3: %4")
                  .arg(versionId)
                  .arg(spec.requiredJavaMajor > 0 ? QString::number(spec.requiredJavaMajor)
                                                 : QStringLiteral("совместимая"))
                  .arg(javaDisplayVersion, spec.program));
    if (javaEdit_->text().trimmed() != spec.program) {
        javaEdit_->setText(spec.program);
        saveSettings();
    }
    appendLog(QStringLiteral("Запуск Minecraft %1 через %2").arg(versionId, spec.program));
    gameProcess_ = new QProcess(this);
    gameProcess_->setProgram(spec.program);
    gameProcess_->setArguments(spec.arguments);
    gameProcess_->setWorkingDirectory(spec.workingDirectory);
    gameProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(gameProcess_, &QProcess::readyReadStandardOutput, this, &MainWindow::gameOutput);
    connect(gameProcess_, &QProcess::readyReadStandardError, this, &MainWindow::gameErrorOutput);
    connect(gameProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &MainWindow::gameFinished);
    connect(gameProcess_, &QProcess::errorOccurred, this, &MainWindow::gameError);
    gameProcess_->start();
    if (!gameProcess_->waitForStarted(3000)) {
        appendLog(QStringLiteral("Java не запустилась: %1").arg(gameProcess_->errorString()));
        gameProcess_->deleteLater();
        gameProcess_ = nullptr;
        updatePlayButton();
        return;
    }
    statusLabel_->setText(QStringLiteral("Minecraft запущен."));
    updatePlayButton();
}

void MainWindow::gameOutput()
{
    if (auto *process = qobject_cast<QProcess *>(sender())) {
        appendLog(QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed());
    }
}

void MainWindow::gameErrorOutput()
{
    if (auto *process = qobject_cast<QProcess *>(sender())) {
        appendLog(QString::fromLocal8Bit(process->readAllStandardError()).trimmed());
    }
}

void MainWindow::gameFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    appendLog(QStringLiteral("Minecraft завершился: код %1 (%2).")
                  .arg(exitCode)
                  .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("обычно")
                                                           : QStringLiteral("аварийно")));
    statusLabel_->setText(QStringLiteral("Minecraft завершён."));
    if (gameProcess_ == sender()) {
        gameProcess_->deleteLater();
        gameProcess_ = nullptr;
    }
    refreshCrashReports();
    updatePlayButton();
}

void MainWindow::gameError(QProcess::ProcessError error)
{
    Q_UNUSED(error);
    if (auto *process = qobject_cast<QProcess *>(sender())) {
        appendLog(QStringLiteral("Ошибка процесса Minecraft: %1").arg(process->errorString()));
    }
}

void MainWindow::loginClicked()
{
    if (authenticator_->account().isAuthenticated()) {
        authenticator_->logout();
        return;
    }
    statusLabel_->setText(QStringLiteral("Подготовка входа Microsoft…"));
    authenticator_->login();
}

void MainWindow::browseGameDirectory()
{
    const QString selected = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Выберите папку игры"), gameDirectoryEdit_->text());
    if (!selected.isEmpty()) {
        gameDirectoryEdit_->setText(selected);
        saveSettings();
        loadLocalProfiles();
        refreshMods();
        updatePlayButton();
    }
}

void MainWindow::browseJava()
{
    const QString selected = QFileDialog::getOpenFileName(
        this, QStringLiteral("Выберите java.exe"), javaEdit_->text(),
#ifdef Q_OS_WIN
        QStringLiteral("Java (java.exe);;Все файлы (*.*)")
#else
        QStringLiteral("Java;Все файлы (*)")
#endif
    );
    if (!selected.isEmpty()) {
        javaEdit_->setText(selected);
        saveSettings();
    }
}

void MainWindow::openGameDirectory()
{
    const QString directory = gameDirectoryEdit_->text().trimmed();
    if (directory.isEmpty()) {
        return;
    }
    QDir().mkpath(directory);
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

void MainWindow::openModsDirectory()
{
    const QString directory = QDir(gameDirectoryEdit_->text().trimmed()).filePath(QStringLiteral("mods"));
    QDir().mkpath(directory);
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

void MainWindow::loadServers()
{
    QSettings settings;
    serverNames_.clear();
    serverAddresses_.clear();
    serverPorts_.clear();
    const int count = qMax(0, settings.value(QStringLiteral("servers/count"), 0).toInt());
    for (int index = 0; index < count; ++index) {
        QString address = settings.value(QStringLiteral("servers/%1/address").arg(index)).toString().trimmed();
        if (address.isEmpty() || address.size() > 255 || address.contains(QRegularExpression(QStringLiteral("\\s")))) {
            continue;
        }
        QString name = settings.value(QStringLiteral("servers/%1/name").arg(index)).toString().trimmed();
        if (name.isEmpty()) {
            name = address;
        }
        const int port = qBound(1, settings.value(QStringLiteral("servers/%1/port").arg(index), 25565).toInt(), 65535);
        serverNames_.append(name);
        serverAddresses_.append(address);
        serverPorts_.append(port);
    }

    serversList_->blockSignals(true);
    serversList_->clear();
    for (int index = 0; index < serverNames_.size(); ++index) {
        serversList_->addItem(QStringLiteral("%1  —  %2:%3")
                                  .arg(serverNames_.at(index), serverAddresses_.at(index))
                                  .arg(serverPorts_.at(index)));
    }
    serversList_->setCurrentRow(-1);
    serversList_->blockSignals(false);
    serverNameEdit_->clear();
    serverAddressBookEdit_->clear();
    serverPortBookSpin_->setValue(25565);
}

void MainWindow::saveServers() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("servers/count"), serverNames_.size());
    for (int index = 0; index < serverNames_.size(); ++index) {
        settings.setValue(QStringLiteral("servers/%1/name").arg(index), serverNames_.at(index));
        settings.setValue(QStringLiteral("servers/%1/address").arg(index), serverAddresses_.at(index));
        settings.setValue(QStringLiteral("servers/%1/port").arg(index), serverPorts_.at(index));
    }
}

void MainWindow::serverSelectionChanged(int row)
{
    if (row < 0 || row >= serverNames_.size()) {
        serverNameEdit_->clear();
        serverAddressBookEdit_->clear();
        serverPortBookSpin_->setValue(25565);
        return;
    }
    serverNameEdit_->setText(serverNames_.at(row));
    serverAddressBookEdit_->setText(serverAddresses_.at(row));
    serverPortBookSpin_->setValue(serverPorts_.at(row));
}

void MainWindow::addOrUpdateServer()
{
    QString name = serverNameEdit_->text().trimmed();
    const QString address = serverAddressBookEdit_->text().trimmed();
    if (address.isEmpty() || address.size() > 255 || address.contains(QRegularExpression(QStringLiteral("\\s")))
        || address.contains(QLatin1Char('/')) || address.contains(QLatin1Char('\\'))) {
        QMessageBox::warning(this, QStringLiteral("Сервер"),
                             QStringLiteral("Введите корректный адрес без пробелов, протокола и пути."));
        return;
    }
    if (name.isEmpty()) {
        name = address;
    }

    int row = serversList_->currentRow();
    if (row >= 0 && row < serverNames_.size()) {
        serverNames_[row] = name;
        serverAddresses_[row] = address;
        serverPorts_[row] = serverPortBookSpin_->value();
    } else {
        row = serverNames_.size();
        serverNames_.append(name);
        serverAddresses_.append(address);
        serverPorts_.append(serverPortBookSpin_->value());
    }

    serversList_->blockSignals(true);
    serversList_->clear();
    for (int index = 0; index < serverNames_.size(); ++index) {
        serversList_->addItem(QStringLiteral("%1  —  %2:%3")
                                  .arg(serverNames_.at(index), serverAddresses_.at(index))
                                  .arg(serverPorts_.at(index)));
    }
    serversList_->setCurrentRow(row);
    serversList_->blockSignals(false);
    serverSelectionChanged(row);
    saveSettings();
    statusLabel_->setText(QStringLiteral("Сохранён сервер «%1». Добавьте его в быстрое подключение кнопкой ниже.")
                              .arg(name));
}

void MainWindow::removeServer()
{
    const int row = serversList_->currentRow();
    if (row < 0 || row >= serverNames_.size()) {
        QMessageBox::information(this, QStringLiteral("Серверы"),
                                 QStringLiteral("Выберите сервер для удаления."));
        return;
    }
    const QString name = serverNames_.at(row);
    if (QMessageBox::question(this, QStringLiteral("Удалить сервер?"),
                              QStringLiteral("Удалить запись «%1»? Файлы игры не изменятся.").arg(name))
        != QMessageBox::Yes) {
        return;
    }
    serverNames_.removeAt(row);
    serverAddresses_.removeAt(row);
    serverPorts_.removeAt(row);
    serversList_->blockSignals(true);
    serversList_->clear();
    for (int index = 0; index < serverNames_.size(); ++index) {
        serversList_->addItem(QStringLiteral("%1  —  %2:%3")
                                  .arg(serverNames_.at(index), serverAddresses_.at(index))
                                  .arg(serverPorts_.at(index)));
    }
    const int next = serverNames_.isEmpty() ? -1 : qMin(row, serverNames_.size() - 1);
    serversList_->setCurrentRow(next);
    serversList_->blockSignals(false);
    serverSelectionChanged(next);
    saveSettings();
    statusLabel_->setText(QStringLiteral("Сервер удалён."));
}

void MainWindow::useSelectedServer()
{
    const int row = serversList_->currentRow();
    if (row < 0 || row >= serverNames_.size()) {
        QMessageBox::information(this, QStringLiteral("Серверы"),
                                 QStringLiteral("Выберите сервер из адресной книги."));
        return;
    }
    serverAddressEdit_->setText(serverAddresses_.at(row));
    serverPortSpin_->setValue(serverPorts_.at(row));
    connectServerCheck_->setChecked(true);
    saveSettings();
    statusLabel_->setText(QStringLiteral("Быстрое подключение настроено: %1.").arg(serverNames_.at(row)));
    if (tabs_) {
        tabs_->setCurrentIndex(0);
    }
}

void MainWindow::refreshCrashReports()
{
    if (!crashReportsList_ || !crashReportEdit_) {
        return;
    }
    crashReportPaths_.clear();
    crashReportsList_->clear();
    crashReportEdit_->clear();

    const QString gameDir = gameDirectoryEdit_ ? gameDirectoryEdit_->text().trimmed() : QString();
    if (gameDir.isEmpty()) {
        return;
    }
    QFileInfoList reports;
    const QDir crashDirectory(QDir(gameDir).filePath(QStringLiteral("crash-reports")));
    reports.append(crashDirectory.entryInfoList({QStringLiteral("*.txt")},
                                                 QDir::Files | QDir::Readable, QDir::Time));
    const QDir gameDirectory(gameDir);
    reports.append(gameDirectory.entryInfoList({QStringLiteral("hs_err_pid*.log"),
                                                QStringLiteral("replay_pid*.log")},
                                               QDir::Files | QDir::Readable, QDir::Time));
    std::sort(reports.begin(), reports.end(), [](const QFileInfo &left, const QFileInfo &right) {
        return left.lastModified() > right.lastModified();
    });

    for (const QFileInfo &report : reports) {
        const QString path = report.absoluteFilePath();
        crashReportPaths_.append(path);
        const QString timestamp = report.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        auto *item = new QListWidgetItem(QStringLiteral("%1  %2").arg(timestamp, report.fileName()));
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        crashReportsList_->addItem(item);
    }
    if (crashReportsList_->count() == 0) {
        auto *item = new QListWidgetItem(QStringLiteral("Локальные отчёты не найдены."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        crashReportsList_->addItem(item);
        return;
    }
    crashReportsList_->setCurrentRow(0);
}

void MainWindow::showCrashReport(int row)
{
    if (!crashReportEdit_ || row < 0 || row >= crashReportsList_->count()) {
        if (crashReportEdit_) {
            crashReportEdit_->clear();
        }
        return;
    }
    const QString path = crashReportsList_->item(row)->data(Qt::UserRole).toString();
    if (path.isEmpty()) {
        crashReportEdit_->clear();
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        crashReportEdit_->setPlainText(QStringLiteral("Не удалось открыть отчёт: %1").arg(file.errorString()));
        return;
    }
    constexpr qint64 kMaximumReportSize = 8 * 1024 * 1024;
    QByteArray contents = file.read(kMaximumReportSize + 1);
    const bool truncated = contents.size() > kMaximumReportSize;
    if (truncated) {
        contents.truncate(kMaximumReportSize);
    }
    QString text = QString::fromUtf8(contents);
    if (text.isEmpty() && !contents.isEmpty()) {
        text = QString::fromLocal8Bit(contents);
    }
    if (truncated) {
        text += QStringLiteral("\\n\\n[Отчёт обрезан после 8 МБ]");
    }
    crashReportEdit_->setPlainText(text);
}

void MainWindow::openCrashReportsDirectory()
{
    const QString directory = QDir(gameDirectoryEdit_->text().trimmed()).filePath(QStringLiteral("crash-reports"));
    QDir().mkpath(directory);
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

void MainWindow::syncCurrentInstance()
{
    if (activeInstanceIndex_ >= 0 && activeInstanceIndex_ < instanceDirectories_.size()
        && gameDirectoryEdit_) {
        instanceDirectories_[activeInstanceIndex_] = gameDirectoryEdit_->text().trimmed();
    }
}

void MainWindow::loadInstances()
{
    QSettings settings;
    instanceNames_.clear();
    instanceDirectories_.clear();
    const int count = settings.value(QStringLiteral("instances/count"), 0).toInt();
    if (count <= 0) {
        instanceNames_.append(QStringLiteral("По умолчанию"));
        instanceDirectories_.append(gameDirectoryEdit_->text().trimmed().isEmpty()
                                        ? Paths::defaultGameDir()
                                        : gameDirectoryEdit_->text().trimmed());
    } else {
        for (int index = 0; index < count; ++index) {
            const QString name = settings.value(QStringLiteral("instances/%1/name").arg(index)).toString();
            const QString directory = settings.value(QStringLiteral("instances/%1/directory").arg(index)).toString();
            if (!name.isEmpty() && !directory.isEmpty()) {
                instanceNames_.append(name);
                instanceDirectories_.append(directory);
            }
        }
        if (instanceNames_.isEmpty()) {
            instanceNames_.append(QStringLiteral("По умолчанию"));
            instanceDirectories_.append(Paths::defaultGameDir());
        }
    }

    instanceCombo_->blockSignals(true);
    instanceCombo_->clear();
    instanceCombo_->addItems(instanceNames_);
    int selected = settings.value(QStringLiteral("instances/active"), 0).toInt();
    selected = qBound(0, selected, instanceNames_.size() - 1);
    instanceCombo_->setCurrentIndex(selected);
    instanceCombo_->blockSignals(false);
    activeInstanceIndex_ = selected;
    gameDirectoryEdit_->setText(instanceDirectories_.at(activeInstanceIndex_));
}

void MainWindow::saveInstances() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("instances/count"), instanceNames_.size());
    for (int index = 0; index < instanceNames_.size(); ++index) {
        settings.setValue(QStringLiteral("instances/%1/name").arg(index), instanceNames_.at(index));
        settings.setValue(QStringLiteral("instances/%1/directory").arg(index), instanceDirectories_.at(index));
    }
    settings.setValue(QStringLiteral("instances/active"), activeInstanceIndex_);
}

void MainWindow::instanceChanged(int index)
{
    if (index < 0 || index >= instanceDirectories_.size()) {
        return;
    }
    syncCurrentInstance();
    activeInstanceIndex_ = index;
    gameDirectoryEdit_->setText(instanceDirectories_.at(index));
    // Remove loader profiles that belonged to the previous instance, then rescan.
    for (int itemIndex = versionCombo_->count() - 1; itemIndex >= 0; --itemIndex) {
        const QString id = versionCombo_->itemData(itemIndex).toString();
        if (!id.isEmpty() && versionUrls_.value(id).isEmpty()) {
            versionCombo_->removeItem(itemIndex);
        }
    }
    loadLocalProfiles();
    refreshMods();
    refreshCrashReports();
    saveSettings();
    updatePlayButton();
}

void MainWindow::addInstance()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Новый инстанс"),
                                                QStringLiteral("Название:"), QLineEdit::Normal,
                                                QStringLiteral("Новый мир"), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    syncCurrentInstance();
    QString safeName = name;
    safeName.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
    const QString baseDirectory = QDir(gameDirectoryEdit_->text().trimmed()).absolutePath();
    const QString directory = QDir(baseDirectory).filePath(QStringLiteral("instances/%1").arg(safeName));
    instanceNames_.append(name);
    instanceDirectories_.append(directory);
    instanceCombo_->blockSignals(true);
    instanceCombo_->addItem(name);
    const int newIndex = instanceCombo_->count() - 1;
    instanceCombo_->setCurrentIndex(newIndex);
    instanceCombo_->blockSignals(false);
    instanceChanged(newIndex);
    saveSettings();
}

void MainWindow::removeInstance()
{
    if (instanceNames_.size() <= 1 || activeInstanceIndex_ < 0) {
        QMessageBox::information(this, QStringLiteral("Инстансы"),
                                 QStringLiteral("Нельзя удалить единственный инстанс."));
        return;
    }
    const QString name = instanceNames_.at(activeInstanceIndex_);
    if (QMessageBox::question(this, QStringLiteral("Удалить инстанс?"),
                              QStringLiteral("Папка с файлами не будет удалена. Удалить только запись «%1»?").arg(name))
        != QMessageBox::Yes) {
        return;
    }
    syncCurrentInstance();
    const int removed = activeInstanceIndex_;
    instanceNames_.removeAt(removed);
    instanceDirectories_.removeAt(removed);
    instanceCombo_->blockSignals(true);
    instanceCombo_->removeItem(removed);
    const int next = qBound(0, removed, instanceNames_.size() - 1);
    instanceCombo_->setCurrentIndex(next);
    instanceCombo_->blockSignals(false);
    activeInstanceIndex_ = -1;
    instanceChanged(next);
    saveSettings();
}

void MainWindow::loadLocalProfiles()
{
    const QString gameDir = gameDirectoryEdit_->text().trimmed();
    if (gameDir.isEmpty() || !versionCombo_) {
        return;
    }
    const QDir versionsDirectory(QDir(gameDir).filePath(QStringLiteral("versions")));
    const QFileInfoList directories = versionsDirectory.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &directory : directories) {
        const QString id = directory.fileName();
        const QString jsonPath = QDir(directory.absoluteFilePath()).filePath(id + QStringLiteral(".json"));
        const QJsonDocument document = readJsonFile(jsonPath);
        if (!document.isObject()
            || document.object().value(QStringLiteral("inheritsFrom")).toString().isEmpty()
            || versionCombo_->findData(id) >= 0) {
            continue;
        }
        QString label = id;
        if (id.startsWith(QStringLiteral("fabric-loader-"))) {
            label = QStringLiteral("Fabric · %1").arg(id);
        } else if (id.startsWith(QStringLiteral("forge-"))) {
            label = QStringLiteral("Forge · %1").arg(id);
        } else if (id.startsWith(QStringLiteral("quilt-loader-"))) {
            label = QStringLiteral("Quilt · %1").arg(id);
        }
        versionCombo_->addItem(label, id);
    }
}

void MainWindow::refreshMods()
{
    if (!modsList_) {
        return;
    }
    modsList_->clear();
    const QDir modsDirectory(QDir(gameDirectoryEdit_ ? gameDirectoryEdit_->text().trimmed() : QString())
                                 .filePath(QStringLiteral("mods")));
    const QFileInfoList files = modsDirectory.entryInfoList(
        {QStringLiteral("*.jar"), QStringLiteral("*.zip")}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        auto *item = new QListWidgetItem(QStringLiteral("Моды не найдены"));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        modsList_->addItem(item);
        return;
    }
    for (const QFileInfo &file : files) {
        modsList_->addItem(QStringLiteral("%1   (%2 МБ)")
                               .arg(file.fileName())
                               .arg(QString::number(file.size() / (1024.0 * 1024.0), 'f', 1)));
    }
}

void MainWindow::appendLog(const QString &message)
{
    if (!logEdit_ || message.trimmed().isEmpty()) {
        return;
    }
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("[HH:mm:ss] "));
    logEdit_->appendPlainText(timestamp + message.trimmed());
}

void MainWindow::setBusy(bool busy)
{
    versionCombo_->setEnabled(!busy && !versionUrls_.isEmpty());
    installButton_->setEnabled(!busy);
    fabricButton_->setEnabled(!busy && !fabricThread_ && !forgeThread_ && !quiltThread_ && !installThread_ && !modrinthInstallThread_
                              && !selectedVersionUrl().isEmpty());
    forgeButton_->setEnabled(!busy && !fabricThread_ && !forgeThread_ && !quiltThread_ && !installThread_ && !modrinthInstallThread_
                             && !selectedVersionUrl().isEmpty());
    quiltButton_->setEnabled(!busy && !fabricThread_ && !forgeThread_ && !quiltThread_ && !installThread_ && !modrinthInstallThread_
                             && !selectedVersionUrl().isEmpty());
    if (busy) {
        playButton_->setEnabled(false);
    } else {
        updatePlayButton();
    }
}

QString MainWindow::selectedVersionId() const
{
    if (!versionCombo_ || versionCombo_->currentIndex() < 0) {
        return {};
    }
    return versionCombo_->currentData().toString();
}

QString MainWindow::selectedVersionUrl() const
{
    return versionUrls_.value(selectedVersionId());
}

bool MainWindow::isVersionInstalled(const QString &versionId) const
{
    if (versionId.isEmpty()) {
        return false;
    }
    const QString directory = Paths::versionDir(gameDirectoryEdit_->text().trimmed(), versionId);
    const QString jsonPath = QDir(directory).filePath(versionId + QStringLiteral(".json"));
    if (!QFileInfo::exists(jsonPath)) {
        return false;
    }
    if (QFileInfo::exists(QDir(directory).filePath(versionId + QStringLiteral(".jar")))) {
        return true;
    }

    QString error;
    const QJsonDocument profile = readJsonFile(jsonPath, &error);
    const QString parent = profile.isObject()
                               ? profile.object().value(QStringLiteral("inheritsFrom")).toString()
                               : QString();
    return !parent.isEmpty() && isVersionInstalled(parent);
}

void MainWindow::updatePlayButton()
{
    const bool canPlay = !installThread_ && !fabricThread_ && !forgeThread_ && !quiltThread_ && !modrinthInstallThread_
                         && gameProcess_ == nullptr
                         && authenticator_ && authenticator_->account().isAuthenticated()
                         && isVersionInstalled(selectedVersionId());
    playButton_->setEnabled(canPlay);
}

} // namespace mcl
