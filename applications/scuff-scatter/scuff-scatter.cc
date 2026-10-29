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
 * scuff-scatter.cc  -- a standalone code within the scuff-EM suite for
 *                   -- solving problems involving the scattering of
 *                   -- electromagnetic radiation from an arbitrary
 *                   -- compact object
 *
 * homer reid        -- 10/2006 -- 1/2012
 *
 * documentation at: http://homerreid.com/scuff-em/
 *
 */
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <fenv.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "ScuffCLI.h"
#include "scuff-scatter.h"

/***************************************************************/
/* legacy limits on the number of occurrences of the repeatable*/
/* options (kept as caps on the corresponding vectors)         */
/***************************************************************/
#define MAXPW    10    // max number of plane waves
#define MAXGB    1     // max number of gaussian beams
#define MAXPS    10    // max number of point sources
#define MAXFREQ  10    // max number of frequencies
#define MAXEPF   10    // max number of evaluation-point files
#define MAXFVM   10    // max number of field visualization meshes
#define MAXCACHE 10    // max number of cache files for preload

#define MAXSTR   1000

/***************************************************************/
/* Deletes an IncFieldList created by ReadIncFieldList or      */
/* AddIncFieldToList: the list owns each incident-field chain  */
/* (DeleteIncFieldChain walks the ->Next links) and the strdup'd*/
/* labels.                                                     */
/***************************************************************/
struct IncFieldListDeleter
{
  void operator()(IncFieldList *List) const
  {
    if (List==nullptr)
     return;
    /* this is the custom deleter for the malloc'd IncFieldList record:
       the free calls here are the ownership implementation, not legacy
       manual memory management */
    for(int n=0; n<List->NumIFs; n++) // NOLINT(cppcoreguidelines-owning-memory)
     { DeleteIncFieldChain(List->IFs[n]);
       if (List->Labels[n])
        free(List->Labels[n]); // NOLINT(cppcoreguidelines-no-malloc)
     };
    free(List->IFs);    // NOLINT(cppcoreguidelines-no-malloc,bugprone-multi-level-implicit-pointer-conversion)
    free(List->Labels); // NOLINT(cppcoreguidelines-no-malloc)
    free(List);         // NOLINT(cppcoreguidelines-no-malloc)
  }
};

/***************************************************************/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[])
{
  /***************************************************************/
  /* process options *********************************************/
  /***************************************************************/
  InstallHRSignalHandler();
  InitializeLog(argv[0]);

  std::string GeoFile;
  std::vector<cdouble> OmegaVals;
  std::string OmegaFile;
  std::vector<cdouble> LambdaVals;
  std::string LambdaFile;
  std::vector<double> pwDirection;      // 3 values per occurrence
  std::vector<cdouble> pwPolarization;  // 3 values per occurrence
  std::vector<double> gbDirection;      // 3 values, single occurrence
  std::vector<cdouble> gbPolarization;  // 3 values, single occurrence
  std::vector<double> gbCenter;         // 3 values, single occurrence
  double gbWaist=0.0;
  std::vector<double> psLocation;       // 3 values per occurrence
  std::vector<cdouble> psStrength;      // 3 values per occurrence
  std::string IFFile;
  std::string TransFile;
  std::vector<std::string> EPFiles;
  std::string FileBase;
  std::vector<std::string> FVMeshes, FVMeshTransFiles, FVFuncs;
  std::string PFTFile, OPFTFile, MomentPFTFile, EMTPFTFile, DSIPFTFile;
  double DSIRadius = 0.0;
  int DSIPoints    = 302;
  int DSIPoints2   = 0;
  std::string DSIMesh;
  bool DSIFarField = false;
  bool GetRegionPFTs = false;
  bool PlotPFTFlux=false;
  std::string MomentFile, PSDFile;
  bool PlotSurfaceCurrents=false;
  std::string HDF5File;
  std::string Cache, WriteCache;
  std::vector<std::string> ReadCache;
  std::string LogLevel;

  CLI::App App{"solve scattering problems for arbitrary compact objects"};
  App.option_defaults()->ignore_case();

  App.add_option("--geometry", GeoFile, "geometry file")
     ->required()
     ->take_last();
  scuff::cli::AddComplexVectorOption(App, "--Omega", OmegaVals,
                                     "(angular) frequency", MAXFREQ);
  App.add_option("--OmegaFile", OmegaFile, "file listing angular frequencies")
     ->take_last();
  scuff::cli::AddComplexVectorOption(App, "--Lambda", LambdaVals,
                                     "wavelength", MAXFREQ);
  App.add_option("--LambdaFile", LambdaFile, "file listing wavelengths")
     ->take_last();

  App.add_option("--pwDirection", pwDirection, "plane wave direction")
     ->type_size(3)
     ->expected(1, MAXPW);
  scuff::cli::AddComplexVectorOption(App, "--pwPolarization", pwPolarization,
                                     "plane wave polarization", MAXPW, 3);

  App.add_option("--gbDirection", gbDirection, "gaussian beam direction")
     ->type_size(3)
     ->expected(1, MAXGB);
  scuff::cli::AddComplexVectorOption(App, "--gbPolarization", gbPolarization,
                                     "gaussian beam polarization", MAXGB, 3);
  App.add_option("--gbCenter", gbCenter, "gaussian beam center")
     ->type_size(3)
     ->expected(1, MAXGB);
  App.add_option("--gbWaist", gbWaist, "gaussian beam waist")
     ->take_last();

  App.add_option("--psLocation", psLocation, "point source location")
     ->type_size(3)
     ->expected(1, MAXPS);
  scuff::cli::AddComplexVectorOption(App, "--psStrength", psStrength,
                                     "point source strength", MAXPS, 3);

  App.add_option("--IFFile", IFFile, "list of incident fields")
     ->take_last();

  App.add_option("--TransFile", TransFile, "list of geometrical transformations")
     ->take_last();

  App.add_option("--EPFile", EPFiles, "list of evaluation points")
     ->expected(1, MAXEPF);
  App.add_option("--FileBase", FileBase, "base filename for EPFile output")
     ->take_last();

  App.add_option("--FVMesh", FVMeshes, "field visualization mesh")
     ->expected(1, MAXFVM);
  App.add_option("--FVMeshTransFile", FVMeshTransFiles,
                 "list of geometrical transformations applied to FVMesh")
     ->expected(1, MAXFVM);
  App.add_option("--FVFunctions", FVFuncs,
                 "comma-separated list of field functions to visualize")
     ->expected(1, MAXFVM);

  App.add_option("--PFTFile", PFTFile,
                 "name of power, force, and torque output file")
     ->take_last();

  App.add_option("--OPFTFile", OPFTFile, "name of overlap PFT output file")
     ->take_last();
  App.add_option("--MomentPFTFile", MomentPFTFile,
                 "name of multipole-moment PFT output file")
     ->take_last();
  App.add_option("--EMTPFTFile", EMTPFTFile,
                 "name of energy/momentum-transfer PFT output file")
     ->take_last();
  App.add_option("--DSIPFTFile", DSIPFTFile,
                 "name of displaced surface-integral PFT output file")
     ->take_last();
  App.add_option("--DSIMesh", DSIMesh, "mesh file for surface-integral PFT")
     ->take_last();
  App.add_option("--DSIRadius", DSIRadius,
                 "radius of bounding sphere for surface-integral PFT")
     ->take_last();
  App.add_option("--DSIPoints", DSIPoints,
                 "number of quadrature points for surface-integral PFT "
                 "(6, 14, 26, 38, 50, 74, 86, 110, 146, 170, 194, 230, 266, "
                 "302, 350, 434, 590, 770, 974, 1202, 1454, 1730, 2030, "
                 "2354, 2702, 3074, 3470, 3890, 4334, 4802, 5294, 5810)")
     ->take_last();
  App.add_option("--DSIPoints2", DSIPoints2,
                 "number of quadrature points for DSIPFT second opinion")
     ->take_last();
  App.add_flag("--DSIFarField", DSIFarField,
               "retain only far-field contributions to DSIPFT");

  App.add_flag("--GetRegionPFTs", GetRegionPFTs, "report PFTs for each region");
  App.add_flag("--PlotPFTFlux", PlotPFTFlux,
               "generate plots of spatially-resolved PFT flux");

  App.add_option("--MomentFile", MomentFile, "name of dipole moment output file")
     ->take_last();
  App.add_option("--PSDFile", PSDFile, "name of panel source density file")
     ->take_last();
  App.add_flag("--PlotSurfaceCurrents", PlotSurfaceCurrents,
               "generate surface current visualization files");

  App.add_option("--HDF5File", HDF5File,
                 "name of HDF5 file for BEM matrix/vector export")
     ->take_last();

  App.add_option("--LogLevel", LogLevel, "none | terse | verbose | verbose2")
     ->take_last();

  App.add_option("--Cache", Cache, "read/write cache")
     ->take_last();
  App.add_option("--ReadCache", ReadCache, "read cache")
     ->expected(1, MAXCACHE);
  App.add_option("--WriteCache", WriteCache, "write cache")
     ->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;

  /*******************************************************************/
  /* process frequency-related options                               */
  /*******************************************************************/
  std::unique_ptr<HVector> OmegaList(
    GetOmegaList(OmegaFile.empty() ? nullptr : OmegaFile.c_str(),
                 OmegaVals.data(), (int)OmegaVals.size(),
                 LambdaFile.empty() ? nullptr : LambdaFile.c_str(),
                 LambdaVals.data(), (int)LambdaVals.size()));
  if (OmegaList.get()==nullptr)
   { CLI::ValidationError Error("scuff-scatter",
        "you must specify at least one frequency");
     App.exit(Error);
     return 1;
   };

  /*******************************************************************/
  /* process incident-field-related options to construct the data    */
  /* used to generate the incident field in our scattering problem.  */
  /*******************************************************************/
  if ( pwPolarization.size() != pwDirection.size() )
   ErrExit("numbers of --pwPolarization and --pwDirection options must agree");
  /* the legacy parser read an uninitialized stack value for the waist
     when the other gaussian-beam options were given without --gbWaist;
     it is now required, matching the documented agreement rule */
  bool gbWaistSpecified = App.get_option("--gbWaist")->count() > 0;
  if (    gbPolarization.size() != gbDirection.size()
       || gbDirection.size() != gbCenter.size()
       || (gbCenter.size()>0 && !gbWaistSpecified) )
   ErrExit("numbers of --gbPolarization, --gbDirection, --gbCenter, and --gbWaist options must agree ");
  if ( psLocation.size() != psStrength.size() )
   ErrExit("numbers of --psLocation and --psStrength options must agree");

  IncField *IF=nullptr;
  for(size_t npw=0; npw<pwPolarization.size()/3; npw++)
   { PlaneWave *PW=new PlaneWave(pwPolarization.data() + 3*npw,
                                 pwDirection.data() + 3*npw);
     PW->Next=IF;
     IF=PW;
   };
  for(size_t ngb=0; ngb<gbCenter.size()/3; ngb++)
   { GaussianBeam *GB=new GaussianBeam(gbCenter.data() + 3*ngb,
                                      gbDirection.data() + 3*ngb,
                                      gbPolarization.data() + 3*ngb,
                                      gbWaist);
     GB->Next=IF;
     IF=GB;
   };
  for(size_t nps=0; nps<psLocation.size()/3; nps++)
   { PointSource *PS=new PointSource(psLocation.data() + 3*nps,
                                     psStrength.data() + 3*nps);
     PS->Next=IF;
     IF=PS;
   };

  std::unique_ptr<IncFieldList, IncFieldListDeleter> IFListOwner;
  if ( !IFFile.empty() && IF!=nullptr )
   ErrExit("--IFFile is incompatible with other incident-field specifications");
  else if ( !IFFile.empty() )
   IFListOwner.reset( ReadIncFieldList(IFFile.c_str()) );
  else if ( IF!=nullptr )
   IFListOwner.reset( AddIncFieldToList(IF,"Default") );
  IncFieldList *IFList=IFListOwner.get();

  /*******************************************************************/
  /* make --PFTFile synonymous with --EMTPFTFile *********************/
  /*******************************************************************/
  if ( !PFTFile.empty() )
   { if ( !EMTPFTFile.empty() )
      Warn("specifying both --PFTFile and --EMTPFTFile is redundant (ignoring --PFTFile)");
     else
      EMTPFTFile=PFTFile;
   };

  /*******************************************************************/
  /* sanity check to make sure the user specified an incident field  */
  /* if one is required for the outputs the user requested           */
  /*******************************************************************/
  bool NeedIncidentField = (    !MomentFile.empty()
                             || !OPFTFile.empty()
                             || !MomentPFTFile.empty()
                             || !EMTPFTFile.empty()
                             || !DSIPFTFile.empty()
                             || EPFiles.size()>0
                             || FVMeshes.size()>0
                             || PlotSurfaceCurrents
                           );
  if ( NeedIncidentField && IFList==nullptr )
   ErrExit("you must specify at least one incident field source");

  /*******************************************************************/
  /*******************************************************************/
  /*******************************************************************/
  if ( FVMeshTransFiles.size()>0 && FVMeshes.size()!=FVMeshTransFiles.size() )
   ErrExit("--FVMeshTransFile must be specified for every --FVMesh");
  if ( FVFuncs.size()>0 && FVMeshes.size()!=FVFuncs.size() )
   ErrExit("--FVFuncs must be specified for every --FVMesh");

  /*******************************************************************/
  /* PFT options *****************************************************/
  /*******************************************************************/
  PFTOptions MyPFTOpts, *PFTOpts=&MyPFTOpts;
  InitPFTOptions(PFTOpts);
  PFTOpts->DSIMesh       = DSIMesh.empty() ? nullptr : DSIMesh.c_str();
  PFTOpts->DSIRadius     = DSIRadius;
  PFTOpts->DSIPoints     = DSIPoints;
  PFTOpts->DSIFarField   = DSIFarField;
  PFTOpts->GetRegionPFTs = GetRegionPFTs;

  std::string DSIPFTFile2;
  if ( !DSIPFTFile.empty() && DSIPoints2 )
   DSIPFTFile2=std::string(GetFileBase(DSIPFTFile.c_str())) + ".DSI"
               + std::to_string(DSIPoints2);

  /*******************************************************************/
  /* create the SSData structure containing everything we need to    */
  /* execute scattering calculations                                 */
  /*******************************************************************/
  SSData MySSData, *SSD=&MySSData;

  std::unique_ptr<RWGGeometry> G(new RWGGeometry(GeoFile.c_str()));
  std::unique_ptr<HMatrix> M(G->AllocateBEMMatrix());
  std::unique_ptr<HVector> RHS(G->AllocateRHSVector());
  std::unique_ptr<HVector> KN(G->AllocateRHSVector());
  SSD->G   = G.get();
  SSD->M   = M.get();
  SSD->RHS = RHS.get();
  SSD->KN  = KN.get();
  std::array<double,3> kBlochBuffer{0.0, 0.0, 0.0};
  double *kBloch = nullptr;
  SSD->kBloch         = nullptr;
  SSD->IF             = nullptr;
  SSD->TransformLabel = nullptr;
  SSD->IFLabel        = nullptr;
  if ( FileBase.empty() )
   FileBase=GetFileBase(GeoFile.c_str());
  SSD->FileBase       = FileBase.c_str();

  if ( !LogLevel.empty() )
   G->SetLogLevel(LogLevel.c_str());

  /*--------------------------------------------------------------*/
  /*- read the transformation file if one was specified and check */
  /*- that it plays well with the specified geometry file.        */
  /*- note if TransFile==0 then this code snippet still works; in */
  /*- this case the list of GTComplices is initialized to contain */
  /*- a single empty GTComplex and the check automatically passes.*/
  /*--------------------------------------------------------------*/
  GTCList GTCs=ReadTransFile(TransFile.empty() ? nullptr : TransFile.c_str());
  int NumTransformations=GTCs.size();
  char *ErrMsg=G->CheckGTCList(GTCs);
  if (ErrMsg)
   ErrExit("file %s: %s",TransFile.c_str(),ErrMsg);

  /*******************************************************************/
  /* for periodic geometries, all incident field sources that are    */
  /* active at a given time must involve  single incident field      */
  /* source, which must be a plane wave, and the bloch wavevector    */
  /* is extracted from the plane wave direction                      */
  /*******************************************************************/
  if (G->LDim>0)
   { if ( pwPolarization.size()!=3 || gbCenter.size()!=0 || psLocation.size()!=0 )
      ErrExit("for extended geometries, the incident field must be a single plane wave");
     kBloch = SSD->kBloch = kBlochBuffer.data();
   };

  /*******************************************************************/
  /* preload the scuff cache with any cache preload files the user   */
  /* may have specified                                              */
  /*******************************************************************/
  if ( !Cache.empty() && !WriteCache.empty() )
   ErrExit("--cache and --writecache options are mutually exclusive");
  if ( !Cache.empty() )
   WriteCache=Cache;
  for (size_t nrc=0; nrc<ReadCache.size(); nrc++)
   PreloadCache( ReadCache[nrc].c_str() );
  if ( !Cache.empty() )
   PreloadCache( Cache.c_str() );

  /*******************************************************************/
  /*******************************************************************/
  /*******************************************************************/
  void *HDF5Context=nullptr;
  if ( !HDF5File.empty() )
   HDF5Context=HMatrix::OpenHDF5Context(HDF5File.c_str());

  /*******************************************************************/
  /* if we have more than one geometrical transformation,            */
  /* allocate storage for BEM matrix blocks.  the owner vectors      */
  /* hold every allocated block; the view vectors include the        */
  /* mate-aliased entries, which point at another surface's block    */
  /* and are not owned.                                              */
  /*******************************************************************/
  std::vector<std::unique_ptr<HMatrix>> TBlockOwner, UBlockOwner;
  std::vector<HMatrix *> TBlocks, UBlocks;
  int NS=G->NumSurfaces;
  if (NumTransformations>1)
   { int NADB = NS*(NS-1)/2; // number of above-diagonal blocks
     TBlocks.resize(NS, nullptr);
     UBlocks.resize(NADB, nullptr);
     for(int ns=0, nb=0; ns<NS; ns++)
      {
        int nsMate = G->Mate[ns];
        if ( nsMate!=-1 )
         TBlocks[ns] = TBlocks[nsMate];
        else
         { int NBF=G->Surfaces[ns]->NumBFs;
           TBlocks[ns] = new HMatrix(NBF, NBF, M->RealComplex);
           TBlockOwner.emplace_back(TBlocks[ns]);
         };

        for(int nsp=ns+1; nsp<NS; nsp++, nb++)
         { int NBF=G->Surfaces[ns]->NumBFs;
           int NBFp=G->Surfaces[nsp]->NumBFs;
           UBlocks[nb] = new HMatrix(NBF, NBFp, M->RealComplex);
           UBlockOwner.emplace_back(UBlocks[nb]);
         };
      };
   };

  /*******************************************************************/
  /* loop over frequencies *******************************************/
  /*******************************************************************/
  for(int nFreq=0; nFreq<OmegaList->N; nFreq++)
   {
     cdouble Omega = OmegaList->GetEntry(nFreq);
     SSD->Omega    = Omega;

     char OmegaStr[MAXSTR];
     z2s(Omega, OmegaStr, sizeof(OmegaStr));
     Log("Working at frequency %s...",OmegaStr);

     /*******************************************************************/
     /* for periodic geometries, extract bloch wavevector from incident field */
     /*******************************************************************/
     if (G->LDim>0)
      { cdouble EpsExterior, MuExterior;
        G->RegionMPs[0]->GetEpsMu(Omega, &EpsExterior, &MuExterior);
        double kExterior = real( csqrt2(EpsExterior*MuExterior) * Omega );
        kBloch[0] = kExterior*pwDirection[0];
        kBloch[1] = kExterior*pwDirection[1];
        kBloch[2] = 0.0;
      };

     /*******************************************************************/
     /* if we have more than one transformation, pre-assemble diagonal  */
     /* matrix blocks at this frequency; otherwise just assemble the    */
     /* whole matrix                                                    */
     /*******************************************************************/
     if (NumTransformations==1)
      G->AssembleBEMMatrix(Omega, kBloch, M.get());
     else
      for(int ns=0; ns<G->NumSurfaces; ns++)
       if (G->Mate[ns]==-1)
        G->AssembleBEMMatrixBlock(ns, ns, Omega, kBloch, TBlocks[ns]);

     /*******************************************************************/
     /* dump the scuff cache to a cache storage file if requested. note */
     /* we do this only once per execution of the program, after the    */
     /* assembly of the diagonal BEM matrix blocks at first frequency,  */
     /* since at that point all cache elements that are to be computed  */
     /* will have been computed and the cache will not grow any further */
     /* for the rest of the program run.                                */
     /*******************************************************************/
     if ( !WriteCache.empty() )
      { StoreCache( WriteCache.c_str() );
        WriteCache.clear();
      };

     /*******************************************************************/
     /*******************************************************************/
     /*******************************************************************/
     for(int nt=0; nt<NumTransformations; nt++)
      {
        char TransformStr[100]="";
        if ( !TransFile.empty() )
         { G->Transform(GTCs[nt]);
           SSD->TransformLabel=GTCs[nt]->Tag;
           Log("Working at transformation %s...",SSD->TransformLabel);
           snprintf(TransformStr,100,"_%s",SSD->TransformLabel);
         };

        /*******************************************************************/
        /* assemble and insert off-diagonal blocks as necessary ************/
        /*******************************************************************/
        if (NumTransformations>1)
         { for(int ns=0, nb=0; ns<G->NumSurfaces; ns++)
            for(int nsp=ns+1; nsp<G->NumSurfaces; nsp++, nb++)
             G->AssembleBEMMatrixBlock(ns, nsp, Omega, kBloch, UBlocks[nb]);

           for(int ns=0, nb=0; ns<G->NumSurfaces; ns++)
            { int RowOffset=G->BFIndexOffset[ns];
              M->InsertBlock(TBlocks[ns], RowOffset, RowOffset);
              for(int nsp=ns+1; nsp<G->NumSurfaces; nsp++, nb++)
               { int ColOffset=G->BFIndexOffset[nsp];
                 M->InsertBlock(UBlocks[nb], RowOffset, ColOffset);
                 M->InsertBlockAdjoint(UBlocks[nb], ColOffset, RowOffset);
               };
            };
         };

        /*******************************************************************/
        /* export BEM matrix to a binary .hdf5 file if that was requested  */
        /*******************************************************************/
        if (HDF5Context)
         M->ExportToHDF5(HDF5Context,"M_%s%s",OmegaStr,TransformStr);

        /*******************************************************************/
        /* if the user requested no output options (for example, if she   **/
        /* just wanted to export the matrix to a binary file), don't      **/
        /* bother LU-factorizing the matrix or assembling the RHS vector. **/
        /*******************************************************************/
        if ( !NeedIncidentField )
         continue;

        /*******************************************************************/
        /* LU-factorize the BEM matrix to prepare for solving scattering   */
        /* problems                                                        */
        /*******************************************************************/
        Log("  LU-factorizing BEM matrix...");
        M->LUFactorize();

        /***************************************************************/
        /* loop over incident fields                                   */
        /***************************************************************/
        for(int nIF=0; nIF<IFList->NumIFs; nIF++)
         {
           IF = SSD->IF = IFList->IFs[nIF];
           SSD->IFLabel = !IFFile.empty() ? IFList->Labels[nIF] : nullptr;
           if (SSD->IFLabel)
            Log("  Processing incident field %s...",SSD->IFLabel);

           char IFStr[100]="";
           if (SSD->IFLabel)
            snprintf(IFStr,100,"_%s",SSD->IFLabel);

           /***************************************************************/
           /* assemble RHS vector and solve BEM system*********************/
           /***************************************************************/
           Log("  Assembling RHS vector...");
           G->AssembleRHSVector(Omega, kBloch, IF, KN.get());
           RHS->Copy(KN.get()); // copy RHS vector for later
           Log("  Solving the BEM system...");
           M->LUSolve(KN.get());

           if (HDF5Context)
            { RHS->ExportToHDF5(HDF5Context,"RHS_%s%s%s",OmegaStr,TransformStr,IFStr);
              KN->ExportToHDF5(HDF5Context,"KN_%s%s%s",OmegaStr,TransformStr,IFStr);
            };

           /***************************************************************/
           /* now process all requested outputs                           */
           /***************************************************************/

           /*--------------------------------------------------------------*/
           /*- power, force, torque by various methods --------------------*/
           /*--------------------------------------------------------------*/
           if ( !OPFTFile.empty() )
            WritePFTFile(SSD, PFTOpts, SCUFF_PFT_OVERLAP, PlotPFTFlux, OPFTFile.c_str());

           if ( !MomentPFTFile.empty() )
            WritePFTFile(SSD, PFTOpts, SCUFF_PFT_MOMENTS, PlotPFTFlux, MomentPFTFile.c_str());

           if ( !DSIPFTFile.empty() )
            { PFTOpts->DSIPoints=DSIPoints;
              WritePFTFile(SSD, PFTOpts, SCUFF_PFT_DSI, PlotPFTFlux, DSIPFTFile.c_str());
            };

           if ( !DSIPFTFile2.empty() )
            { PFTOpts->DSIPoints=DSIPoints2;
              WritePFTFile(SSD, PFTOpts, SCUFF_PFT_DSI, PlotPFTFlux, DSIPFTFile2.c_str());
            };

           if ( !EMTPFTFile.empty() )
            WritePFTFile(SSD, PFTOpts, SCUFF_PFT_EMT, PlotPFTFlux, EMTPFTFile.c_str());

           /*--------------------------------------------------------------*/
           /*- panel source densities -------------------------------------*/
           /*--------------------------------------------------------------*/
           if ( !PSDFile.empty() )
            WritePSDFile(SSD, PSDFile.c_str());

           /*--------------------------------------------------------------*/
           /*- scattered fields at user-specified points ------------------*/
           /*--------------------------------------------------------------*/
           for(size_t nepf=0; nepf<EPFiles.size(); nepf++)
            ProcessEPFile(SSD, EPFiles[nepf].c_str());

           /*--------------------------------------------------------------*/
           /*- induced dipole moments       -------------------------------*/
           /*--------------------------------------------------------------*/
           if ( !MomentFile.empty() )
            GetMoments(SSD, MomentFile.c_str());

           /*--------------------------------------------------------------*/
           /*- surface current visualization-------------------------------*/
           /*--------------------------------------------------------------*/
           if (PlotSurfaceCurrents)
            G->PlotSurfaceCurrents(KN.get(), Omega, SSD->kBloch, "%s.pp", FileBase.c_str());

           /*--------------------------------------------------------------*/
           /*- field visualization meshes ---------------------------------*/
           /*--------------------------------------------------------------*/
           for(size_t nfm=0; nfm<FVMeshes.size(); nfm++)
            VisualizeFields(SSD, FVMeshes[nfm].c_str(),
                            FVMeshTransFiles.size() ? FVMeshTransFiles[nfm].c_str() : nullptr,
                            FVFuncs.size() ? FVFuncs[nfm].c_str() : nullptr);

         }; // for(int nIF=0; nIF<IFList->NumIFs; nIF++

        /*******************************************************************/
        /*******************************************************************/
        /*******************************************************************/
        G->UnTransform();

      }; // for(int nt=0; nt<NumTransformations; nt++)

   }; //  for(nFreq=0; nFreq<NumFreqs; nFreq++)

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (HDF5Context)
   HMatrix::CloseHDF5Context(HDF5Context);
  printf("Thank you for your support.\n");

}
