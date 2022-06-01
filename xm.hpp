#ifndef XM_HPP
#define XM_HPP
//==============================================================================
//
// eXaM - single source, public domain, C++ unit testing framework.
//
// Refer to http://unlicense.org/ for licensing information.
//
//==============================================================================
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdint>

// Compiler identification
#if defined(_MSC_VER)
#define XM_COMPILER_MSVC
#endif

// Diagnostics control
#if defined(XM_COMPILER_MSVC)
#define XM_MSVC_WARNING(x) __pragma(warning(x))

#define XM_WARNINGS_PUSH XM_MSVC_WARNING(push)
#define XM_WARNINGS_POP XM_MSVC_WARNING(pop)
#else
#define XM_MSVC_WARNING(x)

#define XM_WARNINGS_PUSH
#define XM_WARNINGS_POP
#endif

//==============================================================================
/// Using eXaM, your interaction will mainly be with the functions in the 'xm'
/// namespace (outside of 'detail'), and the macros at the bottom. Refer to the
/// XM_TEST() and XM_TEST_F() macros for declaring and defining test cases and
/// the XM_ASSERT_*() macros for the actual checks.
namespace xm
{

///@brief Sets @a output as the stream where messages are sent (stdout by default).
///@note The Windows version only supports coloured output on stdout and stderr.
void SetOutput(std::ostream& output);

///@brief Allows specifying inclusion and exclusion filters, which tests' suite
/// and name is checked against.
///@par Tests' names must be valid C++ identifiers, so it makes sense to include only
/// accepted characters in filter names (this is not checked). In addition, the
/// '*' wildcard may be used to substitute for 0-n characters at any point in names.
/// Filter names are delimited by ':'. Filter names preceding the first '-' character
/// in @a filterStr are inclusion filters, exclusion filters thereafter. Zero length
/// names are ignored.
///@par If any inclusion filters are specified, tests must match at least one of them.
/// Exclusion filters (if any) are applied thereafter.
///@note Filters set by a previous call are discarded.
void SetFilter(char const* filterStr);

///@brief Runs all test, checking suite and test name against the filters first.
/// Each test is run until the first failed assertion (if any), at which point the
/// reason for the failure is printed.
///@return The number of failed tests (i.e. it can be used as exit status to return
/// from main() directly).
int RunTests();

namespace detail
{

// Provides a facility to format strings into a pre-allocated, thread local buffer
// without making any further allocations.
struct StaticStringBuilder
{
  StaticStringBuilder();
  ~StaticStringBuilder();

  std::ostream& Stream() { return mStream; }

  operator char const*() const;

private:
  std::ostream mStream;

  StaticStringBuilder(StaticStringBuilder const&) = delete;
  StaticStringBuilder& operator=(StaticStringBuilder const&) = delete;
  StaticStringBuilder(StaticStringBuilder&&) = delete;
  StaticStringBuilder& operator=(StaticStringBuilder&&) = delete;
};

// Big enough integer type to wrap integers and enums for printing.
using IntWrap = long long;

// Wraps C strings, char arrays and types that provide size() and c_str() methods,
// for equality comparison and printing.
struct StringWrap
{
  StringWrap(char const* str): mString{ str }, mSize{ str ? strlen(str) : 0 }
  {}

  template <size_t kSize>
  StringWrap(char const (&str)[kSize]) : mString{ str }, mSize{ kSize }
  {}

  template <typename T,
    std::enable_if_t<
      std::is_member_function_pointer_v<decltype(&T::c_str)> &&
      std::is_member_function_pointer_v<decltype(&T::size)>
    >* = nullptr
  >
  StringWrap(T const& str) : mString{ str.c_str() }, mSize{ str.size() }
  {}

  bool operator==(StringWrap const& other) const
  {
    return !!mString == !!other.mString &&
      strncmp(mString, other.mString, std::max(mSize, other.mSize)) == 0;
  }

  char const* const mString;
  size_t const mSize;
};

std::ostream& operator<<(std::ostream& os, StringWrap const& sw);

// Printer categories for funneling values.
enum Category
{
  kNumeric,
  kString,
  kFunction,
  kOther,
};

template <Category k> // kOther is default. Prints objects as a byte buffer, up to 64 bytes.
struct Printer
{
  enum { kMaxBytesPrinted = 64u };

  template <typename T>
  static void Print(T const& value, std::ostream& os)
  {
    auto i0 = reinterpret_cast<uint8_t const*>(&value);
    auto i1 = i0 + std::min(sizeof(T), size_t(kMaxBytesPrinted));
    char buffer[4];
    while (i0 != i1)
    {
      snprintf(buffer, sizeof(buffer), "%02x ", *i0);
      os << buffer;
      ++i0;
    }

    if constexpr (sizeof(T) > kMaxBytesPrinted)
    {
      os << "...";
    }
  }
};

template <>
struct Printer<kFunction>
{
  template <typename T>
  static void Print(T const& value, std::ostream& os)
  {
    auto f = &value;
    os << reinterpret_cast<void const*&>(f);
  }
};

template <>
struct Printer<kString> // Prints wrappable strings.
{
  static void Print(StringWrap const& value, std::ostream& os)
  {
    os << "\"" << value << "\"";
  }
};

template <>
struct Printer<kNumeric>  // Prints numeric types, including enums (on a best effort basis) and bools.
{
  template <typename T>
  static void Print(T const& value, std::ostream& os)
  {
    os << static_cast<IntWrap>(value);
  }

  static void Print(float value, std::ostream& os)
  {
    os << value << "f";
  }

  static void Print(double value, std::ostream& os)
  {
    os << value;
  }

  static void Print(bool value, std::ostream& os)
  {
    os << (value ? "true" : "false");
  }
};

// Responsible for dispatching print calls based on the types of the values being
// compared. E.g. char* will only be printed as a string if the other value is
// string wrappable (otherwise it may be a pointer to the end of a range / buffer,
// so we shouldn't be dereferencing it).
template <typename T1, typename T2>
struct PrintDispatcher
{
XM_WARNINGS_PUSH
XM_MSVC_WARNING(disable: 4180)  // MSVC considers const-qualifying function pointer types redundant.

  static void Dispatch(T1 const& value, std::ostream& os)
  {
XM_WARNINGS_POP

    Printer<std::is_convertible_v<T1, StringWrap> ? kString :
      (std::is_enum_v<T1> || std::is_convertible_v<T1, IntWrap>) ? kNumeric :
      std::is_function_v<T1> ? kFunction :
      kOther>::Print(value, os);
  }
};

template <typename T1, typename T2>
struct PrintDispatcher<T1*, T2*>
{
  static void Dispatch(T1 const* value, std::ostream& os)
  {
    if (value)
    {
      os << static_cast<void const*>(value);
    }
    else
    {
      os << "nullptr";
    }
  }
};

template <typename T1>
struct PrintDispatcher<T1*, nullptr_t>
{
  static void Dispatch(T1 const* value, std::ostream& os)
  {
    if (value)
    {
      os << static_cast<void const*>(value);
    }
    else
    {
      os << "nullptr";
    }
  }
};

template <typename T2>
struct PrintDispatcher<nullptr_t, T2>
{
  static void Dispatch(nullptr_t, std::ostream& os)
  {
    os << "nullptr";
  }
};

struct Formatter  // Formats the messages displayed for failed assertions.
{
  template <typename T1, typename T2>
  static void FormatExpression(char const* expr, T1 const& value, std::ostream& os)
  {
    os << expr;

    std::ostringstream ss;
    PrintDispatcher<T1, T2>::Dispatch(value, ss);
    if (ss.str().compare(expr) != 0)
    {
      os << " (which is " << ss.str() << ")";
    }
  }

  template <typename T1, typename T2>
  static char const* Format(char const* aStr, T1 const& a, char const* opStr, char const* bStr, T2 const& b)
  {
    StaticStringBuilder ssb;
    auto& stream = ssb.Stream();
    stream << "Expected: ";
    FormatExpression<T1, T2>(aStr, a, stream);
    stream << " " << opStr << " ";
    FormatExpression<T2, T1>(bStr, b, stream);

    return ssb;
  }

  static char const* Format(char const* str);

private:
  Formatter() = delete;
};

// Throws the type of exception that the test framework recognises as a failure,
// with the given message.
void Fail(char const* message);

struct Assert // Performs checks and throws exceptions for RunTests() to catch.
{
  static void True(bool value, char const* str);

  template <typename T, typename U>
  static void Equal(T const& a, U const& b, char const* aStr, char const* bStr)
  {
    Check(a == b, Formatter::Format(aStr, a, "==", bStr, b));
  }

  template <typename T, typename U>
  static void LessThan(T const& a, U const& b, char const* aStr, char const* bStr)
  {
    Check(a < b, Formatter::Format(aStr, a, "<", bStr, b));
  }

  template <typename T, typename U>
  static void LessEqual(T const& a, U const& b, char const* aStr, char const* bStr)
  {
    Check(a <= b, Formatter::Format(aStr, a, "<=", bStr, b));
  }

  template <typename T, typename U>
  static void GreaterThan(T const& a, U const& b, char const* aStr, char const* bStr)
  {
    Check(a > b, Formatter::Format(aStr, a, ">", bStr, b));
  }

  template <typename T, typename U>
  static void GreaterEqual(T const& a, U const& b, char const* aStr, char const* bStr)
  {
    Check(a >= b, Formatter::Format(aStr, a, ">=", bStr, b));
  }

  template <typename T, typename U>
  static void NotEqual(T const& a, U const& b, char const* aStr, char const* bStr)
  {
    Check(a != b, Formatter::Format(aStr, a, "!=", bStr, b));
  }

  static void Check(bool value, char const* message);

private:
  Assert() = delete;
};

class Test  // Test base class. Derive from & instantiate using the XM_TEST() and XM_TEST_F() macros.
{
protected:
  Test(char const* suite, char const* name);
  virtual ~Test();

  bool Run();

  virtual void RunInternal() =0;

private:
  char const* mSuite;
  char const* mName;
  Test* mNext = nullptr;

  friend int xm::RunTests();
};

} // detail
} // xm

#define XM_DETAIL_TEST_NAME(suite, name) suite ## _ ## name
#define XM_DETAIL_TEST_CLASS_NAME(suite, name) XM_DETAIL_TEST_NAME(suite, name ## TestType)

///@brief Use this to declare and define a simple test case. e.g.:<br/>
/// XM_TEST(Io, Serialization) {<br/>
///   // test body here.<br/>
/// }<br/>
#define XM_TEST(suite, name) class XM_DETAIL_TEST_CLASS_NAME(suite, name) : protected xm::detail::Test\
  {\
  public:\
    XM_DETAIL_TEST_CLASS_NAME(suite, name) () : xm::detail::Test(#suite, #name) {}\
  protected:\
    void RunInternal() override;\
  } XM_DETAIL_TEST_NAME(suite, name ## Test);\
  void XM_DETAIL_TEST_CLASS_NAME(suite, name) ::RunInternal()

///@brief Use this to declare and define a test case using a default constructible
/// class which shall be instantiated, providing an opportunity for fixture setup
/// (in its constructor) and teardown (in its destructor). e.g.:<br/>
/// struct Io // fixture<br/>
/// {<br/>
///   Io() { InitFileSystem(); }<br/>
///   ~Io() { ShutdownFileSystem(); }<br/>
/// };<br/>
/// XM_TEST_F(Io, Serialization) {<br/>
///   // test body here.<br/>
/// }<br/>
#define XM_TEST_F(fixture, name) class XM_DETAIL_TEST_CLASS_NAME(fixture, name) : protected xm::detail::Test\
  {\
  public:\
    XM_DETAIL_TEST_CLASS_NAME(fixture, name) () : xm::detail::Test(#fixture, #name) {}\
    void RunInternal() override {\
      fixture f;\
      RunItAlready();\
    };\
  protected:\
    void RunItAlready();\
  } XM_DETAIL_TEST_NAME(fixture, name ## Test);\
  void XM_DETAIL_TEST_CLASS_NAME(fixture, name) ::RunItAlready()

///@brief Fails a test with the given @a message.
///@note The message is printed as is, with no further formatting.
#define XM_FAIL(message) xm::detail::Fail(message)

///@brief Asserts @a expr to be true.
///@note Prefer to use XM_ASSERT_STREQ() for equality of strings, which eliminates
/// the rist of (char) pointers being compared arithmetically, rather than their
/// pointed-to data, lexically.
#define XM_ASSERT_TRUE(expr) xm::detail::Assert::True(expr, #expr)

///@brief Asserts @a expr to be false.
#define XM_ASSERT_FALSE(expr) xm::detail::Assert::True(!expr, "!(" #expr ")")

///@brief Asserts @a a and @a b to be equal.
#define XM_ASSERT_EQ(a, b) xm::detail::Assert::Equal(a, b, #a, #b)

///@brief Asserts @a a to be less than @a b.
#define XM_ASSERT_LT(a, b) xm::detail::Assert::LessThan(a, b, #a, #b)

///@brief Asserts @a a to be less than or equal to @a b.
#define XM_ASSERT_LE(a, b) xm::detail::Assert::LessEqual(a, b, #a, #b)

///@brief Asserts @a a to be greater than @a b.
#define XM_ASSERT_GT(a, b) xm::detail::Assert::GreaterThan(a, b, #a, #b)

///@brief Asserts @a a to be greater than or equal to @a b.
#define XM_ASSERT_GE(a, b) xm::detail::Assert::GreaterEqual(a, b, #a, #b)

///@brief Asserts @a and @a b to not be equal.
#define XM_ASSERT_NE(a, b) xm::detail::Assert::NotEqual(a, b, #a, #b)

///@brief Asserts equality of floating point values @a a and @a b
#define XM_ASSERT_FEQ(a, b, epsilon) xm::detail::Assert::LessThan(std::abs(a - b), epsilon)

///@brief Asserts @a and @a b, explicitly handled as strings, to be equal.
#define XM_ASSERT_STREQ(a, b) xm::detail::Assert::Equal(xm::detail::StringWrap(a), xm::detail::StringWrap(b), #a, #b)

///@brief Asserts @a expr to result in an exception of the given @a exception type being thrown.
#define XM_ASSERT_THROW(expr, exception)\
  try {\
    expr;\
    XM_FAIL(#exception ". No exception was thrown from " #expr ".");\
  }\
  catch (exception const&) {}\
  catch (...) { XM_FAIL(#exception ". " #expr " threw the wrong exception."); }

#endif  //XM_HPP
