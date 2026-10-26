/*
 * Unit tests for the maintainable C++ surface of SCUFF-EM.
 *
 * The harness is deliberately small and dependency-free.  Tests are added
 * here as the libhrutil parser and string helpers are modernized; numerical
 * regression coverage remains in tests/Mie and tests/Fresnel.
 */

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "libhrutil.h"

namespace {

constexpr std::size_t MAXSTR_UNDER_TEST = 1000;

int NumChecks = 0;
int NumFailures = 0;

void Check(bool Condition, const char *Expression, const char *File, int Line)
{
  ++NumChecks;
  if (!Condition)
   { ++NumFailures;
     std::fprintf(stderr, "FAIL %s:%i: %s\n", File, Line, Expression);
   }
}

void CheckStringEqual(const char *Actual, const char *Expected,
                      const char *File, int Line)
{
  ++NumChecks;
  if (std::strcmp(Actual, Expected) != 0)
   { ++NumFailures;
     std::fprintf(stderr, "FAIL %s:%i: expected \"%s\", got \"%s\"\n",
                  File, Line, Expected, Actual);
   }
}

#define CHECK(Condition) Check((Condition), #Condition, __FILE__, __LINE__)
#define CHECK_STRING(Actual, Expected) \
  CheckStringEqual((Actual), (Expected), __FILE__, __LINE__)

void TestPathHelpers()
{
  CHECK_STRING(RemoveDirectories("/a/b/c.dat"), "c.dat");
  CHECK_STRING(RemoveDirectories("c.dat"), "c.dat");

  CHECK_STRING(RemoveExtension("/a/b/c.dat"), "/a/b/c");
  CHECK_STRING(RemoveExtension("/a.b/c"), "/a.b/c");
  CHECK_STRING(RemoveExtension("c"), "c");

  CHECK_STRING(GetFileExtension("/a/b/c.dat"), "dat");
  CHECK_STRING(GetFileExtension("/a.b/c"), "");
  CHECK_STRING(GetFileExtension("c"), "");

  CHECK_STRING(GetFileBase("/a/b/c.dat"), "c");
  CHECK_STRING(GetFileBase("c.dat"), "c");

  /* Boundary-length inputs must remain NUL-terminated rather than
     overrunning the fixed-size result buffers. */
  std::string LongName(2000, 'a');
  CHECK(std::strlen(RemoveDirectories(LongName.c_str())) == MAXSTR_UNDER_TEST - 1);
  CHECK(std::strlen(RemoveExtension((LongName + ".dat").c_str())) ==
        MAXSTR_UNDER_TEST - 1);
  CHECK_STRING(GetFileExtension((LongName + ".dat").c_str()), "dat");
}

void TestComplexParsing()
{
  cdouble z;

  CHECK(S2CD("3+4i", &z) == 0);
  CHECK(real(z) == 3.0);
  CHECK(imag(z) == 4.0);

  CHECK(S2CD("3+4j", &z) == 0);
  CHECK(real(z) == 3.0);
  CHECK(imag(z) == 4.0);

  CHECK(S2CD("3-4I", &z) == 0);
  CHECK(real(z) == 3.0);
  CHECK(imag(z) == -4.0);

  CHECK(S2CD("3-4J", &z) == 0);
  CHECK(real(z) == 3.0);
  CHECK(imag(z) == -4.0);

  CHECK(S2CD("3i", &z) == 0);
  CHECK(real(z) == 0.0);
  CHECK(imag(z) == 3.0);

  CHECK(S2CD("3j", &z) == 0);
  CHECK(real(z) == 0.0);
  CHECK(imag(z) == 3.0);

  CHECK(S2CD("3 + 4i", &z) == 0);
  CHECK(real(z) == 3.0);
  CHECK(imag(z) == 4.0);

  CHECK(S2CD("-2.5", &z) == 0);
  CHECK(real(z) == -2.5);
  CHECK(imag(z) == 0.0);

  CHECK(S2CD("1e3", &z) == 0);
  CHECK(real(z) == 1000.0);

  CHECK(S2CD("", &z) != 0);
  CHECK(S2CD("i", &z) != 0);
  CHECK(S2CD("1foo", &z) != 0);
  CHECK(S2CD("3+4x", &z) != 0);
  CHECK(S2CD("3+4i tail", &z) != 0);
  CHECK(S2CD("3i4", &z) != 0);
}

void TestComplexFormatting()
{
  const char *s = CD2S(cdouble(1.5, 0.0), "%g");
  CHECK_STRING(s, "1.5");

  s = CD2S(cdouble(1.0, -2.0), "%g%+gi");
  CHECK_STRING(s, "1-2i");

  /* A format without conversion specifiers is copied literally and must
     still be NUL-terminated. */
  s = CD2S(cdouble(1.0, 2.0),
           "012345678901234567890123456789012345678901234567890123456789");
  CHECK(std::strlen(s) == 49);
}

void TestTokenize()
{
  char Line[] = "alpha beta gamma";
  char *Tokens[4] = {nullptr, nullptr, nullptr, nullptr};
  int NumTokens = Tokenize(Line, Tokens, 4);

  CHECK(NumTokens == 3);
  CHECK_STRING(Tokens[0], "alpha");
  CHECK_STRING(Tokens[1], "beta");
  CHECK_STRING(Tokens[2], "gamma");
  CHECK(Tokens[3] == nullptr);

  char TruncatedLine[] = "one two three four";
  char *TruncatedTokens[2] = {nullptr, nullptr};
  bool Truncated = false;
  NumTokens = Tokenize(TruncatedLine, TruncatedTokens, 2, " \t\n", &Truncated);

  CHECK(NumTokens == 2);
  CHECK(Truncated);
  CHECK_STRING(TruncatedTokens[0], "one");
  CHECK_STRING(TruncatedTokens[1], "two");

  char EmptyLine[] = "";
  Truncated = true;
  NumTokens = Tokenize(EmptyLine, TruncatedTokens, 2, " \t\n", &Truncated);
  CHECK(NumTokens == 0);
  CHECK(!Truncated);
}

struct TestCase
{
  const char *Name;
  void (*Function)();
};

} // namespace

int main()
{
  const TestCase Tests[] = {
    {"path helpers", TestPathHelpers},
    {"complex parsing", TestComplexParsing},
    {"complex formatting", TestComplexFormatting},
    {"tokenize", TestTokenize},
  };

  for (const TestCase &Test : Tests)
   { std::printf("[ RUN      ] %s\n", Test.Name);
     Test.Function();
   }

  std::printf("%i checks, %i failures\n", NumChecks, NumFailures);
  return NumFailures == 0 ? 0 : 1;
}
