#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "libhrutil.h"
#include "libhmat.h"
#include "ScuffCLI.h"
#include "lapack.h"

/***************************************************************/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[])
{ 
  /*--------------------------------------------------------------*/
  /*- process command-line arguments -----------------------------*/
  /*--------------------------------------------------------------*/
  int N1=10;
  int N2=20;
  int N3=30;
  CLI::App App{"test zgemm"};
  App.option_defaults()->ignore_case();
  App.add_option("--N1", N1, "dimension 1")->take_last();
  App.add_option("--N2", N2, "dimension 2")->take_last();
  App.add_option("--N3", N3, "dimension 3")->take_last();

  if (auto Status=scuff::cli::Parse(App,argc,argv))
   return *Status;

  HMatrix *A=new HMatrix(N2,N1,LHM_COMPLEX);
  HMatrix *B=new HMatrix(N2,N3,LHM_COMPLEX);

  srand48(time(0));
  for(int m=0; m<A->NR; m++)
   for(int n=0; n<A->NC; n++)
     A->SetEntry(m,n, cdouble(5.0*(drand48()-2.5), 5.0*(drand48()-2.5) ));
  for(int m=0; m<B->NR; m++)
   for(int n=0; n<B->NC; n++)
     B->SetEntry(m,n, cdouble(5.0*(drand48()-2.5), 5.0*(drand48()-2.5) ));

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  HMatrix *C1=new HMatrix(N1,N3,LHM_COMPLEX);
  C1->Zero();
  for(int i=0; i<N1; i++)
   for(int j=0; j<N3; j++)
    for(int k=0; k<N2; k++)
     C1->AddEntry(i,j, conj(A->GetEntry(k,i))*B->GetEntry(k,j));

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  HMatrix *C2=new HMatrix(N1,N3,LHM_COMPLEX);
  int LDA = A->NR;
  int LDB = B->NR;
  int LDC = C2->NR;
  cdouble zOne=1.0;
  cdouble zZero=0.0;
  zgemm_("C","N",&N1,&N3,&N2,&zOne,A->ZM,&LDA,B->ZM,&LDB,&zZero,C2->ZM,&LDC);

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  double NormC=0.0, NormError=0.0;
  for(int nr=0; nr<C1->NR; nr++)
   for(int nc=0; nc<C1->NC; nc++)
    { NormC     += norm(C1->GetEntry(nr,nc));
      NormError += norm(C1->GetEntry(nr,nc) - C2->GetEntry(nr,nc));
    };
  printf("rel norm error=%e\n",sqrt(NormError/NormC));


}
