#include "application/ProjectApplicationService.h"
#include "presentation/MainWindow.h"

#include <QApplication>
#include <QMessageBox>

#include <exception>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("NovelRelationAnalyzer"));
    QCoreApplication::setApplicationName(
        QStringLiteral("小说人物关系分析系统"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    try {
        novel::application::ProjectApplicationService service;
        novel::presentation::MainWindow window(service);
        window.show();
        return application.exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(
            nullptr, QObject::tr("启动失败"),
            QObject::tr("应用程序无法启动：%1")
                .arg(QString::fromUtf8(error.what())));
        return 1;
    }
}
