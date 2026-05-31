#include <QApplication>

#include "studio/main_window.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  studio::MainWindow main_window;
  main_window.show();

  return app.exec();
}
