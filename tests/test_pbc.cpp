// PbcEngine navigation over synthetic PSD/LOT data, with a fake player and
// timers standing in for mpv and QTimer.

#include <map>
#include <optional>

#include "PbcEngine.h"
#include "TestSupport.h"

using vcd::PbcEngine;
using vcd::RemoteButton;
using vcd::TimerId;

namespace {

// Descriptors live in fixed 64-byte slots, so slot k is PSD offset k*64,
// i.e. offset field value k*8 (PSD offsets are in units of 8 bytes).
class PsdBuilder {
 public:
  static constexpr uint16_t kNone = 0xffff;
  static uint16_t slot(int k) { return static_cast<uint16_t>(k * 8); }

  struct Selection {
    int item = 0;  // item id (2..99 = track)
    std::vector<uint16_t> choices;
    int bsn = 1;
    int totime = 255;
    int loop = 1;
    uint16_t timeout = kNone, next = kNone, prev = kNone, ret = kNone, def = kNone;
  };
  struct Playlist {
    std::vector<int> items;
    int wait = 0;
    uint16_t next = kNone, prev = kNone, ret = kNone;
  };

  void selection(int k, const Selection& s) {
    uint8_t* p = at(k);
    p[0] = 0x18;
    p[2] = static_cast<uint8_t>(s.choices.size());
    p[3] = static_cast<uint8_t>(s.bsn);
    test::putBe16(p + 6, s.prev);
    test::putBe16(p + 8, s.next);
    test::putBe16(p + 0x0a, s.ret);
    test::putBe16(p + 0x0c, s.def);
    test::putBe16(p + 0x0e, s.timeout);
    p[0x10] = static_cast<uint8_t>(s.totime);
    p[0x11] = static_cast<uint8_t>(s.loop);
    test::putBe16(p + 0x12, static_cast<uint16_t>(s.item));
    for (size_t i = 0; i < s.choices.size(); i++) test::putBe16(p + 0x14 + i * 2, s.choices[i]);
  }

  void playlist(int k, const Playlist& l) {
    uint8_t* p = at(k);
    p[0] = 0x10;
    p[1] = static_cast<uint8_t>(l.items.size());
    test::putBe16(p + 4, l.prev);
    test::putBe16(p + 6, l.next);
    test::putBe16(p + 8, l.ret);
    p[0x0c] = static_cast<uint8_t>(l.wait);
    for (size_t i = 0; i < l.items.size(); i++) test::putBe16(p + 0x0e + i * 2, static_cast<uint16_t>(l.items[i]));
  }

  std::vector<uint8_t> psd;

  // LOT with list 1 -> slot 0.
  static std::vector<uint8_t> lot(uint16_t list1 = 0) {
    std::vector<uint8_t> l(65536, 0xff);
    test::putBe16(l.data() + 2, list1);
    return l;
  }

 private:
  uint8_t* at(int k) {
    if (psd.size() < static_cast<size_t>(k + 1) * 64) psd.resize(static_cast<size_t>(k + 1) * 64, 0);
    return psd.data() + static_cast<size_t>(k) * 64;
  }
};

struct FakeHost : vcd::IPlaybackSink, vcd::ITimerHost {
  std::vector<int> loadedTracks;  // track number of every loadAndPlay
  int stops = 0;
  struct Pending {
    int64_t ms;
    std::function<void()> cb;
  };
  std::map<TimerId, Pending> timers;

  int64_t lastLba = -1, lastSectors = 0;
  double lastStart = 0;
  void loadAndPlay(int64_t lba, int64_t sectors, double startSeconds) override {
    loadedTracks.push_back(static_cast<int>(lba / 1000));
    lastLba = lba;
    lastSectors = sectors;
    lastStart = startSeconds;
  }
  void play() override {}
  void pause() override {}
  void stop() override { stops++; }
  void scheduleTimeout(TimerId id, int64_t ms, std::function<void()> cb) override { timers[id] = {ms, std::move(cb)}; }
  void cancelTimeout(TimerId id) override { timers.erase(id); }

  bool has(TimerId id) const { return timers.count(id) != 0; }
  void fire(TimerId id) {
    auto it = timers.find(id);
    CHECK(it != timers.end());
    const std::function<void()> cb = std::move(it->second.cb);
    timers.erase(it);
    cb();
  }
  int lastTrack() const { return loadedTracks.empty() ? -1 : loadedTracks.back(); }
};

// Tracks 2..40 exist, track N at lba N*1000.
std::map<int, vcd::StreamRef> tracks() {
  std::map<int, vcd::StreamRef> t;
  for (int n = 2; n <= 40; n++) t[n] = vcd::StreamRef{n * 1000, 10};
  return t;
}

// Chapters: track 2 at its start and 4 sectors in, track 3 at its start.
vcd::EntriesFile entries() {
  vcd::EntriesFile e;
  // The first two have known start times, the last one doesn't.
  e.entries = {{.track = 2, .isoLba = 2000, .trackSeconds = 0},
               {.track = 2, .isoLba = 2004, .trackSeconds = 2.5},
               {.track = 3, .isoLba = 3004}};
  return e;
}

struct Engine {
  FakeHost host;
  PbcEngine engine;
  explicit Engine(const PsdBuilder& b, std::vector<uint8_t> lot = PsdBuilder::lot())
      : engine(vcd::VcdInfo{}, entries(), std::move(lot), b.psd, tracks(), {}, &host, &host) {}
};

// A disc without PBC (no LOT/PSD): plays its tracks in order.
struct PlainEngine {
  FakeHost host;
  PbcEngine engine{vcd::VcdInfo{}, entries(), {}, {}, tracks(), {}, &host, &host};
};

using Kind = PbcEngine::State::Kind;

}  // namespace

TEST(nos0_selection_goes_to_timeout_list_on_eof) {
  // "Play this, then go there" nodes: nos=0, totime=0, only timeoutOfs set.
  PsdBuilder b;
  b.selection(0, {.item = 2, .totime = 0, .timeout = PsdBuilder::slot(1)});
  b.selection(1, {.item = 3, .choices = {PsdBuilder::slot(0)}});
  Engine e(b);
  e.engine.start();
  CHECK_EQ(e.host.lastTrack(), 2);
  CHECK(e.engine.state().kind == Kind::Playing);
  e.engine.onEof();
  CHECK_EQ(e.host.lastTrack(), 3);
  CHECK(e.engine.state().kind == Kind::Menu);
}

TEST(selection_timeout_starts_after_item_finishes) {
  PsdBuilder b;
  b.selection(
      0,
      {.item = 2, .choices = {PsdBuilder::slot(1), PsdBuilder::slot(1)}, .totime = 5, .timeout = PsdBuilder::slot(1)});
  b.selection(1, {.item = 3, .choices = {PsdBuilder::slot(0)}});
  Engine e(b);
  e.engine.start();
  CHECK(!e.host.has(TimerId::Navigation));  // not while the item is still playing
  e.engine.onEof();
  CHECK(e.host.has(TimerId::Navigation));
  CHECK_EQ(e.host.timers[TimerId::Navigation].ms, int64_t{5000});
  e.host.fire(TimerId::Navigation);
  CHECK_EQ(e.host.lastTrack(), 3);
}

TEST(selection_loop_replays_item) {
  PsdBuilder b;
  b.selection(0, {.item = 2, .totime = 0, .loop = 2, .timeout = PsdBuilder::slot(1)});
  b.selection(1, {.item = 3, .choices = {PsdBuilder::slot(0)}});
  Engine e(b);
  e.engine.start();
  e.engine.onEof();
  CHECK_EQ(e.host.lastTrack(), 2);  // second play
  e.engine.onEof();
  CHECK_EQ(e.host.lastTrack(), 3);
  e.engine.onEof();  // a stray EOF on the menu must not replay or jump
  CHECK_EQ(e.host.loadedTracks.size(), size_t{3});
}

TEST(playlist_plays_every_item_then_waits) {
  PsdBuilder b;
  b.playlist(0, {.items = {2, 3, 4}, .wait = 2, .next = PsdBuilder::slot(1)});
  b.selection(1, {.item = 5, .choices = {PsdBuilder::slot(0)}});
  Engine e(b);
  e.engine.start();
  e.engine.onEof();
  e.engine.onEof();
  CHECK((e.host.loadedTracks == std::vector<int>{2, 3, 4}));
  e.engine.onEof();
  CHECK_EQ(e.host.timers[TimerId::Navigation].ms, int64_t{2000});
  e.host.fire(TimerId::Navigation);
  CHECK_EQ(e.host.lastTrack(), 5);
}

namespace {

// Menu with 12 choices numbered from bsn 0: choice k plays track 10 + k.
PsdBuilder twelveChoiceMenu() {
  PsdBuilder b;
  PsdBuilder::Selection menu{.item = 2, .bsn = 0};
  for (int k = 0; k < 12; k++) {
    menu.choices.push_back(PsdBuilder::slot(k + 1));
    b.playlist(k + 1, {.items = {10 + k}});
  }
  b.selection(0, menu);
  return b;
}

}  // namespace

TEST(two_digit_menu_numbers) {
  Engine e(twelveChoiceMenu());
  e.engine.start();
  CHECK_EQ(e.engine.state().firstChoice, 0);
  e.engine.onRemoteButton(RemoteButton::Number, 1);
  CHECK_EQ(e.host.lastTrack(), 2);  // still waiting: 1x could be 10 or 11
  CHECK(e.host.has(TimerId::DigitEntry));
  e.engine.onRemoteButton(RemoteButton::Number, 1);
  CHECK_EQ(e.host.lastTrack(), 21);  // choice 11
}

TEST(digit_that_cannot_grow_selects_at_once) {
  Engine e(twelveChoiceMenu());
  e.engine.start();
  e.engine.onRemoteButton(RemoteButton::Number, 5);
  CHECK_EQ(e.host.lastTrack(), 15);
}

TEST(single_digit_selected_after_pause) {
  Engine e(twelveChoiceMenu());
  e.engine.start();
  e.engine.onRemoteButton(RemoteButton::Number, 0);  // bsn 0: choice 0 exists
  e.host.fire(TimerId::DigitEntry);
  CHECK_EQ(e.host.lastTrack(), 10);
}

TEST(stop_then_play_restarts_disc) {
  PsdBuilder b;
  b.selection(0, {.item = 2, .choices = {PsdBuilder::slot(1)}});
  b.playlist(1, {.items = {3}});
  Engine e(b);
  e.engine.start();
  e.engine.onRemoteButton(RemoteButton::Number, 1);
  CHECK_EQ(e.host.lastTrack(), 3);
  e.engine.onRemoteButton(RemoteButton::Stop);
  CHECK_EQ(e.host.stops, 1);
  CHECK(e.engine.state().kind == Kind::Stopped);
  e.engine.onRemoteButton(RemoteButton::Play);
  CHECK_EQ(e.host.lastTrack(), 2);
  CHECK(e.engine.state().kind == Kind::Menu);
}

TEST(corrupt_psd_offset_ends_disc_without_throwing) {
  PsdBuilder b;
  b.selection(0, {.item = 2});
  Engine e(b, PsdBuilder::lot(/*list1=*/0x7000));  // points far past the PSD
  e.engine.start();
  CHECK(e.engine.state().kind == Kind::End);
}

TEST(zero_wait_cycle_without_playable_items_ends_the_disc) {
  // Corrupt PSD: a selection with nothing to play and totime 0 that times
  // out to itself, and a playlist of unresolvable items with wait 0 whose
  // next is itself. Both used to recurse until the stack overflowed.
  PsdBuilder b;
  b.selection(0, {.item = 0, .totime = 0, .timeout = PsdBuilder::slot(0)});
  b.playlist(1, {.items = {99}, .wait = 0, .next = PsdBuilder::slot(1)});
  Engine sel(b);
  sel.engine.start();
  CHECK(sel.engine.state().kind == Kind::End);

  Engine list(b, PsdBuilder::lot(PsdBuilder::slot(1)));
  list.engine.start();
  CHECK(list.engine.state().kind == Kind::End);
  // Play restarts it, and the cycle is detected again rather than the
  // count carrying over.
  list.engine.onRemoteButton(RemoteButton::Play);
  CHECK(list.engine.state().kind == Kind::End);
}

TEST(short_zero_wait_chain_is_followed) {
  // Two empty nodes chained with totime 0 before a real menu: fine.
  PsdBuilder b;
  b.selection(0, {.item = 0, .totime = 0, .timeout = PsdBuilder::slot(1)});
  b.selection(1, {.item = 0, .totime = 0, .timeout = PsdBuilder::slot(2)});
  b.selection(2, {.item = 5, .choices = {PsdBuilder::slot(0)}});
  Engine e(b);
  e.engine.start();
  CHECK_EQ(e.host.lastTrack(), 5);
  CHECK(e.engine.state().kind == Kind::Menu);
}

TEST(plain_disc_next_prev_step_through_tracks) {
  PlainEngine e;
  e.engine.start();
  CHECK_EQ(e.host.lastTrack(), 2);
  e.engine.onRemoteButton(RemoteButton::Next);
  CHECK_EQ(e.host.lastTrack(), 3);
  e.engine.onRemoteButton(RemoteButton::Prev);
  CHECK_EQ(e.host.lastTrack(), 2);
  e.engine.onRemoteButton(RemoteButton::Prev);  // first track: restart it
  CHECK_EQ(e.host.lastTrack(), 2);
  CHECK_EQ(e.host.loadedTracks.size(), size_t{4});
  e.engine.onRemoteButton(RemoteButton::Return);  // no PBC: nothing to return to
  CHECK_EQ(e.host.loadedTracks.size(), size_t{4});
}

TEST(jump_to_entry_leaves_pbc_and_return_goes_back_to_menu) {
  PsdBuilder b;
  b.selection(0, {.item = 2, .choices = {PsdBuilder::slot(0)}});
  Engine e(b);
  e.engine.start();
  CHECK(e.engine.state().kind == Kind::Menu);
  e.engine.jumpToEntry(1);  // whole track 2, started at the chapter's time
  CHECK_EQ(e.host.lastLba, 2000);
  CHECK_EQ(e.host.lastSectors, 10);
  CHECK_EQ(e.host.lastStart, 2.5);
  e.engine.jumpToEntry(2);  // time unknown: the stream starts at its sector
  CHECK_EQ(e.host.lastLba, 3004);
  CHECK_EQ(e.host.lastSectors, 6);
  CHECK_EQ(e.host.lastStart, 0.0);
  e.engine.jumpToEntry(1);
  CHECK(e.engine.state().kind == Kind::Playing);
  e.engine.onEof();  // carries on with the next track
  CHECK_EQ(e.host.lastTrack(), 3);
  e.engine.onRemoteButton(RemoteButton::Return);
  CHECK(e.engine.state().kind == Kind::Menu);
  e.engine.jumpToEntry(99);  // out of range: ignored
  CHECK(e.engine.state().kind == Kind::Menu);
}

TEST(resume_point_round_trip_in_a_playlist) {
  PsdBuilder b;
  b.playlist(0, {.items = {2, 3, 4}, .next = PsdBuilder::slot(1)});
  b.selection(1, {.item = 5, .choices = {PsdBuilder::slot(0)}});
  Engine e(b);
  e.engine.start();
  e.engine.onEof();
  CHECK_EQ(e.host.lastTrack(), 3);
  const auto point = e.engine.resumePoint();
  CHECK(point.has_value());
  CHECK_EQ(point->psdOffset, 0);
  CHECK_EQ(point->playlistIndex, 1);

  // A fresh engine (disc reopened) picks up from there...
  Engine again(b);
  CHECK(again.engine.resume(*point, 42.5));
  CHECK_EQ(again.host.lastTrack(), 3);
  CHECK_EQ(again.host.lastStart, 42.5);
  CHECK(again.engine.state().kind == Kind::Playing);
  // ...and the play list carries on normally after it.
  again.engine.onEof();
  CHECK_EQ(again.host.lastTrack(), 4);
  again.engine.onEof();
  CHECK_EQ(again.host.lastTrack(), 5);
  CHECK(again.engine.state().kind == Kind::Menu);

  // No resume point in a menu.
  CHECK(!again.engine.resumePoint().has_value());
}

TEST(resume_plain_track_and_reject_bogus_points) {
  PlainEngine e;
  e.engine.start();
  e.engine.onEof();
  const auto point = e.engine.resumePoint();
  CHECK(point.has_value());
  CHECK_EQ(point->plainTrack, 3);

  PlainEngine again;
  CHECK(again.engine.resume(*point, 10));
  again.engine.onEof();
  CHECK_EQ(again.host.lastTrack(), 4);

  PlainEngine fresh;
  vcd::PbcEngine::ResumePoint bogus = *point;
  bogus.streamLba = 999999;  // not on this disc
  CHECK(!fresh.engine.resume(bogus, 10));
  bogus = *point;
  bogus.plainTrack = 0;  // claims a PBC node, but the disc has no PBC
  CHECK(!fresh.engine.resume(bogus, 10));
  CHECK(fresh.host.loadedTracks.empty());
}

TEST(entry_item_past_its_track_is_unplayable) {
  // A play list whose first item is ENTRIES.VCD entry 1 (item id 100 + n),
  // pointing 50 sectors into a 10-sector track: skipped, not played as an
  // empty stream; the next item plays.
  vcd::EntriesFile bad;
  bad.entries = {{.track = 2, .isoLba = 2050}};
  PsdBuilder b;
  b.playlist(0, {.items = {100, 3}});
  FakeHost host;
  PbcEngine engine(vcd::VcdInfo{}, bad, PsdBuilder::lot(), b.psd, tracks(), {}, &host, &host);
  engine.start();
  CHECK_EQ(host.loadedTracks.size(), size_t{1});
  CHECK_EQ(host.lastTrack(), 3);
}
