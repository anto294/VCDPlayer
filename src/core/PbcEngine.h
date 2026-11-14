#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "VcdMeta.h"

namespace vcd {

// Reference (ISO LBA, see imageLayout()) to a Form2 stream already resolved
// by DiscLoader: a video track (MPEGAV/AVSEQnn.DAT) or a segment item
// (SEGMENT/ITEMnnnn.DAT).
struct StreamRef {
  int64_t lba = 0;
  int64_t sectorCount = 0;
};

// Abstracts mpv away from PbcEngine, so the PBC state machine stays
// testable without a real player and has no dependency on libmpv/Qt.
class IPlaybackSink {
 public:
  virtual ~IPlaybackSink() = default;
  virtual void loadAndPlay(int64_t lba, int64_t sectorCount) = 0;
  virtual void play() = 0;
  virtual void pause() = 0;
};

// Abstracts the engine's one-shot timers. Each TimerId is independent;
// scheduling one replaces whatever was pending on that same id.
enum class TimerId {
  Navigation,  // wait/timeout of the current play list or selection node
  DigitEntry,  // commits a partially typed menu number
};

class ITimerHost {
 public:
  virtual ~ITimerHost() = default;
  virtual void scheduleTimeout(TimerId id, int64_t ms, std::function<void()> cb) = 0;
  virtual void cancelTimeout(TimerId id) = 0;
};

// Input from the "remote control". Stop has no dedicated case in the state
// machine (already true in the original version): it stays a no-op,
// exposed only for completeness of the user-facing interface.
enum class RemoteButton { Number, Next, Prev, Return, Play, Pause, Stop };

// PBC (Playback Control) state machine: interprets the PSD.VCD descriptors
// and drives playback through IPlaybackSink, exposing the current state
// (playing / menu with N choices / end) to the UI. Reproduces the
// behavior of a real VCD player: PlayList = automatic playback advancing
// at the end of the track; Selection = shows an item (still/track) and
// waits for numeric input or arrow/Return navigation, with an optional
// timeout toward a default destination.
class PbcEngine {
 public:
  struct State {
    enum class Kind { Loading, Playing, Menu, End };
    Kind kind = Kind::Loading;
    int choiceCount = 0;
    int firstChoice = 1;  // number of the first choice (the node's bsn)
  };

  PbcEngine(VcdInfo info, EntriesFile entriesFile, std::vector<uint8_t> lot, std::vector<uint8_t> psd,
            std::map<int, StreamRef> trackStreams, std::map<int, StreamRef> segmentStreams, IPlaybackSink* sink,
            ITimerHost* timerHost);

  bool pbcAvailable() const;

  // Starts playback from ListID 1 (standard entry point) if the disc has
  // PBC, otherwise from track 2.
  void start();

  void onRemoteButton(RemoteButton btn, int number = 0);

  // Call when mpv signals end-of-file due to reaching eof (not for
  // stop/error/explicit file change).
  void onEof();

  State state() const;

  // Notified (synchronously, on the same thread that calls
  // start()/onRemoteButton()/onEof()) on every state change.
  std::function<void(const State&)> onStateChanged;

 private:
  void goTo(std::optional<int64_t> byteOffset);
  // Returns false if the item can't be played (none, unknown, unresolved).
  bool playItemRef(const ItemRef& ref);
  // Playlist node: play its next item, or once all are done wait its
  // delay and go to nextOfs.
  void playNextPlaylistItem();
  // Selection node whose item finished playing: wait `totime`, then jump.
  void startSelectionWait();
  void playPlainTrack(int trackNumber);
  void end();
  // Selects the menu choice numbered pendingNumber_, if it exists.
  void commitTypedNumber();
  void notify();

  VcdInfo info_;
  EntriesFile entriesFile_;
  std::vector<uint8_t> lot_;
  std::vector<uint8_t> psd_;
  std::map<int, StreamRef> trackStreams_;
  std::map<int, StreamRef> segmentStreams_;
  IPlaybackSink* sink_;
  ITimerHost* timerHost_;

  std::optional<PsdDescriptor> currentDescriptor_;
  std::optional<int> plainTrack_;
  int selectionPlaysLeft_ = 1;  // -1 = loop forever
  int playlistIndex_ = -1;      // item of the current playlist being played
  int pendingNumber_ = -1;      // menu number typed so far (-1 = none)
  bool ended_ = false;
};

}  // namespace vcd
