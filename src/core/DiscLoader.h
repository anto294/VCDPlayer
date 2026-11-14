#pragma once

#include <map>
#include <string>

#include "Cue.h"
#include "PbcEngine.h"
#include "VcdMeta.h"

namespace vcd {

struct LoadedDisc {
  Disc disc;
  DiscImage image;  // where each ISO sector lives in the .bin file(s)
  std::string volumeId;
  VcdInfo info;
  EntriesFile entriesFile;
  std::vector<uint8_t> lot;                 // empty if PBC disabled or LOT.VCD absent
  std::vector<uint8_t> psd;                 // empty if PBC disabled or PSD.VCD absent
  std::map<int, StreamRef> trackStreams;    // CD track number -> Form2 stream
  std::map<int, StreamRef> segmentStreams;  // segment item number -> Form2 stream
};

// Opens a VCD disc from its .cue: parses the ISO9660 filesystem and PBC
// metadata, resolves the ISO LBA of every video track
// (MPEGAV/AVSEQnn.DAT) and segment item (SEGMENT/ITEMnnnn.DAT), plus the
// image layout mapping those onto the .bin (see imageLayout()). Throws
// std::runtime_error on invalid input/not a VCD.
LoadedDisc loadDisc(const std::string& cuePath);

}  // namespace vcd
