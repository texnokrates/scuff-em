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
 * homer reid  -- 9/2011                                        
 *                                         
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <libhrutil.h>
#include <libhmat.h>
#include <libscuff.h>
#include <libSGJC.h>

#include "RWGPorts.h"

#define ABSTOL 1.0e-8
#define RELTOL 1.0e-4
#define FREQ2OMEGA (2.0*M_PI/300.0)
#define II cdouble(0.0,1.0)

FILE *BreakoutFile=0; 
int SkipInterior=0, SkipExterior=0;

using namespace scuff;

/***************************************************************/
/***************************************************************/
/***************************************************************/
void GetPanelPotentials(RWGObject *O, int np, int iQ, cdouble IK,
                        double *X, cdouble *PhiA);

cdouble GetPanelPotential(RWGObject *O, int np, cdouble IK, double *X);

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
void WriteST(RWGObject *O, int np, double Val, FILE *f);
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

/***************************************************************/
/* integrand function used to evaluate the line integral       */
/* iwA \cdot dl from X1 to X2                                  */
/* x[0] = Tau where X = X_1 + Tau(X_2-X_1)                     */
/***************************************************************/
typedef struct iwAIData 
 {
   RWGGeometry *G;
   HVector *KN;
   RWGPort **Ports;
   int NumPorts;
   cdouble *PortCurrents;
   cdouble IK;
   double *X1, *X2;
 } iwAIData;

void iwaIntegrand(unsigned ndim, const double *x, void *params, 
                  unsigned fdim, double *fval)
{
  (void) ndim;
  (void) fdim;

  iwAIData *iwAID = (iwAIData *)params;

  RWGGeometry *G        = iwAID->G;
  HVector *KN           = iwAID->KN;
  RWGPort **Ports       = iwAID->Ports;
  int NumPorts          = iwAID->NumPorts;
  cdouble *PortCurrents = iwAID->PortCurrents;
  cdouble IK            = iwAID->IK;
  double *X1            = iwAID->X1;
  double *X2            = iwAID->X2;

  double Tau=x[0];
  double X[3], X2mX1[3];
  VecSub(X2, X1, X2mX1);
  X[0] = X1[0] + Tau*X2mX1[0];
  X[1] = X1[1] + Tau*X2mX1[1];
  X[2] = X1[2] + Tau*X2mX1[2];

  /*--------------------------------------------------------------*/
  /*- contributions of interior edges ----------------------------*/
  /*--------------------------------------------------------------*/
  int BFIndex, no, ne;
  cdouble PhiAP[4], PhiAM[4], iwAI;
  RWGObject *O;
  RWGEdge *E;
  iwAI=0.0;
  for(BFIndex=0, no=0; no<G->NumObjects; no++)
   for(O=G->Objects[no], ne=0; ne<O->NumEdges; ne++, BFIndex++)
    { 
      E=O->Edges[ne];
      GetPanelPotentials(O, E->iPPanel, E->PIndex, IK, X, PhiAP);
      GetPanelPotentials(O, E->iMPanel, E->MIndex, IK, X, PhiAM);
      iwAI += KN->GetEntry(BFIndex) * (  (PhiAP[1]-PhiAM[1])*X2mX1[0]
                                        +(PhiAP[2]-PhiAM[2])*X2mX1[1]
                                        +(PhiAP[3]-PhiAM[3])*X2mX1[2]
                                      );
    };

  /*--------------------------------------------------------------*/
  /*- contributions of driven ports ------------------------------*/
  /*--------------------------------------------------------------*/
  int nPort, nPanel, PanelIndex, iQ;
  cdouble PortCurrent, Weight, PhiA[4];
  RWGPort *Port;
  for(nPort=0; nPort<NumPorts; nPort++)
   { 
     PortCurrent=PortCurrents[nPort];
     if (PortCurrent==0.0) 
      continue;
     Port=Ports[nPort];
     
     /*--------------------------------------------------------------*/
     /*- contribution of panels on positive edge of port ------------*/
     /*--------------------------------------------------------------*/
     O = Port->PObject;
     Weight = PortCurrent / Port->PPerimeter;
     for(nPanel=0; nPanel<Port->NumPEdges; nPanel++)
      { 
        PanelIndex   = Port->PPanelIndices[nPanel];
        iQ           = Port->PPaneliQs[nPanel];
        
        GetPanelPotentials(O, PanelIndex, iQ, IK, X, PhiA);
        iwAI -= Weight * ( PhiA[1]*X2mX1[0] + PhiA[2]*X2mX1[1] + PhiA[3]*X2mX1[2] );
      };
     
     /*--------------------------------------------------------------*/
     /*- contribution of panels on negative edge of port ------------*/
     /*--------------------------------------------------------------*/
     O = Port->MObject;
     Weight = PortCurrent / Port->MPerimeter;
     for(nPanel=0; nPanel<Port->NumMEdges; nPanel++)
      { 
        PanelIndex   = Port->MPanelIndices[nPanel];
        iQ           = Port->MPaneliQs[nPanel];
        
        GetPanelPotentials(O, PanelIndex, iQ, IK, X, PhiA);
        iwAI += Weight * ( PhiA[1]*X2mX1[0] + PhiA[2]*X2mX1[1] + PhiA[3]*X2mX1[2] );
      };

   };
  
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  *(cdouble *)fval=iwAI;

}

/***************************************************************/
/* compute the line integral \int iwA dl over the straight     */
/* line connecting X1 to X2                                    */
/***************************************************************/
cdouble iwAIntegral(RWGGeometry *G, HVector *KN, 
                    RWGPort **Ports, int NumPorts, cdouble *PortCurrents,
                    cdouble IK, double *X1, double *X2, double RefVal)
{
  iwAIData MyiwAIData, *iwAID=&MyiwAIData;

  iwAID->G=G;
  iwAID->KN=KN;
  iwAID->Ports=Ports;
  iwAID->NumPorts=NumPorts;
  iwAID->PortCurrents=PortCurrents;
  iwAID->IK=IK;
  iwAID->X1=X1;
  iwAID->X2=X2;

  cdouble iwAI, Error;

  double Lower=0.0;
  double Upper=1.0;
  adapt_integrate(2, iwaIntegrand, (void *)iwAID, 1, 
                  &Lower, &Upper,
		  0, RELTOL*fabs(RefVal), RELTOL,
		  (double *)&iwAI, (double *)&Error);

  return iwAI;
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void GetPortVoltages(RWGGeometry *G, HVector *KN,
                     RWGPort **Ports, int NumPorts, cdouble *PortCurrents,
                     cdouble Omega, cdouble *PortVoltages)
{
  cdouble IW = II*Omega;
  cdouble IK = IW;

  memset(PortVoltages,0,NumPorts*sizeof(cdouble)); 

  /***************************************************************/
  /* PanelCharges is a statically-maintained vector that stores  */
  /* the total charge on each panel in the geometry.             */
  /***************************************************************/
  static int NumPanels=0;
  static cdouble *PanelCharges=0;

  if (NumPanels!=G->TotalPanels)
   { NumPanels=G->TotalPanels;
     if (PanelCharges==0) free(PanelCharges);
     PanelCharges=(cdouble *)mallocEC(NumPanels * sizeof(cdouble));
   };

  /***************************************************************/
  /* do a first pass through all interior edges and all driven   */
  /* ports to compute the total charge on each panel             */
  /***************************************************************/
  memset(PanelCharges, 0, NumPanels*sizeof(cdouble));

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
cdouble TotalVP, TotalVM;
static cdouble *PortPanelCharges=0;
if (PortPanelCharges==0)
 PortPanelCharges=(cdouble *)malloc(NumPanels * sizeof(cdouble));
memset(PortPanelCharges, 0, NumPanels*sizeof(cdouble));
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

  /*--------------------------------------------------------------*/
  /*- contributions of interior edges  ---------------------------*/
  /*--------------------------------------------------------------*/
  int BFIndex, no, ne; 
  RWGObject *O;
  RWGEdge *E;
  int PIOffset;

  for(BFIndex=0, no=0; no<G->NumObjects; no++)
   { 
     O=G->Objects[no];
     PIOffset=G->PanelIndexOffset[no];

     for(ne=0; ne<O->NumEdges; ne++, BFIndex++)
      { 
        E=O->Edges[ne];
        PanelCharges[PIOffset + E->iPPanel] += KN->GetEntry(BFIndex) * E->Length  / IW;
        PanelCharges[PIOffset + E->iMPanel] -= KN->GetEntry(BFIndex) * E->Length  / IW;
      };

   };

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*! 20120701 !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
if (SkipInterior)
 memset(PanelCharges, 0, NumPanels*sizeof(cdouble));
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*! 20120701 !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

  /*--------------------------------------------------------------*/
  /*- contributions of driven ports ------------------------------*/
  /*--------------------------------------------------------------*/
  int nPort, nPanel;
  RWGPort *Port;
  cdouble PortCurrent;

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*! 20120701 !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
if (SkipExterior==0)
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
  for(nPort=0; nPort<NumPorts; nPort++)
   { 
     PortCurrent=PortCurrents[nPort];
     if (PortCurrent==0.0) continue;
     Port=Ports[nPort];

     O=Ports[nPort]->PObject;
     PIOffset=G->PanelIndexOffset[O->Index];
     for(nPanel=0; nPanel<Port->NumPEdges; nPanel++)
      PanelCharges[PIOffset + Port->PPanelIndices[nPanel]] 
       -= Port->PLengths[nPanel]*PortCurrent/(IW*Port->PPerimeter);

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
for(nPanel=0; nPanel<Port->NumPEdges; nPanel++)
 PortPanelCharges[PIOffset + Port->PPanelIndices[nPanel]]
  -= Port->PLengths[nPanel]*PortCurrent/(IW*Port->PPerimeter);
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

     O=Ports[nPort]->MObject;
     PIOffset=G->PanelIndexOffset[O->Index];
     for(nPanel=0; nPanel<Port->NumMEdges; nPanel++)
      PanelCharges[PIOffset + Port->MPanelIndices[nPanel]]
       += Port->MLengths[nPanel]*PortCurrent/(IW*Port->MPerimeter);

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
for(nPanel=0; nPanel<Port->NumMEdges; nPanel++)
 PortPanelCharges[PIOffset + Port->MPanelIndices[nPanel]] 
   += Port->MLengths[nPanel]*PortCurrent/(IW*Port->MPerimeter);
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
   };

  /***************************************************************/
  /* ok, now that we know the charge on all panels, go through   */
  /* and compute the contribution of each panel to the potential */
  /* difference between the positive and negative sides of each  */
  /* port.                                                       */
  /***************************************************************/
  memset(PortVoltages, 0, NumPorts*sizeof(cdouble) );
  cdouble VP, VM;
TotalVP=TotalVM=0.0;
  Log("   scalar potential contribution");
  for(no=0; no<G->NumObjects; no++)
   for(O=G->Objects[no], nPanel=0; nPanel<O->NumPanels; nPanel++)
    for(nPort=0; nPort<NumPorts; nPort++)
     {
       Port=Ports[nPort];
       VP = GetPanelPotential(O, nPanel, IK, Port->PRefPoint);
       VM = GetPanelPotential(O, nPanel, IK, Port->MRefPoint);
       PortVoltages[nPort] += PanelCharges[G->PanelIndexOffset[no] + nPanel] * (VP - VM);
TotalVP+=PanelCharges[G->PanelIndexOffset[no] + nPanel]*VP;
TotalVM+=PanelCharges[G->PanelIndexOffset[no] + nPanel]*VM;
     };

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
#if 0
{ int no, nop, np, npp;
  RWGObject *O, *OP;
FILE *f=fopen("doom.out","a");
cdouble V;
double *X;
for(no=0; no<G->NumObjects; no++)
 for(O=G->Objects[no], np=0; np<O->NumPanels; np++)
  { 
    X=O->Panels[np]->Centroid;

    for(V=0.0, nop=0; nop<G->NumObjects; nop++)
     for(OP=G->Objects[nop], npp=0; npp<OP->NumPanels; npp++)
      V+=PanelCharges[G->PanelIndexOffset[nop] + npp]*GetPanelPotential(OP, npp, IK, X);

    fprintf(f,"%e %e %e %e %e \n",X[0],X[1],X[2],real(V),imag(V));
    if ( np+1 == O->NumPanels)  
     fprintf(f,"\n\n");
  };
fclose(f);
}
#endif 

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

  /***************************************************************/
  /* next get the contribution to the port voltage from the line */
  /* integral of iwA                                             */
  /***************************************************************/
  cdouble iwAI;
  Log("   vector potential contribution");
  for(nPort=0; nPort<NumPorts; nPort++)
   { iwAI=iwAIntegral(G, KN, Ports, NumPorts, PortCurrents, IK, 
                      Ports[nPort]->PRefPoint, Ports[nPort]->MRefPoint,
                      abs(PortVoltages[nPort]));
     PortVoltages[nPort] += iwAI;
   }; 

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
if (BreakoutFile)
 fprintf(BreakoutFile,"%s %e %e %e %e %e %e %e %e\n",
         z2s(Omega/FREQ2OMEGA), 
         real(TotalVP),imag(TotalVP),
         real(TotalVM),imag(TotalVM),
         real(iwAI),imag(iwAI),
         real(TotalVP-TotalVM+iwAI),imag(TotalVP-TotalVM+iwAI));
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/


}