/*
 * Animation plugin for compiz/beryl
 *
 * animation.c
 *
 * Copyright : (C) 2006 Erkin Bahceci
 * E-mail    : erkinbah@gmail.com
 *
 * Based on Wobbly and Minimize plugins by
 *           : David Reveman
 * E-mail    : davidr@novell.com>
 *
 * Particle system added by : (C) 2006 Dennis Kasprzyk
 * E-mail                   : onestone@beryl-project.org
 *
 * Beam-Up added by : Florencio Guimaraes
 * E-mail           : florencio@nexcorp.com.br
 *
 * Hexagon tessellator added by : Mike Slegeir
 * E-mail                       : mikeslegeir@mail.utexas.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "animation-internal.h"

static Bool ensureLargerClipCapacity(PolygonSet * pset)
{
    if (pset->clipCapacity == pset->nClips)	// if list full
    {
	Clip4Polygons *newList = realloc
	    (pset->clips, sizeof(Clip4Polygons) *
	     (pset->clipCapacity + ANIM_CLIP_LIST_INCREMENT));
	if (!newList)
	    return FALSE;
	// reset newly allocated part of this memory to 0
	memset(newList + pset->clipCapacity,
	       0, sizeof(Clip4Polygons) * ANIM_CLIP_LIST_INCREMENT);

	int *newList2 = realloc
	    (pset->lastClipInGroup, sizeof(int) *
	     (pset->clipCapacity + ANIM_CLIP_LIST_INCREMENT));
	if (!newList2)
	{
	    free(newList);
	    pset->clips = 0;
	    pset->lastClipInGroup = 0;
	    return FALSE;
	}
	// reset newly allocated part of this memory to 0
	memset(newList2 + pset->clipCapacity,
	       0, sizeof(int) * ANIM_CLIP_LIST_INCREMENT);

	pset->clips = newList;
	pset->clipCapacity += ANIM_CLIP_LIST_INCREMENT;
	pset->lastClipInGroup = newList2;
    }
    return TRUE;
}

// Frees up polygon objects in pset
static void freePolygonObjects(PolygonSet * pset)
{
    PolygonObject *p = pset->polygons;

    if (!p)
    {
	pset->nPolygons = 0;
	return;
    }
    int i;

    for (i = 0; i < pset->nPolygons; i++, p++)
    {
	if (p->nVertices > 0)
	{
	    if (p->vertices)
		free(p->vertices);
	    if (p->sideIndices)
		free(p->sideIndices);
	    if (p->normals)
		free(p->normals);
	    p->vertices = 0;
	    p->sideIndices = 0;
	    p->normals = 0;
	    p->nVertices = 0;
	}
	/*if (p->nShadowQuads > 0)
	  {
	  if (p->shadowVertices)
	  free(p->shadowVertices);
	  if (p->shadowTexCoords)
	  free(p->shadowTexCoords);
	  p->shadowVertices = 0;
	  p->shadowTexCoords = 0;
	  p->nShadowQuads = 0;
	  } */
	p->nSides = 0;
    }
    free(pset->polygons);
    pset->polygons = 0;
    pset->nPolygons = 0;
}

// Frees up intersecting polygon info of PolygonSet clips
static void freeClipsPolygons(PolygonSet * pset)
{
    int k;

    for (k = 0; k < pset->clipCapacity; k++)
    {
	if (pset->clips[k].intersectingPolygons)
	{
	    free(pset->clips[k].intersectingPolygons);
	    pset->clips[k].intersectingPolygons = 0;
	}
	if (pset->clips[k].polygonVertexTexCoords)
	{
	    free(pset->clips[k].polygonVertexTexCoords);
	    pset->clips[k].polygonVertexTexCoords = 0;
	}
	pset->clips[k].nIntersectingPolygons = 0;
    }
}

// Frees up the whole polygon set
void freePolygonSet(AnimWindow * aw)
{
    PolygonSet *pset = aw->polygonSet;

    freePolygonObjects(pset);
    if (pset->clipCapacity > 0)
    {
	freeClipsPolygons(pset);
	free(pset->clips);
	pset->clips = 0;
	pset->nClips = 0;
	pset->firstNondrawnClip = 0;
	pset->clipCapacity = 0;
    }
    free(pset);
    aw->polygonSet = 0;
}

/*
// Tessellates window into extruded rectangular objects
// in a brick wall formation
static Bool
tessellateIntoBricks(CompWindow * w,
int gridSizeX, int gridSizeY, float thickness)
{
}

// Tessellates window into Voronoi segments
static Bool
tessellateVoronoi(CompWindow * w,
float thickness)
{
}
*/

// Tessellates window into extruded rectangular objects
Bool
tessellateIntoRectangles(CompWindow * w,
			 int gridSizeX, int gridSizeY, float thickness)
{
    ANIM_WINDOW(w);

    PolygonSet *pset = aw->polygonSet;

    if (!pset)
	return FALSE;

    int winLimitsX;				// boundaries of polygon tessellation
    int winLimitsY;
    int winLimitsW;
    int winLimitsH;

    if (pset->includeShadows)
    {
	winLimitsX = WIN_X(w);
	winLimitsY = WIN_Y(w);
	winLimitsW = WIN_W(w) - 1; // avoid artifact on right edge
	winLimitsH = WIN_H(w);
    }
    else
    {
	winLimitsX = BORDER_X(w);
	winLimitsY = BORDER_Y(w);
	winLimitsW = BORDER_W(w);
	winLimitsH = BORDER_H(w);
    }
    float minRectSize = MIN_WINDOW_GRID_SIZE;
    float rectW = winLimitsW / (float)gridSizeX;
    float rectH = winLimitsH / (float)gridSizeY;

    if (rectW < minRectSize)
	gridSizeX = winLimitsW / minRectSize;	// int div.
    if (rectH < minRectSize)
	gridSizeY = winLimitsH / minRectSize;	// int div.

    if (pset->nPolygons != gridSizeX * gridSizeY)
    {
	if (pset->nPolygons > 0)
	    freePolygonObjects(pset);

	pset->nPolygons = gridSizeX * gridSizeY;

	pset->polygons = calloc(pset->nPolygons, sizeof(PolygonObject));
	if (!pset->polygons)
	{
	    compLogMessage (w->screen->display, "animation",
			    CompLogLevelError, "Not enough memory");
	    pset->nPolygons = 0;
	    return FALSE;
	}
    }

    thickness /= w->screen->width;
    pset->thickness = thickness;
    pset->nTotalFrontVertices = 0;

    float cellW = (float)winLimitsW / gridSizeX;
    float cellH = (float)winLimitsH / gridSizeY;
    float halfW = cellW / 2;
    float halfH = cellH / 2;

    float halfThick = pset->thickness / 2;
    PolygonObject *p = pset->polygons;
    int x, y;

    //float vec = 1 / sqrt(3); // vector component for normals

    for (y = 0; y < gridSizeY; y++)
    {
	float posY = winLimitsY + cellH * (y + 0.5);

	for (x = 0; x < gridSizeX; x++, p++)
	{
	    p->centerPos.x = p->centerPosStart.x =
		winLimitsX + cellW * (x + 0.5);
	    p->centerPos.y = p->centerPosStart.y = posY;
	    p->centerPos.z = p->centerPosStart.z = -halfThick;
	    p->rotAngle = p->rotAngleStart = 0;

	    p->centerRelPos.x = (x + 0.5) / gridSizeX;
	    p->centerRelPos.y = (y + 0.5) / gridSizeY;

	    p->nSides = 4;
	    p->nVertices = 2 * 4;
	    pset->nTotalFrontVertices += 4;

	    // 4 front, 4 back vertices
	    if (!p->vertices)
	    {
		p->vertices = calloc(8 * 3, sizeof(GLfloat));
	    }
	    //if (!p->vertexOnEdge)
	    //  p->vertexOnEdge = calloc (1, sizeof (int) * p->nSides);
	    //p->vertexTexCoords4Clips = calloc (1, sizeof (GLfloat) * 4 * 2 * 2);
	    if (!p->vertices)	// || !p->vertexOnEdge)// || !p->vertexTexCoords4Clips)
	    {
		compLogMessage (w->screen->display, "animation",
				CompLogLevelError, "Not enough memory");
		freePolygonObjects(pset);
		return FALSE;
	    }

	    GLfloat *pv = p->vertices;

	    // Determine 4 front vertices in ccw direction
	    pv[0] = -halfW;
	    pv[1] = -halfH;
	    pv[2] = halfThick;

	    pv[3] = -halfW;
	    pv[4] = halfH;
	    pv[5] = halfThick;

	    pv[6] = halfW;
	    pv[7] = halfH;
	    pv[8] = halfThick;

	    pv[9] = halfW;
	    pv[10] = -halfH;
	    pv[11] = halfThick;

	    // Determine 4 back vertices in cw direction
	    pv[12] = halfW;
	    pv[13] = -halfH;
	    pv[14] = -halfThick;

	    pv[15] = halfW;
	    pv[16] = halfH;
	    pv[17] = -halfThick;

	    pv[18] = -halfW;
	    pv[19] = halfH;
	    pv[20] = -halfThick;

	    pv[21] = -halfW;
	    pv[22] = -halfH;
	    pv[23] = -halfThick;

	    // 16 indices for 4 sides (for quad strip)
	    if (!p->sideIndices)
	    {
		p->sideIndices = calloc(4 * 4, sizeof(GLushort));
	    }
	    if (!p->sideIndices)
	    {
		compLogMessage (w->screen->display, "animation",
				CompLogLevelError, "Not enough memory");
		freePolygonObjects(pset);
		return FALSE;
	    }

	    GLushort *ind = p->sideIndices;

	    /*
	      ind[0] = 0;
	      ind[1] = 7;
	      ind[2] = 1;
	      ind[3] = 6;
	      ind[4] = 2;
	      ind[5] = 5;
	      ind[6] = 3;
	      ind[7] = 4;
	      ind[8] = 0;
	      ind[9] = 7;
	    */
	    int id = 0;

	    ind[id++] = 0;
	    ind[id++] = 7;
	    ind[id++] = 6;
	    ind[id++] = 1;

	    ind[id++] = 1;
	    ind[id++] = 6;
	    ind[id++] = 5;
	    ind[id++] = 2;

	    ind[id++] = 2;
	    ind[id++] = 5;
	    ind[id++] = 4;
	    ind[id++] = 3;

	    ind[id++] = 3;
	    ind[id++] = 4;
	    ind[id++] = 7;
	    ind[id++] = 0;

	    // Surface normals
	    if (!p->normals)
	    {
		p->normals = calloc((2 + 4) * 3, sizeof(GLfloat));
	    }
	    if (!p->normals)
	    {
		compLogMessage (w->screen->display, "animation",
				CompLogLevelError,
				"Not enough memory");
		freePolygonObjects(pset);
		return FALSE;
	    }

	    GLfloat *nor = p->normals;

	    // Front
	    nor[0] = 0;
	    nor[1] = 0;
	    nor[2] = -1;

	    // Back
	    nor[3] = 0;
	    nor[4] = 0;
	    nor[5] = 1;

	    // Sides
	    nor[6] = -1;
	    nor[7] = 0;
	    nor[8] = 0;

	    nor[9] = 0;
	    nor[10] = 1;
	    nor[11] = 0;

	    nor[12] = 1;
	    nor[13] = 0;
	    nor[14] = 0;

	    nor[15] = 0;
	    nor[16] = -1;
	    nor[17] = 0;

	    // Determine bounding box (to test intersection with clips)
	    p->boundingBox.x1 = -halfW + p->centerPos.x;
	    p->boundingBox.y1 = -halfH + p->centerPos.y;
	    p->boundingBox.x2 = ceil(halfW + p->centerPos.x);
	    p->boundingBox.y2 = ceil(halfH + p->centerPos.y);
	    /*
	    // Determine edge/corner status
	    if (x == 0)
	    {
	    p->nShadowQuads++;
	    if (y == 0)
	    p->vertexOnEdge[0] = 5;
	    if (y == gridSizeY - 1)
	    p->nShadowQuads++;
	    }
	    if (x == gridSizeX - 1)
	    {
	    p->nShadowQuads++;
	    if (y == 0)
	    p->nShadowQuads++;
	    if (y == gridSizeY - 1)
	    p->nShadowQuads++;
	    }
	    if (y == 0)
	    p->nShadowQuads++;
	    if (y == gridSizeY - 1)
	    p->nShadowQuads++; */
	}
    }
    return TRUE;
}

// Tessellates window into extruded hexagon objects
Bool
tessellateIntoHexagons(CompWindow * w,
		       int gridSizeX, int gridSizeY, float thickness)
{
    ANIM_WINDOW(w);

    PolygonSet *pset = aw->polygonSet;

    if (!pset)
	return FALSE;

    int winLimitsX;				// boundaries of polygon tessellation
    int winLimitsY;
    int winLimitsW;
    int winLimitsH;

    if (pset->includeShadows)
    {
	winLimitsX = WIN_X(w);
	winLimitsY = WIN_Y(w);
	winLimitsW = WIN_W(w) - 1; // avoid artifact on right edge
	winLimitsH = WIN_H(w);
    }
    else
    {
	winLimitsX = BORDER_X(w);
	winLimitsY = BORDER_Y(w);
	winLimitsW = BORDER_W(w);
	winLimitsH = BORDER_H(w);
    }
    float minSize = 20;
    float hexW = winLimitsW / (float)gridSizeX;
    float hexH = winLimitsH / (float)gridSizeY;

    if (hexW < minSize)
	gridSizeX = winLimitsW / minSize;	// int div.
    if (hexH < minSize)
	gridSizeY = winLimitsH / minSize;	// int div.

    int nPolygons = (gridSizeY + 1) * gridSizeX + (gridSizeY + 1) / 2;

    if (pset->nPolygons != nPolygons)
    {
	if (pset->nPolygons > 0)
	    freePolygonObjects(pset);

	pset->nPolygons = nPolygons;

	pset->polygons = calloc(pset->nPolygons, sizeof(PolygonObject));
	if (!pset->polygons)
	{
	    compLogMessage (w->screen->display, "animation", CompLogLevelError,
			    "Not enough memory");
	    pset->nPolygons = 0;
	    return FALSE;
	}
    }

    thickness /= w->screen->width;
    pset->thickness = thickness;
    pset->nTotalFrontVertices = 0;

    float cellW = (float)winLimitsW / gridSizeX;
    float cellH = (float)winLimitsH / gridSizeY;
    float halfW = cellW / 2;
    float twoThirdsH = 2*cellH / 3;
    float thirdH = cellH / 3;

    float halfThick = pset->thickness / 2;
    PolygonObject *p = pset->polygons;
    int x, y;

    for (y = 0; y < gridSizeY+1; y++)
    {
	float posY = winLimitsY + cellH * (y);
	int numPolysinRow = (y%2==0) ? gridSizeX : (gridSizeX + 1);
	// Clip polygons to the window dimensions
	float topY, topRightY, topLeftY, bottomY, bottomLeftY, bottomRightY;
	if(y == 0){
	    topY = topRightY = topLeftY = 0;
	    bottomY = twoThirdsH;
	    bottomLeftY = bottomRightY = thirdH;
	} else if(y == gridSizeY){
	    bottomY = bottomLeftY = bottomRightY = 0;
	    topY = -twoThirdsH;
	    topLeftY = topRightY = -thirdH;
	} else {
	    topY = -twoThirdsH;
	    topLeftY = topRightY = -thirdH;
	    bottomLeftY = bottomRightY = thirdH;
	    bottomY = twoThirdsH;
	}

	for (x = 0; x < numPolysinRow; x++, p++)
	{
	    // Clip odd rows when necessary
	    float topLeftX, topRightX, bottomLeftX, bottomRightX;
	    if(y%2 == 1){
		if(x == 0){
		    topLeftX = bottomLeftX = 0;
		    topRightX = halfW;
		    bottomRightX = halfW;
		} else if(x == numPolysinRow-1){
		    topRightX = bottomRightX = 0;
		    topLeftX = -halfW;
		    bottomLeftX = -halfW;
		} else {
		    topLeftX = bottomLeftX = -halfW;
		    topRightX = bottomRightX = halfW;
		}
	    } else {
		topLeftX = bottomLeftX = -halfW;
		topRightX = bottomRightX = halfW;
	    }
			
	    p->centerPos.x = p->centerPosStart.x =
		winLimitsX + cellW * (x + (y%2 ? 0.0 : 0.5));
	    p->centerPos.y = p->centerPosStart.y = posY;
	    p->centerPos.z = p->centerPosStart.z = -halfThick;
	    p->rotAngle = p->rotAngleStart = 0;

	    p->centerRelPos.x = (x + 0.5) / gridSizeX;
	    p->centerRelPos.y = (y + 0.5) / gridSizeY;

	    p->nSides = 6;
	    p->nVertices = 2 * 6;
	    pset->nTotalFrontVertices += 6;

	    // 6 front, 6 back vertices
	    if (!p->vertices)
	    {
		p->vertices = calloc(6 * 2 * 3, sizeof(GLfloat));
		if (!p->vertices)
		{
		    compLogMessage (w->screen->display, "animation", CompLogLevelError,
				    "Not enough memory");
		    freePolygonObjects(pset);
		    return FALSE;
		}
	    }

	    GLfloat *pv = p->vertices;

	    // Determine 6 front vertices in ccw direction
	    // Starting at top
	    pv[0] = 0;
	    pv[1] = topY;
	    pv[2] = halfThick;

	    pv[3] = topLeftX;
	    pv[4] = topLeftY;
	    pv[5] = halfThick;

	    pv[6] = bottomLeftX;
	    pv[7] = bottomLeftY;
	    pv[8] = halfThick;

	    pv[9] = 0;
	    pv[10] = bottomY;
	    pv[11] = halfThick;

	    pv[12] = bottomRightX;
	    pv[13] = bottomRightY;
	    pv[14] = halfThick;

	    pv[15] = topRightX;
	    pv[16] = topRightY;
	    pv[17] = halfThick;
			
	    // Determine 6 back vertices in cw direction
	    pv[18] = topRightX;
	    pv[19] = topRightY;
	    pv[20] = -halfThick;

	    pv[21] = bottomRightX;
	    pv[22] = bottomRightY;
	    pv[23] = -halfThick;
			
	    pv[24] = 0;
	    pv[25] = bottomY;
	    pv[26] = -halfThick;

	    pv[27] = bottomLeftX;
	    pv[28] = bottomLeftY;
	    pv[29] = -halfThick;
			
	    pv[30] = topLeftX;
	    pv[31] = topLeftY;
	    pv[32] = -halfThick;

	    pv[33] = 0;
	    pv[34] = topY;
	    pv[35] = -halfThick;

	    // 24 indices per 6 sides (for quad strip)
	    if (!p->sideIndices)
	    {
		p->sideIndices = calloc(4 * 6, sizeof(GLushort));
	    }
	    if (!p->sideIndices)
	    {
		compLogMessage (w->screen->display, "animation",
				CompLogLevelError, "Not enough memory");
		freePolygonObjects(pset);
		return FALSE;
	    }

	    GLushort *ind = p->sideIndices;

	    int id = 0;
	    // upper left side face
	    ind[id++] = 0;
	    ind[id++] = 11;
	    ind[id++] = 10;
	    ind[id++] = 1;
	    // left side face
	    ind[id++] = 1;
	    ind[id++] = 10;
	    ind[id++] = 9;
	    ind[id++] = 2;
	    // lower left side face
	    ind[id++] = 2;
	    ind[id++] = 9;
	    ind[id++] = 8;
	    ind[id++] = 3;
	    // lower right side face
	    ind[id++] = 3;
	    ind[id++] = 8;
	    ind[id++] = 7;
	    ind[id++] = 4;
	    // right side face
	    ind[id++] = 4;
	    ind[id++] = 7;
	    ind[id++] = 6;
	    ind[id++] = 5;
	    // upper right side face
	    ind[id++] = 5;
	    ind[id++] = 6;
	    ind[id++] = 11;
	    ind[id++] = 0;			

	    // Surface normals
	    if (!p->normals)
	    {
		p->normals = calloc((2 + 6) * 3, sizeof(GLfloat));
	    }
	    if (!p->normals)
	    {
		compLogMessage (w->screen->display, "animation",
				CompLogLevelError, "Not enough memory");
		freePolygonObjects(pset);
		return FALSE;
	    }

	    GLfloat *nor = p->normals;

	    // Front
	    nor[0] = 0;
	    nor[1] = 0;
	    nor[2] = -1;
			
	    // Back
	    nor[3] = 0;
	    nor[4] = 0;
	    nor[5] = 1;
			
	    // Sides
	    nor[6] = -1;
	    nor[7] = 1;
	    nor[8] = 0;
			
	    nor[9] = -1;
	    nor[10] = 0;
	    nor[11] = 0;
			
	    nor[12] = -1;
	    nor[13] = -1;
	    nor[14] = 0;

	    nor[15] = 1;
	    nor[16] = -1;
	    nor[17] = 0;

	    nor[18] = 1;
	    nor[19] = 0;
	    nor[20] = 0;

	    nor[21] = 1;
	    nor[22] = 1;
	    nor[23] = 0;			

	    // Determine bounding box (to test intersection with clips)
	    p->boundingBox.x1 = topLeftX + p->centerPos.x;
	    p->boundingBox.y1 = topY + p->centerPos.y;
	    p->boundingBox.x2 = ceil(bottomRightX + p->centerPos.x);
	    p->boundingBox.y2 = ceil(bottomY + p->centerPos.y);
	}
    }
    if (pset->nPolygons != p - pset->polygons)
	compLogMessage (w->screen->display, "animation", CompLogLevelError,
			"%s: Error in tessellateIntoHexagons at line %d!",
			__FILE__, __LINE__);
    return TRUE;

}

void
polygonsStoreClips(CompScreen * s, CompWindow * w,
		   int nClip, BoxPtr pClip, int nMatrix, CompMatrix * matrix)
{
    ANIM_WINDOW(w);

    PolygonSet *pset = aw->polygonSet;

    // if polygon set is not valid or effect is not 3D (glide w/thickness=0)
    if (!pset)
	return;

    // only draw windows on current viewport
    if (w->attrib.x > s->width || w->attrib.x + w->width < 0 ||
	w->attrib.y > s->height || w->attrib.y + w->height < 0 ||
	(aw->lastKnownCoords.x != NOT_INITIALIZED &&
	 (aw->lastKnownCoords.x != w->attrib.x ||
	  aw->lastKnownCoords.y != w->attrib.y)))
    {
	return;
	// since this is not the viewport the window was drawn
	// just before animation started
    }

    Bool dontStoreClips = TRUE;

    // If this clip doesn't match the corresponding stored clip,
    // clear the stored clips from this point (aw->nClipsPassed)
    // to the end and store the new ones instead.

    if (aw->nClipsPassed < pset->nClips) // if we have clips stored earlier
    {
	Clip4Polygons *c = pset->clips + aw->nClipsPassed;
	// the stored clip at position aw->nClipsPassed

	// if either clip coordinates or texture matrix is different
	if (memcmp(pClip, &c->box, sizeof(Box)) ||
	    memcmp(matrix, &c->texMatrix, sizeof(CompMatrix)))
	{
	    // get rid of the clips from here (aw->nClipsPassed) to the end
	    pset->nClips = aw->nClipsPassed;
	    dontStoreClips = FALSE;
	}
    }
    else
	dontStoreClips = FALSE;

    if (dontStoreClips)
    {
	aw->nClipsPassed += nClip;
	return;
    }
    // For each clip passed to this function
    for (; nClip--; pClip++, aw->nClipsPassed++)
    {
	// New clip

	if (!ensureLargerClipCapacity(pset))
	{
	    compLogMessage (s->display, "animation", CompLogLevelError,
			    "Not enough memory");
	    return;
	}

	Clip4Polygons *newClip = &pset->clips[pset->nClips];

	newClip->id = aw->nClipsPassed;
	memcpy(&newClip->box, pClip, sizeof(Box));
	memcpy(&newClip->texMatrix, matrix, sizeof(CompMatrix));
	// nMatrix is not used for now
	// (i.e. only first texture matrix is considered)

	// avoid clipping along window edge
	// for the "window contents" clip
	if (pClip->x1 == BORDER_X(w) &&
	    pClip->y1 == BORDER_Y(w) &&
	    pClip->x2 == BORDER_X(w) + BORDER_W(w) &&
	    pClip->y2 == BORDER_Y(w) + BORDER_H(w))
	{
	    newClip->boxf.x1 = pClip->x1 - 0.1f;
	    newClip->boxf.y1 = pClip->y1 - 0.1f;
	    newClip->boxf.x2 = pClip->x2 + 0.1f;
	    newClip->boxf.y2 = pClip->y2 + 0.1f;
	}
	else
	{
	    newClip->boxf.x1 = pClip->x1;
	    newClip->boxf.y1 = pClip->y1;
	    newClip->boxf.x2 = pClip->x2;
	    newClip->boxf.y2 = pClip->y2;
	}

	pset->nClips++;
	aw->clipsUpdated = TRUE;

	/*
	// Look for a container / contained clip to minimize # of clips.
	// Go backward, since such a clip is likely to be one of the last ones.
	// Go until you hit the clips corresponding to the previous texture.
	int i;
	for (i=pset->nClips-1; i>=pset->firstClipWithNoTex; i--)
	{
	Clip4Polygons *clip = &pset->clips[i];

	// if tex matrices are different
	if (clip->texMatrix.xx != matrix->xx ||
	clip->texMatrix.yy != matrix->yy ||
	clip->texMatrix.x0 != matrix->x0 ||
	clip->texMatrix.y0 != matrix->y0)
	continue;

	// if an old clip contains the new clip
	if (x1 >= clip->box.x1 && y1 >= clip->box.y1 &&
	x2 <= clip->box.x2 && y2 <= clip->box.y2)
	{
	newClipNo = i;
	updateCoords = FALSE;
	break;
	}
	// if the new clip contains an old clip
	if (x1 <= clip->box.x1 && y1 <= clip->box.y1 &&
	x2 >= clip->box.x2 && y2 >= clip->box.y2)
	{
	// update the old clip in this case
	newClipNo = i;
	break;
	}
	}
	if (pset->firstClipWithNoTex > 0)
	{
	// Go through clip's of earlier textures to look for
	// an identical clip. If such clips are found, their
	// texture should be updated with the latest one
	for (i=pset->firstClipWithNoTex-1; i>=0; i--)
	{
	Clip4Polygons *clip = &pset->clips[i];

	// if tex matrices are different
	if (clip->texMatrix.xx != matrix->xx ||
	clip->texMatrix.yy != matrix->yy ||
	clip->texMatrix.x0 != matrix->x0 ||
	clip->texMatrix.y0 != matrix->y0)
	continue;

	// if an old clip == the new clip
	if (x1 == clip->box.x1 && y1 == clip->box.y1 &&
	x2 == clip->box.x2 && y2 == clip->box.y2)
	{
	newClipNo = i;
	updateCoords = FALSE;
	clip->textureNo = aw->nTextures;
	break;
	}
	}
	}

	if (newClipNo == pset->nClips && // if no such old clip is found
	!ensureLargerClipCapacity (pset))
	{
	fprintf(stderr, "%s: Not enough memory at line %d!\n",
	__FILE__, __LINE__);
	return;
	}
	Clip4Polygons *newClip = &pset->clips[newClipNo];
	if (updateCoords)
	{
	memcpy (&newClip->box, pClip, sizeof (Box));
	memcpy (&newClip->texMatrix, matrix, sizeof (CompMatrix));
	// nMatrix is not used for now
	// (i.e. only first texture matrix is considered)
	}
	if (newClipNo == pset->nClips) // if new clip is being added
	{
	//printf("adding x1: %4d, y1: %4d, x2: %4d, y2: %4d\n",
	//       pClip->x1, pClip->y1, pClip->x2, pClip->y2);

	newClip->textureNo = aw->nTextures;
	pset->nClips++;
	} */
    }
}

// For each rectangular clip, this function finds polygons which
// have a bounding box that intersects the clip. For intersecting
// polygons, it computes the texture coordinates for the vertices
// of that polygon (to draw the clip texture).
static Bool processIntersectingPolygons(CompScreen * s, PolygonSet * pset)
{
    int j;

    for (j = pset->firstNondrawnClip; j < pset->nClips; j++)
    {
	Clip4Polygons *c = pset->clips + j;
	Box *cb = &c->box;
	int nFrontVerticesTilThisPoly = 0;

	c->nIntersectingPolygons = 0;

	// TODO: If it doesn't affect speed much, for each clip,
	// consider doing 2 passes, counting the intersecting polygons
	// in the 1st pass and allocating just enough space for those
	// polygons instead of all polygons in the 2nd pass.
	int i;

	for (i = 0; i < pset->nPolygons; i++)
	{
	    PolygonObject *p = pset->polygons + i;

	    Box *bb = &p->boundingBox;

	    if (bb->x2 <= cb->x1)
		continue;		// no intersection
	    if (bb->y2 <= cb->y1)
		continue;		// no intersection
	    if (bb->x1 >= cb->x2)
		continue;		// no intersection
	    if (bb->y1 >= cb->y2)
		continue;		// no intersection

	    // There is intersection, add clip info

	    if (!c->intersectingPolygons)
	    {
		c->intersectingPolygons =
		    calloc(pset->nPolygons, sizeof(int));
	    }
	    // allocate tex coords
	    // 2 {x, y} * 2 {front, back} * <total # of polygon front vertices>
	    if (!c->polygonVertexTexCoords)
	    {
		c->polygonVertexTexCoords =
		    calloc(2 * 2 * pset->nTotalFrontVertices, sizeof(GLfloat));
	    }
	    if (!c->intersectingPolygons || !c->polygonVertexTexCoords)
	    {
		compLogMessage (s->display, "animation", CompLogLevelError,
				"Not enough memory");
		freeClipsPolygons(pset);
		return FALSE;
	    }
	    c->intersectingPolygons[c->nIntersectingPolygons] = i;

	    int k;

	    for (k = 0; k < p->nSides; k++)
	    {
		float x = p->vertices[3 * k] +
		    p->centerPosStart.x;
		float y = p->vertices[3 * k + 1] +
		    p->centerPosStart.y;
		GLfloat tx;
		GLfloat ty;
		if (c->texMatrix.xy != 0.0f || c->texMatrix.yx != 0.0f)
		{	// "non-rect" coordinates
		    tx = COMP_TEX_COORD_XY(&c->texMatrix, x, y);
		    ty = COMP_TEX_COORD_YX(&c->texMatrix, x, y);
		}
		else
		{
		    tx = COMP_TEX_COORD_X(&c->texMatrix, x);
		    ty = COMP_TEX_COORD_Y(&c->texMatrix, y);
		}
		// for front vertices
		int ti = 2 * (2 * nFrontVerticesTilThisPoly + k);

		c->polygonVertexTexCoords[ti] = tx;
		c->polygonVertexTexCoords[ti + 1] = ty;

		// for back vertices
		ti = 2 * (2 * nFrontVerticesTilThisPoly +
			  (2 * p->nSides - 1 - k));
		c->polygonVertexTexCoords[ti] = tx;
		c->polygonVertexTexCoords[ti + 1] = ty;
	    }
	    c->nIntersectingPolygons++;
	    nFrontVerticesTilThisPoly += p->nSides;
	}
    }

    return TRUE;
}

/*
  static Bool computePolygonShadows(PolygonSet * pset)
  {
  PolygonObject *p = pset->polygons;
  int i;
  for (i=0; i<pset->nPolygons; i++, p++)
  {
  p->nShadowQuads = 0;

  // If an edge polygon, determine shadow quads
  // First count them
  if (x == 0)
  {
  p->nShadowQuads++;
  if (y == 0)
  p->nShadowQuads++;
  if (y == gridSizeY - 1)
  p->nShadowQuads++;
  }
  if (x == gridSizeX - 1)
  {
  p->nShadowQuads++;
  if (y == 0)
  p->nShadowQuads++;
  if (y == gridSizeY - 1)
  p->nShadowQuads++;
  }
  if (y == 0)
  p->nShadowQuads++;
  if (y == gridSizeY - 1)
  p->nShadowQuads++;

  // Allocate them
  p->shadowVertices = calloc (1, sizeof (GLfloat) * 3 *
  4 * p->nShadowQuads);
  if (!p->shadowVertices)
  {
  fprintf(stderr, "%s: Not enough memory at line %d!\n",
  __FILE__, __LINE__);
  freePolygonObjects (pset);
  return FALSE;
  }
  // Store them
  int n = 0;
  GLfloat *v = p->shadowVertices;
  if (x == 0)
  {
  // Left quad
  v[12 * n]     = w->attrib.x - w->output.left;
  v[12 * n + 1] = w->attrib.x - w->output.left;
  v[12 * n + 2] = w->attrib.x - w->output.left;
  n++;
  if (y == 0)
  n++;
  if (y == gridSizeY - 1)
  n++;
  }
  if (x == gridSizeX - 1)
  {
  n++;
  if (y == 0)
  n++;
  if (y == gridSizeY - 1)
  n++;
  }
  if (y == 0)
  n++;
  if (y == gridSizeY - 1)
  n++;

  p->nShadowQuads = n;
  }
  return TRUE;
  }
*/

void polygonsDrawCustomGeometry(CompScreen * s, CompWindow * w)
{
    ANIM_WINDOW(w);

    if (						// only draw windows on current viewport
	w->attrib.x > s->width || w->attrib.x + w->width < 0 ||
	w->attrib.y > s->height || w->attrib.y + w->height < 0 ||
	(aw->lastKnownCoords.x != NOT_INITIALIZED &&
	 (aw->lastKnownCoords.x != w->attrib.x ||
	  aw->lastKnownCoords.y != w->attrib.y)))
    {
	return;
	// since this is not the viewport the window was drawn
	// just before animation started
    }
    PolygonSet *pset = aw->polygonSet;

    // if polygon set is not valid or effect is not 3D (glide w/thickness=0)
    if (!pset)
	return;

    // TODO: Fix the source of the crash problem
    // (uninitialized lastClipInGroup)
    // instead of doing this uninitialized value check
    if (pset->firstNondrawnClip < 0 ||
	pset->firstNondrawnClip > pset->nClips ||
	(!aw->clipsUpdated &&
	 (pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] < 0 ||
	  pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] >= pset->nClips)))
    {
	return;
    }

    if (aw->clipsUpdated && aw->nDrawGeometryCalls > 0)
    {
	if (!processIntersectingPolygons(s, pset))
	{
	    return;
	}
    }

    int lastClip;				// last clip to draw

    if (aw->clipsUpdated)
    {
	lastClip = pset->nClips - 1;
    }
    else
    {
	lastClip = pset->lastClipInGroup[aw->nDrawGeometryCalls - 1];
    }

    float forwardProgress = defaultAnimProgress(aw);

    // OpenGL stuff starts here

    if (pset->doLighting)
    {
	glPushAttrib(GL_LIGHT0);
	glPushAttrib(GL_COLOR_MATERIAL);
	glPushAttrib(GL_LIGHTING);
	glEnable(GL_COLOR_MATERIAL);
	glEnable(GL_LIGHTING);

	GLfloat ambientLight[] = { 0.3f, 0.3f, 0.3f, 0.3f };
	GLfloat diffuseLight[] = { 0.9f, 0.9f, 0.9f, 0.9f };
	GLfloat light0Position[] = { -0.5f, 0.5f, -9.0f, 0.0f };

	glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseLight);
	glLightfv(GL_LIGHT0, GL_POSITION, light0Position);
    }
    glPushMatrix();

    // Store old blend values
    GLint blendSrcWas, blendDstWas;

    glGetIntegerv(GL_BLEND_SRC, &blendSrcWas);
    glGetIntegerv(GL_BLEND_DST, &blendDstWas);
    GLboolean blendWas = glIsEnabled(GL_BLEND);

    glPushAttrib(GL_STENCIL_BUFFER_BIT);
    glDisable(GL_STENCIL_TEST);

    //GLboolean normalArrayWas = glIsEnabled(GL_NORMAL_ARRAY);
    //glShadeModel(GL_FLAT);

    if (pset->doDepthTest)
    {
	// Depth test
	glPushAttrib(GL_DEPTH_FUNC);
	glPushAttrib(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_DEPTH_TEST);
    }

    // Clip planes
    GLdouble clipPlane0[] = { 1, 0, 0, 0 };
    GLdouble clipPlane1[] = { 0, 1, 0, 0 };
    GLdouble clipPlane2[] = { -1, 0, 0, 0 };
    GLdouble clipPlane3[] = { 0, -1, 0, 0 };

    // Save old color values
    GLfloat oldColor[4];

    glGetFloatv(GL_CURRENT_COLOR, oldColor);


    // Determine where we are called from in paint.c's drawWindowTexture
    // to find out how we should change the opacity
    GLint prevActiveTexture = GL_TEXTURE0_ARB;
    Bool saturationFull = TRUE;

    if (w->screen->canDoSaturated && aw->curPaintAttrib.saturation != COLOR)
    {
	saturationFull = FALSE;
	if (w->screen->canDoSlightlySaturated &&
	    aw->curPaintAttrib.saturation > 0)
	{
	    if (aw->curPaintAttrib.opacity < OPAQUE ||
		aw->curPaintAttrib.brightness != BRIGHT)
		prevActiveTexture = GL_TEXTURE3_ARB;
	    else
		prevActiveTexture = GL_TEXTURE2_ARB;
	}
	else
	    prevActiveTexture = GL_TEXTURE1_ARB;
    }

    float brightness = aw->curPaintAttrib.brightness / 65535.0;
    float opacity = aw->curPaintAttrib.opacity / 65535.0;

    float newOpacity = opacity;
    float fadePassedBy;

    if (!blendWas)				// if translucency is not already turned on in paint.c
    {
	glEnable(GL_BLEND);
    }
    if (saturationFull)
    {
	screenTexEnvMode(w->screen, GL_MODULATE);
    }
    else if (prevActiveTexture == GL_TEXTURE2_ARB)
    {
	w->screen->activeTexture(prevActiveTexture + 1);
	enableTexture(w->screen, aw->curTexture, aw->curTextureFilter);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // if fade-out duration is not specified per polygon
    if (pset->allFadeDuration > -1.0f)
    {
	fadePassedBy = forwardProgress - (1 - pset->allFadeDuration);

	// if "fade out starting point" is passed
	if (fadePassedBy > 1e-5)	// if true, allFadeDuration should be > 0
	{
	    float opacityFac;

	    if (aw->deceleratingMotion)
		opacityFac = 1 - decelerateProgress
		    (fadePassedBy / pset->allFadeDuration);
	    else
		opacityFac = 1 - fadePassedBy / pset->allFadeDuration;
	    if (opacityFac < 0)
		opacityFac = 0;
	    if (opacityFac > 1)
		opacityFac = 1;
	    newOpacity = opacity * opacityFac;
	}
    }

    int pass;
    // 0: draw opaque ones
    // 2: draw transparent ones
    for (pass = 0; pass < 2; pass++)
    {
	int j;

	for (j = pset->firstNondrawnClip; j <= lastClip; j++)
	{
	    Clip4Polygons *c = pset->clips + j;
	    int nFrontVerticesTilThisPoly = 0;
	    int nNewSides = 0;
	    int i;

	    for (i = 0; i < c->nIntersectingPolygons;
		 i++, nFrontVerticesTilThisPoly += nNewSides)
	    {
		PolygonObject *p =
		    pset->polygons + c->intersectingPolygons[i];
		nNewSides = p->nSides;

		float newOpacityPolygon = newOpacity;

		// if fade-out duration is specified per polygon
		if (pset->allFadeDuration == -1.0f)
		{
		    fadePassedBy = forwardProgress - p->fadeStartTime;
		    // if "fade out starting point" is passed
		    if (fadePassedBy > 1e-5)	// if true, then allFadeDuration > 0
		    {
			float opacityFac;

			if (aw->deceleratingMotion)
			    opacityFac = 1 - decelerateProgress
				(fadePassedBy / p->fadeDuration);
			else
			    opacityFac = 1 - fadePassedBy / p->fadeDuration;
			if (opacityFac < 0)
			    opacityFac = 0;
			if (opacityFac > 1)
			    opacityFac = 1;
			newOpacityPolygon = newOpacity * opacityFac;
		    }
		}

		if (newOpacityPolygon < 1e-5)	// if polygon object is invisible
		    continue;

		if (pass == 0)
		{
		    if (newOpacityPolygon < 0.9999)	// if not fully opaque
			continue;	// draw only opaque ones in pass 0
		}
		else if (newOpacityPolygon > 0.9999)	// if fully opaque
		    continue;	// draw only non-opaque ones in pass 1

		glPushMatrix();

		if (pset->correctPerspective != CorrectPerspectiveNone)
		{
		    Point center;

		    if (pset->correctPerspective == CorrectPerspectivePolygon)
		    {
			// use polygon's center
			center.x = p->centerPos.x;
			center.y = p->centerPos.y;
		    }
		    else // CorrectPerspectiveWindow
		    {
			// use window's center
			center.x = WIN_X(w) + WIN_W(w) / 2;
			center.y = WIN_Y(w) + WIN_H(w) / 2;
		    }
		    // Correct perspective appearance by skewing
		    GLfloat skewx = -((center.x - s->width / 2) * 1.15);
		    GLfloat skewy = -((center.y - s->height / 2) * 1.15);

		    // column-major order
		    GLfloat skewMat[16] =
			{1,0,0,0,
			 0,1,0,0,
			 skewx,skewy,1,0,
			 0,0,0,1};
		    glMultMatrixf( skewMat);
		}

		// Center
		glTranslatef(p->centerPos.x, p->centerPos.y, p->centerPos.z);

		// Scale z first
		glScalef(1.0f, 1.0f, 1.0f / s->width);

		// Move by "rotation axis offset"
		glTranslatef(p->rotAxisOffset.x, p->rotAxisOffset.y,
			     p->rotAxisOffset.z);

		// Rotate by desired angle
		glRotatef(p->rotAngle, p->rotAxis.x, p->rotAxis.y,
			  p->rotAxis.z);

		// Move back to center
		glTranslatef(-p->rotAxisOffset.x, -p->rotAxisOffset.y,
			     -p->rotAxisOffset.z);

		// Scale back
		glScalef(1.0f, 1.0f, s->width);


		clipPlane0[3] = -(c->boxf.x1 - p->centerPosStart.x);
		clipPlane1[3] = -(c->boxf.y1 - p->centerPosStart.y);
		clipPlane2[3] = (c->boxf.x2 - p->centerPosStart.x);
		clipPlane3[3] = (c->boxf.y2 - p->centerPosStart.y);
		glClipPlane(GL_CLIP_PLANE0, clipPlane0);
		glClipPlane(GL_CLIP_PLANE1, clipPlane1);
		glClipPlane(GL_CLIP_PLANE2, clipPlane2);
		glClipPlane(GL_CLIP_PLANE3, clipPlane3);

		int k;

		for (k = 0; k < 4; k++)
		    glEnable(GL_CLIP_PLANE0 + k);
		Bool fadeBackAndSides =
		    pset->backAndSidesFadeDur > 0 &&
		    forwardProgress <= pset->backAndSidesFadeDur;

		float newOpacityPolygon2 = newOpacityPolygon;

		if (fadeBackAndSides)
		{
		    // Fade-in opacity for back face and sides
		    newOpacityPolygon2 *=
			(forwardProgress / pset->backAndSidesFadeDur);
		}

		if (saturationFull)
		    glColor4f(brightness, brightness, brightness,
			      newOpacityPolygon2);
		else if (prevActiveTexture == GL_TEXTURE1_ARB)
		{
		    // From paint.c

		    GLfloat constant2[4] =
			{ 0.5f +
			  0.5f * RED_SATURATION_WEIGHT * brightness,
			  0.5f + 0.5f * GREEN_SATURATION_WEIGHT * brightness,
			  0.5f + 0.5f * BLUE_SATURATION_WEIGHT * brightness,
			  newOpacityPolygon2
			};

		    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR,
			       constant2);
		}
		else			//if (prevActiveTexture >= GL_TEXTURE2_ARB)
		{
		    GLfloat constant2[4] = { brightness,
					     brightness,
					     brightness,
					     newOpacityPolygon2
		    };

		    // From paint.c

		    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR,
			       constant2);

		    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
			      GL_COMBINE);

		    glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
		    glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
		    glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
		    glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
		    glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

		    glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
		    glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
		    glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_CONSTANT);
		    glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA,
			      GL_SRC_ALPHA);
		    glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA,
			      GL_SRC_ALPHA);
		}

		//glEnableClientState(GL_NORMAL_ARRAY);
		//glEnable(GL_NORMALIZE);

		// Draw back face
		glVertexPointer(3, GL_FLOAT, 0, p->vertices + 3 * p->nSides);
		//glNormalPointer(GL_FLOAT, 0,
		//              p->normals + 3 * p->nSides);
		glTexCoordPointer(2, GL_FLOAT, 0,
				  c->polygonVertexTexCoords +
				  2 * (2 * nFrontVerticesTilThisPoly +
				       p->nSides));

		glNormal3f(p->normals[3], p->normals[4], p->normals[5]);
		glDrawArrays(GL_POLYGON, 0, p->nSides);

		// Front vertex coords
		glVertexPointer(3, GL_FLOAT, 0, p->vertices);
		//glNormalPointer(GL_FLOAT, 0, p->normals);
		glTexCoordPointer(2, GL_FLOAT, 0,
				  c->polygonVertexTexCoords +
				  2 * 2 * nFrontVerticesTilThisPoly);

		//if (pset->thickness > 1e-5)
		{
		    // TODO: Surface normals for sides
		    // Draw quad strip for sides
		    if (TRUE)
		    {
			// Do each quad separately to be able to specify
			// different normals
			for (k = 0; k < p->nSides; k++)
			{
			    /*
			      int k2 = (k + 2) * 3;
			      glNormal3f(p->normals[k2 + 0],
			      p->normals[k2 + 1],
			      p->normals[k2 + 2]); */
			    glNormal3f(p->normals[0],	// front face normal for now
				       p->normals[1], p->normals[2]);
			    glDrawElements(GL_QUADS, 4,
					   GL_UNSIGNED_SHORT,
					   p->sideIndices + k * 4);
			}
		    }
		    else			// no need for separate quad rendering
			glDrawElements(GL_QUAD_STRIP, 2 * (p->nSides + 1),
				       GL_UNSIGNED_SHORT, p->sideIndices);
		}
		if (fadeBackAndSides)
		    // if opacity was changed just above
		{
		    // Go back to normal opacity for front face

		    if (saturationFull)
			glColor4f(brightness, brightness, brightness,
				  newOpacityPolygon);
		    else if (prevActiveTexture == GL_TEXTURE1_ARB)
		    {
			GLfloat constant[4] =
			    { 0.5f +
			      0.5f * RED_SATURATION_WEIGHT * brightness,
			      0.5f + 0.5f * GREEN_SATURATION_WEIGHT *
			      brightness,
			      0.5f + 0.5f * BLUE_SATURATION_WEIGHT * brightness,
			      newOpacityPolygon
			    };

			glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR,
				   constant);
		    }
		    else
		    {
			GLfloat constant[4] = { brightness,
						brightness,
						brightness,
						newOpacityPolygon
			};
			glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR,
				   constant);
		    }
		}
		// Draw front face

		glNormal3f(p->normals[0], p->normals[1], p->normals[2]);
		glDrawArrays(GL_POLYGON, 0, p->nSides);

		for (k = 0; k < 4; k++)
		    glDisable(GL_CLIP_PLANE0 + k);

		glPopMatrix();
	    }
	}
    }
    // Restore
    // -----------------------------------------

    // Restore old color values
    glColor4f(oldColor[0], oldColor[1], oldColor[2], oldColor[3]);
    //screenTexEnvMode(w->screen, GL_REPLACE);

    glPopAttrib();
    if (pset->doDepthTest)
    {
	glPopAttrib();
	glPopAttrib();
    }

    //if (!normalArrayWas)
    //  glDisableClientState(GL_NORMAL_ARRAY);

    // Restore texture stuff
    if (saturationFull)
	screenTexEnvMode(w->screen, GL_REPLACE);
    else if (prevActiveTexture == GL_TEXTURE2_ARB)
    {
	disableTexture(w->screen, aw->curTexture);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	w->screen->activeTexture(prevActiveTexture);
    }
    // Restore blend values
    if (!blendWas)
	glDisable(GL_BLEND);
    glBlendFunc(blendSrcWas, blendDstWas);

    glPopMatrix();

    if (pset->doLighting)		// && !s->lighting)
    {
	glPopAttrib();
	glPopAttrib();
	glPopAttrib();
    }
    if (aw->clipsUpdated)		// set end mark for this group of clips
	pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] = lastClip;

    // Next time, start drawing from next group of clips
    pset->firstNondrawnClip =
	pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] + 1;
}

void polygonsPrePaintWindow(CompScreen * s, CompWindow * w)
{
    ANIM_WINDOW(w);
    if (aw->polygonSet)
	aw->polygonSet->firstNondrawnClip = 0;
}

void polygonsPostPaintWindow(CompScreen * s, CompWindow * w)
{
    ANIM_WINDOW(w);
    if (aw->clipsUpdated &&	// clips should be dropped only in the 1st step
	aw->polygonSet && aw->nDrawGeometryCalls == 0)	// if clips not drawn
    {
	// drop these unneeded clips (e.g. ones passed by blurfx)
	aw->polygonSet->nClips = aw->polygonSet->firstNondrawnClip;
    }
}

// Computes polygon's new position and orientation
// with linear movement
void
polygonsLinearAnimStepPolygon(CompWindow * w,
			      PolygonObject * p, float forwardProgress)
{
    float moveProgress = forwardProgress - p->moveStartTime;

    if (p->moveDuration > 0)
	moveProgress /= p->moveDuration;
    if (moveProgress < 0)
	moveProgress = 0;
    else if (moveProgress > 1)
	moveProgress = 1;

    p->centerPos.x = moveProgress * p->finalRelPos.x + p->centerPosStart.x;
    p->centerPos.y = moveProgress * p->finalRelPos.y + p->centerPosStart.y;
    p->centerPos.z = 1.0f / w->screen->width *
	moveProgress * p->finalRelPos.z + p->centerPosStart.z;

    p->rotAngle = moveProgress * p->finalRotAng + p->rotAngleStart;
}

// Similar to polygonsLinearAnimStepPolygon,
// but slightly ac/decelerates movement
void
polygonsDeceleratingAnimStepPolygon(CompWindow * w,
				    PolygonObject * p, float forwardProgress)
{
    float moveProgress = forwardProgress - p->moveStartTime;

    if (p->moveDuration > 0)
	moveProgress /= p->moveDuration;
    if (moveProgress < 0)
	moveProgress = 0;
    else if (moveProgress > 1)
	moveProgress = 1;

    moveProgress = decelerateProgress(moveProgress);

    p->centerPos.x = moveProgress * p->finalRelPos.x + p->centerPosStart.x;
    p->centerPos.y = moveProgress * p->finalRelPos.y + p->centerPosStart.y;
    p->centerPos.z = 1.0f / w->screen->width *
	moveProgress * p->finalRelPos.z + p->centerPosStart.z;

    p->rotAngle = moveProgress * p->finalRotAng + p->rotAngleStart;
}

Bool polygonsAnimStep(CompScreen * s, CompWindow * w, float time)
{
    if (!defaultAnimStep(s, w, time))
	return FALSE;

    ANIM_WINDOW(w);

    Model *model = aw->model;

    float forwardProgress = defaultAnimProgress(aw);

    if (aw->polygonSet)
    {
	if (animEffectPropertiesTmp[aw->curAnimEffect].
	    animStepPolygonFunc)
	{
	    int i;
	    for (i = 0; i < aw->polygonSet->nPolygons; i++)
		animEffectPropertiesTmp[aw->curAnimEffect].
		    animStepPolygonFunc
		    (w, &aw->polygonSet->polygons[i],
		     forwardProgress);
	}
    }
    else
	compLogMessage (s->display, "animation", CompLogLevelDebug,
			"%s: pset null at line %d\n",__FILE__,  __LINE__);
    modelCalcBounds(model);
    return TRUE;
}
