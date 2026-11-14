#include "MpvPlayer.h"

#include <mpv/render_gl.h>

#include <QMetaObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QVector>
#include <stdexcept>

namespace {

// Software GL rasterizers report themselves through GL_RENDERER just like
// real GPUs do, but reproduce a real artifact in mpv's GL shader pipeline
// (see MpvPlayer::initOpenGlRenderContext()) — not something a proc-address
// probe can detect on its own, so detectBestRenderBackend() instead refuses
// the OpenGL backend outright whenever the renderer string names one of
// these, and only ever recommends it where genuine GPU rendering is likely.
bool IsKnownSoftwareRenderer(const QString& glRenderer) {
  static const QStringList kNeedles = {
      QStringLiteral("llvmpipe"),
      QStringLiteral("softpipe"),
      QStringLiteral("swiftshader"),
      QStringLiteral("microsoft basic render driver"),
      QStringLiteral("d3d12 (microsoft basic render driver)"),
      QStringLiteral("mesa dozen"),
  };
  const QString lower = glRenderer.toLower();
  for (const QString& needle : kNeedles) {
    if (lower.contains(needle)) return true;
  }
  return false;
}

QVariant MpvNodeToQVariant(mpv_node* node) {
  switch (node->format) {
    case MPV_FORMAT_STRING:
      return QVariant(QString::fromUtf8(node->u.string));
    case MPV_FORMAT_FLAG:
      return QVariant(static_cast<bool>(node->u.flag));
    case MPV_FORMAT_INT64:
      return QVariant(static_cast<qlonglong>(node->u.int64));
    case MPV_FORMAT_DOUBLE:
      return QVariant(node->u.double_);
    default:
      return QVariant();
  }
}

}  // namespace

MpvPlayer::MpvPlayer(QObject* parent) : QObject(parent) {
  mpv_ = mpv_create();
  if (!mpv_) throw std::runtime_error("mpv_create() failed.");

  // Key/mouse bindings are handled by our own Qt UI, not by mpv: no
  // OSC/default bindings, mpv only receives the commands we send it.
  mpv_set_option_string(mpv_, "input-default-bindings", "no");
  mpv_set_option_string(mpv_, "input-vo-keyboard", "no");
  mpv_set_option_string(mpv_, "osc", "no");
  mpv_set_option_string(mpv_, "vo", "libmpv");
  mpv_set_option_string(mpv_, "keep-open", "yes");
  mpv_set_option_string(mpv_, "idle", "yes");
  mpv_set_option_string(mpv_, "volume", "100");
  mpv_set_option_string(mpv_, "mute", "no");
  mpv_set_option_string(mpv_, "screenshot-template", "vcdplayer-%tY%tm%td-%tH%tM%tS");

  const int rc = mpv_initialize(mpv_);
  if (rc < 0) throw std::runtime_error(QString("mpv_initialize() failed: %1").arg(mpv_error_string(rc)).toStdString());

  // Warnings and errors only by default: "v" (verbose) logs several lines
  // per file load and every option change, all forwarded to stderr. Set
  // VCDPLAYER_MPV_LOG=v (or debug, trace, ...) to get them back when
  // diagnosing a playback problem.
  const QByteArray logLevel = qgetenv("VCDPLAYER_MPV_LOG");
  mpv_request_log_messages(mpv_, logLevel.isEmpty() ? "warn" : logLevel.constData());
  mpv_set_wakeup_callback(mpv_, &MpvPlayer::OnMpvEvent, this);

  observeProperty("time-pos", MPV_FORMAT_DOUBLE);
  observeProperty("duration", MPV_FORMAT_DOUBLE);
  observeProperty("volume", MPV_FORMAT_DOUBLE);
  observeProperty("mute", MPV_FORMAT_FLAG);
  // With keep-open=yes (needed so a menu's still image stays on screen),
  // mpv never sends end-file when a file plays to its end: it pauses on
  // the last frame and flips eof-reached instead. That flag is therefore
  // the only "item finished" signal (see DiscSession).
  observeProperty("eof-reached", MPV_FORMAT_FLAG);
}

MpvPlayer::~MpvPlayer() {
  // For the OpenGL backend, libmpv requires its GL context to be current
  // while the render context is freed (it deletes its own GL objects
  // then), and so does deleting our FBO — so make it current first, before
  // either, rather than after as before.
  const bool glCurrent = glContext_ && glSurface_ && glContext_->makeCurrent(glSurface_);
  if (renderCtx_) mpv_render_context_free(renderCtx_);
  delete glFbo_;
  if (glCurrent) glContext_->doneCurrent();
  // The render context must be gone before the core it belongs to.
  if (mpv_) mpv_terminate_destroy(mpv_);
  delete glContext_;
  delete glSurface_;
}

MpvPlayer::RenderBackend MpvPlayer::detectBestRenderBackend() {
  QOffscreenSurface surface;
  surface.create();
  if (!surface.isValid()) return RenderBackend::Software;

  QOpenGLContext probe;
  if (!probe.create() || !probe.makeCurrent(&surface)) return RenderBackend::Software;

  const auto* renderer = reinterpret_cast<const char*>(probe.functions()->glGetString(GL_RENDERER));
  const QString rendererName = renderer ? QString::fromUtf8(renderer) : QString();
  probe.doneCurrent();

  if (rendererName.isEmpty() || IsKnownSoftwareRenderer(rendererName)) return RenderBackend::Software;
  return RenderBackend::OpenGL;
}

void MpvPlayer::initRenderContext() {
  if (renderCtx_) return;

  // Two render API backends, chosen by whoever calls
  // setPreferredRenderBackend() before this (MainWindow, from the Advanced
  // settings menu / detectBestRenderBackend() for "Automatic"):
  //  - Software (MPV_RENDER_API_TYPE_SW): draws directly into a pixel
  //    buffer in RAM, never touching a GPU driver. Needed on machines
  //    without a real GPU — this project's own dev sandbox renders via
  //    Mesa llvmpipe, where mpv's classic OpenGL shader path produced a
  //    real grid artifact on the video, reproduced identically under any
  //    combination of scaling/dithering/FBO-format options (verified as a
  //    bug in mpv's GPU renderer on that software driver, not the content,
  //    by decoding/scaling the same stream with plain ffmpeg — no OpenGL
  //    involved, no artifact). Also the safe fallback if OpenGL init below
  //    fails for any other reason.
  //  - OpenGL: lets mpv decode/scale/color-convert on the GPU, which is
  //    both faster and avoids that CPU path entirely on real hardware.
  // Either way the frame ends up in the same CPU pixel buffer (see
  // renderSoftware()/renderOpenGl()), so VideoWidget's own paint path never
  // needs to know which backend is active.
  bool ok = false;
  if (backend_ == RenderBackend::OpenGL) {
    ok = initOpenGlRenderContext();
    if (!ok) backend_ = RenderBackend::Software;  // fall back rather than leave video blank
  }
  if (!ok) ok = initSoftwareRenderContext();

  if (!ok) throw std::runtime_error("mpv_render_context_create() failed for both OpenGL and software backends.");

  mpv_render_context_set_update_callback(renderCtx_, &MpvPlayer::OnMpvRenderUpdate, this);
}

bool MpvPlayer::initSoftwareRenderContext() {
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
      {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  backend_ = RenderBackend::Software;
  return mpv_render_context_create(&renderCtx_, mpv_, params) >= 0;
}

bool MpvPlayer::initOpenGlRenderContext() {
  glSurface_ = new QOffscreenSurface();
  glSurface_->create();
  if (!glSurface_->isValid()) return false;

  glContext_ = new QOpenGLContext();
  if (!glContext_->create() || !glContext_->makeCurrent(glSurface_)) return false;

  mpv_opengl_init_params glInitParams = {
      [](void*, const char* name) -> void* {
        // Only ever called while glContext_ is current (during
        // mpv_render_context_create() below), so the thread-local
        // QOpenGLContext::currentContext() is guaranteed to be it.
        return reinterpret_cast<void*>(QOpenGLContext::currentContext()->getProcAddress(name));
      },
      nullptr,
  };
  // Deliberately not setting MPV_RENDER_PARAM_ADVANCED_CONTROL: it requires
  // implementing mpv_render_context_update()'s full contract (calling
  // report_swap() etc.), which an earlier attempt at this got wrong,
  // silently breaking the redraw signal after the first frame. "Simple"
  // mode (the default, unset) works fine without it.
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
      {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams},
      {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  backend_ = RenderBackend::OpenGL;
  const bool ok = mpv_render_context_create(&renderCtx_, mpv_, params) >= 0;
  glContext_->doneCurrent();
  return ok;
}

void MpvPlayer::render(void* pixels, int width, int height, size_t stride) {
  if (!renderCtx_) return;
  if (backend_ == RenderBackend::OpenGL) {
    renderOpenGl(pixels, width, height, stride);
  } else {
    renderSoftware(pixels, width, height, stride);
  }
}

void MpvPlayer::renderSoftware(void* pixels, int width, int height, size_t stride) {
  int size[2] = {width, height};
  char format[] = "bgr0";  // QImage::Format_RGB32's in-memory layout (little-endian)
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_SW_SIZE, size},      {MPV_RENDER_PARAM_SW_FORMAT, format},
      {MPV_RENDER_PARAM_SW_STRIDE, &stride}, {MPV_RENDER_PARAM_SW_POINTER, pixels},
      {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  mpv_render_context_render(renderCtx_, params);
}

void MpvPlayer::refreshSoftwareScaler() {
  if (backend_ != RenderBackend::Software || !renderCtx_) return;
  // A throwaway 1x1 render, never painted anywhere: its only purpose is to
  // make the next real-size render() call look, from libmpv_sw.c's point of
  // view, like the target size just changed (1x1 -> the widget's actual
  // size), which is what makes it reinitialize its scaler — and pick up
  // sws-ls/sws-cs et al. along with it — instead of reusing the one it
  // built the first time these options were whatever they were before.
  char scratch[4] = {0};
  int size[2] = {1, 1};
  size_t stride = 4;
  char format[] = "bgr0";
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_SW_SIZE, size},      {MPV_RENDER_PARAM_SW_FORMAT, format},
      {MPV_RENDER_PARAM_SW_STRIDE, &stride}, {MPV_RENDER_PARAM_SW_POINTER, scratch},
      {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  mpv_render_context_render(renderCtx_, params);
}

void MpvPlayer::renderOpenGl(void* pixels, int width, int height, size_t stride) {
  glContext_->makeCurrent(glSurface_);

  if (!glFbo_ || glFbo_->size() != QSize(width, height)) {
    delete glFbo_;
    glFbo_ = new QOpenGLFramebufferObject(width, height);
  }

  mpv_opengl_fbo fbo{};
  fbo.fbo = static_cast<int>(glFbo_->handle());
  fbo.w = width;
  fbo.h = height;
  // FLIP_Y must stay 0 here. mpv's "normal" (unflipped) rendering into an
  // FBO puts the image's top row at GL row 0, and glReadPixels below also
  // reads starting from GL row 0 — so `pixels` comes out top row first,
  // exactly what QImage expects. FLIP_Y=1 is only meant for drawing
  // straight to a window's default framebuffer (fbo 0), whose on-screen
  // origin is bottom-left; using it with this offscreen FBO + readback
  // showed every video upside down.
  int flipY = 0;
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
      {MPV_RENDER_PARAM_FLIP_Y, &flipY},
      {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  mpv_render_context_render(renderCtx_, params);

  auto* gl = glContext_->functions();
  gl->glBindFramebuffer(GL_FRAMEBUFFER, glFbo_->handle());
  gl->glFinish();
  // Row length in `pixels` (from stride) always equals width * 4 for
  // QImage::Format_RGB32 (each pixel is already 4-byte aligned), which is
  // exactly glReadPixels' own tightly-packed default — no glPixelStorei
  // needed. GL_BGRA + GL_UNSIGNED_BYTE matches "bgr0"'s byte order.
  gl->glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
  Q_UNUSED(stride);

  glContext_->doneCurrent();
}

int MpvPlayer::command(const QStringList& args) {
  if (!mpv_) return MPV_ERROR_UNINITIALIZED;
  QVector<QByteArray> owned;
  QVector<const char*> cargs;
  owned.reserve(args.size());
  cargs.reserve(args.size() + 1);
  for (const QString& a : args) {
    owned.push_back(a.toUtf8());
    cargs.push_back(owned.back().constData());
  }
  cargs.push_back(nullptr);
  return mpv_command(mpv_, cargs.data());
}

void MpvPlayer::setPropertyString(const QString& name, const QString& value) {
  const QByteArray n = name.toUtf8();
  const QByteArray v = value.toUtf8();
  mpv_set_property_string(mpv_, n.constData(), v.constData());
}

void MpvPlayer::setPropertyFlag(const QString& name, bool value) {
  const QByteArray n = name.toUtf8();
  int flag = value ? 1 : 0;
  mpv_set_property(mpv_, n.constData(), MPV_FORMAT_FLAG, &flag);
}

void MpvPlayer::setPropertyDouble(const QString& name, double value) {
  const QByteArray n = name.toUtf8();
  mpv_set_property(mpv_, n.constData(), MPV_FORMAT_DOUBLE, &value);
}

QVariant MpvPlayer::getProperty(const QString& name) const {
  const QByteArray n = name.toUtf8();
  mpv_node node;
  if (mpv_get_property(mpv_, n.constData(), MPV_FORMAT_NODE, &node) < 0) return QVariant();
  const QVariant v = MpvNodeToQVariant(&node);
  mpv_free_node_contents(&node);
  return v;
}

void MpvPlayer::observeProperty(const QString& name, mpv_format format) {
  const QByteArray n = name.toUtf8();
  mpv_observe_property(mpv_, 0, n.constData(), format);
}

void MpvPlayer::OnMpvEvent(void* ctx) {
  auto* self = static_cast<MpvPlayer*>(ctx);
  // Called from mpv's internal thread: defers processing to the Qt thread.
  QMetaObject::invokeMethod(self, [self]() { self->processMpvEvents(); }, Qt::QueuedConnection);
}

void MpvPlayer::OnMpvRenderUpdate(void* ctx) {
  auto* self = static_cast<MpvPlayer*>(ctx);
  QMetaObject::invokeMethod(self, [self]() { emit self->videoRedrawNeeded(); }, Qt::QueuedConnection);
}

void MpvPlayer::processMpvEvents() {
  while (true) {
    mpv_event* event = mpv_wait_event(mpv_, 0);
    if (event->event_id == MPV_EVENT_NONE) break;
    if (event->event_id == MPV_EVENT_END_FILE) {
      auto* endFileEvent = static_cast<mpv_event_end_file*>(event->data);
      emit logMessage(
          QString("end-file reason=%1 error=%2").arg(endFileEvent->reason).arg(mpv_error_string(endFileEvent->error)));
      emit endFile(static_cast<int>(endFileEvent->reason));
    } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
      emit fileLoaded();
    } else if (event->event_id == MPV_EVENT_LOG_MESSAGE) {
      auto* msg = static_cast<mpv_event_log_message*>(event->data);
      emit logMessage(QString("[%1/%2] %3").arg(msg->prefix, msg->level, QString::fromUtf8(msg->text).trimmed()));
    } else if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
      auto* prop = static_cast<mpv_event_property*>(event->data);
      QVariant value;
      if (prop->format == MPV_FORMAT_DOUBLE) {
        value = *static_cast<double*>(prop->data);
      } else if (prop->format == MPV_FORMAT_FLAG) {
        value = static_cast<bool>(*static_cast<int*>(prop->data));
      } else if (prop->format == MPV_FORMAT_STRING) {
        value = QString::fromUtf8(*static_cast<char**>(prop->data));
      }
      emit propertyChanged(QString::fromUtf8(prop->name), value);
    }
  }
}
