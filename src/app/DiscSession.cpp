#include "DiscSession.h"

#include <mpv/client.h>

#include <stdexcept>

#include "../mpv/MpvPlayer.h"

DiscSession::DiscSession(MpvPlayer* player, QObject* parent) : QObject(parent), player_(player) {
  registry_.installOn(player_->handle());

  for (PendingTimer* t : {&navigationTimer_, &digitEntryTimer_}) {
    t->timer = new QTimer(this);
    t->timer->setSingleShot(true);
    connect(t->timer, &QTimer::timeout, this, [t]() {
      if (!t->cb) return;
      const std::function<void()> cb = std::move(t->cb);
      t->cb = nullptr;
      cb();
    });
  }

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

  cancelAllTimeouts();
  pbc_.reset();
  disc_ = std::move(loaded);

  pbc_ = std::make_unique<vcd::PbcEngine>(disc_->info, disc_->entriesFile, disc_->lot, disc_->psd, disc_->trackStreams,
                                          disc_->segmentStreams, this, this);
  pbc_->onStateChanged = [this](const vcd::PbcEngine::State& s) {
    emit stateChanged(static_cast<int>(s.kind), s.choiceCount, s.firstChoice);
  };

  emit discLoaded(QString::fromStdString(disc_->volumeId), disc_->info.pbcEnabled,
                  static_cast<int>(disc_->entriesFile.entries.size()));
  pbc_->start();
  return true;
}

bool DiscSession::isIdle() const {
  if (!pbc_) return true;
  const auto kind = pbc_->state().kind;
  return kind == vcd::PbcEngine::State::Kind::Stopped || kind == vcd::PbcEngine::State::Kind::End;
}

void DiscSession::sendRemoteButton(vcd::RemoteButton btn, int number) {
  if (pbc_) pbc_->onRemoteButton(btn, number);
}

void DiscSession::loadAndPlay(int64_t lba, int64_t sectorCount) {
  if (!disc_) return;
  const std::string uri = registry_.registerStream(disc_->image, lba, sectorCount);
  player_->command({"loadfile", QString::fromStdString(uri)});
}

void DiscSession::play() { player_->setPropertyFlag("pause", false); }

void DiscSession::pause() { player_->setPropertyFlag("pause", true); }

// mpv reports this as end-file with reason STOP, not EOF, so onMpvEndFile
// doesn't mistake it for the item finishing.
void DiscSession::stop() { player_->command({"stop"}); }

DiscSession::PendingTimer& DiscSession::timerFor(vcd::TimerId id) {
  return id == vcd::TimerId::DigitEntry ? digitEntryTimer_ : navigationTimer_;
}

void DiscSession::scheduleTimeout(vcd::TimerId id, int64_t ms, std::function<void()> cb) {
  PendingTimer& t = timerFor(id);
  t.cb = std::move(cb);
  t.timer->start(static_cast<int>(ms));
}

void DiscSession::cancelTimeout(vcd::TimerId id) {
  PendingTimer& t = timerFor(id);
  t.timer->stop();
  t.cb = nullptr;
}

void DiscSession::cancelAllTimeouts() {
  cancelTimeout(vcd::TimerId::Navigation);
  cancelTimeout(vcd::TimerId::DigitEntry);
}

void DiscSession::onMpvEndFile(int reason) {
  if (reason == MPV_END_FILE_REASON_EOF && pbc_) pbc_->onEof();
}
