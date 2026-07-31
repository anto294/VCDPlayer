#pragma once

#include <mpv/client.h>
#include <mpv/render.h>

#include <cstddef>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

// Wrapper around libmpv: creates the client handle and the render context
// (libmpv render API, software backend — see MpvPlayer.cpp for why),
// pumps mpv events into the Qt loop, and forwards commands. Replaces the
// JSON-over-pipe IPC used in the Electron version — here everything runs
// in-process, no external mpv.exe.
class MpvPlayer : public QObject {
  Q_OBJECT

 public:
  explicit MpvPlayer(QObject* parent = nullptr);
  ~MpvPlayer() override;

  mpv_handle* handle() const { return mpv_; }

  void initRenderContext();
  bool hasRenderContext() const { return renderCtx_ != nullptr; }

  // Draws the current frame into the given pixel buffer (BGR0 format, i.e.
  // QImage::Format_RGB32), called from VideoWidget::paintEvent.
  void render(void* pixels, int width, int height, size_t stride);

  int command(const QStringList& args);
  void setPropertyString(const QString& name, const QString& value);
  void setPropertyFlag(const QString& name, bool value);
  void setPropertyDouble(const QString& name, double value);
  QVariant getProperty(const QString& name) const;

  // Observes a property: changes arrive via propertyChanged().
  void observeProperty(const QString& name, mpv_format format = MPV_FORMAT_DOUBLE);

 signals:
  // Emitted (on the Qt thread) when mpv has a new frame ready to draw.
  void videoRedrawNeeded();
  void propertyChanged(const QString& name, const QVariant& value);
  void endFile(int reason);
  void fileLoaded();
  void logMessage(const QString& text);

 private:
  static void OnMpvEvent(void* ctx);
  static void OnMpvRenderUpdate(void* ctx);

  void processMpvEvents();

  mpv_handle* mpv_ = nullptr;
  mpv_render_context* renderCtx_ = nullptr;
};
