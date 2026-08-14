#include "test_util.h"

namespace gridpulse_test {
namespace {

// Fixed-capacity registry: static initialisation order is unspecified across
// translation units, so a std::vector here would risk being constructed after the
// first Register() call. A plain array with a counter has no such problem.
constexpr std::size_t kMaxTests = 512;

struct TestEntry {
  const char* group;
  const char* name;
  TestFn fn;
};

TestEntry g_tests[kMaxTests];
std::size_t g_test_count = 0;

const char* g_current_group = "";
const char* g_current_name = "";
int g_current_failures = 0;

int g_assertions_passed = 0;
int g_assertions_failed = 0;
int g_tests_failed = 0;
bool g_registry_overflowed = false;

}  // namespace

bool Register(const char* group, const char* name, TestFn fn) {
  if (g_test_count >= kMaxTests) {
    g_registry_overflowed = true;
    return false;
  }
  g_tests[g_test_count++] = TestEntry{group, name, fn};
  return true;
}

void Pass() {
  ++g_assertions_passed;
}

void Fail(const char* file, int line, const char* message) {
  ++g_assertions_failed;
  ++g_current_failures;
  std::printf("  FAIL %s.%s\n    %s:%d\n    %s\n", g_current_group, g_current_name, file,
              line, message);
}

void ReportEq(const char* file, int line, const char* expr, long long actual,
              long long expected) {
  if (actual == expected) {
    Pass();
    return;
  }
  ++g_assertions_failed;
  ++g_current_failures;
  std::printf("  FAIL %s.%s\n    %s:%d\n    %s\n    expected %lld, got %lld\n",
              g_current_group, g_current_name, file, line, expr, expected, actual);
}

void ReportEqU(const char* file, int line, const char* expr, unsigned long long actual,
               unsigned long long expected) {
  if (actual == expected) {
    Pass();
    return;
  }
  ++g_assertions_failed;
  ++g_current_failures;
  std::printf(
      "  FAIL %s.%s\n    %s:%d\n    %s\n"
      "    expected %llu (0x%llX), got %llu (0x%llX)\n",
      g_current_group, g_current_name, file, line, expr, expected, expected, actual,
      actual);
}

void ReportEqDouble(const char* file, int line, const char* expr, double actual,
                    double expected) {
  if (actual == expected) {
    Pass();
    return;
  }
  ++g_assertions_failed;
  ++g_current_failures;
  std::printf("  FAIL %s.%s\n    %s:%d\n    %s\n    expected %.17g, got %.17g\n",
              g_current_group, g_current_name, file, line, expr, expected, actual);
}

void ReportEqStr(const char* file, int line, const char* expr, const char* actual,
                 const char* expected) {
  const bool equal = (actual == expected) || (actual != nullptr && expected != nullptr &&
                                              std::strcmp(actual, expected) == 0);
  if (equal) {
    Pass();
    return;
  }
  ++g_assertions_failed;
  ++g_current_failures;
  std::printf("  FAIL %s.%s\n    %s:%d\n    %s\n    expected \"%s\", got \"%s\"\n",
              g_current_group, g_current_name, file, line, expr,
              expected == nullptr ? "(null)" : expected,
              actual == nullptr ? "(null)" : actual);
}

int RunAll() {
  if (g_registry_overflowed) {
    std::printf("FATAL: more than %zu tests registered; raise kMaxTests\n", kMaxTests);
    return 1;
  }

  for (std::size_t i = 0; i < g_test_count; ++i) {
    g_current_group = g_tests[i].group;
    g_current_name = g_tests[i].name;
    g_current_failures = 0;
    g_tests[i].fn();
    if (g_current_failures > 0) {
      ++g_tests_failed;
    }
  }

  std::printf(
      "GRIDPULSE_NATIVE_TESTS: %s (%zu tests, %d assertions passed, "
      "%d failed)\n",
      g_assertions_failed == 0 ? "PASS" : "FAIL", g_test_count, g_assertions_passed,
      g_assertions_failed);
  return g_assertions_failed;
}

}  // namespace gridpulse_test

int main() {
  return gridpulse_test::RunAll() == 0 ? 0 : 1;
}
