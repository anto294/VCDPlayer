#include "PbcEngine.h"

#include <algorithm>

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
  timerHost_->cancelTimeout();
  if (!byteOffset) {
    end();
    return;
  }
  const PsdDescriptor d = readPsdDescriptor(psd_, *byteOffset);
  currentDescriptor_ = d;

  if (d.type == PsdType::End) {
    end();
    return;
  }
  if (d.type != PsdType::Playlist && d.type != PsdType::Selection) {
    end();
    return;
  }

  const ItemRef item = d.type == PsdType::Playlist ? (d.items.empty() ? ItemRef{} : d.items[0].ref) : d.item.ref;
  playItemRef(item);

  if (d.type == PsdType::Selection && !d.choices.empty()) {
    const std::optional<int64_t> timeoutMs = totimeToMs(d.totime);
    if (timeoutMs && d.timeoutOfs.kind == PsdOfs::Kind::Offset) {
      const int64_t offset = d.timeoutOfs.byteOffset;
      timerHost_->scheduleTimeout(*timeoutMs, [this, offset]() { goTo(offset); });
    }
  }

  notify();
}

void PbcEngine::playItemRef(const ItemRef& itemRef) {
  if (itemRef.kind == ItemRef::Kind::None || itemRef.kind == ItemRef::Kind::Unknown) return;

  if (itemRef.kind == ItemRef::Kind::Track) {
    const auto it = trackStreams_.find(itemRef.track);
    if (it == trackStreams_.end()) return;
    sink_->loadAndPlay(it->second.lba, it->second.sectorCount);
    sink_->play();
    return;
  }

  if (itemRef.kind == ItemRef::Kind::Segment) {
    const auto it = segmentStreams_.find(itemRef.segmentNumber);
    if (it == segmentStreams_.end()) return;
    sink_->loadAndPlay(it->second.lba, it->second.sectorCount);
    sink_->play();
    return;
  }

  if (itemRef.kind == ItemRef::Kind::Entry) {
    if (itemRef.entryIndex < 0 || static_cast<size_t>(itemRef.entryIndex) >= entriesFile_.entries.size()) return;
    const EntriesEntry& entry = entriesFile_.entries[itemRef.entryIndex];
    const auto it = trackStreams_.find(entry.track);
    if (it == trackStreams_.end()) return;
    const int64_t startSector = std::max<int64_t>(0, entry.isoLba - it->second.lba);
    sink_->loadAndPlay(it->second.lba + startSector, it->second.sectorCount - startSector);
    sink_->play();
  }
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
  if (d.type == PsdType::Playlist && d.nextOfs.kind == PsdOfs::Kind::Offset) {
    goTo(d.nextOfs.byteOffset);
  } else if (d.type == PsdType::Selection && d.choices.empty() && d.nextOfs.kind == PsdOfs::Kind::Offset) {
    // Non-interactive node (nos=0): behaves like a playlist, advances automatically.
    goTo(d.nextOfs.byteOffset);
  }
  // Node with numeric choices: once playback ends, it waits for user input.
}

void PbcEngine::onRemoteButton(RemoteButton btn, int number) {
  const std::optional<PsdDescriptor>& d = currentDescriptor_;
  if (btn == RemoteButton::Number) {
    if (d && d->type == PsdType::Selection && number >= 1 && number <= static_cast<int>(d->choices.size())) {
      const PsdOfs& choice = d->choices[number - 1];
      if (choice.kind == PsdOfs::Kind::Offset) goTo(choice.byteOffset);
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
    sink_->play();
    return;
  }
  if (btn == RemoteButton::Pause) {
    sink_->pause();
    return;
  }
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
  if (ended_) return State{State::Kind::End, 0};
  if (!currentDescriptor_) return State{State::Kind::Loading, 0};
  if (currentDescriptor_->type == PsdType::Selection && !currentDescriptor_->choices.empty()) {
    return State{State::Kind::Menu, static_cast<int>(currentDescriptor_->choices.size())};
  }
  return State{State::Kind::Playing, 0};
}

}  // namespace vcd
