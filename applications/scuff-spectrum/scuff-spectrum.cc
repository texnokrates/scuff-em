#include <stdio.h>
#include <stdlib.h>

#include <string.h> 
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libhrutil.h>
#include <libscuff.h>
#include <libSGJC.h>
#include "libBeyn.h"

#include <memory>
#include <string>
#include <vector>

#include <ScuffCLI.h>

#define II cdouble(0.0,1.0)

using namespace scuff;

#define MAXEPF   10    // max number of evaluation-point files
#define MAXFVM   10    // max number of field visualization meshes

/***************************************************************/
/* prototypes for postprocessing routines in OutputModules.cc***/
/***************************************************************/
void ProcessEPFile(RWGGeometry *G, HVector *KN, cdouble Omega, double *kBloch,
                   const char *EPFileName, const char *OutFileBase);

void VisualizeFields(RWGGeometry *G, HVector *KN, cdouble Omega, double *kBloch,
                     const char *OutFileBase, const char *FVMesh, const char *TransFile);

void VisualizeFields(RWGGeometry *G, HVector *KN, cdouble Omega, double *kBloch,
                     const char *OutFileBase, double *Screen);

void VisualizeSurfaceFields(RWGGeometry *G, HVector *KN, cdouble Omega, double *kBloch,
                            const char *OutFileBase);

void WriteCartesianMoments(RWGGeometry *G, HVector *KN, cdouble Omega, double *kBloch, const char *CartesianMomentFile);

void WriteSphericalMoments(RWGGeometry *G, HVector *KN, cdouble Omega, int LMax, const char *SphericalMomentFile);

/***************************************************************/
/***************************************************************/
/***************************************************************/
using BFData = struct BFData
 { 
   RWGGeometry *G;
   HMatrix *M;
   double kBloch[2];
   FILE *LogFile;
 };

void BeynFunc(cdouble Omega, void *UserData, HMatrix *VHat, HMatrix *MVHat)
{
  BFData *Data   = (BFData *)UserData;

  RWGGeometry *G = Data->G;
  HMatrix *M     = Data->M;
  double *kBloch = Data->kBloch;
  FILE *LogFile  = Data->LogFile;

  if (G->LDim==0)
   Log(" assembling BEM matrix at Omega=%s",CD2S(Omega));
  if (G->LDim==1)
   Log(" assembling BEM matrix at k={%e},Omega=%s", kBloch[0],CD2S(Omega));
  if (G->LDim==2)
   Log(" assembling BEM matrix at k={%e,%e},Omega=%s", kBloch[0],kBloch[1],CD2S(Omega));

  if (G->LDim==0)
   G->AssembleBEMMatrix(Omega, M);
  else
   G->AssembleBEMMatrix(Omega, kBloch, M);

  if (LogFile)
   fprintf(LogFile,"%e %e\n",real(Omega),imag(Omega));

  if (MVHat)
   M->Multiply(VHat, MVHat);
  else
   { Log(" LUFactorizing...");
     M->LUFactorize(); 
     Log(" LUSolving...");
     M->LUSolve(VHat);
   }
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[])
{
  InstallHRSignalHandler();
  InitializeLog(argv[0]);

  /***************************************************************/
  /* process command-line arguments ******************************/
  /***************************************************************/
  std::string GeoFile;
  cdouble Omega0   = cdouble(1.0,-1.0);
  double Rx        = 0.1;
  double Ry        = 0.1;
  int    N         = 20;
  int    L         = 5;
  double kx        = 0.0;
  double ky        = 0.0;
  std::string ContourFile;
  std::string FileBase;
  bool PlotContours=false;
  bool PlotSurfaceCurrents=false;
  bool PlotSurfaceFields=false;
  std::string SphericalMomentFile;
  int LMax=3;
  std::string CartesianMomentFile;
  std::vector<std::string> EPFiles;
  std::vector<double> FVScreens;      // 11 values per occurrence
  std::vector<std::string> FVMeshes, FVMeshTransFiles;

  CLI::App App{"compute eigenmodes (resonances) of a scatterer"};
  App.option_defaults()->ignore_case();

  App.add_option("--geometry", GeoFile, ".scuffgeo file")
     ->required()
     ->take_last();
  scuff::cli::AddComplexOption(App, "--Omega0", Omega0, "center of contour");
  App.add_option("--Rx", Rx, "horizonal radius")->take_last();
  App.add_option("--Ry", Ry, "vertical radius")->take_last();
  App.add_option("--N", N, "number of quadrature points")->take_last();
  App.add_option("--L", L,
                 "upper bound on expected number of eigenvalues in contour ")->take_last();
  /* the legacy parser declared these PA_INT but stored them into
     doubles through a mismatched pointer cast; they are now plain
     double options */
  App.add_option("--kx", kx, "x component of bloch vector")->take_last();
  App.add_option("--ky", ky, "y component of bloch vector")->take_last();
  App.add_option("--ContourFile", ContourFile, "list of contours")->take_last();
  App.add_flag("--PlotContours", PlotContours, "plot contours for visualization");
  App.add_flag("--PlotSurfaceCurrents", PlotSurfaceCurrents,
               "generate visualization files for eigenmode currents");
  App.add_flag("--PlotSurfaceFields", PlotSurfaceFields,
               "generate visualization files for eigenmode surface fields");
  App.add_option("--SphericalMomentFile", SphericalMomentFile,
                 "name of output file for spherical multipole moments")->take_last();
  App.add_option("--LMax", LMax,
                 "index of highest spherical multipole moment to retain")->take_last();
  App.add_option("--CartesianMomentFile", CartesianMomentFile,
                 "name of output file for cartesian multipole moments")->take_last();
  App.add_option("--EPFile", EPFiles, "list of evaluation points")
     ->expected(1, MAXEPF);
  App.add_option("--FVScreen", FVScreens, "field visualization screen")
     ->type_size(11)
     ->expected(1, MAXFVM);
  App.add_option("--FVMesh", FVMeshes, "field visualization mesh")
     ->expected(1, MAXFVM);
  App.add_option("--FVMeshTransFile", FVMeshTransFiles,
                 "list of geometrical transformations applied to FVMesh")
     ->expected(1, MAXFVM);
  App.add_option("--FileBase", FileBase, "base name for output files")->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;
  int nEPFiles          = (int)EPFiles.size();
  int nFVScreens        = (int)FVScreens.size()/11;
  int nFVMeshes         = (int)FVMeshes.size();
  int nFVMeshTransFiles = (int)FVMeshTransFiles.size();

  if (FileBase.empty())
   FileBase=GetFileBase(GeoFile.c_str());

  /***************************************************************/
  /* initialize RWGGeometry, read list of contours               */
  /***************************************************************/
  std::unique_ptr<RWGGeometry> G(new RWGGeometry(GeoFile.c_str()));
  std::unique_ptr<HMatrix> M(G->AllocateBEMMatrix());
  int D = G->TotalBFs;

  /***************************************************************/
  /* process contour specifications ******************************/
  /***************************************************************/
  std::unique_ptr<HMatrix> ContourMatrix;
  if (!ContourFile.empty())
   ContourMatrix.reset(new HMatrix(ContourFile.c_str()));
  else
   { ContourMatrix.reset(new HMatrix(1, 5 + G->LDim, LHM_COMPLEX));
     int nc=0;
     if (G->LDim>=1) ContourMatrix->SetEntry(0,nc++,kx);
     if (G->LDim==2) ContourMatrix->SetEntry(0,nc++,ky);
     ContourMatrix->SetEntry(0,nc++,Omega0);
     ContourMatrix->SetEntry(0,nc++,Rx);
     ContourMatrix->SetEntry(0,nc++,Ry);
     ContourMatrix->SetEntry(0,nc++,N);
     ContourMatrix->SetEntry(0,nc++,L);
   };
    
  /***************************************************************/
  /* open eigenfrequency output file and write file header       */
  /***************************************************************/
  FILE *f=vfopen("%s.ModeFrequencies","a",FileBase.c_str());
  fprintf(f,"#%s " VERSION " run on %s (%s)\n",argv[0], GetHostName(), GetTimeString());
  fclose(f);

  /***************************************************************/
  /* apply Beyn's method to each user-specified contour          */
  /***************************************************************/
  SetDefaultCD2SFormat("%.8e %.8e");
  for(int nr=0; nr<ContourMatrix->NR; nr++)
   {
     /***************************************************************/
     /* extract information on the contour **************************/
     /***************************************************************/
     struct BFData MyBFData = {G.get(), M.get(), {0,0}, nullptr};
     double *kBloch = (G->LDim > 0) ? MyBFData.kBloch : nullptr;
     for(int d=0; d<G->LDim; d++) 
      MyBFData.kBloch[d] = ContourMatrix->GetEntryD(nr, d);
     Omega0              = ContourMatrix->GetEntry (nr, G->LDim+0);
     Rx                  = ContourMatrix->GetEntryD(nr, G->LDim+1);
     Ry                  = ContourMatrix->GetEntryD(nr, G->LDim+2);
     N                   = (int)(ContourMatrix->GetEntryD(nr, G->LDim+3));
     L                   = (int)(ContourMatrix->GetEntryD(nr, G->LDim+4));

     // create a string identifier for this contour
     char kbStr[64]="";
     if (G->LDim==1)
      snprintf(kbStr,sizeof(kbStr),"kx%g_",kBloch[0]);
     if (G->LDim==2)
      snprintf(kbStr,sizeof(kbStr),"kx%g_ky%g_",kBloch[0],kBloch[1]);
     char ContourLabel[256];
     snprintf(ContourLabel, sizeof(ContourLabel),
              "C%i_w%s_%sRX%g_RY%g_N%i",nr,z2s(Omega0),kbStr,Rx,Ry,N);
     Log("Looking for eigenvalues in contour %s",ContourLabel);

     // initialize visualization files showing the contour if that was requested
     if (PlotContours)
      { MyBFData.LogFile = vfopen("%s.contours","a",FileBase.c_str());
        fprintf(MyBFData.LogFile,"\n\n# (Omega0,Rx,Ry,N)=%s,%e,%e,%i\n",CD2S(Omega0),Rx,Ry,N);
      }

     /***************************************************************/
     /* run Beyn's algorithm for this contour                       */
     /***************************************************************/
     std::unique_ptr<BeynSolver, void (*)(BeynSolver *)> Solver(CreateBeynSolver(D, L),
                                                                DestroyBeynSolver);
     HVector *Eigenvalues  = Solver->Eigenvalues;
     HVector *EVErrors     = Solver->EVErrors;
     HMatrix *Eigenvectors = Solver->Eigenvectors;
     HVector *Residuals    = Solver->Residuals;
     int NumModes=BeynSolve(Solver.get(), BeynFunc, (void *)&MyBFData, Omega0, Rx, Ry, N);

     if (PlotContours)
      fclose(MyBFData.LogFile);

     /***************************************************************/
     /* write eigenfrequency results to .ModeFrequencies file       */
     /***************************************************************/
     f=vfopen("%s.ModeFrequencies","a",FileBase.c_str());
     fprintf(f,"# For contour w0=%s, Rx=%e, Ry=%e, N=%i, L=%i",z2s(Omega0),Rx,Ry,N,L);
     if (kBloch)
      { fprintf(f,", kBloch=");
        fprintVec(f,kBloch,G->LDim);
      }
     fprintf(f,":\n");
     fprintf(f,"# re(w) im(w)   estimated error in re(w), im(w)    residual\n");
     for(int n=0; n<NumModes; n++)
      { fprintf(f,"%+12e %+12e  ",real(Eigenvalues->GetEntry(n)), imag(Eigenvalues->GetEntry(n)));
        if (n<EVErrors->N)
         fprintf(f,"%+12e %+12e  ",real(EVErrors->GetEntry(n)), imag(EVErrors->GetEntry(n)));
        fprintf(f,"%+e ",Residuals->GetEntryD(n));
        fprintf(f,"\n");
      }
     fclose(f);

     /***************************************************************/
     /* post-processing of eigenvector data                         */
     /***************************************************************/
     for(int nm=0; nm<NumModes; nm++)
      { 
        std::string OutFileBase=std::string(FileBase)+"_"+ContourLabel+"_Mode"
                                + std::to_string(nm);

        cdouble Omega = Eigenvalues->GetEntry(nm);
        HVector KN(D, LHM_COMPLEX, (cdouble *)Eigenvectors->GetColumnPointer(nm));

        // write cartesian multiple moments
        if (!CartesianMomentFile.empty())
         WriteCartesianMoments(G.get(), &KN, Omega, kBloch, CartesianMomentFile.c_str());

        // write cartesian multiple moments
        if (!SphericalMomentFile.empty())
         WriteSphericalMoments(G.get(), &KN, Omega, LMax, SphericalMomentFile.c_str());

        // write surface-current visualization files
        if (PlotSurfaceCurrents)
         G->PlotSurfaceCurrents(&KN, Omega, kBloch, "%s.pp", OutFileBase.c_str());

        if (PlotSurfaceFields)
         VisualizeSurfaceFields(G.get(), &KN, Omega, kBloch, OutFileBase.c_str());

        // process user-specified lists of field-evaluation points
        for(int nepf=0; nepf<nEPFiles; nepf++)
         ProcessEPFile(G.get(), &KN, Omega, kBloch, EPFiles[nepf].c_str(),
                       OutFileBase.c_str());

        // process user-specified field-visualization meshes
        for(int nfm=0; nfm<nFVMeshes; nfm++)
         VisualizeFields(G.get(), &KN, Omega, kBloch, OutFileBase.c_str(),
                         FVMeshes[nfm].c_str(),
                         FVMeshTransFiles.size() ? FVMeshTransFiles[nfm].c_str() : nullptr);
        for(int ns=0; ns<nFVScreens; ns++)
         { std::string VFFileBase=OutFileBase+".Screen"+std::to_string(ns);
           VisualizeFields(G.get(), &KN, Omega, kBloch, VFFileBase.c_str(),
                           FVScreens.data() + (size_t)11*ns);
         }
      }


   }

}
