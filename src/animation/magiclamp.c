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
fxMagicLampInitGrid(AnimScreen *as, AnimWindow *aw,
		    int *gridWidth, int *gridHeight)
{
    *gridWidth = 2;
    *gridHeight = animGetI(as, aw, ANIM_SCREEN_OPTION_MAGIC_LAMP_GRID_RES);
}
void
fxVacuumInitGrid(AnimScreen * as, AnimWindow *aw,
		 int *gridWidth, int *gridHeight)
{
    *gridWidth = 2;
    *gridHeight = animGetI(as, aw, ANIM_SCREEN_OPTION_VACUUM_GRID_RES);
}

void fxMagicLampInit(CompScreen * s, CompWindow * w)
{
    ANIM_SCREEN(s);
    ANIM_WINDOW(w);

    int screenHeight = s->height;
    aw->minimizeToTop = (WIN_Y(w) + WIN_H(w) / 2) >
	(aw->icon.y + aw->icon.height / 2);
    int maxWaves;
    float waveAmpMin, waveAmpMax;
    float distance;

    if (aw->curAnimEffect == AnimEffectMagicLamp)
    {
	maxWaves = animGetI(as, aw, ANIM_SCREEN_OPTION_MAGIC_LAMP_MAX_WAVES);
	waveAmpMin =
	    animGetF(as, aw, ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MIN);
	waveAmpMax =
	    animGetF(as, aw, ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MAX);
    }
    else
    {
	maxWaves = 0;
	waveAmpMin = 0;
	waveAmpMax = 0;
    }
    if (waveAmpMax < waveAmpMin)
	waveAmpMax = waveAmpMin;

    if (maxWaves == 0)
    {
	aw->magicLampWaveCount = 0;
	return;
    }

    // Initialize waves

    if (aw->minimizeToTop)
	distance = WIN_Y(w) + WIN_H(w) - aw->icon.y;
    else
	distance = aw->icon.y - WIN_Y(w);

    aw->magicLampWaveCount =
	1 + (float)maxWaves *distance / screenHeight;

    if (!(aw->magicLampWaves))
    {
	aw->magicLampWaves =
	    calloc(aw->magicLampWaveCount, sizeof(WaveParam));
	if (!aw->magicLampWaves)
	{
	    compLogMessage (w->screen->display, "animation", CompLogLevelError,
			    "Not enough memory");
	    return;
	}
    }
    // Compute wave parameters

    int ampDirection = (RAND_FLOAT() < 0.5 ? 1 : -1);
    int i;
    float minHalfWidth = 0.22f;
    float maxHalfWidth = 0.38f;

    for (i = 0; i < aw->magicLampWaveCount; i++)
    {
	aw->magicLampWaves[i].amp =
	    ampDirection * (waveAmpMax - waveAmpMin) *
	    rand() / RAND_MAX + ampDirection * waveAmpMin;
	aw->magicLampWaves[i].halfWidth =
	    RAND_FLOAT() * (maxHalfWidth -
			    minHalfWidth) + minHalfWidth;

	// avoid offset at top and bottom part by added waves
	float availPos = 1 - 2 * aw->magicLampWaves[i].halfWidth;
	float posInAvailSegment = 0;

	if (i > 0)
	    posInAvailSegment =
		(availPos / aw->magicLampWaveCount) * rand() / RAND_MAX;

	aw->magicLampWaves[i].pos =
	    (posInAvailSegment +
	     i * availPos / aw->magicLampWaveCount +
	     aw->magicLampWaves[i].halfWidth);

	// switch wave direction
	ampDirection *= -1;
    }
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

    // Compute current y position
    if (forwardProgress < preShapePhaseEnd)
    {
	float stretchProgress =	forwardProgress / stretchPhaseEnd;
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
    }

    // Compute "target shape" x position
    float fx = ((iconCloseEndY - object->position.y) / 
		(iconCloseEndY - winFarEndY));
    float fy = ((sigmoid(fx) - sigmoid(0)) /
		(sigmoid(1) - sigmoid(0)));
    float targetx = fy * (origx - iconx) + iconx;

    // Apply waves
    int i;
    for (i = 0; i < aw->magicLampWaveCount; i++)
    {
	float cosfx = ((fx - aw->magicLampWaves[i].pos) /
		       aw->magicLampWaves[i].halfWidth);
	if (cosfx < -1 || cosfx > 1)
	    continue;
	targetx +=
	    aw->magicLampWaves[i].amp * model->scale.x *
	    (cos(cosfx * M_PI) + 1) / 2;
    }

    // Compute current x position
    if (forwardProgress < preShapePhaseEnd)
    {
	float preShapeProgress = forwardProgress / preShapePhaseEnd;

	// Slow down "shaping" toward the end
	preShapeProgress = 1 - decelerateProgress(1 - preShapeProgress);

	object->position.x =
	    (1 - preShapeProgress) * origx + preShapeProgress * targetx;
    }
    else	    
	object->position.x = targetx;

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

void
fxMagicLampModelStep (CompScreen *s, CompWindow *w, float time)
{
    defaultAnimStep (s, w, time);

    ANIM_SCREEN(s);
    ANIM_WINDOW(w);

    Model *model = aw->model;

    if ((aw->curWindowEvent == WindowEventOpen ||
	 aw->curWindowEvent == WindowEventClose) &&
	((aw->curAnimEffect == AnimEffectMagicLamp &&
	  animGetB(as, aw, ANIM_SCREEN_OPTION_MAGIC_LAMP_MOVING_END)) ||
	 (aw->curAnimEffect == AnimEffectVacuum &&
	  animGetB(as, aw, ANIM_SCREEN_OPTION_VACUUM_MOVING_END))))
    {
	// Update icon position
	getMousePointerXY(s, &aw->icon.x, &aw->icon.y);
    }
    float forwardProgress = defaultAnimProgress(aw);

    if (aw->magicLampWaveCount > 0 && !aw->magicLampWaves)
	return;

    int i;
    for (i = 0; i < model->numObjects; i++)
	fxMagicLampModelStepObject(w, model, &model->objects[i],
				   forwardProgress);
}
