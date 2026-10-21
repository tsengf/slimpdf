#include <QApplication>
#include "main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("SlimPDF");
    app.setApplicationVersion(QStringLiteral(SLIMPDF_VERSION));
    app.setOrganizationName("slimpdf");

    MainWindow w;
    w.show();

    if (argc > 1)
        w.loadFile(QString::fromLocal8Bit(argv[1]));

    return app.exec();
}
