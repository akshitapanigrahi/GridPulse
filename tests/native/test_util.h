// GRID PULSE - a minimal test framework for the native (host-compiled) suite.
//
// WHY NOT A REAL FRAMEWORK
//   The whole point of firmware/src/pure/ is that it has no dependencies. Pulling in
//   GoogleTest or Catch2 to test it would mean the test build needs a package
//   manager or a vendored third-party tree, which is exactly the "exotic setup" the
//   project is meant to avoid. Registration, assertions and reporting are about a
//   hundred lines; that is a better trade than a dependency.
//
// USAGE
//   TEST(Scoring, ClampsNetNegativeToZero) {
//     CHECK_EQ(BitRate(25, 10, 20, 60.0), 0.0);
//   }
//
// Failures record the file, line, expression and both values, then continue, so one
// run reports every failure rather than only the first.

#ifndef GRIDPULSE_TEST_UTIL_H_
#define GRIDPULSE_TEST_UTIL_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace gridpulse_test {

using TestFn = void (*)();

// Registers a test. Returns true so it can be used to initialise a static bool.
bool Register(const char* group, const char* name, TestFn fn);

// Records a failure against the currently running test.
void Fail(const char* file, int line, const char* message);

// Records a passing assertion, for the final count.
void Pass();

// Runs everything. Returns the number of failed assertions.
int RunAll();

// --- assertion plumbing ------------------------------------------------------

void ReportEq(const char* file, int line, const char* expr, long long actual,
              long long expected);
void ReportEqU(const char* file, int line, const char* expr, unsigned long long actual,
               unsigned long long expected);
void ReportEqDouble(const char* file, int line, const char* expr, double actual,
                    double expected);
void ReportEqStr(const char* file, int line, const char* expr, const char* actual,
                 const char* expected);

}  // namespace gridpulse_test

#define GRIDPULSE_CONCAT_INNER(a, b) a##b
#define GRIDPULSE_CONCAT(a, b) GRIDPULSE_CONCAT_INNER(a, b)

#define TEST(group, name)                                                                \
  static void group##_##name();                                                          \
  [[maybe_unused]] static const bool GRIDPULSE_CONCAT(group##_##name##_reg_, __LINE__) = \
      ::gridpulse_test::Register(#group, #name, group##_##name);                         \
  static void group##_##name()

#define CHECK_TRUE(expr)                                                   \
  do {                                                                     \
    if (!(expr)) {                                                         \
      ::gridpulse_test::Fail(__FILE__, __LINE__, "expected true: " #expr); \
    } else {                                                               \
      ::gridpulse_test::Pass();                                            \
    }                                                                      \
  } while (0)

#define CHECK_FALSE(expr)                                                   \
  do {                                                                      \
    if ((expr)) {                                                           \
      ::gridpulse_test::Fail(__FILE__, __LINE__, "expected false: " #expr); \
    } else {                                                                \
      ::gridpulse_test::Pass();                                             \
    }                                                                       \
  } while (0)

// Signed integer equality.
#define CHECK_EQ(actual, expected)                           \
  ::gridpulse_test::ReportEq(__FILE__, __LINE__, #actual,    \
                             static_cast<long long>(actual), \
                             static_cast<long long>(expected))

// Unsigned integer equality, so a 32- or 64-bit value with the top bit set is not
// printed as a negative number when it fails.
#define CHECK_EQ_U(actual, expected)                                   \
  ::gridpulse_test::ReportEqU(__FILE__, __LINE__, #actual,             \
                              static_cast<unsigned long long>(actual), \
                              static_cast<unsigned long long>(expected))

#define CHECK_EQ_STR(actual, expected) \
  ::gridpulse_test::ReportEqStr(__FILE__, __LINE__, #actual, (actual), (expected))

// Exact double equality. Used where the spec demands an exact value, such as a
// clamped zero.
#define CHECK_EQ_DOUBLE(actual, expected) \
  ::gridpulse_test::ReportEqDouble(__FILE__, __LINE__, #actual, (actual), (expected))

// Approximate equality with an explicit tolerance.
#define CHECK_NEAR(actual, expected, tolerance)                                  \
  do {                                                                           \
    const double gridpulse_a = (actual);                                         \
    const double gridpulse_e = (expected);                                       \
    const double gridpulse_d = (gridpulse_a > gridpulse_e)                       \
                                   ? (gridpulse_a - gridpulse_e)                 \
                                   : (gridpulse_e - gridpulse_a);                \
    if (!(gridpulse_d <= (tolerance))) {                                         \
      ::gridpulse_test::ReportEqDouble(__FILE__, __LINE__, #actual, gridpulse_a, \
                                       gridpulse_e);                             \
    } else {                                                                     \
      ::gridpulse_test::Pass();                                                  \
    }                                                                            \
  } while (0)

#endif  // GRIDPULSE_TEST_UTIL_H_
