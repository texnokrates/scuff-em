/*
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include <libscuff.h>
#include "GetTMatrix.h"

using namespace scuff;

/***************************************************************/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[])
{
  /*--------------------------------------------------------------*/
  /*- process command-line options -------------------------------*/
  /*--------------------------------------------------------------*/
  std::string GeoFileName;  // .scuffgeo file
  int lMax=5;               // maximum L-value of spherical wave computed
  cdouble Omega=0;          // angular frequency at which to run the computation
  std::string Cache;        // scuff cache file
  int l=1, m=0;
  int Type=0;
  CLI::App App{"compute spherical moments"};
  App.option_defaults()->ignore_case();
  App.add_option("--geometry", GeoFileName, ".scuffgeo file")->take_last();
  scuff::cli::AddComplexOption(App, "--Omega", Omega, "angular frequency");
  App.add_option("--l", l, "l value")->take_last();
  App.add_option("--m", m, "m value")->take_last();
  App.add_flag("--electric", Type, "use electric monopole (default is magnetic)");
  App.add_option("--lMax", lMax, "maximum l-value")->take_last();
  App.add_option("--Cache", Cache, "scuff cache file")->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;
  if (GeoFileName.empty())
   { CLI::ValidationError Error("tSphericalMoments","--geometry option is mandatory");
     App.exit(Error);
     return 1;
   }
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  SetLogFileName("%s.log",GetFileBase(GeoFileName.c_str()));
  RWGGeometry *G=new RWGGeometry(GeoFileName.c_str());
  HVector *KN = G->AllocateRHSVector();

  SphericalWave SW(l, m, Type);
  SW.Omega=Omega;
  SW.Eps=SW.Mu=1.0;
  G->ExpandCurrentDistribution(&SW, KN);

  int nm, NumMoments = 2*(lMax+1)*(lMax+1);
  HVector *AVector=GetSphericalMoments(G->Objects[0], Omega, lMax, KN, 0);

  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  SetDefaultCD2SFormat("%+15.8e %+15.8e");
  char FileName[100];
  sprintf(FileName,"%s.%i.%i.SphericalMoments",GetFileBase(GeoFileName.c_str()),l,m);
  FILE *f=vfopen("%s.%i.%i.SphericalMoments","w",GetFileBase(GeoFileName.c_str()),l,m);
  const char *TypeChar="ME";
  for(nm=l=0; l<=lMax; l++)
   for(m=-l; m<=l; m++)
    for(Type=0; Type<=1; Type++, nm++)
     fprintf(f,"%i %3i  %3i  %s\n",Type,l,m,CD2S(AVector->GetEntry(nm)));
  fclose(f);

  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  char buffer[200];
  sprintf(buffer,"sort -g -k1 -k3 -k2 %s > doom",FileName);
  system(buffer);
  sprintf(buffer,"/bin/mv doom %s",FileName);
  system(buffer);

}
