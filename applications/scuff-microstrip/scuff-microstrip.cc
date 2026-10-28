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
 * scuff-microstrip.cc -- scuff-EM module for RF modeling of microstrip
 *                     -- geometries
 * 
 * homer reid    -- 3/2018
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libhrutil.h>
#include <libhmat.h>
#include <libscuff.h>
#include "ScuffCLI.h"
#include <libIncField.h>
#include <libSubstrate.h>
#include <scuffSolver.h>

#define II cdouble(0.0,1.0)

using namespace scuff;

#define MAXFREQ  10    // max number of frequencies
#define MAXSTR   1000
#define MAXEPF   10    // max number of field visualization files
#define MAXFVM   10    // max number of field visualization meshes

/***************************************************************/
/***************************************************************/
/***************************************************************/
void InitZSParmFile(char *FileBase, int NumPorts, char ZS, int NT)
{ FILE *f=vfopen("%s.%cparms","w",FileBase,ZS);
  setbuf(f,0);
  char TimeString[200];
  time_t MyTime=time(0);
  struct tm *MyTm=localtime(&MyTime);
  strftime(TimeString,30,"%D::%T",MyTm);
  fprintf(f,"# scuff-microstrip ran on %s (%s)\n",getenv("HOST"),TimeString);
  fprintf(f,"# columns:\n");
  fprintf(f,"# 1 frequency (GHz)\n");
  int nc=2;
  if (NT>1)
   fprintf(f,"# %i transform tag\n",nc++);
  for(int ndp=0; ndp<NumPorts; ndp++)
   for(int nsp=0; nsp<NumPorts; nsp++, nc+=2)
    fprintf(f,"#%i,%i real,imag %c_{%i%i}\n",nc,nc+1,ZS,ndp+1,nsp+1);
  fclose(f);
}

void WriteZSParms(char *FileBase, char *Tag, char ZS, double Freq, HMatrix *ZSMatrix, HMatrix **ZTerms=0)
{
  FILE *f=vfopen("%s.%cparms","a",FileBase,ZS);
  fprintf(f,"%e ",Freq);
  if (Tag) fprintf(f,"%s ",Tag);
  for(int ndPort=0; ndPort<ZSMatrix->NR; ndPort++)
   for(int nsPort=0; nsPort<ZSMatrix->NC; nsPort++)
    fprintf(f,"%e %e ",real(ZSMatrix->GetEntry(ndPort,nsPort)), imag(ZSMatrix->GetEntry(ndPort,nsPort)));
  if (ZTerms)
   for(int nt=0; nt<3; nt++)
    for(int ndPort=0; ndPort<ZSMatrix->NR; ndPort++)
     for(int nsPort=0; nsPort<ZSMatrix->NC; nsPort++)
      fprintf(f,"%e %e ",real(ZTerms[nt]->GetEntry(ndPort,nsPort)), imag(ZTerms[nt]->GetEntry(ndPort,nsPort)));
  fprintf(f,"\n");
  fclose(f);
}


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
  char *PortFile=0;
  bool PlotGeometry=false;
//
  char *SubstrateFile=0;
  cdouble Eps  = 0.0;
  double h     = 0.0;
//
  char *TransFile=0;
//
  char *FreqFile=0;
  double Freqs[MAXFREQ];    int nFreqs;
  double MinFreq;           int nMinFreq;
  double MaxFreq;	    int nMaxFreq;
  int NumFreqs;		    int nNumFreqs;
  bool LogFreq=false;
//
  char *PCFile=0;
//
  char *EPFiles[MAXEPF];          int nEPFiles;
  char *FVMeshes[MAXFVM];         int nFVMeshes;
  char *FVMeshTransFiles[MAXFVM]; int nFVMeshTransFiles;
  memset(FVMeshTransFiles, 0, MAXFVM*sizeof(char *));
//
  bool ZParms=false;
  bool SParms=false;
  double ZCharacteristic=50.0;
//
  char *FileBase=0;
  char *ContribOnly=0;
  CLI::App App{"scuff-microstrip"};
  App.option_defaults()->ignore_case();
  App.add_option("--geometry", GeoFile, "geometry file")
     ->required()
     ->take_last();
  App.add_option("--portfile", PortFile, "port file")->take_last();
  App.add_flag("--PlotGeometry", PlotGeometry, "generate geometry/port visualization file");
  App.add_option("--SubstrateFile", SubstrateFile, "substrate definition file")->take_last();
  scuff::cli::AddComplexOption(App, "--Eps", Eps, "substrate permittivity");
  App.add_option("--h", h, "substrate thickness")->take_last();
  App.add_option("--TransFile", TransFile, "list of geometry transforms")->take_last();
  App.add_option("--freqfile", FreqFile, "list of frequencies")->take_last();
  App.add_option("--frequency", Freqs, "frequency (GHz)")->type_name("DOUBLE")->expected(1, MAXFREQ);
  App.add_option("--minfreq", MinFreq, "starting frequency")->take_last();
  App.add_option("--maxfreq", MaxFreq, "ending frequency")->take_last();
  App.add_option("--numfreqs", NumFreqs, "number of frequencies")->take_last();
  App.add_flag("--logfreq", LogFreq, "use logarithmic frequency steps");
  App.add_option("--portcurrentfile", PCFile, "port current file")->take_last();
  App.add_option("--EPFile", EPFiles, "list of evaluation points")->expected(1, MAXEPF);
  App.add_option("--FVMesh", FVMeshes, "field visualization mesh")->expected(1, MAXFVM);
  App.add_option("--FVMeshTransFile", FVMeshTransFiles, "")->expected(1, MAXFVM);
  App.add_option("--FileBase", FileBase, "base name for output files")->take_last();
  App.add_option("--ContribOnly", ContribOnly, "select port voltage contributors")->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;
  if (GeoFile.empty())
   { CLI::ValidationError Error("scuff-microstrip","--geometry option is mandatory");
     App.exit(Error);
     return 1;
   }
  if (PortFile.empty())
   { CLI::ValidationError Error("scuff-microstrip","--portfile option is mandatory");
     App.exit(Error);
     return 1;
   }
  if (GeoFile==0)
   OSUsage(argv[0], VERSION, OSArray,"--geometry option is mandatory");
  if (PortFile==0)
   OSUsage(argv[0], VERSION, OSArray,"--PortFileName option is mandatory");
  if (!FileBase)
   FileBase=strdup(GetFileBase(GeoFile));

  /***************************************************************/
  /* create the scuffSolver                                         */
  /***************************************************************/
  scuffSolver *Solver = new scuffSolver(GeoFile, PortFile);
  if (SubstrateFile)
   Solver->SetSubstrateFile(SubstrateFile);
  else
   { if (Eps!=1.0) Solver->SetSubstratePermittivity(Eps);
     if (h!=0.0)   Solver->SetSubstrateThickness(h);
   }
  Solver->InitGeometry();
  RWGGeometry *G = Solver->G;
  int NumPorts = Solver->NumPorts;

  /***************************************************************/
  /* plot the geometry if requested                ***************/
  /***************************************************************/
  if (PlotGeometry)
   { fprintf(stderr,"--PlotGeometry option was specified; plotting ports ONLY.\n");
     Solver->PlotGeometry();
     fprintf(stderr,"Thank you for your support.\n");
     exit(0);
   }

  /***************************************************************/
  /* parse frequency specifications to create the FreqList vector*/
  /***************************************************************/
  HVector *FreqList=0;
  if (FreqFile) 
   FreqList = new HVector(FreqFile);
  else if (nFreqs)
   FreqList=new HVector(nFreqs, Freqs);
  else if (nMinFreq || nMaxFreq || nNumFreqs)
   { if ( !nMinFreq || !nMaxFreq || !nNumFreqs ) ErrExit("--MinFreq, --MaxFreq, --NumFreqs must be all present or all absent");
     FreqList = LogFreq ? LogSpace(MinFreq, MaxFreq, NumFreqs) : LinSpace(MinFreq, MaxFreq, NumFreqs);
   }

  /***************************************************************/ 
  /* parse the port-current list if that was specified           */ 
  /***************************************************************/
  HMatrix *PCMatrix=0;
  cdouble *PortCurrents=0;
  if (PCFile)
   { if (FreqList)
      ErrExit("--PortCurrentFile may not be specified together with a frequency specification");
     PCMatrix=new HMatrix(PCFile);
     if (PCMatrix->NC != NumPorts+1)
      ErrExit("%s: expected %i columns (1 frequency, %i ports)\n",PCFile,NumPorts+1,NumPorts);
     FreqList=new HVector(PCMatrix->NR, LHM_REAL, PCMatrix->GetColumnPointer(0));
     PortCurrents = new cdouble[NumPorts];
   }

  /***************************************************************/
  /* sanity check input arguments ********************************/
  /***************************************************************/
  if (PCFile==0 && FreqList->N!=0 && (ZParms==0 && SParms==0) )
   OSUsage(argv[0], VERSION, OSArray,"--ZParameters and/or --SParameters must be specified if a frequency specification is present");
  if (PCFile!=0 && (ZParms!=0 || SParms!=0) )
   OSUsage(argv[0], VERSION, OSArray,"--ZParameters and --SParameters may not be used with --PortCurrentFile");
  //if (PCFile!=0 && nEPFiles==0 && nFVMeshes==0)
   //OSUsage(argv[0], VERSION, OSArray,"--EPFile or --FVMesh must be specified if --PortCurrentFile is specified");
  if (PCFile==0 && (nEPFiles!=0 || nFVMeshes!=0) )
   OSUsage(argv[0], VERSION, OSArray,"--EPFile and --FVMesh require --PortCurrentFile");

  /***************************************************************/
  /* process list of geometric transformations, if any           */
  /***************************************************************/
  GTCList GTCs = ReadTransFile(TransFile);
  G->CheckGTCList(GTCs);
  int NT = GTCs.size();
  if (NT>1) Solver->EnableSystemBlockCache();

  char OutFileBaseBuffer[100], *OutFileBase = (NT>1 ? OutFileBaseBuffer : FileBase);

  HMatrix *ZSMatrix = 0;
  if (ZParms) InitZSParmFile(FileBase, NumPorts, 'Z', NT);
  if (SParms) InitZSParmFile(FileBase, NumPorts, 'S', NT);

  HMatrix *ZTermBuffer[3]={0,0,0}, **ZTerms=0;
  if (ZParms)
   { ZTerms = ZTermBuffer;
     ZTerms[0] = new HMatrix(NumPorts, NumPorts, LHM_COMPLEX);
     ZTerms[1] = new HMatrix(NumPorts, NumPorts, LHM_COMPLEX);
     ZTerms[2] = new HMatrix(NumPorts, NumPorts, LHM_COMPLEX);
   }

  /***************************************************************/
  /* loop over frequencies and geometric transforms              */
  /***************************************************************/
  int NF = FreqList->N, NFNT=NF*NT;
  for (int nfnt = 0; nfnt<NFNT; nfnt++)
   { 
     int nf = nfnt/NT;
     int nt = nfnt%NT;
     double Freq = FreqList->GetEntryD(nf);

     G->Transform(GTCs[nt]);
     char *Tag = (NT>1) ? GTCs[nt]->Tag : 0;
     if (Tag) 
      Log("Working at f=%g GHz, transform %s",Freq,Tag);
     else
      Log("Working at f=%g GHz",Freq);

     /*--------------------------------------------------------------*/
     /* (re)assemble and factorize the BEM matrix                    */
     /*--------------------------------------------------------------*/
     Solver->AssembleSystemMatrix(Freq);

     /*--------------------------------------------------------------*/
     /* switch off to output modules to handle various calculations -*/
     /*--------------------------------------------------------------*/
     if (ZParms || SParms)
      { ZSMatrix=Solver->GetZMatrix(ZSMatrix, ZTerms);
        if (ZParms) 
         WriteZSParms(FileBase, Tag, 'Z', Freq, ZSMatrix, ZTerms);
        if (SParms)
         { Solver->Z2S(ZSMatrix, 0, ZCharacteristic);
           WriteZSParms(FileBase, Tag, 'S', Freq, ZSMatrix);
         }
      }
 
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
         /* currents at this frequency, then solve the BEM sytem and     */
         /* handle post-processing field computations                    */
         /*--------------------------------------------------------------*/
         PCMatrix->GetEntries(nf,"1:end",PortCurrents);
         Solver->Solve(PortCurrents);

         if (Tag) snprintf(OutFileBase,100,"%s.%s",OutFileBase,Tag);

         for(int n=0; n<nEPFiles; n++)
          Solver->ProcessEPFile(EPFiles[n], OutFileBase);

         for(int n=0; n<nFVMeshes; n++)
          Solver->ProcessFVMesh(FVMeshes[n], FVMeshTransFiles[n], OutFileBase);

       } // if (nEPFiles!=0 || nFVMeshes!=0)

     G->UnTransform();

    }  // for (nfnt=0..)
 
   printf("Thank you for your support.\n");
}
