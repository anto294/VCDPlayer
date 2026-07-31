#include "MainWindow.h"

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTranslator>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "../app/DiscSession.h"
#include "../mpv/MpvPlayer.h"
#include "RemotePanel.h"
#include "VideoWidget.h"

namespace {

constexpr int kSeekBarMax = 1000;

QString formatTime(double seconds) {
  const int s = static_cast<int>(std::max(0.0, std::floor(seconds)));
  return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  // Install the saved language's translator before building any widget, so
  // the very first buildMenuBar()/RemotePanel construction below already
  // produces the right-language text instead of flashing English first.
  currentLanguage_ = QSettings().value(QStringLiteral("language"), QStringLiteral("en")).toString();
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

  setWindowTitle(QStringLiteral("VCD Player"));
  resize(1150, 880);
  setMinimumSize(760, 800);
  setAcceptDrops(true);

  player_ = new MpvPlayer(this);
  session_ = new DiscSession(player_, this);

  const QString screenshotDir =
      QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/VCDPlayer";
  QDir().mkpath(screenshotDir);
  player_->setPropertyString("screenshot-directory", screenshotDir);

  auto* central = new QWidget(this);
  setCentralWidget(central);
  auto* rootLayout = new QHBoxLayout(central);
  rootLayout->setContentsMargins(24, 24, 24, 24);
  rootLayout->setSpacing(10);

  auto* mainArea = new QWidget(central);
  auto* mainAreaLayout = new QVBoxLayout(mainArea);
  mainAreaLayout->setContentsMargins(0, 0, 0, 0);
  mainAreaLayout->setSpacing(10);

  video_ = new VideoWidget(player_, mainArea);
  video_->setMinimumSize(160, 120);
  video_->setStyleSheet("background: #000; border: 1px solid #2a2d36; border-radius: 4px;");
  mainAreaLayout->addWidget(video_, 1);

  progressRow_ = new QWidget(mainArea);
  auto* progressLayout = new QHBoxLayout(progressRow_);
  progressLayout->setContentsMargins(0, 0, 0, 0);
  progressLayout->setSpacing(10);
  timeCurrentLabel_ = new QLabel(QStringLiteral("00:00"), progressRow_);
  timeCurrentLabel_->setStyleSheet("font-size: 12px; color: #b7b9c2;");
  seekBar_ = new QSlider(Qt::Horizontal, progressRow_);
  seekBar_->setRange(0, kSeekBarMax);
  timeDurationLabel_ = new QLabel(QStringLiteral("00:00"), progressRow_);
  timeDurationLabel_->setStyleSheet("font-size: 12px; color: #b7b9c2;");
  progressLayout->addWidget(timeCurrentLabel_);
  progressLayout->addWidget(seekBar_, 1);
  progressLayout->addWidget(timeDurationLabel_);
  mainAreaLayout->addWidget(progressRow_, 0);

  rootLayout->addWidget(mainArea, 1);

  remote_ = new RemotePanel(central);
  rootLayout->addWidget(remote_, 0);

  panelToggleButton_ = new QPushButton(central);
  panelToggleButton_->setFixedSize(18, 36);
  panelToggleButton_->setStyleSheet(
      "QPushButton { background: #262932; color: #e8e8ec; border: 1px solid #383c48;"
      "  border-radius: 6px 0 0 6px; font-size: 12px; padding: 0; }"
      "QPushButton:hover { background: #313543; }");
  panelToggleButton_->raise();

  buildMenuBar();

  connect(video_, &VideoWidget::doubleClicked, this, &MainWindow::toggleFullscreen);
  connect(video_, &VideoWidget::rightClicked, this, &MainWindow::showContextMenuAt);

  connect(panelToggleButton_, &QPushButton::clicked, this, &MainWindow::toggleRemotePanel);

  connect(remote_, &RemotePanel::openDiscRequested, this, &MainWindow::openDiscDialog);
  connect(remote_, &RemotePanel::remoteButtonPressed, this,
          [this](vcd::RemoteButton btn) { sendRemoteButton(btn); });
  connect(remote_, &RemotePanel::numberPressed, this,
          [this](int digit) { sendRemoteButton(vcd::RemoteButton::Number, digit); });
  connect(remote_, &RemotePanel::muteToggled, this, &MainWindow::toggleMute);
  connect(remote_, &RemotePanel::volumeChanged, this,
          [this](int value) { player_->setPropertyDouble("volume", value); });
  connect(remote_, &RemotePanel::zoomInRequested, this, [this]() { applyZoom(ZoomDirection::In); });
  connect(remote_, &RemotePanel::zoomOutRequested, this, [this]() { applyZoom(ZoomDirection::Out); });
  connect(remote_, &RemotePanel::zoomResetRequested, this, [this]() { applyZoom(ZoomDirection::Reset); });
  connect(remote_, &RemotePanel::screenshotRequested, this, &MainWindow::takeScreenshot);
  connect(remote_, &RemotePanel::fullscreenToggled, this, &MainWindow::toggleFullscreen);

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

  connect(session_, &DiscSession::discLoaded, this, [this](QString volumeId, bool pbcEnabled, int chapters) {
    currentDiscTitle_ = volumeId.isEmpty() ? tr("Disc") : volumeId;
    const QString status = pbcEnabled ? tr("PBC active · %1 chapters").arg(chapters)
                                       : tr("Sequential playback · %1 chapters").arg(chapters);
    remote_->setDiscInfo(currentDiscTitle_, status);
  });
  connect(session_, &DiscSession::stateChanged, this, [this](int kind, int choiceCount) {
    using Kind = vcd::PbcEngine::State::Kind;
    QString status;
    switch (static_cast<Kind>(kind)) {
      case Kind::End:
        status = tr("End of disc");
        break;
      case Kind::Menu:
        status = tr("Menu — %1 choices (use the number keys)").arg(choiceCount);
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
  auto* fileMenu = menuBar()->addMenu(tr("&File"));
  QAction* openAction = fileMenu->addAction(tr("Open disc…"));
  openAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
  connect(openAction, &QAction::triggered, this, &MainWindow::openDiscDialog);
  fileMenu->addSeparator();
  QAction* quitAction = fileMenu->addAction(tr("Exit"));
  connect(quitAction, &QAction::triggered, this, &MainWindow::close);

  auto* playbackMenu = menuBar()->addMenu(tr("&Playback"));
  QAction* playAction = playbackMenu->addAction(tr("Play"));
  playAction->setShortcut(QKeySequence(Qt::Key_Space));
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
  playbackMenu->addSeparator();
  QAction* screenshotAction = playbackMenu->addAction(tr("Take screenshot"));
  screenshotAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
  connect(screenshotAction, &QAction::triggered, this, &MainWindow::takeScreenshot);

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
  muteAction_ = audioMenu->addAction(tr("Mute"));
  muteAction_->setCheckable(true);
  muteAction_->setShortcut(QKeySequence(QStringLiteral("M")));
  connect(muteAction_, &QAction::triggered, this, &MainWindow::toggleMute);

  auto* viewMenu = menuBar()->addMenu(tr("&View"));
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
  connect(showProgressBarAction_, &QAction::triggered, this, &MainWindow::setShowProgressBar);
  showTimeLabelAction_ = viewMenu->addAction(tr("Time label"));
  showTimeLabelAction_->setCheckable(true);
  connect(showTimeLabelAction_, &QAction::triggered, this, &MainWindow::setShowTimeLabel);
  viewMenu->addSeparator();
  showRemoteAction_ = viewMenu->addAction(tr("Remote panel"));
  showRemoteAction_->setCheckable(true);
  showRemoteAction_->setChecked(true);
  connect(showRemoteAction_, &QAction::triggered, this, &MainWindow::toggleRemotePanel);

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
  connect(menu->addAction(tr("Take screenshot")), &QAction::triggered, this, &MainWindow::takeScreenshot);
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
  QAction* muteItem = audioMenu->addAction(tr("Mute"));
  muteItem->setCheckable(true);
  muteItem->setChecked(player_->getProperty("mute").toBool());
  connect(muteItem, &QAction::triggered, this, &MainWindow::toggleMute);
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
  connect(zoomMenu->addAction(tr("Zoom in")), &QAction::triggered, this,
          [this]() { applyZoom(ZoomDirection::In); });
  connect(zoomMenu->addAction(tr("Zoom out")), &QAction::triggered, this,
          [this]() { applyZoom(ZoomDirection::Out); });
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
  if (!session_->openDiscPath(cuePath, &error)) {
    remote_->setStatus(QStringLiteral("—"));
    QMessageBox::critical(this, tr("Error opening disc"), error);
  }
}

void MainWindow::sendRemoteButton(vcd::RemoteButton btn, int number) { session_->sendRemoteButton(btn, number); }

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
  remote_->setVisible(!goingFullscreen && showRemote_);
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
  if (showRemoteAction_) showRemoteAction_->setChecked(showRemote_);
  applyViewSettingsToUi();
}

void MainWindow::setShowProgressBar(bool show) {
  showProgressBarSetting_ = show;
  if (showProgressBarAction_) showProgressBarAction_->setChecked(show);
  applyViewSettingsToUi();
}

void MainWindow::setShowTimeLabel(bool show) {
  showTimeLabelSetting_ = show;
  if (showTimeLabelAction_) showTimeLabelAction_->setChecked(show);
  applyViewSettingsToUi();
}

void MainWindow::takeScreenshot() {
  player_->command({"screenshot"});
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/VCDPlayer";
  statusBar()->showMessage(tr("Screenshot saved to %1").arg(dir), 3000);
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
}

void MainWindow::applyViewSettingsToUi() {
  if (!isFullScreen()) {
    remote_->setVisible(showRemote_);
    progressRow_->setVisible(showProgressBarSetting_ || showTimeLabelSetting_);
  }
  seekBar_->setVisible(showProgressBarSetting_);
  timeCurrentLabel_->setVisible(showTimeLabelSetting_);
  timeDurationLabel_->setVisible(showTimeLabelSetting_);
  panelToggleButton_->setText(showRemote_ ? QStringLiteral("›") : QStringLiteral("‹"));
  updatePanelTogglePosition();
}

void MainWindow::updatePanelTogglePosition() {
  auto* central = centralWidget();
  if (!central) return;
  const int w = panelToggleButton_->width();
  const int h = panelToggleButton_->height();
  const int rightGap = showRemote_ ? 235 : 2;
  const int x = central->width() - rightGap - w;
  const int y = central->height() / 2 - h / 2;
  panelToggleButton_->setGeometry(x, y, w, h);
  panelToggleButton_->raise();
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

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  updatePanelTogglePosition();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
  const bool noModifier = event->modifiers() == Qt::NoModifier;
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
