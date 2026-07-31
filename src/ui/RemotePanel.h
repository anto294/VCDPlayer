#pragma once

#include <QWidget>

#include "../core/PbcEngine.h"

class QLabel;
class QPushButton;
class QSlider;

// Collapsible remote panel: dpad, numeric keypad, transport controls,
// volume/mute, fullscreen. Zoom and screenshot are deliberately not
// duplicated here — they stay in the menu bar/context menu (with keyboard
// shortcuts), keeping this panel focused on disc navigation. Contains no
// playback logic: it emits signals that MainWindow forwards to
// DiscSession/MpvPlayer, and receives state updates through its public
// set*() methods.
class RemotePanel : public QWidget {
  Q_OBJECT

 public:
  explicit RemotePanel(QWidget* parent = nullptr);

  void setDiscInfo(const QString& title, const QString& status);
  void setStatus(const QString& status);
  void setMuted(bool muted);
  void setVolume(int value);  // 0..100, does not re-emit volumeChanged

  // Resets every stored widget's text/tooltip via tr() for the
  // now-installed language. Called by MainWindow after switching languages.
  void retranslateUi();

 signals:
  void openDiscRequested();
  void remoteButtonPressed(vcd::RemoteButton btn);
  void numberPressed(int digit);
  void muteToggled();
  void volumeChanged(int value);
  void fullscreenToggled();

 private:
  QLabel* discTitle_;
  QLabel* discStatus_;
  QPushButton* openButton_;
  QPushButton* dpadUp_;
  QPushButton* dpadDown_;
  QPushButton* playButton_;
  QPushButton* pauseButton_;
  QPushButton* stopButton_;
  QPushButton* muteButton_;
  QSlider* volumeSlider_;
  QPushButton* fullscreenButton_;

  bool hasDisc_ = false;
  bool muted_ = false;
};
