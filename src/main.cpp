#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    biem::ui::MainWindow window;
    window.resize(1100, 700);
    window.show();
    return app.exec();
}
