#pragma once

// Minimal self-contained test harness (no external framework, so the tests
// build anywhere vcdcore does) plus helpers to synthesize disc images.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "DiscError.h"

namespace test {

struct Case {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure {
  std::string message;
};

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)
#define TEST(name)                                                           \
  static void name();                                                        \
  static const ::test::Registrar TEST_CONCAT(registrar_, name)(#name, name); \
  static void name()

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::ostringstream os_;                                            \
      os_ << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed"; \
      throw ::test::Failure{os_.str()};                                  \
    }                                                                    \
  } while (0)

#define CHECK_EQ(a, b)                                                                                     \
  do {                                                                                                     \
    const auto va_ = (a);                                                                                  \
    const auto vb_ = (b);                                                                                  \
    if (!(va_ == vb_)) {                                                                                   \
      std::ostringstream os_;                                                                              \
      os_ << __FILE__ << ":" << __LINE__ << ": CHECK_EQ(" #a ", " #b ") failed: " << va_ << " != " << vb_; \
      throw ::test::Failure{os_.str()};                                                                    \
    }                                                                                                      \
  } while (0)

// Expects `expr` to throw vcd::DiscError with the given code.
#define CHECK_DISC_ERROR(expr, expectedCode)                                                    \
  do {                                                                                          \
    bool threw_ = false;                                                                        \
    try {                                                                                       \
      (void)(expr);                                                                             \
    } catch (const vcd::DiscError& e_) {                                                        \
      threw_ = true;                                                                            \
      if (e_.code() != (expectedCode)) {                                                        \
        throw ::test::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) +          \
                              ": " #expr " threw DiscError with the wrong code: " + e_.what()}; \
      }                                                                                         \
    }                                                                                           \
    if (!threw_) {                                                                              \
      throw ::test::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) +            \
                            ": " #expr " did not throw DiscError"};                             \
    }                                                                                           \
  } while (0)

// A fresh, empty directory under the system temp dir, removed on scope exit.
class TempDir {
 public:
  TempDir() {
    static int counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("vcdcore-test-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" + std::to_string(counter++));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  std::string file(const std::string& name) const { return (path_ / name).string(); }

 private:
  std::filesystem::path path_;
};

inline void writeText(const std::string& path, const std::string& text) {
  std::ofstream(path, std::ios::binary) << text;
}

inline void putLe32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

inline void putBe16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

inline void putBe32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>(v >> (24 - 8 * i));
}

inline uint8_t toBcd(int v) { return static_cast<uint8_t>((v / 10) << 4 | (v % 10)); }

// Raw 2352-byte-sector image built in memory. Every sector gets a valid
// header carrying the absolute MSF of the ISO LBA it is meant to hold, so
// tests can check which sector a read really returned.
class Image {
 public:
  explicit Image(int sectors) : data_(static_cast<size_t>(sectors) * 2352, 0) {}

  int sectors() const { return static_cast<int>(data_.size() / 2352); }
  uint8_t* sector(int fileLba) { return data_.data() + static_cast<size_t>(fileLba) * 2352; }
  uint8_t* form1Payload(int fileLba) { return sector(fileLba) + 24; }

  // Stamps the header of file sector `fileLba` as ISO LBA `isoLba`.
  void stamp(int fileLba, int64_t isoLba) {
    const int64_t abs = isoLba + 150;
    uint8_t* s = sector(fileLba);
    s[12] = toBcd(static_cast<int>(abs / 75 / 60));
    s[13] = toBcd(static_cast<int>(abs / 75 % 60));
    s[14] = toBcd(static_cast<int>(abs % 75));
    s[15] = 2;
  }

  void save(const std::string& path, int fromLba = 0, int count = -1) const {
    if (count < 0) count = sectors() - fromLba;
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(data_.data()) + static_cast<size_t>(fromLba) * 2352,
               static_cast<std::streamsize>(count) * 2352);
  }

 private:
  std::vector<uint8_t> data_;
};

// ISO LBA stored in the header of a raw sector (inverse of Image::stamp).
inline int64_t headerIsoLba(const uint8_t* raw) {
  auto bcd = [](uint8_t b) { return (b >> 4) * 10 + (b & 0x0f); };
  return (static_cast<int64_t>(bcd(raw[12])) * 60 + bcd(raw[13])) * 75 + bcd(raw[14]) - 150;
}

}  // namespace test
