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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <libhrutil.h>
#include <libhmat.h>
#include <libscuff.h>
#include <libIncField.h>
#include <libSubstrate.h>
#include <scuffSolver.h>

#define II cdouble(0.0,1.0)

using namespace scuff;

/***************************************************************/
/* main function   *********************************************/
/***************************************************************/  
int main(int argc, char *argv[])
{
  InstallHRSignalHandler();
  InitializeLog(argv[0]);

  /***************************************************************/
  /** process command-line arguments *****************************/
  /***************************************************************/
  char *GeoFile=0;
//
  char *SubstrateFile=0;
  char *EpsStr = const_cast<char *>("1.0");
  double h     = 0.0;
//
  cdouble Omega=1.0;
  int nsa=0;
  int nsb=0;

  CLI::App App{"test equivalent pairs"};
  App.option_defaults()->ignore_case();
  App.add_option("--geometry", GeoFile, "geometry file")->take_last();
  scuff::cli::AddComplexOption(App, "--Omega", Omega, "angular frequency");
  App.add_option("--nsa", nsa, "")->take_last();
  App.add_option("--nsb", nsb, "")->take_last();
  App.add_option("--SubstrateFile", SubstrateFile, "substrate definition file")->take_last();
  App.add_option("--Eps", EpsStr, "substrate permittivity")->take_last();
  App.add_option("--h", h, "substrate thickness")->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;
  if (GeoFile.empty())
   { CLI::ValidationError Error("tEquivalentPairs","--geometry option is mandatory");
     App.exit(Error);
     return 1;
   }
  if (GeoFile==0)
   OSUsage(argv[0],OSArray,"--geometry option is mandatory");

  /***************************************************************/
  /* create the geometry                                         */
  /***************************************************************/
  RWGGeometry *G=new RWGGeometry(GeoFile);

  char SubstrateDefinition[100];
  if (h==0.0)
   snprintf(SubstrateDefinition,100,"0.0 CONST_EPS_%s\n",EpsStr);
  else
   snprintf(SubstrateDefinition,100,"0.0 CONST_EPS_%s\n%e GROUNDPLANE\n",EpsStr,-h);
  G->Substrate = CreateLayeredSubstrate(SubstrateDefinition);

  int NEA = G->Surfaces[nsa]->NumEdges, NEB=G->Surfaces[nsb]->NumEdges;
  HMatrix *MSlow = new HMatrix(NEA, NEB, LHM_COMPLEX);
  HMatrix *MFast = new HMatrix(NEA, NEB, LHM_COMPLEX);

  Tic();
  setenv("SCUFF_IGNORE_EEPS","1",1);
  AssembleMOIMatrixBlock(G, nsa, nsb, Omega, MSlow);
  printf("Slow time 1: %e s\n",Toc());

  Tic();
  AssembleMOIMatrixBlock(G, nsa, nsb, Omega, MSlow);
  printf("Slow time 2: %e s\n",Toc());

  Tic();
  setenv("SCUFF_IGNORE_EEPS","0",1);
  AssembleMOIMatrixBlock(G, nsa, nsb, Omega, MFast);
  printf("Fast time: %e s\n",Toc());

  double MaxRD=0.0, MeanRD=0.0;
  for(int nea=0; nea<NEA; nea++)
   for(int neb=0; neb<NEB; neb++)
    { 
      cdouble MESlow=MSlow->GetEntry(nea,neb), MEFast=MFast->GetEntry(nea,neb);
      double ThisRD = RD(MESlow, MEFast);
      MaxRD=fmax(MaxRD,ThisRD);
      MeanRD+=ThisRD;
      if(ThisRD>0.1) 
       printf("Bawonkatage! M[%i,%i]={%s} (slow), {%s} (fast)\n",nea,neb,CD2S(MESlow),CD2S(MEFast));
    }
  printf("Mean/max RD %e/%e\n",MeanRD/(NEA*NEB),MaxRD);

}
