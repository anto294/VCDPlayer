#pragma once

#include <filesystem>
#include <string>

namespace vcd {

// Paths cross vcdcore's API as UTF-8 std::strings on every platform (the
// app passes QString::toStdString()). They must not reach the file APIs as
// narrow strings: on Windows those take the ANSI code page, so any disc in
// a folder named outside it ("Città", "perché", ...) failed to open.
// These convert to and from std::filesystem::path, which holds the native
// form (UTF-16 on Windows), with the OS's own conversion functions rather
// than u8path/u8string, whose types and behaviour vary across C++
// versions and standard libraries.
std::filesystem::path pathFromUtf8(const std::string& utf8);
std::string pathToUtf8(const std::filesystem::path& path);

}  // namespace vcd
