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
 * FIPPICache.cc -- implementation of the FIPPICache class for libscuff
 * 
 * homer reid    -- 11/2005 -- 1/2012
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libhrutil.h>

#include "libscuff.h"
#include "libscuffInternals.h"

namespace scuff {

#define KEYLEN 15 
#define KEYSIZE (KEYLEN*sizeof(float))

/*--------------------------------------------------------------*/
/*- note: i found this on wikipedia ... ------------------------*/
/*--------------------------------------------------------------*/
long JenkinsHash(const char *key, size_t len); // in FIBBICache.cc

long HashFunction(const float *Key)
{ return JenkinsHash( (const char *)Key, KEYSIZE );
} 

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
// on-disk record for the FIPPI cache file format: differs from the
// FIPPIKeyValuePair structure above in that it contains the actual contents
// of a QIFIPPIData structure, whereas FIPPIKeyValuePair contains just a
// pointer to such a structure.  (Defined here rather than with the
// Store/PreLoad routines because the destructor needs FIPPICF_RECSIZE
// to reason about the extent of the preloaded-records buffer.)
using FIPPICF_Record = struct FIPPICF_Record
 { FIPPIKeyStruct K;
   QIFIPPIData QIFDBuffer;
 };
#define FIPPICF_RECSIZE sizeof(FIPPICF_Record)

/*--------------------------------------------------------------*/
/*- class constructor ------------------------------------------*/
/*--------------------------------------------------------------*/
FIPPICache::FIPPICache()
{
  PreloadFileName=nullptr;
  RecordsPreloaded=0;
  PreloadedRecords=nullptr;
  NumPreloadedRecords=0;
}

/*--------------------------------------------------------------*/
/*- class destructor  ------------------------------------------*/
/*--------------------------------------------------------------*/
FIPPICache::~FIPPICache()
{
  if (PreloadFileName)
   free(PreloadFileName);

  /* entries created on the fly own their QIFIPPIData and are freed
     individually; preloaded entries are interior pointers into
     PreloadedRecords, which is freed in one piece. */
  const char *PreloadBegin=(const char *)PreloadedRecords;
  const char *PreloadEnd=PreloadBegin + NumPreloadedRecords*FIPPICF_RECSIZE;
  for(FIPPIKeyValueMap::iterator p=Table.begin(); p!=Table.end(); p++)
   { const char *Value=(const char *)(p->second);
     if ( !(Value >= PreloadBegin && Value < PreloadEnd) )
      free(p->second); // NOLINT(cppcoreguidelines-no-malloc) -- mixed value ownership documented above
   };
  free(PreloadedRecords); // NOLINT(cppcoreguidelines-no-malloc)
}

static void inline VecSubFloat(double *V1, double *V2, float *V1mV2)
{ V1mV2[0] = ((float)V1[0]) - ((float)V2[0]);
  V1mV2[1] = ((float)V1[1]) - ((float)V2[1]);
  V1mV2[2] = ((float)V1[2]) - ((float)V2[2]);
}

/*--------------------------------------------------------------*/
/*- routine for fetching a FIPPI data record from a FIPPIDT:    */
/*- we look through our table to see if we have a record for    */
/*- this panel pair, we return it if we do, and otherwise we    */
/*- compute a new FIPPI data record for this panel pair and     */
/*- add it to the table.                                        */
/*- important note: the vertices are assumed to be canonically  */
/*- ordered on entry.                                           */
/*--------------------------------------------------------------*/
QIFIPPIData *FIPPICache::GetQIFIPPIData(double **OVa, double **OVb, int ncv)
{
  /***************************************************************/
  /* construct a search key from the canonically-ordered panel   */
  /* vertices.                                                   */
  /* the search key is kinda stupid, just a string of 15 floats  */
  /* as follows:                                                 */
  /*  0--2    VMed  - VMin [0..2]                                */
  /*  3--5    VMax  - VMin [0..2]                                */
  /*  6--8    VMinP - VMin [0..2]                                */
  /*  9--11   VMedP - VMin [0..2]                                */
  /*  12--14  VMaxP - VMin [0..2]                                */
  /* note that we store floats, not doubles, since we only want  */
  /* to check equality of vertex coordinates up to one part in   */
  /* 10^8 anyway.                                                */
  /***************************************************************/
  FIPPIKeyStruct K = {};
  VecSubFloat(OVa[1], OVa[0], K.Key+0  );
  VecSubFloat(OVa[2], OVa[0], K.Key+3  );
  VecSubFloat(OVb[0], OVa[0], K.Key+6  );
  VecSubFloat(OVb[1], OVa[0], K.Key+9  );
  VecSubFloat(OVb[2], OVa[0], K.Key+12 );

  /***************************************************************/
  /* look for this key in the cache ******************************/
  /***************************************************************/
  {
    std::shared_lock<std::shared_mutex> Guard(FCLock);
    FIPPIKeyValueMap::const_iterator p=Table.find(K);
    if ( p != Table.end() )
     { Hits++;
       return p->second;
     }
  }

  /***************************************************************/
  /* if it was not found, allocate and compute a new QIFIPPIData */
  /* structure, then add this structure to the cache             */
  /***************************************************************/
  Misses++;
  QIFIPPIData *QIFD=(QIFIPPIData *)mallocEC(sizeof *QIFD);
  ComputeQIFIPPIData(OVa, OVb, ncv, QIFD);

  {
    std::unique_lock<std::shared_mutex> Guard(FCLock);
    Table.insert( FIPPIKeyValuePair(K, QIFD) );
  }

  return QIFD;
}

/***************************************************************/
/* this routine and the following routine implement a mechanism*/
/* for storing the contents of a FIPPI cache to a binary file, */
/* and subsequently pre-loading a FIPPI cache with the content */
/* of a file created by this storage operation.                */
/*                                                             */
/* the file format is pretty simple (and non-portable w.r.t.   */
/* endianness):                                                */
/*  bytes 0--10:   'FIPPICACHE' + 0 (a file signature used as  */
/*                                   a simple sanity check)    */
/*  next xx bytes:  first record                               */
/*  next xx bytes:  second record                              */
/*  ...             ...                                        */
/*                                                             */
/* where xx is the size of the record; each record consists of */
/* a search key (15 double values) followed by the content     */
/* of the QIFIPPIDataRecord for that search key.               */
/*                                                             */
/* note: FIPPICF = 'FIPPI cache file'                          */
/***************************************************************/
const char FIPPICF_Signature[]="FIPPICACHE";
#define FIPPICF_SIGSIZE sizeof(FIPPICF_Signature)

void FIPPICache::Store(const char *FileName)
{
  if (FileName==nullptr) return;

  /*--------------------------------------------------------------*/
  /*- pause to check if the following conditions are satisfied:  -*/
  /*-  (1) the FIPPI cache was preloaded from an input file whose-*/
  /*-      name matches the name of the output file to which we  -*/
  /*-      are being asked to dump the cache                     -*/
  /*-  (2) the number of cache records hasn't changed since we   -*/
  /*-      preloaded from the input file.                        -*/
  /*- if both conditions are satisfied, we don't bother to dump  -*/
  /*- the cache since the operation would result in a cache dump -*/
  /*- file identical to the one that already exists.             -*/
  /*--------------------------------------------------------------*/
  if (     PreloadFileName 
       && !strcmp(PreloadFileName, FileName) 
       && RecordsPreloaded==(Table.size())
     )  
   { Log("FIPPI cache unchanged since reading from %s (skipping cache dump)",FileName);
     return;
   };

  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  std::shared_lock<std::shared_mutex> Guard(FCLock);

  int NumRecords=0;

  /*--------------------------------------------------------------*/
  /*- try to open the file ---------------------------------------*/
  /*--------------------------------------------------------------*/
  FILE *f=fopen(FileName,"w");
  if (!f)
   { fprintf(stderr,"warning: could not open file %s (aborting cache dump)...",FileName);
     return;
   };
  Log("Writing FIPPI cache to file %s...",FileName);

  /*---------------------------------------------------------------------*/
  /*- write file signature ----------------------------------------------*/
  /*---------------------------------------------------------------------*/
  fwrite(FIPPICF_Signature,FIPPICF_SIGSIZE,1,f);

  /*---------------------------------------------------------------------*/
  /*- iterate through the table and write entries to the file one-by-one */
  /*---------------------------------------------------------------------*/
  FIPPIKeyStruct K = {};
  QIFIPPIData *QIFD;
  FIPPICF_Record MyRecord;
  for ( auto it = Table.begin(); it != Table.end(); it++ ) 
   { 
     K=it->first;
     QIFD=it->second;
     memcpy(&(MyRecord.K.Key),       K.Key, sizeof(MyRecord.K.Key ));
     memcpy(&(MyRecord.QIFDBuffer),  QIFD,  sizeof(MyRecord.QIFDBuffer));
     if ( 1 != fwrite(&(MyRecord),sizeof(MyRecord),1,f ) )
      break;
     NumRecords++;
   };

  /*---------------------------------------------------------------------*/
  /*- and that's it -----------------------------------------------------*/
  /*---------------------------------------------------------------------*/
  fclose(f);
  Log(" ...wrote %i FIPPI records.",NumRecords);
}

void FIPPICache::PreLoad(const char *FileName)
{

  std::unique_lock<std::shared_mutex> Guard(FCLock);

  /*--------------------------------------------------------------*/
  /*- try to open the file ---------------------------------------*/
  /*--------------------------------------------------------------*/
  FILE *f=fopen(FileName,"r");
  if (!f)
   { fprintf(stderr,"warning: could not open file %s (skipping cache preload)\n",FileName);
     Log("Could not open FIPPI cache file %s...",FileName);
     return;
   };

  /*--------------------------------------------------------------*/
  /*- run through some sanity checks to make sure we have a valid */
  /*- cache file                                                  */
  /*--------------------------------------------------------------*/
  const char *ErrMsg;
  ErrMsg = nullptr;
  
  struct stat fileStats;
  if ( fstat(fileno(f), &fileStats) )
   ErrMsg="invalid cache file";
  
  // check that the file signature is present and correct 
  off_t FileSize;
  FileSize=fileStats.st_size;
  char FileSignature[FIPPICF_SIGSIZE]; 
  if ( ErrMsg==nullptr && (FileSize < (signed int )FIPPICF_SIGSIZE) )
   ErrMsg="invalid cache file";
  if ( ErrMsg==nullptr && 1!=fread(FileSignature, FIPPICF_SIGSIZE, 1, f) )
   ErrMsg="invalid cache file";
  if ( ErrMsg==nullptr && strcmp(FileSignature, FIPPICF_Signature) ) 
   ErrMsg="invalid cache file";

  // the file size, minus the portion taken up by the signature, 
  // should be an integer multiple of the size of a FIPPICF_Record
  FileSize-=FIPPICF_SIGSIZE;
  if ( ErrMsg==nullptr && (FileSize % FIPPICF_RECSIZE)!=0 )
   ErrMsg="cache file has incorrect size";

  /*--------------------------------------------------------------*/
  /*- allocate memory to store cache entries read from the file. -*/
  /*- for now, we abort if there is not enough memory to store   -*/
  /*- the full cache; TODO explore schemes for partial preload.  -*/
  /*--------------------------------------------------------------*/
  unsigned int NumRecords;
  FIPPICF_Record *Records;
  NumRecords = FileSize / FIPPICF_RECSIZE;
  if ( ErrMsg==nullptr )
   { Records=(FIPPICF_Record *)mallocEC(NumRecords*FIPPICF_RECSIZE);
     if ( !Records)
      ErrMsg="insufficient memory to preload cache";
   };

  /* from this point on, cache entries inserted below point into the
     Records buffer, so the destructor needs to know about it even if
     the preload is aborted midway.  (PreLoad is called at most once
     per cache instance.) */
  if (ErrMsg==nullptr)
   { PreloadedRecords=Records;
     NumPreloadedRecords=NumRecords;
   };

  /*--------------------------------------------------------------*/
  /*- pause here to clean up if anything went wrong --------------*/
  /*--------------------------------------------------------------*/
  if (ErrMsg)
   { fprintf(stderr,"warning: file %s: %s (skipping cache preload)\n",FileName,ErrMsg);
     Log("FIPPI cache file %s: %s (skipping cache preload)",FileName,ErrMsg);
     fclose(f);
     return;
   };

  /*--------------------------------------------------------------*/
  /*- now just read records from the file one at a time and add   */
  /*- them to the table.                                          */
  /*--------------------------------------------------------------*/
  unsigned int nr;
  Log("Preloading FIPPI records from file %s...",FileName);
  for(nr=0; nr<NumRecords; nr++)
   { 
     if ( fread(Records+nr, FIPPICF_RECSIZE,1,f) != 1 )
      { fprintf(stderr,"warning: file %s: read only %i of %i records",FileName,nr+1,NumRecords);
        fclose(f);
        return;
      };

     Table.insert( FIPPIKeyValuePair(Records[nr].K, &(Records[nr].QIFDBuffer)) );
   };

  /*--------------------------------------------------------------*/
  /*- the full file was successfully preloaded -------------------*/
  /*--------------------------------------------------------------*/
  Log(" ...successfully preloaded %i FIPPI records.",NumRecords);
  fclose(f);

  // the most recent file from which we preloaded, and the number of 
  // records preloaded, are stored within the class body to allow us 
  // to skip dumping the cache back to disk in cases where that would
  // amount to just rewriting the same cache file
  if (PreloadFileName)
   free(PreloadFileName);
  PreloadFileName=strdupEC(FileName);
  RecordsPreloaded=NumRecords;
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
FIPPICache GlobalFIPPICache;

void PreloadCache(const char *FileName)
{ 
  GlobalFIPPICache.PreLoad(FileName);
}

void StoreCache(const char *FileName)
{ 
  GlobalFIPPICache.Store(FileName);
}


} // namespace scuff
