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
 * libhrutil.cc -- library of general-purpose utility routines 
 *
 * homer reid   -- 10/2008
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <process.h>
#else
#  include <sys/resource.h>
#  include <sys/times.h>
#  include <fenv.h>
#endif

#ifdef HAVE_PTHREAD
#  include <pthread.h>
#endif 

#include <math.h>
#include <string>

#include "libhrutil.h"

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_EXECINFO_H
   #include <execinfo.h>
#endif

#ifdef USE_OPENMP
#  include <omp.h>
#endif

#define MAXSTR 1000

/***************************************************************/
/* Timing functions ********************************************/
/***************************************************************/
static double tictoc=0.0;
static unsigned long tictocMem=0;
double Secs()
{ 
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (double)(tv.tv_sec) + 1.0e-6*((double)(tv.tv_usec));
}
void Tic(bool MeasureBytesAllocated)
 { tictoc=Secs(); 
   if (MeasureBytesAllocated) tictocMem=GetMemoryUsage();
 }
double Toc(unsigned long *BytesAllocated)
 { if (BytesAllocated)
    *BytesAllocated=GetMemoryUsage() - tictocMem;
   return Secs()-tictoc; 
 }

/***************************************************************/
/* String functions *********************************************/
/*                                                              */
/* These helpers retain the historical C-style interface, but   */
/* copy into thread-local buffers with explicit bounds and NUL  */
/* termination.  New code should prefer std::string /            */
/* std::filesystem rather than relying on the static lifetime.  */
/***************************************************************/
/* given "/home/homer/work/MyFile.dat", return "MyFile.dat" */
char *RemoveDirectories(const char *s)
{ static thread_local char buffer[MAXSTR];
  const char *p = nullptr;

  if (!s)
   { buffer[0]=0;
     return buffer;
   };

  p=strrchr(s,'/');
  snprintf(buffer,sizeof(buffer),"%s", p ? p+1 : s);

  return buffer;
}

/* given "/home/homer/work/MyFile.dat", return "/home/homer/work/MyFile" */
char *RemoveExtension(const char *s)
{ static thread_local char buffer[MAXSTR];
  char *p = nullptr, *dot = nullptr;

  if (!s)
   { buffer[0]=0;
     return buffer;
   };

  snprintf(buffer,sizeof(buffer),"%s",s);

  /* only consider dots in the final path component */
  p=strrchr(buffer,'/');
  dot=strrchr(p ? p+1 : buffer,'.');
  if (dot)
   *dot=0;

  return buffer;

}

/* given "/home/homer/work/MyFile.dat", return "dat" */
char *GetFileExtension(const char *s)
{
  static thread_local char buffer[MAXSTR];
  const char *base = nullptr, *dot = nullptr;

  if (!s)
   { buffer[0]=0;
     return buffer;
   };

  base=strrchr(s,'/');
  base=base ? base+1 : s;
  dot=strrchr(base,'.');

  snprintf(buffer,sizeof(buffer),"%s", dot ? dot+1 : "");

  return buffer;
}

/* given "/home/homer/work/MyFile.dat", return "MyFile" */
char *GetFileBase(const char *s)
{  
#define NUMBUFS 4
  static thread_local int bufhead=0;
  static thread_local char buffers[NUMBUFS][MAXSTR];
  bufhead = (bufhead+1)%NUMBUFS;
  char *buffer = buffers[bufhead];
  snprintf(buffer,MAXSTR,"%s", s ? s : "");
  char *p = strrchr(buffer,'/');
  char *ret = p ? p+1 : buffer;
  if ( (p=strrchr(ret,'.')) ) *p=0;         // remove file extension
  return ret;
}

/*
 * public domain strtok_r() by Charlie Gordon
 *
 *   from comp.lang.c  9/14/2007
 *
 *      http://groups.google.com/group/comp.lang.c/msg/2ab1ecbb86646684
 *
 *     (Declaration that it's public domain):
 *      http://groups.google.com/group/comp.lang.c/msg/7c7b39328fefab9c
 * 
*/
char* strtok_r_PublicDomain(
    char *str, 
    const char *delim, 
    char **nextp)
{
    char *ret;

    if (str == nullptr)
    {
        str = *nextp;
    }

    str += strspn(str, delim);

    if (*str == '\0')
    {
        return nullptr;
    }

    ret = str;

    str += strcspn(str, delim);

    if (*str)
    {
        *str++ = '\0';
    }

    *nextp = str;

    return ret;
}

// MS uses "strtok_s" but this is not supported by MinGW apparently,
// so just punt and use the non re-entrant version
// #define strtok_r(s,d,p) strtok(s,d)
// update 20150802 no, this seems to cause core dumps on
// window. instead use the public-domain strtok implementation
// included above.
#if defined(_WIN32)
  #define strtok_r(s,d,p) strtok_r_PublicDomain(s,d,p)
#endif

// portable wrapper around StrCaseCmp
int StrCaseCmp(const char *s1, const char *s2)
{
#if defined(_WIN32)
  return _stricmp(s1,s2);
#else
  return strcasecmp(s1,s2);
#endif
}

/* given "Now is the time for all", return Tokens[0]="Now", */
/* Tokens[1]="is", etc.                                     */
/* Note that s is modified on return.                       */
sVec Tokenize(char *s, const char *Separators)
{ 
  sVec Tokens;
  char *saveptr=nullptr, *Token=strtok_r(s, Separators, &saveptr);
  while (Token)
   { Tokens.push_back(Token);
     Token=strtok_r(nullptr, Separators, &saveptr);
   }
  return Tokens;
}

// old calling convention retained for backward compatibility
int Tokenize(char *s, char **Tokens, int MaxTokens, const char *Separators,
             bool *Truncated)
{ 
  if (Truncated) *Truncated=false;

  if (MaxTokens<=0 || Tokens==nullptr)
   { if (Truncated && s && *s) *Truncated=true;
     return 0;
   };

  sVec TokenVec=Tokenize(s,Separators);
  int NumStored=static_cast<int>(TokenVec.size());
  if (NumStored>MaxTokens)
   { NumStored=MaxTokens;
     if (Truncated) *Truncated=true;
   };

  for(int nt=0; nt<NumStored; nt++)
   Tokens[nt]=TokenVec[nt];

  return NumStored;
}

/***************************************************************/
/* Error-checked vsnprintf *************************************/
/***************************************************************/
int vsnprintfEC(char *str, size_t size, const char *format, va_list ap)
{
  int n = vsnprintf(str, size, format, ap);
  if (unsigned(n) >= size)
    ErrExit("string of length %d exceeded maximum length %lu\n",
	    n, (unsigned long) size);
  return n;
}

/***************************************************************/
/* extension of fopen that accepts an optional colon-separated */
/* search path.                                                */
/* If Path is null this reduces to just the usual fopen.       */
/* If WhichDir is non-null, then on return *WhichDir points to */
/* a static buffer containing the directory in which the file  */
/* was found.                                                  */
/***************************************************************/
FILE *fopenPath(const char *Path, const char *FileName,
                const char *Mode, char **WhichDir)
{ 
  static thread_local char DirFound[MAXSTR];
  if (WhichDir) *WhichDir=DirFound;
  DirFound[0]=0;

  if ( FILE *f=fopen(FileName,Mode) )
   { if (strchr(FileName,'/'))
      { snprintf(DirFound,sizeof(DirFound),"%s",FileName);
        char *Slash=strrchr(DirFound,'/');
        if (Slash) *Slash=0;
      }
     else 
      snprintf(DirFound,sizeof(DirFound),"%s",".");
     return f;
   }

  if (!Path || !*Path) return nullptr;

  std::string SearchPath(Path);
  size_t Start=0;
  while( Start <= SearchPath.size() )
   {
     size_t Colon=SearchPath.find(':',Start);
     std::string Dir=SearchPath.substr(Start,
         Colon==std::string::npos ? std::string::npos : Colon-Start);
     std::string FullFileName=Dir + "/" + FileName;
     if ( FILE *f=fopen(FullFileName.c_str(),Mode) )
      { snprintf(DirFound,sizeof(DirFound),"%s",Dir.c_str());
        return f;
      }
     if (Colon==std::string::npos) break;
     Start=Colon+1;
   }
  return nullptr;
}

/***************************************************************/
/* Vararg versions of common functions *************************/
/***************************************************************/
FILE *vfopen(const char *format, const char *mode, ...)
{
  COMPLETE_VARARGS2(format, mode, buffer);
  return fopen(buffer,mode);
}

int vmkdir(const char *format, ...)
{
  COMPLETE_VARARGS(format,buffer);
#if defined(_WIN32)
  return _mkdir(buffer);
#else
  return mkdir(buffer,0755);
#endif
}

int vsystem(const char *format, ...)
{
  COMPLETE_VARARGS(format, buffer);
  return system(buffer);
}

// note: unlike the usual strncat, the parameter buflen here is the 
// total number of bytes that will be written to s, *including* what
// is already present in s, and also including the trailing 0.
// Example: 
//  s="hello" + trailing 0
//  buflen=10;
//  format + ...  evaluates to "world"
// --> on return, s="helloworl" + trailing 0
//
void vstrncat(char *s, size_t n, const char *format, ...)
{
  COMPLETE_VARARGS(format,buffer);
  size_t OldLength = strlen(s) + 1;
  if (OldLength>=n) return;
  strncat(s, buffer, n - OldLength);
}

char *vstrappend(char *s, const char *format, ...)
{ COMPLETE_VARARGS(format,buffer);
  if (s==nullptr) 
   s=strdupEC(buffer);
  else
   { int NS=strlen(s), NB=strlen(buffer);
     s = (char *)reallocEC(s, NS+NB+1);
     strcpy(s + NS, buffer);
   }
  return s;
}

char *vstrdup(const char *format, ...)
{
  COMPLETE_VARARGS(format,buffer);
  return strdupEC(buffer);
}

void vsetenv(const char *VariableName, const char *format, ...)
{
  COMPLETE_VARARGS(format,buffer);
#if defined(_WIN32)
  SetEnvironmentVariable(VariableName, buffer);
#else
  setenv(VariableName, buffer, 1);
#endif
}

void ErrExit(const char *format, ...)
{
  va_list ap; 
  char buffer[2*MAXSTR];
  va_start(ap,format);
  vsnprintfEC(buffer,2*MAXSTR,format,ap);
  va_end(ap);
  fprintf(stderr,"error: %s (aborting)\n",buffer);
  Log("error: %s (aborting)",buffer);
  exit(1);
}

void Warn(const char *format, ...)
{
  COMPLETE_VARARGS(format, buffer);
  fprintf(stderr,"**warning: %s \n",buffer);
  Log("warning: %s \n",buffer);
}

/***************************************************************/
/* Simple general-purpose status logging (not thread-safe) *****/
/***************************************************************/
static char *LogFileName=nullptr;
static int LogToConsole=0;

void SetConsoleLogging()
 { LogToConsole=1; }

void SetLogFileName(const char *format, ...)
{ COMPLETE_VARARGS(format, buffer);
  LogToConsole=0;
  if (LogFileName) free(LogFileName);
  LogFileName=strdupEC(buffer);
}

void DoLog(bool ByThread, bool WriteCR, const char *Message)
{
  FILE *f=nullptr;
  if (LogToConsole) 
   f=stdout;
  else if (LogFileName==nullptr) 
   return;
  else
   f = (ByThread ? vfopen("%s.%i","a",LogFileName,GetThreadNum()) : fopen(LogFileName,"a")); 
  if (f==nullptr) return;
  fprintf(f,"%s: %s%s",GetTimeString(),Message,WriteCR ? "\n" : "");
  if (!LogToConsole) fclose(f);
}

void Log(const char *format, ...)
{ COMPLETE_VARARGS(format,buffer);
  DoLog(false,true,buffer);
}

void LogC(const char *format, ...)
{ COMPLETE_VARARGS(format,buffer);
  DoLog(false,false,buffer);
}

void LogThread(const char *format, ...)
{ COMPLETE_VARARGS(format,buffer);
  DoLog(true,true,buffer);
}

void InitializeLog(char *argv0, const char *Path)
{ char *CodeName=GetFileBase(argv0);
  if (LogFileName==nullptr)
   Path ? SetLogFileName("%s/%s.log",Path,CodeName)
        : SetLogFileName("%s.log",CodeName);
  Log("%s running on %s:%d (%s)",CodeName, GetHostName(), getpid(), GetTimeString());
}

// 20120225 thread-safe logging
#ifdef USE_PTHREAD 
static pthread_mutex_t LogMutex = PTHREAD_MUTEX_INITIALIZER;
#endif
void MutexLog(const char *format, ... )
{ COMPLETE_VARARGS(format,buffer);

#ifdef USE_PTHREAD 
  pthread_mutex_lock(&LogMutex);
#endif
  Log(buffer);
#ifdef USE_PTHREAD 
  pthread_mutex_unlock(&LogMutex);
#endif
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void LogPercent(int n, int N, int Gradations)
{ 
  for(int PerCent=0; PerCent<Gradations; PerCent++)
   if ( n == (N*PerCent/Gradations) )
    MutexLog("  %i %% (%i/%i)...",PerCent*100/Gradations,n,N);
}

/***************************************************************/
/* figure out how many processors (cores) are on this machine. */
/***************************************************************/
int GetNumProcs()
{ 
#if defined(USE_OPENMP)
  return omp_get_num_procs();
#elif defined(_WIN32)
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
#else
  return sysconf(_SC_NPROCESSORS_ONLN);
#endif
  
#if 0 // old non-portable method
  int n,NumProcs;
  FILE *f;

  f=popen("cat /proc/cpuinfo | grep processor | wc -l","r");
  if (!f)
   return 1;

  n=fscanf(f,"%i",&NumProcs);
  if ( n!=1 || NumProcs<=0 )
   return 1;

  return NumProcs;
#endif
} 


/***************************************************************/
/* figure out default number of threads to use.                */
/*                                                             */
/* The static library variable NumThreads and the SetNumThreads*/
/* routine provide a mechanism for API programmers using       */
/* pthreads to specify a number of threads to use. By default  */
/* this variable is unset (set to -1), in which case           */
/* GetNumThreads() will return the number of processor cores.  */
/***************************************************************/
static int NumThreads=-1;
void SetNumThreads(int pNumThreads)
{ NumThreads=pNumThreads; }

int GetNumThreads()
{
#if defined(USE_OPENMP)
  return omp_get_max_threads();
#elif defined(USE_PTHREAD)
  if (NumThreads!=-1) return NumThreads;
  return GetNumProcs();
#else
  return 1;
#endif
}

int GetThreadNum()
{
#if defined(USE_OPENMP)
  return omp_get_thread_num();
#else
  return 0;
#endif
}

/***************************************************************/
/* return 1 if we are running on a core i7 machine (because in */
/* case we will want to set the number of threads used by the  */
/* multithreaded BLAS library equal to half the actual number  */
/* of cores detected by GetNumProcs() because hyperthreading   */
/* screws up the BLAS cache optimizations)                     */
/***************************************************************/
int DetectCorei7()
{ 
  int GrepReturnValue;

  GrepReturnValue=system("cat /proc/cpuinfo | grep -iq i7");
  if(GrepReturnValue==0)
   return 1;
  else
   return 0;
} 

/***************************************************************/
/* nonportable linux memory usage function since getrusage() is*/
/* not useful on linux systems                                 */
/* if MemoryUsage is non-null, it should point to a buffer     */
/* with room for at least 7 unsigned longs, which are filled   */
/* in on return with the various memory usage statistics       */
/***************************************************************/
unsigned long GetMemoryUsage(unsigned long *MemoryUsage)
{
#if defined(_WIN32)
  for (int i=0; i<7; ++i) MemoryUsage[i] = 0;
#else
  FILE *f;
  if ( (f=fopen("/proc/self/statm","r")) )
   { 
     char Line[MAXSTR];
     char *result=fgets(Line,MAXSTR,f);
     fclose(f);
     if (result==nullptr) return 0;

     unsigned long Mem[7];
     sscanf(Line,"%lu %lu %lu %lu %lu %lu %lu",
                  Mem+0, Mem+1, Mem+2, Mem+3, Mem+4, Mem+5, Mem+6);

     long sz = sysconf(_SC_PAGESIZE);
     if (MemoryUsage)
      for(int n=0; n<7; n++)
       MemoryUsage[n] = sz*Mem[n];

     return sz*Mem[0];

   };
#endif
  return 0;
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void SetCPUAffinity(int WhichCore)
{ 
#if defined(_GNU_SOURCE) && defined(USE_PTHREAD)
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(WhichCore,&cpuset); 
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
  (void) WhichCore; // unused
#endif
}

void GetCPUAffinity()
{
#if defined(_WIN32)
#elif 0 // nonportable
  cpu_set_t cpuset;
  sched_getaffinity(0, sizeof(cpu_set_t), &cpuset);
  printf("** CPUAffinity: ");
  for(int i=0; i<8; i++)
   printf("%i", CPU_ISSET(i,&cpuset) ? 1 : 0);
  printf("\n");
#endif
}

/***************************************************************/
/* wait for keypress *******************************************/
/***************************************************************/
void KeyPause()
{ char buffer[100];
  printf("press any key to continue...");
  fgets(buffer,100,stdin);
}

/***************************************************************/
/* relative difference *****************************************/
/***************************************************************/
double RD(double x, double y)
{ 
  if ( x==0.0 && y==0.0 )
   return 0.0;
  return 2.0*fabs(x-y)/(fabs(x)+fabs(y));
}

double RD(cdouble x, cdouble y)
{ 
  double xMag2=norm(x);
  double yMag2=norm(y);
  cdouble z=x-y;
  double zMag2=norm(z);

  if ( z == cdouble(0.0,0.0) )
   return 0.0;
  return 2.0*sqrt(zMag2 / (xMag2+yMag2) );
}

/***************************************************************/
/* error-checked malloc and realloc.                           */
/* note: this malloc() zeros out the buffer before returning,  */
/* i.e. it behaves like calloc().                              */
/***************************************************************/
void *mallocEC(size_t size)
{
  if (size==0)
   return nullptr;
  void *v=malloc(size);
  if (size && v==nullptr)
   ErrExit("out of memory");
  memset(v,0,size); 
  return v;
}

void *reallocEC(void *v, size_t size)
{
  v=realloc(v, size);
  if (size && v==nullptr)
   ErrExit("out of memory");
  return v;
}

/***************************************************************/
/* error-checked strdup ****************************************/
/***************************************************************/
char *strdupEC(const char *s)
{
  char *dup = (char *) mallocEC(strlen(s) + 1);
  strcpy(dup, s);
  return dup;
}

/***************************************************************/
/* strdup for general pointers *********************************/
/***************************************************************/
void *memdup(void *v, size_t size)
{ if (v==nullptr || size==0)
   return nullptr;
  void *vv=mallocEC(size);
  if (vv) memcpy(vv,v,size);
  return vv;
}

/***************************************************************/
/* some complex-number functions *******************************/
/***************************************************************/
cdouble expi(double x)   { return cdouble(cos(x),sin(x)); }

/***************************************************************/
/* complex sqrt that always returns a value with imag part >=0 */
/***************************************************************/
cdouble csqrt2(cdouble z)
{ 
  cdouble rtz=sqrt(z);
  if ( imag(rtz) < 0.0 )
   return -rtz;
  else
   return rtz;
}

/***************************************************************/
/* try to convert string to complex number.                    */
/*   examples: 3+4i, -545.234, -545.234I, 18e3-19e5i           */
/* returns 0 on success, 1 on error.                           */
/***************************************************************/
int S2CD(const char *str, cdouble *pZ)
{ 
  if (!str || !pZ) return 1;

  const char *p=str;
  char *End=nullptr;
  double X=strtod(p,&End);
  if (End==p) return 1;
  p=End;
  while (isspace((unsigned char)*p)) p++;

  /* pure real value */
  if (*p==0)
   { *pZ=cdouble(X,0.0); return 0; };

  /* pure imaginary value: 3i, 3j, 3I, 3J */
  if (*p=='i' || *p=='I' || *p=='j' || *p=='J')
   { p++;
     while (isspace((unsigned char)*p)) p++;
     if (*p!=0) return 1;
     *pZ=cdouble(0.0,X); return 0;
   };

  /* complex value: X+Yi, X-Yi, ... */
  if (*p!='+' && *p!='-') return 1;
  int Sign = (*p=='+') ? 1 : -1;
  p++;
  while (isspace((unsigned char)*p)) p++;

  char *EndY=nullptr;
  double Y=strtod(p,&EndY);
  if (EndY==p) return 1;
  p=EndY;
  while (isspace((unsigned char)*p)) p++;

  if (*p!='i' && *p!='I' && *p!='j' && *p!='J') return 1;
  p++;
  while (isspace((unsigned char)*p)) p++;
  if (*p!=0) return 1;

  *pZ=cdouble(X, Sign*Y);
  return 0;

} 

/***************************************************************/
/* convert complex number to a string                          */
/***************************************************************/
static char DefaultCD2SFormat[100];
static int DefaultCD2SFormatInitialized=0;

char *CD2S(cdouble z, const char *format)  
{ 
  #define NUMSTRINGS 100
  #define STRLEN 50
  static thread_local char Strings[NUMSTRINGS][STRLEN];
  static thread_local int BufPtr=0;

  BufPtr = (BufPtr+1) % NUMSTRINGS;
  char *zStr=Strings[BufPtr];

  if (!format)
   { if ( imag(z)==0.0 )
      snprintf(zStr,STRLEN,"%g",real(z));
     else if ( real(z)==0.0 )
      snprintf(zStr,STRLEN,"%gi",imag(z));
     else 
      snprintf(zStr,STRLEN,"%g+%gi",real(z),imag(z));
     return zStr; 
   };

  int nc, nConv;
  for(nConv=0, nc=0; format[nc]; nc++)
   if (format[nc]=='%') nConv++;

  if (nConv==2)
   snprintf(zStr,STRLEN,format,real(z),imag(z));
  else if (nConv==1)
   snprintf(zStr,STRLEN,format,real(z));
  else 
   snprintf(zStr,STRLEN,"%s",format);

  return zStr;
}

char *CD2S(cdouble z)
{ 
  if (DefaultCD2SFormatInitialized==0)
   { snprintf(DefaultCD2SFormat,sizeof(DefaultCD2SFormat),
              "%s","(%+9.5e,%+9.5e)");
     DefaultCD2SFormatInitialized=1;
   };
  return CD2S(z,DefaultCD2SFormat);
}

void SetDefaultCD2SFormat(const char *format)
{ 
  snprintf(DefaultCD2SFormat,sizeof(DefaultCD2SFormat),"%s",
           format ? format : "");
  DefaultCD2SFormatInitialized=1;
}

char *z2s(cdouble z)
{ 
  return CD2S(z,nullptr);
}

void z2s(cdouble z, char *zStr, size_t Size)
{ snprintf(zStr, Size, "%s", z2s(z)); }

void z2s(cdouble z, char *zStr)
{ z2s(z, zStr, STRLEN); }

/***************************************************************/
/* given a string like 'MyFile.dat', try to create a new file  */
/* called 'MyFile.dat.' if that doesn't work (because there is */
/* already a file called 'MyFile.dat', then try to create a    */
/* new file called 'MyFile.dat.1'. if that doesn't work, try   */
/* to create a new file called 'MyFile.dat.2', and so on.      */
/*                                                             */
/* on success, return the new file, opened for writing.        */
/*                                                             */
/* if PrintMessage is nonzero, then we print a console message */
/* in cases in which we had to append an integer to base to    */
/* obtain a unique filename.                                   */
/*                                                             */
/* if FileName is nonzero, then FileName must point to a buffer*/
/* with space for at least MAXSTR characters, which is filled  */
/* in with the name of the file that was eventually created.   */
/***************************************************************/
FILE *CreateUniqueFile(const char *Base, int ConsoleMessage, char *FileName)
 {
   FILE *f;
   char NewFileName[MAXSTR];
   int N;

   for(N=0; N<MAXSTR; N++)
    { 
      if (N==0)
       snprintf(NewFileName,sizeof(NewFileName)-10,"%s",Base);
      else
       snprintf(NewFileName,sizeof(NewFileName)-10,"%s.%i",Base,N);

      f=fopen(NewFileName,"r");
      if (f==nullptr) break;
      fclose(f);

    };
   if (f!=nullptr)
    return nullptr;

   f=fopen(NewFileName,"w");

   if ( ConsoleMessage && N>0 )
    fprintf(stderr,"\n ** WARNING: file %s exists (writing to file %s instead)\n",
            Base,NewFileName);
                  
   if (FileName)
    snprintf(FileName,MAXSTR,"%s",NewFileName);

   return f;

 } 

// return a string form of the current time. If no buffer is supplied,
// the return value points to a thread-local buffer.
char *GetTimeString(char *s)
{
  static thread_local char TimeStr[30];
  if (s==nullptr) s=TimeStr;
  s[0]=0;
  time_t MyTime=time(nullptr);
  struct tm TimeStruct;
#if defined(_WIN32)
  localtime_s(&TimeStruct,&MyTime);
#else
  localtime_r(&MyTime,&TimeStruct);
#endif
  strftime(s,30,"%D::%T",&TimeStruct);
  s[29]=0;
  return s;
}

char *GetHostName()
{ 
  static thread_local char HostName[MAXSTR];
#if defined(_WIN32)
  // gethostname exists on Win32 but causes link failure with MinGW
  // ... punt, since scuff doesn't actually use this function.
  snprintf(HostName,sizeof(HostName),"%s","Windows");
#else
  gethostname(HostName,sizeof(HostName)-1);
  HostName[sizeof(HostName)-1]=0;
#endif
  return HostName;
}
  

FILE *CreateUniqueFile(const char *Base, int ConsoleMessage) 
 { return CreateUniqueFile(Base,ConsoleMessage,nullptr); }

FILE *CreateUniqueFile(const char *Base) 
 { return CreateUniqueFile(Base,0,nullptr); }

/***************************************************************/
/***************************************************************/
/***************************************************************/

#if !defined(_WIN32)
static char CodeMarker[MAXSTR];
void SetCodeMarker(const char *Marker)
{ snprintf(CodeMarker,sizeof(CodeMarker),"%s", Marker ? Marker : ""); }

static void SignalHandler(int WhichSignal)
{
  /* The handler reports to the log file if one is in use, otherwise to
     stderr.  Only close the file we opened ourselves: closing stderr
     would break all subsequent output of the process.  (fopen/fprintf
     are not async-signal-safe; the report is a best-effort diagnostic
     on the way to exit.) */
  bool OpenedHere=false;
  FILE *f = (LogFileName ? fopen(LogFileName,"a") : stderr );
  if (f==nullptr)
   f=stderr;
  else
   OpenedHere=true;

  dprintf(fileno(f),"\n\n**<><> received signal %i ",WhichSignal);
  if (CodeMarker[0]!=0)
   dprintf(fileno(f),"while %s",CodeMarker);
  dprintf(fileno(f),"\n\n");

#ifdef HAVE_BACKTRACE
  void *buffer[20];
  int nptrs=backtrace(buffer, 20);
  backtrace_symbols_fd(buffer, nptrs, fileno(f));
#endif

  if (OpenedHere)
   fclose(f);
  exit(1);
}
#endif // !defined(_WIN32)

void InstallHRSignalHandler()
{ 
#if defined(_WIN32) || defined(__APPLE__)
#else

  // this allows users to set the environment variable
  //  SCUFF_ABORT_ON_FPE=1
  // to force the code to abort on any floating-point error
  char *s=getenv("SCUFF_ABORT_ON_FPE");
  if (s && s[0]=='1')
   feenableexcept(FE_INVALID | FE_OVERFLOW);

  struct sigaction sa;

  sa.sa_handler=SignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags=0;
  //sigaction(SIGKILL, &sa, 0); //can't mask this one 
  sigaction(SIGSEGV, &sa, nullptr);
  sigaction(SIGPIPE, &sa, nullptr);
  sigaction(SIGALRM, &sa, nullptr);
  sigaction(SIGFPE,  &sa, nullptr);
  sigaction(SIGTRAP, &sa, nullptr);
  sigaction(SIGABRT, &sa, nullptr);
  //sigaction(SIGCHLD, &sa, 0);

  CodeMarker[0]=0;
#endif
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
bool IsFinite(double d)
{ return !(ISNAN(d));
#if 0
// 20120622 I don't understand why fpclassify() seems not to 
//          be portable; isn't it a POSIX function? I am 
//          removing this for now. 
  // int fpc = fpclassify(d);
  int fpc = __fpclassify(d);
  if (fpc==FP_NORMAL || fpc==FP_ZERO)
   return 1;
  return 0;
#endif
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
bool IsFinite(cdouble z)
 { return IsFinite(real(z)) && IsFinite(imag(z)); }
 
/***************************************************************/
/***************************************************************/
/***************************************************************/
void PlusEqualsVec(double *V1, double Alpha, double *V2, int N)
 { for(int n=0; n<N; n++) V1[n]+=Alpha*V2[n]; }
void PlusEqualsVec(cdouble *V1, cdouble Alpha, cdouble *V2, int N)
 { for(int n=0; n<N; n++) V1[n]+=Alpha*V2[n]; }
void PlusEqualsVec(cdouble *V1, cdouble Alpha, double *V2, int N)
 { for(int n=0; n<N; n++) V1[n]+=Alpha*V2[n]; }
void PlusEqualsVec(double *V1, double *V2, int N)
 { for(int n=0; n<N; n++) V1[n]+=V2[n]; }
void PlusEqualsVec(cdouble *V1, cdouble *V2, int N)
 { for(int n=0; n<N; n++) V1[n]+=V2[n]; }
void ScaleVec(double *V1, double Alpha, int N)
 { for(int n=0; n<N; n++) V1[n]*=Alpha; }
void ScaleVec(cdouble *V1, cdouble Alpha, int N)
 { for(int n=0; n<N; n++) V1[n]*=Alpha; }

/***************************************************************/
/***************************************************************/
/***************************************************************/
bool CheckEnv(const char *Name, const char *fmt, void *Destination, bool LogSuccess)
{ char *Value=getenv(Name);
  bool Status=(Value && 1==sscanf(Value,fmt,Destination));
  if (Status && LogSuccess) Log("Set %s to %s",Name,Value);
  return Status;
}
bool CheckEnv(const char *Name, double *Destination, bool LogSuccess)
{ return CheckEnv(Name, "%le", (void *)Destination, LogSuccess); }
bool CheckEnv(const char *Name, int *Destination, bool LogSuccess)
{ return CheckEnv(Name, "%i", (void *)Destination, LogSuccess); }

bool CheckEnv(const char *Name, char **Destination, bool LogSuccess)
{ char *Value = getenv(Name);
  if (Value)
   { *Destination = strdup(Value);
     if (LogSuccess) Log("Set %s to %s",Name,Value);
     return true;
   }
  return false;
}

bool CheckEnv(const char *Name, bool LogSuccess)
{ char *s=getenv(Name);
  bool Status = (s && s[0]=='1');
  if (Status && LogSuccess) Log("Set %s = true",Name);
  return Status;
}

void AppendEnv(const char *Name, const char *Separator, const char *format, ...)
{ 
  COMPLETE_VARARGS(format, buffer);
  char *Old = getenv(Name);
  if (Old) { // check if Old already contains buffer
    const char *p = strstr(Old, buffer);
    if (p) {
      size_t seplen = strlen(Separator);
      size_t buflen = strlen(buffer);
      if ((p == Old || (p >= Old + seplen && !strncmp(p-seplen, Separator, seplen)))
          && (*(p+buflen)==0 || !strcmp(p+buflen, Separator)))
        return; // Old contains buffer with Separator before & after
    }
  }
  char *New = vstrdup("%s%s%s",Old ? Old:"",Old&&Separator ? Separator:"",buffer);
  setenv(Name,New,1);
  free(New);
  return;
}
