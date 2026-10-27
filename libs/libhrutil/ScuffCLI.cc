/*
 * ScuffCLI.cc -- typed CLI11 support shared by migrated applications.
 *
 * The legacy option-file format is line oriented:
 *
 *     name value1 value2 ...
 *
 * with comments introduced by '#' as the first token.  CLI11 does not read
 * this format directly, so the adapter translates each line to
 * "--name=value1 value2 ..." and feeds the combined token stream to CLI11.
 * Stdin options are applied before argv options, matching ProcessOptions.
 */

#include <stdio.h>
#include <unistd.h>

#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "libhrutil.h"
#include "ScuffCLI.h"

namespace scuff {
namespace cli {

namespace {

constexpr std::size_t MAXTOK = 50;

/***************************************************************/
/* Append translated stdin option lines to Tokens.  The legacy */
/* reader aborts on malformed input, so this helper does too.  */
/***************************************************************/
void AppendStdinTokens(std::vector<std::string> &Tokens)
{
  if ( isatty(fileno(stdin)) )
   return;

  std::string Line;
  int LineNum=0;

  while( std::getline(std::cin, Line) )
   {
     LineNum++;

     std::vector<std::string> Fields;
     std::istringstream Stream(Line);
     std::string Field;
     while(Stream >> Field)
      { Fields.push_back(Field);
        if (Fields.size()>MAXTOK)
         ErrExit("stdin:%i: too many tokens (maximum %zu)",LineNum,MAXTOK);
      };

     if ( Fields.empty() || Fields[0][0]=='#' )
      continue;

     std::string Name=Fields[0];
     if ( Name.rfind("--",0)==0 )
      Name=Name.substr(2);

     std::string Token="--"+Name;
     if ( Fields.size()==1 )
      Tokens.push_back(Token);
     else
      { /* Attach the first value so that a leading '-' cannot be mistaken
           for the start of another option.  Empty values cannot use this
           form, but an option-file field is never empty. */
        Tokens.push_back(Token + "=" + Fields[1]);
        for(std::size_t n=2; n<Fields.size(); n++)
         Tokens.push_back(Fields[n]);
      };
   };
}

} // namespace

/***************************************************************/
/***************************************************************/
/***************************************************************/
std::vector<std::string> CollectArgs(int argc, char *argv[])
{
  std::vector<std::string> Tokens;

  /* Legacy order: stdin first, then argv. */
  AppendStdinTokens(Tokens);
  for(int n=1; n<argc; n++)
   Tokens.emplace_back(argv[n]);

  /* CLI11's vector parser consumes tokens from the back, so reverse the
     normalized stream to make it parse in the order built above. */
  return std::vector<std::string>(Tokens.rbegin(), Tokens.rend());
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
CLI::Option *AddComplexOption(CLI::App &App, const std::string &Name,
                              std::complex<double> &Value,
                              const std::string &Description)
{
  return App.add_option_function<std::string>(
    Name,
    [&Value, Name](const std::string &Text)
    {
      cdouble Parsed;
      if ( S2CD(Text.c_str(), &Parsed)!=0 )
       throw CLI::ValidationError(Name,
                                  "invalid complex value \"" + Text + "\"");
      Value=Parsed;
    },
    Description)
    ->type_name("COMPLEX")
    ->expected(1)
    ->take_last();
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
CLI::Option *AddComplexVectorOption(CLI::App &App, const std::string &Name,
                                    std::vector<std::complex<double>> &Values,
                                    const std::string &Description,
                                    int MaxInstances, int NumArgs)
{
  /* The values arrive in one callback call, in order of appearance.
     A vector-valued callback is required here: a scalar-valued
     callback option runs only once per parse regardless of the
     multi-occurrence policy.  expected() bounds are in units of
     type_size, so (1, MaxInstances) means 1..MaxInstances groups of
     NumArgs values, exactly like the legacy NumArgs/MaxInstances. */
  return App.add_option_function<std::vector<std::string>>(
    Name,
    [&Values, Name](const std::vector<std::string> &Texts)
    {
      for(const std::string &Text : Texts)
       { cdouble Parsed;
         if ( S2CD(Text.c_str(), &Parsed)!=0 )
          throw CLI::ValidationError(Name,
                                     "invalid complex value \"" + Text + "\"");
         Values.push_back(Parsed);
       };
    },
    Description)
    ->type_name("COMPLEX")
    ->type_size(NumArgs)
    ->expected(1, MaxInstances)
    ->take_all();
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
std::optional<int> Parse(CLI::App &App, int argc, char *argv[])
{
  if ( argc>0 && App.get_name().empty() )
   App.name(argv[0]);

  std::vector<std::string> Args=CollectArgs(argc,argv);
  try
   {
     App.parse(Args);
   }
  catch(const CLI::ParseError &e)
   {
     int Status=App.exit(e);
     return Status==0 ? 0 : 1;
   };

  return std::nullopt;
}

} // namespace cli
} // namespace scuff
