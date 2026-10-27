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
 * scuff-caspol.cc   -- a standalone code within the scuff-EM suite for
 *                   -- computing casimir-polder potentials for
 *                   -- polarizable molecules in the vicinity of material
 *                   -- bodies
 *
 * documentation at: 
 *  http://homerreid.com/scuff-em/scuff-caspol
 *
 * homer reid        -- 10/2006 -- 8/2013
 *
 */
#include <time.h>

#include <memory>
#include <string>
#include <vector>

#include <ScuffCLI.h>
#include "scuff-caspol.h"

/***************************************************************/
/***************************************************************/
/***************************************************************/
#define MAXSTR  1000
#define MAXFREQ 10
#define MAXATOM 10

/***************************************************************/
/***************************************************************/
/***************************************************************/
void WriteFilePreamble(SCPData *SCPD, const char *FileName, 
                       int argc, char *argv[], int FileType)
{ 
  FILE *f = fopen(FileName, "a");
  if (!f) return;

  time_t MyTime=time(0);
  struct tm *MyTm=localtime(&MyTime);
  char TimeString[200];
  strftime(TimeString,30,"%D::%T",MyTm);

  fprintf(f,"# scuff-caspol " VERSIONSTRING " run on %s (%s)\n",getenv("HOST"),TimeString);
  fprintf(f,"#\n");
  fprintf(f,"# command line:\n");
  fprintf(f,"#\n");
  fprintf(f,"# scuff-caspol ");
  for(int narg=0; narg<(argc-1); narg++)
   fprintf(f," %s%s",argv[narg+1],(narg%4)==3 ? "\n# " : " ");
  fprintf(f,"\n");
  fprintf(f,"#\n");
  fprintf(f,"# data columns:\n");
  fprintf(f,"#1: X (um) \n");
  fprintf(f,"#2: Y (um) \n");
  fprintf(f,"#3: Z (um) \n");

  int nc=4;
  if (FileType==FILETYPE_BYXI || FileType==FILETYPE_BYXIK)
   { 
     fprintf(f,"#%i: Xi (imaginary frequency) (3e14 rad/sec)\n",nc++);

     int LDim = (FileType==FILETYPE_BYXI ? 0 : SCPD->G->LDim);
     char const *Quantity=0, *Units=0;
     switch(LDim)
      { case 0: Quantity="CP potential per unit frequency";
                Units="neV / (3e14 rad/sec)";
                break;

        case 1: Quantity="CP potential integrand";
                Units="neV / (k_0 * 3e14 rad/sec)";
                fprintf(f,"#%i: kx (bloch vector) (microns^{-1})\n",nc);
                nc+=1;
                break;

        case 2: Quantity="CP potential integrand";
                Units="neV / (k_0^2 * 3e14 rad/sec)";
                fprintf(f,"#%i,%i: kx,ky (bloch vector) (microns^{-1})\n",nc+1,nc+2);
                nc+=2;
                break;
      };
        
     for(int na=0; na<SCPD->NumAtoms; na++)
      { fprintf(f,"#%i: polarizability of %s (a_0^3)\n",
                   nc++,SCPD->PolModels[na]->Name);
        fprintf(f,"#%i: %s for %s (%s)\n",
                   nc++,Quantity,SCPD->PolModels[na]->Name,Units);
      };

   }
  else // (FileType==FILETYPE_OUT
   for(int na=0; na<SCPD->NumAtoms; na++)
    fprintf(f,"#%i: Casimir-polder potential for %s (neV)\n",
               nc++,SCPD->PolModels[na]->Name);

  fclose(f);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
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
  bool PECPlate=false;
  std::vector<std::string> Atoms;
  std::vector<std::string> Particles;
  std::string EPFile;
  double Temperature = 0.0;
  std::vector<double> XiVals;
  std::string XiFile;
  std::string XikBlochFile;
  std::string FileBase;
  double RelTol    = 1.0e-2;
  double AbsTol    = 1.0e-10;

  CLI::App App{"compute Casimir-Polder potentials and polarizabilities"};
  App.option_defaults()->ignore_case();

  App.add_option("--geometry", GeoFile, ".scuffgeo file")->take_last();
  App.add_flag("--PECPlate", PECPlate, "use PEC plate");
  App.add_option("--atom", Atoms,
                 "atomic species (built-in polarizability models)")
     ->expected(1, MAXATOM);
  App.add_option("--particle", Particles,
                 "particle species (user-defined polarizability models)")
     ->expected(1, MAXATOM);
  App.add_option("--EPFile", EPFile, "list of evaluation points")->take_last();
  App.add_option("--temperature", Temperature, "temperature in Kelvin")->take_last();
  App.add_option("--xi", XiVals, "imaginary frequency")->expected(1, MAXFREQ);
  App.add_option("--xifile", XiFile, "file containing Xi values")->take_last();
  App.add_option("--xikblochfile", XikBlochFile,
                 "file containing (Xi,kBloch) values")->take_last();
  App.add_option("--filebase", FileBase, "base name for output files")->take_last();
  App.add_option("--reltol", RelTol, "relative error tolerance")->take_last();
  App.add_option("--abstol", AbsTol, "absolute error tolerance")->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;

  /***************************************************************/
  /* some sanity checks on input arguments ***********************/
  /***************************************************************/
  if (GeoFile.empty() && !PECPlate)
   { CLI::ValidationError Error("scuff-caspol",
        "either --geometry or --PECPlate must be specified");
     App.exit(Error);
     return 1;
   };
  if (!GeoFile.empty() && PECPlate)
   ErrExit("geometry and --PECPlate are mutually exclusive");
  if (EPFile.empty())
   { CLI::ValidationError Error("scuff-caspol","--EPFile option is mandatory");
     App.exit(Error);
     return 1;
   };
  if ( !XiFile.empty() && XiVals.size()>0 )
   ErrExit("--XiFile and --Xi are mutually exclusive");
  bool TemperatureSpecified = App.get_option("--temperature")->count() > 0;
  if ( TemperatureSpecified && (!XiFile.empty() || XiVals.size()>0) )
   ErrExit("--Xi and --XiFile options are incompatible with --Temperature");
  if (Temperature<0.0)
   ErrExit("--temperature may not be negative");
  int NumAtoms = (int)(Atoms.size() + Particles.size());
  if ( Atoms.empty() && Particles.empty() )
   { CLI::ValidationError Error("scuff-caspol",
        "you must specify at least one --atom or --particle");
     App.exit(Error);
     return 1;
   };

  std::vector<const char *> AtomPtrs, ParticlePtrs;
  for(const std::string &A : Atoms)     AtomPtrs.push_back(A.c_str());
  for(const std::string &P : Particles) ParticlePtrs.push_back(P.c_str());

  /*******************************************************************/
  /*******************************************************************/
  /*******************************************************************/
  SCPData *SCPD=CreateSCPData(GeoFile.empty() ? 0 : GeoFile.c_str(),
                              AtomPtrs.data(), (int)Atoms.size(),
                              ParticlePtrs.data(), (int)Particles.size(),
                              EPFile.c_str(),
                              FileBase.empty() ? 0 : FileBase.c_str());

  /*******************************************************************/
  /* fill in a few simple fields in the SCPData structure ************/
  /*******************************************************************/
  SCPD->AbsTol = AbsTol;
  SCPD->RelTol = RelTol;

  if (SCPD->G && SCPD->G->LDim>=1)
   { UpdateBZIArgs(BZIArgs, SCPD->G->RLBasis, SCPD->G->RLVolume);
     BZIArgs->BZIFunc  = GetCPIntegrand;
     BZIArgs->UserData = (void *)SCPD;
     BZIArgs->FDim     = NumAtoms * (SCPD->EPMatrix)->NR;
     SCPD->BZIArgs=BZIArgs;
   };

  WriteFilePreamble(SCPD, SCPD->ByXiFileName, argc, argv,
                    FILETYPE_BYXI);

  if (SCPD->ByXikFileName)
   WriteFilePreamble(SCPD, SCPD->ByXikFileName, argc, argv,
                     FILETYPE_BYXIK);

  /*******************************************************************/
  /* process frequency-related options */
  /*******************************************************************/
  std::unique_ptr<HVector> XiList;
  std::unique_ptr<HMatrix> XikList;
  int LDim = (SCPD->G) ? SCPD->G->LDim : 0;
  if ( !XikBlochFile.empty() )
   { if (LDim==0)
      ErrExit("--XikBloch file cannot be used with non-periodic geometries");
     XikList.reset(new HMatrix(XikBlochFile.c_str()));
     Log("Read %i (Xi,kBloch) points from file %s.",XikList->NR, XikBlochFile.c_str());
   }
  else if (!XiFile.empty())
   { XiList.reset(new HVector(XiFile.c_str()));
     Log("Read %i Xi points from file %s.",XiList->N, XiFile.c_str());
   }
  else if ( XiVals.size()>0 )
   { XiList.reset(new HVector((int)XiVals.size()));
     for(size_t n=0; n<XiVals.size(); n++)
      XiList->SetEntry((int)n,XiVals[n]);
   };

  /*******************************************************************/
  /*******************************************************************/
  /*******************************************************************/
  if ( XikList.get()!=0 )
   Log("Computing CP integrand at %i (Xi,k) points",XikList->NR);
  else if ( XiList.get()!=0 )
   Log("Computing Casimir-Polder quantities at %i user-specified frequency points.",XiList->N);
  else if ( Temperature!=0.0 )
   Log("Computing full Matsubara-summed Casimir-Polder potentials at T=%e Kelvin.",Temperature);
  else
   Log("Computing full zero-temperature Casimir-Polder potentials.");

  /*******************************************************************/
  /* main program body:                                              */
  /*                                                                 */
  /*  (a) if the user specified a set of frequencies, then we just   */
  /*      calculate the contributions to the casimir-polder potential*/
  /*      from those frequencies.                                    */
  /*                                                                 */
  /*  (b) if the user specified a temperature, then we evaluate the  */
  /*      matsubara sum for the full casimir-polder potential at     */
  /*      that temperature.                                          */
  /*                                                                 */
  /*  (c) if the user specified neither of the above, then we        */
  /*      evaluate the integral over the imaginary-frequency axis    */
  /*      to get the full zero-temperature casimir-polder potential. */
  /*                                                                 */
  /*******************************************************************/
  int NumEvalPoints=SCPD->EPMatrix->NR;
  int NumDataValues = NumEvalPoints*NumAtoms;
  std::vector<double> U(NumDataValues, 0.0);
  if (XikList.get()!=0)
   {
     for(int n=0; n<XikList->NR; n++)
      {
        double Xi, kBloch[2]={0.0, 0.0};
        Xi = XikList->GetEntryD(n,0);
        kBloch[0] = XikList->GetEntryD(n,1);
        if (LDim>1)
         kBloch[1] = XikList->GetEntryD(n,2);

        GetCPIntegrand((void *)SCPD, cdouble(0.0,Xi), kBloch, U.data());
      }
   }
  if (XiList.get()!=0)
   {
     for(int nXi=0; nXi<XiList->N; nXi++)
      GetXiIntegrand(SCPD, XiList->GetEntryD(nXi), U.data());
   }
  else if ( Temperature != 0.0 )
   {
     EvaluateMatsubaraSum(SCPD, Temperature, U.data());
   }
  else
   {
     EvaluateFrequencyIntegral(SCPD, U.data());
   };

  /***************************************************************/
  /* write frequency-integrated or Matsubara-summed results to   */
  /* output file                                                 */
  /***************************************************************/
  if (XiList.get()==0)
   {
     std::string OutFileName=std::string(GetFileBase(GeoFile.empty()
                                ? "PECPlate" : GeoFile.c_str())) + ".out";
     WriteFilePreamble(SCPD, OutFileName.c_str(), argc, argv, FILETYPE_OUT);

     FILE *f=fopen(OutFileName.c_str(),"a");
     HMatrix *EPMatrix=SCPD->EPMatrix;
     for(int nep=0; nep<EPMatrix->NR; nep++)
      { fprintf(f,"%e %e %e ", EPMatrix->GetEntryD(nep,0),
                               EPMatrix->GetEntryD(nep,1),
                               EPMatrix->GetEntryD(nep,2) );
        for(int na=0; na<NumAtoms; na++)
         fprintf(f,"%e ", U[nep*NumAtoms + na]);

        fprintf(f,"\n");
      };
     fclose(f);

     if (Temperature>0.0)
      printf("Matsubara-summed data written to file %s.\n",OutFileName.c_str());
     else
      printf("Frequency-integrated data written to file %s.\n",OutFileName.c_str());
   };

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  DestroyBZIArgs(BZIArgs);
  printf("Frequency-resolved data written to file %s.\n",SCPD->ByXiFileName);
  printf("Thank you for your support.\n");

}
