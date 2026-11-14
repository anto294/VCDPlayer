#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace vcd {

class CdxaReader;
struct StreamRef;

// Playback times of Video CD MPEG streams, read from the SCR (system clock
// reference, 90 kHz) of their MPEG pack headers. A track's sector count
// says nothing reliable about its length: tracks aren't always muxed at
// exactly 75 sectors per second (one real disc: 200494 sectors, 38:56).

// SCR in seconds of the first MPEG-1 or MPEG-2 pack header in data.
std::optional<double> firstPackScr(const uint8_t* data, size_t size);

// SCR of the first pack header found scanning from sector `lba` towards
// the end of `stream` (or, with backward, towards its start), looking at
// a few sectors at most. Never throws: unreadable means unknown.
std::optional<double> scrNear(CdxaReader& reader, const StreamRef& stream, int64_t lba, bool backward = false);

}  // namespace vcd
