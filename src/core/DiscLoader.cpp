#include "DiscLoader.h"

#include <cstdio>
#include <regex>
#include <stdexcept>

#include "Cdxa.h"
#include "Iso9660.h"

namespace vcd {

namespace {

std::vector<uint8_t> readWholeFile(CdxaReader& reader, const DirEntry& entry) {
  // Only used for the small VCD/*.VCD metadata files (LOT.VCD is 64 KB,
  // PSD.VCD at most 256 KB by spec): cap well above that so a corrupt
  // length can't make us read most of the disc into memory.
  constexpr int64_t kMaxMetadataFileBytes = 4 * 1024 * 1024;
  if (entry.length > kMaxMetadataFileBytes)
    throw std::runtime_error("VCD metadata file size is corrupt: " + entry.name);
  const int64_t sectorCount = (entry.length + kForm1DataSize - 1) / kForm1DataSize;
  std::vector<uint8_t> data = reader.readForm1Range(entry.lba, sectorCount);
  data.resize(static_cast<size_t>(entry.length));
  return data;
}

std::string trackFileName(int trackNumber) {
  // CD track N (N>=2) -> MPEGAV/AVSEQ(N-1).DAT
  char buf[32];
  std::snprintf(buf, sizeof(buf), "MPEGAV/AVSEQ%02d.DAT", trackNumber - 1);
  return buf;
}

}  // namespace

LoadedDisc loadDisc(const std::string& cuePath) {
  LoadedDisc out;
  out.disc = parseCue(cuePath);
  // Every reader below assumes raw 2352-byte sectors (sync + header +
  // subheader + payload). A MODE2/2336 or MODE1/2048 image would still
  // "load" and then stream garbage, so refuse it up front instead.
  for (const Track& t : out.disc.tracks) {
    if (t.sectorSize != kSectorSize) {
      throw std::runtime_error("Track " + std::to_string(t.number) + " uses " + t.mode +
                               ": only raw 2352-byte sector images (MODE2/2352) are supported.");
    }
  }
  out.image = imageLayout(out.disc);

  CdxaReader fsReader = CdxaReader::open(out.image);
  const PvdInfo pvd = readPvd(fsReader);
  out.volumeId = pvd.volumeId;

  const std::optional<DirEntry> infoEntry = resolvePath(fsReader, pvd.root, "VCD/INFO.VCD");
  if (!infoEntry) throw std::runtime_error("VCD/INFO.VCD not found: this doesn't look like a valid Video CD.");
  out.info = parseInfoVcd(readWholeFile(fsReader, *infoEntry));

  const std::optional<DirEntry> entriesEntry = resolvePath(fsReader, pvd.root, "VCD/ENTRIES.VCD");
  if (entriesEntry) {
    out.entriesFile = parseEntriesVcd(readWholeFile(fsReader, *entriesEntry));
  }

  if (out.info.pbcEnabled) {
    const std::optional<DirEntry> lotEntry = resolvePath(fsReader, pvd.root, "VCD/LOT.VCD");
    const std::optional<DirEntry> psdEntry = resolvePath(fsReader, pvd.root, "VCD/PSD.VCD");
    if (lotEntry) out.lot = readWholeFile(fsReader, *lotEntry);
    if (psdEntry) out.psd = readWholeFile(fsReader, *psdEntry);
  }

  for (const Track& t : out.disc.tracks) {
    if (t.number == 1) continue;
    const std::optional<DirEntry> entry = resolvePath(fsReader, pvd.root, trackFileName(t.number));
    if (!entry) continue;
    const int64_t sectorCount = (entry->length + kForm1DataSize - 1) / kForm1DataSize;
    out.trackStreams[t.number] = StreamRef{entry->lba, sectorCount};
  }

  const std::optional<DirEntry> segmentDir = resolvePath(fsReader, pvd.root, "SEGMENT");
  if (segmentDir) {
    static const std::regex kItemRe(R"(^ITEM(\d+)\.DAT$)", std::regex::icase);
    for (const DirEntry& it : listDirectory(fsReader, segmentDir->lba, segmentDir->length)) {
      std::smatch m;
      if (!std::regex_match(it.name, m, kItemRe)) continue;
      const int segNum = std::stoi(m[1]);
      const int64_t sectorCount = (it.length + kForm1DataSize - 1) / kForm1DataSize;
      out.segmentStreams[segNum] = StreamRef{it.lba, sectorCount};
    }
  }

  fsReader.close();
  return out;
}

}  // namespace vcd
