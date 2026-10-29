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
 * scuff-neq   -- a standalone code within the scuff-em suite
 *             -- for implementing the fluctuating-surface-current
 *             -- approach to nonequilibrium phenomena (more 
 *             -- specifically, for computing heat radiation, 
 *             -- heat transfer, and nonequilibrium casimir forces) 
 *
 * homer reid  -- 5/2012
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <BZIntegration.h>
#include "scuff-neq.h"
#include <libhrutil.h>

#include <memory>
#include <string>
#include <vector>

#include <ScuffCLI.h>

/***************************************************************/
/***************************************************************/
/***************************************************************/
#define MAXFREQ  10    // max number of frequencies 
#define MAXCACHE 10    // max number of cache files for preload

#define MAXSTR   1000

#define II cdouble(0.0,1.0)

/***************************************************************/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[])
{
  InstallHRSignalHandler();
  InitializeLog(argv[0]);

  /***************************************************************/
  /* process options *********************************************/
  /***************************************************************/
  std::string GeoFile;
  std::string TransFile;
  std::string SourceObject;
  std::string DestObject;
  std::string EPFile;
  std::vector<cdouble> OmegaVals;
  std::string OmegaFile;
  std::string OmegaKBFile;
  bool EMTPFT      = false;
  bool OPFT        = false;
  bool DSIPFT      = false;
  int DSIPoints    = 0;
  int DSIPoints2   = 0;
  std::string DSIMesh;
  double DSIRadius = 0.0;
  bool DSIFarField = false;
  std::string DSIOmegaFile;
  std::string FileBase;
  bool PlotFlux=false;
  bool OmitSelfTerms=false;
  std::string Cache, WriteCache;
  std::vector<std::string> ReadCache;

  CLI::App App{"compute nonequilibrium energy/heat exchange"};
  App.option_defaults()->ignore_case();

  App.add_option("--geometry", GeoFile, "geometry file")
     ->required()
     ->take_last();
  App.add_option("--transfile", TransFile,
                 "list of geometrical transformation")->take_last();
  App.add_option("--sourceobject", SourceObject, "label of source object")->take_last();
  App.add_option("--destobject", DestObject, "label of destination object")->take_last();
  App.add_option("--EPFile", EPFile,
                 "list of evaluation points for spatially-resolved flux")->take_last();
  scuff::cli::AddComplexVectorOption(App, "--Omega", OmegaVals,
                                     "(angular) frequency", MAXFREQ);
  App.add_option("--OmegaFile", OmegaFile, "list of (angular) frequencies")->take_last();
  App.add_option("--OmegaKBFile", OmegaKBFile, "list of (Omega, kx, ky) points")->take_last();
  App.add_option("--FileBase", FileBase,
                 "prefix for names of .log, .flux, .out files")->take_last();
  App.add_flag("--PlotFlux", PlotFlux, "write spatially-resolved flux data");
  App.add_flag("--OmitSelfTerms", OmitSelfTerms,
               "omit contributions of an object to the PFT of itself");
  App.add_flag("--EMTPFT", EMTPFT, "compute SIFlux using EMT method");
  App.add_flag("--OPFT", OPFT, "compute SIFlux using overlap method");
  App.add_flag("--DSIPFT", DSIPFT,
               "compute SIFlux using displaced surface integral method");
  App.add_option("--DSIRadius", DSIRadius,
                 "bounding-sphere radius for DSIPFT")->take_last();
  App.add_option("--DSIPoints", DSIPoints,
                 "number of cubature points for DSIPFT over sphere "
                 "(6, 14, 26, 38, 50, 74, 86, 110, 146, 170, 194, 230, 266, "
                 "302, 350, 434, 590, 770, 974, 1202, 1454, 1730, 2030, "
                 "2354, 2702, 3074, 3470, 3890, 4334, 4802, 5294, 5810)")->take_last();
  App.add_option("--DSIPoints2", DSIPoints2,
                 "number of cubature points for DSIPFT second opinion")->take_last();
  App.add_option("--DSIMesh", DSIMesh,
                 "bounding surface .msh file for DSIPFT")->take_last();
  App.add_option("--DSIOmegaFile", DSIOmegaFile,
                 "list of frequencies at which to perform DSI calculations")->take_last();
  App.add_option("--cache", Cache, "read/write cache")->take_last();
  App.add_option("--readcache", ReadCache, "read cache")->expected(1, MAXCACHE);
  App.add_option("--writecache", WriteCache, "write cache")->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;

  /*******************************************************************/
  /*******************************************************************/
  /*******************************************************************/
  if (FileBase.empty())
   FileBase=GetFileBase(GeoFile.c_str());

  if ( !Cache.empty() && !WriteCache.empty() )
   ErrExit("--cache and --writecache options are mutually exclusive");

  if (DSIPFT && DSIPoints==0)
   DSIPoints=230;

  /*******************************************************************/
  /* determine which PFT methods were requested       ****************/
  /*******************************************************************/
  std::vector<int> PFTMethods;
  if(EMTPFT)
   PFTMethods.push_back(SCUFF_PFT_EMT);
  if(OPFT)
   PFTMethods.push_back(SCUFF_PFT_OVERLAP);
  if(!DSIMesh.empty())
   PFTMethods.push_back(-SCUFF_PFT_DSI);
  if(DSIPoints)
   PFTMethods.push_back(DSIPoints);
  if(DSIPoints2)
   PFTMethods.push_back(DSIPoints2);

  if ( PFTMethods.empty() && EPFile.empty() )
   PFTMethods.push_back(SCUFF_PFT_EMT);

  if (!DSIMesh.empty())
   PlotFlux=true;

  /*******************************************************************/
  /*******************************************************************/
  /*******************************************************************/
  std::unique_ptr<HMatrix> OmegaKBPoints;
  if (!OmegaKBFile.empty())
   { OmegaKBPoints.reset(new HMatrix(OmegaKBFile.c_str(),LHM_TEXT,"--ncol 3"));
     if (OmegaKBPoints->ErrMsg)
      ErrExit("%s",OmegaKBPoints->ErrMsg);
   };

  /*******************************************************************/
  /* process frequency-related options to construct a list of        */
  /* frequencies at which to run simulations                         */
  /*******************************************************************/
  std::unique_ptr<HVector> OmegaPoints;
  int NumFreqs=0;
  if (!OmegaFile.empty()) // first process --OmegaFile option if present
   {
     OmegaPoints.reset(new HVector(OmegaFile.c_str(),LHM_TEXT));
     if (OmegaPoints->ErrMsg)
      ErrExit("%s",OmegaPoints->ErrMsg);
     NumFreqs=OmegaPoints->N;
     Log("Read %i frequencies from file %s.",NumFreqs,OmegaFile.c_str());
   };

  // now add any individually specified --Omega options
  if (OmegaVals.size()>0)
   {
     std::unique_ptr<HVector> OmegaPoints0(std::move(OmegaPoints));
     NumFreqs += (int)OmegaVals.size();
     OmegaPoints.reset(new HVector(NumFreqs, LHM_COMPLEX));
     int nFreq=0;
     if (OmegaPoints0)
      { for(nFreq=0; nFreq<OmegaPoints0->N; nFreq++)
         OmegaPoints->SetEntry(nFreq, OmegaPoints0->GetEntry(nFreq));
      };
     for(size_t nOV=0; nOV<OmegaVals.size(); nOV++)
      OmegaPoints->SetEntry(nFreq+(int)nOV, OmegaVals[nOV]);
     Log("Read %i frequencies from command line.",(int)OmegaVals.size());
   };

  /*******************************************************************/
  /* create the SNEQData structure that contains all the info needed */
  /* to evaluate the neq transfer at a single frequency              */
  /*******************************************************************/
  SNEQData *SNEQD=CreateSNEQData(GeoFile.c_str(), TransFile.c_str(),
                                 PFTMethods.data(), (int)PFTMethods.size(),
                                 EPFile.empty() ? nullptr : EPFile.c_str(),
                                 FileBase.c_str());
  RWGGeometry *G=SNEQD->G;
  SNEQD->PlotFlux                = PlotFlux;
  SNEQD->OmitSelfTerms           = OmitSelfTerms;
  SNEQD->PFTOpts.DSIMesh         = DSIMesh.empty() ? nullptr : DSIMesh.c_str();
  SNEQD->PFTOpts.DSIRadius       = DSIRadius;
  SNEQD->PFTOpts.DSIFarField     = DSIFarField;
  SNEQD->DSIOmegaPoints          = DSIOmegaFile.empty() ? nullptr
                                  : new HVector(DSIOmegaFile.c_str());

  if (OmegaKBPoints.get()!=nullptr && !G->LBasis)
   ErrExit("--OmegaKBPoints may only be used with extended geometries");
  else if (G->LBasis && OmegaKBPoints.get()==nullptr)
   ErrExit("--OmegaKBPoints is required for extended geometries");

  SNEQD->SourceOnly=-1;
  if (!SourceObject.empty())
   { G->GetSurfaceByLabel(SourceObject.c_str(), &(SNEQD->SourceOnly) );
     if (SNEQD->SourceOnly == -1)
      ErrExit("geometry contains no object with label %s",SourceObject.c_str());
     Log("Computing only quantities sourced by object %s.",SourceObject.c_str());
   };
  SNEQD->DestOnly=-1;
  if (!DestObject.empty())
   { G->GetSurfaceByLabel(DestObject.c_str(), &(SNEQD->DestOnly) );
     if (SNEQD->DestOnly == -1)
      ErrExit("geometry contains no object with label %s",DestObject.c_str());
     Log("Computing only PFT quantities for object %s.",DestObject.c_str());
   };

  /*******************************************************************/
  /* preload the scuff cache with any cache preload files the user   */
  /* may have specified                                              */
  /*******************************************************************/
  for (size_t nrc=0; nrc<ReadCache.size(); nrc++)
   PreloadCache( ReadCache[nrc].c_str() );
  if (!Cache.empty())
   PreloadCache( Cache.c_str() );

  if (!Cache.empty()) WriteCache=Cache;
  SNEQD->WriteCache = WriteCache.empty() ? nullptr : WriteCache.c_str();

  /*******************************************************************/
  /* now switch off based on the requested frequency behavior to     */
  /* perform the actual calculations.                                */
  /*******************************************************************/
  if (OmegaKBPoints.get()!=nullptr)
   { for (int nok=0; nok<OmegaKBPoints->NR; nok++)
      {
        cdouble Omega;
        double kBloch[2];
        Omega     = OmegaKBPoints->GetEntryD(nok, 0);
        kBloch[0] = OmegaKBPoints->GetEntryD(nok, 1);
        kBloch[1] = OmegaKBPoints->GetEntryD(nok, 2);
        WriteFlux(SNEQD, Omega, kBloch);
      };
   }
  else
   for (int nFreq=0; nFreq<NumFreqs; nFreq++)
    WriteFlux(SNEQD, OmegaPoints->GetEntry(nFreq));

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  printf("Thank you for your support.\n");

}
