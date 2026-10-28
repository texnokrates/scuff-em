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
 * tZMatrix.cc
 * homer reid    -- 4/2018
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
  std::string GeoFile;
  std::string PortFile;
//
  std::string SubstrateFile;
  std::string EpsStr;
  double h     = 0.0;
//
  double Freq=1.0;
//
  int ns=0;
  int ne=-1;
//
  double X[3]; int nX=0;
  bool Subtract = true;
  
  CLI::App App{"test one-BF MOI fields"};
  App.option_defaults()->ignore_case();
  App.add_option("--geometry", GeoFile, "geometry file")->take_last();
  App.add_option("--portfile", PortFile, "port file")->take_last();
  App.add_option("--SubstrateFile", SubstrateFile, "substrate definition file")->take_last();
  App.add_option("--Eps", EpsStr, "substrate permittivity")->take_last();
  App.add_option("--h", h, "substrate thickness")->take_last();
  App.add_option("--Freq", Freq, "frequency in GHz")->take_last();
  App.add_option("--ns", ns, "")->take_last();
  App.add_option("--ne", ne, "")->take_last();
  App.add_option("--X", X, "")->type_name("COMPLEX")->expected(3)->take_last();
  App.add_flag("--Subtract", Subtract, "");

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;
  if (GeoFile.empty())
   { CLI::ValidationError Error("t1BFMOIFields","--geometry option is mandatory");
     App.exit(Error);
     return 1;
   }
  if (PortFile.empty())
   { CLI::ValidationError Error("t1BFMOIFields","--PortFileName option is mandatory");
     App.exit(Error);
     return 1;
   }
  /***************************************************************/
  /* create the geometry                                         */
  /***************************************************************/
  RWGGeometry::UseHRWGFunctions=false;
  RWGGeometry *G=new RWGGeometry(GeoFile.c_str());

  /***************************************************************/
  /* process substrate-related options                           */
  /***************************************************************/
  if (!SubstrateFile.empty())
   G->Substrate = new LayeredSubstrate(SubstrateFile.c_str());
  else if (!EpsStr.empty())
   { 
     char SubstrateDefinition[1000];
     if (h==0.0) // no ground plane
      snprintf(SubstrateDefinition,1000,"0.0 CONST_EPS_%s\n",EpsStr.c_str());
     else
      snprintf(SubstrateDefinition,1000,"0.0 CONST_EPS_%s\n%e GROUNDPLANE\n",EpsStr.c_str(),-h);
     G->Substrate=CreateLayeredSubstrate(SubstrateDefinition);
   }

  RWGSurface *Surf = G->Surfaces[ns];
  srandom(time(0));
  if (ne==-1)
   ne = irand(0,Surf->NumEdges);
  double Radius = Surf->Edges[ne]->Radius;
  double *X0 = Surf->Edges[ne]->Centroid;

  /***************************************************************/
  /* parse the port list and plot if requested *******************/
  /***************************************************************/
  cdouble Omega=FREQ2OMEGA * Freq;
  int Orders[]={0, 4,7,9,13,16,20,25};
  double Times[8];
  cdouble PFVector[8][NPFC];

  double XPoints[5][3];
  int NumPoints;
  if (nX)
   { XPoints[0][0]=X[0];
     XPoints[0][1]=X[1];
     XPoints[0][2]=X[2];
     NumPoints=1;
   }
  else
   { NumPoints=0;
     for(double r=0.1*Radius; r<=10.1*Radius; r*=10.0)
      { double CosTheta=randU(-1.0,1.0), Phi=randU(0.0,2.0*M_PI);
        double SinTheta=sqrt(1.0-CosTheta*CosTheta);
        XPoints[NumPoints][0] = X0[0] + r*SinTheta*cos(Phi);
        XPoints[NumPoints][1] = X0[1] + r*SinTheta*sin(Phi);
        XPoints[NumPoints][2] = X0[2] + r*CosTheta;
        NumPoints++;
      }
   }
   

  SetDefaultCD2SFormat("{%+.4e,%+.6e}");
  for(int nx=0; nx<NumPoints; nx++)
   { 
     printf("\n\n X=(%g,%g,%g) r=%g: \n", XPoints[nx][0],XPoints[nx][1],XPoints[nx][2],VecDistance(X0,XPoints[nx]));
     for(int no=0; no<8; no++)
      { Tic();
        Get1BFMOIFields(G, ns, ne, Omega, XPoints[nx], PFVector[no], Orders[no], Subtract);
        Times[no]=Toc(); 
        printf("Order %2i: (%.2e s)\n",Orders[no],Times[no]);
        for(int npfc=0; npfc<NPFC; npfc++)
         { if (npfc==1 || npfc==5) continue;
           printf("%s (%.1e)  ",CD2S(PFVector[no][npfc]),RD(PFVector[no][npfc],PFVector[0][npfc]));
         };
        printf("\n");
         
      } 
        
   }

}
