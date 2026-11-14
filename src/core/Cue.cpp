#include "Cue.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

#include "DiscError.h"

namespace vcd {

namespace fs = std::filesystem;

int64_t msfToLba(int mm, int ss, int ff) { return (static_cast<int64_t>(mm) * 60 + ss) * kFramesPerSecond + ff; }

namespace {

int64_t parseMsf(const std::string& str) {
  static const std::regex kMsfRe(R"(^\s*(\d+):(\d+):(\d+)\s*$)");
  std::smatch m;
  if (!std::regex_match(str, m, kMsfRe)) {
    throw DiscError(DiscError::Code::CueInvalid, "Invalid MSF timecode: \"" + str + "\"");
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

std::string lowerAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// The FILE directive's name as written is tried first. If it doesn't exist
// — typically a sheet made on Windows, where "DISC.BIN" also opens
// disc.bin, or one carrying the ripping machine's absolute path
// ("C:\rips\disc.bin") — fall back to the bare file name, matched
// case-insensitively, next to the .cue itself.
std::string resolveBinPath(const fs::path& cueDir, const std::string& fileName) {
  const fs::path direct = cueDir / fileName;
  std::error_code ec;
  if (fs::exists(direct, ec)) return direct.string();

  const auto sep = fileName.find_last_of("/\\");
  const std::string bareName = sep == std::string::npos ? fileName : fileName.substr(sep + 1);
  const std::string wanted = lowerAscii(bareName);
  for (const auto& entry : fs::directory_iterator(cueDir.empty() ? fs::path(".") : cueDir, ec)) {
    if (entry.is_regular_file(ec) && lowerAscii(entry.path().filename().string()) == wanted) {
      return entry.path().string();
    }
  }
  return direct.string();  // not found: let the open fail with the name as written
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
  if (!in) throw DiscError(DiscError::Code::CueUnreadable, "Unable to open .cue file: " + cuePath);
  std::stringstream buf;
  buf << in.rdbuf();
  std::string text = buf.str();
  // Windows tools (Notepad, some rippers) save .cue files with a UTF-8 byte
  // order mark; left in, it sits in front of the first "FILE" keyword and
  // the whole sheet parses as if it had no FILE directive at all.
  if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);

  static const std::regex kFileRe(R"re(^FILE\s+"([^"]+)"\s+(\S+))re", std::regex::icase);
  static const std::regex kTrackRe(R"re(^TRACK\s+(\d+)\s+(\S+))re", std::regex::icase);
  static const std::regex kPregapRe(R"re(^PREGAP\s+(\S+))re", std::regex::icase);
  static const std::regex kIndexRe(R"re(^INDEX\s+(\d+)\s+(\S+))re", std::regex::icase);

  Disc disc;
  disc.cuePath = cuePath;
  Track* currentTrack = nullptr;

  std::istringstream lines(text);
  std::string rawLine;
  while (std::getline(lines, rawLine)) {
    const std::string line = trim(rawLine);
    if (line.empty()) continue;

    std::smatch m;
    if (std::regex_search(line, m, kFileRe)) {
      disc.files.push_back(resolveBinPath(fs::path(cuePath).parent_path(), m[1].str()));
      continue;
    }
    if (std::regex_search(line, m, kTrackRe)) {
      if (disc.files.empty()) throw DiscError(DiscError::Code::CueInvalid, "TRACK before any FILE directive.");
      disc.tracks.push_back(Track{});
      currentTrack = &disc.tracks.back();
      currentTrack->number = std::stoi(m[1]);
      currentTrack->mode = m[2];
      currentTrack->sectorSize = sectorSizeFromMode(currentTrack->mode);
      currentTrack->fileIndex = disc.files.size() - 1;
      continue;
    }
    if (std::regex_search(line, m, kPregapRe)) {
      if (!currentTrack) throw DiscError(DiscError::Code::CueInvalid, "PREGAP outside of a TRACK.");
      currentTrack->pregap = parseMsf(m[1]);
      continue;
    }
    if (std::regex_search(line, m, kIndexRe)) {
      if (!currentTrack) throw DiscError(DiscError::Code::CueInvalid, "INDEX outside of a TRACK.");
      currentTrack->indices[std::stoi(m[1])] = parseMsf(m[2]);
      continue;
    }
    // Other directives (CATALOG, FLAGS, ISRC, TITLE, PERFORMER, ...) ignored.
  }

  if (disc.files.empty()) throw DiscError(DiscError::Code::CueInvalid, "The .cue file has no FILE directive.");
  if (disc.tracks.empty()) throw DiscError(DiscError::Code::CueInvalid, "The .cue file has no tracks.");

  for (const auto& t : disc.tracks) {
    if (t.indices.find(1) == t.indices.end()) {
      throw DiscError(DiscError::Code::CueInvalid, "Track " + std::to_string(t.number) + ": missing INDEX 01.");
    }
  }

  return disc;
}

DiscImage imageLayout(const Disc& disc) {
  DiscImage image;
  image.files = disc.files;

  // Every file's sectors, laid end to end in FILE order ("global" positions).
  std::vector<int64_t> fileStart;
  int64_t total = 0;
  for (const std::string& path : disc.files) {
    std::error_code ec;
    const auto bytes = fs::file_size(path, ec);
    if (ec) throw DiscError(DiscError::Code::BinUnreadable, "Unable to open .bin file: " + path);
    fileStart.push_back(total);
    total += static_cast<int64_t>(bytes / kSectorSize);
  }

  // ISO LBA 0 = the data track's INDEX 01.
  const Track& dataTrack = disc.tracks.front();
  const int64_t origin = fileStart[dataTrack.fileIndex] + dataTrack.indices.at(1);

  // Region boundaries: each file's start, plus the first stored sector of
  // every later track with a PREGAP — the disc has `pregap` extra sectors
  // right before it, so every ISO LBA from there on is that much further
  // ahead of the file position.
  struct Split {
    int64_t global;
    int64_t gap;
  };
  std::vector<Split> splits;
  splits.reserve(disc.files.size() + disc.tracks.size());
  for (size_t f = 0; f < disc.files.size(); f++) splits.push_back({fileStart[f], 0});
  for (size_t i = 1; i < disc.tracks.size(); i++) {
    const Track& t = disc.tracks[i];
    if (t.pregap <= 0) continue;
    const auto idx0 = t.indices.find(0);
    const int64_t firstStored = idx0 != t.indices.end() ? idx0->second : t.indices.at(1);
    splits.push_back({fileStart[t.fileIndex] + firstStored, t.pregap});
  }
  std::stable_sort(splits.begin(), splits.end(), [](const Split& a, const Split& b) { return a.global < b.global; });

  int64_t gapSoFar = 0;
  for (size_t k = 0; k < splits.size(); k++) {
    gapSoFar += splits[k].gap;
    const int64_t start = splits[k].global;
    const int64_t end = k + 1 < splits.size() ? splits[k + 1].global : total;
    if (end <= start) continue;
    size_t file = 0;
    while (file + 1 < fileStart.size() && fileStart[file + 1] <= start) file++;
    image.regions.push_back(ImageRegion{file, start - fileStart[file], start - origin + gapSoFar, end - start});
  }
  return image;
}

}  // namespace vcd
