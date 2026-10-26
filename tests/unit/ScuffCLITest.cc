/*
 * Tests for the typed CLI11 helpers in libhrutil/ScuffCLI.
 *
 * The legacy stdin option-file format is process-wide state, so cases that
 * exercise it run in a forked child with a private stdin pipe.  Numeric and
 * ordering cases can then use the same public entry point as the migrated
 * applications.
 */

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <complex>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <CLI11.hpp>

#include "ScuffCLI.h"

namespace {

using Args = std::vector<std::string>;

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

void CheckStringEqual(const std::string &Actual, const std::string &Expected,
                      const char *File, int Line)
{
  ++NumChecks;
  if (Actual != Expected)
   { ++NumFailures;
     std::fprintf(stderr, "FAIL %s:%i: expected \"%s\", got \"%s\"\n",
                  File, Line, Expected.c_str(), Actual.c_str());
   }
}

#define CHECK(Condition) Check((Condition), #Condition, __FILE__, __LINE__)
#define CHECK_STRING(Actual, Expected) \
  CheckStringEqual((Actual), (Expected), __FILE__, __LINE__)

/***************************************************************/
/* A small application mirroring the options used by the first */
/* typed migration.                                            */
/***************************************************************/
struct ProbeOptions
{
  std::string Geometry;
  int LMax = 3;
  std::complex<double> Omega{0.0, 0.0};
  std::string OmegaFile;
  std::string Cache;
  std::string FileBase;
  bool WriteHDF5Files = false;
};

void AddProbeOptions(CLI::App &App, ProbeOptions &Options)
{
  App.option_defaults()->ignore_case();

  App.add_option("--geometry", Options.Geometry, "geometry")
     ->required()
     ->take_last();
  App.add_option("--LMax", Options.LMax, "maximum l-value")
     ->take_last();
  scuff::cli::AddComplexOption(App, "--omega", Options.Omega,
                               "angular frequency");
  App.add_option("--OmegaFile", Options.OmegaFile, "frequency file")
     ->take_last();
  App.add_option("--Cache", Options.Cache, "cache file")
     ->take_last();
  App.add_option("--FileBase", Options.FileBase, "output base")
     ->take_last();
  App.add_flag("--WriteHDF5Files", Options.WriteHDF5Files, "HDF5 output");
}

/***************************************************************/
/* Run Function in a child with StdinText (or /dev/null) on    */
/* stdin.  The child's failure count becomes its exit status.  */
/***************************************************************/
int RunChild(const char *StdinText, void (*Function)())
{
  int PipeFD[2] = {-1, -1};
  if (StdinText && pipe(PipeFD) != 0)
   return -1;

  pid_t Child = fork();
  if (Child < 0)
   return -1;

  if (Child == 0)
   {
     if (StdinText)
      { close(PipeFD[1]);
        dup2(PipeFD[0], STDIN_FILENO);
        close(PipeFD[0]);
      }
     else
      { int NullFD = open("/dev/null", O_RDONLY);
        if (NullFD < 0)
         _exit(1);
        dup2(NullFD, STDIN_FILENO);
        close(NullFD);
      }

     Function();
     std::fflush(stdout);
     std::fflush(stderr);
     _exit(NumFailures == 0 ? 0 : 1);
   }

  if (StdinText)
   { close(PipeFD[0]);
     const char *p = StdinText;
     size_t Remaining = std::strlen(StdinText);
     while (Remaining > 0)
      { ssize_t Written = write(PipeFD[1], p, Remaining);
        if (Written <= 0)
         break;
        p += Written;
        Remaining -= static_cast<size_t>(Written);
      }
     close(PipeFD[1]);
   }

  int Status = 0;
  if (waitpid(Child, &Status, 0) != Child)
   return -1;

  return WIFEXITED(Status) ? WEXITSTATUS(Status) : -1;
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestArgvValues()
{
  ProbeOptions Options;
  CLI::App App{"probe"};
  AddProbeOptions(App, Options);

  char Arg0[] = "probe";
  char Arg1[] = "--GEOMETRY";
  char Arg2[] = "sphere.scuffgeo";
  char Arg3[] = "--lmax";
  char Arg4[] = "-2";
  char Arg5[] = "--OMEGA";
  char Arg6[] = "-2.5";
  char Arg7[] = "--cache";
  char Arg8[] = "-cache.dat";
  char Arg9[] = "--writehdf5files";
  char *Argv[] = {Arg0, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6,
                  Arg7, Arg8, Arg9, nullptr};

  std::optional<int> Result = scuff::cli::Parse(App, 10, Argv);
  CHECK(!Result.has_value());
  CHECK_STRING(Options.Geometry, "sphere.scuffgeo");
  CHECK(Options.LMax == -2);
  CHECK(Options.Omega == std::complex<double>(-2.5, 0.0));
  CHECK_STRING(Options.Cache, "-cache.dat");
  CHECK(Options.WriteHDF5Files);

  CHECK_STRING(App.get_name(), "probe");
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestComplexSyntax()
{
  ProbeOptions Options;
  CLI::App App{"probe"};
  AddProbeOptions(App, Options);

  char Arg0[] = "probe";
  char Arg1[] = "--geometry";
  char Arg2[] = "sphere.scuffgeo";
  char Arg3[] = "--omega";
  char Arg4[] = "1e-3-4e-2I";
  char *Argv[] = {Arg0, Arg1, Arg2, Arg3, Arg4, nullptr};

  std::optional<int> Result = scuff::cli::Parse(App, 5, Argv);
  CHECK(!Result.has_value());
  CHECK(Options.Omega == std::complex<double>(1.0e-3, -4.0e-2));
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestStdinThenArgv()
{
  ProbeOptions Options;
  CLI::App App{"probe"};
  AddProbeOptions(App, Options);

  char Arg0[] = "probe";
  char Arg1[] = "--geometry";
  char Arg2[] = "from-argv.scuffgeo";
  char Arg3[] = "--omega";
  char Arg4[] = "3.5";
  char *Argv[] = {Arg0, Arg1, Arg2, Arg3, Arg4, nullptr};

  std::optional<int> Result = scuff::cli::Parse(App, 5, Argv);
  CHECK(!Result.has_value());
  CHECK_STRING(Options.Geometry, "from-argv.scuffgeo");
  CHECK(Options.Omega == std::complex<double>(3.5, 0.0));
  CHECK(Options.WriteHDF5Files);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestStdinOnly()
{
  ProbeOptions Options;
  CLI::App App{"probe"};
  AddProbeOptions(App, Options);

  char Arg0[] = "probe";
  char *Argv[] = {Arg0, nullptr};

  std::optional<int> Result = scuff::cli::Parse(App, 1, Argv);
  CHECK(!Result.has_value());
  CHECK_STRING(Options.Geometry, "from-stdin.scuffgeo");
  CHECK(Options.LMax == 4);
  CHECK(Options.Omega == std::complex<double>(1.0, 2.0));
  CHECK_STRING(Options.OmegaFile, "frequencies");
  CHECK(Options.WriteHDF5Files);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestParseErrors()
{
  {
    ProbeOptions Options;
    CLI::App App{"probe"};
    AddProbeOptions(App, Options);

    char Arg0[] = "probe";
    char Arg1[] = "--geometry";
    char Arg2[] = "sphere.scuffgeo";
    char Arg3[] = "--LMax";
    char Arg4[] = "not-a-number";
    char *Argv[] = {Arg0, Arg1, Arg2, Arg3, Arg4, nullptr};

    std::optional<int> Result = scuff::cli::Parse(App, 5, Argv);
    CHECK(Result.has_value());
    CHECK(*Result == 1);
  }

  {
    ProbeOptions Options;
    CLI::App App{"probe"};
    AddProbeOptions(App, Options);

    char Arg0[] = "probe";
    char *Argv[] = {Arg0, nullptr};

    /* --geometry is required and no stdin value is supplied. */
    std::optional<int> Result = scuff::cli::Parse(App, 1, Argv);
    CHECK(Result.has_value());
    CHECK(*Result == 1);
  }

  {
    ProbeOptions Options;
    CLI::App App{"probe"};
    AddProbeOptions(App, Options);

    char Arg0[] = "probe";
    char Arg1[] = "--geometry";
    char Arg2[] = "sphere.scuffgeo";
    char Arg3[] = "--bogus";
    char *Argv[] = {Arg0, Arg1, Arg2, Arg3, nullptr};

    std::optional<int> Result = scuff::cli::Parse(App, 4, Argv);
    CHECK(Result.has_value());
    CHECK(*Result == 1);
  }

  {
    ProbeOptions Options;
    CLI::App App{"probe"};
    AddProbeOptions(App, Options);

    char Arg0[] = "probe";
    char Arg1[] = "--geometry";
    char Arg2[] = "sphere.scuffgeo";
    char Arg3[] = "--omega";
    char Arg4[] = "1+2x";
    char *Argv[] = {Arg0, Arg1, Arg2, Arg3, Arg4, nullptr};

    std::optional<int> Result = scuff::cli::Parse(App, 5, Argv);
    CHECK(Result.has_value());
    CHECK(*Result == 1);
  }
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestHelp()
{
  ProbeOptions Options;
  CLI::App App{"probe"};
  AddProbeOptions(App, Options);

  char Arg0[] = "probe";
  char Arg1[] = "--help";
  char *Argv[] = {Arg0, Arg1, nullptr};

  std::optional<int> Result = scuff::cli::Parse(App, 2, Argv);
  CHECK(Result.has_value());
  CHECK(*Result == 0);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void TestUnknownStdinOption()
{
  ProbeOptions Options;
  CLI::App App{"probe"};
  AddProbeOptions(App, Options);

  char Arg0[] = "probe";
  char *Argv[] = {Arg0, nullptr};

  std::optional<int> Result = scuff::cli::Parse(App, 1, Argv);
  CHECK(Result.has_value());
  CHECK(*Result == 1);
}

} // namespace

int main()
{
  const int StdinArgv = RunChild(nullptr, TestArgvValues);
  CHECK(StdinArgv == 0);

  const int StdinComplex = RunChild(nullptr, TestComplexSyntax);
  CHECK(StdinComplex == 0);

  const int StdinThenArgv = RunChild(
    "# comment line\n"
    "geometry from-stdin.scuffgeo\n"
    "\n"
    "omega 1+2I\n"
    "writehdf5files\n",
    TestStdinThenArgv);
  CHECK(StdinThenArgv == 0);

  const int StdinOnly = RunChild(
    "geometry from-stdin.scuffgeo\n"
    "lmax 4\n"
    "omega 1+2I\n"
    "omegafile frequencies\n"
    "writehdf5files\n",
    TestStdinOnly);
  CHECK(StdinOnly == 0);

  const int StdinErrors = RunChild(nullptr, TestParseErrors);
  CHECK(StdinErrors == 0);

  const int StdinHelp = RunChild(nullptr, TestHelp);
  CHECK(StdinHelp == 0);

  const int StdinUnknown = RunChild("bogus 1\n", TestUnknownStdinOption);
  CHECK(StdinUnknown == 0);

  std::printf("%i checks, %i failures\n", NumChecks, NumFailures);
  return NumFailures == 0 ? 0 : 1;
}
