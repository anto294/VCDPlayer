#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vcd {

constexpr int kFramesPerSecond = 75;

// MM:SS:FF -> absolute LBA.
int64_t msfToLba(int mm, int ss, int ff);

struct Track {
  int number = 0;
  std::string mode;  // e.g. "MODE2/2352", "AUDIO"
  int sectorSize = 2352;
  std::map<int, int64_t> indices;  // index number -> absolute LBA in the .bin
};

struct Disc {
  std::string cuePath;
  std::string binPath;
  std::vector<Track> tracks;
};

// Parses a .cue file and returns { binPath, tracks }. Only supports .cue
// files with a single FILE directive (VCDs extracted as multi-track
// .bin/.cue). Throws std::runtime_error on malformed input.
Disc parseCue(const std::string& cuePath);

// Absolute LBA of the data start (INDEX 01) of track 1 — where the
// ISO9660 filesystem begins.
int64_t dataTrackStartLba(const Disc& disc);

}  // namespace vcd
