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
 * LBWrappers.cc  -- HMatrix class methods that amount to calls to 
 *                -- LAPACK / BLAS routines, possibly with some pre- or 
 *                -- post-processing 
 *
 * homer reid     -- 12/2009 -- 9/2012
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#include <libhrutil.h>

extern "C" {
 #include "lapack.h" 
}

#include "libhmat.h"

/***************************************************************/
/* multiply matrix by B on the right to yield C                */
/* in other words, if this matrix is A, then this operation    */
/* sets C=A*B.                                                 */
/*                                                             */
/* Options is an optional string argument that allows you to   */
/* specify values for the options "--transA" and '--transB" to */
/* request that the matrix operands be transposed / conjugated,*/
/* like this:                                                  */
/*                                                             */
/*  Options="--transA T"    // use the transpose of A          */
/*  Options="--transB C"    // use the adjoint of B            */
/*  Options="--transA T --transB C"                            */
/***************************************************************/
void HMatrix::Multiply(HMatrix *B, HMatrix *C, const char *Options)
{
  double dOne=1.0, dZero=0.0;
  cdouble zOne=1.0, zZero=0.0;

  if ( StorageType!=B->StorageType || StorageType!=C->StorageType )
   ErrExit("%s:%i: storage type mismatch",__FILE__,__LINE__);
  if ( RealComplex!=B->RealComplex || RealComplex!=C->RealComplex)
   ErrExit("%s:%i: data type mismatch",__FILE__,__LINE__);

  /***************************************************************/ 
  /* parse Options string ****************************************/ 
  /***************************************************************/ 
  char TransA[2]="N", TransB[2]="N";
  if (Options)
   { int MaxTokens=4, NumTokens;
     char *Tokens[4];
     char Line[101];
     strncpy(Line, Options, 100);
     Line[100] = '\0';
     NumTokens=Tokenize(Line, Tokens, MaxTokens);
     for (int nt=0; nt<NumTokens; nt++)
      { if (!strcasecmp(Tokens[nt],"--transa"))    
         { if ( (nt+1) >= NumTokens ) 
            ErrExit("invalid options string in HMatrix::Multiply");
           strncpy(TransA,Tokens[nt+1],1);
           nt++;
         }
        else if (!strcasecmp(Tokens[nt],"--transb"))    
         { if ( (nt+1) >= NumTokens ) 
            ErrExit("invalid options string in HMatrix::Multiply");
           strncpy(TransB,Tokens[nt+1],1);
           nt++;
         }
        else 
         ErrExit("invalid options string in HMatrix::Multiply");
      };
   };

  /***************************************************************/ 
  /* sanity check on matrix sizes: if operand dimensions are     */ 
  /* (NRAxNCA), (NRBxNCB), (NRCxNCC), then we must have          */
  /* NCA=NRB, NRA=NRC, NCB=NCC.                                  */
  /***************************************************************/ 
  int NRA = (toupper(TransA[0])=='N') ? NR : NC;
  int NCA = (toupper(TransA[0])=='N') ? NC : NR;
  int NRB = (toupper(TransB[0])=='N') ? B->NR : B->NC;
  int NCB = (toupper(TransB[0])=='N') ? B->NC : B->NR;
  int NRC = C->NR;
  int NCC = C->NC;
  if ( (NCA!=NRB) || (NRA!=NRC) || (NCB!=NCC) )
   ErrExit("%s:%i: dimension mismatch",__FILE__,__LINE__);

  /***************************************************************/ 
  /***************************************************************/ 
  /***************************************************************/ 
  if ( RealComplex==LHM_REAL && StorageType==LHM_NORMAL )
   dgemm_(TransA,TransB,&NRC,&NCC,&NCA,&dOne,DM,&NR,B->DM,&(B->NR),&dZero,C->DM,&(C->NR));
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_NORMAL )
   zgemm_(TransA,TransB,&NRC,&NCC,&NCA,&zOne,ZM,&NR,B->ZM,&(B->NR),&zZero,C->ZM,&(C->NR));
  else
   ErrExit("%s:%i: multiplication not implemented for storage type",__FILE__,__LINE__);

}

/***************************************************************/
/* this routine is somewhat similar to Multiply(), but it only */
/* computes the diagonal of the product matrix, and it does    */
/* this in N^2 time (instead of N^3 for a usual matrix-matrix  */
/* multiplication) by dotting the rows of A into the columns   */
/* of B.                                                       */
/***************************************************************/
void HMatrix::GetMatrixProductDiagonal(HMatrix *B, HVector *DAB)
{
  if ( NC != B->NR )
   ErrExit("%s:%i: dimension mismatch in GetMatrixProductDiagonal",__FILE__,__LINE__);

  if ( RealComplex != B->RealComplex )
   ErrExit("%s:%i: real/complex mismatch in GetMatrixProductDiagonal",__FILE__,__LINE__);

  int n;
  int Length=NC;
  int AStride=NR;
  int BStride=1;

  if ( RealComplex==LHM_REAL )
   for(n=0; n<DAB->N; n++) 
    DAB->SetEntry( n, ddot_( &Length, DM + n, &AStride, B->DM + n*Length, &BStride) );
  else
   for(n=0; n<DAB->N; n++)
    DAB->SetEntry( n, zdotu_( &Length, ZM + n, &AStride, B->ZM + n*Length, &BStride) );

}

/***************************************************************/
/* replace the matrix with its LU factorization ****************/
/***************************************************************/
int HMatrix::LUFactorize()
{ 
  int info;

  if (ipiv==nullptr)
   ipiv=(int *)mallocEC(NR*sizeof(int));

  if ( RealComplex==LHM_REAL && StorageType==LHM_NORMAL )
   dgetrf_(&NR, &NC, DM, &NR, ipiv, &info); 
  else if ( RealComplex==LHM_REAL && StorageType==LHM_SYMMETRIC )
   dsptrf_("U", &NR, DM, ipiv, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_NORMAL )
   zgetrf_(&NR, &NC, ZM, &NR, ipiv, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_HERMITIAN )
   zhptrf_("U", &NR, ZM, ipiv, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_SYMMETRIC ) 
   zsptrf_("U", &NR, ZM, ipiv, &info);
  else ErrExit("Invalid matrix properties");

  return info;
}

/***************************************************************/
/* solve linear system using LU factorization ******************/
/***************************************************************/
int HMatrix::LUSolve(HVector *X)
{ 
  int info;
  int iOne=1;

  if ( RealComplex != X->RealComplex )
   ErrExit("type mismatch in LUSolve");
  if ( NR!=NC || NR!=X->N )
   ErrExit("dimension mismatch in LUSolve");

  if (ipiv==nullptr)  
   ErrExit("LUFactorize() must be called before LUSolve()");

  if ( RealComplex==LHM_REAL && StorageType==LHM_NORMAL )
   dgetrs_("N", &NR, &iOne, DM, &NR, ipiv, X->DV, &NR, &info);
  else if ( RealComplex==LHM_REAL && StorageType==LHM_SYMMETRIC )
   dsptrs_("U", &NR, &iOne, DM, ipiv, X->DV, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_NORMAL )
   zgetrs_("N", &NR, &iOne, ZM, &NR, ipiv, X->ZV, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_HERMITIAN )
   zhptrs_("U", &NR, &iOne, ZM, ipiv, X->ZV, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_SYMMETRIC )
   zsptrs_("U", &NR, &iOne, ZM, ipiv, X->ZV, &NR, &info);
  else ErrExit("Invalid matrix properties");

  return info;
}

/***************************************************************/
/* solve linear systems using LU factorization                 */
/*                                                             */
/* Trans should be 'N', 'T', or 'C' for no transpose,          */
/* transpose, or conjugate transpose.                         */
/***************************************************************/
int HMatrix::LUSolve(HMatrix *X, char Trans, int nrhs)
{ 
  int info;

  if ( RealComplex != X->RealComplex )
   ErrExit("type mismatch in LUSolve");
  if ( NR!=NC || NR!=X->NR )
   ErrExit("dimension mismatch in LUSolve");
  if ( nrhs > X->NC )
   ErrExit("too many RHSs requested in LUSolve");
  if (ipiv==nullptr)  
   ErrExit("LUFactorize() must be called before LUSolve()");
  if ( Trans!='N' && StorageType!=LHM_NORMAL )
   ErrExit("transposed LU-solves not available for packed matrices");
if ( RealComplex==LHM_REAL && StorageType==LHM_NORMAL )
   dgetrs_(&Trans, &NR, &nrhs, DM, &NR, ipiv, X->DM, &NR, &info);
  else if ( RealComplex==LHM_REAL && StorageType==LHM_SYMMETRIC )
   dsptrs_("U", &NR, &nrhs, DM, ipiv, X->DM, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_NORMAL )
   zgetrs_(&Trans, &NR, &nrhs, ZM, &NR, ipiv, X->ZM, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_HERMITIAN )
   zhptrs_("U", &NR, &nrhs, ZM, ipiv, X->ZM, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_SYMMETRIC )
   zsptrs_("U", &NR, &nrhs, ZM, ipiv, X->ZM, &NR, &info);
  else ErrExit("Invalid matrix properties");

  return info;
}

/* entry points with one or more parameters set to default */

int HMatrix::LUSolve(HMatrix *X, char Trans) 
 { return LUSolve(X,Trans,X->NC); }

int HMatrix::LUSolve(HMatrix *X, int nrhs) 
 { return LUSolve(X,'N',nrhs); }

int HMatrix::LUSolve(HMatrix *X) 
 { return LUSolve(X,'N',X->NC); }

/***************************************************************/
/* replace the matrix with its inverse, assuming LUFactorize() */
/* has already been called                                     */
/***************************************************************/
int HMatrix::LUInvert()
{ 
  int info;
  double *dwork;
  cdouble *zwork;
  int lworkOptimal;
  double dlworkOptimal;
  cdouble zlworkOptimal;

  if ( NR!=NC )
   ErrExit("dimension mismatch in LUSolve");

  if (ipiv==nullptr)  
   ErrExit("LUFactorize() must be called before LUInvert()");

  int MinusOne=-1;
  if ( RealComplex==LHM_REAL && StorageType==LHM_NORMAL )
   {
     // workspace size query
     dgetri_(&NR, DM, &NR, ipiv, &dlworkOptimal, &MinusOne, &info);
     lworkOptimal=(int)dlworkOptimal;

     if (lworkOptimal > lwork)
 {
        work.resize(lworkOptimal*sizeof(double));
        lwork=lworkOptimal;
      };
     dwork=reinterpret_cast<double *>(work.data());

     dgetri_(&NR, DM, &NR, ipiv, dwork, &lwork, &info);
   }
  else if ( RealComplex==LHM_REAL && StorageType==LHM_SYMMETRIC )
   { 
     lworkOptimal = NR;
     if (lworkOptimal > lwork)
 {
        work.resize(lworkOptimal*sizeof(double));
        lwork=lworkOptimal;
      };
     dwork=reinterpret_cast<double *>(work.data());

     dsptri_("U", &NR, DM, ipiv, dwork, &info);
 
   }
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_NORMAL )
   { 
     zgetri_(&NR, ZM, &NR, ipiv, &zlworkOptimal, &MinusOne, &info);
     lworkOptimal=(int) real(zlworkOptimal);

     if (lworkOptimal > lwork)
 {
        work.resize(lworkOptimal*sizeof(cdouble));
        lwork=lworkOptimal;
      };
     zwork=reinterpret_cast<cdouble *>(work.data());

     zgetri_(&NR, ZM, &NR, ipiv, zwork, &lwork, &info);
   }
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_HERMITIAN )
   { 
     lworkOptimal=NR;
     if (lworkOptimal > lwork)
 {
        work.resize(lworkOptimal*sizeof(cdouble));
        lwork=lworkOptimal;
      };
     zwork=reinterpret_cast<cdouble *>(work.data());
 
     zhptri_("U", &NR, ZM, ipiv, zwork, &info);
   }
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_SYMMETRIC )
   { 
     lworkOptimal=NR;
     if (lworkOptimal > lwork)
 {
        work.resize(lworkOptimal*sizeof(cdouble));
        lwork=lworkOptimal;
      };
     zwork=reinterpret_cast<cdouble *>(work.data());
 
     zsptri_("U", &NR, ZM, ipiv, zwork, &info);
   };

  return info;
}

/***************************************************************/
/* replace the matrix with its cholesky factorization **********/
/***************************************************************/
int HMatrix::CholFactorize()
{ 
  int info;

  if ( RealComplex==LHM_REAL && StorageType==LHM_NORMAL )
   dpotrf_("U", &NR, DM, &NR, &info);
  else if ( RealComplex==LHM_REAL && StorageType==LHM_SYMMETRIC )
   dpptrf_("U", &NR, DM, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_NORMAL )
   zpotrf_("U", &NR, ZM, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_HERMITIAN )
   zpptrf_("U", &NR, ZM, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_SYMMETRIC )
   { fprintf(stderr,"\n*\n* ERROR: cannot cholesky-factorize a complex symmetric matrix \n*\n");
     return -1;
   }
  else ErrExit("Invalid matrix properties");

  return info;
}

/***************************************************************/
/* solve linear system using cholesky factorization ************/
/***************************************************************/
int HMatrix::CholSolve(HVector *X)
{ 
  int info;
  int iOne=1;

  if ( NR!=NC || NR!=X->N )
   ErrExit("dimension mismatch in CholSolve");

  if ( RealComplex==LHM_REAL && StorageType==LHM_NORMAL )
   dpotrs_("U", &NR, &iOne, DM, &NR, X->DV, &NR, &info);
  else if ( RealComplex==LHM_REAL && StorageType==LHM_SYMMETRIC )
   dpptrs_("U", &NR, &iOne, DM, X->DV, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_NORMAL )
   zpotrs_("U", &NR, &iOne, ZM, &NR, X->ZV, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_HERMITIAN )
   zpptrs_("U", &NR, &iOne, ZM, X->ZV, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_SYMMETRIC )
   { fprintf(stderr,"\n*\n* ERROR: cannot cholesky-factorize a complex symmetric matrix \n*\n");
     return -1;
   }
  else ErrExit("Invalid matrix properties");

  return info;
}

/***************************************************************/
/* solve linear systems using cholesky factorization ***********/
/***************************************************************/
int HMatrix::CholSolve(HMatrix *X, int nrhs)
{ 
  int info;

  if ( NR!=NC || NR!=X->NR )
   ErrExit("dimension mismatch in CholSolve");
  if ( nrhs > X->NC )
   ErrExit("too many RHSs requested in CholSolve");

  if ( RealComplex==LHM_REAL && StorageType==LHM_NORMAL )
   dpotrs_("U", &NR, &nrhs, DM, &NR, X->DM, &NR, &info);
  else if ( RealComplex==LHM_REAL && StorageType==LHM_SYMMETRIC )
   dpptrs_("U", &NR, &nrhs, DM, X->DM, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_NORMAL )
   zpotrs_("U", &NR, &nrhs, ZM, &NR, X->ZM, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_HERMITIAN )
   zpptrs_("U", &NR, &nrhs, ZM, X->ZM, &NR, &info);
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_SYMMETRIC )
   { fprintf(stderr,"\n*\n* ERROR: cannot cholesky-factorize a complex symmetric matrix \n*\n");
     return -1;
   }
  else ErrExit("Invalid matrix properties");

  return info;
}

int HMatrix::CholSolve(HMatrix *X) { return CholSolve(X,X->NC); }
 
/***************************************************************/
/* QR-factorization in which 'this' is replaced by its own     */
/* QR-factorization. If *pQ and *pR are NULL on entry, or if   */
/* they point to matrices of the wrong sizes, then new         */
/* HMatrices of the appropriate size are allocated and returned*/
/* in those slots.                                             */
/***************************************************************/
int HMatrix::QR(HMatrix &Q, HMatrix &R)
{
  // the caller's Q and R must have the right sizes, namely Q is NRxNR
  // and R is NRxNC
  if ( Q.NR!=NR || Q.NC!=NR )
   ErrExit("wrong-size Q matrix (%ix%i) passed to HMatrix::QR (expected %ix%i)",
            Q.NR,Q.NC,NR,NR);
  if ( R.NR!=NR || R.NC!=NC )
   ErrExit("wrong-size R matrix (%ix%i) passed to HMatrix::QR (expected %ix%i)",
            R.NR,R.NC,NR,NC);

  /*--------------------------------------------------------------*/
  /*- set Q equal to the unit matrix -----------------------------*/
  /*--------------------------------------------------------------*/
  Q.Zero();
  for(int nr=0; nr<NR; nr++)
   Q.SetEntry(nr, nr, 1.0);

  /*--------------------------------------------------------------*/
  /*- call lapack's QR routine to get the R matrix, plus the     -*/
  /*- sequence of reflectors that define Q, in the body of the   -*/
  /*- R HMatrix                                                  -*/
  /*--------------------------------------------------------------*/
  R.Copy(this);
  int info, MinusOne=-1;
  int TauLength = NR<NC ? NR:NC;
  if ( RealComplex==LHM_REAL )
   { 
     double *Tau=R.DM, *dwork=nullptr;
     double dlworkOptimal;
     int lworkOptimal;

     // workspace size query
     dgeqrf_(&NR, &NC, R.DM, &NR, Tau, &dlworkOptimal, &MinusOne, &info);
     lworkOptimal = (int) dlworkOptimal;

     // make sure our internally-stored workspace is large enough
     // to store TauLength + lworkOptimal doubles
     if ( lwork < (TauLength+lworkOptimal) )
      { work.resize((TauLength+lworkOptimal)*sizeof(double));
        lwork=(TauLength+lworkOptimal);
      };
     Tau   = reinterpret_cast<double *>(work.data());
     dwork = Tau + TauLength;

     // do the QR factorization 
     dgeqrf_(&NR, &NC, R.DM, &NR, Tau, dwork, &lworkOptimal, &info);

     // reconstruct the Q matrix from the Householder reflectors
     dormqr_("L", "N", &NR, &NR, &TauLength, R.DM, &NR, Tau, Q.DM, &NR,
             dwork, &lworkOptimal, &info);

   }
  else //( RealComplex==LHM_COMPLEX
   { 
     cdouble *Tau=R.ZM, *zwork=nullptr;
     cdouble zlworkOptimal;
     int lworkOptimal;

     // workspace size query
     zgeqrf_(&NR, &NC, R.ZM, &NR, Tau, &zlworkOptimal, &MinusOne, &info);
     lworkOptimal = (int) real(zlworkOptimal);

     // make sure our internally-stored workspace is large enough
     // to store TauLength + lworkOptimal cdoubles
     if ( lwork < (TauLength+lworkOptimal) )
      { work.resize((TauLength+lworkOptimal)*sizeof(cdouble));
        lwork=(TauLength+lworkOptimal);
      };

     Tau   = reinterpret_cast<cdouble *>(work.data());
     zwork = Tau + TauLength;

     // do the QR factorization 
     zgeqrf_(&NR, &NC, R.ZM, &NR, Tau, zwork, &lworkOptimal, &info);

     // reconstruct the Q matrix from the Householder reflectors
     zunmqr_("L", "N", &NR, &NR, &TauLength, R.ZM, &NR, Tau, Q.ZM, &NR,
             zwork, &lworkOptimal, &info);

   };

  // zero out the lower diagonal of R 
  for(int nc=0; nc<NC; nc++)
   for(int nr=nc+1; nr<NR; nr++)
    R.SetEntry(nr,nc,0.0);

  return info;
}

/***************************************************************/
/* routine for eigenvalues and optionally vectors of           */
/* symmetric/hermitian matrices (dsyevr / zheevr)              */
/***************************************************************/
void HMatrix::EigCore(HVector *Lambda, HMatrix *U)
{
  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if ( U && ( (U->NR!=NR) || (U->NC!=NC)) )
   ErrExit("wrong-size U matrix (%ix%i) passed to Eig (expected %ix%i)",
            U->NR,U->NC,NR,NC);

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  const char *jobz  = (U==nullptr) ? "N" : "V";
  int idum1, idum2, idum3, lworkOptimal, liworkOptimal, info, MinusOne=-1;
  double ddum1, ddum2, AbsTol=0.0, dlworkOptimal;

  auto isuppz=std::make_unique<int[]>(static_cast<size_t>(2)*NR);
 
  if ( RealComplex==LHM_REAL && StorageType==LHM_NORMAL )
   { 
     /*--------------------------------------------------------------*/
     /*- query optimal workspace sizes and (re)allocate internally-  */
     /*- stored workspaces as necessary                              */
     /*--------------------------------------------------------------*/
     dsyevr_(jobz, "A", "U", &NR, DM, &NR, &ddum1, &ddum2, &idum1, &idum2, &AbsTol, &idum3,
             Lambda->DV, U ? U->DM : nullptr, &NR, isuppz.get(), &dlworkOptimal, &MinusOne,
             &liworkOptimal, &MinusOne, &info);
     lworkOptimal = (int)dlworkOptimal;
     if (lworkOptimal > lwork)
      { work.resize(lworkOptimal*sizeof(double));
        lwork=lworkOptimal;
      };
     if (liworkOptimal > liwork)
      { iwork.resize(liworkOptimal);
        liwork=liworkOptimal;
      };

     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     dsyevr_(jobz, "A", "U", &NR, DM, &NR, &ddum1, &ddum2, &idum1, &idum2, &AbsTol, &idum3,
             Lambda->DV, U ? U->DM : nullptr, &NR, isuppz.get(), reinterpret_cast<double *>(work.data()), &lwork,
             iwork.data(), &liwork, &info);
   }
  else if ( RealComplex==LHM_REAL && StorageType==LHM_SYMMETRIC )
   {
      ErrExit("Eig() not yet implemented for packed matrices");
   }
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_NORMAL )
   {
     /*--------------------------------------------------------------*/
     /*- query optimal workspace sizes and (re)allocate internally-  */
     /*- stored workspaces as necessary                              */
     /*--------------------------------------------------------------*/
     cdouble zlworkOptimal;
     double dlrworkOptimal;
     zheevr_(jobz, "A", "U", &NR, ZM, &NR, &ddum1, &ddum2, &idum1, &idum2,
             &AbsTol, &idum3, Lambda->DV, U ? U->ZM : nullptr, &NR, isuppz.get(),
             &zlworkOptimal, &MinusOne, &dlrworkOptimal, &MinusOne,
             &liworkOptimal, &MinusOne, &info);

     int workSize  = (int)real(zlworkOptimal);
     int rworkSize = (int)dlrworkOptimal;
     lworkOptimal = 2*workSize + rworkSize;
     if (lworkOptimal > lwork)
      { work.resize(lworkOptimal*sizeof(double));
        lwork=lworkOptimal;
      };

     cdouble *zwork = reinterpret_cast<cdouble *>(work.data());
     double *rwork = (double *)(zwork + workSize);

     if (liworkOptimal > liwork)
      { iwork.resize(liworkOptimal);
        liwork=liworkOptimal;
      };

     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     zheevr_(jobz, "A", "U", &NR, ZM, &NR, &ddum1, &ddum2, &idum1, &idum2, &AbsTol, &idum3,
             Lambda->DV, U ? U->ZM : nullptr, &NR, isuppz.get(), reinterpret_cast<cdouble *>(work.data()), &workSize,
             rwork, &rworkSize, iwork.data(), &liwork, &info);

   }
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_HERMITIAN )
   {
      ErrExit("Eig() not yet implemented for packed matrices");
   }
  else if ( RealComplex==LHM_COMPLEX && StorageType==LHM_SYMMETRIC )
   {
      ErrExit("use NSEig() for non-hermitian eigenproblems");
   }

}

/***************************************************************/
/* Eig entry points                                            */
/***************************************************************/
std::unique_ptr<HVector> HMatrix::Eig()
{
  auto Lambda=std::make_unique<HVector>(NR, LHM_REAL);
  EigCore(Lambda.get(), nullptr);
  return Lambda;
}

std::unique_ptr<HVector> HMatrix::Eig(HMatrix &U)
{
  auto Lambda=std::make_unique<HVector>(NR, LHM_REAL);
  EigCore(Lambda.get(), &U);
  return Lambda;
}

void HMatrix::Eig(HVector &Lambda)
{
  if ( Lambda.N!=NR || Lambda.RealComplex!=LHM_REAL )
   ErrExit("wrong-size Lambda vector passed to Eig (%i entries; expected %i)",
            Lambda.N,NR);
  EigCore(&Lambda, nullptr);
}

void HMatrix::Eig(HVector &Lambda, HMatrix &U)
{
  if ( Lambda.N!=NR || Lambda.RealComplex!=LHM_REAL )
   ErrExit("wrong-size Lambda vector passed to Eig (%i entries; expected %i)",
            Lambda.N,NR);
  EigCore(&Lambda, &U);
}

/***************************************************************/
/* routine for eigenvalues and optionally vectors of           */
/* non-symmetric matrices (dsyevr / zheevr)                    */
/***************************************************************/
void HMatrix::NSEigCore(HVector *Lambda, HMatrix *U)
{
  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if ( U && ( (U->NR!=NR) || (U->NC!=NC)) )
   ErrExit("wrong-size U matrix (%ix%i) passed to NSEig (expected %ix%i)",
            U->NR,U->NC,NR,NC);

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  const char *jobvl = "N";
  const char *jobvr = ((U==nullptr) ? "N" : "V");
  int lworkOptimal, info, MinusOne=-1;

  if (RealComplex==LHM_REAL)
   { 
     /*--------------------------------------------------------------*/
     /*- query optimal workspace sizes and (re)allocate internally-  */
     /*- stored workspaces as necessary                              */
     /*--------------------------------------------------------------*/
     double *wr = new double[NR];
     double *wi = new double[NR];
     double dlworkOptimal;
     dgeev_(jobvl, jobvr, &NR, DM, &NR, wr, wi, nullptr, &NR, 
            U ? U->DM : nullptr, &NR, &dlworkOptimal, &MinusOne, &info);

     lworkOptimal = (int)dlworkOptimal;
     if (lworkOptimal > lwork)
      { work.resize(lworkOptimal*sizeof(double));
        lwork=lworkOptimal;
      };

     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     dgeev_(jobvl, jobvr, &NR, DM, &NR, wr, wi, nullptr, &NR, 
            U ? U->DM : nullptr, &NR, reinterpret_cast<double *>(work.data()), &lwork, &info);

     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     for(int n=0; n<NR; n++)
      Lambda->SetEntry( n, cdouble(wr[n], wi[n]));

     delete[] wr;
     delete[] wi;
   }
  else // (RealComplex==LHM_COMPLEX)
   {
     /*--------------------------------------------------------------*/
     /*- query optimal workspace sizes and allocate workspaces       */
     /*--------------------------------------------------------------*/
     double *rwork = (double *)mallocEC(2*NR*sizeof(double));

     cdouble zlworkOptimal;
     zgeev_(jobvl, jobvr, &NR, ZM, &NR, Lambda->ZV, nullptr, &NR, 
            (U ? U->ZM : nullptr), &NR, 
            &zlworkOptimal, &MinusOne, rwork, &info);
     lworkOptimal = 2*(int)(ceil(real(zlworkOptimal)));
     cdouble *zwork = (cdouble *)mallocEC(lworkOptimal * sizeof(cdouble));

     zgeev_(jobvl, jobvr, &NR, ZM, &NR, Lambda->ZV, nullptr, &NR,
            U ? U->ZM : nullptr, &NR, zwork, &lworkOptimal, rwork, &info);

     Log("zgeev(%i): info=%i",NR,info);
     free(zwork);
     free(rwork);
   }

}

/***************************************************************/
/* NSEig entry points                                          */
/***************************************************************/
std::unique_ptr<HVector> HMatrix::NSEig()
{
  auto Lambda=std::make_unique<HVector>(NR, LHM_COMPLEX);
  NSEigCore(Lambda.get(), nullptr);
  return Lambda;
}

std::unique_ptr<HVector> HMatrix::NSEig(HMatrix &U)
{
  auto Lambda=std::make_unique<HVector>(NR, LHM_COMPLEX);
  NSEigCore(Lambda.get(), &U);
  return Lambda;
}

void HMatrix::NSEig(HVector &Lambda)
{
  if ( Lambda.N!=NR || Lambda.RealComplex!=RealComplex )
   ErrExit("wrong-size Lambda vector passed to NSEig (%i entries; expected %i)",
            Lambda.N,NR);
  NSEigCore(&Lambda, nullptr);
}

void HMatrix::NSEig(HVector &Lambda, HMatrix &U)
{
  if ( Lambda.N!=NR || Lambda.RealComplex!=RealComplex )
   ErrExit("wrong-size Lambda vector passed to NSEig (%i entries; expected %i)",
            Lambda.N,NR);
  NSEigCore(&Lambda, &U);
}

/***************************************************************/
/* routine for singular-value decomposition (dgesvd / zgesvd)  */
/*                                                             */
/* If Sigma is NULL on entry, a new vector is allocated and    */
/* returned.                                                   */
/*                                                             */
/* If U is:                                                    */
/*  -- NULL:     no right singular vectors computed            */
/*  -- NR x NR:  U <-- matrix of full RSVs (JOBU='A')          */
/*  -- NR x NS:  U <-- matrix of reduced RSVs (JOBU='S')       */
/*                                                             */
/* If VT is:                                                   */
/*  -- NULL:     no left singular vectors computed             */
/*  -- NC x NC:  VT <-- (adjoint of) matrix of full LSVs (JOBV='A')  */
/*  -- NS x NC:  VT <-- (adjoint of) matrix of reduced LSVs (JOBV='S') */
/***************************************************************/
void HMatrix::SVDCore(HVector *Sigma, HMatrix *U, HMatrix *VT)
{
  if (StorageType!=LHM_NORMAL)
   ErrExit("SVD() not supported for packed-storage matrices");

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  int NRCMin = (NR<NC ? NR : NC);

  char jobu[2]="";
  if ( U==nullptr )
   jobu[0]='N';
  else if ( U->NR==NR && U->NC==NR     && U->RealComplex==RealComplex)
   jobu[0]='A';
  else if ( U->NR==NR && U->NC==NRCMin && U->RealComplex==RealComplex)
   jobu[0]='S';
  else
   ErrExit("wrong-size U matrix (%ix%i) passed to SVD",U->NR,U->NC);
  int LDU = U ? U->NR : 0;

  char jobvt[2]="";
  if ( VT==nullptr )
   jobvt[0]='N';
  else if ( VT->NR==NC && VT->NC==NC     && VT->RealComplex==RealComplex)
   jobvt[0]='A';
  else if ( VT->NR==NRCMin && VT->NC==NC && VT->RealComplex==RealComplex)
   jobvt[0]='S';
  else
   ErrExit("wrong-size VT matrix (%ix%i) passed to SVD",VT->NR,VT->NC);
  int LDVT = VT ? VT->NR : 0;

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  int lworkOptimal, info, MinusOne=-1;
  if (RealComplex==LHM_REAL)
   { 
     /*--------------------------------------------------------------*/
     /*- query optimal workspace sizes and (re)allocate internally-  */
     /*- stored workspaces as necessary                              */
     /*--------------------------------------------------------------*/
     double dlworkOptimal;
     dgesvd_(jobu, jobvt, &NR, &NC, DM, &NR, Sigma->DV,
             U ? U->DM : nullptr, &LDU, VT ? VT->DM : nullptr, &LDVT,
             &dlworkOptimal, &MinusOne, &info);

     lworkOptimal = (int)dlworkOptimal;
     if (lworkOptimal > lwork)
      { work.resize(lworkOptimal*sizeof(double));
        lwork=lworkOptimal;
      };

     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     dgesvd_(jobu, jobvt, &NR, &NC, DM, &NR, Sigma->DV,
             U ? U->DM : nullptr, &LDU, VT ? VT->DM : nullptr, &LDVT,
             reinterpret_cast<double *>(work.data()), &lwork, &info);
   }
  else // (RealComplex==LHM_COMPLEX)
   {
     /*--------------------------------------------------------------*/
     /*- query optimal workspace sizes and (re)allocate internally-  */
     /*- stored workspaces as necessary                              */
     /*--------------------------------------------------------------*/
     double *rwork = new double[5*NRCMin];
     cdouble zlworkOptimal;
     zgesvd_(jobu, jobvt, &NR, &NC, ZM, &NR, Sigma->DV, 
             U ? U->ZM : nullptr, &LDU, VT ? VT->ZM : nullptr, &LDVT, 
             &zlworkOptimal, &MinusOne, rwork, &info);

     lworkOptimal = 2*(int)(real(zlworkOptimal));
     if (lworkOptimal > lwork)
      { work.resize(lworkOptimal*sizeof(double));
        lwork=lworkOptimal;
      };

     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     zgesvd_(jobu, jobvt, &NR, &NC, ZM, &NR, Sigma->DV, 
             U ? U->ZM : nullptr, &LDU, VT ? VT->ZM : nullptr, &LDVT,
             reinterpret_cast<cdouble *>(work.data()), &lwork, rwork, &info);
     delete[] rwork;

   };
}

/***************************************************************/
/* SVD entry points                                            */
/***************************************************************/
std::unique_ptr<HVector> HMatrix::SVD()
{
  int NRCMin = (NR<NC ? NR : NC);
  auto Sigma=std::make_unique<HVector>(NRCMin, LHM_REAL);
  SVDCore(Sigma.get(), nullptr, nullptr);
  return Sigma;
}

std::unique_ptr<HVector> HMatrix::SVD(HMatrix &U, HMatrix &VT)
{
  int NRCMin = (NR<NC ? NR : NC);
  auto Sigma=std::make_unique<HVector>(NRCMin, LHM_REAL);
  SVDCore(Sigma.get(), &U, &VT);
  return Sigma;
}

void HMatrix::SVD(HVector &Sigma)
{
  int NRCMin = (NR<NC ? NR : NC);
  if ( Sigma.N!=NRCMin || Sigma.RealComplex!=LHM_REAL )
   ErrExit("wrong-size Sigma vector passed to SVD (%i entries; expected %i)",
            Sigma.N,NRCMin);
  SVDCore(&Sigma, nullptr, nullptr);
}

void HMatrix::SVD(HVector &Sigma, HMatrix &U, HMatrix &VT)
{
  int NRCMin = (NR<NC ? NR : NC);
  if ( Sigma.N!=NRCMin || Sigma.RealComplex!=LHM_REAL )
   ErrExit("wrong-size Sigma vector passed to SVD (%i entries; expected %i)",
            Sigma.N,NRCMin);
  SVDCore(&Sigma, &U, &VT);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
double HMatrix::GetNorm(bool UseInfinityNorm)
{
  char *Norm = const_cast <char *>(UseInfinityNorm ? "I" : "1");

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (RealComplex==LHM_REAL)
   { 
     /*--------------------------------------------------------------*/
     /*- (re)allocate internally--stored workspaces as necessary     */
     /*--------------------------------------------------------------*/
     int lworkOptimal = UseInfinityNorm ? NR : 0;
     if (lworkOptimal > lwork)
      { work.resize(lworkOptimal*sizeof(double));
        lwork=lworkOptimal;
      };

     double *rwork=reinterpret_cast<double *>(work.data());
     return dlange_(Norm, &NR, &NC, DM, &NR, rwork);
   }
  else // (RealComplex==LHM_COMPLEX)
   {
     /*--------------------------------------------------------------*/
     /*- (re)allocate internally--stored workspaces as necessary     */
     /*--------------------------------------------------------------*/
     int lworkOptimal = UseInfinityNorm ? NR : 0;
     if (lworkOptimal > lwork)
      { work.resize(lworkOptimal*sizeof(double));
        lwork=lworkOptimal;
      };
     double *rwork  = reinterpret_cast<double *>(work.data());
     return zlange_(Norm, &NR, &NC, ZM, &NR, rwork);
   };

  return 0.0;

}

/***************************************************************/
/***************************************************************/ 
/***************************************************************/
double HMatrix::GetRCond(double ANorm, bool UseInfinityNorm)
{
  char *Norm = const_cast<char *> (UseInfinityNorm ? "I" : "1");
  double RCond;

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (RealComplex==LHM_REAL)
   { 
     /*--------------------------------------------------------------*/
     /*- (re)allocate internally--stored workspaces as necessary     */
     /*--------------------------------------------------------------*/
     int lworkOptimal = 4*NR;
     if (lworkOptimal > lwork)
      { work.resize(lworkOptimal*sizeof(double));
        lwork=lworkOptimal;
      };
     int iworkOptimal = NR;
     if (iworkOptimal > liwork)
      { iwork.resize(iworkOptimal);
        liwork=iworkOptimal;
      };

     double *rwork=reinterpret_cast<double *>(work.data());
     int info;
     dgecon_(Norm, &NR, DM, &NR, &ANorm, &RCond, rwork, iwork.data(), &info);
   }
  else // (RealComplex==LHM_COMPLEX)
   {
     /*--------------------------------------------------------------*/
     /*- (re)allocate internally--stored workspaces as necessary     */
     /*--------------------------------------------------------------*/
     int lworkOptimal = 6*NR; // 2N doubles plus 2N cdoubles
     if (lworkOptimal > lwork)
      { work.resize(lworkOptimal*sizeof(double));
        lwork=lworkOptimal;
      };
  
     double *rwork  = reinterpret_cast<double *>(work.data());
     double *work2  = rwork + 2*NR;
     cdouble *zwork = (cdouble *)work2;
     int info;
     zgecon_(Norm, &NR, ZM, &NR, &ANorm, &RCond, zwork, rwork, &info);

   };

  return RCond;

}

/***************************************************************/
/* dot product of vectors **************************************/
/***************************************************************/
cdouble DoDot(HVector *A, HVector *B, bool Hermitian=true)
{
  if ( (A->N != B->N) || (A->RealComplex != B->RealComplex) )
   { Warn("attempt to dot-product incompatible vectors (returning 0)");
     return 0.0;
   };

  int N=A->N, IncX=1, IncY=1;
  if (A->RealComplex==LHM_REAL)
   return ddot_(&N, A->DV, &IncX, B->DV, &IncY);
  else if (Hermitian)
   return zdotc_(&N, A->ZV, &IncX, B->ZV, &IncY);
  else 
   return zdotu_(&N, A->ZV, &IncX, B->ZV, &IncY);
}

cdouble HVector::Dot(HVector *B)
 { return DoDot(this, B, true); }

cdouble HVector::DotU(HVector *B)
 { return DoDot(this, B, false); }

double HVector::DotD(HVector *B)
 { return real(DoDot(this, B, true)); }

/***************************************************************/
/* matrix-vector multiplication, A*X = Y (where this=A)        */
/*                                                             */
/*  Transpose=='C'   // use the hermitian transpose            */
/*  Transpose=='T'   // use the non-hermitian transpose        */
/*  Transpose==anything else for no transpose                  */
/***************************************************************/
void HMatrix::Apply(HVector *X, HVector *Y, char TransChar)
{
  if ( StorageType!=LHM_NORMAL )
   ErrExit("%s:%i: HMatrix::Apply not yet implemented for packed matrices",
           __FILE__,__LINE__);
  if ( X==nullptr || Y==nullptr  )
   ErrExit("%s:%i: invalid operand in HMatrix::Apply",__FILE__,__LINE__);
  if ( (RealComplex!=X->RealComplex) || (RealComplex!=Y->RealComplex) )
   ErrExit("%s:%i: data type mismatch in HMatrix::Apply",__FILE__,__LINE__);

  char Trans[2]="N";
  int NREffective=NR;
  int NCEffective=NC;
  if (TransChar=='T' || TransChar=='C')
   { Trans[0]=TransChar;
     NREffective=NC;
     NCEffective=NR;
   };

  if ( (X->N != NCEffective) || (Y->N != NREffective) )
   ErrExit("%s:%i: dimension mismatch in HMatrix::Apply",__FILE__,__LINE__);

  int IncX=1, IncY=1;
  double dOne=1.0, dZero=0.0;
  cdouble zOne=1.0, zZero=0.0;

  if (RealComplex==LHM_REAL)
   dgemv_(Trans, &NR, &NC, &dOne, DM, &NR, X->DV, &IncX, &dZero, Y->DV, &IncY);
  else 
   zgemv_(Trans, &NR, &NC, &zOne, ZM, &NR, X->ZV, &IncX, &zZero, Y->ZV, &IncY);

}
