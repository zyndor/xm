eXaM - single source, public domain, C++ unit testing framework

License
-------

eXaM is placed in the public domain. Refer to http://unlicense.org/ for further
details.

Usage
-----

1. Create an executable project to serve as your test runner, incorporating
  `xm.hpp` and `xm.cpp` into its build.

2. Declare your tests using `XM_TEST(suite, name)` or, if you intend to use a
  fixture class - which performs setup in its default constructor and teardown
  in its destructor -, using `XM_TEST_F(fixture, name)`. (The name of the
  fixture doubles as the name of the suite.)

3. Perform your checks using the `XM_ASSERT_*()` macros. Alternatively, for
  scenarios that the asserts cannot serve, you can fail tests explicitly with a
  message of your choosing, using `XM_FAIL(message)`.

4. (optional) Set inclusion / exclusion filters using `xm::SetFilters()`, in
  your test runner.

5. Execute the tests with `xm::RunTests()`. Progress will be logged to the
  output stream (stdout by default; use `xm::SetOutput()` prior, to override).

Filters
-------

Filters are set from a single string expression, where the individual filters
are delimited using the `:` character. As the filters are checked against test
suites and names (which are used in their declaration), it makes sense to only
use characters allowed in C++ identifiers (no validation is done).

Wildcards (`*`) are supported in filter expressions and substitute 0 to n
characters.

Filters preceding the first `-` character are inclusion filters. Exclusion
filters must follow a `-` character. Both are optional.

All tests are included by default. Exclusion filters are applied after inclusion
filters.

Example
-------
```c++
#include "xm.hpp"

// Foo.hpp
struct Foo {
  uintptr_t value;

  Foo(): value(reinterpret_cast<uintptr_t>(this))
  {}

  uintptr_t Hash(Foo const& other) const {
    return value ^ other.value;
  }

  operator uintptr_t() const {
    return value;
  }
};

// FooTest.cpp
struct FooTest { // Fixture class
  FooTest() {
    std::cout << "Hello!" << std::endl;
  }

  ~FooTest() {
    std::cout << "Goodbye!" << std::endl;
  }
};

XM_TEST(FooTest, That) {
  XM_FAIL("'That' should not have been included.");
}

XM_TEST_F(FooTest, NotThis) { // using fixture, not executed
  XM_FAIL("'NotThis' should have been excluded.");
}

XM_TEST_F(FooTest, This) { // using fixture
  Foo f1, f2;

  XM_ASSERT_NE(f1, 0);
  XM_ASSERT_NE(f1, f2);

  auto hash = f1.Hash(f2);
  XM_ASSERT_EQ(hash ^ f1, f2);
  XM_ASSERT_EQ(hash ^ f2, f1);
}

int main(int argc, char** argv) {
  xm::SetFilter("*This*-Not*"); // includes This and NotThis, then excludes NotThis.
  return xm::RunTests();
}
```
