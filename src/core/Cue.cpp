#include "Cue.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace vcd {

namespace fs = std::filesystem;

int64_t msfToLba(int mm, int ss, int ff) { return (static_cast<int64_t>(mm) * 60 + ss) * kFramesPerSecond + ff; }

namespace {

int64_t parseMsf(const std::string& str) {
  static const std::regex kMsfRe(R"(^\s*(\d+):(\d+):(\d+)\s*$)");
  std::smatch m;
  if (!std::regex_match(str, m, kMsfRe)) {
    throw std::runtime_error("Invalid MSF timecode: \"" + str + "\"");
  }
  return msfToLba(std::stoi(m[1]), std::stoi(m[2]), std::stoi(m[3]));
}

int sectorSizeFromMode(const std::string& mode) {
  // "MODE2/2352", "MODE1/2352", "AUDIO", ...
  static const std::regex kModeRe(R"(/(\d+)$)");
  std::smatch m;
  if (std::regex_search(mode, m, kModeRe)) return std::stoi(m[1]);
  return 2352;
}

std::string trim(const std::string& s) {
  const auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

}  // namespace

Disc parseCue(const std::string& cuePath) {
  std::ifstream in(cuePath, std::ios::binary);
  if (!in) throw std::runtime_error("Unable to open .cue file: " + cuePath);
  std::stringstream buf;
  buf << in.rdbuf();
  const std::string text = buf.str();

  static const std::regex kFileRe(R"re(^FILE\s+"([^"]+)"\s+(\S+))re", std::regex::icase);
  static const std::regex kTrackRe(R"re(^TRACK\s+(\d+)\s+(\S+))re", std::regex::icase);
  static const std::regex kPregapRe(R"re(^PREGAP\s+(\S+))re", std::regex::icase);
  static const std::regex kIndexRe(R"re(^INDEX\s+(\d+)\s+(\S+))re", std::regex::icase);

  Disc disc;
  disc.cuePath = cuePath;
  Track* currentTrack = nullptr;
  std::map<int, int64_t> pregapByTrack;  // number -> pregap length (in frames)

  std::istringstream lines(text);
  std::string rawLine;
  while (std::getline(lines, rawLine)) {
    const std::string line = trim(rawLine);
    if (line.empty()) continue;

    std::smatch m;
    if (std::regex_search(line, m, kFileRe)) {
      if (!disc.binPath.empty()) {
        throw std::runtime_error("Only .cue files with a single FILE directive are supported.");
      }
      disc.binPath = (fs::path(cuePath).parent_path() / m[1].str()).string();
      continue;
    }
    if (std::regex_search(line, m, kTrackRe)) {
      disc.tracks.push_back(Track{});
      currentTrack = &disc.tracks.back();
      currentTrack->number = std::stoi(m[1]);
      currentTrack->mode = m[2];
      currentTrack->sectorSize = sectorSizeFromMode(currentTrack->mode);
      continue;
    }
    if (std::regex_search(line, m, kPregapRe)) {
      if (!currentTrack) throw std::runtime_error("PREGAP outside of a TRACK.");
      pregapByTrack[currentTrack->number] = parseMsf(m[1]);
      continue;
    }
    if (std::regex_search(line, m, kIndexRe)) {
      if (!currentTrack) throw std::runtime_error("INDEX outside of a TRACK.");
      currentTrack->indices[std::stoi(m[1])] = parseMsf(m[2]);
      continue;
    }
    // Other directives (CATALOG, FLAGS, ISRC, TITLE, PERFORMER, ...) ignored.
  }

  if (disc.binPath.empty()) throw std::runtime_error("The .cue file has no FILE directive.");
  if (disc.tracks.empty()) throw std::runtime_error("The .cue file has no tracks.");

  for (auto& t : disc.tracks) {
    if (t.indices.find(1) == t.indices.end()) {
      throw std::runtime_error("Track " + std::to_string(t.number) + ": missing INDEX 01.");
    }
    if (t.indices.find(0) == t.indices.end()) {
      // Without an explicit INDEX00, the pregap (if any) precedes INDEX01.
      const auto pIt = pregapByTrack.find(t.number);
      t.indices[0] = pIt != pregapByTrack.end() ? t.indices[1] - pIt->second : t.indices[1];
    }
  }

  return disc;
}

int64_t dataTrackStartLba(const Disc& disc) {
  for (const auto& t : disc.tracks) {
    if (t.number == 1) return t.indices.at(1);
  }
  throw std::runtime_error("Track 1 not found in the .cue file.");
}

}  // namespace vcd
