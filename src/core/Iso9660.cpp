#include "Iso9660.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace vcd {

namespace {

constexpr int64_t kPvdLbaOffset = 16;  // relative to the data track start (LBA 0 = INDEX01 of track 1)

uint32_t readU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

std::string trimAscii(const uint8_t* data, size_t len) {
  std::string s(reinterpret_cast<const char*>(data), len);
  const auto start = s.find_first_not_of(' ');
  if (start == std::string::npos) return "";
  const auto end = s.find_last_not_of(' ');
  return s.substr(start, end - start + 1);
}

std::string toUpperAscii(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::toupper(c); });
  return out;
}

}  // namespace

PvdInfo readPvd(CdxaReader& reader, int64_t trackStartLba) {
  const std::vector<uint8_t> sector = reader.readForm1Range(trackStartLba + kPvdLbaOffset, 1);
  if (sector[0] != 1 || std::string(reinterpret_cast<const char*>(sector.data() + 1), 5) != "CD001") {
    throw std::runtime_error("ISO9660 Primary Volume Descriptor not found (invalid data track).");
  }
  PvdInfo info;
  info.systemId = trimAscii(sector.data() + 8, 32);
  info.volumeId = trimAscii(sector.data() + 40, 32);
  const uint8_t* rootRecord = sector.data() + 156;
  info.root.lba = readU32LE(rootRecord + 2);
  info.root.length = readU32LE(rootRecord + 10);
  return info;
}

std::vector<DirEntry> listDirectory(CdxaReader& reader, int64_t trackStartLba, int64_t dirLba, int64_t dirLen) {
  const int64_t sectorCount = (dirLen + kForm1DataSize - 1) / kForm1DataSize;
  const std::vector<uint8_t> raw = reader.readForm1Range(trackStartLba + dirLba, sectorCount);

  std::vector<DirEntry> entries;
  size_t off = 0;
  while (off < raw.size()) {
    const uint8_t recLen = raw[off];
    if (recLen == 0) {
      // A record can't cross a logical sector boundary: skip to the next one.
      const size_t nextSectorBoundary = ((off + 1 + kForm1DataSize - 1) / kForm1DataSize) * kForm1DataSize;
      if (nextSectorBoundary >= raw.size()) break;
      off = nextSectorBoundary;
      continue;
    }
    const uint8_t flags = raw[off + 25];
    const uint8_t nameLen = raw[off + 32];
    const std::string rawName(reinterpret_cast<const char*>(raw.data() + off + 33), nameLen);
    const bool isDir = (flags & 0x02) != 0;

    if (rawName.size() == 1 && (rawName[0] == '\0' || rawName[0] == '\x01')) {
      // "." / ".." — self / parent, ignored.
    } else {
      DirEntry e;
      if (isDir) {
        e.name = rawName;
      } else {
        const auto semi = rawName.find(';');
        e.name = semi != std::string::npos ? rawName.substr(0, semi) : rawName;
      }
      e.isDir = isDir;
      e.lba = readU32LE(raw.data() + off + 2);
      e.length = readU32LE(raw.data() + off + 10);
      entries.push_back(std::move(e));
    }
    off += recLen;
  }
  return entries;
}

std::optional<DirEntry> resolvePath(CdxaReader& reader, int64_t trackStartLba, const DirRecord& root,
                                    const std::string& relPath) {
  std::vector<std::string> parts;
  std::istringstream iss(relPath);
  std::string part;
  while (std::getline(iss, part, '/')) {
    if (!part.empty()) parts.push_back(part);
  }
  if (parts.empty()) return std::nullopt;

  DirRecord current = root;
  for (size_t i = 0; i < parts.size(); i++) {
    const std::vector<DirEntry> entries = listDirectory(reader, trackStartLba, current.lba, current.length);
    const std::string wanted = toUpperAscii(parts[i]);
    const auto it =
        std::find_if(entries.begin(), entries.end(), [&](const DirEntry& e) { return toUpperAscii(e.name) == wanted; });
    if (it == entries.end()) return std::nullopt;
    if (i == parts.size() - 1) return *it;
    if (!it->isDir) return std::nullopt;
    current = DirRecord{it->lba, it->length};
  }
  return std::nullopt;
}

}  // namespace vcd
