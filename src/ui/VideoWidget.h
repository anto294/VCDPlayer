#pragma once

#include <QImage>
#include <QPoint>
#include <QWidget>

class MpvPlayer;

// Video rendering surface. MpvPlayer renders each frame into frame_, a
// plain CPU pixel buffer (directly with mpv's software backend, or read
// back from an offscreen FBO with the OpenGL one), and paintEvent draws it
// with QPainter: one paint path for both backends, inside an ordinary
// QWidget, so double-click and right-click are plain Qt events.
class VideoWidget : public QWidget {
  Q_OBJECT

 public:
  explicit VideoWidget(MpvPlayer* player, QWidget* parent = nullptr);

 signals:
  void doubleClicked();
  void rightClicked(const QPoint& globalPos);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  MpvPlayer* player_;
  QImage frame_;  // pixel buffer MpvPlayer::render() fills (Format_RGB32 = mpv's "bgr0")
};
