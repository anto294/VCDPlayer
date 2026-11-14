#include "DiscLoader.h"

#include <cstdio>
#include <regex>
#include <stdexcept>

#include "Cdxa.h"
#include "Iso9660.h"

namespace vcd {

namespace {

std::vector<uint8_t> readWholeFile(CdxaReader& reader, int64_t trackStartLba, const DirEntry& entry) {
  // Only used for the small VCD/*.VCD metadata files (LOT.VCD is 64 KB,
  // PSD.VCD at most 256 KB by spec): cap well above that so a corrupt
  // length can't make us read most of the disc into memory.
  constexpr int64_t kMaxMetadataFileBytes = 4 * 1024 * 1024;
  if (entry.length > kMaxMetadataFileBytes)
    throw std::runtime_error("VCD metadata file size is corrupt: " + entry.name);
  const int64_t sectorCount = (entry.length + kForm1DataSize - 1) / kForm1DataSize;
  std::vector<uint8_t> data = reader.readForm1Range(trackStartLba + entry.lba, sectorCount);
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
  const int64_t trackStartLba = dataTrackStartLba(out.disc);

  CdxaReader fsReader = CdxaReader::open(out.disc.binPath);
  const PvdInfo pvd = readPvd(fsReader, trackStartLba);
  out.volumeId = pvd.volumeId;

  const std::optional<DirEntry> infoEntry = resolvePath(fsReader, trackStartLba, pvd.root, "VCD/INFO.VCD");
  if (!infoEntry) throw std::runtime_error("VCD/INFO.VCD not found: this doesn't look like a valid Video CD.");
  out.info = parseInfoVcd(readWholeFile(fsReader, trackStartLba, *infoEntry));

  const std::optional<DirEntry> entriesEntry = resolvePath(fsReader, trackStartLba, pvd.root, "VCD/ENTRIES.VCD");
  if (entriesEntry) {
    out.entriesFile = parseEntriesVcd(readWholeFile(fsReader, trackStartLba, *entriesEntry));
  }

  if (out.info.pbcEnabled) {
    const std::optional<DirEntry> lotEntry = resolvePath(fsReader, trackStartLba, pvd.root, "VCD/LOT.VCD");
    const std::optional<DirEntry> psdEntry = resolvePath(fsReader, trackStartLba, pvd.root, "VCD/PSD.VCD");
    if (lotEntry) out.lot = readWholeFile(fsReader, trackStartLba, *lotEntry);
    if (psdEntry) out.psd = readWholeFile(fsReader, trackStartLba, *psdEntry);
  }

  for (const Track& t : out.disc.tracks) {
    if (t.number == 1) continue;
    const std::optional<DirEntry> entry = resolvePath(fsReader, trackStartLba, pvd.root, trackFileName(t.number));
    if (!entry) continue;
    const int64_t sectorCount = (entry->length + kForm1DataSize - 1) / kForm1DataSize;
    const int64_t absLba = trackStartLba + entry->lba;
    out.trackStreams[t.number] = StreamRef{absLba, sectorCount};
  }

  const std::optional<DirEntry> segmentDir = resolvePath(fsReader, trackStartLba, pvd.root, "SEGMENT");
  if (segmentDir) {
    static const std::regex kItemRe(R"(^ITEM(\d+)\.DAT$)", std::regex::icase);
    for (const DirEntry& it : listDirectory(fsReader, trackStartLba, segmentDir->lba, segmentDir->length)) {
      std::smatch m;
      if (!std::regex_match(it.name, m, kItemRe)) continue;
      const int segNum = std::stoi(m[1]);
      const int64_t sectorCount = (it.length + kForm1DataSize - 1) / kForm1DataSize;
      const int64_t absLba = trackStartLba + it.lba;
      out.segmentStreams[segNum] = StreamRef{absLba, sectorCount};
    }
  }

  fsReader.close();
  return out;
}

}  // namespace vcd
