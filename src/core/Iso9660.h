#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Cdxa.h"

namespace vcd {

struct DirRecord {
  int64_t lba = 0;
  int64_t length = 0;
};

struct PvdInfo {
  std::string systemId;
  std::string volumeId;
  DirRecord root;
};

struct DirEntry {
  std::string name;
  bool isDir = false;
  int64_t lba = 0;
  int64_t length = 0;
};

// Reads the Primary Volume Descriptor (ISO LBA 16) and extracts its root
// directory record. All LBAs here are ISO LBAs (see imageLayout()).
PvdInfo readPvd(CdxaReader& reader);

// Lists a directory's entries (files and subdirectories), resolving
// absolute LBAs on the disc. "." and ".." are excluded.
std::vector<DirEntry> listDirectory(CdxaReader& reader, int64_t dirLba, int64_t dirLen);

// Resolves a path like "VCD/INFO.VCD" (case-insensitive) starting from root.
std::optional<DirEntry> resolvePath(CdxaReader& reader, const DirRecord& root, const std::string& relPath);

}  // namespace vcd
