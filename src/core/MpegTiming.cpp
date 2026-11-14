#include "MpegTiming.h"

#include <exception>
#include <vector>

#include "Cdxa.h"
#include "PbcEngine.h"

namespace vcd {

namespace {

constexpr double kScrHz = 90000.0;
// Stream sectors normally each start with a pack, but tracks end with
// empty sectors (a postgap of 150 or more), which the backward scan from
// the end has to get past. Beyond that, the time is just unknown.
constexpr int kMaxSectorsScanned = 600;

}  // namespace

std::optional<double> firstPackScr(const uint8_t* data, size_t size) {
  constexpr size_t kPackHeaderBytes = 4 + 6;  // start code + SCR (MPEG-2: + extension)
  for (size_t i = 0; i + kPackHeaderBytes <= size; i++) {
    if (data[i] != 0 || data[i + 1] != 0 || data[i + 2] != 1 || data[i + 3] != 0xBA) continue;
    const uint8_t* b = data + i + 4;
    uint64_t scr = 0;
    if ((b[0] & 0xF0) == 0x20) {
      // MPEG-1: '0010' SCR[32..30] 1 SCR[29..15] 1 SCR[14..0] 1
      scr = (uint64_t{b[0]} >> 1 & 0x07) << 30 | uint64_t{b[1]} << 22 | (uint64_t{b[2]} >> 1) << 15 |
            uint64_t{b[3]} << 7 | uint64_t{b[4]} >> 1;
    } else if ((b[0] & 0xC0) == 0x40) {
      // MPEG-2: '01' SCR[32..30] 1 SCR[29..28] SCR[27..15] 1 SCR[14..0] ...
      scr = (uint64_t{b[0]} >> 3 & 0x07) << 30 | (uint64_t{b[0]} & 0x03) << 28 | uint64_t{b[1]} << 20 |
            (uint64_t{b[2]} >> 3) << 15 | (uint64_t{b[2]} & 0x03) << 13 | uint64_t{b[3]} << 5 | uint64_t{b[4]} >> 3;
    } else {
      continue;
    }
    return static_cast<double>(scr) / kScrHz;
  }
  return std::nullopt;
}

std::optional<double> scrNear(CdxaReader& reader, const StreamRef& stream, int64_t lba, bool backward) {
  const int64_t first = stream.lba;
  const int64_t last = stream.lba + stream.sectorCount - 1;
  for (int n = 0; n < kMaxSectorsScanned; n++) {
    const int64_t at = backward ? lba - n : lba + n;
    if (at < first || at > last) break;
    try {
      const std::vector<uint8_t> payload = reader.readForm2Range(at, 1);
      if (const std::optional<double> scr = firstPackScr(payload.data(), payload.size())) return scr;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

}  // namespace vcd
