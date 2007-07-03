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


void
fxHorizontalFoldsInitGrid(AnimScreen * as,
						  WindowEvent forWindowEvent,
						  int *gridWidth, int *gridHeight)
{
	*gridWidth = 2;
	if (forWindowEvent == WindowEventShade ||
		forWindowEvent == WindowEventUnshade)
		*gridHeight = 3 + 2 *	
			as->opt[ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_NUM_FOLDS].value.i;
	else
		*gridHeight = 1 + 2 *
			as->opt[ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_NUM_FOLDS].value.i;
}

static void
fxHorizontalFoldsModelStepObject(CompWindow * w,
								 Model * model,
								 Object * object,
								 float forwardProgress,
								 float curveMaxAmp, int rowNo)
{
	ANIM_WINDOW(w);

	float origx = w->attrib.x + (WIN_W(w) * object->gridPosition.x -
								 w->output.left) * model->scale.x;
	float origy = w->attrib.y + (WIN_H(w) * object->gridPosition.y -
								 w->output.top) * model->scale.y;

	if (aw->curWindowEvent == WindowEventShade ||
		aw->curWindowEvent == WindowEventUnshade)
	{
		// Execute shade mode

		float relDistToFoldCenter = (rowNo % 2 == 1 ? 0.5 : 0);

		if (object->gridPosition.y == 0)
		{
			object->position.x = origx;
			object->position.y = WIN_Y(w);
		}
		else if (object->gridPosition.y == 1)
		{
			object->position.x = origx;
			object->position.y =
					(1 - forwardProgress) * origy +
					forwardProgress *
					(WIN_Y(w) + model->topHeight + model->bottomHeight);
		}
		else
		{
			object->position.x =
					origx + sin(forwardProgress * M_PI / 2) *
					(0.5 -
					 object->gridPosition.x) * 2 * model->scale.x *
					(curveMaxAmp -
					 curveMaxAmp * 4 * relDistToFoldCenter *
					 relDistToFoldCenter);
			object->position.y =
					(1 - forwardProgress) * origy +
					forwardProgress * (WIN_Y(w) + model->topHeight);
		}
	}
	else
	{							// Execute normal mode

		float relDistToFoldCenter;

		relDistToFoldCenter = (rowNo % 2 == 0 ? 0.5 : 0);

		object->position.x =
				origx + sin(forwardProgress * M_PI / 2) *
				(0.5 - object->gridPosition.x) * 2 * model->scale.x *
				(curveMaxAmp - curveMaxAmp * 4 *
				 relDistToFoldCenter * relDistToFoldCenter);
		object->position.y =
				(1 - forwardProgress) * origy +
				forwardProgress * (BORDER_Y(w) + BORDER_H(w) / 2.0);
	}
}

Bool
fxHorizontalFoldsModelStep(CompScreen * s, CompWindow * w, float time)
{
	if (!defaultAnimStep(s, w, time))
		return FALSE;

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	float forwardProgress;
	if ((aw->curWindowEvent == WindowEventMinimize ||
		 aw->curWindowEvent == WindowEventUnminimize) &&
		as->opt[ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_Z2TOM].
		value.b)
	{
		float dummy;
		fxZoomAnimProgress(as, aw, &forwardProgress, &dummy, TRUE);
	}
	else
		forwardProgress = defaultAnimProgress(aw);

	int i;
	for (i = 0; i < model->numObjects; i++)
		fxHorizontalFoldsModelStepObject(w, 
										 model,
										 &model->objects[i],
										 forwardProgress,
										 as->opt[ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_AMP].value.f *
										 WIN_W(w),
										 i / model->gridWidth);
	modelCalcBounds(model);
	return TRUE;
}
