#include <QApplication>
#include <QIcon>
#include <QSize>
#include <clocale>
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

// Loads every pre-rendered resolution (see resources.qrc / img/icon/) into
// one QIcon, instead of a single large image Qt would have to downscale on
// the fly: window/taskbar/Alt-Tab each pick whichever size renders sharpest.
QIcon AppIcon() {
  QIcon icon;
  for (int size : {16, 32, 48, 64, 128, 256, 512}) {
    icon.addFile(QStringLiteral(":/icons/icon-%1.png").arg(size), QSize(size, size));
  }
  return icon;
}

}  // namespace

int main(int argc, char** argv) {
  qInstallMessageHandler(FlushingMessageHandler);

  QApplication app(argc, argv);
  // On Unix, QApplication's constructor calls setlocale(LC_ALL, ""), which
  // on any system whose locale uses a decimal comma (it_IT, de_DE, ...)
  // makes mpv_create() refuse to run ("Non-C locale detected"): libmpv
  // parses floating-point option values with the C library and requires
  // LC_NUMERIC to be "C". Must come after QApplication (which would undo
  // it) and before MainWindow creates the player. Qt's own number
  // formatting goes through QLocale, not the C locale, so the UI is
  // unaffected.
  std::setlocale(LC_NUMERIC, "C");
  app.setApplicationName("VCD Player");
  app.setApplicationVersion(QStringLiteral(VCDPLAYER_VERSION));
  app.setOrganizationName("VCD Player");
  app.setWindowIcon(AppIcon());

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
