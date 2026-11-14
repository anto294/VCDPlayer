#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <cstdint>

// Keeps the screen on (no screensaver, screen lock or display sleep) while
// a video is actually playing. mpv does this itself only for the windows it
// creates; with the render API the app owns the window, so it's on us.
//
// - Linux/BSD: org.freedesktop.ScreenSaver over D-Bus (KDE, Xfce, LXQt,
//   ...), falling back to the XDG desktop portal's Inhibit (GNOME,
//   sandboxes).
// - Windows: SetThreadExecutionState.
// - macOS: an IOKit power assertion.
class SleepInhibitor : public QObject {
  Q_OBJECT

 public:
  explicit SleepInhibitor(QObject* parent = nullptr);
  ~SleepInhibitor() override;
  SleepInhibitor(const SleepInhibitor&) = delete;
  SleepInhibitor& operator=(const SleepInhibitor&) = delete;

  // Turning it on takes effect at once; turning it off waits a few seconds,
  // so the brief idle moments between two items, or while seeking, don't
  // release and re-acquire the inhibition each time.
  void setActive(bool active);

 private:
  void acquire();
  void release();

  QTimer releaseDelay_;
  bool held_ = false;
#if defined(Q_OS_MACOS)
  uint32_t assertionId_ = 0;
#elif !defined(Q_OS_WIN)
  uint32_t screenSaverCookie_ = 0;  // org.freedesktop.ScreenSaver
  QString portalRequest_;           // XDG portal Inhibit request handle
#endif
};
