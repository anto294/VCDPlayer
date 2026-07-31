#include "VcdMeta.h"

#include "Cue.h"

namespace vcd {

namespace {

uint8_t bcd(uint8_t byte) { return static_cast<uint8_t>((byte >> 4) * 10 + (byte & 0x0f)); }

uint16_t readU16BE(const std::vector<uint8_t>& buf, size_t off) {
  return static_cast<uint16_t>((static_cast<uint16_t>(buf[off]) << 8) | buf[off + 1]);
}

uint32_t readU32BE(const std::vector<uint8_t>& buf, size_t off) {
  return (static_cast<uint32_t>(buf[off]) << 24) | (static_cast<uint32_t>(buf[off + 1]) << 16) |
         (static_cast<uint32_t>(buf[off + 2]) << 8) | static_cast<uint32_t>(buf[off + 3]);
}

std::string trimAscii(const std::vector<uint8_t>& buf, size_t off, size_t len) {
  std::string s(reinterpret_cast<const char*>(buf.data() + off), len);
  const auto start = s.find_first_not_of(' ');
  if (start == std::string::npos) return "";
  const auto end = s.find_last_not_of(' ');
  return s.substr(start, end - start + 1);
}

}  // namespace

int64_t absoluteMsfToIsoLba(int mm, int ss, int ff) { return msfToLba(mm, ss, ff) - kCdLeadinSectors; }

VcdInfo parseInfoVcd(const std::vector<uint8_t>& buf) {
  VcdInfo info;
  info.id = std::string(reinterpret_cast<const char*>(buf.data()), 8);
  info.versionMajor = buf[8];
  info.systemProfileTag = buf[9];
  info.album = trimAscii(buf, 0x0a, 16);
  info.totalCds = readU16BE(buf, 0x1a);
  info.thisCd = readU16BE(buf, 0x1c);
  info.statusFlags = buf[0x2b];
  info.psdSize = readU32BE(buf, 0x2c);
  info.pbcEnabled = info.psdSize > 0;
  info.firstSegmentMsf[0] = bcd(buf[0x30]);
  info.firstSegmentMsf[1] = bcd(buf[0x31]);
  info.firstSegmentMsf[2] = bcd(buf[0x32]);
  info.firstSegmentIsoLba =
      absoluteMsfToIsoLba(info.firstSegmentMsf[0], info.firstSegmentMsf[1], info.firstSegmentMsf[2]);
  info.offsetMultiplier = buf[0x33] != 0 ? buf[0x33] : 8;
  info.listIdsCount = readU16BE(buf, 0x34);
  info.itemFilesCount = readU16BE(buf, 0x36);
  return info;
}

EntriesFile parseEntriesVcd(const std::vector<uint8_t>& buf) {
  EntriesFile f;
  f.id = std::string(reinterpret_cast<const char*>(buf.data()), 8);
  f.version = buf[8];
  f.systemProfileTag = buf[9];
  f.count = readU16BE(buf, 0x0a);
  f.entries.reserve(f.count);
  for (int i = 0; i < f.count; i++) {
    const size_t off = 0x0c + static_cast<size_t>(i) * 4;
    EntriesEntry e;
    e.track = bcd(buf[off]);
    e.mm = bcd(buf[off + 1]);
    e.ss = bcd(buf[off + 2]);
    e.ff = bcd(buf[off + 3]);
    e.isoLba = absoluteMsfToIsoLba(e.mm, e.ss, e.ff);
    f.entries.push_back(e);
  }
  return f;
}

std::optional<int64_t> lotPsdByteOffset(const std::vector<uint8_t>& lot, int listId, int offsetMultiplier) {
  if (listId < 1) return std::nullopt;
  const size_t pos = 2 + static_cast<size_t>(listId - 1) * 2;
  if (pos + 2 > lot.size()) return std::nullopt;
  const uint16_t raw = readU16BE(lot, pos);
  if (raw == 0xffff) return std::nullopt;
  return static_cast<int64_t>(raw) * offsetMultiplier;
}

PsdOfs psdOfs(uint16_t raw) {
  if (raw == 0xffff) return PsdOfs{PsdOfs::Kind::Disabled, 0};
  if (raw == 0xfffe || raw == 0xfffd) return PsdOfs{PsdOfs::Kind::MultiDefault, 0};
  return PsdOfs{PsdOfs::Kind::Offset, static_cast<int64_t>(raw) * 8};
}

ItemRef decodeItemId(int itemId) {
  ItemRef ref;
  ref.raw = itemId;
  if (itemId <= 1) {
    ref.kind = ItemRef::Kind::None;
  } else if (itemId <= 99) {
    ref.kind = ItemRef::Kind::Track;
    ref.track = itemId;
  } else if (itemId <= 599) {
    ref.kind = ItemRef::Kind::Entry;
    ref.entryIndex = itemId - 100;
  } else if (itemId <= 2979) {
    ref.kind = ItemRef::Kind::Segment;
    ref.segmentNumber = itemId - 1000 + 1;
  } else {
    ref.kind = ItemRef::Kind::Unknown;
  }
  return ref;
}

PsdDescriptor readPsdDescriptor(const std::vector<uint8_t>& buf, int64_t byteOffset) {
  const size_t o = static_cast<size_t>(byteOffset);
  const uint8_t type = buf[o];
  PsdDescriptor d;
  d.byteOffset = byteOffset;

  if (type == 0x10) {  // PSD_TYPE_PLAYLIST
    const uint8_t noi = buf[o + 1];
    d.type = PsdType::Playlist;
    d.listId = readU16BE(buf, o + 2);
    d.prevOfs = psdOfs(readU16BE(buf, o + 4));
    d.nextOfs = psdOfs(readU16BE(buf, o + 6));
    d.returnOfs = psdOfs(readU16BE(buf, o + 8));
    d.playTime = readU16BE(buf, o + 0x0a);
    d.delayTime = buf[o + 0x0c];
    d.autoPauseTime = buf[o + 0x0d];
    d.items.reserve(noi);
    for (int i = 0; i < noi; i++) {
      const int itemId = readU16BE(buf, o + 0x0e + static_cast<size_t>(i) * 2);
      d.items.push_back(PsdItem{itemId, decodeItemId(itemId)});
    }
    d.byteLength = 0x0e + static_cast<int64_t>(noi) * 2;
    return d;
  }

  if (type == 0x18 || type == 0x1a) {  // PSD_TYPE_SELECTION / PSD_TYPE_SELECTION_EXT
    const uint8_t nos = buf[o + 2];
    d.type = PsdType::Selection;
    d.extended = type == 0x1a;
    d.flags = buf[o + 1];
    d.bsn = buf[o + 3];
    d.listId = readU16BE(buf, o + 4);
    d.prevOfs = psdOfs(readU16BE(buf, o + 6));
    d.nextOfs = psdOfs(readU16BE(buf, o + 8));
    d.returnOfs = psdOfs(readU16BE(buf, o + 0x0a));
    d.defaultOfs = psdOfs(readU16BE(buf, o + 0x0c));
    d.timeoutOfs = psdOfs(readU16BE(buf, o + 0x0e));
    d.totime = buf[o + 0x10];
    d.loop = buf[o + 0x11];
    const int itemId = readU16BE(buf, o + 0x12);
    d.item = PsdItem{itemId, decodeItemId(itemId)};
    d.choices.reserve(nos);
    for (int i = 0; i < nos; i++) {
      d.choices.push_back(psdOfs(readU16BE(buf, o + 0x14 + static_cast<size_t>(i) * 2)));
    }
    d.byteLength = 0x14 + static_cast<int64_t>(nos) * 2 + (d.extended ? 16 + 4 * nos : 0);
    return d;
  }

  if (type == 0x1f) {  // PSD_TYPE_END
    d.type = PsdType::End;
    d.nextDisc = buf[o + 1];
    const int itemId = readU16BE(buf, o + 2);
    d.item = PsdItem{itemId, decodeItemId(itemId)};
    d.byteLength = 8;
    return d;
  }

  d.type = PsdType::Unknown;
  d.rawType = type;
  d.byteLength = 1;
  return d;
}

std::optional<int64_t> totimeToMs(int totime) {
  if (totime >= 255) return std::nullopt;
  if (totime <= 60) return static_cast<int64_t>(totime) * 1000;
  return static_cast<int64_t>((totime - 60) * 10 + 60) * 1000;
}

}  // namespace vcd
