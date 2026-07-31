#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vcd {

// All MSF/BCD timecodes in the VCD files (ENTRIES.VCD, INFO.VCD
// First_segment_addr, ...) are expressed as absolute CD-ROM time, which
// includes the standard 2s physical pregap (150 sectors) before the data
// track starts. The "ISO" (relative) LBA used by Iso9660.cpp/Cdxa.cpp is
// therefore always MSF-150.
constexpr int64_t kCdLeadinSectors = 150;

int64_t absoluteMsfToIsoLba(int mm, int ss, int ff);

// ---------------------------------------------------------------------------
// INFO.VCD
// ---------------------------------------------------------------------------
struct VcdInfo {
  std::string id;
  uint8_t versionMajor = 0;
  uint8_t systemProfileTag = 0;
  std::string album;
  uint16_t totalCds = 0;
  uint16_t thisCd = 0;
  uint8_t statusFlags = 0;
  bool pbcEnabled = false;
  uint32_t psdSize = 0;
  int firstSegmentMsf[3] = {0, 0, 0};
  int64_t firstSegmentIsoLba = 0;
  uint8_t offsetMultiplier = 8;
  uint16_t listIdsCount = 0;
  uint16_t itemFilesCount = 0;
};

VcdInfo parseInfoVcd(const std::vector<uint8_t>& buf);

// ---------------------------------------------------------------------------
// ENTRIES.VCD
// ---------------------------------------------------------------------------
struct EntriesEntry {
  int track = 0;
  int mm = 0;
  int ss = 0;
  int ff = 0;
  int64_t isoLba = 0;
};

struct EntriesFile {
  std::string id;
  uint8_t version = 0;
  uint8_t systemProfileTag = 0;
  uint16_t count = 0;
  std::vector<EntriesEntry> entries;
};

EntriesFile parseEntriesVcd(const std::vector<uint8_t>& buf);

// ---------------------------------------------------------------------------
// LOT.VCD — ListID -> PsdOffset table (in units of `offsetMultiplier` bytes)
// ---------------------------------------------------------------------------
// nullopt if the ListID is disabled or out of the buffer bounds.
std::optional<int64_t> lotPsdByteOffset(const std::vector<uint8_t>& lot, int listId, int offsetMultiplier = 8);

// ---------------------------------------------------------------------------
// PSD.VCD — Play Sequence Descriptors
// ---------------------------------------------------------------------------
struct PsdOfs {
  enum class Kind { Disabled, MultiDefault, Offset };
  Kind kind = Kind::Disabled;
  int64_t byteOffset = 0;
};

PsdOfs psdOfs(uint16_t raw);

// Interprets the item id (2 bytes) according to the VCD 2.0 standard ranges.
struct ItemRef {
  enum class Kind { None, Track, Entry, Segment, Unknown };
  Kind kind = Kind::None;
  int track = 0;
  int entryIndex = 0;
  int segmentNumber = 0;
  int raw = 0;
};

ItemRef decodeItemId(int itemId);

struct PsdItem {
  int itemId = 0;
  ItemRef ref;
};

enum class PsdType { Playlist, Selection, End, Unknown };

struct PsdDescriptor {
  PsdType type = PsdType::Unknown;
  int64_t byteOffset = 0;
  int64_t byteLength = 0;

  // playlist
  int listId = 0;
  PsdOfs prevOfs, nextOfs, returnOfs;
  int playTime = 0;
  int delayTime = 0;
  int autoPauseTime = 0;
  std::vector<PsdItem> items;

  // selection
  bool extended = false;
  int flags = 0;
  int bsn = 0;
  PsdOfs defaultOfs, timeoutOfs;
  int totime = 0;
  int loop = 0;
  PsdItem item;
  std::vector<PsdOfs> choices;

  // end
  int nextDisc = 0;

  // unknown
  uint8_t rawType = 0;
};

// Reads a single PSD descriptor at the given byte offset (relative to the
// start of PSD.VCD).
PsdDescriptor readPsdDescriptor(const std::vector<uint8_t>& buf, int64_t byteOffset);

// Converts the VCD totime field (1 byte) to milliseconds. nullopt = no timeout.
std::optional<int64_t> totimeToMs(int totime);

}  // namespace vcd
