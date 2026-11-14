#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>

// MPRIS 2 (org.mpris.MediaPlayer2) on the session bus, Linux/BSD only: lets
// the desktop drive the player from the global media keys, the panel's
// media widget, the lock screen, KDE Connect, playerctl, ... The service
// only reports state and forwards requests; MainWindow does the rest.
class Mpris : public QObject {
  Q_OBJECT

 public:
  explicit Mpris(QObject* parent = nullptr);

  // "Playing", "Paused" or "Stopped".
  void setPlaybackStatus(const QString& status);
  // Current disc: title (empty = no disc) and the playing item's length.
  void setMetadata(const QString& title, const QString& album, double lengthSeconds);
  void setVolume(double volume01);
  // Reported to clients that poll the position (it isn't signalled).
  std::function<double()> positionSeconds;

  QString playbackStatus() const { return playbackStatus_; }
  QVariantMap metadata() const;
  double volume() const { return volume_; }
  bool hasDisc() const { return !title_.isEmpty(); }
  bool canSeek() const { return lengthSeconds_ > 0; }

 signals:
  void raiseRequested();
  void quitRequested();
  void playRequested();
  void pauseRequested();
  void playPauseRequested();
  void stopRequested();
  void nextRequested();
  void previousRequested();
  void seekRequested(double offsetSeconds);           // relative
  void setPositionRequested(double positionSeconds);  // absolute
  void volumeRequested(double volume01);
  void seeked(qlonglong positionMicroseconds);

 private:
  void notifyChanged(const QVariantMap& changed);

  QString playbackStatus_ = QStringLiteral("Stopped");
  QString title_;
  QString album_;
  double lengthSeconds_ = 0;
  double volume_ = 1.0;
  int trackSerial_ = 0;  // bumped per item, for mpris:trackid
};

// org.mpris.MediaPlayer2
class MprisRootAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
  Q_PROPERTY(bool CanQuit READ canQuit)
  Q_PROPERTY(bool CanRaise READ canRaise)
  Q_PROPERTY(bool HasTrackList READ hasTrackList)
  Q_PROPERTY(QString Identity READ identity)
  Q_PROPERTY(QString DesktopEntry READ desktopEntry)
  Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
  Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

 public:
  explicit MprisRootAdaptor(Mpris* mpris) : QDBusAbstractAdaptor(mpris), mpris_(mpris) {}

  bool canQuit() const { return true; }
  bool canRaise() const { return true; }
  bool hasTrackList() const { return false; }
  QString identity() const { return QStringLiteral("VCD Player"); }
  QString desktopEntry() const;
  QStringList supportedUriSchemes() const { return {}; }
  QStringList supportedMimeTypes() const { return {}; }

 public slots:
  void Raise() { emit mpris_->raiseRequested(); }
  void Quit() { emit mpris_->quitRequested(); }

 private:
  Mpris* mpris_;
};

// org.mpris.MediaPlayer2.Player
class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
  Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
  Q_PROPERTY(double Rate READ rate WRITE setRate)
  Q_PROPERTY(double MinimumRate READ rate)
  Q_PROPERTY(double MaximumRate READ rate)
  Q_PROPERTY(QVariantMap Metadata READ metadata)
  Q_PROPERTY(double Volume READ volume WRITE setVolume)
  Q_PROPERTY(qlonglong Position READ position)
  Q_PROPERTY(bool CanGoNext READ hasDisc)
  Q_PROPERTY(bool CanGoPrevious READ hasDisc)
  Q_PROPERTY(bool CanPlay READ hasDisc)
  Q_PROPERTY(bool CanPause READ hasDisc)
  Q_PROPERTY(bool CanSeek READ canSeek)
  Q_PROPERTY(bool CanControl READ canControl)

 public:
  explicit MprisPlayerAdaptor(Mpris* mpris);

  QString playbackStatus() const { return mpris_->playbackStatus(); }
  double rate() const { return 1.0; }
  void setRate(double) {}  // fixed: a Video CD plays at normal speed
  QVariantMap metadata() const { return mpris_->metadata(); }
  double volume() const { return mpris_->volume(); }
  void setVolume(double v) { emit mpris_->volumeRequested(v); }
  qlonglong position() const;
  bool hasDisc() const { return mpris_->hasDisc(); }
  bool canSeek() const { return mpris_->canSeek(); }
  bool canControl() const { return true; }

 public slots:
  void Next() { emit mpris_->nextRequested(); }
  void Previous() { emit mpris_->previousRequested(); }
  void Pause() { emit mpris_->pauseRequested(); }
  void PlayPause() { emit mpris_->playPauseRequested(); }
  void Stop() { emit mpris_->stopRequested(); }
  void Play() { emit mpris_->playRequested(); }
  void Seek(qlonglong offsetMicroseconds) { emit mpris_->seekRequested(static_cast<double>(offsetMicroseconds) / 1e6); }
  void SetPosition(const QDBusObjectPath& trackId, qlonglong positionMicroseconds);
  void OpenUri(const QString&) {}

 signals:
  void Seeked(qlonglong Position);  // NOLINT(readability-identifier-naming): D-Bus member name

 private:
  Mpris* mpris_;
};
