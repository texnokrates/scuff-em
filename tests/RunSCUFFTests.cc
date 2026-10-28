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
 * RunSCUFFTests.cc --
 *
 * homer reid       -- 2/2016 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libhrutil.h>

#include "libscuff.h"

using namespace scuff;

#define MAXSTR 1000
#define MAXTOK 50

/***************************************************************/
/***************************************************************/
/***************************************************************/
struct SCUFFTest
 { 
   char *CodeName;
   char **Args;      int NumArgs;
   char **OutFiles;  int NumOutFiles;
   char **RefFiles;  int NumRefFiles;
   int  *
   char *ErrMsg;
 } SCUFFTest;

/***************************************************************/
/***************************************************************/
/***************************************************************/
SCUFFTest *ParseTestFile(char *FileName)
{
  // all fields initialized to 0 or NULL by mallocEC
  SCUFFTest *Test=(SCUFFTest *)mallocEC(sizeof(*Test));

  FILE *f=fopen(FileName);
  if (!f)
   { Test->ErrMsg=vstrdup("could not open file %s",FileName);
     return Test;
   };

  int LineNum=0;
  while( fgets(Line,MAXSTR,f) )
   { 
     char *Tokens[MAXTOK];
     char Line[MAXSTR];
     int NumTokens=Tokenize(Line, Tokens, MAXTOK);
     if ( NumTokens==0 || Tokens[0][0]=='#' )
      continue; 
    
     // boolean options go here

     // all other options require at least one argument
     if (NumTokens==1)
      { Test->ErrMsg=vstrdup("%s:%i: %s keyword requires arguments",FileName,LineNum,Tokens[0]);
        return Test;
      };

     if ( !StrCaseCmp(Tokens[0],"CODE") )
      { 
        Test->CodeName = strdup(Tokens[1]);
      }
     else if ( !StrCaseCmp(Tokens[0],"ARG") )
      { Test->Args = realloc(Test->Args, NumArgs+NumTokens-1);
        for(int nt=1; nt<NumTokens; nt++)
         Test->Args[NumArgs++] = strdup(Tokens[nt]);
      }
     else if ( !StrCaseCmp(Tokens[0],"OUTFILE") )
      { Test->OutFiles = realloc(Test->OutFiles, NumOutFiles+1);
        Test->OutFiles[NumOutFiles]=strdup(Tokens[1]);
        NumOutFiles++;
      }
     else if ( !StrCaseCmp(Tokens[0],"REFFILE") )
      { Test->RefFiles = realloc(Test->RefFiles, NumRefFiles+1);
        Test->RefFiles[NumRefFiles]=strdup(Tokens[1]);
        NumRefFiles++;
      }
     else if ( !StrCaseCmp(Tokens[0],"KEYCOLUMN") )
      { if (NumTokens<3) 
         { Test->ErrMsg=vstrdup("%s:%i: not enough arguments to KEYCOLUMN",FileName,LineNum);
           return Test;
         };
      };

   };

}

/***************************************************************/
/* main function   *********************************************/
/***************************************************************/  
int main(int argc, char *argv[])
{
  /***************************************************************/
  /* convenience shortcuts that allow the code to be invoked as  */
  /*  % scuff-analyze File.msh                                   */
  /* or                                                          */
  /*  % scuff-analyze File.scuffgeo                              */
  /***************************************************************/
  char *GeoFile=0;
  char *MeshFile=0;
  if (argc>=2)
   { char *Ext = GetFileExtension(argv[1]);
     if (Ext && !strcasecmp(Ext,"scuffgeo"))
      { GeoFile=strdup(argv[1]); 
        argv[1]=0; 
      }
     else if (Ext && !strcasecmp(Ext,"msh"))
      { MeshFile=strdup(argv[1]); 
        argv[1]=0; 
      };
   };

  /***************************************************************/
  /* process options *********************************************/
  /***************************************************************/
  int PhysicalRegion=-1;
  char *TransFile=0;
  int WriteGPFiles=0;
  int WritePPFiles=0;
  int WriteLabels=0;
  int Neighbors=0;
  bool RegionVolumes=false;
  double WhichRegion[3]={HUGE_VAL, HUGE_VAL, HUGE_VAL};
  char *EPFile=0;
  CLI::App App{"run SCUFF regression tests"};
  App.option_defaults()->ignore_case();
  App.add_option("--geometry", GeoFile, "geometry file")->take_last();
  App.add_option("--mesh", MeshFile, "mesh file")->take_last();
  App.add_option("--meshfile", MeshFile, "mesh file")->take_last();
  App.add_option("--PhysicalRegion", PhysicalRegion, "index of surface within mesh file")->take_last();
  App.add_option("--TransFile", TransFile, "list of transformations")->take_last();
  App.add_option("--EPFile", EPFile, "list of points")->take_last();
  App.add_flag("--WriteGnuplotFiles", WriteGPFiles, "write gnuplot visualization files");
  App.add_flag("--WriteGMSHFiles", WritePPFiles, "write GNUPLOT visualization files ");
  App.add_flag("--WriteGMSHLabels", WriteLabels, "write GNUPLOT labels");
  App.add_option("--Neighbors", Neighbors, "number of neighboring cells to plot")->take_last();
  App.add_flag("--RegionVolumes", RegionVolumes, "compute volumes of closed regions");
  App.add_option("--WhichRegion", WhichRegion, "identify region in which the given point lives")->type_name("COMPLEX")->expected(3)->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;
  if (GeoFile.empty() && MeshFile.empty())
   { CLI::ValidationError Error("RunSCUFFTests","either --geometry or --meshfile option must be specified");
     App.exit(Error);
     return 1;
   }

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (GeoFile.empty() && MeshFile.empty())
   OSUsage(argv[0],OSArray,"either --geometry or --meshfile option must be specified");
  if (GeoFile!=0 && MeshFile!=0)
   ErrExit("--geometry and --meshfile options are mutually exclusive");
  if (PhysicalRegion!=-1 && MeshFile==0)
   ErrExit("--PhysicalRegion option may only be used with --meshfile");
  if (TransFile && GeoFile==0)
   ErrExit("--transfile option may only be used with --geometry");
  if (EPFile)
   WritePPFiles=1;  if (GeoFile==0 && MeshFile==0)
   OSUsage(argv[0],OSArray,"either --geometry or --meshfile option must be specified");
  if (GeoFile!=0 && MeshFile!=0)
   ErrExit("--geometry and --meshfile options are mutually exclusive");
  if (PhysicalRegion!=-1 && MeshFile==0)
   ErrExit("--PhysicalRegion option may only be used with --meshfile");
  if (TransFile && GeoFile==0)
   ErrExit("--transfile option may only be used with --geometry");
  if (EPFile)
   WritePPFiles=1;
   
  /***************************************************************/
  /**************************************************************/
  /***************************************************************/
  RWGSurface *S=0;
  RWGGeometry *G=0;
  SetLogFileName("scuff-analyze.log");
  if (MeshFile)
   {
     RWGGeometry::AssignBasisFunctionsToExteriorEdges=false;
     S=new RWGSurface(MeshFile, PhysicalRegion);
     AnalyzeSurface(S);

     if (WriteGPFiles)
      WriteMeshGPFiles(S);

     if (WritePPFiles)
      WriteMeshPPFiles(S);

   }
  else
   { G=new RWGGeometry(GeoFile);

     AnalyzeGeometry(G);

     if (Neighbors!=0 && G->LBasis==0)
      { Warn("--Neighbors option only makes sense for periodic geometries");
        Neighbors=0;
      }

     if (WriteGPFiles)
      WriteGeometryGPFiles(G);

     if (WritePPFiles)
      WriteGeometryPPFiles(G, Neighbors);

     if (WriteLabels)
      WriteGeometryLabels(G);
   };

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (TransFile)
   { 
     if (!G) 
      ErrExit("--transfile option may only be used with --geometry");

     int ngtc, NGTC;
     GTComplex **GTCList=ReadTransFile(TransFile, &NGTC);
     char *ErrMsg=G->CheckGTCList(GTCList, NGTC);
     if (ErrMsg)
      ErrExit("file %s: %s",TransFile,ErrMsg);

     char PPFileName[MAXSTR];
     snprintf(PPFileName,MAXSTR,"%s.transformed.pp",GetFileBase(G->GeoFileName));
     unlink(PPFileName);

     for(ngtc=0; ngtc<NGTC; ngtc++) 
      {
        G->Transform(GTCList[ngtc]);
        G->WritePPMesh(PPFileName, GTCList[ngtc]->Tag);
        G->UnTransform();
      };

     printf("Visualizations for %i transforms written to %s.\n",NGTC,PPFileName);
 
   };

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (EPFile)
   { HMatrix *XMatrix=new HMatrix(EPFile);
     char PPFileName[MAXSTR];
     if (MeshFile)
      snprintf(PPFileName,MAXSTR,"%s.pp",GetFileBase(S->MeshFileName));
     else
      snprintf(PPFileName,MAXSTR,"%s.pp",GetFileBase(G->GeoFileName));

     FILE *f=fopen(PPFileName,"a");
     fprintf(f,"View \"%s\" {\n",EPFile);
     for(int n=0; n<XMatrix->NR; n++)
      fprintf(f,"SP(%e,%e,%e) {0.0};\n",
                 XMatrix->GetEntryD(n,0),
                 XMatrix->GetEntryD(n,1),
                 XMatrix->GetEntryD(n,2));
     fprintf(f,"};\n\n");
     fclose(f);
  };

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (RegionVolumes)
   { printf("Region volumes: \n");
     for(int nr=1; nr<G->NumRegions; nr++)
      { 
        printf("%i (%-20s) ",nr,G->RegionLabels[nr]);
        double RV=GetRegionVolume(G,nr);
        if (RV==0.0)
         printf("unbounded\n");
        else
         printf("%e \n",RV);
      };
   }

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (WhichRegion[0]!=HUGE_VAL)
   { int nr=G->GetRegionIndex(WhichRegion);
     printf("Point {%g,%g,%g} lives in region %s.\n",
             WhichRegion[0],WhichRegion[1],WhichRegion[2],G->RegionLabels[nr]);
   };

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  printf("Thank you for your support.\n");

}
