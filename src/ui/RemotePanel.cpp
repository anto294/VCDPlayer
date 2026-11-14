#include "RemotePanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace {

QPushButton* makeButton(const QString& text, const QString& tooltip = QString()) {
  auto* btn = new QPushButton(text);
  if (!tooltip.isEmpty()) btn->setToolTip(tooltip);
  return btn;
}

}  // namespace

RemotePanel::RemotePanel(QWidget* parent) : QWidget(parent) {
  // A plain QWidget doesn't paint its stylesheet's background/border on its
  // own; without this it stays see-through, which used to be invisible when
  // the panel sat beside the video but now that it floats on top of it (see
  // MainWindow::updateOverlayPositions()) the video would otherwise show
  // through underneath it.
  setAttribute(Qt::WA_StyledBackground, true);
  setStyleSheet(
      "RemotePanel { background: #1a1c22; border: 1px solid #2a2d36; border-radius: 10px; }"
      "QPushButton { background: #262932; color: #e8e8ec; border: 1px solid #383c48;"
      "  border-radius: 6px; padding: 8px 6px; font-size: 13px; }"
      "QPushButton:hover { background: #313543; }"
      "QPushButton:pressed { background: #3a3f50; }"
      "QLabel { color: #e8e8ec; }");

  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(14, 14, 14, 14);
  mainLayout->setSpacing(14);

  // --- Disc info -----------------------------------------------------
  auto* discInfoLayout = new QVBoxLayout();
  discInfoLayout->setSpacing(2);
  discTitle_ = new QLabel(tr("No disc"));
  discTitle_->setStyleSheet("font-size: 14px; font-weight: 600;");
  discTitle_->setWordWrap(true);
  discStatus_ = new QLabel(QStringLiteral("—"));
  discStatus_->setStyleSheet("font-size: 12px; color: #8a8d98;");
  // Status strings like "Menu — 9 choices (use the number keys)" can
  // easily be wider than the panel; wrap instead of getting clipped.
  discStatus_->setWordWrap(true);
  discInfoLayout->addWidget(discTitle_);
  discInfoLayout->addWidget(discStatus_);
  auto* discInfoWidget = new QWidget();
  discInfoWidget->setLayout(discInfoLayout);
  discInfoWidget->setStyleSheet("border-bottom: 1px solid #2a2d36; padding-bottom: 10px;");
  mainLayout->addWidget(discInfoWidget);

  openButton_ = makeButton(tr("Open disc…"));
  connect(openButton_, &QPushButton::clicked, this, &RemotePanel::openDiscRequested);
  mainLayout->addWidget(openButton_);

  // --- Dpad -------------------------------------------------------------
  auto* dpad = new QGridLayout();
  dpad->setSpacing(4);
  dpadUp_ = makeButton(tr("Menu"));
  auto* dpadLeft = makeButton(QStringLiteral("◀"));
  auto* dpadCenter = makeButton(QStringLiteral("▶ ⏸"));
  auto* dpadRight = makeButton(QStringLiteral("▶"));
  dpadDown_ = makeButton(tr("Next chap."));
  dpad->addWidget(dpadUp_, 0, 1);
  dpad->addWidget(dpadLeft, 1, 0);
  dpad->addWidget(dpadCenter, 1, 1);
  dpad->addWidget(dpadRight, 1, 2);
  dpad->addWidget(dpadDown_, 2, 1);
  dpad->setColumnStretch(0, 1);
  dpad->setColumnStretch(1, 2);
  dpad->setColumnStretch(2, 1);
  mainLayout->addLayout(dpad);

  connect(dpadUp_, &QPushButton::clicked, this, [this]() { emit remoteButtonPressed(vcd::RemoteButton::Return); });
  connect(dpadLeft, &QPushButton::clicked, this, [this]() { emit remoteButtonPressed(vcd::RemoteButton::Prev); });
  connect(dpadCenter, &QPushButton::clicked, this, [this]() { emit remoteButtonPressed(vcd::RemoteButton::Play); });
  connect(dpadRight, &QPushButton::clicked, this, [this]() { emit remoteButtonPressed(vcd::RemoteButton::Next); });
  connect(dpadDown_, &QPushButton::clicked, this, [this]() { emit remoteButtonPressed(vcd::RemoteButton::Next); });

  // --- Numeric keypad ----------------------------------------------
  auto* numpad = new QGridLayout();
  numpad->setSpacing(6);
  for (int digit = 1; digit <= 9; digit++) {
    auto* btn = makeButton(QString::number(digit));
    const int index = digit - 1;
    numpad->addWidget(btn, index / 3, index % 3);
    connect(btn, &QPushButton::clicked, this, [this, digit]() { emit numberPressed(digit); });
  }
  // 0 goes under 8 (row 3, column 1), like a phone/calculator keypad,
  // instead of lining up under the 1/4/7 column.
  auto* zeroButton = makeButton(QStringLiteral("0"));
  numpad->addWidget(zeroButton, 3, 1);
  connect(zeroButton, &QPushButton::clicked, this, [this]() { emit numberPressed(0); });
  mainLayout->addLayout(numpad);

  mainLayout->addStretch(1);

  // --- Footer -------------------------------------------------------------
  auto* transportRow = new QHBoxLayout();
  playButton_ = makeButton(tr("Play"));
  pauseButton_ = makeButton(tr("Pause"));
  stopButton_ = makeButton(tr("Stop"));
  transportRow->addWidget(playButton_);
  transportRow->addWidget(pauseButton_);
  transportRow->addWidget(stopButton_);
  mainLayout->addLayout(transportRow);
  connect(playButton_, &QPushButton::clicked, this, [this]() { emit remoteButtonPressed(vcd::RemoteButton::Play); });
  connect(pauseButton_, &QPushButton::clicked, this, [this]() { emit remoteButtonPressed(vcd::RemoteButton::Pause); });
  connect(stopButton_, &QPushButton::clicked, this, [this]() { emit remoteButtonPressed(vcd::RemoteButton::Stop); });

  auto* volumeRow = new QHBoxLayout();
  muteButton_ = makeButton(QStringLiteral("\U0001F50A"), tr("Mute"));
  volumeSlider_ = new QSlider(Qt::Horizontal);
  volumeSlider_->setRange(0, 100);
  volumeSlider_->setValue(100);
  volumeRow->addWidget(muteButton_);
  volumeRow->addWidget(volumeSlider_, 1);
  mainLayout->addLayout(volumeRow);
  connect(muteButton_, &QPushButton::clicked, this, &RemotePanel::muteToggled);
  connect(volumeSlider_, &QSlider::valueChanged, this, &RemotePanel::volumeChanged);

  fullscreenButton_ = makeButton(QStringLiteral("⛶ ") + tr("Fullscreen"));
  mainLayout->addWidget(fullscreenButton_);
  connect(fullscreenButton_, &QPushButton::clicked, this, &RemotePanel::fullscreenToggled);
}

void RemotePanel::setDiscInfo(const QString& title, const QString& status) {
  hasDisc_ = true;
  discTitle_->setText(title);
  discStatus_->setText(status);
}

void RemotePanel::setStatus(const QString& status) { discStatus_->setText(status); }

void RemotePanel::setMuted(bool muted) {
  muted_ = muted;
  muteButton_->setText(muted ? QStringLiteral("\U0001F507") : QStringLiteral("\U0001F50A"));
  muteButton_->setToolTip(muted ? tr("Unmute") : tr("Mute"));
}

void RemotePanel::retranslateUi() {
  if (!hasDisc_) discTitle_->setText(tr("No disc"));
  openButton_->setText(tr("Open disc…"));
  dpadUp_->setText(tr("Menu"));
  dpadDown_->setText(tr("Next chap."));
  playButton_->setText(tr("Play"));
  pauseButton_->setText(tr("Pause"));
  stopButton_->setText(tr("Stop"));
  muteButton_->setToolTip(muted_ ? tr("Unmute") : tr("Mute"));
  fullscreenButton_->setText(QStringLiteral("⛶ ") + tr("Fullscreen"));
}

void RemotePanel::setVolume(int value) {
  const QSignalBlocker blocker(volumeSlider_);
  volumeSlider_->setValue(value);
}
