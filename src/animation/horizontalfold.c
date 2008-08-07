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

// =====================  Effect: Horizontal Folds  =========================

static inline float
getObjectZ (Model *model,
	    float forwardProgress,
	    float sinForProg,
	    float relDistToFoldCenter,
	    float foldMaxAmp)
{
    return -(sinForProg *
	     foldMaxAmp *
	     model->scale.x *
	     2 * (0.5 - relDistToFoldCenter));
}

void
fxHorizontalFoldsInitGrid(AnimScreen *as,
			  AnimWindow *aw,
			  int *gridWidth, int *gridHeight)
{
    *gridWidth = 2;
    if (aw->curWindowEvent == WindowEventShade ||
	aw->curWindowEvent == WindowEventUnshade)
	*gridHeight = 3 + 2 *	
	    animGetI(as, aw, ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_NUM_FOLDS);
    else
	*gridHeight = 1 + 2 *
	    animGetI(as, aw, ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_NUM_FOLDS);
}

static void inline
fxHorizontalFoldsModelStepObject(CompWindow * w,
				 Model * model,
				 Object * object,
				 float forwardProgress,
				 float sinForProg,
				 float foldMaxAmp, int rowNo)
{
    ANIM_WINDOW(w);

    float origx = w->attrib.x + (WIN_W(w) * object->gridPosition.x -
				 w->output.left) * model->scale.x;
    float origy = w->attrib.y + (WIN_H(w) * object->gridPosition.y -
				 w->output.top) * model->scale.y;

    object->position.x = origx;

    if (aw->curWindowEvent == WindowEventShade ||
	aw->curWindowEvent == WindowEventUnshade)
    {
	// Execute shade mode

	float relDistToFoldCenter = (rowNo % 2 == 1 ? 0.5 : 0);

	if (object->gridPosition.y == 0)
	{
	    object->position.y = WIN_Y(w);
	    object->position.z = 0;
	}
	else if (object->gridPosition.y == 1)
	{
	    object->position.y =
		(1 - forwardProgress) * origy +
		forwardProgress *
		(WIN_Y(w) + model->topHeight + model->bottomHeight);
	    object->position.z = 0;
	}
	else
	{
	    object->position.y =
		(1 - forwardProgress) * origy +
		forwardProgress * (WIN_Y(w) + model->topHeight);
	    object->position.z =
		getObjectZ (model, forwardProgress, sinForProg,
			    relDistToFoldCenter, foldMaxAmp);
	}
    }
    else
    {
	// Execute normal mode

	float relDistToFoldCenter;

	relDistToFoldCenter = (rowNo % 2 == 0 ? 0.5 : 0);

	object->position.y =
	    (1 - forwardProgress) * origy +
	    forwardProgress * (BORDER_Y(w) + BORDER_H(w) / 2.0);
	object->position.z =
		getObjectZ (model, forwardProgress, sinForProg,
			    relDistToFoldCenter, foldMaxAmp);
    }
}

void
fxHorizontalFoldsModelStep (CompScreen *s, CompWindow *w, float time)
{
    defaultAnimStep (s, w, time);

    ANIM_SCREEN(s);
    ANIM_WINDOW(w);

    Model *model = aw->model;

    // center for perspective correction
    Point center;

    float winHeight = 0;
    if (aw->curWindowEvent == WindowEventShade ||
	aw->curWindowEvent == WindowEventUnshade)
    {
	winHeight = (w)->height;
    }
    else
    {
	winHeight = BORDER_H (w);
    }
    int nHalfFolds =
	2.0 * animGetI (as, aw, ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_NUM_FOLDS);
    float foldMaxAmp =
	0.3 * pow ((winHeight / nHalfFolds) / s->height, 0.3) *
	animGetF (as, aw, ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_AMP_MULT);

    float forwardProgress = getProgressAndCenter (w, &center);

    float sinForProg = sin (forwardProgress * M_PI / 2);

    Object *object = model->objects;
    int i;
    for (i = 0; i < model->numObjects; i++, object++)
	fxHorizontalFoldsModelStepObject(w, 
					 model,
					 object,
					 forwardProgress,
					 sinForProg,
					 foldMaxAmp,
					 i / model->gridWidth);

    applyPerspectiveSkew (w->screen, &aw->transform, &center);
}

