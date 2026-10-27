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
#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <memory>
#include <string>
#include <vector>

#include <ScuffCLI.h>
#include <libhrutil.h>
#include <libscuff.h>
#include <libscuffInternals.h>
#include <libSGJC.h>
#include <BZIntegration.h>
#include "scuff-ldos.h"

#define II cdouble(0.0,1.0)
#define MAXEPFILES 100
#define MAXFREQ    10

using namespace scuff;

/***************************************************************/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[])
{
  InstallHRSignalHandler();
  InitializeLog(argv[0]);

  /***************************************************************/
  /* pre-process command-line arguments to extract the arguments */
  /* relevant for Brillouin-zone integration.  InitBZIArgs       */
  /* consumes its --BZI* options by nulling their argv slots,    */
  /* which the typed option reader below then skips, exactly as  */
  /* the legacy argv reader did.                                 */
  /***************************************************************/
  GetBZIArgStruct *BZIArgs=InitBZIArgs(argc, argv);

  /***************************************************************/
  /* process remaining command-line arguments ********************/
  /***************************************************************/
  std::string GeoFile;
  std::vector<cdouble> OmegaVals;
  std::vector<cdouble> LambdaVals;
  std::string OmegaFile;
  std::string LambdaFile;
  std::string OkBFile;
  std::vector<std::string> EPFiles;
  std::string TransFile;
  bool GroundPlane=false;
  std::string HalfSpace;
  bool SkipBZIntegration=false;
  double RelTol=1.0e-2;
  double AbsTol=1.0e-8;
  int MaxEvals=10000;
  std::string FileBase;
  bool LDOSOnly=false;
  bool FullTPDGF=false;

  CLI::App App{"compute the local density of states and two-point \
differential Green's functions of a compact or periodic object"};
  App.option_defaults()->ignore_case();

  App.add_option("--geometry", GeoFile, ".scuffgeo file")
     ->take_last();
  App.add_option("--HalfSpace", HalfSpace,
                 "simulate an infinite half-space for z<0 with the given material")
     ->take_last();
  App.add_flag("--GroundPlane", GroundPlane,
               "simulate an infinite PEC ground plane at z=0");
  App.add_flag("--SkipBZIntegration", SkipBZIntegration,
               "bypass BZ integration for analytical half-space/ground-plane calculation");
  App.add_option("--EPFile", EPFiles, "list of evaluation points")
     ->expected(1, MAXEPFILES);
  App.add_option("--TransFile", TransFile,
                 "list of geometrical transformations")
     ->take_last();
  scuff::cli::AddComplexVectorOption(App, "--Omega", OmegaVals,
                                     "angular frequency", MAXFREQ);
  scuff::cli::AddComplexVectorOption(App, "--Lambda", LambdaVals,
                                     "(free-space) wavelength", MAXFREQ);
  App.add_option("--OmegaFile", OmegaFile, "list of omega points ")
     ->take_last();
  App.add_option("--LambdaFile", LambdaFile,
                 "list of (free-space) wavelengths")
     ->take_last();
  App.add_option("--OmegakBlochFile", OkBFile, "list of (omega,kBloch) points ")
     ->take_last();
  App.add_option("--RelTol", RelTol, "relative tolerance")
     ->take_last();
  App.add_option("--AbsTol", AbsTol, "absolute tolerance")
     ->take_last();
  App.add_option("--MaxEvals", MaxEvals,
                 "maximum number of integrand/summand evaluation")
     ->take_last();
  App.add_option("--FileBase", FileBase, "base name for output files")
     ->take_last();
  App.add_flag("--LDOSOnly", LDOSOnly,
               "omit DGF components from Brillouin-zone integration");
  App.add_flag("--FullTPDGF", FullTPDGF,
               "compute full (bare+scattered) two-point DGF (default is scattering part only)");

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (GeoFile.empty() && !SkipBZIntegration)
   { CLI::ValidationError Error("scuff-ldos","--geometry option is mandatory");
     App.exit(Error);
     return 1;
   };
  if (EPFiles.empty())
   { CLI::ValidationError Error("scuff-ldos",
        "you must specify at least one --EPFile");
     App.exit(Error);
     return 1;
   };
  if (FileBase.empty() && !GeoFile.empty())
   FileBase=GetFileBase(GeoFile.c_str());
  if (!HalfSpace.empty())
   FileBase = FileBase + "." + HalfSpace;
  if (GroundPlane)
   FileBase = FileBase + ".GroundPlane";

  std::vector<const char *> EPFilePtrs;
  for(const std::string &EPFile : EPFiles)
   EPFilePtrs.push_back(EPFile.c_str());

  /***************************************************************/
  /* process --Omega, --OmegaFile, --OmegakBlochFile arguments   */
  /***************************************************************/
  std::unique_ptr<HMatrix> OkBPoints;
  std::unique_ptr<HVector> OmegaPoints;
  if (!OkBFile.empty())
   { OkBPoints.reset(new HMatrix(OkBFile.c_str()));
     if (OkBPoints->ErrMsg)
      ErrExit("%s",OkBPoints->ErrMsg);
   }
  else
   { OmegaPoints.reset(
      GetOmegaList(OmegaFile.empty() ? 0 : OmegaFile.c_str(),
                   OmegaVals.data(), (int)OmegaVals.size(),
                   LambdaFile.empty() ? 0 : LambdaFile.c_str(),
                   LambdaVals.data(), (int)LambdaVals.size()) );
     if (OmegaPoints.get()==0)
      ErrExit("you must specify at least one frequency");
   };

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (SkipBZIntegration)
   { if (OkBPoints.get()!=0)
      ErrExit("--SkipBZIntegration is incompatible with --OmegakBlochFile");
     if ( !GroundPlane && HalfSpace.empty() )
      ErrExit("--SkipBZIntegration requires either --HalfSpace or --GroundPlane");
     ProcessHalfSpaceDGFs(OmegaPoints.get(), EPFilePtrs.data(), (int)EPFiles.size(),
                          HalfSpace.empty() ? 0 : HalfSpace.c_str(),
                          RelTol, AbsTol, MaxEvals);
     return 0;   /* was exit(1), reporting failure on the success path */
   };

  /***************************************************************/
  /* create the SLDData structure that will be passed to all     */
  /* computational routines                                      */
  /***************************************************************/
  SLDData *Data     = CreateSLDData(GeoFile.c_str(),
                                    TransFile.empty() ? 0 : TransFile.c_str(),
                                    EPFilePtrs.data(), (int)EPFiles.size());
  Data->RelTol      = RelTol;
  Data->AbsTol      = AbsTol;
  Data->MaxEvals    = MaxEvals;
  Data->FileBase    = FileBase.c_str();
  Data->LDOSOnly    = LDOSOnly;
  Data->ScatteringOnly = !FullTPDGF;
  Data->GroundPlane = GroundPlane;
  Data->HalfSpaceMP = HalfSpace.empty() ? 0 : new MatProp(HalfSpace.c_str());

  // set LDOSOnly = false if any EPFiles have 6 coordinates
  // (for two-point DGF calculations)
  for(int n=0; n<Data->NumXMatrices; n++)
   if (Data->XMatrices[n]->NC==6)
    Data->LDOSOnly=false;

  int LDim = Data->G->LDim;
  if (!HalfSpace.empty() && LDim!=2)
   { CLI::ValidationError Error("scuff-ldos",
        "--HalfSpace requires a 2D-periodic geometry unless you also say --SkipBZIntegration");
     App.exit(Error);
     return 1;
   };

  int NX         = Data->TotalEvalPoints;
  int NFun       = Data->LDOSOnly ? 2 : 38; // # outputs per eval pt
  int FDim       = NX*NFun;
  std::vector<double> Result(FDim, 0.0);

  /***************************************************************/
  /* now switch off to figure out what to do:                    */
  /*  1. if we have a non-periodic geometry, simply evaluate     */
  /*     the LDOS at each evaluation point at each frequency.    */
  /*                                                             */
  /*  2. if we have a periodic geometry and the user specified   */
  /*      a list of (Omega, kBloch) points, evaluate the LDOS    */
  /*      at each evaluation point at each (Omega, kBloch) point.*/
  /*                                                             */
  /*  3. otherwise (we have a periodic geometry and the user     */
  /*     did not request specific kBloch points), for each       */
  /*     frequency we perform a Brillouin-zone integral to get   */
  /*     the LDOS at each evaluation point.                      */
  /***************************************************************/

  /*--------------------------------------------------------------*/
  /*- non-PBC geometry: just do non-periodic DGF calculations at  */
  /*-                   each requested Omega value                */
  /*--------------------------------------------------------------*/
  if (LDim==0)
   {
     for(int no=0; no<OmegaPoints->N; no++)
      GetLDOS( (void *)Data, OmegaPoints->GetEntry(no), 0, Result.data());
   }
  /*--------------------------------------------------------------*/
  /*- PBC structure with specified kBloch points: do a periodic   */
  /*- DGF calculation at each (Omega, kBloch) point               */
  /*--------------------------------------------------------------*/
  else if (OkBPoints.get()!=0)
   {
     for(int nokb=0; nokb<OkBPoints->NR; nokb++)
      {
        cdouble Omega=OkBPoints->GetEntry(nokb,0);

        double kBloch[3]={0.0, 0.0, 0.0};
        for(int d=0; d<LDim; d++)
         kBloch[d]=OkBPoints->GetEntryD(nokb,1+d);

        GetLDOS( (void *)Data, Omega, kBloch, Result.data());
      };
   }
  /*--------------------------------------------------------------*/
  /*- PBC structure without specified kBloch points: perform a    */
  /*- Brillouin-zone integration at each requested Omega value    */
  /*--------------------------------------------------------------*/
  else
   {
     /***************************************************************/
     /* complete the argument structure for Brillouin-zone          */
     /* that we started to initialize above                         */
     /***************************************************************/
     BZIArgs->BZIFunc     = GetLDOS;
     BZIArgs->UserData    = (void *)Data;
     BZIArgs->FDim        = FDim;
     UpdateBZIArgs(BZIArgs, Data->G->RLBasis, Data->G->RLVolume);

     /***************************************************************/
     /***************************************************************/
     /***************************************************************/
     for(int no=0; no<OmegaPoints->N; no++)
      { cdouble Omega=OmegaPoints->GetEntry(no);
        Log("Evaluating Brillouin-zone integral at omega=%s",z2s(Omega));
        GetBZIntegral(BZIArgs, Omega, Result.data());
        WriteData(Data, Omega, 0, FILETYPE_LDOS, Result.data(), BZIArgs->BZIError);
      };
   };

  DestroyBZIArgs(BZIArgs);
  return 0;
}
