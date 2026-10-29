/*
 * SphericalTest.cc -- reference tests for the libSpherical public API.
 *
 * GetYlm (scalar) had a `m%1` typo that made the (-1)^m parity factor
 * of negative-m harmonics inert; the test compares it against an
 * independently coded closed-form implementation of the standard
 * complex spherical harmonics and against GetYlmDerivArray, which
 * already implemented the correct relation.  GetVSWRadialFunctions
 * zeroed only a fraction of its output for r==0 (missing
 * sizeof(cdouble)); the test checks the r==0 limiting values against
 * the documented ones.
 */

#include <cmath>
#include <cstdio>
#include <complex>

#include "libSpherical.h"

namespace {

using zdouble = std::complex<double>;

int NumChecks = 0;
int NumFailures = 0;

void Check(bool Condition, const char *Expression, const char *File, int Line)
{
  ++NumChecks;
  if (!Condition)
   { ++NumFailures;
     std::fprintf(stderr, "FAIL %s:%i: %s\n", File, Line, Expression);
   }
}

#define CHECK(Condition) Check((Condition), #Condition, __FILE__, __LINE__)

void CheckNear(zdouble Actual, zdouble Expected, double Tol,
               const char *What, const char *File, int Line)
{
  ++NumChecks;
  if (std::abs(Actual - Expected) > Tol)
   { ++NumFailures;
     std::fprintf(stderr, "FAIL %s:%i: %s: expected (%g,%g), got (%g,%g)\n",
                  File, Line, What, real(Expected), imag(Expected),
                  real(Actual), imag(Actual));
   }
}

#define CHECK_NEAR(Actual, Expected, Tol, What) \
  CheckNear((Actual), (Expected), (Tol), (What), __FILE__, __LINE__)

/*--------------------------------------------------------------*/
/* independently coded reference implementation of the standard */
/* complex spherical harmonics (Condon-Shortley convention),    */
/* written from the textbook recursions rather than by calling  */
/* any libSpherical routine                                     */
/*--------------------------------------------------------------*/
double DoubleFactorial(int n)
{ double Result=1.0;
  for(int k=n; k>=1; k-=2) Result*=k;
  return Result;
}

double Factorial(int n)
{ double Result=1.0;
  for(int k=2; k<=n; k++) Result*=k;
  return Result;
}

/* unnormalized associated Legendre polynomial with the
   Condon-Shortley phase: P_m^m(x) = (-1)^m (2m-1)!! (1-x^2)^{m/2} */
double RefPlm(int l, int m, double x)
{
  double Pmm = (m%2 ? -1.0 : 1.0)
             * DoubleFactorial(2*m-1)
             * std::pow(1.0-x*x, 0.5*m);
  if (l==m) return Pmm;
  double Pm1m = x*(2*m+1)*Pmm;
  if (l==m+1) return Pm1m;
  double Pll=0.0;
  for(int ll=m+2; ll<=l; ll++)
   { Pll = ( (2*ll-1)*x*Pm1m - (ll+m-1)*Pmm ) / (ll-m);
     Pmm = Pm1m;
     Pm1m = Pll;
   };
  return Pll;
}

zdouble RefYlm(int l, int m, double Theta, double Phi)
{
  int absm = std::abs(m);
  double Normalization = std::sqrt( (2*l+1)/(4.0*M_PI)
                                    * Factorial(l-absm)/Factorial(l+absm) );
  zdouble Positive = Normalization * RefPlm(l, absm, cos(Theta))
                     * std::polar(1.0, absm*Phi);
  if (m>=0) return Positive;
  return (absm%2 ? -1.0 : 1.0) * std::conj(Positive);
}

/*--------------------------------------------------------------*/
/* index convention of GetYlmDerivArray: entry l*l+l+m          */
/*--------------------------------------------------------------*/
void TestYlmAgainstReference()
{
  const double Thetas[]={0.7, 2.5, 1.5707963267948966};
  const double Phis[]={1.3, -0.4, 0.0};

  for(unsigned nt=0; nt<sizeof(Thetas)/sizeof(double); nt++)
   for(unsigned np=0; np<sizeof(Phis)/sizeof(double); np++)
    for(int l=0; l<=3; l++)
     for(int m=-l; m<=l; m++)
      { zdouble Got=GetYlm(l,m,Thetas[nt],Phis[np]);
        char What[64];
        std::snprintf(What,sizeof(What),"Ylm(%i,%i)",l,m);
        CHECK_NEAR(Got, RefYlm(l,m,Thetas[nt],Phis[np]), 1.0e-10, What);
      };
}

void TestYlmAgainstArray()
{
  int lMax=3;
  int NAlpha=(lMax+1)*(lMax+1);
  zdouble YArray[NAlpha];
  const double Theta=0.9, Phi=2.1;

  GetYlmArray(lMax, Theta, Phi, YArray);
  for(int l=0; l<=lMax; l++)
   for(int m=-l; m<=l; m++)
    { char What[64];
      std::snprintf(What,sizeof(What),"Ylm vs array (%i,%i)",l,m);
      CHECK_NEAR(GetYlm(l,m,Theta,Phi), YArray[l*l+l+m], 1.0e-11, What);
    };
}

void TestRadialFunctionsAtOrigin()
{
  int LMax=2;
  int NumRF=3*(LMax+1);
  zdouble RFArray[NumRF];
  zdouble Workspace[10*(LMax+2)];

  /* poison the output: the r==0 path must fully zero it */
  for(int n=0; n<NumRF; n++)
   RFArray[n]=zdouble(1.0e99, 1.0e99);

  GetVSWRadialFunctions(LMax, zdouble(1.0), 0.0, LS_REGULAR,
                        (cdouble *)RFArray, (double *)Workspace,
                        false, false);

  for(int n=0; n<NumRF; n++)
   if (n!=3*1+1 && n!=3*1+2)
    CHECK(RFArray[n]==zdouble(0.0,0.0));

  /* documented limiting values at r=0 (for TimesrFactor=false) */
  CHECK_NEAR(RFArray[3*1+1], zdouble(2.0/3.0), 1.0e-12, "RF[4]");
  CHECK_NEAR(RFArray[3*1+2], zdouble(-std::sqrt(2.0)/3.0), 1.0e-12, "RF[5]");
}

} // namespace

int main()
{
  TestYlmAgainstReference();
  TestYlmAgainstArray();
  TestRadialFunctionsAtOrigin();

  std::printf("%s: %i checks, %i failures\n",
              NumFailures ? "FAILED" : "PASSED", NumChecks, NumFailures);
  return NumFailures ? 1 : 0;
}
