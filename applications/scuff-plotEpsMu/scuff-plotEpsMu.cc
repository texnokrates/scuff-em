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
 * scuff-plotEpsMu.cc -- a little application to check scuff-EM material
 *                    -- property specifications by plotting epsilon and mu
 *                    -- versus frequency for a given material
 * 
 * homer reid         -- 12/2008 / 5/2012
 * 
 * --------------------------------------------------------------
 * 
 * usage:
 *  
 *   scuff-plotEpsMu MaterialName [options]
 *  
 * options:
 *  
 *  --OmegaMin    xx  (specify lower limit of frequency range to plot)
 *  --OmegaMax    xx  (specify upper limit of frequency range to plot)
 *  --LengthUnit  xx  (specify length unit in meters)
 *  --GnuPlot     xx  (specify length unit in meters)
 *  --geometry    xx  (specify a .scuffgeo file that contains the 
 *                     MATERIAL...ENDMATERIAL definition for your material)
 *  
 * --------------------------------------------------------------
 *  
 * if the program runs successfully, the result will be a data file
 * named 'MaterialName.out.' 
 * each line in this data file will have 6 columns: 
 *  
 * w  |  real(eps(w))  |  imag(eps(w))  |  mu(w)  |  eps(iw)  | mu(iw)
 *  
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <memory>
#include <string>

#include <ScuffCLI.h>
#include "libhrutil.h"
#include "libMatProp.h"
#include "libscuff.h"

#define OMEGAMIN 1.0e8
#define OMEGAMAX 1.0e16
#define PTS_PER_DECADE 20

using namespace scuff;

/***************************************************************/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[])
{ 
  /***************************************************************/
  /*- process command-line arguments  ****************************/
  /***************************************************************/
  std::string Material;
  double OmegaMin=OMEGAMIN;
  double OmegaMax=OMEGAMAX;
  double LengthUnit=0.0;
  bool GnuPlot=false;
  std::string GeoFile;

  CLI::App App{"plot epsilon and mu versus frequency for a material"};
  App.option_defaults()->ignore_case();

  App.add_option("--material", Material, "name of material")
     ->required()
     ->take_last();
  App.add_option("--OmegaMin", OmegaMin, "minimum angular frequency")->take_last();
  App.add_option("--OmegaMax", OmegaMax, "maximum angular frequency")->take_last();
  App.add_option("--LengthUnit", LengthUnit, "length unit")->take_last();
  App.add_flag("--gnuplot", GnuPlot, "use GNUPLOT to plot results");
  App.add_option("--geometry", GeoFile,
                 ".scuffgeo file containing material definition")->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;

  if (LengthUnit!=0.0)
   MatProp::SetLengthUnit(LengthUnit);

  if (OmegaMax<=OmegaMin)
   ErrExit("--OmegaMax must be greater than --OmegaMin");

  SetLogFileName("scuff-plotEpsMu.log");

  /***************************************************************/
  /* if the user specified a .scuffgeo file, we need to create an*/
  /* RWGGeometry for it in order to get its MATERIAL definitions */
  /* into the system.                                            */
  /***************************************************************/
  std::unique_ptr<RWGGeometry> G( GeoFile.empty() ? 0 : new RWGGeometry(GeoFile.c_str()) );
  if (G.get()!=0)
   { Log("Reading material definitions from geometry file %s.\n",G->GeoFileName);
     MatProp::SetFreqUnit(1.0);
   };

  /***************************************************************/
  /* try to create a MatProp for the specified material          */
  /***************************************************************/
  std::unique_ptr<MatProp> MP(new MatProp(Material.c_str()));
  if (MP->ErrMsg)
   ErrExit("%s",MP->ErrMsg);

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  int HaveRealFreqData=1, HaveImagFreqData=1;
  if (MP->Type==MP_INTERP && MP->InterpReal==0)
   HaveRealFreqData=0;
  if (MP->Type==MP_INTERP && MP->InterpImag==0)
   HaveImagFreqData=0;

  /***************************************************************/
  /*- write eps, mu vs. frequency to data file                   */
  /***************************************************************/
  cdouble Eps; 
  cdouble Mu;
  double OmegaMult=pow(10.0, 1.0 / PTS_PER_DECADE);
  FILE *f=vfopen("%s.epsmu","w",MP->Name);
  if (!f) ErrExit("could not create file %s.epsmu",MP->Name);
  fprintf(f,"# scuff-plotEpsMu ran on %s (%s)\n",GetHostName(),GetTimeString());
  fprintf(f,"# data file columns\n");
  fprintf(f,"# 1 omega          \n");
  fprintf(f,"# 2 re Eps(omega)  \n");
  fprintf(f,"# 3 im Eps(omega)  \n");
  fprintf(f,"# 4 re Mu(omega)   \n");
  fprintf(f,"# 5 im Mu(omega)   \n");
  fprintf(f,"# 6 Eps (i*omega)  \n");
  fprintf(f,"# 7 Mu  (i*omega)  \n");
  double FreqUnit = MatProp::FreqUnit;
  OmegaMin /= FreqUnit;
  OmegaMax /= FreqUnit;
  for(double Omega=OmegaMin; Omega<=OmegaMax; Omega*=OmegaMult)
   { 
      fprintf(f,"%e ",Omega*FreqUnit);

      if (HaveRealFreqData)
       MP->GetEpsMu( cdouble(Omega,0.0), &Eps, &Mu);
      else 
       Eps=Mu=1.0;
      fprintf(f,"%+15.12e %+15.12e %+15.12e %+15.12e",real(Eps), imag(Eps), real(Mu), imag(Mu));

      if (HaveImagFreqData)
       MP->GetEpsMu( cdouble(0.0,Omega), &Eps, &Mu);
      else
       Eps=Mu=1.0;
      fprintf(f," %+15.12e %+15.12e \n",real(Eps), real(Mu));

   };
  fclose(f);
  printf("Material property data written to file %s.epsmu.\n",MP->Name);
  printf("Thank you for your support.\n");

  /***************************************************************/
  /* generate gnuplot plots if that was requested                */
  /***************************************************************/
  if (GnuPlot)
   { 
     f=popen("gnuplot -persist","w");
     if (!f) 
      ErrExit("could not launch gnuplot");

     fprintf(f,"set logscale x\n");

     if (HaveRealFreqData)
      { fprintf(f,"set title 'Eps,Mu for %s along the real omega axis\n",MP->Name);
        fprintf(f,"set xlabel 'Real(Omega) (radians/second)'\n");
        fprintf(f,"set ylabel 'Relative permittivity / permeability'\n");
        fprintf(f,"plot '%s.epsmu' u 1:2 t 'Re(Eps)' w lp,",MP->Name); 
        fprintf(f,"     '%s.epsmu' u 1:3 t 'Im(Eps)' w lp,",MP->Name); 
        fprintf(f,"     '%s.epsmu' u 1:4 t 'Re(Mu)' w lp,",MP->Name); 
        fprintf(f,"     '%s.epsmu' u 1:5 t 'Im(Mu)' w lp\n",MP->Name); 
      };

     if (HaveImagFreqData)
      { fprintf(f,"set title 'Eps,Mu for %s along the imaginary omega axis\n",MP->Name);
        fprintf(f,"set terminal x11 2\n");
        fprintf(f,"set xlabel 'Imag(Omega) (radians/second)'\n");
        fprintf(f,"set ylabel 'Relative permittivity / permeability'\n");
        fprintf(f,"plot '%s.epsmu' u 1:6 t 'Eps' w lp,",MP->Name); 
        fprintf(f,"     '%s.epsmu' u 1:7 t 'Mu' w lp\n",MP->Name); 
      };

     pclose(f);

   };

}
