/*
 * CLI compatibility tests for the CLI11-backed ProcessOptions adapter.
 *
 * The adapter must preserve the legacy OptStruct contract while using CLI11
 * for option recognition, arity checking, and generated help.  Error paths
 * call OSUsage(), which exits; each case therefore runs in a child process so
 * that one tool call cannot terminate the whole test binary.  The child also
 * receives a private stdin pipe for the legacy "Name arg..." option files.
 */

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libhrutil.h"

namespace {

int NumChecks = 0;
int NumFailures = 0;

void Check(bool Condition, const char *Expression,
           const char *File, int Line)
{
  ++NumChecks;
  if (!Condition)
   { ++NumFailures;
     std::fprintf(stderr, "FAIL %s:%i: %s\n", File, Line, Expression);
   }
}

#define CHECK(Condition) Check((Condition), #Condition, __FILE__, __LINE__)

struct Options
{
  char *Text = nullptr;          int NumText = 0;
  int Integer = 0;               int NumInteger = 0;
  double Omega = 0.0;            int NumOmega = 0;
  double Direction[9] = {};      int NumDirection = 0;
  double Pair[2] = {};           int NumPair = 0;
  cdouble Z[3] = {};             int NumZ = 0;
  bool Flag = false;             int NumFlag = 0;
  char *File = nullptr;          int NumFile = 0;

  void MakeArray(std::vector<OptStruct> &Array)
  {
    Array.push_back({"Text", PA_STRING, 1, 1, &Text, &NumText,
                     "text value"});
    Array.push_back({"Integer", PA_INT, 1, 1, &Integer, &NumInteger,
                     "integer value"});
    Array.push_back({"Omega", PA_DOUBLE, 1, 1, &Omega, &NumOmega,
                     "frequency"});
    Array.push_back({"Direction", PA_DOUBLE, 3, 3, Direction,
                     &NumDirection, "direction vector"});
    Array.push_back({"Pair", PA_DOUBLE, 2, 1, Pair, &NumPair,
                     "two-value option"});
    Array.push_back({"Z", PA_CDOUBLE, 1, 3, Z, &NumZ,
                     "complex value"});
    Array.push_back({"Flag", PA_BOOL, 1, 1, &Flag, &NumFlag,
                     "boolean flag"});
    Array.push_back({"File", PA_STRING, 1, 1, &File, &NumFile,
                     "file name"});
    Array.push_back({nullptr, 0, 0, 0, nullptr, nullptr, nullptr});
  }
};

using CheckFunction =
  std::function<void(Options &, const std::vector<char *> &)>;

struct RunSpec
{
  std::vector<std::string> Args;
  const char *StdinText = nullptr;
  bool AbortOnUnknown = true;
  bool ZeroArgs = false;
  bool UseStringOverload = false;
  std::string StringArgs;

  int ExpectedExit = 0;
  bool ExpectReturn = true;
  std::vector<std::string> ExpectedOutput;
  CheckFunction Checks;
};

std::string ReadAll(int FileDescriptor)
{
  std::string Result;
  char Buffer[4096];

  for(;;)
   { ssize_t NumRead = read(FileDescriptor, Buffer, sizeof(Buffer));
     if (NumRead > 0)
      Result.append(Buffer, static_cast<size_t>(NumRead));
     else if (NumRead == 0)
      break;
     else if (errno != EINTR)
      break;
   }

  return Result;
}

bool Contains(const std::string &Haystack, const std::string &Needle)
{
  return Haystack.find(Needle) != std::string::npos;
}

void RunCase(const char *Name, const RunSpec &Spec)
{
  std::printf("[ RUN      ] %s\n", Name);
  std::fflush(nullptr);

  int Capture[2];
  if (pipe(Capture) != 0)
   { std::perror("pipe");
     ++NumChecks;
     ++NumFailures;
     return;
   }

  pid_t Pid = fork();
  if (Pid < 0)
   { std::perror("fork");
     close(Capture[0]);
     close(Capture[1]);
     ++NumChecks;
     ++NumFailures;
     return;
   }

  if (Pid == 0)
   {
     /* Child: isolate counters inherited from earlier failures. */
     NumChecks = 0;
     NumFailures = 0;

     close(Capture[0]);
     if (dup2(Capture[1], STDOUT_FILENO) < 0 ||
         dup2(Capture[1], STDERR_FILENO) < 0)
      _exit(120);
     close(Capture[1]);

     int InputFD;
     if (Spec.StdinText)
      { int InputPipe[2];
        if (pipe(InputPipe) != 0)
         _exit(120);

        const char *Pointer = Spec.StdinText;
        size_t Remaining = std::strlen(Spec.StdinText);
        while(Remaining > 0)
         { ssize_t NumWritten = write(InputPipe[1], Pointer, Remaining);
           if (NumWritten < 0)
            { if (errno == EINTR)
               continue;
              _exit(120);
            };
           Pointer += NumWritten;
           Remaining -= static_cast<size_t>(NumWritten);
         };
        close(InputPipe[1]);
        InputFD = InputPipe[0];
      }
     else
      InputFD = open("/dev/null", O_RDONLY);

     if (InputFD < 0 || dup2(InputFD, STDIN_FILENO) < 0)
      _exit(120);
     close(InputFD);

     Options OptionValues;
     std::vector<OptStruct> Array;
     OptionValues.MakeArray(Array);

     std::vector<std::string> ArgStorage;
     ArgStorage.emplace_back("scuff-cli-tests");
     for(const std::string &Arg : Spec.Args)
      ArgStorage.push_back(Arg);

     std::vector<char *> Argv;
     Argv.reserve(ArgStorage.size() + 1);
     for(std::string &Arg : ArgStorage)
      Argv.push_back(Arg.data());
     Argv.push_back(nullptr);

     if (Spec.UseStringOverload)
      ProcessOptions(Spec.StringArgs.c_str(), Array.data());
     else
      ProcessOptions(static_cast<int>(ArgStorage.size()), Argv.data(),
                     Array.data(), Spec.AbortOnUnknown, Spec.ZeroArgs);

     if (!Spec.ExpectReturn)
      { std::fflush(nullptr);
        _exit(2);
      };

     std::printf("CLI TEST RETURNED\n");
     std::fflush(nullptr);

     if (Spec.Checks)
      Spec.Checks(OptionValues, Argv);

     std::printf("CLI CHECKS %i %i\n", NumChecks, NumFailures);
     std::fflush(nullptr);
     _exit(NumFailures == 0 ? 0 : 3);
   }

  close(Capture[1]);
  std::string Output = ReadAll(Capture[0]);
  close(Capture[0]);

  int Status = 0;
  pid_t WaitResult;
  do
   { WaitResult = waitpid(Pid, &Status, 0); }
  while(WaitResult < 0 && errno == EINTR);

  bool Passed = (WaitResult == Pid);
  int ExitCode = -1;

  if (Passed && WIFEXITED(Status))
   { ExitCode = WEXITSTATUS(Status);
     Passed = (ExitCode == Spec.ExpectedExit);
   }
  else
   Passed = false;

  if (Spec.ExpectReturn && !Contains(Output, "CLI TEST RETURNED"))
   Passed = false;

  for(const std::string &Expected : Spec.ExpectedOutput)
   if (!Contains(Output, Expected))
    Passed = false;

  int ChildChecks = 0;
  int ChildFailures = 0;
  bool HasChildChecks = false;
  const std::string Marker = "CLI CHECKS ";
  size_t MarkerPosition = Output.rfind(Marker);
  if (MarkerPosition != std::string::npos)
   { if (std::sscanf(Output.c_str() + MarkerPosition + Marker.size(),
                     "%i %i", &ChildChecks, &ChildFailures) == 2)
      HasChildChecks = true;
   };

  if (HasChildChecks)
   { NumChecks += (ChildChecks > 0 ? ChildChecks : 1);
     NumFailures += ChildFailures;
   }
  else
   ++NumChecks;

  if (Passed)
   { std::printf("[       OK ] %s\n", Name);
     return;
   };

  if (!HasChildChecks || ChildFailures == 0)
   ++NumFailures;

  std::printf("[     FAIL ] %s (exit status %i, expected %i)\n",
              Name, ExitCode, Spec.ExpectedExit);

  for(const std::string &Expected : Spec.ExpectedOutput)
   if (!Contains(Output, Expected))
    std::printf("missing expected output: %s\n", Expected.c_str());

  if (!Output.empty())
   { std::printf("---- captured output ----\n%s", Output.c_str());
     if (Output.back() != '\n')
      std::printf("\n");
     std::printf("-------------------------\n");
   };
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestBasicTypes()
{
  RunSpec Spec;
  Spec.Args = {"--Text", "hello",
               "--Integer", "42",
               "--Omega", "1.5",
               "--Direction", "1", "0", "0",
               "--Direction", "0", "-1", "0.5",
               "--Z", "3+4I",
               "--Flag"};
  Spec.Checks = [](Options &O, const std::vector<char *> &)
   {
     CHECK(O.Text && std::strcmp(O.Text, "hello") == 0);
     CHECK(O.NumText == 1);
     CHECK(O.Integer == 42);
     CHECK(O.NumInteger == 1);
     CHECK(O.Omega == 1.5);
     CHECK(O.NumOmega == 1);
     CHECK(O.NumDirection == 2);
     CHECK(O.Direction[0] == 1.0 && O.Direction[1] == 0.0 &&
           O.Direction[2] == 0.0);
     CHECK(O.Direction[3] == 0.0 && O.Direction[4] == -1.0 &&
           O.Direction[5] == 0.5);
     CHECK(std::fabs(O.Z[0].real() - 3.0) < 1e-12);
     CHECK(std::fabs(O.Z[0].imag() - 4.0) < 1e-12);
     CHECK(O.NumZ == 1);
     CHECK(O.Flag);
     CHECK(O.NumFlag == 1);
   };
  RunCase("basic types, negative values, and complex syntax", Spec);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestCaseInsensitiveOptions()
{
  RunSpec Spec;
  Spec.Args = {"--TEXT", "mixed",
               "--InTeGeR", "17",
               "--oMeGa", "2.25",
               "--fLaG"};
  Spec.Checks = [](Options &O, const std::vector<char *> &)
   {
     CHECK(O.Text && std::strcmp(O.Text, "mixed") == 0);
     CHECK(O.Integer == 17);
     CHECK(O.Omega == 2.25);
     CHECK(O.Flag);
   };
  RunCase("option names are case-insensitive", Spec);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestRepeatedOptions()
{
  RunSpec LastWins;
  LastWins.Args = {"--Omega", "1.0", "--OMEGA", "2.0",
                   "--Pair", "1", "2", "--Pair", "3", "4",
                   "--Flag", "--flag"};
  LastWins.ExpectedOutput = {
    "overrides previously specified value",
    "unnecessarily specified multiple times"
  };
  LastWins.Checks = [](Options &O, const std::vector<char *> &)
   {
     CHECK(O.Omega == 2.0);
     CHECK(O.NumOmega == 1);
     CHECK(O.Pair[0] == 3.0 && O.Pair[1] == 4.0);
     CHECK(O.NumPair == 1);
     CHECK(O.Flag);
     CHECK(O.NumFlag == 1);
   };
  RunCase("MaxInstances==1 keeps the last value and warns", LastWins);

  RunSpec TooMany;
  TooMany.Args = {"--Z", "1i", "--Z", "2i", "--Z", "3i", "--Z", "4i"};
  TooMany.ExpectedExit = 1;
  TooMany.ExpectReturn = false;
  TooMany.ExpectedOutput = {"option --Z specified too many times"};
  RunCase("MaxInstances>1 rejects excess occurrences", TooMany);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestStdinOptions()
{
  RunSpec StdinOnly;
  StdinOnly.StdinText =
    "# legacy option file\n"
    "\n"
    "text from-stdin\n"
    "omega 3.5\n"
    "--Direction -1 -2 -3\n";
  StdinOnly.Checks = [](Options &O, const std::vector<char *> &)
   {
     CHECK(O.Text && std::strcmp(O.Text, "from-stdin") == 0);
     CHECK(O.Omega == 3.5);
     CHECK(O.NumOmega == 1);
     CHECK(O.NumDirection == 1);
     CHECK(O.Direction[0] == -1.0 && O.Direction[1] == -2.0 &&
           O.Direction[2] == -3.0);
   };
  RunCase("stdin option files accept bare and --prefixed names", StdinOnly);

  RunSpec Ordering;
  Ordering.StdinText = "omega 1.0\n";
  Ordering.Args = {"--Omega", "2.0"};
  Ordering.ExpectedOutput = {"overrides previously specified value"};
  Ordering.Checks = [](Options &O, const std::vector<char *> &)
   {
     CHECK(O.Omega == 2.0);
     CHECK(O.NumOmega == 1);
   };
  RunCase("stdin options are processed before argv options", Ordering);

  RunSpec Unknown;
  Unknown.StdinText = "bogus value\n";
  Unknown.ExpectedExit = 1;
  Unknown.ExpectReturn = false;
  Unknown.ExpectedOutput = {"stdin:1: unknown option --bogus"};
  RunCase("malformed stdin options abort even with AbortOnUnknown=false",
          Unknown);

  std::string TooManyTokens = "text";
  for(int n = 0; n < 50; n++)
   TooManyTokens += " x";
  RunSpec Truncated;
  Truncated.StdinText = TooManyTokens.c_str();
  Truncated.ExpectedExit = 1;
  Truncated.ExpectReturn = false;
  Truncated.ExpectedOutput = {"stdin:1: too many tokens (maximum 50)"};
  RunCase("over-long stdin lines are rejected", Truncated);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestNumericParsing()
{
  RunSpec InvalidDouble;
  InvalidDouble.Args = {"--Omega", "1foo"};
  InvalidDouble.ExpectedExit = 1;
  InvalidDouble.ExpectReturn = false;
  InvalidDouble.ExpectedOutput = {
    "invalid argument (1foo) passed to option --Omega"
  };
  RunCase("trailing garbage in a double is rejected", InvalidDouble);

  RunSpec InvalidInt;
  InvalidInt.Args = {"--Integer", "12x"};
  InvalidInt.ExpectedExit = 1;
  InvalidInt.ExpectReturn = false;
  InvalidInt.ExpectedOutput = {
    "invalid argument (12x) passed to option --Integer"
  };
  RunCase("trailing garbage in an integer is rejected", InvalidInt);

  RunSpec InvalidComplex;
  InvalidComplex.Args = {"--Z", "3+4x"};
  InvalidComplex.ExpectedExit = 1;
  InvalidComplex.ExpectReturn = false;
  InvalidComplex.ExpectedOutput = {
    "invalid argument (3+4x) passed to option --Z"
  };
  RunCase("trailing garbage in a complex value is rejected", InvalidComplex);

  RunSpec IntegerBase;
  IntegerBase.Args = {"--Integer", "0x10"};
  IntegerBase.Checks = [](Options &O, const std::vector<char *> &)
   {
     CHECK(O.Integer == 16);
   };
  RunCase("integer arguments keep the legacy base-0 syntax", IntegerBase);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestArityErrors()
{
  RunSpec Missing;
  Missing.Args = {"--Omega"};
  Missing.ExpectedExit = 1;
  Missing.ExpectReturn = false;
  Missing.ExpectedOutput = {
    "option --Omega needs 1 arguments (0 provided)"
  };
  RunCase("missing option argument is rejected", Missing);

  RunSpec Extra;
  Extra.Args = {"--Omega", "1", "2"};
  Extra.ExpectedExit = 1;
  Extra.ExpectReturn = false;
  Extra.ExpectedOutput = {
    "option --Omega needs 1 arguments (2 provided)"
  };
  RunCase("extra option argument is rejected", Extra);

  RunSpec MultiMissing;
  MultiMissing.Args = {"--Direction", "1", "2"};
  MultiMissing.ExpectedExit = 1;
  MultiMissing.ExpectReturn = false;
  MultiMissing.ExpectedOutput = {
    "option --Direction needs 3 arguments (2 provided)"
  };
  RunCase("multi-value arity is checked", MultiMissing);

  RunSpec BooleanValue;
  BooleanValue.Args = {"--Flag", "value"};
  BooleanValue.ExpectedExit = 1;
  BooleanValue.ExpectReturn = false;
  BooleanValue.ExpectedOutput = {
    "boolean option --Flag takes no arguments"
  };
  RunCase("boolean options reject values", BooleanValue);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestUnknownOptions()
{
  RunSpec Unknown;
  Unknown.Args = {"--Bogus"};
  Unknown.ExpectedExit = 1;
  Unknown.ExpectReturn = false;
  Unknown.ExpectedOutput = {"unknown option --Bogus"};
  RunCase("unknown long option is rejected", Unknown);

  RunSpec Terminator;
  Terminator.Args = {"--"};
  Terminator.ExpectedExit = 1;
  Terminator.ExpectReturn = false;
  Terminator.ExpectedOutput = {"unknown option --"};
  RunCase("bare -- retains the legacy unknown-option behavior", Terminator);

  RunSpec Positional;
  Positional.Args = {"not-an-option"};
  Positional.ExpectedExit = 1;
  Positional.ExpectReturn = false;
  Positional.ExpectedOutput = {"unknown option not-an-option"};
  RunCase("bare words are not treated as positional arguments", Positional);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestHelp()
{
  RunSpec LongHelp;
  LongHelp.Args = {"--help"};
  LongHelp.ExpectReturn = false;
  LongHelp.ExpectedOutput = {"[OPTIONS]", "--Omega", "--Direction", "--help"};
  RunCase("--help prints generated help and exits successfully", LongHelp);

  RunSpec ShortHelp;
  ShortHelp.Args = {"-h"};
  ShortHelp.ExpectReturn = false;
  ShortHelp.ExpectedOutput = {"[OPTIONS]", "--Omega"};
  RunCase("-h prints generated help and exits successfully", ShortHelp);

  RunSpec CaseInsensitiveHelp;
  CaseInsensitiveHelp.Args = {"--HELP"};
  CaseInsensitiveHelp.ExpectReturn = false;
  CaseInsensitiveHelp.ExpectedOutput = {"[OPTIONS]", "--Omega"};
  RunCase("--help is case-insensitive", CaseInsensitiveHelp);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestZeroArgs()
{
  RunSpec Spec;
  Spec.Args = {"--Omega", "1.5", "--Flag"};
  Spec.ZeroArgs = true;
  Spec.Checks = [](Options &O, const std::vector<char *> &Argv)
   {
     CHECK(O.Omega == 1.5);
     CHECK(O.Flag);
     CHECK(Argv.size() == 5);
     CHECK(Argv[1] == nullptr);
     CHECK(Argv[2] == nullptr);
     CHECK(Argv[3] == nullptr);
     CHECK(Argv[4] == nullptr);
   };
  RunCase("ZeroArgs clears consumed argv slots", Spec);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestAbortOnUnknownFalse()
{
  RunSpec Return;
  Return.Args = {"--Bogus", "value", "--Omega", "2.0"};
  Return.AbortOnUnknown = false;
  Return.Checks = [](Options &O, const std::vector<char *> &Argv)
   {
     CHECK(O.Omega == 2.0);
     CHECK(O.NumOmega == 1);
     CHECK(O.Text == nullptr);
     CHECK(Argv[1] != nullptr);
     CHECK(Argv[2] != nullptr);
   };
  RunCase("AbortOnUnknown=false skips unknown options", Return);

  RunSpec Zero;
  Zero.Args = {"--Bogus", "value", "--Omega", "2.0"};
  Zero.AbortOnUnknown = false;
  Zero.ZeroArgs = true;
  Zero.Checks = [](Options &O, const std::vector<char *> &Argv)
   {
     CHECK(O.Omega == 2.0);
     CHECK(Argv[1] != nullptr);
     CHECK(Argv[2] != nullptr);
     CHECK(Argv[3] == nullptr);
     CHECK(Argv[4] == nullptr);
   };
  RunCase("AbortOnUnknown=false only clears successfully processed slots",
          Zero);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestStringValues()
{
  RunSpec Empty;
  Empty.Args = {"--Text", "", "--File", "-mesh.msh"};
  Empty.Checks = [](Options &O, const std::vector<char *> &)
   {
     CHECK(O.Text != nullptr);
     CHECK(O.Text && std::strcmp(O.Text, "") == 0);
     CHECK(O.File && std::strcmp(O.File, "-mesh.msh") == 0);
   };
  RunCase("empty and single-dash string values are preserved", Empty);

  RunSpec Equals;
  Equals.Args = {"--Text", "key=value"};
  Equals.Checks = [](Options &O, const std::vector<char *> &)
   {
     CHECK(O.Text && std::strcmp(O.Text, "key=value") == 0);
   };
  RunCase("an equals sign inside a value is preserved", Equals);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestStringOverload()
{
  RunSpec Spec;
  Spec.UseStringOverload = true;
  Spec.StringArgs = "--Omega 2.5 --Integer 7 --Flag";
  Spec.Checks = [](Options &O, const std::vector<char *> &)
   {
     CHECK(O.Omega == 2.5);
     CHECK(O.Integer == 7);
     CHECK(O.Flag);
   };
  RunCase("the ProcessOptions(const char *) overload still works", Spec);
}

} // namespace

int main()
{
  TestBasicTypes();
  TestCaseInsensitiveOptions();
  TestRepeatedOptions();
  TestStdinOptions();
  TestNumericParsing();
  TestArityErrors();
  TestUnknownOptions();
  TestHelp();
  TestZeroArgs();
  TestAbortOnUnknownFalse();
  TestStringValues();
  TestStringOverload();

  std::printf("%i checks, %i failures\n", NumChecks, NumFailures);
  return NumFailures == 0 ? 0 : 1;
}
