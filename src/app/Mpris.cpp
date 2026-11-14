#include "Mpris.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>

namespace {

const char* const kObjectPath = "/org/mpris/MediaPlayer2";
const char* const kPlayerInterface = "org.mpris.MediaPlayer2.Player";

}  // namespace

Mpris::Mpris(QObject* parent) : QObject(parent) {
  new MprisRootAdaptor(this);
  new MprisPlayerAdaptor(this);

  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) return;
  bus.registerObject(QString::fromLatin1(kObjectPath), this);
  // A second running instance gets its own name, as the spec suggests.
  const QString name = QStringLiteral("org.mpris.MediaPlayer2.vcdplayer");
  if (!bus.registerService(name)) {
    bus.registerService(name + QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid()));
  }
}

QVariantMap Mpris::metadata() const {
  if (title_.isEmpty()) return {};
  QVariantMap m;
  m.insert(QStringLiteral("mpris:trackid"),
           QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/vcdplayer/item/%1").arg(trackSerial_))));
  m.insert(QStringLiteral("xesam:title"), title_);
  if (!album_.isEmpty()) m.insert(QStringLiteral("xesam:album"), album_);
  if (lengthSeconds_ > 0) m.insert(QStringLiteral("mpris:length"), static_cast<qlonglong>(lengthSeconds_ * 1e6));
  return m;
}

void Mpris::setPlaybackStatus(const QString& status) {
  if (status == playbackStatus_) return;
  playbackStatus_ = status;
  notifyChanged({{QStringLiteral("PlaybackStatus"), status}});
}

void Mpris::setMetadata(const QString& title, const QString& album, double lengthSeconds) {
  if (title == title_ && album == album_ && lengthSeconds == lengthSeconds_) return;
  const bool discChanged = hasDisc() != !title.isEmpty();
  const bool seekChanged = canSeek() != (lengthSeconds > 0);
  title_ = title;
  album_ = album;
  lengthSeconds_ = lengthSeconds;
  trackSerial_++;
  QVariantMap changed{{QStringLiteral("Metadata"), metadata()}};
  if (discChanged) {
    for (const char* p : {"CanGoNext", "CanGoPrevious", "CanPlay", "CanPause"}) {
      changed.insert(QString::fromLatin1(p), hasDisc());
    }
  }
  if (seekChanged) changed.insert(QStringLiteral("CanSeek"), canSeek());
  notifyChanged(changed);
}

void Mpris::setVolume(double volume01) {
  if (volume01 == volume_) return;
  volume_ = volume01;
  notifyChanged({{QStringLiteral("Volume"), volume01}});
}

void Mpris::notifyChanged(const QVariantMap& changed) {
  QDBusMessage signal =
      QDBusMessage::createSignal(QString::fromLatin1(kObjectPath), QStringLiteral("org.freedesktop.DBus.Properties"),
                                 QStringLiteral("PropertiesChanged"));
  signal << QString::fromLatin1(kPlayerInterface) << changed << QStringList();
  QDBusConnection::sessionBus().send(signal);
}

QString MprisRootAdaptor::desktopEntry() const {
  // The .desktop file's name without extension (packaging/linux/), so the
  // desktop can show the app's own name and icon for it.
  return QStringLiteral("io.github.anto294.VCDPlayer");
}

MprisPlayerAdaptor::MprisPlayerAdaptor(Mpris* mpris) : QDBusAbstractAdaptor(mpris), mpris_(mpris) {
  connect(mpris_, &Mpris::seeked, this, &MprisPlayerAdaptor::Seeked);
}

qlonglong MprisPlayerAdaptor::position() const {
  return mpris_->positionSeconds ? static_cast<qlonglong>(mpris_->positionSeconds() * 1e6) : 0;
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath& trackId, qlonglong positionMicroseconds) {
  // Ignored unless it's about the item playing now, per the spec.
  const QVariant current = mpris_->metadata().value(QStringLiteral("mpris:trackid"));
  if (!current.isValid() || current.value<QDBusObjectPath>() != trackId) return;
  emit mpris_->setPositionRequested(positionMicroseconds / 1e6);
}
