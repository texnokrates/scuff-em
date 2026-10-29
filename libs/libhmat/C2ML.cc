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
 * C2ML.cc     -- libhmat class methods for exporting HMatrices 
 *             -- and HVectors to binary data files in a way that 
 *             -- allows subsequent import into a MATLAB session
 *
 * homer reid  -- 12/2007 -- 5/2011
 *
 * --------------------------------------------------------------
 *
 * to export HMatrices and HVectors from C++ programs to MATLAB
 * sessions, proceed as follows:
 *
 * a) in your C++ program, say 
 * 
 *    void *pCC=HMatrix::OpenMATLABContext("MyMatrices");
 *
 *    this will lead to the creation, in your current working, 
 *    directory, of two files named 'MyMatrices.hdf5' and 
 *    'MyMatrices.m'. (if you already have files with these names,
 *    they will be overwritten).
 * 
 * b) in your C++ program, say 
 *      
 *       M1->ExportToMATLAB(pCC, "M1");
 *       M2->ExportToMATLAB(pCC, "M2");
 *       V->ExportToMATLAB(pCC, "V");
 * 
 *    etc. you may write as many matrices and vectors as you like.
 *     
 * c) when you are finished, in your C++ program say 
 *
 *       HMatrix::CloseMATLABContext(pCC);
 *     
 * d) and now, from the MATLAB prompt, just type 'MyMatrices' 
 *    (or whatever name you passed to CreateMATLABContext()):
 *
 *   >> MyMatrices
 *
 * e) now your MATLAB session contains matrices named M1 and M2 and 
 *    a vector named V: 
 *
 *   >> who
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <libhrutil.h>

#include "libhmat.h"

/***************************************************************/
/* this is a utility routine that converts certain characters  */
/* into others to achieve MATLAB-friendly strings (e.g. matlab */
/* will refuse to read files with names that contain decimal   */
/* points, etc.)                                               */
/***************************************************************/
static void KillDP(char *s)
{ char *p;
  for(p=s; *p; p++)
   { if ( *p=='.' || *p=='-' ) *p='_';
     if ( *p=='+' ) *p='p';
   };
}

/***************************************************************/
/* this is a data structure containing everything that i need  */
/* to keep track of in a C2ML session.                         */
/***************************************************************/
using MATLABContext = struct MATLABContext
 { 
   void *pHC;     // HDF5Context 
   FILE *MFile;   // .m file to which I write matlab instructions
   char *HDF5FileName;

 };

/***************************************************************/
/***************************************************************/
/***************************************************************/
void *HMatrix::OpenMATLABContext(const char *format, ... )
{ 
  va_list ap;
  char Name[1000];
  va_start(ap,format);
  vsnprintfEC(Name,1000,format,ap);
  va_end(ap);
  KillDP(Name);

  /* Name may use all of its 1000 bytes; leave room for the suffixes. */
  char buffer[1100];
  snprintf(buffer,sizeof(buffer),"%s.m",Name);
  FILE *MFile=fopen(buffer,"w");

  snprintf(buffer,sizeof(buffer),"%s.hdf5",Name);
  void *pHC=HMatrix::OpenHDF5Context(buffer);

  if ( pHC!=nullptr && MFile!=nullptr )
   { MATLABContext *CC=(MATLABContext *)malloc(sizeof(*CC));
     CC->pHC=pHC;
     CC->MFile=MFile;
     CC->HDF5FileName=strdupEC(buffer);
     return (void *)CC;
   }
  else
   { if (MFile) fclose(MFile);
     fprintf(stderr,"warning: failed to open MATLAB context %s\n",Name);
     return nullptr;
   };

}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void HMatrix::CloseMATLABContext(void *pCC)
{ 
  MATLABContext *CC=(MATLABContext *)pCC;
  HMatrix::CloseHDF5Context(CC->pHC);
  fclose(CC->MFile);
  free(CC);

}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void HMatrix::ExportToMATLAB(void *pCC, const char *format, ...)
{ 
  va_list ap;
  char MName[1000];
  va_start(ap,format);
  vsnprintfEC(MName,1000,format,ap);
  va_end(ap);
  KillDP(MName);

  MATLABContext *CC=(MATLABContext *)pCC;
  HMatrix *MCopy;
  int nr, nc;
  /* MName may use all of its 1000 bytes; "real_"/"imag_" add five. */
  char buffer[1100];

  if (RealComplex==LHM_REAL && StorageType==LHM_NORMAL)
   { 
      this->ExportToHDF5(CC->pHC, MName);
      fprintf(CC->MFile,"%s=hdf5read('%s','%s');\n",
                          MName,CC->HDF5FileName,MName);
   }
  // FIXME all the remaining 'else' cases could be done more efficiently
  else if (RealComplex==LHM_REAL && StorageType==LHM_SYMMETRIC)
   { 
      MCopy=new HMatrix(NR, NR, LHM_REAL, LHM_NORMAL);
      MCopy->Copy(this);
      MCopy->ExportToHDF5(CC->pHC, MName);
      fprintf(CC->MFile,"%s=hdf5read('%s','%s');\n",
                          MName,CC->HDF5FileName,MName);
      delete(MCopy);
   }
  else if (RealComplex==LHM_COMPLEX )
   {  
      MCopy=new HMatrix(NR, NC, LHM_REAL, LHM_NORMAL);

      for(nr=0; nr<NR; nr++)
       for(nc=0; nc<NC; nc++)
        MCopy->SetEntry(nr, nc, real(this->GetEntry(nr,nc)));

      snprintf(buffer,sizeof(buffer),"real_%s",MName);
      MCopy->ExportToHDF5(CC->pHC, buffer);
      fprintf(CC->MFile,"%s=hdf5read('%s','%s');\n",
                          buffer,CC->HDF5FileName,buffer);

      for(nr=0; nr<NR; nr++)
       for(nc=0; nc<NC; nc++)
        MCopy->SetEntry(nr, nc, imag(this->GetEntry(nr,nc)));
      snprintf(buffer,sizeof(buffer),"imag_%s",MName);
      MCopy->ExportToHDF5(CC->pHC, buffer);
      fprintf(CC->MFile,"%s=hdf5read('%s','%s');\n",
                          buffer,CC->HDF5FileName,buffer);

      fprintf(CC->MFile,"%s=real_%s + sqrt(-1)*imag_%s;\n",MName,MName,MName);
      fprintf(CC->MFile,"clear real_%s; clear imag_%s;\n",MName,MName);

      delete(MCopy);

   };

}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void HVector::ExportToMATLAB(void *pCC, const char *format, ...)
{
  va_list ap;
  char VName[1000];
  va_start(ap,format);
  vsnprintfEC(VName,1000,format,ap);
  va_end(ap);
  KillDP(VName);

  MATLABContext *CC=(MATLABContext *)pCC;

  ExportToHDF5(CC->pHC, VName);

  fprintf(CC->MFile,"%s=hdf5read('%s','%s');\n",
                      VName,CC->HDF5FileName,VName);
}
