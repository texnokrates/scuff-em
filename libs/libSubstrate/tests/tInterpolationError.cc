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
 * tInterpolation -- test of interpolation table for scalar GFs
 */
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <fenv.h>
#include <unistd.h>

#include "libhrutil.h"
#include "libSubstrate.h"
#include "ScuffCLI.h"
#include "libscuff.h"

const char *GroundedSiSlab=
 "0.0 CONST_EPS_11.7\n"
 "-1.0 GROUNDPLANE\n";

using namespace scuff;

	#define II cdouble(0.0,1.0)

typedef struct PhiVDFuncData
 { 
   LayeredSubstrate *S;
   cdouble Omega;
   int Dimension;
   double zFixed;
   ScalarGFOptions Options;

 } PhiVDFuncData;

void PhiVDFunc_ScalarGFs(dVec RhoZ, void *UserData, double *PhiVD, iVec dRZMax);

/***************************************************************/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[])
{
  InstallHRSignalHandler();
  InitializeLog(argv[0]);

  /*--------------------------------------------------------------*/
  /*- process command-line arguments -----------------------------*/
  /*--------------------------------------------------------------*/
  char *SubstrateFile=0;
  cdouble Eps=2.25;
  double h=0.79;
//
  cdouble Omega=0.7;
  double Freq=0.0;
//
  double RhoMin=0.0, RhoMax=2.0;
  double ZMin=0.0,     ZMax=0.0;
//
  double RelTol=1.0e-3;
//
  bool PPIsOnly=true;
  bool NoSubtract=false;
//
  /* name, type, #args, max_instances, storage, count, description*/
  double AbsTol=1.0e-12;
  CLI::App App{"test interpolation error"};
  App.option_defaults()->ignore_case();
  App.add_option("--SubstrateFile", SubstrateFile, ".substrate file")->take_last();
  scuff::cli::AddComplexOption(App, "--Eps", Eps, "substrate permittivity");
  App.add_option("--h", h, "substrate thickness")->take_last();
  App.add_option("--Omega", Omega, "angular frequency")->type_name("COMPLEX")->expected(1)->take_last();
  App.add_option("--Freq", Freq, "frequency in GHz")->take_last();
  App.add_option("--RhoMin", RhoMin, "")->take_last();
  App.add_option("--RhoMax", RhoMax, "")->take_last();
  App.add_option("--ZMin", ZMin, "")->take_last();
  App.add_option("--ZMax", ZMax, "")->take_last();
  App.add_option("--RelTol", RelTol, "")->take_last();
  App.add_flag("--PPIsOnly", PPIsOnly, "");
  App.add_flag("--NoSubtract", NoSubtract, "");

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;
  bool Subtract = !NoSubtract;
  if (Freq!=0.0)
   Omega = Freq * (2.0*M_PI/300.0);
  if (Freq!=0.0)
   Omega = Freq * (2.0*M_PI/300.0);

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  LayeredSubstrate *S;
  if (SubstrateFile)
   { S = new LayeredSubstrate(SubstrateFile);
     S->UpdateCachedEpsMu(Omega);
   }
  else
   { char EpsStr[100], SubstrateDefinition[1000];
     if (imag(Eps)==0.0)
      snprintf(EpsStr,100,"%g",real(Eps));
     else
      snprintf(EpsStr,100,"%g+%gi",real(Eps),imag(Eps));
     if (h==0.0)
      snprintf(SubstrateDefinition,1000,"0.0 CONST_EPS_%s\n",EpsStr);
     else
      snprintf(SubstrateDefinition,1000,"0.0 CONST_EPS_%s\n%e GROUNDPLANE\n",EpsStr,-h);
     S=CreateLayeredSubstrate(SubstrateDefinition);
   };
  S->Describe();
  S->UpdateCachedEpsMu(Omega);
  Eps=S->EpsLayer[1];
  h = S->zInterface[0]-S->zGP;

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  bool RetainSingularTerms = !Subtract;
  S->InitScalarGFInterpolator(Omega, RhoMin, RhoMax, ZMin, ZMax,
                              PPIsOnly, Subtract, RetainSingularTerms);
  InterpND *Interp = S->ScalarGFInterpolator;
  printf("{%lu",Interp->XGrids[0].size());
  if (Interp->XGrids.size() > 1 )
   printf(",%lu",Interp->XGrids[1].size());
  printf("}: max error ");

  PhiVDFuncData Data;
  Data.S         = S;
  Data.Omega     = Omega;
  Data.Dimension = EqualFloat(ZMin,ZMax) ? 1 : 2;
  Data.zFixed    = S->zSGFI;
  memcpy(&(Data.Options), &(S->SGFIOptions), sizeof(ScalarGFOptions));

#if 0
  FILE *f=fopen("/tmp/doom","w");
//int NN=Interp->XGrids[0].size();
  for(int n=0; n<100; n++)
   { //double Rho = Interp->XGrids[0][NN-3] + ((double)n)*(Interp->XGrids[0][NN-1]-Interp->XGrids[0][NN-3])/100.0;
     double Rho = Interp->XGrids[0][0] + ((double)n)*(Interp->XGrids[0][2]-Interp->XGrids[0][0])/100.0;
     double Z   = 0.0;
     double RhoZ[2];
     RhoZ[0]=Rho;
     RhoZ[1]=Z;
     double PhiExact[8], PhiInterp[4];
     PhiVDFunc_ScalarGFs(&Rho, (void *)&Data, PhiExact);
     Interp->Evaluate(RhoZ,PhiInterp);
     fprintf(f,"%e ",Rho);
     fprintf(f,"%e %e %e ",PhiExact[0],PhiExact[1],PhiInterp[0]);
     fprintf(f,"%e %e %e ",PhiExact[2],PhiExact[3],PhiInterp[1]);
     fprintf(f,"%e %e %e ",PhiExact[4],PhiExact[5],PhiInterp[2]);
     fprintf(f,"%e %e %e ",PhiExact[6],PhiExact[7],PhiInterp[3]);
     fprintf(f,"\n");
   }
#endif

  double MaxError = Interp->PlotInterpolationError(PhiVDFunc_ScalarGFs, (void *)&Data,
                                                   const_cast<char *>("/tmp/tInterpolationError.out"));
  printf("%e \n",MaxError);
/*
  printf("Rho points (%lu) = { ",Interp->xPoints[0].size());
  for(size_t n=0; n<Interp->xPoints[0].size(); n++)
   printf("%e ",Interp->xPoints[0][n]);
  printf("}\n");
  if (ZMin!=ZMax)
   { printf("Z   points (%lu) = { ",Interp->xPoints[1].size());
     for(size_t n=0; n<Interp->xPoints[1].size(); n++)
       printf("%e ",Interp->xPoints[1][n]);
   }
*/
  printf("Wrote error data to /tmp/tInterpolationError.out.\n");
  printf("Thank you for your support.\n");

}
