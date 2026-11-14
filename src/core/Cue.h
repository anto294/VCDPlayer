#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Cdxa.h"

namespace vcd {

constexpr int kFramesPerSecond = 75;

// MM:SS:FF -> absolute LBA.
int64_t msfToLba(int mm, int ss, int ff);

struct Track {
  int number = 0;
  std::string mode;  // e.g. "MODE2/2352", "AUDIO"
  int sectorSize = 2352;
  size_t fileIndex = 0;            // which Disc::files entry stores this track
  std::map<int, int64_t> indices;  // index number -> sector position within that file
  int64_t pregap = 0;              // PREGAP sectors: part of the disc, but not stored in the file
};

struct Disc {
  std::string cuePath;
  std::vector<std::string> files;  // FILE directives in order, resolved to real paths
  std::vector<Track> tracks;
};

// Parses a .cue file: a single .bin holding every track, or one FILE per
// track (or any mix). Throws std::runtime_error on malformed input.
Disc parseCue(const std::string& cuePath);

// Builds the map from the disc's ISO LBAs to positions in the image
// file(s). ISO LBA 0 is track 1's INDEX 01, which is the numbering ISO9660
// directory records and the VCD/*.VCD files use. That numbering is NOT the
// same as the position in the .bin as soon as a track has a PREGAP: those
// sectors exist on the disc (and are counted by every LBA after them) but
// aren't stored in the file. Reads the image files' sizes; throws
// std::runtime_error if one is missing.
DiscImage imageLayout(const Disc& disc);

}  // namespace vcd
