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
 * scuff-tmatrix.cc 
 *
 * This program computes the T-matrix, in a spherical-wave basis,  
 * for an arbitary scatterer. (What this means is the following: 
 * For each spherical wave up to a user-specified maximum L value, 
 * the code scatters that wave off of the geometry and decomposes
 * the resulting scattered field into a basis of spherical waves.)
 * The implementation of this code demonstrates, among other things, 
 * how to define your own type of incident field routine.
 *
 * Homer Reid    -- 6/2012
 *
 * Update, 11/2017: 
 *
 *   -- fixed incorrect normalization conventions
 *   -- updated spherical-moment calculation to use modern 'Panel Cubature' framework
 *   -- updated documentation at
 *      http://homerreid.github.io/scuff-em-documentation/applications/scuff-tmatrix/scuff-tmatrix
 */

#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <string>

#include "libscuff.h"
#include "libhmat.h"
#include "libSpherical.h"
#include "ScuffCLI.h"

using namespace scuff;
#define II cdouble (0.0,1.0)

/***************************************************************/
/***************************************************************/
/***************************************************************/
HVector *GetSphericalMoments(RWGGeometry *G, cdouble k, int LMax,
                             HVector *KN, HVector *MomentVector);

/***************************************************************/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[])
{
  InstallHRSignalHandler();
  InitializeLog(argv[0]);

  /*--------------------------------------------------------------*/
  /*- process command-line options -------------------------------*/
  /*--------------------------------------------------------------*/
  std::string GeoFileName;   // .scuffgeo file
  int LMax=3;                // maximum L-value of spherical wave computed
  cdouble Omega=0;           // angular frequency at which to run the computation
  std::string OmegaFile;     // list of angular frequencies
  std::string Cache;         // scuff cache file
  std::string FileBase;      // base filename for output file
  bool WriteHDF5Files=false; // write T-matrix data to HDF5 files

  CLI::App App{"compute the T-matrix of an arbitrary scatterer"};
  App.option_defaults()->ignore_case();
  App.add_option("--geometry", GeoFileName, ".scuffgeo file")
     ->required()
     ->take_last();
  App.add_option("--LMax", LMax, "maximum l-value")
     ->take_last();
  scuff::cli::AddComplexOption(App, "--Omega", Omega, "angular frequency");
  App.add_option("--OmegaFile", OmegaFile, "list of angular frequencies")
     ->take_last();
  App.add_option("--Cache", Cache, "scuff cache file")
     ->take_last();
  App.add_option("--FileBase", FileBase, "base filename for output files")
     ->take_last();
  App.add_flag("--WriteHDF5Files", WriteHDF5Files, "write HDF5 output files");

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;

  /*--------------------------------------------------------------*/
  /*- process frequency options ----------------------------------*/
  /*--------------------------------------------------------------*/
  std::unique_ptr<HVector> OmegaVector;
  if (!OmegaFile.empty())
   OmegaVector.reset(new HVector(OmegaFile.c_str()));
  else if ( Omega!=0.0 )
   { OmegaVector.reset(new HVector(1, LHM_COMPLEX));
     OmegaVector->SetEntry(0,Omega);
   }
  else
   { CLI::ValidationError Error("scuff-tmatrix",
        "either --omega or --omegafile must be specified");
     App.exit(Error);
     return 1;
   };

  /*--------------------------------------------------------------*/
  /* create the RWGGeometry from the .scuffgeo file               */
  /*--------------------------------------------------------------*/
  RWGGeometry *G=new RWGGeometry(GeoFileName.c_str());
  G->SetLogLevel(SCUFF_VERBOSELOGGING);
  if (!Cache.empty())
   PreloadCache(Cache.c_str());

  /*--------------------------------------------------------------*/
  /* preallocate BEM matrix and RHS vector                        */
  /*--------------------------------------------------------------*/
  HMatrix *M  = G->AllocateBEMMatrix();
  HVector *KN = G->AllocateRHSVector();

  /*--------------------------------------------------------------*/
  /*- preallocate an HMatrix to store the T-matrix data           */
  /*--------------------------------------------------------------*/
  int NumLMs = (LMax+1)*(LMax+1) - 1;
  int NumMoments= 2*NumLMs;
  HMatrix *TMatrix = new HMatrix(NumMoments, NumMoments, LHM_COMPLEX);

  /*--------------------------------------------------------------*/
  /* instantiate a SphericalWave structure (we will set the L, M, */
  /* and P fields later)                                          */
  /*--------------------------------------------------------------*/
  SphericalWave SW;

  /*--------------------------------------------------------------*/
  /* open output file and write preamble    ----------------------*/
  /*--------------------------------------------------------------*/
  if (FileBase.empty())
   FileBase = GetFileBase(GeoFileName.c_str());
  FILE *f=vfopen("%s.TMatrix","a",FileBase.c_str());
  fprintf(f,"# scuff-tmatrix " VERSIONSTRING " run on %s (%s)\n",GetHostName(),GetTimeString());
  fprintf(f,"# columns:\n");
  fprintf(f,"# 1 omega\n");
  fprintf(f,"# 2,3,4,5 (alpha, {L,M,P}_alpha)   (T-matrix row index)\n");
  fprintf(f,"# 6,7,8,9 ( beta, {L,M,P}_beta)    (T-matrix column index)\n");
  fprintf(f,"# 10, 11  real, imag T_{alpha, beta}\n");

  /*--------------------------------------------------------------*/
  /*- outer loop over frequencies. -------------------------------*/
  /*--------------------------------------------------------------*/
  for(int nOmega=0; nOmega<OmegaVector->N; nOmega++)
   {
     Log("Computing T matrix at frequency %s...",z2s(Omega));

     Omega=OmegaVector->GetEntry(nOmega);
     
     /*----------------------------------------------*/
     /*  Early check of material properties validity */
     /*----------------------------------------------*/
     G->UpdateCachedEpsMuValues(Omega);
     {
       int material_invalid = 0;

       for (int regi = 0; regi < G->NumRegions; ++regi)
         if (    isnan(real(G->EpsTF[regi])) 
	      || isnan(imag(G->EpsTF[regi])) 
	      || isnan(real(G->MuTF[regi]))  
	      || isnan(imag(G->MuTF[regi]))   )
           ++material_invalid;

       if (material_invalid) {
	  Log("Some of the regions contain invalid material properties at frequency %s; giving up.", z2s(Omega));
	  Log(" (Check the frequency ranges in the material data files.)");
	  fprintf(f, "# %s\tnot computed due to invalid material properties;"
		     " check the frequency ranges in the material data files.\n", z2s(Omega));
	  continue;
       }
     }

     /*--------------------------------------------------------------*/
     /* assemble and factorize the BEM matrix at this frequency      */
     /*--------------------------------------------------------------*/
     G->AssembleBEMMatrix(Omega, M);
     M->LUFactorize();

     /*--------------------------------------------------------------*/
     /*- inner loop over incident spherical waves (i.e. over columns */
     /*- of the T matrix; note Betais a running column index)        */
     /*--------------------------------------------------------------*/
     TMatrix->Zero();
     for(int LBeta=1, Beta=0; LBeta<=LMax; LBeta++)
      for(int MBeta=-LBeta; MBeta<=LBeta; MBeta++)
       for(int PBeta=0; PBeta<2; PBeta++, Beta++)
        { 
           SW.SetL(LBeta);
           SW.SetM(MBeta);
           SW.SetP(PBeta);

           // solve the scattering problem for this incident spherical wave
           Log("Solving scattering problem with incident spherical wave #%i: (L,M,P)=(%i,%i,%i)",Beta,LBeta,MBeta,PBeta);
           G->AssembleRHSVector(Omega, &SW, KN);
           M->LUSolve(KN);

           // compute the full vector of spherical multipole moments induced by the
           // incident wave on the object and store it as the Betath column of the T-matrix
           HVector TColumn(NumMoments, LHM_COMPLEX, (cdouble *)TMatrix->GetColumnPointer(Beta));
           GetSphericalMoments(G, Omega, LMax, KN, &TColumn);
        }; // for (nc=l=0...)

     /*--------------------------------------------------------------*/
     /*- write the full content of the T-matrix at this frequency to */
     /*- the text output file                                        */
     /*--------------------------------------------------------------*/
     for(int LAlpha=1, Alpha=0; LAlpha<=LMax; LAlpha++)
      for(int MAlpha=-LAlpha; MAlpha<=LAlpha; MAlpha++)
       for(int PAlpha=0; PAlpha<2; PAlpha++, Alpha++)
        for(int LBeta=1, Beta=0; LBeta<=LMax; LBeta++)
         for(int MBeta=-LBeta; MBeta<=LBeta; MBeta++)
          for(int PBeta=0; PBeta<2; PBeta++, Beta++)
           fprintf(f,"%s  %i %i %+i %i   %i %i %+i %i  %+15.8e %+15.8e\n",
                      z2s(Omega), 
                      Alpha,LAlpha,MAlpha,PAlpha,Beta,LBeta,MBeta,PBeta,
                      real(TMatrix->GetEntry(Alpha,Beta)),
                      imag(TMatrix->GetEntry(Alpha,Beta)));

     if (WriteHDF5Files)
      { std::string FileName=FileBase + "_w" + z2s(Omega) + ".HDF5";
        TMatrix->ExportToHDF5(FileName.c_str(),"T");
      };

    }; // for( nOmega= ... )

  fclose(f);
      
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  if (!Cache.empty())
   StoreCache(Cache.c_str());
  printf("Thank you for your support.\n");
  
}
