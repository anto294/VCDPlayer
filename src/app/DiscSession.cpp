#include "DiscSession.h"

#include <mpv/client.h>

#include <stdexcept>

#include "../mpv/MpvPlayer.h"

DiscSession::DiscSession(MpvPlayer* player, QObject* parent) : QObject(parent), player_(player) {
  registry_.installOn(player_->handle());

  timeoutTimer_ = new QTimer(this);
  timeoutTimer_->setSingleShot(true);
  connect(timeoutTimer_, &QTimer::timeout, this, [this]() {
    if (!pendingTimeoutCb_) return;
    const std::function<void()> cb = std::move(pendingTimeoutCb_);
    pendingTimeoutCb_ = nullptr;
    cb();
  });

  connect(player_, &MpvPlayer::endFile, this, &DiscSession::onMpvEndFile);
}

bool DiscSession::openDiscPath(const QString& cuePath, QString* errorMessage) {
  vcd::LoadedDisc loaded;
  try {
    loaded = vcd::loadDisc(cuePath.toStdString());
  } catch (const std::exception& e) {
    if (errorMessage) *errorMessage = QString::fromUtf8(e.what());
    return false;
  }

  cancelTimeout();
  pbc_.reset();
  disc_ = std::move(loaded);

  pbc_ = std::make_unique<vcd::PbcEngine>(disc_->info, disc_->entriesFile, disc_->lot, disc_->psd, disc_->trackStreams,
                                          disc_->segmentStreams, this, this);
  pbc_->onStateChanged = [this](const vcd::PbcEngine::State& s) {
    emit stateChanged(static_cast<int>(s.kind), s.choiceCount);
  };

  emit discLoaded(QString::fromStdString(disc_->volumeId), disc_->info.pbcEnabled,
                  static_cast<int>(disc_->entriesFile.entries.size()));
  pbc_->start();
  return true;
}

void DiscSession::sendRemoteButton(vcd::RemoteButton btn, int number) {
  if (pbc_) pbc_->onRemoteButton(btn, number);
}

void DiscSession::loadAndPlay(int64_t lba, int64_t sectorCount) {
  if (!disc_) return;
  const std::string uri = registry_.registerStream(disc_->disc.binPath, lba, sectorCount);
  player_->command({"loadfile", QString::fromStdString(uri)});
}

void DiscSession::play() { player_->setPropertyFlag("pause", false); }

void DiscSession::pause() { player_->setPropertyFlag("pause", true); }

void DiscSession::scheduleTimeout(int64_t ms, std::function<void()> cb) {
  pendingTimeoutCb_ = std::move(cb);
  timeoutTimer_->start(static_cast<int>(ms));
}

void DiscSession::cancelTimeout() {
  timeoutTimer_->stop();
  pendingTimeoutCb_ = nullptr;
}

void DiscSession::onMpvEndFile(int reason) {
  if (reason == MPV_END_FILE_REASON_EOF && pbc_) pbc_->onEof();
}
