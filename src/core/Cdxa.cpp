#include "Cdxa.h"

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

}  // namespace

int64_t ftell64(FILE* fp) {
#if defined(_WIN32)
  return _ftelli64(fp);
#else
  return static_cast<int64_t>(ftello(fp));
#endif
}

CdxaReader CdxaReader::open(const std::string& binPath) {
  FILE* fp = std::fopen(binPath.c_str(), "rb");
  if (!fp) throw std::runtime_error("Unable to open .bin file: " + binPath);
  if (fseek64(fp, 0, SEEK_END) != 0) {
    std::fclose(fp);
    throw std::runtime_error("Unable to read the size of the .bin file: " + binPath);
  }
  const int64_t bytes = ftell64(fp);
  std::rewind(fp);
  return CdxaReader(fp, bytes > 0 ? bytes / kSectorSize : 0);
}

CdxaReader::CdxaReader(CdxaReader&& other) noexcept : fp_(other.fp_), sectorCount_(other.sectorCount_) {
  other.fp_ = nullptr;
  other.sectorCount_ = 0;
}

CdxaReader& CdxaReader::operator=(CdxaReader&& other) noexcept {
  if (this != &other) {
    close();
    fp_ = other.fp_;
    sectorCount_ = other.sectorCount_;
    other.fp_ = nullptr;
    other.sectorCount_ = 0;
  }
  return *this;
}

CdxaReader::~CdxaReader() { close(); }

void CdxaReader::close() {
  if (fp_) {
    std::fclose(fp_);
    fp_ = nullptr;
  }
}

void CdxaReader::checkRange(int64_t lba, int64_t count) const {
  // lba/count are derived from LBAs and lengths stored on the disc itself,
  // so they're checked against the real file size before anything gets
  // allocated for them: a corrupt length can't turn into a multi-GB
  // allocation or a read past the end of the image.
  if (lba < 0 || count < 0 || lba > sectorCount_ || count > sectorCount_ - lba) {
    throw std::runtime_error("Read beyond the end of the .bin file (corrupt or truncated image).");
  }
}

void CdxaReader::readSectorRaw(int64_t lba, uint8_t* out) {
  checkRange(lba, 1);
  if (fseek64(fp_, lba * kSectorSize, SEEK_SET) != 0) {
    throw std::runtime_error("Seek failed in .bin file.");
  }
  const size_t n = std::fread(out, 1, kSectorSize, fp_);
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
  if (fseek64(fp_, lba * kSectorSize, SEEK_SET) != 0) {
    throw std::runtime_error("Seek failed in .bin file.");
  }
  const size_t n = std::fread(raw.data(), 1, raw.size(), fp_);
  if (n != raw.size()) throw std::runtime_error("Incomplete read in .bin file (readForm2Range).");

  std::vector<uint8_t> out(static_cast<size_t>(count) * kForm2DataSize);
  for (int64_t i = 0; i < count; i++) {
    std::memcpy(out.data() + i * kForm2DataSize, raw.data() + i * kSectorSize + kForm1DataOffset, kForm2DataSize);
  }
  return out;
}

}  // namespace vcd
