#pragma once

// Minimal self-contained test runner.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace testing
{

struct TestCase
{
  const char* name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& Registry()
{
  static std::vector<TestCase> tests;
  return tests;
}

inline int& FailureCount()
{
  static int failures = 0;
  return failures;
}

struct Registrar
{
  Registrar(const char* name, std::function<void()> fn) { Registry().push_back({ name, std::move(fn) }); }
};

inline void ReportFailure(const char* file, int line, const std::string& message)
{
  ++FailureCount();
  std::fprintf(stderr, "  FAILED %s:%d: %s\n", file, line, message.c_str());
}

inline int RunAll(const char* filter)
{
  int failedTests = 0, run = 0;
  for (const auto& test : Registry())
  {
    if (filter && std::string(test.name).find(filter) == std::string::npos)
      continue;
    ++run;
    const int before = FailureCount();
    std::printf("[ RUN  ] %s\n", test.name);
    test.fn();
    const bool ok = FailureCount() == before;
    std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", test.name);
    failedTests += ok ? 0 : 1;
  }
  std::printf("\n%d test(s) run, %d failed\n", run, failedTests);
  return failedTests == 0 && run > 0 ? 0 : 1;
}

} // namespace testing

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)
#define TEST(name)                                                                                   \
  static void TEST_CONCAT(test_fn_, __LINE__)();                                                     \
  static testing::Registrar TEST_CONCAT(test_reg_, __LINE__)(name, &TEST_CONCAT(test_fn_, __LINE__)); \
  static void TEST_CONCAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                       \
  do                                                                      \
  {                                                                       \
    if (!(cond))                                                          \
      testing::ReportFailure(__FILE__, __LINE__, "CHECK(" #cond ")");     \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                                         \
  do                                                                                                  \
  {                                                                                                   \
    const double va_ = static_cast<double>(a), vb_ = static_cast<double>(b);                          \
    if (!(std::fabs(va_ - vb_) <= (tol)))                                                             \
    {                                                                                                 \
      char buf_[256];                                                                                 \
      std::snprintf(buf_, sizeof buf_, "%s = %.9g, %s = %.9g, tol %.3g", #a, va_, #b, vb_, (double)(tol)); \
      testing::ReportFailure(__FILE__, __LINE__, buf_);                                               \
    }                                                                                                 \
  } while (0)

#define CHECK_LT(a, b)                                                                     \
  do                                                                                       \
  {                                                                                        \
    const double va_ = static_cast<double>(a), vb_ = static_cast<double>(b);               \
    if (!(va_ < vb_))                                                                      \
    {                                                                                      \
      char buf_[256];                                                                      \
      std::snprintf(buf_, sizeof buf_, "%s = %.9g is not < %s = %.9g", #a, va_, #b, vb_);  \
      testing::ReportFailure(__FILE__, __LINE__, buf_);                                    \
    }                                                                                      \
  } while (0)
