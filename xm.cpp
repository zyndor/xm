//
// eXaM - single source, public domain, C++ unit testing framework.
//
// Refer to http://unlicense.org/ for licensing information.
//
//==============================================================================
#include "xm.hpp"
#include <chrono>
#include <vector>
#include <string_view>
#include <cassert>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#endif

namespace xm
{
namespace
{

struct StreamBuf : std::streambuf
{
  StreamBuf(size_t size, char* buffer)
  : mSize(size),
    mBuffer(buffer)
  {
    setp(buffer, buffer + size);
  }

  void Reset()
  {
    setp(mBuffer, mBuffer + mSize);
  }

  size_t GetPos() const
  {
    return pptr() - mBuffer;
  }

private:
  size_t mSize;
  char* mBuffer;
};

namespace sc = std::chrono;

struct Clock
{
  Clock()
  : mLast(sc::high_resolution_clock::now())
  {}

  double Measure()
  {
    auto last = sc::high_resolution_clock::now();
    auto diff = sc::duration_cast<sc::duration<double, std::milli>>(last - mLast);
    mLast = last;
    return diff.count();
  }

  sc::high_resolution_clock::time_point mLast;
};

struct Exception
{
  char const* const message;
};

struct StreamColor
{
  uint16_t attribute;
};

#if !defined(FOREGROUND_RED)
#define FOREGROUND_RED 31
#endif
#if !defined(FOREGROUND_GREEN)
#define FOREGROUND_GREEN 32
#endif

#ifdef _WIN32
HANDLE sOutputHandle = GetStdHandle(STD_OUTPUT_HANDLE);

#define FOREGROUND_RESET FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
#else
#define FOREGROUND_RESET 0
#endif

std::ostream& operator<<(std::ostream& stream, StreamColor color)
{
#ifdef _WIN32
  if (sOutputHandle)
  {
    SetConsoleTextAttribute(sOutputHandle, color.attribute);
  }
#else
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "\033[%d;%dm", (color.attribute & 0xff00) >> 8,
    (color.attribute & 0xff));
  stream << buffer;
#endif
  return stream;
}

enum Status
{
  FAILED,
  OK,
  STARTED,
  SUITE,
  TALLY
};

constexpr char const* const kStatus[]{
  "    FAILED",
  "        OK",
  "STARTED   ",
  "==========",
  "----------",
};

constexpr char kFilterWildcard = '*';
constexpr char kIdSeparator = ':';
constexpr char kJoinTestSuiteName = '_';

thread_local char sMessageBuffer[1024];

thread_local StreamBuf sStreamBuf{ sizeof(sMessageBuffer), sMessageBuffer };

std::string sIncludeFilter(1, kFilterWildcard);
std::string sExcludeFilter;

detail::Test const* sFirst = nullptr;
detail::Test* sLast = nullptr;

char const* sError = nullptr;

std::ostream* sOutput = &std::cout;

///@brief Attempts to match the @a filter with the @a id, handling wildcards.
///@returns false on a mismatch, and true if we've made it to both the end of filter
/// and id without a mismatch.
bool FilterMatch(std::string_view filter, std::string_view id)
{
  // If we're at the end of the string, then we succeed if there's no non-wildcard characters
  // left of the filter.
  if (id.empty())
  {
    return filter.find_first_not_of(kFilterWildcard) == std::string::npos;
  }

  if (filter.empty())  // filter finished, id didn't -- fail.
  {
    return false;
  }

  auto const first = filter[0];
  if (first == id.front()) // next character matches -- proceed.
  {
    return FilterMatch(filter.substr(1), id.substr(1));
  }

  if (first == kFilterWildcard) // next character is wildcard -- we either match or ignore the next character of id.
  {
    return FilterMatch(filter.substr(1), id) || FilterMatch(filter, id.substr(1));
  }

  return false; // mismatch -- fail.
}

///@brief Attempts to match the colon-delimited string of @a filters to the id in
/// the [ @a id, @a idEnd ) range.
///@return true if any of the filters have matched, false otherwise.
bool FiltersMatch(std::string const& filters, std::string_view id)
{
  std::string_view filter = filters;
  while (!filter.empty())
  {
    auto const end = [filter]
    {
      auto iEnd = filter.find(kIdSeparator);
      return iEnd == std::string::npos ? filter.size() : iEnd;
    }();
    if (FilterMatch(filter.substr(0, end), id))
    {
      return true;
    }

    filter = filter.substr(end + (end != filter.size()));
  }
  return false;
}

///@brief Determines if the given @a id is allowed through the filters.
bool IsAllowed(std::string_view id)
{
  return FiltersMatch(sIncludeFilter, id) &&
    !FiltersMatch(sExcludeFilter, id);
}

} // nonamespace

void SetOutput(std::ostream& out)
{
  sOutput = &out;
#ifdef _WIN32
  sOutputHandle =
    (&out == &std::cout) ? GetStdHandle(STD_OUTPUT_HANDLE) :
    (&out == &std::cerr) ? GetStdHandle(STD_ERROR_HANDLE) :
    nullptr;
#endif
}

void SetFilter(char const* filterStr)
{
  sIncludeFilter.assign(1, kFilterWildcard);
  sExcludeFilter.clear();
  if (filterStr)
  {
    auto negative = filterStr + strcspn(filterStr, "-");
    if (negative > filterStr)
    {
      sIncludeFilter.assign(filterStr, negative);
    }

    if (*negative == '-')
    {
      ++negative;
      auto end = negative + strcspn(negative, "\0");
      sExcludeFilter.assign(negative, end);
    }
  }
}

bool RunTest(detail::Test const& test)
{
  try
  {
    test.Run();
    return true;
  }
  catch (Exception const& e)
  {
    sError = e.message;
    return false;
  }
  catch (...)
  {
    sError = "Bad exception thrown.";
    return false;
  }
}

int RunTests()
{
  char idBuffer[1024];
  int run = 0;
  int passed = 0;
  int ignored = 0;
  char const* lastSuite = nullptr;
  auto test = sFirst;
  while (test)
  {
    StreamBuf idStreamBuf{ sizeof(idBuffer), idBuffer };
    std::ostream idStream(&idStreamBuf);
    test->GetId(idStream);
    std::string_view const id{ idBuffer, idStreamBuf.GetPos() };
    if (IsAllowed(id))
    {
      auto* const suite = test->GetSuite();
      if (suite != lastSuite)
      {
        *sOutput << "[" << kStatus[SUITE] << "] " << suite << std::endl;
        lastSuite = suite;
      }

      *sOutput << "[" << kStatus[STARTED] << "] " << id << std::endl;
      Clock clock;

      bool const result = RunTest(*test);
      double tDelta = clock.Measure();
      *sOutput << StreamColor{ uint16_t(result ? FOREGROUND_GREEN : FOREGROUND_RED) } <<
        "[" << kStatus[result] << "] " << id << " (" << tDelta << "ms)" <<
        StreamColor{ FOREGROUND_RESET } << std::endl;
      if (result)
      {
        ++passed;
      }
      else if (sError)
      {
        *sOutput << sError << std::endl;
        sError = nullptr;
      }
      ++run;
    }
    else
    {
      ++ignored;
    }

    test = test->GetNext();
  }

  *sOutput << "[" << kStatus[SUITE] << "]" << std::endl;
  *sOutput << "[" << kStatus[TALLY] << "] " << run << " tests run." << std::endl;
  *sOutput << "[" << kStatus[TALLY] << "] " << passed << " tests passed." << std::endl;
  if (ignored > 0)
  {
    *sOutput << "[" << kStatus[TALLY] << "] " << ignored << " tests ignored." << std::endl;
  }

  const bool endResult = passed == run;
  *sOutput << StreamColor{ uint16_t(endResult ? FOREGROUND_GREEN : FOREGROUND_RED) } <<
    "[" << kStatus[endResult] << "] Final result." << StreamColor{ FOREGROUND_RESET } << std::endl;

  return run - passed;
}

namespace detail
{

std::ostream& operator<<(std::ostream& os, StringWrap const& sw)
{
  auto i0 = sw.mString;
  auto i1 = i0 + sw.mSize;
  while (i0 != i1)
  {
    os.put(*i0);
    ++i0;
  }
  return os;
}

StaticStringBuilder::StaticStringBuilder()
: mStream(&sStreamBuf)
{}

StaticStringBuilder::~StaticStringBuilder()
{
  mStream << '\0';
  sStreamBuf.Reset();
}

StaticStringBuilder::operator char const*() const
{
  return sMessageBuffer;
}

char const* Formatter::Format(char const* str)
{
  StaticStringBuilder ssb;
  auto& stream = ssb.Stream();
  stream << "Expected: " << str;

  return ssb;
}

void Fail(char const* message)
{
  throw Exception{ message };
}

void Assert::True(bool value, char const* str)
{
  Check(value, Formatter::Format(str));
}

void Assert::Check(bool value, char const* message)
{
  if (!value)
  {
    Fail(message);
  }
}

Test::Test(char const* suite, char const* name)
: mSuite(suite),
  mName(name)
{
  if (sLast)
  {
    sLast->mNext = this;
  }
  else if (!sFirst)
  {
    sFirst = this;
  }

  sLast = this;
}

Test::~Test() = default;

void Test::GetId(std::ostream& os) const
{
  os << mSuite << kJoinTestSuiteName << mName;
}

} // detail
} // xm

#if defined XM_SELF_TEST

XM_TEST(Xm, FilterMatch)
{
  auto filter = [](std::string_view const& filter, std::string_view const& id) {
    return xm::FilterMatch(filter.data(), id);
  };

  XM_ASSERT_TRUE(filter("A*", "A"));
  XM_ASSERT_TRUE(filter("A*", "AB"));
  XM_ASSERT_TRUE(filter("A*", "ABC"));

  XM_ASSERT_TRUE(filter("*C", "C"));
  XM_ASSERT_TRUE(filter("*C", "BC"));
  XM_ASSERT_TRUE(filter("*C", "ABC"));

  XM_ASSERT_TRUE(filter("*C", "CABC"));

  XM_ASSERT_TRUE(filter("A*C", "AC"));
  XM_ASSERT_TRUE(filter("A*C", "ACBC"));
  XM_ASSERT_TRUE(filter("A*C", "ABCBCC"));

  XM_ASSERT_TRUE(filter("A*B*C", "ABC"));
  XM_ASSERT_TRUE(filter("A*B*C", "AABC"));
  XM_ASSERT_TRUE(filter("A*B*C", "ABBC"));
  XM_ASSERT_TRUE(filter("A*B*C", "ABCC"));
  XM_ASSERT_TRUE(filter("A*B*C", "AABBCC"));

  XM_ASSERT_FALSE(filter("B", "AB"));
  XM_ASSERT_FALSE(filter("B", "BA"));
  XM_ASSERT_FALSE(filter("B", "ABA"));
  XM_ASSERT_FALSE(filter("*AB", "ABC"));
  XM_ASSERT_FALSE(filter("BC*", "ABC"));
  XM_ASSERT_FALSE(filter("A*C", "AB"));
}

#endif // XM_SELF_TEST
