// .cue parsing, image layout and full disc loading.

#include "Cdxa.h"
#include "Cue.h"
#include "DiscBuilder.h"
#include "DiscLoader.h"
#include "TestSupport.h"

using test::SyntheticVcd;
using vcd::DiscError;

namespace {

int64_t readHeaderIso(const vcd::DiscImage& image, int64_t isoLba) {
  vcd::CdxaReader reader = vcd::CdxaReader::open(image);
  std::vector<uint8_t> raw(vcd::kSectorSize);
  reader.readSectorRaw(isoLba, raw.data());
  return test::headerIsoLba(raw.data());
}

void checkSyntheticDisc(const vcd::LoadedDisc& disc) {
  CHECK_EQ(disc.volumeId, std::string("SYNTHETIC"));
  CHECK(!disc.info.pbcEnabled);
  CHECK_EQ(disc.trackStreams.count(2), size_t{1});
  const vcd::StreamRef video = disc.trackStreams.at(2);
  CHECK_EQ(video.lba, int64_t{SyntheticVcd::kVideoIsoLba});
  CHECK_EQ(video.sectorCount, int64_t{SyntheticVcd::kVideoSectors});
  // The sectors actually read are the right ones (PREGAP accounted for).
  CHECK_EQ(readHeaderIso(disc.image, video.lba), video.lba);
  CHECK_EQ(readHeaderIso(disc.image, video.lba + video.sectorCount - 1), video.lba + video.sectorCount - 1);
  CHECK_EQ(disc.entriesFile.entries.size(), size_t{2});
  CHECK_EQ(disc.entriesFile.entries[1].isoLba, int64_t{SyntheticVcd::kVideoIsoLba + SyntheticVcd::kChapterOffset});
  CHECK_EQ(readHeaderIso(disc.image, disc.entriesFile.entries[1].isoLba), disc.entriesFile.entries[1].isoLba);
}

}  // namespace

TEST(loads_single_bin_with_pregap) {
  test::TempDir dir;
  checkSyntheticDisc(vcd::loadDisc(SyntheticVcd().saveSingle(dir)));
}

TEST(loads_multi_file_cue) {
  test::TempDir dir;
  const vcd::LoadedDisc disc = vcd::loadDisc(SyntheticVcd().saveMultiFile(dir));
  CHECK_EQ(disc.disc.files.size(), size_t{2});
  checkSyntheticDisc(disc);
}

TEST(pregap_shifts_iso_lbas_after_it) {
  test::TempDir dir;
  const vcd::Disc disc = vcd::parseCue(SyntheticVcd().saveSingle(dir));
  const vcd::DiscImage image = vcd::imageLayout(disc);
  CHECK_EQ(image.regions.size(), size_t{2});
  CHECK_EQ(image.regions[1].fileLba, int64_t{SyntheticVcd::kTrack1Sectors});
  CHECK_EQ(image.regions[1].isoLba, int64_t{SyntheticVcd::kVideoIsoLba});
  // The gap itself is on the disc but not in the file: reading it fails.
  CHECK_DISC_ERROR(readHeaderIso(image, SyntheticVcd::kTrack1Sectors + 10), DiscError::Code::CorruptImage);
}

TEST(cue_with_utf8_bom) {
  test::TempDir dir;
  test::Image(20).save(dir.file("disc.bin"));
  test::writeText(dir.file("disc.cue"),
                  "\xEF\xBB\xBF"
                  "FILE \"disc.bin\" BINARY\r\n TRACK 01 MODE2/2352\r\n INDEX 01 00:00:00\r\n");
  const vcd::Disc disc = vcd::parseCue(dir.file("disc.cue"));
  CHECK_EQ(disc.tracks.size(), size_t{1});
}

TEST(cue_bin_name_case_and_windows_path) {
  test::TempDir dir;
  test::Image(20).save(dir.file("disc.bin"));
  test::writeText(dir.file("a.cue"), "FILE \"DISC.BIN\" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n");
  CHECK_EQ(vcd::parseCue(dir.file("a.cue")).files[0], dir.file("disc.bin"));
  test::writeText(dir.file("b.cue"), "FILE \"C:\\rips\\Disc.Bin\" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n");
  CHECK_EQ(vcd::parseCue(dir.file("b.cue")).files[0], dir.file("disc.bin"));
}

TEST(cue_errors) {
  test::TempDir dir;
  CHECK_DISC_ERROR(vcd::parseCue(dir.file("missing.cue")), DiscError::Code::CueUnreadable);
  test::writeText(dir.file("noindex.cue"), "FILE \"x.bin\" BINARY\n TRACK 01 MODE2/2352\n");
  CHECK_DISC_ERROR(vcd::parseCue(dir.file("noindex.cue")), DiscError::Code::CueInvalid);
  test::writeText(dir.file("nobin.cue"), "FILE \"x.bin\" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n");
  CHECK_DISC_ERROR(vcd::loadDisc(dir.file("nobin.cue")), DiscError::Code::BinUnreadable);
}

TEST(rejects_non_raw_sector_images) {
  test::TempDir dir;
  test::Image(20).save(dir.file("disc.bin"));
  test::writeText(dir.file("disc.cue"), "FILE \"disc.bin\" BINARY\n TRACK 01 MODE2/2336\n INDEX 01 00:00:00\n");
  CHECK_DISC_ERROR(vcd::loadDisc(dir.file("disc.cue")), DiscError::Code::UnsupportedFormat);
}

TEST(not_a_video_cd) {
  test::TempDir dir;
  test::Image(40).save(dir.file("disc.bin"));  // no PVD at all
  test::writeText(dir.file("disc.cue"), "FILE \"disc.bin\" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n");
  CHECK_DISC_ERROR(vcd::loadDisc(dir.file("disc.cue")), DiscError::Code::NotVideoCd);
}
