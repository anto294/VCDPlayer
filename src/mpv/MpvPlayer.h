#pragma once

#include <mpv/client.h>
#include <mpv/render.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <cstddef>

class QOpenGLContext;
class QOffscreenSurface;
class QOpenGLFramebufferObject;

// Wrapper around libmpv: creates the client handle and the render context
// (libmpv render API — software or OpenGL, see initRenderContext() for how
// the two differ and detectBestRenderBackend() for how one gets picked by
// default), pumps mpv events into the Qt loop, and forwards commands.
// Everything runs in-process through libmpv: no external mpv executable.
class MpvPlayer : public QObject {
  Q_OBJECT

 public:
  enum class RenderBackend { Software, OpenGL };

  explicit MpvPlayer(QObject* parent = nullptr);
  ~MpvPlayer() override;

  mpv_handle* handle() const { return mpv_; }

  // Must be called before initRenderContext() to have any effect (the
  // Advanced settings menu's "Rendering" choice, resolved from "Automatic"
  // to a concrete backend beforehand if needed — see MainWindow). Defaults
  // to Software if never called.
  void setPreferredRenderBackend(RenderBackend backend) { backend_ = backend; }
  RenderBackend renderBackend() const { return backend_; }

  // Creates a real (offscreen, throwaway) OpenGL context just to read back
  // GL_RENDERER and see whether it names a known software rasterizer —
  // used to resolve "Automatic" without ever letting mpv's own GL renderer
  // touch a driver known to mishandle it (see initRenderContext()).
  // Returns Software if no GL context could be created at all.
  static RenderBackend detectBestRenderBackend();

  void initRenderContext();
  bool hasRenderContext() const { return renderCtx_ != nullptr; }

  // Draws the current frame into the given pixel buffer (BGR0 format, i.e.
  // QImage::Format_RGB32), called from VideoWidget::paintEvent.
  void render(void* pixels, int width, int height, size_t stride);

  // Software backend only: mpv's SW render backend (video/out/libmpv_sw.c)
  // only re-reads scaler options like sws-ls/sws-cs when it thinks the
  // target has changed size between two render() calls — nothing makes it
  // notice a property change on its own, unlike the GPU renderer's shader
  // list. Call this right after changing such an option so it takes effect
  // on the very next real frame instead of silently doing nothing until
  // the window happens to get resized. No-op for the OpenGL backend.
  void refreshSoftwareScaler();

  // False while no video output is configured (nothing loaded, or after
  // "stop"): mpv then draws nothing at all, so the widget must clear itself
  // instead of rendering, or the last frame would stay on screen.
  bool hasVideo() const { return voConfigured_; }

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

  bool initSoftwareRenderContext();
  bool initOpenGlRenderContext();
  void renderSoftware(void* pixels, int width, int height, size_t stride);
  void renderOpenGl(void* pixels, int width, int height, size_t stride);

  mpv_handle* mpv_ = nullptr;
  mpv_render_context* renderCtx_ = nullptr;
  RenderBackend backend_ = RenderBackend::Software;
  bool voConfigured_ = false;

  // Only used for RenderBackend::OpenGL: an offscreen context/surface mpv
  // renders into, read back into the same CPU pixel buffer the software
  // backend fills directly, so VideoWidget's paint path (QImage + QPainter)
  // stays identical either way — the difference is entirely in how the
  // frame gets decoded/scaled/color-converted before it lands there.
  QOpenGLContext* glContext_ = nullptr;
  QOffscreenSurface* glSurface_ = nullptr;
  QOpenGLFramebufferObject* glFbo_ = nullptr;
};
