#pragma once

// Builds a tiny but structurally complete Video CD image: ISO9660 PVD and
// directories, VCD/INFO.VCD + ENTRIES.VCD, and one MPEGAV/AVSEQ01.DAT video
// track behind a 2-second PREGAP that is NOT stored in the .bin, exactly
// like the common "TRACK 02 / PREGAP 00:02:00" rips.

#include <string>

#include "TestSupport.h"

namespace test {

struct SyntheticVcd {
  // Layout (file sector -> ISO LBA): data track at file 0..kTrack1Sectors-1
  // = ISO 0.., then the video track at file kTrack1Sectors.. = ISO
  // kTrack1Sectors + 150 (the PREGAP isn't in the file).
  static constexpr int kTrack1Sectors = 300;
  static constexpr int kPregap = 150;
  static constexpr int kVideoSectors = 10;
  static constexpr int kVideoIsoLba = kTrack1Sectors + kPregap;
  static constexpr int kChapterOffset = 4;  // second ENTRIES.VCD entry, sectors into the track

  Image image{kTrack1Sectors + kVideoSectors};

  SyntheticVcd() {
    for (int f = 0; f < image.sectors(); f++) {
      image.stamp(f, f < kTrack1Sectors ? f : f + kPregap);
      image.sector(f)[18] = f < kTrack1Sectors ? 0x08 : 0x62;  // data / form2 video
    }
    // PVD at ISO 16, root directory at ISO 18.
    uint8_t* pvd = image.form1Payload(16);
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    std::memcpy(pvd + 40, "SYNTHETIC                       ", 32);
    putLe32(pvd + 156 + 2, 18);
    putLe32(pvd + 156 + 10, 2048);

    // root: VCD/ (ISO 19), MPEGAV/ (ISO 20)
    uint8_t* root = image.form1Payload(18);
    size_t off = 0;
    off += dirRecord(root + off, std::string(1, '\0'), 18, 2048, true);  // "."
    off += dirRecord(root + off, std::string(1, '\1'), 18, 2048, true);  // ".."
    off += dirRecord(root + off, "VCD", 19, 2048, true);
    dirRecord(root + off, "MPEGAV", 20, 2048, true);

    // VCD/: INFO.VCD (ISO 21), ENTRIES.VCD (ISO 22)
    uint8_t* vcdDir = image.form1Payload(19);
    off = 0;
    off += dirRecord(vcdDir + off, "INFO.VCD;1", 21, 2048, false);
    dirRecord(vcdDir + off, "ENTRIES.VCD;1", 22, 2048, false);

    // MPEGAV/: AVSEQ01.DAT at the video track (ISO LBA, behind the PREGAP)
    dirRecord(image.form1Payload(20), "AVSEQ01.DAT;1", kVideoIsoLba, kVideoSectors * 2048, false);

    // INFO.VCD: "VIDEO_CD", no PBC (psd size 0).
    uint8_t* info = image.form1Payload(21);
    std::memcpy(info, "VIDEO_CD", 8);
    info[8] = 2;

    // ENTRIES.VCD: two entry points in track 2 (absolute MSF, BCD).
    uint8_t* entries = image.form1Payload(22);
    std::memcpy(entries, "ENTRYVCD", 8);
    putBe16(entries + 0x0a, 2);
    writeEntry(entries + 0x0c, 2, kVideoIsoLba);
    writeEntry(entries + 0x10, 2, kVideoIsoLba + kChapterOffset);
  }

  // Single .bin with a PREGAP'd track 2.
  std::string saveSingle(const TempDir& dir) const {
    image.save(dir.file("disc.bin"));
    writeText(dir.file("disc.cue"),
              "FILE \"disc.bin\" BINARY\n"
              "  TRACK 01 MODE2/2352\n"
              "    INDEX 01 00:00:00\n"
              "  TRACK 02 MODE2/2352\n"
              "    PREGAP 00:02:00\n"
              "    INDEX 01 00:04:00\n");  // file sector 300 = 4 s
    return dir.file("disc.cue");
  }

  // Same disc as one file per track.
  std::string saveMultiFile(const TempDir& dir) const {
    image.save(dir.file("track01.bin"), 0, kTrack1Sectors);
    image.save(dir.file("track02.bin"), kTrack1Sectors, kVideoSectors);
    writeText(dir.file("disc.cue"),
              "FILE \"track01.bin\" BINARY\n"
              "  TRACK 01 MODE2/2352\n"
              "    INDEX 01 00:00:00\n"
              "FILE \"track02.bin\" BINARY\n"
              "  TRACK 02 MODE2/2352\n"
              "    PREGAP 00:02:00\n"
              "    INDEX 01 00:00:00\n");
    return dir.file("disc.cue");
  }

  static size_t dirRecord(uint8_t* p, const std::string& name, uint32_t lba, uint32_t length, bool isDir) {
    const size_t len = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
    p[0] = static_cast<uint8_t>(len);
    putLe32(p + 2, lba);
    putLe32(p + 10, length);
    p[25] = isDir ? 0x02 : 0x00;
    p[32] = static_cast<uint8_t>(name.size());
    std::memcpy(p + 33, name.data(), name.size());
    return len;
  }

  static void writeEntry(uint8_t* p, int track, int64_t isoLba) {
    const int64_t abs = isoLba + 150;
    p[0] = toBcd(track);
    p[1] = toBcd(static_cast<int>(abs / 75 / 60));
    p[2] = toBcd(static_cast<int>(abs / 75 % 60));
    p[3] = toBcd(static_cast<int>(abs % 75));
  }
};

}  // namespace test
