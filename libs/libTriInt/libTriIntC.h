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
 * libTriIntC.h -- C-compatible declarations for the libTriInt routines
 *                 that are implemented in plain C (TCR.c, libTriInt.c,
 *                 CCCubature.c, IntegrateCliffFunction.c, and the
 *                 GetCCRule accessor in the generated CCRules.cc).
 *
 * This header is included from libTriInt.h (so C++ callers get the
 * extern-"C" linkage automatically) and directly by the .c files
 * (which cannot include libTriInt.h because that header pulls in the
 * C++ libhrutil/libhmat interfaces).
 */

#ifndef LIBTRIINTC_H
#define LIBTRIINTC_H

#include <stddef.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include <libSGJC.h>   /* integrand, error_norm (C-compatible) */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TriIntFun)(double *X, void *UserData, double *F);

/* TCR.c -- triangle cubature rules */
double *GetTCR(int Order, int *NumPts);

/* libTriInt.c -- fixed and adaptive triangle cubature */
int TriIntFixed(TriIntFun F, int nFun, void *UserData,
                double *V1, double *V2, double *V3,
                int Order, double *Result);

void TriIntEmbedded(TriIntFun F, int nFun, void *UserData,
                    double *V1, double *V2, double *V3,
                    double *I, double *Error);

/* CCRules.cc -- accessor for the generated Clenshaw-Curtis tables */
double *GetCCRule(int NumPts);

/* CCCubature.c -- embedded and rectangular-rank CC cubature */
void ECC2D(int p, double xMin[2], double xMax[2],
           integrand Integrand, void *UserData, int nFun,
           bool xySymmetric,
           double *AllValues, double *OuterValues,
           double *Result, double *Error);

void ECC(int p, double xMin, double xMax,
         integrand Integrand, void *UserData, int nFun,
         double *AllValues, double *OuterValues,
         double *Result, double *Error);

int CCCubature(int Order, unsigned fdim, integrand f, void *fdata,
               unsigned dim, const double *xmin, const double *xmax,
               size_t maxEval, double reqAbsError, double reqRelError,
               error_norm norm, double *Integral, double *Error);

int RRCubature(int Order, int *Orders,
               int FDim, integrand f, void *UserData,
               int IDim, const double *Lower, const double *Upper,
               double *Integral, double *Error);

/* IntegrateCliffFunction.c -- adaptive 1D integrator with per-component
   convergence skipping */
typedef int (*CliffFunction) (unsigned ndim, const double *x, void *params,
                              unsigned fdim, const bool *Skip, double *fval);

int IntegrateCliffFunction(CliffFunction fCliff, void *UserData, int nFun,
                           double xMin, double xMax, double xCliff,
                           double AbsTol, double RelTol,
                           double *Integral, double *Error,
                           char *LogFileName);

#ifdef __cplusplus
}
#endif

#endif /* LIBTRIINTC_H */
