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
 * libTriInt.h  -- header for TriInt library providing routines for
 *              -- fixed-order and adaptive cubature over a triangle
 *
 * homer reid   -- 12/2008
 */

#ifndef LIBTRIINT_H
#define LIBTRIINT_H

#include <libhrutil.h>
#include <libhmat.h>
#include <libSGJC.h>

#include "libTriIntC.h"

/***************************************************************/
/* The C-implemented routines (triangle cubature rules, fixed  */
/* and adaptive triangle cubature, Clenshaw-Curtis and         */
/* rectangular-rank cubature, and the cliff integrator) are    */
/* declared in libTriIntC.h with C linkage.                    */
/*                                                             */
/* The declarations below are the C++-specific additions:      */
/* default-argument convenience wrappers and routines that     */
/* remain implemented in C++ (HMatrix interfaces, generated    */
/* Lebedev tables, DCUTRI stubs).                              */
/***************************************************************/

/* the cliff integrator's optional log-file argument */
int IntegrateCliffFunction(CliffFunction fCliff, void *UserData, int nFun,
                           double xMin, double xMax, double xCliff,
                           double AbsTol, double RelTol,
                           double *Integral, double *Error,
                           char *LogFileName=nullptr);

/* accessors for the generated Lebedev tables (LebedevRules.cc, C++) */
double *GetLebedevRule(int NumPts);

/* DCUTRI entry points (BZIntegration.cc, C++) */
void *CreateDCUTRIWorkspace(int numfun, int maxpts);
int DCUTRI(void *opW, double **Vertices, TriIntFun func,
           void *parms, double epsabs, double epsrel,
           double *I, double *abserr);

/***************************************************************/
/* Routine for adaptive lattice summation to a desired error   */
/* tolerance, Sum = \sum_{L} F(L)                              */
/* where F is a user-supplied vector-valued summand            */
/* and L ranges over all points in a D-dimensional lattice.    */
/*                                                             */
/* note that SummandFunction should ACCUMULATE the             */
/* contribution of lattice point U to the sum, i.e. it should  */
/* implement something like                                    */
/*  Sum[ns] += f_{ns}[U]                                       */
/* and not                                                     */
/*  Sum[ns] = f_{ns}[U].                                       */
/***************************************************************/
using SummandFunction = void (*)(double *L, void *UserData, double *Sum);

int GetLatticeSum(SummandFunction Summand, void *UserData, int nSum,
                  HMatrix *LBasis, double *Sum,
                  double AbsTol=0.0, double RelTol=1.0e-2,
                  int MaxCells=1000);

#endif
