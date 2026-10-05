#include "PerformanceMainWindow.h"

#include <QApplication>
#include <cstring>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--self-test") == 0) {
            return 0;
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("BDFR Performance Studio");
    app.setOrganizationName("BDFR");

    bdfrpc::studio::PerformanceMainWindow window;
    window.show();
    return app.exec();
}
