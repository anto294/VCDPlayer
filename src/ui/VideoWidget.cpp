#include "VideoWidget.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

#include "../mpv/MpvPlayer.h"

VideoWidget::VideoWidget(MpvPlayer* player, QWidget* parent) : QWidget(parent), player_(player) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  connect(player_, &MpvPlayer::videoRedrawNeeded, this, QOverload<>::of(&QWidget::update));
  // The render API's software backend needs no graphics context: it can
  // be created right away, unlike the old OpenGL backend which had to
  // wait for initializeGL().
  player_->initRenderContext();
}

void VideoWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  const qreal dpr = devicePixelRatioF();
  const QSize physicalSize = event->size() * dpr;
  if (physicalSize.isEmpty()) return;
  frame_ = QImage(physicalSize, QImage::Format_RGB32);
  frame_.setDevicePixelRatio(dpr);
  frame_.fill(Qt::black);
}

void VideoWidget::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  if (frame_.isNull() || !player_->hasVideo()) {
    painter.fillRect(rect(), Qt::black);
    return;
  }
  player_->render(frame_.bits(), frame_.width(), frame_.height(), static_cast<size_t>(frame_.bytesPerLine()));
  painter.drawImage(0, 0, frame_);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) emit doubleClicked();
}

void VideoWidget::contextMenuEvent(QContextMenuEvent* event) { emit rightClicked(event->globalPos()); }
