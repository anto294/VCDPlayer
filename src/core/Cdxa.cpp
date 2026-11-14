#include "Cdxa.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace vcd {

namespace {

// 64-bit fseek/ftell: needed for .bin files that can exceed 2GB.
int fseek64(FILE* fp, int64_t offset, int origin) {
#if defined(_WIN32)
  return _fseeki64(fp, offset, origin);
#else
  return fseeko(fp, static_cast<off_t>(offset), origin);
#endif
}

int64_t ftell64(FILE* fp) {
#if defined(_WIN32)
  return _ftelli64(fp);
#else
  return static_cast<int64_t>(ftello(fp));
#endif
}

}  // namespace

CdxaReader CdxaReader::open(const DiscImage& image) {
  CdxaReader reader;
  for (const std::string& path : image.files) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) throw std::runtime_error("Unable to open .bin file: " + path);  // reader's dtor closes the others
    reader.files_.push_back(fp);
  }
  reader.regions_ = image.regions;
  return reader;
}

CdxaReader CdxaReader::open(const std::string& binPath) {
  CdxaReader reader = open(DiscImage{{binPath}, {}});
  FILE* fp = reader.files_.front();
  if (fseek64(fp, 0, SEEK_END) != 0) throw std::runtime_error("Unable to read the size of the .bin file: " + binPath);
  const int64_t bytes = ftell64(fp);
  std::rewind(fp);
  reader.regions_.push_back(ImageRegion{0, 0, 0, bytes > 0 ? bytes / kSectorSize : 0});
  return reader;
}

CdxaReader& CdxaReader::operator=(CdxaReader&& other) noexcept {
  if (this != &other) {
    close();
    files_ = std::move(other.files_);
    regions_ = std::move(other.regions_);
    other.files_.clear();
  }
  return *this;
}

CdxaReader::~CdxaReader() { close(); }

void CdxaReader::close() {
  for (FILE* fp : files_) std::fclose(fp);
  files_.clear();
}

const ImageRegion& CdxaReader::regionFor(int64_t lba) const {
  // Last region starting at or before lba.
  auto it = std::upper_bound(regions_.begin(), regions_.end(), lba,
                             [](int64_t value, const ImageRegion& r) { return value < r.isoLba; });
  if (it != regions_.begin()) {
    --it;
    if (lba < it->isoLba + it->count) return *it;
  }
  throw std::runtime_error("Read beyond the end of the .bin file (corrupt or truncated image).");
}

void CdxaReader::checkRange(int64_t lba, int64_t count) const {
  // lba/count are derived from LBAs and lengths stored on the disc itself,
  // so they're checked against what the image really stores before
  // anything gets allocated for them: a corrupt length can't turn into a
  // multi-GB allocation or a read past the end of the image.
  if (count < 0) throw std::runtime_error("Negative sector count (corrupt image).");
  int64_t cur = lba;
  while (cur < lba + count) {
    const ImageRegion& r = regionFor(cur);
    cur = r.isoLba + r.count;
  }
}

void CdxaReader::readSectorRaw(int64_t lba, uint8_t* out) {
  const ImageRegion& r = regionFor(lba);
  FILE* fp = files_.at(r.fileIndex);
  if (fseek64(fp, (r.fileLba + (lba - r.isoLba)) * kSectorSize, SEEK_SET) != 0) {
    throw std::runtime_error("Seek failed in .bin file.");
  }
  const size_t n = std::fread(out, 1, kSectorSize, fp);
  if (n != static_cast<size_t>(kSectorSize)) {
    throw std::runtime_error("Incomplete read of CD-XA sector.");
  }
}

Subheader CdxaReader::subheader(const uint8_t* sectorBuf) {
  const uint8_t submode = sectorBuf[18];
  Subheader sh;
  sh.file = sectorBuf[16];
  sh.channel = sectorBuf[17];
  sh.submode = submode;
  sh.coding = sectorBuf[19];
  sh.isForm2 = submode & kSubmodeForm2;
  sh.isVideo = submode & kSubmodeVideo;
  sh.isAudio = submode & kSubmodeAudio;
  sh.isData = submode & kSubmodeData;
  sh.isTrigger = submode & kSubmodeTrigger;
  sh.isRealtime = submode & kSubmodeRealtime;
  sh.isEndOfRecord = submode & kSubmodeEor;
  sh.isEndOfFile = submode & kSubmodeEof;
  return sh;
}

const uint8_t* CdxaReader::payload(const uint8_t* sectorBuf, int* outSize) {
  const Subheader sh = subheader(sectorBuf);
  *outSize = sh.isForm2 ? kForm2DataSize : kForm1DataSize;
  return sectorBuf + kForm1DataOffset;
}

std::vector<uint8_t> CdxaReader::readForm1Range(int64_t lba, int64_t count) {
  checkRange(lba, count);
  std::vector<uint8_t> out(static_cast<size_t>(count) * kForm1DataSize);
  std::vector<uint8_t> sector(kSectorSize);
  for (int64_t i = 0; i < count; i++) {
    readSectorRaw(lba + i, sector.data());
    std::memcpy(out.data() + i * kForm1DataSize, sector.data() + kForm1DataOffset, kForm1DataSize);
  }
  return out;
}

std::vector<uint8_t> CdxaReader::readForm2Range(int64_t lba, int64_t count) {
  checkRange(lba, count);
  if (count == 0) return {};
  std::vector<uint8_t> raw(static_cast<size_t>(count) * kSectorSize);
  int64_t done = 0;
  while (done < count) {
    // One fread per run of sectors stored contiguously in the same file.
    const ImageRegion& r = regionFor(lba + done);
    const int64_t n = std::min(count - done, r.isoLba + r.count - (lba + done));
    FILE* fp = files_.at(r.fileIndex);
    if (fseek64(fp, (r.fileLba + (lba + done - r.isoLba)) * kSectorSize, SEEK_SET) != 0) {
      throw std::runtime_error("Seek failed in .bin file.");
    }
    const size_t want = static_cast<size_t>(n) * kSectorSize;
    if (std::fread(raw.data() + done * kSectorSize, 1, want, fp) != want) {
      throw std::runtime_error("Incomplete read in .bin file (readForm2Range).");
    }
    done += n;
  }

  std::vector<uint8_t> out(static_cast<size_t>(count) * kForm2DataSize);
  for (int64_t i = 0; i < count; i++) {
    std::memcpy(out.data() + i * kForm2DataSize, raw.data() + i * kSectorSize + kForm1DataOffset, kForm2DataSize);
  }
  return out;
}

}  // namespace vcd
