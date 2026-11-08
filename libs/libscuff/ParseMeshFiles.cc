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
 * ReadGMSHFile.cc -- subroutine of the RWGSurface class constructor
 *
 * homer reid    -- 3/2007 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <algorithm>
#include <vector>

#include "libscuff.h"

namespace scuff {

/*************************************************************/
/* constants needed in this file only  ***********************/
/*************************************************************/
#define TYPE_TRIANGLE 2
#define TYPE_POINT    15

#define MESH_FORMAT_START_KEYWORD_GMSH24 "$MeshFormat"
#define MESH_FORMAT_END_KEYWORD_GMSH24 "$EndMeshFormat"

#define NODE_START_KEYWORD_GMSH1 "$NOD"
#define NODE_START_KEYWORD_GMSH24 "$Nodes"

#define NODE_END_KEYWORD_GMSH1   "$ENDNOD"
#define NODE_END_KEYWORD_GMSH24   "$EndNodes"

#define ELM_START_KEYWORD_GMSH1  "$ELM"
#define ELM_START_KEYWORD_GMSH24  "$Elements"

#define ELM_END_KEYWORD_GMSH1    "$ENDELM"
#define ELM_END_KEYWORD_GMSH24    "$EndElements"

#define FORMAT_GMSH1 1
#define FORMAT_GMSH2 2
#define FORMAT_GMSH4 4

#define MAXREFPTS 100

// vertices whose pairwise separation is less than WELD_RELTOL times the
// diagonal of the mesh bounding box are considered equivalent and welded
// together. the tolerance is deliberately relative to the mesh size: a
// hard-coded absolute tolerance (the historical behavior, 1.0e-6 in
// mesh-file length units) silently welded essentially all vertices of
// any mesh whose coordinates are not written in scuff-em's default
// length unit of microns.
#define WELD_RELTOL 1.0e-6

/*************************************************************/
/* Read vertices and panels from a GMSH .msh file to specify */
/* a surface.                                                */
/* If MeshTag==-1, then all panels are read.                 */
/* Otherwise, only panels on the specified physical region   */
/* are read.                                                 */
/*************************************************************/
char *ParseGMSHFile(FILE *MeshFile, char *FileName, int MeshTag,
                    dVec &VertexCoordinates, iVec &PanelVertexIndices)
{
  /*------------------------------------------------------------*/
  /* Read lines until we hit the keyword indicating the start   */
  /* of the 'nodes' section, then read the number of nodes.     */
  /*------------------------------------------------------------*/
  int LineNum=0;
  char Line[100];
  bool NodesKeywordFound=false;
  int WhichMeshFormat = 0;

  { // Read MSH preambule and determine version. The format checking is not bullet-proof
    // (some malformed files can pass through), but it should parse are correct files
    // correctly.
    double MeshFormatVersion = 0; int file_type, data_size;// for MSH version 2 and higher
    int VersionInfoPhase = 0;
    while(VersionInfoPhase < 5) {
     if (!fgets(Line,100,MeshFile))
        return vstrdup("%s:%i: failed to find valid MSH file version specification", FileName, LineNum);
      LineNum++;
      if (VersionInfoPhase == 0) {
        if( !strncmp(Line,NODE_START_KEYWORD_GMSH1,strlen(NODE_START_KEYWORD_GMSH1))) {
  	  WhichMeshFormat=FORMAT_GMSH1; 
          NodesKeywordFound=true;
          VersionInfoPhase = 5; 
          break; 
        } else if (!strncmp(Line,MESH_FORMAT_START_KEYWORD_GMSH24,strlen(MESH_FORMAT_START_KEYWORD_GMSH24)))
          ++VersionInfoPhase;
	else return vstrdup("%s:%i: expected $MeshFormat or $NOD keyword", FileName, LineNum);
      }
      if (VersionInfoPhase == 1) {
        if (1 == fscanf(MeshFile, "%lf", &MeshFormatVersion)) {
          WhichMeshFormat = (int) MeshFormatVersion;
	  if (WhichMeshFormat != FORMAT_GMSH2) 
            return vstrdup("%s:%i: only MSH v1 and v2 formats currently supported (input file claims to be v%g)", FileName, LineNum, MeshFormatVersion);
	  ++VersionInfoPhase;
	}
      }
      if (VersionInfoPhase == 2) {
        if (1 == fscanf(MeshFile, "%d", &file_type)) ++VersionInfoPhase;
      }
      if (VersionInfoPhase == 3) {
        if (1 == fscanf(MeshFile, "%d", &data_size)) ++VersionInfoPhase;
      }
      if (VersionInfoPhase == 4) {
	if (!strncmp(Line,MESH_FORMAT_END_KEYWORD_GMSH24,strlen(MESH_FORMAT_END_KEYWORD_GMSH24)))
          ++VersionInfoPhase;
      }
    }
  }
  assert(WhichMeshFormat != 0);

  while(!NodesKeywordFound)
   { if (!fgets(Line,100,MeshFile))
      return vstrdup("%s:%i: failed to find node start keyword", FileName, LineNum);
     LineNum++;
     if( !strncmp(Line,NODE_START_KEYWORD_GMSH24,strlen(NODE_START_KEYWORD_GMSH24)))
       NodesKeywordFound=true; 
   }
  int NumNodes;
  if ( !fgets(Line,100,MeshFile) || !(NumNodes=atoi(Line)) )
   return vstrdup("%s:%i: invalid number of nodes",FileName,LineNum);

  /*------------------------------------------------------------*/
  /*- Read in the vertices (which GMSH calls 'nodes.')          */
  /*- Note that the numbering of the vertices in GMSH does not  */
  /*- necessarily correspond to their ordering in the mesh      */
  /*- file. To remedy this situation, we construct a mapping    */
  /*- between GMSH's vertices indices and our internal vertex   */
  /*- indices, which works like this: The vertex that GMSH      */
  /*- calls 'node 3' is stored in slot GMSH2HR[3] within our    */
  /*- internal Vertices array.                                  */ 
  /*------------------------------------------------------------*/
  VertexCoordinates.clear();
  int *GMSH2HR=(int *)mallocEC(2*NumNodes*sizeof(int));
  for(int i=0; i<2*NumNodes; i++)
   GMSH2HR[i]=-1;
  for (int nn=0; nn<NumNodes; nn++)
   { if (!fgets(Line,100,MeshFile))
      return vstrdup("%s:%i: too few nodes",FileName,LineNum);
     LineNum++;
     int NodeIndex;
     double V[3];
     int nConv=sscanf(Line,"%i %le %le %le",&NodeIndex,V+0,V+1,V+2);
     if(nConv!=4)
      return vstrdup("%s:%i: invalid node specification",FileName,LineNum); 
     if (NodeIndex>2*NumNodes)
      return vstrdup("%s:%i: internal error in ReadGMSHFile",FileName,LineNum);
     VertexCoordinates.push_back(V[0]);
     VertexCoordinates.push_back(V[1]);
     VertexCoordinates.push_back(V[2]);
     GMSH2HR[NodeIndex]=nn;
   } /* for (nv=0; nv<NumVertices; nv++) */
   
  /*------------------------------------------------------------*/
  /*- 20151119 -------------------------------------------------*/
  /*------------------------------------------------------------*/
  char *s=getenv("SCUFF_PIXEL_SIZE");
  if (s)
   { double PixelSize;
     if (1!=sscanf(s,"%le",&PixelSize))
      Log("Invalid specification for SCUFF_PIXEL_SIZE (ignoring)");
     else
      Log("Rounding all vertex coordinates to be an integer multiple of %e", PixelSize);      
     for(int nn=0; nn<3*NumNodes; nn++)
      VertexCoordinates[nn] = PixelSize*round(VertexCoordinates[nn]/PixelSize);
   }
 
  /*------------------------------------------------------------*/
  /*- Eliminate any redundant vertices from the vertex list.   -*/
  /*- Vertices closer than WELD_RELTOL times the bounding-box   */
  /*- diagonal are welded together (all references to node j    */
  /*- remapped to the lowest-indexed node in its weld cluster). */
  /*- The weld tolerance being mesh-scale-relative (as opposed  */
  /*- to a hard-coded absolute value in mesh-file length units) */
  /*- is what keeps meshes written in length units other than   */
  /*- scuff-em's default (microns) from being silently destroyed.*/
  /*------------------------------------------------------------*/
  int NumRedundantNodes=0;
  if (NumNodes>1)
   {
     /*------------------------------------------------------------*/
     /*- the diagonal of the vertex bounding box sets the length   */
     /*- scale of the mesh as written in the mesh file             */
     /*------------------------------------------------------------*/
     double *Vertices = &(VertexCoordinates[0]);
     double MinCorner[3], MaxCorner[3];
     for(int nc=0; nc<3; nc++)
      MinCorner[nc]=MaxCorner[nc]=Vertices[nc];
     for(int nn=1; nn<NumNodes; nn++)
      for(int nc=0; nc<3; nc++)
       { MinCorner[nc]=std::min(MinCorner[nc], Vertices[3*nn+nc]);
         MaxCorner[nc]=std::max(MaxCorner[nc], Vertices[3*nn+nc]);
       }
     double MeshDiagonal
      = sqrt(   (MaxCorner[0]-MinCorner[0])*(MaxCorner[0]-MinCorner[0])
              + (MaxCorner[1]-MinCorner[1])*(MaxCorner[1]-MinCorner[1])
              + (MaxCorner[2]-MinCorner[2])*(MaxCorner[2]-MinCorner[2]) );
     double WeldTol = WELD_RELTOL*MeshDiagonal;

     /*------------------------------------------------------------*/
     /*- union-find structure for bookkeeping of weld clusters    -*/
     /*------------------------------------------------------------*/
     std::vector<int> NodeParent(NumNodes);
     for(int nn=0; nn<NumNodes; nn++) NodeParent[nn]=nn;
     auto FindNode=[&NodeParent](int nn)
      { while(NodeParent[nn]!=nn)
         { NodeParent[nn]=NodeParent[NodeParent[nn]]; // path halving
           nn=NodeParent[nn];
         }
        return nn;
      };

     /*------------------------------------------------------------*/
     /*- weld all pairs of vertices closer than the weld tolerance */
     /*- (only pairs whose x-coordinates differ by less than the   */
     /*- weld tolerance can be close enough, which prunes the      */
     /*- pairwise comparison to a tiny fraction of the N^2 pairs)  */
     /*------------------------------------------------------------*/
     std::vector<int> Order(NumNodes);
     for(int nn=0; nn<NumNodes; nn++) Order[nn]=nn;
     std::sort(Order.begin(), Order.end(), [Vertices](int na, int nb)
      { return Vertices[3*na] < Vertices[3*nb]; });
     for(int ii=0; ii<NumNodes; ii++)
      { int na=Order[ii];
        for(int jj=ii+1; jj<NumNodes; jj++)
         { int nb=Order[jj];
           if ( Vertices[3*nb] - Vertices[3*na] >= WeldTol ) break;
           if ( VecDistance(Vertices+3*na, Vertices+3*nb) < WeldTol )
            { int RootA=FindNode(na), RootB=FindNode(nb);
              if (RootA!=RootB)
               { // weld: the lower-indexed node survives
                 if (RootA<RootB) NodeParent[RootB]=RootA;
                 else             NodeParent[RootA]=RootB;
                 NumRedundantNodes++;
               }
             }
          }
       }

     /*------------------------------------------------------------*/
     /*- remap all vertex references to the surviving node of      */
     /*- each weld cluster                                         */
     /*------------------------------------------------------------*/
     if (NumRedundantNodes>0)
      for(int jGMSH=0; jGMSH<2*NumNodes; jGMSH++)
       GMSH2HR[jGMSH]=FindNode(GMSH2HR[jGMSH]);
   }

  if (NumRedundantNodes>0)
   Log("Welded %i redundant vertices in mesh file %s",NumRedundantNodes,FileName);
  if (NumRedundantNodes > NumNodes/100)
   Warn("%i of %i vertices welded in mesh file %s; the mesh coordinates are "
        "probably not in scuff-em's default length unit (microns). Check "
        "your mesh!",NumRedundantNodes,NumNodes,FileName);
 
  /*------------------------------------------------------------*/
  /* Confirm that the next two lines in the file are the        */
  /* end-of-nodes-section keyword and the start-of-elements-section */
  /* keyword, then read the number of elements.                 */
  /*------------------------------------------------------------*/
  if ( !fgets(Line,100,MeshFile) )
   return vstrdup("%s: bad file format (nodes section not terminated)",FileName);
  LineNum++;

  if ( WhichMeshFormat==FORMAT_GMSH1 ) 
   { if ( strncmp(Line,NODE_END_KEYWORD_GMSH1,strlen(NODE_END_KEYWORD_GMSH1)))
      return vstrdup("%s:%i: unexpected keyword",FileName,LineNum);
   }
  else
   { if ( strncmp(Line,NODE_END_KEYWORD_GMSH24,strlen(NODE_END_KEYWORD_GMSH24)))
      return vstrdup("%s:%i: unexpected keyword",FileName,LineNum);
   }

  if ( !fgets(Line,100,MeshFile) )
   return vstrdup("%s: bad file format (elements section not initiated)",FileName);
  LineNum++;

  if ( WhichMeshFormat==FORMAT_GMSH1 ) 
   { if ( strncmp(Line,ELM_START_KEYWORD_GMSH1,strlen(ELM_START_KEYWORD_GMSH1)))
      return vstrdup("%s:%i: unexpected keyword",FileName,LineNum);
   }
  else
   { if ( strncmp(Line,ELM_START_KEYWORD_GMSH24,strlen(ELM_START_KEYWORD_GMSH24)))
      return vstrdup("%s:%i: unexpected keyword",FileName,LineNum);
   }

  if ( !fgets(Line,100,MeshFile) )
   return vstrdup("%s: bad file format (invalid number of elements)",FileName);
  LineNum++;
  int NumElements;
  if ( 1!=sscanf(Line,"%i",&NumElements) || NumElements<0 ) 
   return vstrdup("%s:%i: invalid number of elements",FileName,LineNum);
 
  /*------------------------------------------------------------*/
  /*- Now read each line of the elements section.               */ 
  /*------------------------------------------------------------*/
  PanelVertexIndices.clear();
  for (int ne=0; ne<NumElements; ne++)
   { 
     if (!fgets(Line,100,MeshFile))
      return vstrdup("too few elements in input file");
     LineNum++;

     int VI[3], ElType, RegPhys;
     if (WhichMeshFormat==FORMAT_GMSH1)
      { int ElNum, RegElem, NodeCnt;
        int nConv=sscanf(Line,"%i %i %i %i %i %i %i %i", &ElNum,&ElType,&RegPhys,&RegElem,&NodeCnt,VI,VI+1,VI+2);
        if (nConv<5) return vstrdup("%s:%i: invalid element specification",FileName,LineNum);
      }
     else
      { int ElNum, nTags, nRead;
        int nConv=sscanf(Line,"%i %i %i%n",&ElNum,&ElType,&nTags,&nRead);
        int bufPos=nRead;
        if (nConv<3) return vstrdup("%s:%i: invalid element specification",FileName,LineNum);

        // read the first 'tag,' which should be the physical region
        if (nTags==0)
         RegPhys=0;
        else 
         sscanf(Line+bufPos,"%i%n",&RegPhys,&nRead);
        bufPos+=nRead;

        // read any remaining tags 
        int iDummy;
        for(int nt=0; nt<nTags-1; nt++)
         { sscanf(Line+bufPos,"%i%n",&iDummy,&nRead);
           bufPos+=nRead;
         }

        // finally, read the vertex indices. 
        nConv=sscanf(Line+bufPos,"%i %i %i",VI,VI+1,VI+2);

        if (ElType==TYPE_TRIANGLE && nConv!=3)
         return vstrdup("%s:%i: invalid element specification",FileName,LineNum);
        else if (ElType==TYPE_POINT && nConv!=1)
         return vstrdup("%s:%i: invalid element specification",FileName,LineNum);
      }
   
     /* we only process elements that are triangles */
     if( ElType==TYPE_TRIANGLE && ( (MeshTag == -1) || (MeshTag==RegPhys) ) )
      { PanelVertexIndices.push_back(GMSH2HR[VI[0]]);
        PanelVertexIndices.push_back(GMSH2HR[VI[1]]);
        PanelVertexIndices.push_back(GMSH2HR[VI[2]]);
      }
   } //for (ne=0; ne<NumElements; ne++)

  free(GMSH2HR);
  fclose(MeshFile);
  return nullptr;
} 

#define MAXSTR 1000

/***************************************************************/
/* Given an open file f, read lines until one is found that    */
/* contains the given search string. Returns the number of     */
/* lines read or 0 if the file ended before the string was     */
/* found. If the return value is nonzero, Line contains the    */
/* last line read from the file.                               */
/***************************************************************/
static int SkipTo(FILE *f, const char *SearchString, char *Line)
{ 
  int Lines=0;
  while( fgets(Line,MAXSTR,f) )
   { Lines++;
     if (strstr(Line,SearchString))
      return Lines;
   }
  return 0;
}

/***************************************************************/
/* Constructor helper function for reading in nodes and  *******/
/* elements for a .mphtxt file as produced by COMSOL     *******/
/***************************************************************/
char *ParseComsolFile(FILE *MeshFile, char *FileName, int MeshTag, 
                      dVec &VertexCoordinates, iVec &PanelVertexIndices)
{ 
  if (MeshTag!=1) 
   Warn("mesh tagging not supported for COMSOL files (ignoring mesh tag %i)",MeshTag); 
 
  /***************************************************************/
  /* skip down to node definition section                        */
  /***************************************************************/
  char Line[MAXSTR];
  int LineNum=0;
  int LinesRead=SkipTo(MeshFile,"# number of mesh points",Line);
  if (LinesRead==0)
   return vstrdup("%s: failed to find line '#number of mesh points'",FileName);
  LineNum+=LinesRead;

  int NumVertices;
  if ( 1!=sscanf(Line,"%i ",&NumVertices) || NumVertices<0) 
   return vstrdup("%s:%i: invalid number of vertices (%i)",FileName,LineNum,NumVertices);

  LinesRead=SkipTo(MeshFile,"# Mesh point coordinates",Line);
  if (LinesRead==0)
   return vstrdup("%s: failed to find line '#Mesh point coordinates'",FileName);
  LineNum+=LinesRead;

  /***************************************************************/
  /* read vertices ***********************************************/
  /***************************************************************/
  VertexCoordinates.clear();
  for(int nv=0; nv<NumVertices; nv++)
   { 
     if ( !fgets(Line,MAXSTR,MeshFile) )
      return vstrdup("%s: unexpected end of file",FileName);
     LineNum++;

     double V[3];
     if ( 3!=sscanf(Line,"%le %le %le",V+0, V+1, V+2) )
      return vstrdup("%s:%i: syntax error",FileName,LineNum);
     VertexCoordinates.push_back(V[0]);
     VertexCoordinates.push_back(V[1]);
     VertexCoordinates.push_back(V[2]);
   }
  if (VertexCoordinates.size() != 3*((size_t)NumVertices) )
   return vstrdup("%s: stated number of vertices (%i) disagrees with actual (%zu)",FileName,NumVertices,VertexCoordinates.size()/3);

  /***************************************************************/
  /* skip down to element definition section *********************/
  /***************************************************************/
  LinesRead=SkipTo(MeshFile,"3 # number of nodes per element",Line);
  if (LinesRead==0)
   return vstrdup("%s: failed to find line '3 #number of nodes per element'", FileName);
  LineNum+=LinesRead;

  if ( !fgets(Line,MAXSTR,MeshFile) )
   return vstrdup("%s: unexpected end of file",FileName);
  LineNum++;
  int NumPanels;
  int nConv=sscanf(Line,"%i",&NumPanels);
  if (nConv!=1 || !strstr(Line,"# number of elements"))
   return vstrdup("%s:%i: syntax error",FileName,LineNum);

  if ( !fgets(Line,MAXSTR,MeshFile) )
   return vstrdup("%s: unexpected end of file",FileName);
  LineNum++;
  if ( !strstr(Line,"# Elements") )
   return vstrdup("%s:%i: syntax error",FileName,LineNum);

  /***************************************************************/
  /* read panels    **********************************************/ 
  /***************************************************************/
  PanelVertexIndices.clear();
  for(int np=0; np<NumPanels; np++)
   { 
     if ( !fgets(Line,MAXSTR,MeshFile) )
      return vstrdup("%s: unexpected end of file",FileName);
     LineNum++;

     int VI[3];
     if ( 3!=sscanf(Line,"%i %i %i",VI+0,VI+1,VI+2) )
      return vstrdup("%s:%i: syntax error",FileName,LineNum); 
 
     PanelVertexIndices.push_back(VI[0]);
     PanelVertexIndices.push_back(VI[1]);
     PanelVertexIndices.push_back(VI[2]);
   }
  if (PanelVertexIndices.size() != 3*((size_t)NumPanels) )
   return vstrdup("%s: stated number of panels (%i) disagrees with actual (%zu)",FileName,NumPanels,PanelVertexIndices.size()/3);

  /***************************************************************/
  /* ignore the rest of the file and we are done *****************/
  /***************************************************************/
  fclose(MeshFile);
  return nullptr;
} 

} // namespace scuff
