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

// =====================  Effect: Glide  =========================

void fxGlideGetParams
	(AnimScreen *as, AnimWindow *aw,
	 float *finalDistFac, float *finalRotAng, float *thickness)
{
	// Sub effects:
	// 1: Glide 1
	// 2: Glide 2
	if (aw->subEffectNo == 1)
	{
		*finalDistFac = as->opt[ANIM_SCREEN_OPTION_GLIDE1_AWAY_POS].value.f;
		*finalRotAng = as->opt[ANIM_SCREEN_OPTION_GLIDE1_AWAY_ANGLE].value.f;
		*thickness = as->opt[ANIM_SCREEN_OPTION_GLIDE1_THICKNESS].value.f;
	}
	else
	{
		*finalDistFac = as->opt[ANIM_SCREEN_OPTION_GLIDE2_AWAY_POS].value.f;
		*finalRotAng = as->opt[ANIM_SCREEN_OPTION_GLIDE2_AWAY_ANGLE].value.f;
		*thickness = as->opt[ANIM_SCREEN_OPTION_GLIDE2_THICKNESS].value.f;
	}

}

float fxGlideAnimProgress(AnimWindow * aw)
{
	float forwardProgress =
			1 - (aw->animRemainingTime - aw->timestep) /
			(aw->animTotalTime - aw->timestep);
	forwardProgress = MIN(forwardProgress, 1);
	forwardProgress = MAX(forwardProgress, 0);

	if (aw->curWindowEvent == WindowEventCreate ||
		aw->curWindowEvent == WindowEventUnminimize ||
		aw->curWindowEvent == WindowEventUnshade)
		forwardProgress = 1 - forwardProgress;

	return decelerateProgress2(forwardProgress);
}

static void
fxGlideModelStepObject(CompWindow * w,
					   Model * model,
					   Object * obj,
					   GLfloat *mat,
					   Point3d rotAxisOffset)
{
	float origx = w->attrib.x + (WIN_W(w) * obj->gridPosition.x -
								 w->output.left) * model->scale.x;
	float origy = w->attrib.y + (WIN_H(w) * obj->gridPosition.y -
								 w->output.top) * model->scale.y;

	obj->posRel3d.x = origx - rotAxisOffset.x;
	obj->posRel3d.y = origy - rotAxisOffset.y;

	applyTransformToObject(obj, mat);
	obj->position.x += rotAxisOffset.x;
	obj->position.y += rotAxisOffset.y;
}

void fxGlideAnimStep(CompScreen * s, CompWindow * w, float time)
{
	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	float finalDistFac;
	float finalRotAng;
	float thickness;

	fxGlideGetParams(as, aw, &finalDistFac, &finalRotAng, &thickness);

	if (thickness > 1e-5) // the effect is 3D
	{
		polygonsAnimStep(s, w, time); // do 3D anim step instead
		return;
	}

	// for 2D glide effect
	// ------------------------

	int i, j, steps;
	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->timestep = timestep;

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;

	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	for (j = 0; j < steps; j++)
	{
		float forwardProgress = fxGlideAnimProgress(aw);

		float finalz = finalDistFac * 0.8 * DEFAULT_Z_CAMERA * s->width;

		Vector3d rotAxis = {1, 0, 0};
		Point3d rotAxisOffset =
			{WIN_X(w) + WIN_W(w) * model->scale.x / 2,
			 WIN_Y(w) + WIN_H(w) * model->scale.y / 2,
			 0};
		Point3d modelPos = {0, 0, finalz * forwardProgress};

		GLfloat mat[16];
		obtainTransformMatrix(s, mat, finalRotAng * forwardProgress,
							  rotAxis, modelPos);
		for (i = 0; i < model->numObjects; i++)
			fxGlideModelStepObject(w, model,
								   &model->objects[i],
								   mat,
								   rotAxisOffset);

		aw->animRemainingTime -= timestep;
		if (aw->animRemainingTime <= 0)
		{
			aw->animRemainingTime = 0;	// avoid sub-zero values
			break;
		}
	}
	modelCalcBounds(model);
}

void
fxGlideUpdateWindowAttrib(AnimScreen * as,
						 AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	float finalDistFac;
	float finalRotAng;
	float thickness;

	fxGlideGetParams(as, aw, &finalDistFac, &finalRotAng, &thickness);

	if (thickness > 1e-5) // the effect is 3D
		return;

	// the effect is 2D

	if (aw->model->scale.x < 1.0 &&	// if Scale plugin in progress
		aw->curWindowEvent == WindowEventUnminimize)	// and if unminimizing
		return;					// then allow Fade to take over opacity
	float forwardProgress = fxGlideAnimProgress(aw);

	wAttrib->opacity = aw->storedOpacity * (1 - forwardProgress);
}


void fxGlideInit(CompScreen * s, CompWindow * w)
{
	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	float finalDistFac;
	float finalRotAng;
	float thickness;

	fxGlideGetParams(as, aw, &finalDistFac, &finalRotAng, &thickness);

	if (thickness < 1e-5) // if thicknes is 0, we'll make the effect 2D
	{
		// store window opacity
		aw->storedOpacity = w->paint.opacity;
		aw->timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo
						as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

		return; // we're done with 2D initialization
	}

	// for 3D glide effect
	// ------------------------

	PolygonSet *pset = aw->polygonSet;

	pset->includeShadows = (thickness < 1e-5);

	if (!tessellateIntoRectangles(w, 1, 1, thickness))
		return;

	PolygonObject *p = pset->polygons;

	int i;

	for (i = 0; i < pset->nPolygons; i++, p++)
	{
		p->rotAxis.x = 1;
		p->rotAxis.y = 0;
		p->rotAxis.z = 0;

		p->finalRelPos.x = 0;
		p->finalRelPos.y = 0;
		p->finalRelPos.z = finalDistFac * 0.8 * DEFAULT_Z_CAMERA * s->width;

		p->finalRotAng = finalRotAng;
	}
	pset->allFadeDuration = 1.0f;
	pset->backAndSidesFadeDur = 0.2f;
	pset->doLighting = TRUE;
	pset->correctPerspective = TRUE;
}

