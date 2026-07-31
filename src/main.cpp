#include <QApplication>
#include <QIcon>

#include <cstdio>

#include "ui/MainWindow.h"

namespace {

// stderr is fully buffered when it isn't a terminal (e.g. redirected to a
// file for debugging): without an explicit flush, log lines stay trapped
// in the buffer if the process is terminated abruptly. Useful because,
// since the app has no console subsystem of its own (VCDPlayer WIN32 in
// CMakeLists.txt), qDebug()/qWarning() would otherwise be invisible unless
// explicitly redirected while diagnosing an issue.
void FlushingMessageHandler(QtMsgType, const QMessageLogContext&, const QString& msg) {
  std::fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
  std::fflush(stderr);
}

}  // namespace

int main(int argc, char** argv) {
  qInstallMessageHandler(FlushingMessageHandler);

  QApplication app(argc, argv);
  app.setApplicationName("VCD Player");
  app.setOrganizationName("VCD Player");
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/icon.png")));

  MainWindow window;
  window.show();

  if (argc > 1) {
    // Enables `VCDPlayer.exe path\to\disc.cue` (drag&drop from Explorer,
    // "open with", or manual command-line launch).
    QMetaObject::invokeMethod(
        &window, [&window, path = QString::fromLocal8Bit(argv[1])]() { window.openDiscPath(path); },
        Qt::QueuedConnection);
  }

  return app.exec();
}
