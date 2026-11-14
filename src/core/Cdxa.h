#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace vcd {

constexpr int kSectorSize = 2352;
constexpr int kSyncHeaderSize = 16;                                 // 12 sync + 4 header (MM:SS:FF:MODE)
constexpr int kSubheaderSize = 8;                                   // subheader duplicated 2x4 bytes
constexpr int kForm1DataOffset = kSyncHeaderSize + kSubheaderSize;  // 24
constexpr int kForm1DataSize = 2048;
constexpr int kForm2DataSize = 2324;

constexpr uint8_t kSubmodeEor = 0x01;
constexpr uint8_t kSubmodeVideo = 0x02;
constexpr uint8_t kSubmodeAudio = 0x04;
constexpr uint8_t kSubmodeData = 0x08;
constexpr uint8_t kSubmodeTrigger = 0x10;
constexpr uint8_t kSubmodeForm2 = 0x20;
constexpr uint8_t kSubmodeRealtime = 0x40;
constexpr uint8_t kSubmodeEof = 0x80;

struct Subheader {
  uint8_t file = 0;
  uint8_t channel = 0;
  uint8_t submode = 0;
  uint8_t coding = 0;
  bool isForm2 = false;
  bool isVideo = false;
  bool isAudio = false;
  bool isData = false;
  bool isTrigger = false;
  bool isRealtime = false;
  bool isEndOfRecord = false;
  bool isEndOfFile = false;
};

// Reads/interprets raw CD-XA sectors (2352 bytes) from a .bin file.
class CdxaReader {
 public:
  // Opens the .bin file. Throws std::runtime_error if it can't be opened.
  static CdxaReader open(const std::string& binPath);

  CdxaReader() = default;
  CdxaReader(CdxaReader&& other) noexcept;
  CdxaReader& operator=(CdxaReader&& other) noexcept;
  CdxaReader(const CdxaReader&) = delete;
  CdxaReader& operator=(const CdxaReader&) = delete;
  ~CdxaReader();

  void close();

  // Number of whole 2352-byte sectors in the opened .bin file.
  int64_t sectorCount() const { return sectorCount_; }

  // Reads the full raw sector (2352 bytes) at the given LBA into out (must
  // have at least kSectorSize bytes).
  void readSectorRaw(int64_t lba, uint8_t* out);

  static Subheader subheader(const uint8_t* sectorBuf);

  // The sector's useful payload: 2048B (Form1) or 2324B (Form2), depending
  // on the subheader. Returns a pointer inside sectorBuf (no copy).
  static const uint8_t* payload(const uint8_t* sectorBuf, int* outSize);

  // Reads n consecutive sectors starting at lba and concatenates their
  // Form1 payload (2048B/sector).
  std::vector<uint8_t> readForm1Range(int64_t lba, int64_t count);

  // Reads n consecutive sectors starting at lba and concatenates their
  // Form2 payload (2324B/sector, used by the MPEG-PS streams under
  // MPEGAV/SEGMENT). A single read over the contiguous raw block, then
  // de-interleaves the payloads.
  std::vector<uint8_t> readForm2Range(int64_t lba, int64_t count);

 private:
  CdxaReader(FILE* fp, int64_t sectorCount) : fp_(fp), sectorCount_(sectorCount) {}

  // Throws unless [lba, lba + count) lies entirely inside the .bin file.
  void checkRange(int64_t lba, int64_t count) const;

  FILE* fp_ = nullptr;
  int64_t sectorCount_ = 0;
};

}  // namespace vcd
