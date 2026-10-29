/*
 * GeometryTest.cc -- lifetime tests for libscuff geometry objects.
 *
 * The rest of the test suite solves problems with geometries that are
 * never deleted (they leak at process exit by design), so the
 * RWGGeometry / RWGSurface / FIBBICache destructor paths were previously
 * unexercised.  These tests construct geometries and delete them; run
 * under ASan they would catch use-after-free in ~RWGGeometry (the Mate
 * array used to be freed before it was read) and double frees of shared
 * caches.
 */

#include <cstdio>
#include <cstring>

#include "libscuff.h"

using namespace scuff;

namespace {

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

void TestSingleSurfaceDestructor()
{
  RWGGeometry *G = new RWGGeometry("LossySphere_327.scuffgeo");
  CHECK(G != nullptr);
  CHECK(G->NumSurfaces == 1);
  CHECK(G->Mate[0] == -1);
  CHECK(G->FIBBICaches[0] != nullptr);

  /* ~RWGGeometry must not read the Mate array after freeing it, and
     must destroy the single FIBBI cache exactly once. */
  delete G;
}

void TestMatedSurfacesDestructor()
{
  /* Two OBJECTs sharing one mesh file and one material name are
     recognized as mates: the second surface aliases the first one's
     FIBBI cache, and only the first cache may be destroyed. */
  RWGGeometry *G = new RWGGeometry("TwoSpheres.scuffgeo");
  CHECK(G->NumSurfaces == 2);
  CHECK(G->Mate[0] == -1);
  CHECK(G->Mate[1] == 0);
  CHECK(G->FIBBICaches[0] != nullptr);
  CHECK(G->FIBBICaches[1] == G->FIBBICaches[0]);

  delete G;
}

} // namespace

int main()
{
  TestSingleSurfaceDestructor();
  TestMatedSurfacesDestructor();

  std::printf("%s: %i checks, %i failures\n",
              NumFailures ? "FAILED" : "PASSED", NumChecks, NumFailures);
  return NumFailures ? 1 : 0;
}
