// Robustness of the metadata parsers against truncated/corrupt input. Each
// of these read out of bounds (confirmed with AddressSanitizer) before the
// parsers were bounds-checked; run the suite under ASan to keep it that way.

#include "Cdxa.h"
#include "Iso9660.h"
#include "TestSupport.h"
#include "VcdMeta.h"

using vcd::DiscError;

TEST(info_vcd_shorter_than_header) {
  CHECK_DISC_ERROR(vcd::parseInfoVcd(std::vector<uint8_t>(8, 0)), DiscError::Code::CorruptImage);
}

TEST(entries_vcd_count_larger_than_file) {
  std::vector<uint8_t> buf(16, 0);
  test::putBe16(buf.data() + 0x0a, 500);
  CHECK_DISC_ERROR(vcd::parseEntriesVcd(buf), DiscError::Code::CorruptImage);
}

TEST(psd_offset_past_end) {
  CHECK_DISC_ERROR(vcd::readPsdDescriptor(std::vector<uint8_t>(16, 0), 4000), DiscError::Code::CorruptImage);
}

TEST(psd_descriptor_truncated) {
  std::vector<uint8_t> psd(0x14, 0);
  psd[0] = 0x18;  // selection claiming 50 choices in a 20-byte buffer
  psd[2] = 50;
  CHECK_DISC_ERROR(vcd::readPsdDescriptor(psd, 0), DiscError::Code::CorruptImage);
}

TEST(lot_lookup_out_of_range_is_disabled) {
  CHECK(!vcd::lotPsdByteOffset(std::vector<uint8_t>(4, 0), 1000).has_value());
}

TEST(directory_record_running_off_the_sector) {
  test::TempDir dir;
  test::Image img(20);
  uint8_t* records = img.form1Payload(17);
  size_t off = 0;
  while (off + 255 <= 2040) {
    records[off] = 255;
    off += 255;
  }
  records[off] = 40;  // starts 8 bytes before the end of the sector
  img.save(dir.file("d.bin"));
  vcd::CdxaReader reader = vcd::CdxaReader::open(dir.file("d.bin"));
  CHECK_DISC_ERROR(vcd::listDirectory(reader, 17, 2048), DiscError::Code::CorruptImage);
}

TEST(huge_directory_length) {
  test::TempDir dir;
  test::Image(20).save(dir.file("d.bin"));
  vcd::CdxaReader reader = vcd::CdxaReader::open(dir.file("d.bin"));
  CHECK_DISC_ERROR(vcd::listDirectory(reader, 17, 0xFFFFFFFFu), DiscError::Code::CorruptImage);
}

TEST(read_past_end_of_image) {
  test::TempDir dir;
  test::Image(20).save(dir.file("d.bin"));
  vcd::CdxaReader reader = vcd::CdxaReader::open(dir.file("d.bin"));
  CHECK_DISC_ERROR(reader.readForm2Range(10, 100000), DiscError::Code::CorruptImage);
  CHECK_EQ(reader.readForm2Range(10, 10).size(), size_t{10 * vcd::kForm2DataSize});
}
