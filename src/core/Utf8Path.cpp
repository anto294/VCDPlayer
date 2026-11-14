#include "Utf8Path.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace vcd {

#ifdef _WIN32

std::filesystem::path pathFromUtf8(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int size = static_cast<int>(utf8.size());
  const int wideSize = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, nullptr, 0);
  std::wstring wide(static_cast<size_t>(wideSize), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, wide.data(), wideSize);
  return std::filesystem::path(wide);
}

std::string pathToUtf8(const std::filesystem::path& path) {
  const std::wstring& wide = path.native();
  if (wide.empty()) return {};
  const int size = static_cast<int>(wide.size());
  const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
  std::string utf8(static_cast<size_t>(utf8Size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, utf8.data(), utf8Size, nullptr, nullptr);
  return utf8;
}

#else

// POSIX paths are byte strings; the desktop's are UTF-8.
std::filesystem::path pathFromUtf8(const std::string& utf8) { return std::filesystem::path(utf8); }
std::string pathToUtf8(const std::filesystem::path& path) { return path.native(); }

#endif

}  // namespace vcd
