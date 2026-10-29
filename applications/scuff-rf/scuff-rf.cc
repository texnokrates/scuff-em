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
 * scuff-rf.cc   -- scuff-EM module for modeling of RF and microwave
 *               -- structures
 * 
 * homer reid    -- 9/2011
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

#include <memory>
#include <string>
#include <vector>

#include <ScuffCLI.h>
#include "RWGPorts.h"

#define FREQ2OMEGA (2.0*M_PI/300.0)
#define II cdouble(0.0,1.0)

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
extern int WriteLogFile;
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

using namespace scuff;

#define MAXFREQ  10    // max number of frequencies
#define MAXCACHE 10    // max number of cache files for preload
#define MAXSTR   1000
#define MAXEPF   10    // max number of field visualization files
#define MAXFVM   10    // max number of field visualization meshes

/***************************************************************/
/***************************************************************/
/***************************************************************/
void ProcessEPFile(RWGGeometry *G, HVector *KN, cdouble Omega,
                   RWGPort **Ports, int NumPorts, cdouble *PortCurrents,
                   const char *EPFile, const char *FileBase);

void ProcessFVMesh(RWGGeometry *G, HVector *KN, cdouble Omega,
                   RWGPort **Ports, int NumPorts, cdouble *PortCurrents,
                   const char *FVMesh, const char *FileBase);

/***************************************************************/
/***************************************************************/
/***************************************************************/
void HVConcat(HVector *V1, HVector *V2)
{ 
  if ( V1==nullptr || V2==nullptr || V2->N==0 ) return;

  int NewN=V1->N + V2->N;
  V1->DV = (double *)realloc(V1->DV, NewN*sizeof(double));
  memcpy(V1->DV + V1->N, V2->DV, (V2->N)*sizeof(double));
  V1->N=NewN;

}

/***************************************************************/
/* main function   *********************************************/
/***************************************************************/  
int main(int argc, char *argv[])
{
  /***************************************************************/
  /** process command-line arguments *****************************/
  /***************************************************************/
  std::string GeoFile;
  std::string PortFile;
  std::string PCFile;
  std::vector<std::string> EPFiles;
  std::vector<std::string> FVMeshes;
  bool PlotPorts=false;
  bool ZParameters=false;
  bool SParameters=false;
  bool Moments=false;
  std::vector<double> FrequencyValues;
  double MinFreq=0.0;
  double MaxFreq=0.0;
  int NumFreqsOpt=0;
  bool LogFreq=false;
  std::string FreqFile;
  std::string Cache, WriteCache;
  std::vector<std::string> ReadCache;
  std::string FileBase;
  bool WriteLogFileOpt=false;

  CLI::App App{"RF port analysis for scuff-EM geometries"};
  App.option_defaults()->ignore_case();

  App.add_option("--geometry", GeoFile, "geometry file")
     ->required()
     ->take_last();
  App.add_option("--portfile", PortFile, "port file")->required()->take_last();
  App.add_flag("--PlotPorts", PlotPorts, "generate port visualization file");
  App.add_option("--frequency", FrequencyValues, "frequency (GHz)")
     ->expected(1, MAXFREQ);
  App.add_option("--minfreq", MinFreq, "starting frequency")->take_last();
  App.add_option("--maxfreq", MaxFreq, "ending frequency")->take_last();
  App.add_option("--numfreqs", NumFreqsOpt, "number of frequencies")->take_last();
  App.add_flag("--logfreq", LogFreq, "use logarithmic frequency steps");
  App.add_option("--freqfile", FreqFile, "list of frequencies")->take_last();
  App.add_flag("--ZParameters", ZParameters, "output z parameters");
  App.add_flag("--SParameters", SParameters, "output s parameters");
  App.add_flag("--Moments", Moments, "output dipole moments");
  App.add_option("--portcurrentfile", PCFile, "port current file")->take_last();
  App.add_option("--EPFile", EPFiles, "list of evaluation points")
     ->expected(1, MAXEPF);
  App.add_option("--FVMesh", FVMeshes, "field visualization mesh")
     ->expected(1, MAXFVM);
  App.add_option("--FileBase", FileBase, "base name for output files")->take_last();
  App.add_option("--cache", Cache, "read/write cache")->take_last();
  App.add_option("--readcache", ReadCache, "read cache")->expected(1, MAXCACHE);
  App.add_option("--writecache", WriteCache, "write cache")->take_last();
  App.add_flag("--WriteLogFile", WriteLogFileOpt, "write new log file");

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;
  int nEPFiles  = (int)EPFiles.size();
  int nFVMeshes = (int)FVMeshes.size();

  /* legacy: the --WriteLogFile flag poked libhrutil's log-file switch */
  extern int WriteLogFile;
  if (WriteLogFileOpt)
   WriteLogFile=1;

  if (FileBase.empty())
   FileBase=GetFileBase(GeoFile.c_str());

  /***************************************************************/
  /* create the log file *****************************************/
  /***************************************************************/
  SetLogFileName("scuff-rf.log");
  Log("%s " VERSIONSTRING " running on %s with arguments ",argv[0],getenv("HOST"));
  Log("%s ",argv[0]);
  for(int narg=1; narg<argc; narg++)
   LogC("%s ",argv[narg]);

  /***************************************************************/
  /* create the geometry                                         */
  /***************************************************************/
  RWGGeometry::UseHRWGFunctions=false;
  std::unique_ptr<RWGGeometry> G(new RWGGeometry(GeoFile.c_str()));

  std::unique_ptr<HMatrix> M(G->AllocateBEMMatrix());
  std::unique_ptr<HVector> KN(G->AllocateRHSVector());

  int NumPorts;
  RWGPort **Ports=ParsePortFile(G.get(), PortFile.c_str(), &NumPorts);

  if (PlotPorts)
   {
     fprintf(stderr,"--PlotPorts option was specified; plotting ports ONLY.\n");
     for(int np=0; np<NumPorts; np++)
      { fprintf(stderr," Port %2i, positive edge: %i edges (perimeter %e)\n", np,
                         Ports[np]->NumPEdges, Ports[np]->PPerimeter);
        fprintf(stderr," Port %2i, negative edge: %i edges (perimeter %e)\n", np,
                         Ports[np]->NumMEdges, Ports[np]->MPerimeter);
      };

     PlotPortsInGMSH(Ports, NumPorts, "%s.ports.pp", GetFileBase(G->GeoFileName));
     fprintf(stderr,"RF ports plotted to file %s.ports.pp.\n",GetFileBase(G->GeoFileName));
     fprintf(stderr,"Thank you for your support.\n");
     return 0;   /* legacy exited with status 1 on this success path */
   };


  if (PlotPorts)
   { 
     fprintf(stderr,"--PlotPorts option was specified; plotting ports ONLY.\n");
     for(int np=0; np<NumPorts; np++)
      { fprintf(stderr," Port %2i, positive edge: %i edges (perimeter %e)\n", NumPorts, 
                         Ports[np]->NumPEdges, Ports[np]->PPerimeter);
        fprintf(stderr," Port %2i, negative edge: %i edges (perimeter %e)\n", NumPorts, 
                         Ports[np]->NumMEdges, Ports[np]->MPerimeter);
      };
     
     PlotPortsInGMSH(Ports, NumPorts, "%s.ports.pp", GetFileBase(G->GeoFileName));
     fprintf(stderr,"RF ports plotted to file %s.ports.pp.\n",GetFileBase(G->GeoFileName));
     fprintf(stderr,"Thank you for your support.\n");
     exit(1);
   };

  /***************************************************************/ 
  /* parse the port-current list if that was specified           */ 
  /***************************************************************/
  std::unique_ptr<HMatrix> PCList;
  std::unique_ptr<HVector> FreqList;
  int nf;
  if (!PCFile.empty())
   {
     PCList.reset(new HMatrix(PCFile.c_str(),LHM_TEXT));
     if (PCList->ErrMsg)
      ErrExit("%s: %s",PCFile.c_str(),PCList->ErrMsg);
     if (PCList->NC != 2*NumPorts+1)
      ErrExit("%s: expected %i columns (2*%i ports + 1)\n",PCFile.c_str(),2*NumPorts+1,NumPorts);
     FreqList.reset(new HVector(PCList->NR));
     for(nf=0; nf<FreqList->N; nf++)
      FreqList->SetEntry(nf,PCList->GetEntryD(nf,0));
   };

  /***************************************************************/
  /* parse frequency specifications to create the FreqList vector*/
  /***************************************************************/
  bool MinFreqSpecified = App.get_option("--minfreq")->count() > 0;
  bool MaxFreqSpecified = App.get_option("--maxfreq")->count() > 0;
  bool NumFreqsSpecified= App.get_option("--numfreqs")->count() > 0;
  std::unique_ptr<HVector> FV1, FV2, FV3;
  if (!FreqFile.empty())
   { FV1.reset(new HVector(FreqFile.c_str()));
     if (FV1->ErrMsg)
      ErrExit("%s",FV1->ErrMsg);
   };
  if (FrequencyValues.size()>0)
   { FV2.reset(new HVector((int)FrequencyValues.size()));
     memcpy(FV2->DV, FrequencyValues.data(), FrequencyValues.size()*sizeof(double));
   };
  if (MinFreqSpecified || MaxFreqSpecified || NumFreqsSpecified)
   { if ( !MinFreqSpecified || !MaxFreqSpecified || !NumFreqsSpecified )
      ErrExit("--MinFreq, --MaxFreq, --NumFreqs must be all present or all absent");
     FV3.reset( LogFreq ? LogSpace(MinFreq, MaxFreq, NumFreqsOpt)
                        : LinSpace(MinFreq, MaxFreq, NumFreqsOpt) );
   };
  int NumFreqs=0;
  if ( FV1.get() || FV2.get() || FV3.get() )
   { if (!PCFile.empty() )
      ErrExit("--portcurrentfile may not be combined with a frequency specification");
     FreqList.reset(new HVector(0));
     HVConcat(FreqList.get(), FV1.get());
     HVConcat(FreqList.get(), FV2.get());
     HVConcat(FreqList.get(), FV3.get());
     NumFreqs=FreqList->N;
   };

  /***************************************************************/
  /* sanity check input arguments ********************************/
  /***************************************************************/
  if ( PCFile.empty() && NumFreqs!=0 && (!ZParameters && !SParameters) )
   ErrExit("--zparameters and/or --sparameters must be specified if a frequency specification is present");
  if (!PCFile.empty() && (ZParameters || SParameters) )
   ErrExit("--zparameters and --sparameters may not be used with --portcurrentfile");
  if (PCList.get()!=nullptr && nEPFiles==0 && nFVMeshes==0)
   ErrExit("--EPFile or --FVMesh must be specified if --portcurrentfile is specified");

  /***************************************************************/
  /* create output files *****************************************/
  /***************************************************************/
  char TimeString[200];
  time_t MyTime=time(nullptr);
  struct tm *MyTm=localtime(&MyTime);
  strftime(TimeString,30,"%D::%T",MyTm);

  FILE *ZParFile=nullptr;
  char ZParFileName[1000];
  std::unique_ptr<HMatrix> ZMatrix;
  if (ZParameters)
   {
     snprintf(ZParFileName,999,"%s.zparms",GetFileBase(G->GeoFileName));
     ZParFile=CreateUniqueFile(ZParFileName,1,ZParFileName);
     setbuf(ZParFile,nullptr);
     fprintf(ZParFile,"# %s ran on %s (%s)\n",argv[0],getenv("HOST"),TimeString);
     fprintf(ZParFile,"# command-line arguments: \n#");
     for(int narg=0; narg<argc; narg++)
      fprintf(ZParFile,"%s ",argv[narg]);
     fprintf(ZParFile,"\n#\n");
     fprintf(ZParFile,"# columns:\n");
     fprintf(ZParFile,"# 1 frequency (GHz)\n");
     fprintf(ZParFile,"# 2,3 real,imag Z_{11} \n");
     if (NumPorts>1)
      { fprintf(ZParFile,"# 4,5 real,imag Z_{12} \n");
        fprintf(ZParFile,"# ...\n");
        fprintf(ZParFile,"# %i, %i real, imag Z_{%i%i}\n",
                   2*NumPorts*NumPorts, 2*NumPorts*NumPorts+1, 
                   NumPorts+1,NumPorts+1);
      };

     ZMatrix.reset(new HMatrix(NumPorts, NumPorts, LHM_COMPLEX));
   };

  FILE *SParFile=nullptr;
  char SParFileName[1000];
  std::unique_ptr<HMatrix> SMatrix;
  if (SParameters)
   { 
     snprintf(SParFileName,999,"%s.sparms",GetFileBase(G->GeoFileName));
     SParFile=CreateUniqueFile(SParFileName,1,SParFileName);
     setbuf(SParFile,nullptr);
     fprintf(SParFile,"# %s ran on %s (%s)\n",argv[0],getenv("HOST"),TimeString);
     fprintf(SParFile,"# command-line arguments: \n#");
     for(int narg=0; narg<argc; narg++)
      fprintf(SParFile,"%s ",argv[narg]);
     fprintf(SParFile,"\n#\n"); 
     fprintf(SParFile,"# columns:\n");
     fprintf(SParFile,"# 1 frequency (GHz)\n");
     fprintf(SParFile,"# 2,3 real,imag S_{11} \n");
     if (NumPorts>1)
      { fprintf(SParFile,"# 4,5 real,imag S_{12} \n");
        fprintf(SParFile,"# ...\n");
        fprintf(SParFile,"# %i, %i real, imag S_{%i%i}\n",
                   2*NumPorts*NumPorts,2*NumPorts*NumPorts+1, 
                   NumPorts+1,NumPorts+1);
      };

     SMatrix.reset(new HMatrix(NumPorts, NumPorts, LHM_COMPLEX));
     if (ZMatrix==nullptr) //ZMatrix is needed if s-parameters are computed
      ZMatrix.reset(new HMatrix(NumPorts, NumPorts, LHM_COMPLEX)); 
   };

  /*******************************************************************/
  /*******************************************************************/
  /*******************************************************************/
  FILE *MomentFile=nullptr;
  std::unique_ptr<HMatrix> PM, PSD;
  if (Moments)
   { PM.reset(new HMatrix(G->NumSurfaces, 6, LHM_COMPLEX));
     MomentFile=vfopen("%s.moments","w",GetFileBase(G->GeoFileName));
     PSD.reset(new HMatrix(G->TotalPanels, 13, LHM_COMPLEX));
   }

  /*******************************************************************/
  /* preload the scuff cache with any cache preload files the user   */
  /* may have specified                                              */
  /*******************************************************************/
  if ( !Cache.empty() && !WriteCache.empty() )
   ErrExit("--cache and --writecache options are mutually exclusive");
  if (!Cache.empty())
   WriteCache=Cache;
  for (size_t nrc=0; nrc<ReadCache.size(); nrc++)
   PreloadCache( ReadCache[nrc].c_str() );
  if (!Cache.empty())
   PreloadCache( Cache.c_str() );

  /***************************************************************/  
  /* sweep over frequencies                                      */
  /* note: Freq is measured in GHz, while Omega is measured in   */
  /* our natural unit for angular frequency, which is            */
  /* c / (1 mm) = 3e11 rad/sec                                   */
  /***************************************************************/
  int np, npp;
  double Freq = 0.0;
  cdouble Omega;
  std::vector<cdouble> PortCurrents(NumPorts, cdouble(0.0,0.0));
  std::vector<cdouble> PortVoltages(NumPorts, cdouble(0.0,0.0));
  for (nf=0, Freq=FreqList->GetEntryD(0); nf<FreqList->N; Freq=FreqList->GetEntryD(++nf) )
   { 
      /*--------------------------------------------------------------*/
      /* assemble and factorize the BEM matrix at this frequency      */
      /*--------------------------------------------------------------*/
      Omega=FREQ2OMEGA * Freq;
      Log("Assembling BEM matrix at f=%g GHz...",Freq);
      G->AssembleBEMMatrix(Omega, M.get());
      Log("Factorizing...");
      M->LUFactorize();

      /*--------------------------------------------------------------*/
      /*--------------------------------------------------------------*/
      /*--------------------------------------------------------------*/
      if (!WriteCache.empty())
       { StoreCache( WriteCache.c_str() );
         WriteCache.clear();
       };

      /*--------------------------------------------------------------*/
      /* if the user asked us to compute Z- or S- parameters, compute */
      /* the rows of the Z-matrix at this frequency                   */
      /*--------------------------------------------------------------*/
      if (ZParameters || SParameters)
       { for(np=0; np<NumPorts; np++)
          { 
            Log(" Computing row %i of the impedance matrix:",np+1);

            Log("  assembling RHS vector");
            std::fill(PortCurrents.begin(), PortCurrents.end(), cdouble(0.0,0.0));
            PortCurrents[np]=1.0;
            KN->Zero();
            AddPortContributionsToRHS(G.get(), Ports, NumPorts, PortCurrents.data(), Omega, KN.get());

            /*--------------------------------------------------------------*/
            /*--------------------------------------------------------------*/
            /*--------------------------------------------------------------*/
            if (Moments)
             { G->GetDipoleMoments(Omega, KN.get(), PM.get());
               SetDefaultCD2SFormat("%+.6e %+.6e");
               for(int ns=0; ns<G->NumSurfaces; ns++)
                fprintf(MomentFile,"%e %i BEFORE %s %s %s %s %s %s %s \n",
                                    real(Omega),G->TotalPanels,G->Surfaces[ns]->Label,
                                    CD2S(PM->GetEntry(ns,0)), CD2S(PM->GetEntry(ns,1)), CD2S(PM->GetEntry(ns,2)),
                                    CD2S(PM->GetEntry(ns,3)), CD2S(PM->GetEntry(ns,4)), CD2S(PM->GetEntry(ns,5)));

               G->GetPanelSourceDensities(Omega, KN.get(), PSD.get());
               G->PlotSurfaceCurrents(PSD.get(), Omega, "%s_Before(WO).pp",GetFileBase(G->GeoFileName));

               SetDefaultCD2SFormat("{%+.2e,%+.2e}");
               cdouble Charge=0.0, DPM[3]={0.0, 0.0, 0.0};
               for(np=0; np<PSD->NR; np++)
                { cdouble Q = PSD->GetEntry(np,3) * PSD->GetEntry(np,4);
                  Charge += Q;
                  DPM[0] += PSD->GetEntry(np,0) * Q;
                  DPM[1] += PSD->GetEntry(np,1) * Q;
                  DPM[2] += PSD->GetEntry(np,2) * Q;
                }
               printf("{Q,P} before (WO) = %s %s %s %s\n",CD2S(Charge),CD2S(DPM[0]),CD2S(DPM[1]),CD2S(DPM[2]));

               AddPortContributionsToPSD(G.get(), Ports, NumPorts, PortCurrents.data(), Omega, PSD.get());
               G->PlotSurfaceCurrents(PSD.get(), Omega, "%s_Before(W).pp",GetFileBase(G->GeoFileName));

               Charge=0.0; DPM[0]=DPM[1]=DPM[2]=0.0;
               for(np=0; np<PSD->NR; np++)
                { cdouble Q = PSD->GetEntry(np,3) * PSD->GetEntry(np,4);
                  Charge += Q;
                  DPM[0] += PSD->GetEntry(np,0) * Q;
                  DPM[1] += PSD->GetEntry(np,1) * Q;
                  DPM[2] += PSD->GetEntry(np,2) * Q;
                }
               printf("{Q,P} before (W) = %s %s %s %s\n",CD2S(Charge),CD2S(DPM[0]),CD2S(DPM[1]),CD2S(DPM[2]));
             };
                
            /*--------------------------------------------------------------*/
            /*--------------------------------------------------------------*/
            /*--------------------------------------------------------------*/
            Log("  solving the BEM system");
            M->LUSolve(KN.get());

            /*--------------------------------------------------------------*/
            /*--------------------------------------------------------------*/
            /*--------------------------------------------------------------*/
            if (Moments)
             { G->GetDipoleMoments(Omega, KN.get(), PM.get());
               SetDefaultCD2SFormat("%+.6e %+.6e");
               for(int ns=0; ns<G->NumSurfaces; ns++)
                fprintf(MomentFile,"%e %i AFTER %s %s %s %s %s %s %s \n",
                                    real(Omega),G->TotalPanels,G->Surfaces[ns]->Label,
                                    CD2S(PM->GetEntry(ns,0)), CD2S(PM->GetEntry(ns,1)), CD2S(PM->GetEntry(ns,2)),
                                    CD2S(PM->GetEntry(ns,3)), CD2S(PM->GetEntry(ns,4)), CD2S(PM->GetEntry(ns,5)));

               G->GetPanelSourceDensities(Omega, KN.get(), PSD.get());
               G->PlotSurfaceCurrents(PSD.get(), Omega, "%s_AfterWO.pp",GetFileBase(G->GeoFileName));

               SetDefaultCD2SFormat("{%+.2e,%+.2e}");
               cdouble Charge=0.0, DPM[3]={0.0, 0.0, 0.0};
               for(np=0; np<PSD->NR; np++)
                { cdouble Q = PSD->GetEntry(np,3) * PSD->GetEntry(np,4);
                  Charge += Q;
                  DPM[0] += PSD->GetEntry(np,0) * Q;
                  DPM[1] += PSD->GetEntry(np,1) * Q;
                  DPM[2] += PSD->GetEntry(np,2) * Q;
                }
               printf("{Q,P} after (WO) = %s %s %s %s\n",CD2S(Charge),CD2S(DPM[0]),CD2S(DPM[1]),CD2S(DPM[2]));

               AddPortContributionsToPSD(G.get(), Ports, NumPorts, PortCurrents.data(), Omega, PSD.get());
               G->PlotSurfaceCurrents(PSD.get(), Omega, "%s_AfterW.pp",GetFileBase(G->GeoFileName));

               Charge=DPM[0]=DPM[1]=DPM[2]=0.0;
               for(np=0; np<PSD->NR; np++)
                { cdouble Q = PSD->GetEntry(np,3) * PSD->GetEntry(np,4);
                  Charge += Q;
                  DPM[0] += PSD->GetEntry(np,0) * Q;
                  DPM[1] += PSD->GetEntry(np,1) * Q;
                  DPM[2] += PSD->GetEntry(np,2) * Q;
                }
               printf("{Q,P} after (W) = %s %s %s %s\n",CD2S(Charge),CD2S(DPM[0]),CD2S(DPM[1]),CD2S(DPM[2]));
             };

            Log("  computing port voltages");
            GetPortVoltages(G.get(), KN.get(), Ports, NumPorts, PortCurrents.data(), Omega, PortVoltages.data());

            /* note: the entry in the Z-matrix is the complex conjugate */
            /* of the measured port voltage, because the Z-matrix is    */
            /* defined using the usual circuit theory convention in     */
            /* which all quantities have time dependence exp(+iwt),     */
            /* whereas scuff-EM uses the opposite sign convention.      */
            for(npp=0; npp<NumPorts; npp++)
             ZMatrix->SetEntry(np, npp, conj(PortVoltages[npp]));
          };

         /*--------------------------------------------------------------*/
         /*- write Z parameters to output file if that was requested    -*/
         /*--------------------------------------------------------------*/
         if (ZParameters)
          { fprintf(ZParFile,"%e ",Freq);
            for(np=0; np<NumPorts; np++)
             for(npp=0; npp<NumPorts; npp++)
              fprintf(ZParFile,"%e %e ",real(ZMatrix->GetEntry(np,npp)), 
                                        imag(ZMatrix->GetEntry(np,npp)));
            fprintf(ZParFile,"\n");
          };

         /*--------------------------------------------------------------*/
         /*- write S parameters to output file if that was requested    -*/
         /*--------------------------------------------------------------*/
         if (SParameters)
          { ZToS(ZMatrix.get(), SMatrix.get());
            fprintf(SParFile,"%e ",Freq);
            for(np=0; np<NumPorts; np++)
             for(npp=0; npp<NumPorts; npp++)
              fprintf(SParFile,"%e %e ",real(SMatrix->GetEntry(np,npp)), imag(SMatrix->GetEntry(np,npp)));
            fprintf(SParFile,"\n");
          };
       }; // if (ZParameters || SParameters) 

      /*--------------------------------------------------------------*/
      /*- if the user gave us driving port currents and asked for the */
      /*- radiated fields at a list of points/and or on a user-       */
      /*- specified flux-mesh surface, handle that                    */
      /*--------------------------------------------------------------*/
      if (nEPFiles!=0 || nFVMeshes!=0)
       { 
         Log(" Computing radiated fields..."); 

         /*--------------------------------------------------------------*/
         /* get the RHS vector corresponding to the user-specified port  */
         /* currents at this frequency                                   */
         /*--------------------------------------------------------------*/
         Log("  assembling RHS vector");
         for(np=0; np<NumPorts; np++)
          PortCurrents[np]=cdouble( PCList->GetEntryD(nf, 2*np+1),
                                    PCList->GetEntryD(nf, 2*np+2));
         KN->Zero();
         AddPortContributionsToRHS(G.get(), Ports, NumPorts, PortCurrents.data(), Omega, KN.get());

         /*--------------------------------------------------------------*/
         /*- solve the system and evaluate the radiated fields   --------*/
         /*--------------------------------------------------------------*/
         Log("  solving the BEM system");
         M->LUSolve(KN.get());
         
         for(int n=0; n<nEPFiles; n++)
          ProcessEPFile(G.get(), KN.get(), Omega, Ports, NumPorts,
                        PortCurrents.data(), EPFiles[n].c_str(), FileBase.c_str());

         for(int n=0; n<nFVMeshes; n++)
          ProcessFVMesh(G.get(), KN.get(), Omega, Ports, NumPorts,
                        PortCurrents.data(), FVMeshes[n].c_str(), FileBase.c_str());

       };

   }; // for (nf=0, Freq=FreqList->GetEntryD(0); nf<FreqList->N; Freq=FreqList->GetEntryD(++nf) )

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (ZParameters)
   { fclose(ZParFile);
     printf("Z-parameters vs. frequency written to file %s\n",ZParFileName);
   }
  if (SParameters)
   { fclose(SParFile);
     printf("S-parameters vs. frequency written to file %s\n",SParFileName);
   }
  if (Moments)
   fclose(MomentFile);
  printf("Thank you for your support.\n");
}
