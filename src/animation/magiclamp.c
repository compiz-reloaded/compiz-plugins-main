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

void
fxMagicLampInitGrid(AnimScreen * as,
					 WindowEvent forWindowEvent,
					 int *gridWidth, int *gridHeight)
{
	*gridWidth = 2;
	*gridHeight = as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_GRID_RES].value.i;
}
void
fxVacuumInitGrid(AnimScreen * as, WindowEvent forWindowEvent,
					 int *gridWidth, int *gridHeight)
{
	*gridWidth = 2;
	*gridHeight = as->opt[ANIM_SCREEN_OPTION_VACUUM_GRID_RES].value.i;
}

void fxMagicLampInit(CompScreen * s, CompWindow * w)
{
	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	int screenHeight = s->height;
	aw->minimizeToTop = (WIN_Y(w) + WIN_H(w) / 2) >
			(aw->icon.y + aw->icon.height / 2);
	int maxWaves;
	float waveAmpMin, waveAmpMax;

	if (aw->curAnimEffect == AnimEffectMagicLamp)
	{
		maxWaves = as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_MAX_WAVES].value.i;
		waveAmpMin =
				as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MIN].value.f;
		waveAmpMax =
				as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MAX].value.f;
	}
	else
	{
		maxWaves = 0;
		waveAmpMin = 0;
		waveAmpMax = 0;
	}
	if (waveAmpMax < waveAmpMin)
		waveAmpMax = waveAmpMin;

	if (maxWaves > 0)
	{
		float distance;

		if (aw->minimizeToTop)
			distance = WIN_Y(w) + WIN_H(w) - aw->icon.y;
		else
			distance = aw->icon.y - WIN_Y(w);

		// Initialize waves
		model->magicLampWaveCount =
				1 + (float)maxWaves *distance / screenHeight;

		if (!(model->magicLampWaves))
			model->magicLampWaves =
					calloc(1, model->magicLampWaveCount * sizeof(WaveParam));

		int ampDirection = (RAND_FLOAT() < 0.5 ? 1 : -1);
		int i;
		float minHalfWidth = 0.22f;
		float maxHalfWidth = 0.38f;

		for (i = 0; i < model->magicLampWaveCount; i++)
		{
			model->magicLampWaves[i].amp =
					ampDirection * (waveAmpMax - waveAmpMin) *
					rand() / RAND_MAX + ampDirection * waveAmpMin;
			model->magicLampWaves[i].halfWidth =
					RAND_FLOAT() * (maxHalfWidth -
									minHalfWidth) + minHalfWidth;

			// avoid offset at top and bottom part by added waves
			float availPos = 1 - 2 * model->magicLampWaves[i].halfWidth;
			float posInAvailSegment = 0;

			if (i > 0)
				posInAvailSegment =
						(availPos /
						 model->magicLampWaveCount) * rand() / RAND_MAX;

			model->magicLampWaves[i].pos =
					(posInAvailSegment +
					 i * availPos / model->magicLampWaveCount +
					 model->magicLampWaves[i].halfWidth);

			// switch wave direction
			ampDirection *= -1;
		}
	}
	else
		model->magicLampWaveCount = 0;
}

static void
fxMagicLampModelStepObject(CompWindow * w,
						   Model * model,
						   Object * object,
						   float forwardProgress)
{
	ANIM_WINDOW(w);

	float iconCloseEndY;
	float iconFarEndY;
	float winFarEndY;
	float winVisibleCloseEndY;

	if (aw->minimizeToTop)
	{
		iconFarEndY = aw->icon.y;
		iconCloseEndY = aw->icon.y + aw->icon.height;
		winFarEndY = WIN_Y(w) + WIN_H(w);
		winVisibleCloseEndY = WIN_Y(w);
		if (winVisibleCloseEndY < iconCloseEndY)
			winVisibleCloseEndY = iconCloseEndY;
	}
	else
	{
		iconFarEndY = aw->icon.y + aw->icon.height;
		iconCloseEndY = aw->icon.y;
		winFarEndY = WIN_Y(w);
		winVisibleCloseEndY = WIN_Y(w) + WIN_H(w);
		if (winVisibleCloseEndY > iconCloseEndY)
			winVisibleCloseEndY = iconCloseEndY;
	}

	float preShapePhaseEnd = 0.22f;
	float stretchPhaseEnd =
			preShapePhaseEnd + (1 - preShapePhaseEnd) *
			(iconCloseEndY -
			 winVisibleCloseEndY) / ((iconCloseEndY - winFarEndY) +
									 (iconCloseEndY - winVisibleCloseEndY));
	if (stretchPhaseEnd < preShapePhaseEnd + 0.1)
		stretchPhaseEnd = preShapePhaseEnd + 0.1;

	float origx = w->attrib.x + (WIN_W(w) * object->gridPosition.x -
								 w->output.left) * model->scale.x;
	float origy = w->attrib.y + (WIN_H(w) * object->gridPosition.y -
								 w->output.top) * model->scale.y;

	float iconShadowLeft =
			((float)(w->output.left - w->input.left)) * 
			aw->icon.width / w->width;
	float iconShadowRight =
			((float)(w->output.right - w->input.right)) * 
			aw->icon.width / w->width;
	float iconx =
			(aw->icon.x - iconShadowLeft) + 
			(aw->icon.width + iconShadowLeft + iconShadowRight) *
			object->gridPosition.x;
	float icony = aw->icon.y + aw->icon.height * object->gridPosition.y;

	float stretchedPos;

	if (aw->minimizeToTop)
		stretchedPos =
			object->gridPosition.y * origy +
			(1 - object->gridPosition.y) * icony;
	else
		stretchedPos =
			(1 - object->gridPosition.y) * origy +
			object->gridPosition.y * icony;

	if (forwardProgress < preShapePhaseEnd)
	{
		float preShapeProgress = forwardProgress / preShapePhaseEnd;
		float stretchProgress =	forwardProgress / stretchPhaseEnd;
		float fx = (iconCloseEndY - object->position.y) / 
			       (iconCloseEndY - winFarEndY);
		float fy = (sigmoid(fx) - sigmoid(0)) / (sigmoid(1) - sigmoid(0));
		float targetx = fy * (origx - iconx) + iconx;

		int i;
		for (i = 0; i < model->magicLampWaveCount; i++)
		{
			float cosfx =
					(fx - model->magicLampWaves[i].pos) /
					model->magicLampWaves[i].halfWidth;
			if (cosfx < -1 || cosfx > 1)
				continue;
			targetx +=
					model->magicLampWaves[i].amp * model->scale.x *
					(cos(cosfx * M_PI) + 1) / 2;
		}
		preShapeProgress = 1 - decelerateProgress(1 - preShapeProgress);
		object->position.x =
				(1 - preShapeProgress) * origx + preShapeProgress * targetx;
		object->position.y =
				(1 - stretchProgress) * origy +
				stretchProgress * stretchedPos;
	}
	else
	{
		if (forwardProgress < stretchPhaseEnd)
		{
			float stretchProgress =	forwardProgress / stretchPhaseEnd;

			object->position.y =
					(1 - stretchProgress) * origy +
					stretchProgress * stretchedPos;
		}
		else
		{
			float postStretchProgress =
				(forwardProgress - stretchPhaseEnd) / (1 - stretchPhaseEnd);

			object->position.y =
					(1 - postStretchProgress) *
					stretchedPos +
					postStretchProgress *
					(stretchedPos + (iconCloseEndY - winFarEndY));
		}
		float fx =
				(iconCloseEndY - object->position.y) / (iconCloseEndY -
														winFarEndY);
		float fy = (sigmoid(fx) - sigmoid(0)) / (sigmoid(1) - sigmoid(0));

		int i;

		object->position.x = fy * (origx - iconx) + iconx;
		for (i = 0; i < model->magicLampWaveCount; i++)
		{
			float cosfx =
					(fx -
					 model->magicLampWaves[i].pos) /
					model->magicLampWaves[i].halfWidth;
			if (cosfx < -1 || cosfx > 1)
				continue;
			object->position.x +=
					model->magicLampWaves[i].amp * model->scale.x *
					(cos(cosfx * M_PI) + 1) / 2;
		}
	}
	if (aw->minimizeToTop)
	{
		if (object->position.y < iconFarEndY)
			object->position.y = iconFarEndY;
	}
	else
	{
		if (object->position.y > iconFarEndY)
			object->position.y = iconFarEndY;
	}
}

Bool fxMagicLampModelStep(CompScreen * s, CompWindow * w, float time)
{
	if (!defaultAnimStep(s, w, time))
		return FALSE;

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	if ((aw->curWindowEvent == WindowEventCreate ||
		 aw->curWindowEvent == WindowEventClose) &&
		((aw->curAnimEffect == AnimEffectMagicLamp &&
		  as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_MOVING_END].value.b) ||
		 (aw->curAnimEffect == AnimEffectVacuum &&
		  as->opt[ANIM_SCREEN_OPTION_VACUUM_MOVING_END].value.b)))
	{
		// Update icon position
		getMousePointerXY(s, &aw->icon.x, &aw->icon.y);
	}
	float forwardProgress = defaultAnimProgress(aw);

	int i;
	for (i = 0; i < model->numObjects; i++)
		fxMagicLampModelStepObject(w, model, &model->objects[i],
								   forwardProgress);

	modelCalcBounds(model);
	return TRUE;
}
