#pragma once

#include <stdexcept>
#include <string>

namespace vcd {

// Error raised while opening/reading a disc image. The core has no Qt (and
// so no tr()), so it reports *what kind* of failure happened as a code the
// UI translates, plus an untranslated technical detail (a path, a track, the
// exact structure that was corrupt) shown underneath. what() is the full
// English message, for logs and non-UI callers.
class DiscError : public std::runtime_error {
 public:
  enum class Code {
    CueUnreadable,      // the .cue file can't be opened
    CueInvalid,         // the .cue file is malformed
    BinUnreadable,      // an image (.bin) file is missing or can't be read
    UnsupportedFormat,  // a valid image in a format we don't play
    NotVideoCd,         // readable, but not a Video CD
    CorruptImage,       // a Video CD whose data is truncated or corrupt
  };

  DiscError(Code code, const std::string& detail) : std::runtime_error(detail), code_(code), detail_(detail) {}

  Code code() const { return code_; }
  const std::string& detail() const { return detail_; }

 private:
  Code code_;
  std::string detail_;
};

}  // namespace vcd
