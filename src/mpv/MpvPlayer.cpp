#include "MpvPlayer.h"

#include <QMetaObject>
#include <QVector>
#include <stdexcept>

namespace {

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

  mpv_request_log_messages(mpv_, "v");
  mpv_set_wakeup_callback(mpv_, &MpvPlayer::OnMpvEvent, this);

  observeProperty("time-pos", MPV_FORMAT_DOUBLE);
  observeProperty("duration", MPV_FORMAT_DOUBLE);
  observeProperty("volume", MPV_FORMAT_DOUBLE);
  observeProperty("mute", MPV_FORMAT_FLAG);
}

MpvPlayer::~MpvPlayer() {
  if (renderCtx_) mpv_render_context_free(renderCtx_);
  if (mpv_) mpv_terminate_destroy(mpv_);
}

void MpvPlayer::initRenderContext() {
  if (renderCtx_) return;

  // Software render API backend (MPV_RENDER_API_TYPE_SW) instead of OpenGL:
  // on this machine (no real GPU, rendering via Mesa llvmpipe) mpv's
  // classic OpenGL shader path produced a real grid artifact on the video,
  // reproduced identically under any combination of
  // scaling/dithering/FBO-format options — a bug in mpv's GPU renderer on
  // this software driver, not in the content (verified by decoding/scaling
  // the same stream with plain ffmpeg, no OpenGL involved: no artifact).
  // The SW backend instead draws directly into a pixel buffer in RAM,
  // never touching OpenGL/Mesa — it bypasses that whole class of graphics
  // driver bugs, behaves identically everywhere, and for a VCD source
  // (352x288) the CPU cost is negligible. No GPU/GL context required: can
  // be created right away.
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
      {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  const int rc = mpv_render_context_create(&renderCtx_, mpv_, params);
  if (rc < 0)
    throw std::runtime_error(QString("mpv_render_context_create() failed: %1").arg(mpv_error_string(rc)).toStdString());

  mpv_render_context_set_update_callback(renderCtx_, &MpvPlayer::OnMpvRenderUpdate, this);
}

void MpvPlayer::render(void* pixels, int width, int height, size_t stride) {
  if (!renderCtx_) return;
  int size[2] = {width, height};
  char format[] = "bgr0";  // QImage::Format_RGB32's in-memory layout (little-endian)
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_SW_SIZE, size},
      {MPV_RENDER_PARAM_SW_FORMAT, format},
      {MPV_RENDER_PARAM_SW_STRIDE, &stride},
      {MPV_RENDER_PARAM_SW_POINTER, pixels},
      {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  mpv_render_context_render(renderCtx_, params);
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
  QMetaObject::invokeMethod(
      self, [self]() { self->processMpvEvents(); }, Qt::QueuedConnection);
}

void MpvPlayer::OnMpvRenderUpdate(void* ctx) {
  auto* self = static_cast<MpvPlayer*>(ctx);
  QMetaObject::invokeMethod(
      self, [self]() { emit self->videoRedrawNeeded(); }, Qt::QueuedConnection);
}

void MpvPlayer::processMpvEvents() {
  while (true) {
    mpv_event* event = mpv_wait_event(mpv_, 0);
    if (event->event_id == MPV_EVENT_NONE) break;
    if (event->event_id == MPV_EVENT_END_FILE) {
      auto* endFileEvent = static_cast<mpv_event_end_file*>(event->data);
      emit logMessage(QString("end-file reason=%1 error=%2").arg(endFileEvent->reason).arg(mpv_error_string(endFileEvent->error)));
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
