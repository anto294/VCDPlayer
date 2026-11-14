#include <exception>
#include <iostream>

#include "TestSupport.h"

int main() {
  int failed = 0;
  for (const test::Case& c : test::registry()) {
    try {
      c.fn();
      std::cout << "PASS " << c.name << "\n";
    } catch (const test::Failure& f) {
      failed++;
      std::cout << "FAIL " << c.name << "\n  " << f.message << "\n";
    } catch (const std::exception& e) {
      failed++;
      std::cout << "FAIL " << c.name << "\n  unexpected exception: " << e.what() << "\n";
    }
  }
  std::cout << (test::registry().size() - failed) << "/" << test::registry().size() << " passed\n";
  return failed == 0 ? 0 : 1;
}
