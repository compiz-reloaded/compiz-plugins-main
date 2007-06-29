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

void
fxZoomUpdateWindowAttrib(AnimScreen * as,
						 AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	float forwardProgress;
	fxZoomAnimProgressDir(as, aw, NULL, &forwardProgress);

	wAttrib->opacity =
			(GLushort) (aw->storedOpacity * pow(1 - forwardProgress, 0.75));
}

void
fxZoomUpdateWindowTransform(CompScreen *s, CompWindow *w, CompTransform *wTransform)
{
	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Point winCenter =
		{(WIN_X(w) + WIN_W(w) / 2.0),
		 (WIN_Y(w) + WIN_H(w) / 2.0)};
	Point iconCenter =
		{aw->icon.x + aw->icon.width / 2.0,
		 aw->icon.y + aw->icon.height / 2.0};
	Point winSize =
		{WIN_W(w) * 1.0,
		 WIN_H(w) * 1.0};
	winSize.x = (winSize.x == 0 ? 1 : winSize.x);
	winSize.y = (winSize.y == 0 ? 1 : winSize.y);

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

	Point curCenter =
		{(1 - moveProgress) * winCenter.x + moveProgress * iconCenter.x,
		 (1 - moveProgress) * winCenter.y + moveProgress * iconCenter.y};
	Point curScale =
		{((1 - scaleProgress) * winSize.x + scaleProgress * aw->icon.width) /
		 winSize.x,
		 ((1 - scaleProgress) * winSize.y + scaleProgress * aw->icon.height) /
		 winSize.y};

	matrixTranslate (wTransform, winCenter.x, winCenter.y, 0);
	matrixScale (wTransform, curScale.x, curScale.y, 1.0f);
	float tx = (curCenter.x - winCenter.x) / curScale.x;
	float ty = (curCenter.y - winCenter.y) / curScale.y;
	matrixTranslate (wTransform, tx, ty, 0);
	if (aw->curAnimEffect == AnimEffectSidekick)
	{
		matrixRotate (wTransform, rotateProgress * 360 * aw->numZoomRotations,
					  0.0f, 0.0f, 1.0f);
	}
	matrixTranslate (wTransform, -winCenter.x, -winCenter.y, 0);
}
