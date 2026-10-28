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
#include "libscuff.h"
#include "ScuffCLI.h"

const char *GroundedSiSlab=
 "0.0 CONST_EPS_11.7\n"
 "-1.0 GROUNDPLANE\n";

using namespace scuff;

#define II cdouble(0.0,1.0)

double EtaFD=1.0e-4;

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
  cdouble Eps=12.0;
  double h=1.0;
  //
  cdouble Omega=0.7;
  //
  double RhoMin=0.0, RhoMax=2.0;
  int RhoPoints=100;
  //
  bool PPIsOnly=false;
  bool Subtract=true;
  bool CorrectionOnly=false;
  bool OmitSingularTerms=true;
  CLI::App App{"test interpolation table for scalar GFs"};
  App.option_defaults()->ignore_case();
  App.add_option("--SubstrateFile", SubstrateFile, ".substrate file")->take_last();
  scuff::cli::AddComplexOption(App, "--Eps", Eps, "substrate permittivity");
  App.add_option("--h", h, "substrate thickness")->take_last();
  App.add_option("--Omega", Omega, "angular frequency")->type_name("COMPLEX")->expected(1)->take_last();
  App.add_option("--RhoMin", RhoMin, "XDS file")->take_last();
  App.add_option("--RhoMax", RhoMax, "XDS file")->take_last();
  App.add_option("--RhoPoints", RhoPoints, "")->take_last();
  App.add_flag("--PPIsOnly", PPIsOnly, "");
  App.add_flag("--Subtract", Subtract, "");
  App.add_flag("--CorrectionOnly", CorrectionOnly, "");
  App.add_flag("--OmitSingularTerms", OmitSingularTerms, "");

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;

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
  ScalarGFOptions Options;
  InitScalarGFOptions(&Options);
  Options.PPIsOnly            = PPIsOnly;
  Options.Subtract            = Subtract;
  Options.CorrectionOnly      = CorrectionOnly;
  Options.RetainSingularTerms = !OmitSingularTerms;

  Tic();
  printf("Initializing accelerator...");
  char RPStr[10];
  snprintf(RPStr,10,"%i",RhoPoints);
  setenv("SCUFF_SUBSTRATE_RHOPOINTS",RPStr,1);
  S->InitScalarGFInterpolator(Omega, RhoMin, RhoMax, 0.0, 0.0, PPIsOnly, Subtract, !OmitSingularTerms);
  double InitTime = Toc();
  printf("...%e s (%e s / point)\n",InitTime,InitTime/RhoPoints);
   
  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  double DRho = (RhoMax-RhoMin)/(RhoPoints-1);
  double MaxRD=0.0, MaxRDRho=0.0;
  double MeanRD[NUMSGFS_MOI]={0.0, 0.0, 0.0, 0.0, 0.0};
  int MaxRDComponent=0, Samples=0;
  double InterpTime=0.0, FullTime=0.0;
  int NumSGFs = PPIsOnly ? 2 : NUMSGFS_MOI;
  FILE *f=fopen("/tmp/tInterpolation.out","w");
  double zDest=0.0;
  for(double Rho=RhoMin; Rho<=RhoMax; Rho+=0.25*DRho)
   { 
     Options.UseInterpolator=true;
     cdouble VInterp[NUMSGFS_MOI];
     Tic();
     S->GetScalarGFs_MOI(Omega, Rho, zDest, VInterp, &Options);
     InterpTime+=Toc();

     Options.UseInterpolator=false;
     cdouble VFull[NUMSGFS_MOI];
     Tic();
     S->GetScalarGFs_MOI(Omega, Rho, zDest, VFull, &Options);
     FullTime+=Toc();
   
     fprintf(f,"%e ",Rho);
     for(int n=0; n<NumSGFs; n++)
      { double RDn = RD(VInterp[n], VFull[n]);
        fprintf(f,"%e ",RDn);
        if (RDn > MaxRD)
         { MaxRD = RDn;
           MaxRDRho = Rho;
           MaxRDComponent=n;
         }
        MeanRD[n]+=RDn;
      }
     fprintVec(f,VInterp,NumSGFs);
     fprintVecCR(f,VFull,NumSGFs);
     Samples++;
   }
  fclose(f);

  printf("Full   time: %e ms\n",FullTime);
  printf("Interp time: %e ms\n",InterpTime);
  printf("Max RD = %e (Rho=%e, n=%i)\n",MaxRD,MaxRDRho,MaxRDComponent);
  for(int n=0; n<NumSGFs; n++)
   printf("mean RD %i: %e\n",n,MeanRD[n]/Samples);
  printf("\nThank you for your support.\n");

}

