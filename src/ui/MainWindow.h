#pragma once

#include <QMainWindow>

#include "../core/PbcEngine.h"

class QAction;
class QDragEnterEvent;
class QDropEvent;
class QKeyEvent;
class QLabel;
class QMenu;
class QPushButton;
class QResizeEvent;
class QSlider;
class QTranslator;

class DiscSession;
class MpvPlayer;
class RemotePanel;
class VideoWidget;

// Main window: menu bar, video area + progress bar, collapsible remote
// panel, VLC-style context menu, drag&drop of .cue files. Pure
// orchestration: the real logic lives in MpvPlayer (rendering/mpv
// commands) and DiscSession (disc parsing + PBC state machine).
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  // Opens a disc given a .cue path passed from the command line (drag&drop
  // onto the .exe, "open with", manual launch). Must be called after show(),
  // not from the constructor, so any error shows up in a QMessageBox on top
  // of an already-visible window.
  void openDiscPath(const QString& cuePath);

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  enum class ZoomDirection { In, Out, Reset };

  void buildMenuBar();
  void buildFileMenu();
  void buildPlaybackMenu();
  void buildAudioMenu();
  void buildViewMenu();
  void buildLanguageMenu();
  void buildAdvancedMenu();
  void buildExperimentalMenu();
  void buildInfoMenu();
  QMenu* buildContextMenu();
  void showContextMenuAt(const QPoint& globalPos);

  void openDiscDialog();
  void sendRemoteButton(vcd::RemoteButton btn, int number = 0);
  // Space bar: Play if paused/stopped/ended, Pause if playing.
  void togglePlayPause();

  void applyZoom(ZoomDirection direction);
  void toggleFullscreen();
  void toggleAlwaysOnTop();
  void toggleMute();
  void toggleRemotePanel();
  void setShowProgressBar(bool show);
  void setShowTimeLabel(bool show);
  void takeScreenshot();
  void showAboutDialog();

  // "auto"/"software"/"hardware", persisted and applied on next launch —
  // switching the render backend live mid-session isn't worth the risk
  // (see MpvPlayer::initRenderContext()), so this just saves the choice and
  // tells the user to restart.
  void setRenderBackendSetting(const QString& value);

  // Unlike the render backend itself, this applies immediately — it's just
  // an mpv option (glsl-shaders), not something that needs the render
  // context rebuilt — but only has any visible effect while the Hardware
  // (OpenGL) backend is active, since it hooks into mpv's GPU shader
  // pipeline (see shaders/README.md and MpvPlayer::initRenderContext()).
  void setAiUpscalingEnabled(bool enabled);

  // Also applies immediately. mpv's software renderer already scales with
  // Lanczos + full chroma interpolation by default (about as good as
  // libswscale itself gets); this layers a sharpening pass on top (sws-ls/
  // sws-cs), which is what actually helps a source stretched by as large a
  // factor as VCD's 352x288 usually is. Only affects the Software backend.
  void setCpuUpscalingEnabled(bool enabled);

  // Switches the active UI language ("en"/"it"), persists the choice, and
  // rebuilds every translatable widget in place — no restart required.
  void setLanguage(const QString& languageCode);
  void retranslateUi();

  void applyViewSettingsToUi();
  void setRemoteVisible(bool visible);
  void updateOverlayPositions();
  void updateWindowTitle();
  void onMpvPropertyChanged(const QString& name, const QVariant& value);

  MpvPlayer* player_;
  DiscSession* session_;
  VideoWidget* video_;
  RemotePanel* remote_;
  QWidget* toggleRailSpacer_;  // slot for panelToggleButton_ between video and panel; hidden when panel is collapsed so
                               // the video reclaims that width
  QWidget* progressRow_;
  QSlider* seekBar_;
  QLabel* timeCurrentLabel_;
  QLabel* timeDurationLabel_;
  QPushButton* panelToggleButton_;

  QAction* fullscreenAction_ = nullptr;
  QAction* alwaysOnTopAction_ = nullptr;
  QAction* muteAction_ = nullptr;
  QAction* showProgressBarAction_ = nullptr;
  QAction* showTimeLabelAction_ = nullptr;
  QAction* showRemoteAction_ = nullptr;
  QAction* aiUpscalingAction_ = nullptr;
  QAction* cpuUpscalingAction_ = nullptr;

  bool showRemote_ = true;
  bool showProgressBarSetting_ = false;
  bool showTimeLabelSetting_ = false;

  QString currentDiscTitle_;
  QString currentDiscFileName_;  // basename of the opened .cue, for the window title
  double lastDuration_ = 0.0;
  bool seekDragging_ = false;

  QString currentLanguage_ = QStringLiteral("en");
  QTranslator* translator_ = nullptr;

  QString renderBackendSetting_ = QStringLiteral("auto");
  bool aiUpscalingEnabled_ = false;
  bool cpuUpscalingEnabled_ = false;
};
