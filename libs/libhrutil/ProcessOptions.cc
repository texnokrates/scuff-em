/* Copyright (C) 2005-2011 M. T. Homer Reid
 *
 * This file is part of SCUFF-EM.
 *
 * SCUFF-EM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SCUFF-EM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * ProcessOptions.cc -- libhrutil facility for processing options.
 *
 * This is a CLI11-backed compatibility adapter.  The public OptStruct API
 * and the legacy command-line contract are preserved:
 *
 *  (1) options may also be read from stdin as "Name arg..." lines;
 *  (2) options may be repeated, subject to MaxInstances;
 *  (3) options may take multiple arguments.
 *
 * The adapter translates the legacy stdin/argv syntax into a CLI11 token
 * vector, lets CLI11 perform option recognition and arity validation, and
 * then stores the parsed values into the legacy OptStruct storage.  Keeping
 * the translation layer separate is what allows CLI11 to provide
 * case-insensitive option matching and generated help while still honoring
 * the old rule that a token beginning with "--" starts a new option.
 *
 * homer reid        -- 1/2012
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <CLI11.hpp>

#include "libhrutil.h"

#define MAXTOK 50
#define MAXSTR 1000

/***************************************************************/
/***************************************************************/
/***************************************************************/
static char *ExtraOSUsageMessage=0;
void AppendOSUsageMessage(const char *Message)
{
  ExtraOSUsageMessage=vstrappend(ExtraOSUsageMessage, Message);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void printOSUsage(const char *ProgName, const char *version, OptStruct *OSArray, const char *format, va_list ap)
{
  char buffer[MAXSTR];

  if (version)
    fprintf(stderr, "%s %s\n", ProgName, version);

  if (format)
   {
     vsnprintfEC(buffer,MAXSTR,format,ap);
     fprintf(stderr,"\nerror: %s (aborting)\n\n",buffer);
   };

  fprintf(stderr,"usage: %s [options]\n\n",ProgName);
  fprintf(stderr," options: \n\n");

  int na;
  OptStruct *OS;
  for(OS=OSArray; OS->Name!=0; OS++)
   { 
     snprintf(buffer,sizeof(buffer),"--%s ",OS->Name);
     if (OS->Type!=PA_BOOL)
      for(na=0; na<OS->NumArgs; na++)
       strncat(buffer,"xx ",sizeof(buffer)-strlen(buffer)-1);
     fprintf(stderr,"  %-20s",buffer);

     if ( OS->Description )
      fprintf(stderr,"(%s)",OS->Description);

     if (OS->NumArgs==1)
      { switch(OS->Type)
         { case PA_INT:
             fprintf(stderr,"(%i)",*((int *)OS->Storage));
             break;

           case PA_DOUBLE:
             fprintf(stderr,"(%g)",*((double *)OS->Storage));
             break;

           case PA_CDOUBLE:
             fprintf(stderr,"(%s)",z2s(*((cdouble *)OS->Storage)));
             break;

           case PA_STRING: 
             if ( *(char **)OS->Storage )
              fprintf(stderr,"(%s)",*(char **)OS->Storage);
             break;
         }; 
       };

     fprintf(stderr,"\n");
   };

  fprintf(stderr,"\n");
  if (ExtraOSUsageMessage)
   fprintf(stderr,"%s\n",ExtraOSUsageMessage);
  

}

void OSUsage(const char *ProgName, const char *version, OptStruct *OSArray, const char *format, ...)
{
  va_list ap;
  if(format) 
    va_start(ap, format);
  printOSUsage(ProgName, version, OSArray, format, ap);
  if(format)
    va_end(ap);
  exit(1);
}

void OSUsage(const char *ProgName, OptStruct *OSArray, const char *format, ...)
{
  va_list ap;
  if(format) 
    va_start(ap, format);
  printOSUsage(ProgName, NULL, OSArray, format, ap);
  if(format)
    va_end(ap);
  exit(1);
}

/***************************************************************/
/* internal adapter state                                      */
/***************************************************************/
namespace {

struct OptionOccurrence
{
  std::vector<std::string> Args;
  std::string Source;      // "stdin:<line>" for stdin options, empty for argv
  int ArgvStart = -1;      // index of the option token in argv
  int ArgvEnd = -1;        // one past the last consumed argv token
};

struct ParseInput
{
  std::vector<OptionOccurrence> Occurrences;
  std::vector<std::vector<size_t>> OccurrencesByOption;
  std::vector<std::string> Tokens;
};

size_t FindOption(OptStruct *OSArray, const std::string &Name)
{
  for(size_t n=0; OSArray[n].Name!=0; n++)
   if ( StrCaseCmp(Name.c_str(), OSArray[n].Name)==0 )
    return n;

  return static_cast<size_t>(-1);
}

std::string OptionHeader(const OptStruct &OS)
{
  return std::string("--") + OS.Name;
}

std::string PrefixedMessage(const std::string &Source, const std::string &Message)
{
  if (Source.empty())
   return Message;
  return Source + ": " + Message;
}

/***************************************************************/
/* Validate one occurrence against the declared arity.  The   */
/* legacy parser ignores the NumArgs field for boolean flags  */
/* and rejects any value supplied to a flag.                  */
/***************************************************************/
bool ValidateOccurrence(const OptStruct &OS, const std::vector<std::string> &Args,
                        const std::string &Source, std::string &Message)
{
  if (OS.Type==PA_BOOL)
   {
     if ( !Args.empty() )
      { Message=PrefixedMessage(Source,
          "boolean option " + OptionHeader(OS) + " takes no arguments");
        return false;
      };
     return true;
   };

  if ( static_cast<int>(Args.size()) != OS.NumArgs )
   { Message=PrefixedMessage(Source,
       "option " + OptionHeader(OS) + " needs "
       + std::to_string(OS.NumArgs) + " arguments ("
       + std::to_string(Args.size()) + " provided)");
     return false;
   };

  return true;
}

/***************************************************************/
/* Append an occurrence to the normalized CLI11 token vector.  */
/* The first argument is attached with '=' so that values      */
/* beginning with '-' are unambiguously values rather than     */
/* new options.  An empty first value cannot use '=' (CLI11    */
/* treats --Opt= as missing), so it is passed as a separate    */
/* empty token instead.                                        */
/***************************************************************/
void AppendOccurrence(ParseInput &Input, OptStruct *OSArray,
                      size_t OptionIndex, std::vector<std::string> Args,
                      const std::string &Source, int ArgvStart, int ArgvEnd)
{
  OptStruct &OS = OSArray[OptionIndex];
  std::string Token = OptionHeader(OS);

  if ( !Args.empty() )
   { if ( Args[0].empty() )
      { Input.Tokens.push_back(Token);
        Input.Tokens.push_back(std::string());
      }
     else
      Input.Tokens.push_back(Token + "=" + Args[0]);

     for(size_t na=1; na<Args.size(); na++)
      Input.Tokens.push_back(Args[na]);
   }
  else
   Input.Tokens.push_back(Token);

  OptionOccurrence Occurrence;
  Occurrence.Args      = std::move(Args);
  Occurrence.Source    = Source;
  Occurrence.ArgvStart = ArgvStart;
  Occurrence.ArgvEnd   = ArgvEnd;

  size_t OccurrenceIndex = Input.Occurrences.size();
  Input.Occurrences.push_back(std::move(Occurrence));

  if (OptionIndex>=Input.OccurrencesByOption.size())
   Input.OccurrencesByOption.resize(OptionIndex+1);
  Input.OccurrencesByOption[OptionIndex].push_back(OccurrenceIndex);
}

/***************************************************************/
/* Read the legacy stdin option format.                        */
/*                                                             */
/* The legacy reader always aborted on malformed stdin input,  */
/* regardless of AbortOnUnknown, and the adapter preserves     */
/* that behavior: stdin files are explicit user data, not a    */
/* stream that may contain options belonging to another        */
/* parser.                                                     */
/***************************************************************/
void ReadStdinOptions(const char *ProgName, OptStruct *OSArray,
                      ParseInput &Input)
{
  if ( isatty(fileno(stdin)) )
   return;

  int LineNum=0;
  char Line[MAXSTR];
  while( fgets(Line,sizeof(Line),stdin) )
   {
     LineNum++;

     char *Tokens[MAXTOK];
     bool Truncated=false;
     int NumTokens=Tokenize(Line,Tokens,MAXTOK," \t\n",&Truncated);

     if (Truncated)
      { char Message[MAXSTR+64];
        snprintf(Message,sizeof(Message),
                 "stdin:%i: too many tokens (maximum %i)",LineNum,MAXTOK);
        OSUsage(ProgName,OSArray,"%s",Message);
      };

     if (NumTokens==0 || Tokens[0][0]=='#')
      continue;

     std::string Name=Tokens[0];
     if ( Name.rfind("--",0)==0 )
      Name=Name.substr(2);

     std::vector<std::string> Args;
     for(int nt=1; nt<NumTokens; nt++)
      Args.emplace_back(Tokens[nt]);

     size_t OptionIndex=FindOption(OSArray,Name);
     std::string Source="stdin:" + std::to_string(LineNum);

     if (OptionIndex==static_cast<size_t>(-1))
      { std::string Message=Source + ": unknown option --" + Name;
        OSUsage(ProgName,OSArray,"%s",Message.c_str());
      };

     std::string Message;
     if ( !ValidateOccurrence(OSArray[OptionIndex],Args,Source,Message) )
      OSUsage(ProgName,OSArray,"%s",Message.c_str());

     AppendOccurrence(Input,OSArray,OptionIndex,std::move(Args),Source,-1,-1);
   }
}

/***************************************************************/
/* Translate argv into occurrences.  A token beginning with    */
/* "--" starts a new option; everything else is an argument of */
/* the current option.  This is the legacy boundary rule.      */
/***************************************************************/
void ReadArgvOptions(int argc, char *argv[], const char *ProgName,
                     OptStruct *OSArray, bool AbortOnUnknown, ParseInput &Input)
{
  int narg=1;
  while( narg<argc )
   {
     /* allow earlier argument handlers to mark slots as consumed */
     if ( argv[narg]==0 )
      { narg++;
        continue;
      };

     std::string Token=argv[narg];

     /* A top-level help request is new functionality, but it must not
        prevent the legacy options from being validated first. */
     if ( Token=="-h" || Token=="--help"
       || (Token.rfind("--",0)==0 && StrCaseCmp(Token.c_str()+2,"help")==0) )
      { Input.Tokens.push_back("--help");
        return;
      };

     if ( Token.rfind("--",0)!=0 )
      { if (AbortOnUnknown)
         OSUsage(ProgName,OSArray,"unknown option %s",argv[narg]);
        narg++;
        continue;
      };

     std::string Name=Token.substr(2);
     if ( Name.empty() )
      { if (AbortOnUnknown)
         OSUsage(ProgName,OSArray,"unknown option %s",argv[narg]);
        narg++;
        continue;
      };

     std::vector<std::string> Args;
     int next=narg+1;
     while( next<argc && argv[next]!=0 && strncmp(argv[next],"--",2)!=0 )
      { Args.emplace_back(argv[next]);
        next++;
      };

     size_t OptionIndex=FindOption(OSArray,Name);
     if (OptionIndex==static_cast<size_t>(-1))
      { if (AbortOnUnknown)
         OSUsage(ProgName,OSArray,"unknown option --%s",Name.c_str());
        narg=next;
        continue;
      };

     std::string Message;
     if ( !ValidateOccurrence(OSArray[OptionIndex],Args,std::string(),Message) )
      { if (AbortOnUnknown)
         OSUsage(ProgName,OSArray,"%s",Message.c_str());
        narg=next;
        continue;
      };

     AppendOccurrence(Input,OSArray,OptionIndex,std::move(Args),
                      std::string(),narg,next);
     narg=next;
   };
}

/***************************************************************/
/* Strict numeric conversion.  Trailing garbage is rejected;   */
/* this is the deliberate tightening described in the C++23    */
/* migration plan.                                             */
/***************************************************************/
bool ParseInt(const std::string &Arg, int *Value)
{
  if ( Arg.empty() )
   return false;

  errno=0;
  char *End=0;
  long LongValue=strtol(Arg.c_str(),&End,0);
  if (End==Arg.c_str())
   return false;
  while(isspace((unsigned char)*End))
   End++;
  if (*End!=0 || LongValue<INT_MIN || LongValue>INT_MAX)
   return false;

  *Value=static_cast<int>(LongValue);
  return true;
}

bool ParseDouble(const std::string &Arg, double *Value)
{
  if ( Arg.empty() )
   return false;

  errno=0;
  char *End=0;
  double DoubleValue=strtod(Arg.c_str(),&End);
  if (End==Arg.c_str())
   return false;
  while(isspace((unsigned char)*End))
   End++;
  if (*End!=0)
   return false;

  *Value=DoubleValue;
  return true;
}

bool StoreValue(const OptStruct &OS, int Instance, int ArgNumber,
                const std::string &Arg)
{
  size_t Index=static_cast<size_t>(Instance)*OS.NumArgs + ArgNumber;

  switch(OS.Type)
   {
     case PA_STRING:
       static_cast<char **>(OS.Storage)[Index]=strdupEC(Arg.c_str());
       return true;

     case PA_INT:
       return ParseInt(Arg, static_cast<int *>(OS.Storage) + Index);

     case PA_DOUBLE:
       return ParseDouble(Arg, static_cast<double *>(OS.Storage) + Index);

     case PA_CDOUBLE:
       return S2CD(Arg.c_str(), static_cast<cdouble *>(OS.Storage) + Index)==0;

     default:
       ErrExit("%s:%i: internal error",__FILE__,__LINE__);
   };

  return false;
}

/***************************************************************/
/* Apply the legacy NumInstances / MaxInstances bookkeeping.   */
/* Returns the storage instance index for this occurrence.     */
/***************************************************************/
int GetInstanceIndex(OptStruct &OS, const OptionOccurrence &Occurrence,
                     const char *ProgName, OptStruct *OSArray)
{
  if (OS.NumInstances==0)
   return 0;

  int &Count=*OS.NumInstances;

  if ( OS.MaxInstances==1 && Count==1 )
   {
     if ( Occurrence.Args.empty() )
      Warn("option --%s was unnecessarily specified multiple times",OS.Name);
     else if ( Occurrence.Args.size()==1 )
      Warn("new value (%s) for option --%s overrides previously specified value",
           Occurrence.Args[0].c_str(),OS.Name);
     else
      Warn("new value (%s...) for option --%s overrides previously specified value",
           Occurrence.Args[0].c_str(),OS.Name);
     return 0;
   };

  if ( OS.MaxInstances>1 && Count==OS.MaxInstances )
   { std::string Message="option " + OptionHeader(OS) + " specified too many times";
     OSUsage(ProgName,OSArray,"%s",Message.c_str());
   };

  return Count++;
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void StoreOptions(char *argv[], OptStruct *OSArray,
                  ParseInput &Input, bool ZeroArgs, bool AbortOnUnknown)
{
  const char *ProgName=argv[0];
  static const std::vector<size_t> NoOccurrences;

  for(size_t no=0; OSArray[no].Name!=0; no++)
   {
     OptStruct &OS=OSArray[no];
     const std::vector<size_t> &OccurrenceIndices =
       (no<Input.OccurrencesByOption.size())
        ? Input.OccurrencesByOption[no]
        : NoOccurrences;

     if (OS.Type==PA_BOOL)
      {
        for(size_t k=0; k<OccurrenceIndices.size(); k++)
         { const OptionOccurrence &Occurrence=Input.Occurrences[OccurrenceIndices[k]];
           int Instance=GetInstanceIndex(OS,Occurrence,ProgName,OSArray);
           if (Instance>1)
            fprintf(stderr,"** warning: boolean option --%s specified more than once",
                    OS.Name);
           *static_cast<bool *>(OS.Storage)=true;
           if (ZeroArgs && Occurrence.ArgvStart>=0)
            for(int n=Occurrence.ArgvStart; n<Occurrence.ArgvEnd; n++)
             argv[n]=0;
         };
        continue;
      };

     /* The occurrence count, not the flat value count, determines how
        many times an option was supplied. */
     for(size_t k=0; k<OccurrenceIndices.size(); k++)
      { const OptionOccurrence &Occurrence=Input.Occurrences[OccurrenceIndices[k]];
        int Instance=GetInstanceIndex(OS,Occurrence,ProgName,OSArray);
        bool Stored=true;

        for(int na=0; na<OS.NumArgs; na++)
         { const std::string &Arg=Occurrence.Args[na];
           if ( !StoreValue(OS,Instance,na,Arg) )
            { std::string Message=PrefixedMessage(Occurrence.Source,
                "invalid argument (" + Arg + ") passed to option --" + OS.Name);
              if (AbortOnUnknown)
               OSUsage(ProgName,OSArray,"%s",Message.c_str());
              Stored=false;
              break;
            };
         };

        if (Stored && ZeroArgs && Occurrence.ArgvStart>=0)
         for(int n=Occurrence.ArgvStart; n<Occurrence.ArgvEnd; n++)
          argv[n]=0;
      };
   };
}

/***************************************************************/
/* Build the CLI11 application that recognizes the declared    */
/* options.  Values are collected as strings; the adapter      */
/* performs the legacy type conversions after parsing.         */
/***************************************************************/
bool ParseWithCLI11(const char *ProgName, OptStruct *OSArray,
                    ParseInput &Input, bool AbortOnUnknown)
{
  size_t NumOptions=0;
  while(OSArray[NumOptions].Name!=0)
   NumOptions++;

  std::vector<std::vector<std::string>> StringValues(NumOptions);
  std::vector<std::vector<int>> FlagValues(NumOptions);
  std::vector<CLI::Option *> Options(NumOptions,0);

  CLI::App App;
  App.name(ProgName);
  App.option_defaults()->ignore_case();
  App.set_help_flag("--help","print this help message and exit")->ignore_case();

  /* CLI11 treats a std::vector target as unbounded by default, which both
     suppresses arity information in --help and makes each occurrence look
     like it accepts an arbitrary number of values.  A generous finite cap
     keeps repeated occurrences working while letting the generated help
     show the declared arity. */
  constexpr int MaxOptionOccurrences=1000000;

  for(size_t no=0; no<NumOptions; no++)
   {
     OptStruct &OS=OSArray[no];
     std::string Name=OptionHeader(OS);
     std::string Description=OS.Description ? OS.Description : "";

     if (OS.Type==PA_BOOL)
      Options[no]=App.add_flag(Name,FlagValues[no],Description);
     else
      { Options[no]=App.add_option(Name,StringValues[no],Description);
        Options[no]->type_size(OS.NumArgs);
        Options[no]->expected(1,MaxOptionOccurrences);

        const char *TypeName;
        switch(OS.Type)
         { case PA_INT:     TypeName="INT";     break;
           case PA_DOUBLE:  TypeName="FLOAT";   break;
           case PA_CDOUBLE: TypeName="COMPLEX"; break;
           default:         TypeName="TEXT";    break;
         };

        std::string Placeholder=TypeName;
        if (OS.NumArgs>1)
         Placeholder += " x " + std::to_string(OS.NumArgs);
        Options[no]->type_name(Placeholder);
      };
     Options[no]->ignore_case();
   };

  try
   {
     /* CLI11's vector parser consumes tokens from the back, just as its
        argc/argv parser does after reversing argv.  Reverse the normalized
        stream so that options are seen in their original order. */
     std::vector<std::string> ParseTokens(Input.Tokens.rbegin(),
                                          Input.Tokens.rend());
     App.parse(ParseTokens);
   }
  catch(const CLI::Success &e)
   {
     std::exit(App.exit(e));
   }
  catch(const CLI::ParseError &e)
   {
     if (AbortOnUnknown)
      OSUsage(ProgName,OSArray,"%s",e.what());
     return false;
   };

  /* StringValues is grouped in chunks of NumArgs per occurrence.  Check
     that expectation explicitly so a future CLI11 change cannot silently
     misalign values with occurrences. */
  for(size_t no=0; no<NumOptions; no++)
   {
     OptStruct &OS=OSArray[no];
     size_t NumOccurrences=(no<Input.OccurrencesByOption.size())
                            ? Input.OccurrencesByOption[no].size()
                            : 0;

     if (OS.Type==PA_BOOL)
      { if (FlagValues[no].size()!=NumOccurrences)
         ErrExit("%s:%i: internal error (option --%s: %zu values for %zu occurrences)",
                 __FILE__,__LINE__,OS.Name,FlagValues[no].size(),NumOccurrences);
        continue;
      };

     if (StringValues[no].size()!=NumOccurrences*static_cast<size_t>(OS.NumArgs))
      ErrExit("%s:%i: internal error (option --%s: %zu values for %zu occurrences of %i arguments)",
              __FILE__,__LINE__,OS.Name,StringValues[no].size(),NumOccurrences,
              OS.NumArgs);
   };

  return true;
}

} // namespace

/***************************************************************/
/* public entry point                                          */
/***************************************************************/
void ProcessOptions(int argc, char *argv[], OptStruct *OSArray,
                    bool AbortOnUnknown, bool ZeroArgs)
{
  for(OptStruct *OS=OSArray; OS->Name!=0; OS++)
   if (OS->NumInstances)
    *(OS->NumInstances)=0;

  ParseInput Input;
  Input.OccurrencesByOption.resize(1);

  ReadStdinOptions(argv[0],OSArray,Input);
  ReadArgvOptions(argc,argv,argv[0],OSArray,AbortOnUnknown,Input);

  if ( ParseWithCLI11(argv[0],OSArray,Input,AbortOnUnknown) )
   StoreOptions(argv,OSArray,Input,ZeroArgs,AbortOnUnknown);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void ProcessOptions(const char *ArgString, OptStruct *OSArray)
{
  char *ArgStringCopy=strdup(ArgString ? ArgString : "");
  char *Tokens[100];
  bool Truncated=false;
  int NumTokens=Tokenize(ArgStringCopy,Tokens,99," \t\n",&Truncated);
  if (Truncated)
   ErrExit("too many options in option string");

  /* The argc/argv overload expects a program name in argv[0]; synthesize
     one so that the first option in the string is not skipped. */
  char *Argv[101];
  Argv[0]=const_cast<char *>("ProcessOptions");
  for(int n=0; n<NumTokens; n++)
   Argv[n+1]=Tokens[n];
  Argv[NumTokens+1]=0;

  ProcessOptions(NumTokens+1,Argv,OSArray);

  free(ArgStringCopy);

}
