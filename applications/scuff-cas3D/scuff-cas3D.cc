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
 * scuff-cas3D.cc  -- a standalone code within the scuff-EM suite for
 *                 -- computing the Casimir energy, force, and/or
 *                 -- torque for a collection of interacting objects
 *
 * homer reid     -- 3/2007 -- 3/2012
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <memory>
#include <string>
#include <vector>

#include <ScuffCLI.h>
#include "scuff-cas3D.h"

/***************************************************************/
/***************************************************************/
/***************************************************************/
#define MAXFREQ  10    // max number of frequencies 
#define MAXCACHE 10    // max number of cache files for preload

#define MAXSTR   1000

/***************************************************************/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[])
{
  InstallHRSignalHandler();
  InitializeLog(argv[0]);

  /***************************************************************/
  /* pre-process command-line arguments to extract arguments     */
  /* any relevant for Brillouin-zone integration.  InitBZIArgs   */
  /* consumes its --BZI* options by nulling their argv slots,    */
  /* which the typed option reader below then skips.             */
  /***************************************************************/
  GetBZIArgStruct *BZIArgs=InitBZIArgs(argc, argv);

  /***************************************************************/
  /* process options *********************************************/
  /***************************************************************/
  std::string GeoFile;
  std::string TransFile;
  bool Energy=false;
  bool XForce=false;
  bool YForce=false;
  bool ZForce=false;
  std::vector<double> TorqueAxes;   // 3 values per occurrence
  bool AllTorque=false;
  double Temperature=0.0;
  std::vector<double> XiVals;
  std::string XiFile;
  std::string XikBlochFile;
  std::string XiQuadrature;
  double XiMin=0.001;
  int MaxXiPoints=10000;
  int Intervals=50;
  double AbsTol=0.0;
  double RelTol=1.0e-2;
  std::string FileBase;
  std::string Cache, WriteCache;
  std::vector<std::string> ReadCache;
  bool UseExistingData = false;
  bool NewEnergyMethod = false;
  bool WriteHDF5Files  = false;

  CLI::App App{"compute Casimir energies, forces, and torques"};
  App.option_defaults()->ignore_case();

  App.add_option("--geometry", GeoFile, "geometry file")
     ->required()
     ->take_last();
  App.add_option("--transfile", TransFile,
                 "list of geometrical transformation")->take_last();

  App.add_flag("--energy", Energy, "compute Casimir energy");
  App.add_flag("--xforce", XForce, "compute x-directed Casimir force");
  App.add_flag("--yforce", YForce, "compute y-directed Casimir force");
  App.add_flag("--zforce", ZForce, "compute z-directed Casimir force");
  App.add_option("--torque", TorqueAxes,
                 "compute Casimir torque about a given axis")
     ->type_size(3)
     ->expected(1, 3);
  App.add_flag("--alltorque", AllTorque,
               "compute all three Casimir torque components");

  App.add_option("--temperature", Temperature, "temperature in Kelvin")->take_last();
  App.add_option("--xi", XiVals, "imaginary frequency")
     ->expected(1, MAXFREQ);
  App.add_option("--xifile", XiFile, "file containing Xi values")->take_last();
  App.add_option("--xikblochfile", XikBlochFile,
                 "file containing (Xi, kx, ky) values")->take_last();
  App.add_option("--xiquadrature", XiQuadrature,
                 "quadrature method for Xi integration")->take_last();
  App.add_option("--ximin", XiMin,
                 "assume Xi integrand constant below this value")->take_last();
  App.add_option("--maxxipoints", MaxXiPoints,
                 "maximum number of Xi integrand evaluations")->take_last();
  App.add_option("--intervals", Intervals,
                 "number of subintervals for frequency quadrature")->take_last();
  App.add_option("--abstol", AbsTol,
                 "absolute tolerance for sums and integrations")->take_last();
  App.add_option("--reltol", RelTol,
                 "relative tolerance for sums and integrations")->take_last();

  App.add_option("--filebase", FileBase, "base filename for output files")->take_last();

  App.add_option("--cache", Cache, "read/write cache")->take_last();
  App.add_option("--readcache", ReadCache, "read cache")->expected(1, MAXCACHE);
  App.add_option("--writecache", WriteCache, "write cache")->take_last();

  App.add_flag("--useexistingdata", UseExistingData,
               "reuse data from existing .byXi files");
  App.add_flag("--newenergymethod", NewEnergyMethod,
               "use alternative method for energy calculation");
  App.add_flag("--writehdf5files", WriteHDF5Files,
               "write BEM matrices to .hdf5 files");

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;

  /*******************************************************************/
  /*******************************************************************/
  /*******************************************************************/
  if (FileBase.empty())
   FileBase=GetFileBase(GeoFile.c_str());

  /***************************************************************/
  /* try to create the geometry  *********************************/
  /***************************************************************/
  std::unique_ptr<RWGGeometry> G(new RWGGeometry(GeoFile.c_str()));
  G->SetLogLevel(SCUFF_VERBOSELOGGING);

  /***************************************************************/
  /* process frequency- and kBloch-related options               */
  /***************************************************************/
  std::unique_ptr<HMatrix> XiPoints;
  std::unique_ptr<HMatrix> XiKPoints;
  bool TemperatureSpecified = App.get_option("--temperature")->count() > 0;
  int nTorque = (int)TorqueAxes.size()/3;

  if ( !XikBlochFile.empty() )
   { if (!XiFile.empty())      ErrExit("--XikBlochFile and --XiFile options are mutually exclusive");
     if (XiVals.size()>0)      ErrExit("--XikBlochFile and --Xi options are mutually exclusive");
     if (TemperatureSpecified) ErrExit("--XikBlochFile and --Temperature options are mutually exclusive");
     if (G->LDim==0)           ErrExit("--XikBlochFile may only be used for periodic geometries");
     XiKPoints.reset(new HMatrix(XikBlochFile.c_str(),LHM_TEXT,"--nc 3 --strict"));
     if (XiKPoints->ErrMsg)
      ErrExit("%s",XiKPoints->ErrMsg);
     Log("Read %i (Xi, kBloch) points from file %s.",XiKPoints->NR, XikBlochFile.c_str());
   }
  else if ( !XiFile.empty() )
   { if (XiVals.size()>0)      ErrExit("--XiFile and --Xi options are mutually exclusive");
     if (TemperatureSpecified) ErrExit("--XiFile and --Temperature options are mutually exclusive");
     XiPoints.reset(new HMatrix(XiFile.c_str(),LHM_TEXT,"--nc 1 --strict"));
     if (XiPoints->ErrMsg)
      ErrExit("%s",XiPoints->ErrMsg);
     Log("Read %i Xi points from file %s.",XiPoints->NR, XiFile.c_str());
   }
  else if ( XiVals.size()>0 )
   { if (TemperatureSpecified) ErrExit("--Xi and --Temperature options are mutually exclusive");
     XiPoints.reset(new HMatrix((int)XiVals.size(), 1));
     for(size_t nxv=0; nxv<XiVals.size(); nxv++)
      XiPoints->SetEntry((int)nxv, 0, XiVals[nxv]);
     Log("Performing Casimir calculations at %i command-line Xi points.",(int)XiVals.size());
   }
  else if ( TemperatureSpecified )
   Log("Computing full Matsubara-summed Casimir quantities at T=%e Kelvin.",Temperature);
  else
   Log("Computing full zero-temperature Casimir quantities.");

  /*******************************************************************/
  /* process integration options *************************************/
  /*******************************************************************/
  if ( !XiQuadrature.empty() && XiPoints )
   ErrExit("--XiQuadrature is incompatible with --Xi or --XiFile");
  if ( !XiQuadrature.empty() && XiKPoints )
   ErrExit("--XiQuadrature is incompatible with --XikBlochFile ");
  if ( !XiQuadrature.empty() && TemperatureSpecified )
   ErrExit("--XiQuadrature is incompatible with --Temperature ");

  int XiQMethod = QMETHOD_CLIFF;
  if (!XiQuadrature.empty())
   { if ( !strcasecmp(XiQuadrature.c_str(),"CLIFF") )
      { XiQMethod = QMETHOD_CLIFF;
        Log("Using cliff integration method for Xi quadrature.");
      }
     else if ( !strcasecmp(XiQuadrature.c_str(),"TRAPSIMP") )
      { XiQMethod = QMETHOD_TRAPSIMP;
        Log("Using trap/simp integration method (%i intervals) for Xi quadrature.",Intervals);
      }
     else if ( !strcasecmp(XiQuadrature.c_str(),"ADAPTIVE") )
      { XiQMethod = QMETHOD_ADAPTIVE;
        Log("Using adaptive integration method for Xi quadrature.");
      }
     else
      ErrExit("unknown value %s specified for --XiQuadrature",XiQuadrature.c_str());
   };

  /*******************************************************************/
  /* figure out which quantities to compute **************************/
  /*******************************************************************/
  if (G->LDim>0)
   { if ( XForce || YForce || nTorque>0 || AllTorque)
      ErrExit("only --energy and --zforce are available for extended geometries");
   };

  int WhichQuantities=0, NumQuantities=0;
  if (Energy)    { NumQuantities++; WhichQuantities |= QUANTITY_ENERGY; };
  if (XForce)    { NumQuantities++; WhichQuantities |= QUANTITY_XFORCE; };
  if (YForce)    { NumQuantities++; WhichQuantities |= QUANTITY_YFORCE; };
  if (ZForce)    { NumQuantities++; WhichQuantities |= QUANTITY_ZFORCE; };
  if (nTorque>0) { NumQuantities++; WhichQuantities |= QUANTITY_TORQUE1; };
  if (nTorque>1) { NumQuantities++; WhichQuantities |= QUANTITY_TORQUE2; };
  if (nTorque>2) { NumQuantities++; WhichQuantities |= QUANTITY_TORQUE3; };
  if (AllTorque)
   { if (nTorque>0) ErrExit("--AllTorque and --Torque options are mutually exclusive");
     NumQuantities+=3;
     WhichQuantities |= (QUANTITY_TORQUE1 + QUANTITY_TORQUE2 + QUANTITY_TORQUE3);
     nTorque=3;
     TorqueAxes.assign(9, 0.0);
     TorqueAxes[0]=TorqueAxes[4]=TorqueAxes[8]=1.0;
   };

  /*******************************************************************/
  /* preload the scuff cache with any cache preload files the user   */
  /* may have specified                                              */
  /*******************************************************************/
  if ( !Cache.empty() && !WriteCache.empty() )
   ErrExit("--cache and --writecache options are mutually exclusive");
  for (size_t nrc=0; nrc<ReadCache.size(); nrc++)
   PreloadCache( ReadCache[nrc].c_str() );
  if (!Cache.empty())
   PreloadCache( Cache.c_str() );

  if (!Cache.empty()) WriteCache=Cache;

  /*******************************************************************/
  /* create the SC3Data structure that contains all the info needed  */
  /* to evaluate the contributions of a single frequency and kBloch  */
  /* point to the Casimir quantities                                 */
  /*******************************************************************/
  SC3Data *SC3D=CreateSC3Data(G.get(), TransFile.c_str(), WhichQuantities, NumQuantities,
                              nTorque, TorqueAxes.data(), NewEnergyMethod, FileBase.c_str());

  SC3D->WriteCache         = WriteCache.empty() ? 0 : WriteCache.c_str();
  SC3D->WriteHDF5Files     = WriteHDF5Files;
  SC3D->AbsTol             = AbsTol;
  SC3D->RelTol             = RelTol;
  SC3D->UseExistingData    = UseExistingData;
  SC3D->MaxXiPoints        = MaxXiPoints;
  SC3D->XiMin              = XiMin;

  if (G->LDim>=1)
   { UpdateBZIArgs(BZIArgs, G->RLBasis, G->RLVolume);
     BZIArgs->BZIFunc  = GetCasimirIntegrand;
     BZIArgs->UserData = (void *)SC3D;
     BZIArgs->FDim     = SC3D->NTNQ;
     SC3D->BZIArgs     = BZIArgs;
   };

  /*******************************************************************/
  /* now switch off based on the requested frequency behavior to     */
  /* perform the actual calculations                                 */
  /*******************************************************************/
  std::vector<double> EFT(SC3D->NTNQ, 0.0);
  std::vector<double> Error;
  if ( XiKPoints.get()!=0 )
   {
     for(int nr=0; nr<XiKPoints->NR; nr++)
      {
        double Xi, kBloch[2]={0.0, 0.0};
        Xi        = XiKPoints->GetEntryD(nr, 0);
        kBloch[0] = XiKPoints->GetEntryD(nr, 1);
        if (G->LDim>=2)
         kBloch[1] = XiKPoints->GetEntryD(nr, 2);
        GetCasimirIntegrand(SC3D, cdouble(0.0,Xi), kBloch, EFT.data());
      };
   }
  else if ( XiPoints.get()!=0 )
   {
     for (int nr=0; nr<XiPoints->NR; nr++)
      GetXiIntegrand(SC3D, XiPoints->GetEntryD(nr,0), EFT.data());
   }
  else if ( Temperature > 0.0)
   {
     Error.assign(SC3D->NTNQ, 0.0);
     GetMatsubaraSum(SC3D, Temperature, EFT.data(), Error.data());
   }
  else // full Xi integration for zero-temperature casimir quantities
   {
     Error.assign(SC3D->NTNQ, 0.0);
     if (XiQMethod == QMETHOD_ADAPTIVE)
      GetXiIntegral_Adaptive(SC3D, EFT.data(), Error.data());
     else if (XiQMethod == QMETHOD_TRAPSIMP)
      GetXiIntegral_TrapSimp(SC3D, Intervals, EFT.data(), Error.data());
     else
      GetXiIntegral_Cliff(SC3D, EFT.data(), Error.data());
   };

  /***************************************************************/
  /* write output file if we computed summed or integrated quantities */
  /***************************************************************/
  if (!Error.empty())
   {
     WriteFilePreamble(SC3D, PREAMBLE_OUT);
     FILE *OutFile=fopen(SC3D->OutFileName,"a");
     int ntnq=0;
     for(int nt=0; nt<SC3D->NumTransformations; nt++)
      { fprintf(OutFile,"%s ",SC3D->GTCs[nt]->Tag);
        for(int nq=0; nq<SC3D->NumQuantities; nq++, ntnq++)
         fprintf(OutFile,"%e %e ",EFT[ntnq],Error[ntnq]);
        fprintf(OutFile,"\n");
      };
     fclose(OutFile);
   };

  DestroyBZIArgs(BZIArgs);

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  printf("Thank you for your support.\n");

}
