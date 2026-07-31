#pragma once

#include <QImage>
#include <QPoint>
#include <QWidget>

class MpvPlayer;

// Video rendering surface: mpv draws directly here (libmpv render API,
// software backend — see MpvPlayer.cpp for why), same process, same
// widget — no separate native window. That's why double-click and
// right-click are plain Qt events, without the workarounds needed in the
// Electron version (where the video was drawn by an external window that
// intercepted input on the app's behalf).
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
  QImage frame_;  // buffer di pixel in cui mpv disegna (MPV_RENDER_API_TYPE_SW)
};
