#pragma once

#include <mpv/client.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../core/Cdxa.h"

namespace vcd {

// Registers a custom URI protocol ("vcdxa://<id>") on an mpv_handle that
// demuxes a .bin file's Form2 sectors on the fly, in place of a local HTTP
// server: mpv reads the MPEG-PS payload bytes of a track/segment item
// directly, via mpv_stream_cb_add_ro.
//
// A single instance lives for the whole app lifetime (installOn() must be
// called only once): each registered stream carries its own .bin file, so
// switching discs doesn't require reinstalling the protocol on mpv
// (mpv_stream_cb_add_ro doesn't allow a second registration of the same
// prefix on the same handle).
class VcdStreamRegistry {
 public:
  VcdStreamRegistry() = default;

  // Registers a stream (disc image + ISO LBA + Form2 sector count) and
  // returns the URI to pass to `loadfile` ("vcdxa://<id>").
  std::string registerStream(DiscImage image, int64_t lba, int64_t sectorCount);

  // Registers the "vcdxa" protocol on this mpv_handle. Must be called only
  // once, before any `loadfile` that uses a "vcdxa://" URI.
  void installOn(mpv_handle* mpv);

  struct StreamDef {
    DiscImage image;
    int64_t lba = 0;
    int64_t sectorCount = 0;
  };

  // Thread-safe: called from mpv's demuxer thread (OpenCb) while the UI
  // thread may be registering the next stream. Returns a copy, never a
  // pointer into streams_, which a concurrent registerStream() can
  // reallocate.
  std::optional<StreamDef> find(int id) const;

 private:
  mutable std::mutex mutex_;
  std::vector<StreamDef> streams_;
};

}  // namespace vcd
