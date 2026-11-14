#include "VcdStreamProtocol.h"

#include <mpv/stream_cb.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "../core/Cdxa.h"

namespace vcd {

namespace {

// State of a single stream opened by mpv (a dedicated CdxaReader: each
// stream has its own file handle, so reads from different mpv threads
// don't contend over a shared descriptor).
struct StreamCookie {
  CdxaReader reader;
  int64_t lba = 0;
  int64_t totalBytes = 0;
  int64_t pos = 0;
};

int64_t ReadCb(void* cookieRaw, char* buf, uint64_t nbytes) {
  auto* c = static_cast<StreamCookie*>(cookieRaw);
  if (c->pos >= c->totalBytes) return 0;
  const uint64_t remaining = std::min<uint64_t>(nbytes, static_cast<uint64_t>(c->totalBytes - c->pos));
  if (remaining == 0) return 0;
  uint64_t written = 0;
  try {
    // All the sectors covering [pos, pos + remaining) in one readForm2Range
    // call (one seek + one fread per contiguous run in the image), instead
    // of a seek and two allocations for every 2324-byte sector: mpv asks
    // for tens of KB at a time.
    const int64_t firstSector = c->pos / kForm2DataSize;
    const int64_t lastSector = (c->pos + static_cast<int64_t>(remaining) - 1) / kForm2DataSize;
    const std::vector<uint8_t> data = c->reader.readForm2Range(c->lba + firstSector, lastSector - firstSector + 1);
    std::memcpy(buf, data.data() + (c->pos - firstSector * kForm2DataSize), remaining);
    written = remaining;
  } catch (const std::exception&) {
    return -1;
  }
  c->pos += static_cast<int64_t>(written);
  return static_cast<int64_t>(written);
}

int64_t SeekCb(void* cookieRaw, int64_t offset) {
  auto* c = static_cast<StreamCookie*>(cookieRaw);
  if (offset < 0 || offset > c->totalBytes) return -1;
  c->pos = offset;
  return 0;
}

int64_t SizeCb(void* cookieRaw) {
  auto* c = static_cast<StreamCookie*>(cookieRaw);
  return c->totalBytes;
}

void CloseCb(void* cookieRaw) { delete static_cast<StreamCookie*>(cookieRaw); }

int OpenCb(void* userData, char* uri, mpv_stream_cb_info* info) {
  auto* registry = static_cast<VcdStreamRegistry*>(userData);
  // uri looks like "vcdxa://<id>"
  const std::string uriStr(uri);
  const std::string prefix = "vcdxa://";
  if (uriStr.rfind(prefix, 0) != 0) return MPV_ERROR_LOADING_FAILED;
  int id = 0;
  try {
    id = std::stoi(uriStr.substr(prefix.size()));
    // This callback crosses into mpv's C API, so a C++ exception can't be
    // let through; converting it to mpv's own error code is the handling.
    // NOLINTNEXTLINE(bugprone-empty-catch)
  } catch (const std::exception&) {
    return MPV_ERROR_LOADING_FAILED;
  }
  const std::optional<VcdStreamRegistry::StreamDef> def = registry->find(id);
  if (!def) return MPV_ERROR_LOADING_FAILED;

  auto* cookie = new StreamCookie();
  try {
    cookie->reader = CdxaReader::open(def->image);
  } catch (const std::exception&) {
    delete cookie;
    return MPV_ERROR_LOADING_FAILED;
  }
  cookie->lba = def->lba;
  cookie->totalBytes = def->sectorCount * kForm2DataSize;
  cookie->pos = 0;

  info->cookie = cookie;
  info->read_fn = ReadCb;
  info->seek_fn = SeekCb;
  info->size_fn = SizeCb;
  info->close_fn = CloseCb;
  info->cancel_fn = nullptr;
  return 0;
}

}  // namespace

std::string VcdStreamRegistry::registerStream(DiscImage image, int64_t lba, int64_t sectorCount) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const int id = static_cast<int>(streams_.size());
  streams_.push_back(StreamDef{std::move(image), lba, sectorCount});
  return "vcdxa://" + std::to_string(id);
}

void VcdStreamRegistry::installOn(mpv_handle* mpv) { mpv_stream_cb_add_ro(mpv, "vcdxa", this, OpenCb); }

std::optional<VcdStreamRegistry::StreamDef> VcdStreamRegistry::find(int id) const {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (id < 0 || static_cast<size_t>(id) >= streams_.size()) return std::nullopt;
  return streams_[id];
}

}  // namespace vcd
