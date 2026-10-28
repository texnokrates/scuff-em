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
 * tBeyn411.cc -- a demonstration code that uses libBeyn to
 *             -- solve the 400-dimensional nonlinear eigenproblem
 *             -- discussed in Section 4.11 of Beyn's 2012 paper
 */

#include <stdio.h>
#include <stdlib.h>

#include <string.h> 
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libhrutil.h>
#include "ScuffCLI.h"
#include "libBeyn.h"

#define II cdouble(0.0,1.0)
#define MAXEPF 10
#define MAXFVM 10

//using namespace scuff;

/***************************************************************/
/* function passed to BeynSolver, called for each quadrature   */
/* point on the contour; its task is to replace the MxL matrix */
/* VHat with M(z) \ VHat, i.e. Inverse[ M(z) ] * VHat          */
/***************************************************************/
typedef struct BFData
 { 
   HMatrix *M;
 } BFData;

void BeynFunc(cdouble z, void *UserData, HMatrix *VHat, HMatrix *MVHat)
{
  // unpack fields from data structure
  BFData *Data   = (BFData *)UserData;
  HMatrix *M     = Data->M;

  // assemble M matrix as described in Beyn section 4.11
  int NR = M->NR;
  double m = (double)NR;
  cdouble DE  =  2.0*m - 4.0*z/(6.0*m);   // diagonal entry
  cdouble ODE = -1.0*m -     z/(6.0*m);   // off-diagonal entry
  M->Zero();
  M->SetEntry(0,0,DE);  M->SetEntry(0,1,ODE); // top row
  for(int nr=1; nr<NR-1; nr++)                // rows 2...m-1
   { M->SetEntry(nr,nr,DE);
     M->SetEntry(nr,nr-1,ODE);
     M->SetEntry(nr,nr+1,ODE);
   };
  M->SetEntry(NR-1, NR-2, ODE);
  M->SetEntry(NR-1, NR-1, 0.5*DE + z/(z-1.0) ); // bottom row

  if (MVHat)
   M->Multiply(VHat, MVHat);
  else
   { // replace VHat with M\VHat
     M->LUFactorize();
     M->LUSolve(VHat);
   }
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[]) 
{ 
  InstallHRSignalHandler();
  InitializeLog(argv[0]);

  /***************************************************************/
  /* process command-line arguments ******************************/
  /***************************************************************/
  std::string GeoFileName;
  cdouble Omega0;
  cdouble z0 = cdouble(150.0, 2.0);
  double Rx   = 148.0;
  double Ry   = 148.0;
  int L       = 10;
  int N       = 50;
  int Dim     = 400;
  double kx   = 0.0;
  double ky   = 0.0;
  std::string ContourFile;
  bool PlotContours=false;
  bool PlotSurfaceCurrents=false;
  bool PlotSurfaceFields=false;
  std::string SphericalMomentFile;
  int LMax=3;
  std::string CartesianMomentFile;
  std::vector<std::string> EPFiles;
  std::vector<double> FVScreens;
  int nFVScreens=0;
  std::vector<std::string> FVMeshes;
  std::vector<std::string> FVMeshTransFiles;
  std::string FileBase;

CLI::App App{"compute spectrum via Beyn method"};
  App.option_defaults()->ignore_case();
  App.add_option("--geometry", GeoFileName, ".scuffgeo file")->required()->take_last();
  scuff::cli::AddComplexOption(App, "--Omega", Omega0, "center of contour");
  App.add_option("--Rx", Rx, "horizonal radius")->take_last();
  App.add_option("--Ry", Ry, "vertical radius")->take_last();
  App.add_option("--N", N, "number of quadrature points")->take_last();
  App.add_option("--L", L, "number of EVs expected in contour ")->take_last();
  App.add_option("--kx", kx, "x component of bloch vector")->take_last();
  App.add_option("--ky", ky, "y component of bloch vector")->take_last();
  App.add_option("--ContourFile", ContourFile, "list of contours")->take_last();
  App.add_flag("--PlotContours", PlotContours, "plot contours for visualization");
  App.add_flag("--PlotSurfaceCurrents", PlotSurfaceCurrents, "generate visualization files for eigenmode currents");
  App.add_flag("--PlotSurfaceFields", PlotSurfaceFields, "generate visualization files for eigenmode surface fields");
  App.add_option("--SphericalMomentFile", SphericalMomentFile, "name of output file for spherical multipole moments")->take_last();
  App.add_option("--LMax", LMax, "index of highest spherical multipole moment to retain")->take_last();
  App.add_option("--CartesianMomentFile", CartesianMomentFile, "name of output file for cartesian multipole moments")->take_last();
  App.add_option("--EPFile", EPFiles, "list of evaluation points")->expected(1, MAXEPF);
  App.add_option("--FVScreen", FVScreens, "field visualization screen")->type_name("COMPLEX")->expected(11, MAXFVM);
  App.add_option("--FVMesh", FVMeshes, "field visualization mesh")->expected(1, MAXFVM);
  App.add_option("--FVMeshTransFile", FVMeshTransFiles, "list of geometrical transformations applied to FVMesh")->expected(1, MAXFVM);
  App.add_option("--FileBase", FileBase, "base name for output files")->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;
  if (GeoFileName.empty())
   { CLI::ValidationError Error("tBeyn411","--geometry option is mandatory");
     App.exit(Error);
     return 1;
   }
  int nEPFiles  = (int)EPFiles.size();
  int nFVMeshes = (int)FVMeshes.size();
  SetDefaultCD2SFormat("{%+.12e,%+.12e}");

  /***************************************************************/
  /* initialize BeynSolver data structure ************************/
  /***************************************************************/
  BeynSolver *Solver = CreateBeynSolver(Dim, L);

  /***************************************************************/
  /* initialize data structure needed for our BeynFunction  ******/
  /***************************************************************/
  struct BFData MyBFData, *BFD=&MyBFData;
  BFD->M = new HMatrix(Dim, Dim, LHM_COMPLEX);

  /***************************************************************/
  /* compute eigenvalues by Beyn method                          */
  /***************************************************************/
  int K=BeynSolve(Solver, BeynFunc, (void *)BFD, Omega0, Rx, Ry, N);

  printf("Found %i eigenvalues: \n",K);
  for(int k=0; k<K; k++)
   printf("%i: %s \n",k,CD2S(Solver->Eigenvalues->GetEntry(k)));

}
