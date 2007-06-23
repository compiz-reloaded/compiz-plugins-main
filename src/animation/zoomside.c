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

// =====================  Effect: Zoom and Sidekick  =========================

void fxSidekickInit(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	// determine number of rotations randomly in [0.75, 1.25] range
	aw->numZoomRotations =
			as->opt[ANIM_SCREEN_OPTION_SIDEKICK_NUM_ROTATIONS].value.f *
			(1.0 + 0.2 * rand() / RAND_MAX - 0.1);

	// store window opacity
	aw->storedOpacity = w->paint.opacity;
	aw->timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);
}



static void fxZoomAnimProgressDir(AnimScreen * as,
								  AnimWindow * aw,
								  float *moveProgress,
								  float *scaleProgress)
{
	float forwardProgress =
			1 - (aw->animRemainingTime - aw->timestep) /
			(aw->animTotalTime - aw->timestep);
	forwardProgress = MIN(forwardProgress, 1);
	forwardProgress = MAX(forwardProgress, 0);

	float x = forwardProgress;
	Bool backwards = FALSE;
	int animProgressDir = 1;

	if (aw->curWindowEvent == WindowEventUnminimize ||
		aw->curWindowEvent == WindowEventCreate)
		animProgressDir = 2;
	if (aw->animOverrideProgressDir != 0)
		animProgressDir = aw->animOverrideProgressDir;
	if ((animProgressDir == 1 &&
		 (aw->curWindowEvent == WindowEventUnminimize ||
		  aw->curWindowEvent == WindowEventCreate)) ||
		(animProgressDir == 2 &&
		 (aw->curWindowEvent == WindowEventMinimize ||
		  aw->curWindowEvent == WindowEventClose)))
		backwards = TRUE;
	if (backwards)
		x = 1 - x;
	
	float cx = SPRING_CROSSING_X;
	float nonSpringyProgress = 1 - pow(1-(x/cx*0.5),10);

	x = pow(x, 0.7);
	float damping = (pow(1-(x*0.5),10)-pow(1-0.5,10))/(1-pow(1-0.5,10));

	if (moveProgress)
	{
		float springiness = 0;
		if (aw->curAnimEffect == AnimEffectZoom)
			springiness = 2 *
				as->opt[ANIM_SCREEN_OPTION_ZOOM_SPRINGINESS].value.f;
		else
			springiness = 1.6 *
				as->opt[ANIM_SCREEN_OPTION_SIDEKICK_SPRINGINESS].value.f;

		float springyMoveProgress =
			1 - sin(3.5*M_PI*(pow(x,1.5)-1)) * damping;

		*moveProgress =
			springiness * springyMoveProgress +
			(1 - springiness) * nonSpringyProgress;

		if (aw->curWindowEvent == WindowEventUnminimize ||
			aw->curWindowEvent == WindowEventCreate)
			*moveProgress = 1 - *moveProgress;
		if (backwards)
			*moveProgress = 1 - *moveProgress;
	}
	if (scaleProgress)
	{
		*scaleProgress = nonSpringyProgress;
		if (aw->curWindowEvent == WindowEventUnminimize ||
			aw->curWindowEvent == WindowEventCreate)
			*scaleProgress = 1 - *scaleProgress;
		if (backwards)
			*scaleProgress = 1 - *scaleProgress;
	}
}

static void
fxSidekickModelStepObject(CompWindow * w,
						  Model * model,
						  Object * object,
						  Point currentCenter, Point currentSize,
						  float sinRot, float cosRot)
{
	float x =
		currentCenter.x - currentSize.x / 2 +
		currentSize.x * object->gridPosition.x;
	float y =
		currentCenter.y - currentSize.y / 2 +
		currentSize.y * object->gridPosition.y;

	x -= currentCenter.x;
	y -= currentCenter.y;

	object->position.x = cosRot * x - sinRot * y;
	object->position.y = sinRot * x + cosRot * y;

	object->position.x += currentCenter.x;
	object->position.y += currentCenter.y;
}

static void
fxZoomModelStepObject(CompScreen *s, CompWindow * w,
					  Model * model, Object * object,
					  Point currentCenter, Point currentSize)
{
	object->position.x =
		currentCenter.x - currentSize.x / 2 +
		currentSize.x * object->gridPosition.x;
	object->position.y =
		currentCenter.y - currentSize.y / 2 +
		currentSize.y * object->gridPosition.y;
}

void fxZoomModelStep(CompScreen * s, CompWindow * w, float time)
{
	int i, j, steps;

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

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

	Point winCenter =
		{(WIN_X(w) + WIN_W(w) * model->scale.x / 2),
		 (WIN_Y(w) + WIN_H(w) * model->scale.y / 2)};
	Point iconCenter =
		{aw->icon.x + aw->icon.width / 2,
		 aw->icon.y + aw->icon.height / 2};
	Point winSize =
		{WIN_W(w) * model->scale.x,
		 WIN_H(w) * model->scale.y};

	for (j = 0; j < steps; j++)
	{
		float sinRot = 0;
		float cosRot = 0;

		float scaleProgress;
		float moveProgress;
		float rotateProgress = 0;

		if (aw->curAnimEffect == AnimEffectSidekick)
		{
			fxZoomAnimProgressDir(as, aw, &moveProgress, &scaleProgress);
			rotateProgress = moveProgress;
		}
		else
		{
			fxZoomAnimProgressDir(as, aw, &moveProgress, &scaleProgress);
		}

		Point currentCenter =
			{(1 - moveProgress) * winCenter.x + moveProgress * iconCenter.x,
			 (1 - moveProgress) * winCenter.y + moveProgress * iconCenter.y};
		Point currentSize =
			{(1 - scaleProgress) * winSize.x + scaleProgress * aw->icon.width,
			 (1 - scaleProgress) * winSize.y + scaleProgress * aw->icon.height};

		if (aw->curAnimEffect == AnimEffectSidekick)
		{
			sinRot = sin(2 * M_PI * rotateProgress * aw->numZoomRotations);
			cosRot = cos(2 * M_PI * rotateProgress * aw->numZoomRotations);

			for (i = 0; i < model->numObjects; i++)
				fxSidekickModelStepObject(w, model,
										  &model->objects[i],
										  currentCenter, currentSize,
										  sinRot, cosRot);
		}
		else
			for (i = 0; i < model->numObjects; i++)
				fxZoomModelStepObject(s, w, model,
									  &model->objects[i],
									  currentCenter, currentSize);

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
fxZoomUpdateWindowAttrib(AnimScreen * as,
						 AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	if (aw->model->scale.x < 1.0 &&	// if Scale plugin in progress
		aw->curWindowEvent == WindowEventUnminimize)	// and if unminimizing
		return;					// then allow Fade to take over opacity
	float forwardProgress;
	fxZoomAnimProgressDir(as, aw, NULL, &forwardProgress);

	wAttrib->opacity =
			(GLushort) (aw->storedOpacity * pow(1 - forwardProgress, 0.75));
}
