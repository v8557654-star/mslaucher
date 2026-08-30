#include "main_window.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MCLauncher"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("mclauncher.local"));
    QCoreApplication::setApplicationName(QStringLiteral("MCLauncher"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    mcl::MainWindow window;
    window.show();
    return application.exec();
}
