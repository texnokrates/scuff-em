/*
 * GetTMatrix.cc -- a slightly more complicated example of how to use the
 *               -- scuff-EM C++ API
 *
 *               -- This program computes the T-matrix, in a spherical-wave basis,  
 *               -- for an arbitary scatterer. (What this means is the following: 
 *               -- For each spherical wave up to a user-specified maximum L value, 
 *               -- the code scatters that wave off of the geometry and decomposes
 *               -- the resulting scattered field into a basis of spherical waves.)
 *               -- The implementation of this code demonstrates, among other things, 
 *               -- how to define your own type of incident field routine.
 *
 * Homer Reid    -- 6/2012
 */

#include <stdio.h>
#include <stdlib.h>

#include <libscuff.h>
#include "GetTMatrix.h"

using namespace scuff;

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
  std::string GeoFileName;  // .scuffgeo file
  int lMax=5;               // maximum L-value of spherical wave computed
  cdouble Omega=0;          // angular frequency at which to run the computation
  std::string OmegaFile;    // list of angular frequencies
  std::string Cache;        // scuff cache file
  CLI::App App{"compute the T-matrix of an arbitrary scatterer"};
  App.option_defaults()->ignore_case();
  App.add_option("--geometry", GeoFileName, ".scuffgeo file")->required()->take_last();
  App.add_option("--LMax", LMax, "maximum l-value")->take_last();
  scuff::cli::AddComplexOption(App, "--Omega", Omega, "angular frequency");
  App.add_option("--OmegaFile", OmegaFile, "list of angular frequencies")->take_last();
  App.add_option("--Cache", Cache, "scuff cache file")->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;

  /*--------------------------------------------------------------*/
  /*- process frequency options ----------------------------------*/
  /*--------------------------------------------------------------*/
  HVector *OmegaVector=0;
  if (!OmegaFile.empty())
   OmegaVector=new HVector(OmegaFile.c_str());
  else if ( Omega!=0.0 )
   { OmegaVector=new HVector(1, LHM_COMPLEX);
     OmegaVector->SetEntry(0,Omega);
   }
  else
   { CLI::ValidationError Error("GetTMatrix",
        "either --omega or --omegafile must be specified");
     App.exit(Error);
     return 1;
   }

  /*--------------------------------------------------------------*/
  /* create the RWGGeometry from the .scuffgeo file               */
  /*--------------------------------------------------------------*/
  SetLogFileName("%s.log",GetFileBase(GeoFileName.c_str()));
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
  int NumMoments= 2*(lMax+1)*(lMax+1);
  HMatrix *TMatrix = new HMatrix(NumMoments, NumMoments, LHM_COMPLEX);
  HVector *AVector = new HVector(NumMoments, LHM_COMPLEX);

  /*--------------------------------------------------------------*/
 /* instantiate a SphericalWave structure (we will set the l, m, */
  /* and Type fields later)                                       */
  /*--------------------------------------------------------------*/
  SphericalWave SW;

  /*--------------------------------------------------------------*/
  /*- outer loop over frequencies --------------------------------*/
  /*--------------------------------------------------------------*/
  const char *TypeChar="ME";
  FILE *TextOutputFile=vfopen("%s.TMatrix","w",GetFileBase(GeoFileName.c_str()));
  for(int nOmega=0; nOmega<OmegaVector->N; nOmega++)
   {
     Log("Computing T matrix at frequency %s...",z2s(Omega));

     /*--------------------------------------------------------------*/
     /* assemble and factorize the BEM matrix at this frequency      */
     /*--------------------------------------------------------------*/
     Omega=OmegaVector->GetEntry(nOmega);
     G->AssembleBEMMatrix(Omega, M);
     M->LUFactorize();

     /*--------------------------------------------------------------*/
     /*- inner loop over incident spherical waves (i.e. over columns-*/
     /*- of the T matrix; note nc is a running column index)        -*/
     /*--------------------------------------------------------------*/
     TMatrix->Zero();
     for(int nc=0, l=0; l<=lMax; l++)
      for(int m=-l; m<=l; m++)
       for(int Type=SW_MAGNETIC; Type<=SW_ELECTRIC; Type++, nc++)
        { 
          if (l==0) 
           continue;

           SW.SetType(Type);
           SW.SetL(l);
           SW.SetM(m);

           // solve the scattering problem for this incident spherical wave
           Log("Solving scattering problem with incident spherical wave P(l,m)=%c(%i,%i)",TypeChar[Type],l,m);
           G->AssembleRHSVector(Omega, &SW, KN);
           M->LUSolve(KN);

           // compute the spherical multipole moments induced by the 
           // incident wave on the object 
           GetSphericalMoments(G, Omega, lMax, KN, AVector);

           // stamp in the vector of moments as the ncth row of the T-matrix
           for(nr=0; nr<NumMoments; nr++)
            TMatrix->SetEntry(nr, nc, AVector->GetEntry(nr));

        } // for (nc=l=0...)

     /*--------------------------------------------------------------*/
     /*- write the full content of the T-matrix at this frequency to */
     /*- the text output file                                        */
     /*--------------------------------------------------------------*/
     for(int nr=0, l=0; l<=lMax; l++)
      for(int m=-l; m<=l; m++)
       for(int Type=SW_MAGNETIC; Type<=SW_ELECTRIC; Type++, nr++)
        for(int nc=0, lP=0; lP<=lMax; lP++)
         for(int mP=-lP; mP<=lP; mP++)
          for(int TypeP=SW_MAGNETIC; TypeP<=SW_ELECTRIC; TypeP++, nc++)
           { 
             if (l==0 || lP==0) continue;

             fprintf(TextOutputFile,"%s %c %i %i %c %i %i  %+15.8e %+15.8e\n", z2s(Omega),
                                     TypeChar[Type],l,m, TypeChar[TypeP],lP,mP,
                                     real(TMatrix->GetEntry(nr,nc)), 
                                     imag(TMatrix->GetEntry(nr,nc)));
           }

    } // for( nOmega= ... )

  fclose(TextOutputFile);
      
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  if (!Cache.empty())
   StoreCache(Cache.c_str());
  printf("Thank you for your support.\n");
  
}
