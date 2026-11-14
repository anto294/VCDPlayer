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

// One contiguous run of the disc's ISO sectors, stored contiguously in one
// image file (see imageLayout() in Cue.h for how these are derived).
struct ImageRegion {
  size_t fileIndex = 0;  // index into DiscImage::files
  int64_t fileLba = 0;   // first sector's position within that file
  int64_t isoLba = 0;    // first sector's ISO LBA
  int64_t count = 0;     // number of sectors
};

// The image file(s) of a disc plus where each of its ISO sectors lives.
struct DiscImage {
  std::vector<std::string> files;
  std::vector<ImageRegion> regions;  // sorted by isoLba, non-overlapping
};

// Reads/interprets raw CD-XA sectors (2352 bytes) of a disc image, addressed
// by ISO LBA.
class CdxaReader {
 public:
  // Opens every file of the image. Throws std::runtime_error if one can't
  // be opened.
  static CdxaReader open(const DiscImage& image);

  // Single .bin whose sector N is ISO LBA N (no pregaps, no extra files).
  static CdxaReader open(const std::string& binPath);

  CdxaReader() = default;
  CdxaReader(CdxaReader&& other) noexcept = default;
  CdxaReader& operator=(CdxaReader&& other) noexcept;
  CdxaReader(const CdxaReader&) = delete;
  CdxaReader& operator=(const CdxaReader&) = delete;
  ~CdxaReader();

  void close();

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
  // MPEGAV/SEGMENT). One read per contiguous stored run, then
  // de-interleaves the payloads.
  std::vector<uint8_t> readForm2Range(int64_t lba, int64_t count);

 private:
  // The region holding `lba`. Throws if no image file stores that sector.
  const ImageRegion& regionFor(int64_t lba) const;

  // Throws unless every sector of [lba, lba + count) is stored in the image.
  void checkRange(int64_t lba, int64_t count) const;

  std::vector<FILE*> files_;
  std::vector<ImageRegion> regions_;
};

}  // namespace vcd
