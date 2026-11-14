#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProxyStyle>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTranslator>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <vector>

#include "../app/DiscSession.h"
#include "../mpv/MpvPlayer.h"
#include "RemotePanel.h"
#include "VideoWidget.h"

namespace {

constexpr int kSeekBarMax = 1000;
constexpr int kWindowMargin = 24;
constexpr int kLayoutSpacing = 10;
constexpr int kPanelWidth = 224;      // remote panel column (its scroll area's fixed width)
constexpr int kToggleRailWidth = 36;  // permanent slot for panelToggleButton_, see updateOverlayPositions()

// mpv's software renderer defaults to zimg for the actual scale step
// (sws-allow-zimg=yes by default) when it's compiled in, as it is here —
// and zimg has no equivalent to libswscale's sharpen params, so sws-ls/
// sws-cs below are silently ignored as long as zimg is the one doing the
// work (found by reading video/sws_utils.c's allow_zimg()/mp_sws_reinit()
// after the toggle visibly did nothing). Turning zimg off for these two
// options to have any effect at all is a real quality trade-off — zimg is
// generally the nicer scaler — but that's the whole point of this being an
// explicit, opt-in "sharpen mode" rather than always-on.
// The sharpen values themselves counter the softness a large upscale
// factor (VCD's 352x288 source, stretched to whatever the window ends up)
// tends to leave behind; luma sharpened more than chroma is the usual
// balance, since sharpening chroma harder introduces color fringing before
// it looks any crisper.
constexpr double kCpuUpscaleLumaSharpen = 0.5;
constexpr double kCpuUpscaleChromaSharpen = 0.3;

QString formatTime(double seconds) {
  const int s = static_cast<int>(std::max(0.0, std::floor(seconds)));
  return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

// GPU upscaling presets for Experimental > GPU upscaling: each is a chain
// of mpv user shaders from shaders/ (sources, versions and licenses in
// shaders/README.md). They only take effect with the Hardware (OpenGL)
// render backend, which runs mpv's GPU shader pipeline. Only shaders that
// work with the classic "gpu" renderer behind libmpv's render API qualify:
// ArtCNN (crashes it with an assertion) and NVIDIA Image Scaling (uses
// //!PARAM, which only gpu-next parses) were tried and left out.
struct UpscalerPreset {
  const char* id;     // persisted in the "upscaler" setting
  const char* label;  // menu text, translated in buildExperimentalMenu()
  QStringList files;  // shader chain, applied in this order
};

const std::vector<UpscalerPreset>& UpscalerPresets() {
  static const std::vector<UpscalerPreset> kPresets = {
      {"off", QT_TRANSLATE_NOOP("MainWindow", "Off"), {}},
      {"anime4k",
       QT_TRANSLATE_NOOP("MainWindow", "Anime4K — cartoons (restore + upscale)"),
       {QStringLiteral("Anime4K_Clamp_Highlights.glsl"), QStringLiteral("Anime4K_Restore_CNN_M.glsl"),
        QStringLiteral("Anime4K_Upscale_CNN_x2_M.glsl")}},
      {"fsrcnnx",
       QT_TRANSLATE_NOOP("MainWindow", "FSRCNNX — live action"),
       {QStringLiteral("FSRCNNX_x2_8-0-4-1.glsl")}},
      {"ravu", QT_TRANSLATE_NOOP("MainWindow", "RAVU — general purpose"), {QStringLiteral("ravu-lite-ar-r3.hook")}},
      {"fsr", QT_TRANSLATE_NOOP("MainWindow", "AMD FSR — fast"), {QStringLiteral("FSR.glsl")}},
      {"cas", QT_TRANSLATE_NOOP("MainWindow", "AMD CAS — sharpening only"), {QStringLiteral("CAS-scaled.glsl")}},
  };
  return kPresets;
}

// mpv's glsl-shaders value for a preset: full paths, joined with the
// platform's path-list separator (':' on Unix, ';' on Windows). Shaders are
// plain files next to the executable (see the CMakeLists.txt copy step),
// not Qt resources: mpv reads them by path and knows nothing of :/ paths.
QString UpscalerShaderList(const QString& id) {
  QStringList paths;
  for (const UpscalerPreset& p : UpscalerPresets()) {
    if (id != QLatin1String(p.id)) continue;
    for (const QString& f : p.files) paths << QCoreApplication::applicationDirPath() + QStringLiteral("/shaders/") + f;
  }
  return paths.join(QDir::listSeparator());
}

// Keeps a single child locked to a 4:3 box, centered within whatever space
// the layout gives this container. video_ used to simply fill its layout
// cell, with mpv pillarboxing/letterboxing the picture *inside* that
// (arbitrarily-shaped) black-bordered rect; wrapping it in this box instead
// moves that shaping to the Qt widget itself, so the visible frame always
// matches the VCD picture's own 4:3 shape rather than floating inside a
// bigger box.
class AspectRatioBox : public QWidget {
 public:
  AspectRatioBox(QWidget* content, QWidget* parent) : QWidget(parent), content_(content) { content_->setParent(this); }

 protected:
  void resizeEvent(QResizeEvent*) override {
    constexpr double kAspect = 4.0 / 3.0;
    int targetW = width();
    int targetH = static_cast<int>(std::round(targetW / kAspect));
    if (targetH > height()) {
      targetH = height();
      targetW = static_cast<int>(std::round(targetH * kAspect));
    }
    content_->setGeometry((width() - targetW) / 2, (height() - targetH) / 2, targetW, targetH);
  }

 private:
  QWidget* content_;
};

// Makes a left click on a QSlider's groove jump the handle straight to the
// clicked spot (and start a drag from there), instead of Qt's default of
// stepping one page towards it: for a seek bar, clicking somewhere means
// "go there". Uses the application style as its base without owning it.
class AbsoluteClickSliderStyle : public QProxyStyle {
 public:
  int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                QStyleHintReturn* returnData) const override {
    if (hint == QStyle::SH_Slider_AbsoluteSetButtons) return Qt::LeftButton;
    return QProxyStyle::styleHint(hint, option, widget, returnData);
  }
};

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  // Install the saved language's translator before building any widget, so
  // the very first buildMenuBar()/RemotePanel construction below already
  // produces the right-language text instead of flashing English first.
  // First launch: follow the system language when we ship a translation
  // for it (Italian), English otherwise. An explicit choice from the
  // Language menu is saved and wins from then on.
  const QString systemDefault =
      QLocale::system().language() == QLocale::Italian ? QStringLiteral("it") : QStringLiteral("en");
  currentLanguage_ = QSettings().value(QStringLiteral("language"), systemDefault).toString();
  if (currentLanguage_ != QStringLiteral("en")) {
    translator_ = new QTranslator(this);
    if (translator_->load(QStringLiteral(":/i18n/vcdplayer_%1.qm").arg(currentLanguage_))) {
      qApp->installTranslator(translator_);
    } else {
      delete translator_;
      translator_ = nullptr;
      currentLanguage_ = QStringLiteral("en");
    }
  }

  updateWindowTitle();
  // Preferred size (tall enough for the whole remote panel without
  // scrolling), shrunk to fit small screens: a fixed 1155x700 minimum
  // didn't fit e.g. a 1366x768 laptop at 125% scaling (~1093x614 logical
  // pixels). The real minimum now comes from the layout itself (the video
  // box's own minimum; the remote panel scrolls when it doesn't fit).
  QSize initialSize(1155, 760);
  if (const QScreen* screen = QGuiApplication::primaryScreen()) {
    initialSize = initialSize.boundedTo(screen->availableGeometry().size() * 0.9);
  }
  resize(initialSize);
  // Restores the last session's size/position (and maximized state) over
  // the default above; a no-op on first launch.
  restoreGeometry(QSettings().value(QStringLiteral("window/geometry")).toByteArray());
  setAcceptDrops(true);

  renderBackendSetting_ = QSettings().value(QStringLiteral("renderBackend"), QStringLiteral("auto")).toString();
  // View toggles persist across launches. Progress bar and time default to
  // on: without them there's no visible position or way to seek at all.
  showRemote_ = QSettings().value(QStringLiteral("view/remotePanel"), true).toBool();
  showProgressBarSetting_ = QSettings().value(QStringLiteral("view/progressBar"), true).toBool();
  showTimeLabelSetting_ = QSettings().value(QStringLiteral("view/timeLabel"), true).toBool();

  player_ = new MpvPlayer(this);
  player_->setPropertyDouble("volume", QSettings().value(QStringLiteral("volume"), 100.0).toDouble());
  // "Automatic" is resolved once here, at startup, rather than re-probed
  // every launch inside MpvPlayer itself — keeps the (GL-context-creating)
  // detection probe next to the rest of the Advanced-settings UI instead of
  // buried in the render backend it's picking between.
  MpvPlayer::RenderBackend backend = MpvPlayer::RenderBackend::Software;
  if (renderBackendSetting_ == QStringLiteral("hardware")) {
    backend = MpvPlayer::RenderBackend::OpenGL;
  } else if (renderBackendSetting_ == QStringLiteral("auto")) {
    backend = MpvPlayer::detectBestRenderBackend();
  }
  player_->setPreferredRenderBackend(backend);

  // Just an mpv option (see setUpscaler()), so it's safe to set
  // straight away regardless of which backend ends up active — it only
  // does anything once the GPU shader pipeline (Hardware/OpenGL backend)
  // is actually in use. Set directly rather than through
  // setUpscaler() so startup doesn't pop the "requires Hardware
  // rendering" notice on every launch when the setting is on but Automatic
  // resolved to Software.
  // "aiUpscaling" was the old on/off Anime4K toggle: carry it over.
  const QString legacyDefault = QSettings().value(QStringLiteral("aiUpscaling"), false).toBool()
                                    ? QStringLiteral("anime4k")
                                    : QStringLiteral("off");
  upscaler_ = QSettings().value(QStringLiteral("upscaler"), legacyDefault).toString();
  if (upscaler_ != QStringLiteral("off")) player_->setPropertyString("glsl-shaders", UpscalerShaderList(upscaler_));

  // Same idea as above, but for the Software backend's sharpen-on-top-of-
  // Lanczos pass (see setCpuUpscalingEnabled()): applied directly here to
  // skip the "wrong backend active" notice on every launch.
  cpuUpscalingEnabled_ = QSettings().value(QStringLiteral("cpuUpscaling"), false).toBool();
  if (cpuUpscalingEnabled_) {
    player_->setPropertyFlag("sws-allow-zimg", false);
    player_->setPropertyDouble("sws-ls", kCpuUpscaleLumaSharpen);
    player_->setPropertyDouble("sws-cs", kCpuUpscaleChromaSharpen);
  }

  session_ = new DiscSession(player_, this);

  const QString screenshotDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/VCDPlayer";
  QDir().mkpath(screenshotDir);
  player_->setPropertyString("screenshot-directory", screenshotDir);

  auto* central = new QWidget(this);
  setCentralWidget(central);
  // Video and panel sit side by side, never overlapping — the video always
  // gets every pixel not spoken for by the panel or its toggle button.
  // toggleRailSpacer_ reserves a slot for panelToggleButton_ between the
  // video and the panel while the panel is open; it's hidden along with the
  // panel when collapsed (see setRemoteVisible()) so the video reclaims that
  // width instead of leaving it as dead space. Window margins are equal on
  // both sides, so with no rail reserved the video area is symmetric on its
  // own — and mpv always centers the actual (4:3) picture within whatever
  // rect video_ ends up with — so the picture stays centered too, without
  // needing a separate balancing widget.
  auto* rootLayout = new QHBoxLayout(central);
  rootLayout->setContentsMargins(kWindowMargin, kWindowMargin, kWindowMargin, kWindowMargin);
  rootLayout->setSpacing(kLayoutSpacing);

  auto* mainArea = new QWidget(central);
  auto* mainAreaLayout = new QVBoxLayout(mainArea);
  mainAreaLayout->setContentsMargins(0, 0, 0, 0);
  mainAreaLayout->setSpacing(10);

  video_ = new VideoWidget(player_);
  video_->setStyleSheet("background: #000; border: 1px solid #2a2d36; border-radius: 4px;");
  auto* videoBox = new AspectRatioBox(video_, mainArea);
  videoBox->setMinimumSize(160, 120);  // 4:3, matches AspectRatioBox's own ratio
  mainAreaLayout->addWidget(videoBox, 1);

  progressRow_ = new QWidget(mainArea);
  auto* progressLayout = new QHBoxLayout(progressRow_);
  progressLayout->setContentsMargins(0, 0, 0, 0);
  progressLayout->setSpacing(10);
  timeCurrentLabel_ = new QLabel(QStringLiteral("00:00"), progressRow_);
  timeCurrentLabel_->setStyleSheet("font-size: 12px; color: #b7b9c2;");
  seekBar_ = new QSlider(Qt::Horizontal, progressRow_);
  seekBar_->setRange(0, kSeekBarMax);
  // Click-to-seek: the handle jumps to the click and the existing
  // sliderReleased handler below performs the seek, as after a drag.
  auto* seekBarStyle = new AbsoluteClickSliderStyle;
  seekBarStyle->setParent(seekBar_);
  seekBar_->setStyle(seekBarStyle);
  timeDurationLabel_ = new QLabel(QStringLiteral("00:00"), progressRow_);
  timeDurationLabel_->setStyleSheet("font-size: 12px; color: #b7b9c2;");
  progressLayout->addWidget(timeCurrentLabel_);
  progressLayout->addWidget(seekBar_, 1);
  progressLayout->addWidget(timeDurationLabel_);
  mainAreaLayout->addWidget(progressRow_, 0);

  rootLayout->addWidget(mainArea, 1);

  toggleRailSpacer_ = new QWidget(central);
  toggleRailSpacer_->setFixedWidth(kToggleRailWidth);
  toggleRailSpacer_->setAttribute(Qt::WA_TransparentForMouseEvents);
  rootLayout->addWidget(toggleRailSpacer_, 0);

  remote_ = new RemotePanel(central);
  // The panel's full height (~650 px) used to be the window's minimum
  // height, too tall for small or scaled screens. Inside a scroll area it
  // fills the column normally and only scrolls when the window is shorter
  // than it. The column width stays fixed; the panel itself takes whatever
  // the viewport leaves next to a scroll bar.
  remoteScroll_ = new QScrollArea(central);
  remoteScroll_->setWidget(remote_);
  remoteScroll_->setWidgetResizable(true);
  remoteScroll_->setFrameShape(QFrame::NoFrame);
  remoteScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  remoteScroll_->setFixedWidth(kPanelWidth);
  rootLayout->addWidget(remoteScroll_, 0);

  panelToggleButton_ = new QPushButton(central);
  panelToggleButton_->setFixedSize(28, 51);
  panelToggleButton_->setCursor(Qt::PointingHandCursor);
  // Floats on top of toggleRailSpacer_'s reserved slot while the panel is
  // open, or hugs the window's right margin once it's collapsed (see
  // updateOverlayPositions()), so it reads as its own control rather than a
  // tab fused flush onto either side.
  panelToggleButton_->setStyleSheet(
      "QPushButton { background: #262932; color: #e8e8ec; border: 1px solid #43485a;"
      "  border-radius: 10px; font-size: 14px; font-weight: 600; padding: 0; }"
      "QPushButton:hover { background: #313543; border-color: #5a6178; }");
  panelToggleButton_->raise();

  buildMenuBar();

  connect(video_, &VideoWidget::doubleClicked, this, &MainWindow::toggleFullscreen);
  connect(video_, &VideoWidget::rightClicked, this, &MainWindow::showContextMenuAt);

  connect(panelToggleButton_, &QPushButton::clicked, this, &MainWindow::toggleRemotePanel);

  connect(remote_, &RemotePanel::openDiscRequested, this, &MainWindow::openDiscDialog);
  connect(remote_, &RemotePanel::remoteButtonPressed, this, [this](vcd::RemoteButton btn) { sendRemoteButton(btn); });
  connect(remote_, &RemotePanel::numberPressed, this,
          [this](int digit) { sendRemoteButton(vcd::RemoteButton::Number, digit); });
  connect(remote_, &RemotePanel::muteToggled, this, &MainWindow::toggleMute);
  connect(remote_, &RemotePanel::volumeChanged, this,
          [this](int value) { player_->setPropertyDouble("volume", value); });
  connect(remote_, &RemotePanel::fullscreenToggled, this, &MainWindow::toggleFullscreen);

  // Holding the handle (or, with click-to-seek, the spot just clicked)
  // must stop time-pos updates from moving it until the release seeks.
  connect(seekBar_, &QSlider::sliderPressed, this, [this]() { seekDragging_ = true; });
  connect(seekBar_, &QSlider::sliderMoved, this, [this](int value) {
    seekDragging_ = true;
    if (lastDuration_ > 0) timeCurrentLabel_->setText(formatTime((value / double(kSeekBarMax)) * lastDuration_));
  });
  connect(seekBar_, &QSlider::sliderReleased, this, [this]() {
    if (lastDuration_ > 0) {
      const double secs = (seekBar_->value() / double(kSeekBarMax)) * lastDuration_;
      player_->command({"seek", QString::number(secs, 'f', 3), "absolute"});
    }
    seekDragging_ = false;
  });

  connect(player_, &MpvPlayer::propertyChanged, this, &MainWindow::onMpvPropertyChanged);
  connect(player_, &MpvPlayer::logMessage, this, [](const QString& text) { qWarning().noquote() << text; });

  connect(session_, &DiscSession::discLoaded, this, [this](const QString& volumeId, bool pbcEnabled, int chapters) {
    currentDiscTitle_ = volumeId.isEmpty() ? tr("Disc") : volumeId;
    const QString status = pbcEnabled ? tr("PBC active · %1 chapters").arg(chapters)
                                      : tr("Sequential playback · %1 chapters").arg(chapters);
    remote_->setDiscInfo(currentDiscTitle_, status);
  });
  connect(session_, &DiscSession::stateChanged, this, [this](int kind, int choiceCount, int firstChoice) {
    using Kind = vcd::PbcEngine::State::Kind;
    QString status;
    switch (static_cast<Kind>(kind)) {
      case Kind::End:
        status = tr("End of disc");
        break;
      case Kind::Menu:
        status = tr("Menu — choose %1–%2 with the number keys").arg(firstChoice).arg(firstChoice + choiceCount - 1);
        break;
      case Kind::Stopped:
        status = tr("Stopped");
        break;
      case Kind::Playing:
        status = tr("Playing");
        break;
      case Kind::Loading:
        return;
    }
    remote_->setDiscInfo(currentDiscTitle_, status);
  });

  applyViewSettingsToUi();
}

void MainWindow::buildMenuBar() {
  buildFileMenu();
  buildPlaybackMenu();
  buildAudioMenu();
  buildViewMenu();
  buildLanguageMenu();
  buildAdvancedMenu();
  buildExperimentalMenu();
  buildInfoMenu();

  // Qt only fires a QAction's shortcut while the action belongs to a
  // visible widget, and fullscreen hides the menu bar: Space, F11, M,
  // Ctrl+arrows etc. all went dead until fullscreen was left. Adding the
  // shortcut-bearing actions to the window itself keeps them live. (They
  // die with the menu bar on retranslateUi(), which drops them from here.)
  for (QAction* action : menuBar()->findChildren<QAction*>()) {
    if (!action->shortcut().isEmpty()) addAction(action);
  }

  applyMenuBarColors();
}

void MainWindow::applyMenuBarColors() {
  // Breeze (KDE) gives the menu bar its own "Header" colours from the KDE
  // colour scheme. When that scheme can't be read (AppImage/sandbox, a
  // different XDG config dir, ...) it falls back to light Breeze header
  // colours: dark text, while the bar itself still shows the window's dark
  // background - so the menus became unreadable. Pin the bar to the
  // application palette, the one every other widget here already uses.
  const QPalette p = QApplication::palette();
  menuBar()->setStyleSheet(
      QStringLiteral("QMenuBar { background: %1; color: %2; }"
                     "QMenuBar::item { background: transparent; padding: 4px 8px; }"
                     "QMenuBar::item:selected, QMenuBar::item:pressed { background: %3; color: %4; }")
          .arg(p.color(QPalette::Window).name(), p.color(QPalette::WindowText).name(),
               p.color(QPalette::Highlight).name(), p.color(QPalette::HighlightedText).name()));
}

void MainWindow::changeEvent(QEvent* event) {
  // Follow a system theme switch (light <-> dark) at runtime.
  if (event->type() == QEvent::ApplicationPaletteChange) applyMenuBarColors();
  QMainWindow::changeEvent(event);
}

void MainWindow::buildFileMenu() {
  auto* fileMenu = menuBar()->addMenu(tr("&File"));
  QAction* openAction = fileMenu->addAction(tr("Open disc…"));
  openAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
  connect(openAction, &QAction::triggered, this, &MainWindow::openDiscDialog);
  fileMenu->addSeparator();
  QAction* quitAction = fileMenu->addAction(tr("Exit"));
  connect(quitAction, &QAction::triggered, this, &MainWindow::close);
}

void MainWindow::buildPlaybackMenu() {
  auto* playbackMenu = menuBar()->addMenu(tr("&Playback"));
  QAction* playPauseAction = playbackMenu->addAction(tr("Play/Pause"));
  playPauseAction->setShortcut(QKeySequence(Qt::Key_Space));
  connect(playPauseAction, &QAction::triggered, this, &MainWindow::togglePlayPause);
  QAction* playAction = playbackMenu->addAction(tr("Play"));
  connect(playAction, &QAction::triggered, this, [this]() { sendRemoteButton(vcd::RemoteButton::Play); });
  QAction* pauseAction = playbackMenu->addAction(tr("Pause"));
  connect(pauseAction, &QAction::triggered, this, [this]() { sendRemoteButton(vcd::RemoteButton::Pause); });
  QAction* stopAction = playbackMenu->addAction(tr("Stop"));
  connect(stopAction, &QAction::triggered, this, [this]() { sendRemoteButton(vcd::RemoteButton::Stop); });
  playbackMenu->addSeparator();
  QAction* nextAction = playbackMenu->addAction(tr("Next chapter"));
  nextAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Right")));
  connect(nextAction, &QAction::triggered, this, [this]() { sendRemoteButton(vcd::RemoteButton::Next); });
  QAction* prevAction = playbackMenu->addAction(tr("Previous chapter"));
  prevAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Left")));
  connect(prevAction, &QAction::triggered, this, [this]() { sendRemoteButton(vcd::RemoteButton::Prev); });
  QAction* menuAction = playbackMenu->addAction(tr("Disc menu"));
  menuAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Up")));
  connect(menuAction, &QAction::triggered, this, [this]() { sendRemoteButton(vcd::RemoteButton::Return); });
}

void MainWindow::buildAudioMenu() {
  auto* audioMenu = menuBar()->addMenu(tr("&Audio"));
  QAction* volUpAction = audioMenu->addAction(tr("Volume up"));
  volUpAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Up")));
  connect(volUpAction, &QAction::triggered, this, [this]() {
    const double v = std::min(100.0, player_->getProperty("volume").toDouble() + 5);
    player_->setPropertyDouble("volume", v);
  });
  QAction* volDownAction = audioMenu->addAction(tr("Volume down"));
  volDownAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Down")));
  connect(volDownAction, &QAction::triggered, this, [this]() {
    const double v = std::max(0.0, player_->getProperty("volume").toDouble() - 5);
    player_->setPropertyDouble("volume", v);
  });
  audioMenu->addSeparator();
  muteAction_ = audioMenu->addAction(tr("Mute"));
  muteAction_->setCheckable(true);
  muteAction_->setShortcut(QKeySequence(QStringLiteral("M")));
  connect(muteAction_, &QAction::triggered, this, &MainWindow::toggleMute);
}

void MainWindow::buildViewMenu() {
  auto* viewMenu = menuBar()->addMenu(tr("&View"));
  QAction* screenshotAction = viewMenu->addAction(tr("Take screenshot"));
  screenshotAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
  connect(screenshotAction, &QAction::triggered, this, &MainWindow::takeScreenshot);
  viewMenu->addSeparator();
  fullscreenAction_ = viewMenu->addAction(tr("Fullscreen"));
  fullscreenAction_->setCheckable(true);
  fullscreenAction_->setShortcut(QKeySequence(Qt::Key_F11));
  connect(fullscreenAction_, &QAction::triggered, this, &MainWindow::toggleFullscreen);
  alwaysOnTopAction_ = viewMenu->addAction(tr("Always on top"));
  alwaysOnTopAction_->setCheckable(true);
  connect(alwaysOnTopAction_, &QAction::triggered, this, &MainWindow::toggleAlwaysOnTop);
  viewMenu->addSeparator();
  auto* zoomMenu = viewMenu->addMenu(tr("Zoom"));
  QAction* zoomInAction = zoomMenu->addAction(tr("Zoom in"));
  zoomInAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+=")));
  connect(zoomInAction, &QAction::triggered, this, [this]() { applyZoom(ZoomDirection::In); });
  QAction* zoomOutAction = zoomMenu->addAction(tr("Zoom out"));
  zoomOutAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+-")));
  connect(zoomOutAction, &QAction::triggered, this, [this]() { applyZoom(ZoomDirection::Out); });
  QAction* zoomResetAction = zoomMenu->addAction(tr("Reset zoom"));
  zoomResetAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
  connect(zoomResetAction, &QAction::triggered, this, [this]() { applyZoom(ZoomDirection::Reset); });
  viewMenu->addSeparator();
  showProgressBarAction_ = viewMenu->addAction(tr("Progress bar"));
  showProgressBarAction_->setCheckable(true);
  showProgressBarAction_->setChecked(showProgressBarSetting_);
  connect(showProgressBarAction_, &QAction::triggered, this, &MainWindow::setShowProgressBar);
  showTimeLabelAction_ = viewMenu->addAction(tr("Time label"));
  showTimeLabelAction_->setCheckable(true);
  showTimeLabelAction_->setChecked(showTimeLabelSetting_);
  connect(showTimeLabelAction_, &QAction::triggered, this, &MainWindow::setShowTimeLabel);
  viewMenu->addSeparator();
  showRemoteAction_ = viewMenu->addAction(tr("Remote panel"));
  showRemoteAction_->setCheckable(true);
  showRemoteAction_->setChecked(showRemote_);
  connect(showRemoteAction_, &QAction::triggered, this, &MainWindow::toggleRemotePanel);
}

void MainWindow::buildLanguageMenu() {
  auto* languageMenu = menuBar()->addMenu(tr("&Language"));
  auto* languageGroup = new QActionGroup(languageMenu);
  languageGroup->setExclusive(true);
  QAction* englishAction = languageMenu->addAction(QStringLiteral("English"));
  englishAction->setCheckable(true);
  englishAction->setChecked(currentLanguage_ == QStringLiteral("en"));
  languageGroup->addAction(englishAction);
  connect(englishAction, &QAction::triggered, this, [this]() { setLanguage(QStringLiteral("en")); });
  QAction* italianAction = languageMenu->addAction(QStringLiteral("Italiano"));
  italianAction->setCheckable(true);
  italianAction->setChecked(currentLanguage_ == QStringLiteral("it"));
  languageGroup->addAction(italianAction);
  connect(italianAction, &QAction::triggered, this, [this]() { setLanguage(QStringLiteral("it")); });
}

void MainWindow::buildAdvancedMenu() {
  auto* advancedMenu = menuBar()->addMenu(tr("&Advanced"));
  auto* renderingMenu = advancedMenu->addMenu(tr("Rendering"));
  auto* renderingGroup = new QActionGroup(renderingMenu);
  renderingGroup->setExclusive(true);
  QAction* renderAutoAction = renderingMenu->addAction(tr("Automatic (recommended)"));
  renderAutoAction->setCheckable(true);
  renderAutoAction->setChecked(renderBackendSetting_ == QStringLiteral("auto"));
  renderingGroup->addAction(renderAutoAction);
  connect(renderAutoAction, &QAction::triggered, this, [this]() { setRenderBackendSetting(QStringLiteral("auto")); });
  QAction* renderSwAction = renderingMenu->addAction(tr("Software"));
  renderSwAction->setCheckable(true);
  renderSwAction->setChecked(renderBackendSetting_ == QStringLiteral("software"));
  renderingGroup->addAction(renderSwAction);
  connect(renderSwAction, &QAction::triggered, this, [this]() { setRenderBackendSetting(QStringLiteral("software")); });
  QAction* renderHwAction = renderingMenu->addAction(tr("Hardware (GPU)"));
  renderHwAction->setCheckable(true);
  renderHwAction->setChecked(renderBackendSetting_ == QStringLiteral("hardware"));
  renderingGroup->addAction(renderHwAction);
  connect(renderHwAction, &QAction::triggered, this, [this]() { setRenderBackendSetting(QStringLiteral("hardware")); });
}

void MainWindow::buildExperimentalMenu() {
  auto* experimentalMenu = menuBar()->addMenu(tr("&Experimental"));
  auto* upscalingMenu = experimentalMenu->addMenu(tr("GPU upscaling"));
  auto* upscalingGroup = new QActionGroup(upscalingMenu);
  upscalingGroup->setExclusive(true);
  for (const UpscalerPreset& preset : UpscalerPresets()) {
    QAction* action = upscalingMenu->addAction(tr(preset.label));
    action->setCheckable(true);
    action->setChecked(upscaler_ == QLatin1String(preset.id));
    upscalingGroup->addAction(action);
    const QString id = QString::fromLatin1(preset.id);
    connect(action, &QAction::triggered, this, [this, id]() { setUpscaler(id); });
    if (id == QStringLiteral("off")) upscalingMenu->addSeparator();
  }
  cpuUpscalingAction_ = experimentalMenu->addAction(tr("CPU upscaling (sharpen, no GPU needed)"));
  cpuUpscalingAction_->setCheckable(true);
  cpuUpscalingAction_->setChecked(cpuUpscalingEnabled_);
  connect(cpuUpscalingAction_, &QAction::triggered, this, &MainWindow::setCpuUpscalingEnabled);
}

void MainWindow::buildInfoMenu() {
  auto* infoMenu = menuBar()->addMenu(tr("&Info"));
  QAction* aboutAction = infoMenu->addAction(tr("About VCD Player"));
  connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

QMenu* MainWindow::buildContextMenu() {
  // Rebuilt on every open (VLC-style) so the checkboxes always reflect the
  // current state, independent of the menu bar.
  auto* menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);

  connect(menu->addAction(tr("Play")), &QAction::triggered, this,
          [this]() { sendRemoteButton(vcd::RemoteButton::Play); });
  connect(menu->addAction(tr("Pause")), &QAction::triggered, this,
          [this]() { sendRemoteButton(vcd::RemoteButton::Pause); });
  connect(menu->addAction(tr("Stop")), &QAction::triggered, this,
          [this]() { sendRemoteButton(vcd::RemoteButton::Stop); });
  menu->addSeparator();
  connect(menu->addAction(tr("Next chapter")), &QAction::triggered, this,
          [this]() { sendRemoteButton(vcd::RemoteButton::Next); });
  connect(menu->addAction(tr("Previous chapter")), &QAction::triggered, this,
          [this]() { sendRemoteButton(vcd::RemoteButton::Prev); });
  connect(menu->addAction(tr("Disc menu")), &QAction::triggered, this,
          [this]() { sendRemoteButton(vcd::RemoteButton::Return); });
  menu->addSeparator();

  auto* audioMenu = menu->addMenu(tr("Audio"));
  connect(audioMenu->addAction(tr("Volume up")), &QAction::triggered, this, [this]() {
    const double v = std::min(100.0, player_->getProperty("volume").toDouble() + 5);
    player_->setPropertyDouble("volume", v);
  });
  connect(audioMenu->addAction(tr("Volume down")), &QAction::triggered, this, [this]() {
    const double v = std::max(0.0, player_->getProperty("volume").toDouble() - 5);
    player_->setPropertyDouble("volume", v);
  });
  audioMenu->addSeparator();
  QAction* muteItem = audioMenu->addAction(tr("Mute"));
  muteItem->setCheckable(true);
  muteItem->setChecked(player_->getProperty("mute").toBool());
  connect(muteItem, &QAction::triggered, this, &MainWindow::toggleMute);
  menu->addSeparator();

  connect(menu->addAction(tr("Take screenshot")), &QAction::triggered, this, &MainWindow::takeScreenshot);
  menu->addSeparator();

  QAction* fsItem = menu->addAction(tr("Fullscreen"));
  fsItem->setCheckable(true);
  fsItem->setChecked(isFullScreen());
  connect(fsItem, &QAction::triggered, this, &MainWindow::toggleFullscreen);
  QAction* aotItem = menu->addAction(tr("Always on top"));
  aotItem->setCheckable(true);
  aotItem->setChecked(windowFlags().testFlag(Qt::WindowStaysOnTopHint));
  connect(aotItem, &QAction::triggered, this, &MainWindow::toggleAlwaysOnTop);

  auto* zoomMenu = menu->addMenu(tr("Zoom"));
  connect(zoomMenu->addAction(tr("Zoom in")), &QAction::triggered, this, [this]() { applyZoom(ZoomDirection::In); });
  connect(zoomMenu->addAction(tr("Zoom out")), &QAction::triggered, this, [this]() { applyZoom(ZoomDirection::Out); });
  connect(zoomMenu->addAction(tr("Reset zoom")), &QAction::triggered, this,
          [this]() { applyZoom(ZoomDirection::Reset); });
  menu->addSeparator();

  QAction* progressItem = menu->addAction(tr("Progress bar"));
  progressItem->setCheckable(true);
  progressItem->setChecked(showProgressBarSetting_);
  connect(progressItem, &QAction::triggered, this, &MainWindow::setShowProgressBar);
  QAction* timeItem = menu->addAction(tr("Time label"));
  timeItem->setCheckable(true);
  timeItem->setChecked(showTimeLabelSetting_);
  connect(timeItem, &QAction::triggered, this, &MainWindow::setShowTimeLabel);
  menu->addSeparator();
  QAction* remoteItem = menu->addAction(tr("Remote panel"));
  remoteItem->setCheckable(true);
  remoteItem->setChecked(showRemote_);
  connect(remoteItem, &QAction::triggered, this, &MainWindow::toggleRemotePanel);
  menu->addSeparator();

  connect(menu->addAction(tr("Open disc…")), &QAction::triggered, this, &MainWindow::openDiscDialog);
  menu->addSeparator();
  connect(menu->addAction(tr("Exit")), &QAction::triggered, this, &MainWindow::close);

  return menu;
}

void MainWindow::showContextMenuAt(const QPoint& globalPos) { buildContextMenu()->popup(globalPos); }

void MainWindow::contextMenuEvent(QContextMenuEvent* event) { showContextMenuAt(event->globalPos()); }

void MainWindow::openDiscDialog() {
  const QString path =
      QFileDialog::getOpenFileName(this, tr("Open VCD disc (.cue)"), QString(), tr("CUE files (*.cue)"));
  if (!path.isEmpty()) openDiscPath(path);
}

void MainWindow::openDiscPath(const QString& cuePath) {
  remote_->setStatus(tr("Loading…"));
  QString error;
  if (session_->openDiscPath(cuePath, &error)) {
    currentDiscFileName_ = QFileInfo(cuePath).fileName();
  } else {
    currentDiscFileName_.clear();
    remote_->setStatus(QStringLiteral("—"));
    QMessageBox::critical(this, tr("Error opening disc"), error);
  }
  updateWindowTitle();
}

void MainWindow::sendRemoteButton(vcd::RemoteButton btn, int number) { session_->sendRemoteButton(btn, number); }

void MainWindow::togglePlayPause() {
  const bool resume = session_->isIdle() || player_->getProperty("pause").toBool();
  sendRemoteButton(resume ? vcd::RemoteButton::Play : vcd::RemoteButton::Pause);
}

void MainWindow::applyZoom(ZoomDirection direction) {
  switch (direction) {
    case ZoomDirection::Reset:
      player_->setPropertyDouble("video-zoom", 0);
      break;
    case ZoomDirection::In:
      player_->command({"add", "video-zoom", "0.1"});
      break;
    case ZoomDirection::Out:
      player_->command({"add", "video-zoom", "-0.1"});
      break;
  }
}

void MainWindow::toggleFullscreen() {
  const bool goingFullscreen = !isFullScreen();
  if (goingFullscreen) {
    showFullScreen();
  } else {
    showNormal();
  }
  menuBar()->setVisible(!goingFullscreen);
  panelToggleButton_->setVisible(!goingFullscreen);
  setRemoteVisible(!goingFullscreen && showRemote_);
  progressRow_->setVisible(!goingFullscreen && (showProgressBarSetting_ || showTimeLabelSetting_));
  if (fullscreenAction_) fullscreenAction_->setChecked(goingFullscreen);
}

void MainWindow::toggleAlwaysOnTop() {
  const bool newState = !windowFlags().testFlag(Qt::WindowStaysOnTopHint);
  setWindowFlag(Qt::WindowStaysOnTopHint, newState);
  show();
  if (alwaysOnTopAction_) alwaysOnTopAction_->setChecked(newState);
}

void MainWindow::toggleMute() {
  const bool newState = !player_->getProperty("mute").toBool();
  player_->setPropertyFlag("mute", newState);
}

void MainWindow::toggleRemotePanel() {
  showRemote_ = !showRemote_;
  QSettings().setValue(QStringLiteral("view/remotePanel"), showRemote_);
  if (showRemoteAction_) showRemoteAction_->setChecked(showRemote_);
  applyViewSettingsToUi();
}

void MainWindow::setShowProgressBar(bool show) {
  showProgressBarSetting_ = show;
  QSettings().setValue(QStringLiteral("view/progressBar"), show);
  if (showProgressBarAction_) showProgressBarAction_->setChecked(show);
  applyViewSettingsToUi();
}

void MainWindow::setShowTimeLabel(bool show) {
  showTimeLabelSetting_ = show;
  QSettings().setValue(QStringLiteral("view/timeLabel"), show);
  if (showTimeLabelAction_) showTimeLabelAction_->setChecked(show);
  applyViewSettingsToUi();
}

void MainWindow::takeScreenshot() {
  player_->command({"screenshot"});
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/VCDPlayer";
  statusBar()->showMessage(tr("Screenshot saved to %1").arg(dir), 3000);
}

void MainWindow::showAboutDialog() {
  QMessageBox box(this);
  box.setWindowTitle(tr("About VCD Player"));
  box.setIconPixmap(windowIcon().pixmap(64, 64));
  box.setTextFormat(Qt::RichText);
  box.setText(tr("<h3>VCD Player</h3>"
                 "<p>A native Video CD player with full PBC (Playback Control) menu support.</p>"
                 "<p>Built with Qt %1 and libmpv.</p>"
                 "<p>Experimental GPU upscaling uses third-party mpv shaders: Anime4K, FSRCNNX, RAVU and AMD "
                 "FidelityFX FSR/CAS (MIT and LGPL-3.0 licensed; see shaders/README.md).</p>")
                  .arg(QString::fromLatin1(QT_VERSION_STR)));
  QPushButton* reportButton = box.addButton(tr("Report an issue"), QMessageBox::ActionRole);
  box.addButton(QMessageBox::Ok);
  box.exec();
  if (box.clickedButton() == reportButton) {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/anto294/VCDPlayer/issues")));
  }
}

void MainWindow::setRenderBackendSetting(const QString& value) {
  if (value == renderBackendSetting_) return;
  renderBackendSetting_ = value;
  QSettings().setValue(QStringLiteral("renderBackend"), value);
  QMessageBox::information(this, tr("Restart required"),
                           tr("The new rendering setting will take effect the next time VCD Player starts."));
}

void MainWindow::setUpscaler(const QString& id) {
  if (id == upscaler_) return;
  upscaler_ = id;
  QSettings().setValue(QStringLiteral("upscaler"), id);
  // A plain mpv option, applied to the already-running instance right away
  // — no restart needed, unlike the render backend itself.
  player_->setPropertyString("glsl-shaders", UpscalerShaderList(id));
  if (id != QStringLiteral("off") && player_->renderBackend() != MpvPlayer::RenderBackend::OpenGL) {
    QMessageBox::information(
        this, tr("GPU upscaling"),
        tr("GPU upscaling only has an effect with the Hardware (GPU) rendering backend, which isn't active right "
           "now. Switch to it from Advanced > Rendering and restart VCD Player."));
  }
}

void MainWindow::setCpuUpscalingEnabled(bool enabled) {
  cpuUpscalingEnabled_ = enabled;
  QSettings().setValue(QStringLiteral("cpuUpscaling"), enabled);
  // zimg (on by default, see the constants' comment above) has to be turned
  // off for sws-ls/sws-cs to have any effect at all — it's what actually
  // does the scaling otherwise, and it has no sharpen-equivalent option of
  // its own.
  player_->setPropertyFlag("sws-allow-zimg", !enabled);
  player_->setPropertyDouble("sws-ls", enabled ? kCpuUpscaleLumaSharpen : 0.0);
  player_->setPropertyDouble("sws-cs", enabled ? kCpuUpscaleChromaSharpen : 0.0);
  // Without this, mpv's SW backend just keeps using the scaler it built
  // before this toggle — it only reconfigures on a target-size change, not
  // on these options changing (see refreshSoftwareScaler()) — so the change
  // above would otherwise silently do nothing until the window is resized.
  player_->refreshSoftwareScaler();
  // refreshSoftwareScaler() only primes mpv to reconfigure on the *next*
  // render() call — during active playback that happens within one frame
  // on its own, but paused/idle nothing repaints VideoWidget until
  // something else does, which reads as "the toggle did nothing" even
  // though it took effect internally. Forcing one paintEvent now makes it
  // visible immediately regardless of play state.
  video_->update();
  if (enabled && player_->renderBackend() == MpvPlayer::RenderBackend::OpenGL) {
    QMessageBox::information(
        this, tr("CPU upscaling"),
        tr("CPU upscaling only affects mpv's Software rendering path; the Hardware (GPU) backend is active right "
           "now, so this has no visible effect until you switch back to Software from Advanced > Rendering."));
  }
}

void MainWindow::setLanguage(const QString& languageCode) {
  if (languageCode == currentLanguage_) return;
  currentLanguage_ = languageCode;
  QSettings().setValue(QStringLiteral("language"), currentLanguage_);

  if (translator_) {
    qApp->removeTranslator(translator_);
    delete translator_;
    translator_ = nullptr;
  }
  if (currentLanguage_ != QStringLiteral("en")) {
    translator_ = new QTranslator(this);
    if (translator_->load(QStringLiteral(":/i18n/vcdplayer_%1.qm").arg(currentLanguage_))) {
      qApp->installTranslator(translator_);
    } else {
      delete translator_;
      translator_ = nullptr;
    }
  }

  retranslateUi();
}

void MainWindow::retranslateUi() {
  // installTranslator() only affects tr() calls made from now on: widgets
  // built earlier keep their old-language text until we explicitly rebuild
  // them. The menu bar is cheap to throw away and recreate (setMenuBar()
  // deletes the previous one); RemotePanel instead exposes its own
  // retranslateUi() that resets each stored widget's text in place.
  setMenuBar(new QMenuBar(this));
  buildMenuBar();
  fullscreenAction_->setChecked(isFullScreen());
  alwaysOnTopAction_->setChecked(windowFlags().testFlag(Qt::WindowStaysOnTopHint));
  muteAction_->setChecked(player_->getProperty("mute").toBool());
  showProgressBarAction_->setChecked(showProgressBarSetting_);
  showTimeLabelAction_->setChecked(showTimeLabelSetting_);
  showRemoteAction_->setChecked(showRemote_);

  remote_->retranslateUi();
  updateWindowTitle();
}

void MainWindow::applyViewSettingsToUi() {
  if (!isFullScreen()) {
    setRemoteVisible(showRemote_);
    progressRow_->setVisible(showProgressBarSetting_ || showTimeLabelSetting_);
  }
  seekBar_->setVisible(showProgressBarSetting_);
  timeCurrentLabel_->setVisible(showTimeLabelSetting_);
  timeDurationLabel_->setVisible(showTimeLabelSetting_);
  panelToggleButton_->setText(showRemote_ ? QStringLiteral("›") : QStringLiteral("‹"));
  updateOverlayPositions();
}

void MainWindow::setRemoteVisible(bool visible) {
  remoteScroll_->setVisible(visible);
  toggleRailSpacer_->setVisible(visible);
}

void MainWindow::updateOverlayPositions() {
  auto* central = centralWidget();
  if (!central) return;

  // panelToggleButton_ isn't layout-managed (it has to stay clickable even
  // while the panel it controls is hidden), so its position is derived from
  // the same constants that drive rootLayout instead of the panel's actual
  // geometry — cheaper than forcing a layout activation and just as exact.
  const int btnW = panelToggleButton_->width();
  const int btnH = panelToggleButton_->height();
  int btnX;
  if (showRemote_) {
    // Centered within toggleRailSpacer_'s reserved slot between video and panel.
    const int railX = central->width() - kWindowMargin - kPanelWidth - kLayoutSpacing - kToggleRailWidth;
    btnX = railX + (kToggleRailWidth - btnW) / 2;
  } else {
    // No rail reserved — the video reclaims that width — so the button just
    // hugs the window's right margin, only lightly overlapping the video's
    // own edge (it doesn't fit inside the margin alone), instead of sitting
    // deep inside it like before.
    btnX = central->width() - btnW - kWindowMargin / 4;
  }
  // Vertically centered on the video's row: mainArea spans the same height
  // as central here (the toggle rail/panel columns beside it don't add
  // margins of their own), so central's own vertical center lines up with
  // it, progress row or not.
  const int btnY = central->height() / 2 - btnH / 2;
  panelToggleButton_->setGeometry(btnX, btnY, btnW, btnH);
  panelToggleButton_->raise();
}

void MainWindow::updateWindowTitle() {
  const QString suffix = currentDiscFileName_.isEmpty() ? tr("No disc") : currentDiscFileName_;
  setWindowTitle(QStringLiteral("VCD Player - %1").arg(suffix));
}

void MainWindow::onMpvPropertyChanged(const QString& name, const QVariant& value) {
  if (name == "time-pos") {
    if (seekDragging_) return;
    const double seconds = value.toDouble();
    timeCurrentLabel_->setText(formatTime(seconds));
    if (lastDuration_ > 0) {
      const QSignalBlocker blocker(seekBar_);
      seekBar_->setValue(static_cast<int>(std::round((seconds / lastDuration_) * kSeekBarMax)));
    }
  } else if (name == "duration") {
    lastDuration_ = value.toDouble();
    timeDurationLabel_->setText(formatTime(lastDuration_));
  } else if (name == "volume") {
    remote_->setVolume(value.toInt());
  } else if (name == "mute") {
    const bool muted = value.toBool();
    remote_->setMuted(muted);
    if (muteAction_) muteAction_->setChecked(muted);
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  QSettings settings;
  settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
  settings.setValue(QStringLiteral("volume"), player_->getProperty("volume").toDouble());
  QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  updateOverlayPositions();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
  // Qt reports numeric-keypad keys with Qt::KeypadModifier set even though
  // they're otherwise plain digit/arrow/+−keys (e.g. Numpad "5" is Key_5 +
  // KeypadModifier): masking it out of the comparison is what makes the
  // keypad work as a second set of remote-control keys, not just the keys
  // above the letters.
  const bool noModifier = (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
  if (noModifier && event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9) {
    sendRemoteButton(vcd::RemoteButton::Number, event->key() - Qt::Key_0);
    return;
  }
  if (noModifier && event->key() == Qt::Key_Left) return sendRemoteButton(vcd::RemoteButton::Prev);
  if (noModifier && event->key() == Qt::Key_Right) return sendRemoteButton(vcd::RemoteButton::Next);
  if (noModifier && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Backspace))
    return sendRemoteButton(vcd::RemoteButton::Return);
  if (event->key() == Qt::Key_Escape && isFullScreen()) return toggleFullscreen();
  if (noModifier && (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal))
    return applyZoom(ZoomDirection::In);
  if (noModifier && event->key() == Qt::Key_Minus) return applyZoom(ZoomDirection::Out);
  QMainWindow::keyPressEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasUrls()) {
    for (const QUrl& url : event->mimeData()->urls()) {
      if (url.isLocalFile() && url.toLocalFile().endsWith(".cue", Qt::CaseInsensitive)) {
        event->acceptProposedAction();
        return;
      }
    }
  }
}

void MainWindow::dropEvent(QDropEvent* event) {
  for (const QUrl& url : event->mimeData()->urls()) {
    if (url.isLocalFile() && url.toLocalFile().endsWith(".cue", Qt::CaseInsensitive)) {
      openDiscPath(url.toLocalFile());
      return;
    }
  }
}
