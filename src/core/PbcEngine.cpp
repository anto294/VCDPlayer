#include "PbcEngine.h"

#include <algorithm>
#include <exception>

namespace vcd {

PbcEngine::PbcEngine(VcdInfo info, EntriesFile entriesFile, std::vector<uint8_t> lot, std::vector<uint8_t> psd,
                     std::map<int, StreamRef> trackStreams, std::map<int, StreamRef> segmentStreams,
                     IPlaybackSink* sink, ITimerHost* timerHost)
    : info_(std::move(info)),
      entriesFile_(std::move(entriesFile)),
      lot_(std::move(lot)),
      psd_(std::move(psd)),
      trackStreams_(std::move(trackStreams)),
      segmentStreams_(std::move(segmentStreams)),
      sink_(sink),
      timerHost_(timerHost) {}

bool PbcEngine::pbcAvailable() const { return !lot_.empty() && !psd_.empty(); }

void PbcEngine::start() {
  ended_ = false;
  stopped_ = false;
  currentDescriptor_.reset();
  plainTrack_.reset();
  if (pbcAvailable()) {
    goTo(lotPsdByteOffset(lot_, 1, info_.offsetMultiplier));
  } else {
    playPlainTrack(2);
  }
}

void PbcEngine::playPlainTrack(int trackNumber) {
  const auto it = trackStreams_.find(trackNumber);
  if (it == trackStreams_.end()) {
    end();
    return;
  }
  plainTrack_ = trackNumber;
  sink_->loadAndPlay(it->second.lba, it->second.sectorCount);
  sink_->play();
  notify();
}

void PbcEngine::goTo(std::optional<int64_t> byteOffset) {
  timerHost_->cancelTimeout(TimerId::Navigation);
  timerHost_->cancelTimeout(TimerId::DigitEntry);
  pendingNumber_ = -1;
  if (!byteOffset) {
    end();
    return;
  }
  PsdDescriptor d;
  try {
    d = readPsdDescriptor(psd_, *byteOffset);
  } catch (const std::exception&) {
    // A corrupt PSD.VCD (offset past its end, truncated descriptor) can't
    // be navigated any further. goTo() runs from UI/timer callbacks, where
    // an exception would escape into the Qt event loop, so the disc just
    // ends here like it would on a real player with an unreadable menu.
    end();
    return;
  }
  currentDescriptor_ = d;

  if (d.type == PsdType::End) {
    end();
    return;
  }
  if (d.type != PsdType::Playlist && d.type != PsdType::Selection) {
    end();
    return;
  }

  if (d.type == PsdType::Playlist) {
    playlistIndex_ = -1;
    notify();
    playNextPlaylistItem();
    return;
  }

  // Selection: its item plays `loop` times (low 7 bits; 0x7f = forever),
  // and only once it's done does the totime wait (and then the jump to
  // the timeout list) start.
  const int loopCount = static_cast<int>(static_cast<unsigned>(d.loop) & 0x7fu);
  selectionPlaysLeft_ = loopCount == 0x7f ? -1 : std::max(1, loopCount);
  notify();
  if (!playItemRef(d.item.ref)) startSelectionWait();  // nothing to play: wait right away
}

void PbcEngine::playNextPlaylistItem() {
  if (!currentDescriptor_ || currentDescriptor_->type != PsdType::Playlist) return;
  const PsdDescriptor& d = *currentDescriptor_;
  // Items that can't be resolved are skipped, like a player would.
  while (++playlistIndex_ < static_cast<int>(d.items.size())) {
    if (playItemRef(d.items[playlistIndex_].ref)) return;
  }

  // Whole list played: wait its "wait time" (same encoding as a selection's
  // totime, 255 = until the user does something), then go to nextOfs.
  const std::optional<int64_t> waitMs = totimeToMs(d.delayTime);
  if (!waitMs || d.nextOfs.kind != PsdOfs::Kind::Offset) return;
  const int64_t offset = d.nextOfs.byteOffset;
  if (*waitMs == 0) {
    goTo(offset);
  } else {
    timerHost_->scheduleTimeout(TimerId::Navigation, *waitMs, [this, offset]() { goTo(offset); });
  }
}

void PbcEngine::startSelectionWait() {
  if (!currentDescriptor_ || currentDescriptor_->type != PsdType::Selection) return;
  const PsdDescriptor& d = *currentDescriptor_;
  const std::optional<int64_t> waitMs = totimeToMs(d.totime);
  if (!waitMs) return;  // totime 255: wait for a choice indefinitely

  // Where the node goes when nobody picks anything. Discs routinely use
  // nos=0 selection nodes with totime=0 as "play this, then go there", and
  // point only timeoutOfs (not nextOfs) at the follow-up list.
  PsdOfs target = d.timeoutOfs;
  if (target.kind != PsdOfs::Kind::Offset) target = d.defaultOfs;
  if (target.kind != PsdOfs::Kind::Offset && d.choices.empty()) target = d.nextOfs;
  if (target.kind != PsdOfs::Kind::Offset) return;

  const int64_t offset = target.byteOffset;
  if (*waitMs == 0) {
    goTo(offset);
  } else {
    timerHost_->scheduleTimeout(TimerId::Navigation, *waitMs, [this, offset]() { goTo(offset); });
  }
}

bool PbcEngine::playItemRef(const ItemRef& itemRef) {
  if (itemRef.kind == ItemRef::Kind::Track) {
    const auto it = trackStreams_.find(itemRef.track);
    if (it == trackStreams_.end()) return false;
    sink_->loadAndPlay(it->second.lba, it->second.sectorCount);
    sink_->play();
    return true;
  }

  if (itemRef.kind == ItemRef::Kind::Segment) {
    const auto it = segmentStreams_.find(itemRef.segmentNumber);
    if (it == segmentStreams_.end()) return false;
    sink_->loadAndPlay(it->second.lba, it->second.sectorCount);
    sink_->play();
    return true;
  }

  if (itemRef.kind == ItemRef::Kind::Entry) {
    if (itemRef.entryIndex < 0 || static_cast<size_t>(itemRef.entryIndex) >= entriesFile_.entries.size()) return false;
    const EntriesEntry& entry = entriesFile_.entries[itemRef.entryIndex];
    const auto it = trackStreams_.find(entry.track);
    if (it == trackStreams_.end()) return false;
    const int64_t startSector = std::max<int64_t>(0, entry.isoLba - it->second.lba);
    sink_->loadAndPlay(it->second.lba + startSector, it->second.sectorCount - startSector);
    sink_->play();
    return true;
  }

  return false;  // None/Unknown: nothing to play
}

void PbcEngine::onEof() {
  if (plainTrack_) {
    // No PBC: simply advance to the next video track.
    const int next = *plainTrack_ + 1;
    if (trackStreams_.count(next)) {
      playPlainTrack(next);
    } else {
      end();
    }
    return;
  }

  if (!currentDescriptor_) return;
  const PsdDescriptor& d = *currentDescriptor_;
  if (d.type == PsdType::Playlist) {
    playNextPlaylistItem();
    return;
  }
  if (d.type == PsdType::Selection) {
    // Replay the item until its loop count runs out, then start waiting
    // (once: a stray EOF after that must not restart the item).
    if (selectionPlaysLeft_ < 0 || selectionPlaysLeft_ > 1) {
      if (selectionPlaysLeft_ > 1) selectionPlaysLeft_--;
      playItemRef(d.item.ref);
    } else if (selectionPlaysLeft_ == 1) {
      selectionPlaysLeft_ = 0;
      startSelectionWait();
    }
  }
}

void PbcEngine::onRemoteButton(RemoteButton btn, int number) {
  const std::optional<PsdDescriptor>& d = currentDescriptor_;
  if (btn == RemoteButton::Number) {
    if (!d || d->type != PsdType::Selection || d->choices.empty() || number < 0 || number > 9) return;
    // Choices are numbered bsn .. bsn+nos-1 and can need more than one
    // digit (10+ choices are common on karaoke discs). Digits accumulate;
    // the number is committed as soon as no further digit could still
    // make a valid choice, otherwise after a short pause, like the
    // numeric entry of a hardware player.
    constexpr int64_t kDigitEntryMs = 1500;
    const int lastChoice = d->bsn + static_cast<int>(d->choices.size()) - 1;
    pendingNumber_ = (pendingNumber_ < 0 ? 0 : pendingNumber_ * 10) + number;
    timerHost_->cancelTimeout(TimerId::DigitEntry);
    if (pendingNumber_ * 10 > lastChoice) {
      commitTypedNumber();
    } else {
      timerHost_->scheduleTimeout(TimerId::DigitEntry, kDigitEntryMs, [this]() { commitTypedNumber(); });
    }
    return;
  }
  if (btn == RemoteButton::Next && d && d->nextOfs.kind == PsdOfs::Kind::Offset) {
    goTo(d->nextOfs.byteOffset);
    return;
  }
  if (btn == RemoteButton::Prev && d && d->prevOfs.kind == PsdOfs::Kind::Offset) {
    goTo(d->prevOfs.byteOffset);
    return;
  }
  if (btn == RemoteButton::Return && d && d->returnOfs.kind == PsdOfs::Kind::Offset) {
    goTo(d->returnOfs.byteOffset);
    return;
  }
  if (btn == RemoteButton::Play) {
    if (stopped_ || ended_) {
      start();
    } else {
      sink_->play();
    }
    return;
  }
  if (btn == RemoteButton::Stop) {
    timerHost_->cancelTimeout(TimerId::Navigation);
    timerHost_->cancelTimeout(TimerId::DigitEntry);
    pendingNumber_ = -1;
    currentDescriptor_.reset();
    plainTrack_.reset();
    stopped_ = true;
    sink_->stop();
    notify();
    return;
  }
  if (btn == RemoteButton::Pause) {
    sink_->pause();
    return;
  }
}

void PbcEngine::commitTypedNumber() {
  const int number = pendingNumber_;
  pendingNumber_ = -1;
  const std::optional<PsdDescriptor>& d = currentDescriptor_;
  if (!d || d->type != PsdType::Selection || number < 0) return;
  const int index = number - d->bsn;
  if (index < 0 || index >= static_cast<int>(d->choices.size())) return;
  const PsdOfs& choice = d->choices[index];
  if (choice.kind == PsdOfs::Kind::Offset) goTo(choice.byteOffset);
}

void PbcEngine::end() {
  ended_ = true;
  currentDescriptor_.reset();
  notify();
}

void PbcEngine::notify() {
  if (onStateChanged) onStateChanged(state());
}

PbcEngine::State PbcEngine::state() const {
  if (stopped_) return State{State::Kind::Stopped, 0};
  if (ended_) return State{State::Kind::End, 0};
  if (!currentDescriptor_) return State{State::Kind::Loading, 0};
  if (currentDescriptor_->type == PsdType::Selection && !currentDescriptor_->choices.empty()) {
    return State{State::Kind::Menu, static_cast<int>(currentDescriptor_->choices.size()), currentDescriptor_->bsn};
  }
  return State{State::Kind::Playing, 0};
}

}  // namespace vcd
