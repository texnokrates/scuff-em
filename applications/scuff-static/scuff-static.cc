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
 * scuff-static.cc   -- a standalone code within the scuff-EM suite for 
 *                   -- solving electrostatics problems 
 *
 * homer reid        -- 10/2006 -- 5/2013
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <memory>
#include <string>
#include <vector>

#include <ScuffCLI.h>
#include "scuff-static.h"

using namespace scuff;

/***************************************************************/
/***************************************************************/
/***************************************************************/
#define MAXEPF   10    // max number of evaluation-point files
#define MAXFVM   10    // max number of field-visualization meshes
#define MAXCACHE 10    // max number of cache files for preload

#define MAX_MONOPOLES 10
#define MAX_DIPOLES 10

/***************************************************************/
/* Attempt to read or write the solution vector for a BEM      */
/* electrostatics problem to/from an HDF5 binary data file.    */
/***************************************************************/
#define FILEOP_READ  0
#define FILEOP_WRITE 1
bool FileOp(int Op, HVector *Sigma, const char *SolutionFile, const char *SolutionName, const char *TransformLabel)
{  
  Log("Attempting to %s solution: file %s ",
       Op==FILEOP_READ ? "read" : "write", SolutionFile);

  char DataSetName[1000];
  if(TransformLabel)
   snprintf(DataSetName,1000,"%s_%s",SolutionName,TransformLabel);
  else
   snprintf(DataSetName,1000,"%s",SolutionName);
  
  switch(Op)
   { 
     case FILEOP_READ:
      Sigma->ImportFromHDF5(SolutionFile,DataSetName);
      break;

     case FILEOP_WRITE:
      Sigma->ExportToHDF5(SolutionFile,DataSetName);
      break;
   };

  if (Sigma->ErrMsg)
   { Log("failed! %s",Sigma->ErrMsg);
     free(Sigma->ErrMsg);
     return false;
   }
  Log("success!");
  return true;
}

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
  std::string SubstrateFile;
  std::string TransFile;
  std::string PotFile;
  std::string ConstField;
  std::vector<double> Monopoles;   // 4 values per occurrence
  std::vector<double> Dipoles;     // 6 values per occurrence
  std::string PhiExt;
  std::string ExcitationFile;
  std::vector<std::string> EPFiles;
  std::string FileBase;
  std::string PolFile;
  std::string CapFile;
  std::string PlotFile;
  std::string CMatrixFile;
  std::string CMatrixHDF5File;
  int lMax          = 2;
  std::vector<std::string> FVMeshes, FVMeshTransFiles;
  bool SeparateOutputFiles = false;
  std::string SolutionFile;
  std::string SolutionName;
  bool DisableEquivalentPairs = false;

  CLI::App App{"solve electrostatics problems"};
  App.option_defaults()->ignore_case();

  App.add_option("--geometry", GeoFile, "geometry file")
     ->required()
     ->take_last();
  App.add_option("--substrate", SubstrateFile, "substrate file")->take_last();
  App.add_option("--transfile", TransFile, "list of geometry transformations")->take_last();

  App.add_option("--potentialfile", PotFile, "list of conductor potentials")->take_last();
  App.add_option("--constfield", ConstField,
                 "direction of constant unit-strength E field (x,y,z)")->take_last();
  App.add_option("--monopole", Monopoles,
                 "(x0,y0,z0,Q) for point-source excitation")
     ->type_size(4)
     ->expected(1, MAX_MONOPOLES);
  App.add_option("--dipole", Dipoles,
                 "(x0,y0,z0,px,py,pz) for point-dipole excitation")
     ->type_size(6)
     ->expected(1, MAX_DIPOLES);
  App.add_option("--phiext", PhiExt, "user-specified external potential")->take_last();
  App.add_option("--excitationfile", ExcitationFile, "list of excitations")->take_last();

  App.add_option("--EPFile", EPFiles, "list of evaluation points")
     ->expected(1, MAXEPF);
  App.add_option("--FileBase", FileBase, "base name for field output files")->take_last();
  App.add_option("--polfile", PolFile, "polarizability output file")->take_last();
  App.add_option("--capfile", CapFile, "capacitance-matrix output file")->take_last();
  App.add_option("--plotfile", PlotFile,
                 "surface-charge visualization output file")->take_last();
  App.add_option("--cmatrixfile", CMatrixFile, "C-matrix text output file")->take_last();
  App.add_option("--cmatrixhdf5file", CMatrixHDF5File, "C-matrix HDF5 output file")->take_last();
  App.add_option("--lMax", lMax,
                 "maximum l-value of spherical harmonic in C-matrix")->take_last();
  App.add_option("--FVMesh", FVMeshes, "field visualization mesh")
     ->expected(1, MAXFVM);
  App.add_option("--FVMeshTransFile", FVMeshTransFiles,
                 "list of geometrical transformations for FVMesh")
     ->expected(1, MAXFVM);
  App.add_flag("--SeparateOutputFiles", SeparateOutputFiles,
               "write separate output files for each transformation and excitation");

  App.add_option("--solutionfile", SolutionFile,
                 "name of HDF5 file for solution input/output")->take_last();
  App.add_option("--solutionname", SolutionName,
                 "name of dataset within HDF5 file")->take_last();
  App.add_flag("--DisableEquivalentPairs", DisableEquivalentPairs,
               "do not identify equivalent surface pairs");

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;

  bool lMaxWasSpecified = App.get_option("--lMax")->count() > 0;

  /*******************************************************************/
  /*******************************************************************/
  /*******************************************************************/
  if (lMaxWasSpecified && (CMatrixFile.empty() && CMatrixHDF5File.empty()) )
   ErrExit("--lMax option can only be used with --CMatrixFile or --CMatrixHDF5File");

  /*******************************************************************/
  /* sanity checks on input arguments.                               */
  /* Type-1 calculations (polarizability, capacitance, C-matrix) do  */
  /*  not accept excitation specifications.                          */
  /* Type-2 calculations (fields, field visualization, surface-charge*/
  /*  visualization) require excitation specifications.              */
  /*******************************************************************/
  bool HaveType1Outputs = ( !PolFile.empty() || !CapFile.empty()
                            || !CMatrixFile.empty() || !CMatrixHDF5File.empty() );
  bool HaveType2Outputs = ( EPFiles.size()>0 || FVMeshes.size()>0 || !PlotFile.empty() );
  bool HaveType2Inputs  = (    !ExcitationFile.empty() || !PotFile.empty()
                            || !ConstField.empty()
                            || Monopoles.size()>0    || Dipoles.size()>0
                            || !PhiExt.empty()
                          );

  if ( !HaveType1Outputs && !HaveType2Outputs )
   { CLI::ValidationError Error("scuff-static",
        "you have not selected any type of calculation");
     App.exit(Error);
     return 1;
   };
  if (HaveType1Outputs && HaveType2Outputs)
   ErrExit("{--EPFile,--FVMesh{ may not be used with {--polfilebase, --capfilebase}");
  if (HaveType1Outputs && HaveType2Inputs)
   ErrExit("potential/field specifications may not be used with {--polfilebase, --capfilebase}");
  if (HaveType2Outputs && !HaveType2Inputs)
   ErrExit("you have not specified any external potential or field");
  if (HaveType1Outputs && !SolutionFile.empty())
   ErrExit("--SolutionFile may not be used with {--polfilebase, --capfilebase}");

  if ( !SolutionFile.empty() && SolutionName.empty() )
   SolutionName = "DEFAULT";

  if (FileBase.empty())
   FileBase=GetFileBase(GeoFile.c_str());

  std::vector<const char *> EPFilePtrs;
  for(const std::string &EPFile : EPFiles)
   EPFilePtrs.push_back(EPFile.c_str());

  /*******************************************************************/
  /* create the ScuffStaticGeometry **********************************/
  /*******************************************************************/
  std::unique_ptr<StaticSolver> SS(
    new StaticSolver(GeoFile.c_str(), SubstrateFile.empty() ? 0 : SubstrateFile.c_str()));
  SS->SeparateOutputFiles = SeparateOutputFiles;

  HMatrix *M      = 0;   // owned by ReassembleBEMMatrix's realloc-in-place contract
  std::unique_ptr<HVector> Sigma(SS->AllocateRHSVector());
  RWGGeometry *G  = SS->G;

  /*******************************************************************/
  /* read in information about excitations ***************************/
  /*******************************************************************/
  StaticExcitation **SEList=0;
  int NumExcitations=0;
  if (!ExcitationFile.empty())
   {
     SEList = ReadExcitationFile(SS.get(), ExcitationFile.c_str(), &NumExcitations);
   };
  if ( !PotFile.empty() || !ConstField.empty()
       || Monopoles.size()>0 || Dipoles.size()>0 || !PhiExt.empty() )
   {
     if (!ExcitationFile.empty())
      ErrExit("--ExcitationFile is incompatible with [--PotFile | --ConstField | --Monopole | --Dipole | --PhiExt]");
     SEList = CreateSimpleSEList(G, PotFile.empty() ? 0 : PotFile.c_str(),
                                 ConstField.empty() ? 0 : ConstField.c_str(),
                                 (int)Monopoles.size()/4, Monopoles.data(),
                                 (int)Dipoles.size()/6, Dipoles.data(),
                                 PhiExt.empty() ? 0 : PhiExt.c_str());
     NumExcitations=1;
   };

  /****************************************************************/
  /*- read the transformation file if one was specified and check */
  /*- that it plays well with the specified geometry file.        */
  /*- note if TransFile==0 then this code snippet still works; in */
  /*- this case the list of GTComplices is initialized to contain */
  /*- a single empty GTComplex and the check automatically passes.*/
  /****************************************************************/
  GTCList GTCs=ReadTransFile(TransFile.empty() ? 0 : TransFile.c_str());
  int NT = GTCs.size();
  char *ErrMsg=G->CheckGTCList(GTCs);
  if (ErrMsg)
   ErrExit("file %s: %s",TransFile.c_str(),ErrMsg);

  /*******************************************************************/
  /* if we have more than one geometrical transformation,            */
  /* allocate storage for BEM matrix blocks                          */
  /* 20170619 just do this by default now that we have equivalent-   */
  /*  pair detection                                                 */
  /*******************************************************************/
  //BMAccelerator *BMA = (NT==1) ? 0 : CreateBMAccelerator(SS);
  // TODO figure out interaction between transfiles and equivalent pairs
  if (NT>1) DisableEquivalentPairs=true;
  BMAccelerator *BMA = CreateBMAccelerator(SS.get(), GTCs, !DisableEquivalentPairs);

  /*******************************************************************/
  /* loop over transformations.                                      */
  /* if no --TransFile was specified, this loop iterates just once,  */
  /* for the 'default' (identity) transformation.                    */
  /*******************************************************************/
  for(int nt=0; nt<NT; nt++)
  {
     if (!TransFile.empty())
      { G->Transform(GTCs[nt]);
        free(SS->TransformLabel);
        SS->TransformLabel=strdup(GTCs[nt]->Tag);
        Log("Working at transformation %s...",GTCs[nt]->Tag);
      }

     /*******************************************************************/
     /*******************************************************************/
     /*******************************************************************/
     bool HaveSolution=false;
     if (!SolutionFile.empty())
      HaveSolution=FileOp(FILEOP_READ, Sigma.get(), SolutionFile.c_str(),
                          SolutionName.c_str(), SS->TransformLabel);

     if (!HaveSolution)
      ReassembleBEMMatrix(SS.get(), &M, BMA, nt);

     /*******************************************************************/
     /* now switch off depending on the type of calculation the user    */
     /* requested. The first three options below do not require         */
     /* user-specified external fields.                                 */
     /*******************************************************************/
     if (!PolFile.empty())
      WritePolarizabilities(SS.get(), M, Sigma.get(), PolFile.c_str());
     if (!CapFile.empty())
      WriteCapacitanceMatrix(SS.get(), M, Sigma.get(), CapFile.c_str());
     if (!CMatrixFile.empty() || !CMatrixHDF5File.empty())
      WriteCMatrix(SS.get(), M, Sigma.get(), lMax, CMatrixFile.c_str(),
                   CMatrixHDF5File.empty() ? 0 : CMatrixHDF5File.c_str());

     /******************************************************************/
     /* The remaining options do require user-specified external       */
     /* fields unless the user supplied a precomputed solution vector. */
     /******************************************************************/
     for(int n=0; n<NumExcitations; n++)
      {
        StaticExcitation *SE = SEList[n];
        SS->ExcitationLabel = SE->Label;
        Log("Handling excitation %s",SE->Label ? SE->Label : "(default)");
        if (!HaveSolution)
         {
           SS->AssembleRHSVector(SE, Sigma.get());
           M->LUSolve(Sigma.get());
           if (!SolutionFile.empty())
            FileOp(FILEOP_WRITE, Sigma.get(), SolutionFile.c_str(),
                   SolutionName.c_str(), SS->TransformLabel);
         };

        if (!PlotFile.empty())
         SS->PlotChargeDensity(Sigma.get(), PlotFile.c_str());

        if (EPFilePtrs.size()>0 || !PlotFile.empty() )
         WriteFields(SS.get(), Sigma.get(), SE, EPFilePtrs.data(),
                     (int)EPFilePtrs.size(), FileBase.c_str());

        for(size_t nfm=0; nfm<FVMeshes.size(); nfm++)
         SS->VisualizeFields(Sigma.get(), FVMeshes[nfm].c_str(), FileBase.c_str(), SE,
                             FVMeshTransFiles.size() ? FVMeshTransFiles[nfm].c_str() : 0);
      };

     /*******************************************************************/
     /*******************************************************************/
     /*******************************************************************/
     G->UnTransform();

   }; // for(int nt=0; nt<NT; nt++)

  /*******************************************************************/
  /*******************************************************************/
  /*******************************************************************/
  delete M;
  printf("Thank you for your support.\n");

}
