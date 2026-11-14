#pragma once

#include <filesystem>
#include <string>

namespace vcd {

// Paths cross vcdcore's API as UTF-8 std::strings on every platform (the
// app passes QString::toStdString()). They must not reach the file APIs as
// narrow strings: on Windows those take the ANSI code page, so any disc in
// a folder named outside it ("Città", "perché", ...) failed to open.
// These convert to and from std::filesystem::path, which holds the native
// (UTF-16 on Windows) form.
inline std::filesystem::path pathFromUtf8(const std::string& utf8) {
#if defined(__cpp_char8_t)
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
#else
  return std::filesystem::u8path(utf8);
#endif
}

inline std::string pathToUtf8(const std::filesystem::path& path) {
  const auto u8 = path.u8string();  // std::string in C++17, std::u8string in C++20
  return std::string(u8.begin(), u8.end());
}

}  // namespace vcd
