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
#include "animation_tex.h"

// =====================  Effect: Beam Up  =========================

void fxBeamUpInit(CompScreen * s, CompWindow * w)
{
	int particles = WIN_W(w);

	defaultAnimInit(s, w);
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);
	if (!aw->numPs)
	{
		aw->ps = calloc(1, 2 * sizeof(ParticleSystem));
		if (!aw->ps)
		{
			postAnimationCleanup(w, TRUE);
			return;
		}
		aw->numPs = 2;
	}
	initParticles(particles / 10, &aw->ps[0]);
	initParticles(particles, &aw->ps[1]);
	aw->ps[1].slowdown = as->opt[ANIM_SCREEN_OPTION_BEAMUP_SLOWDOWN].value.f;
	aw->ps[1].darken = 0.5;
	aw->ps[1].blendMode = GL_ONE;

	aw->ps[0].slowdown =
			as->opt[ANIM_SCREEN_OPTION_BEAMUP_SLOWDOWN].value.f / 2.0;
	aw->ps[0].darken = 0.0;
	aw->ps[0].blendMode = GL_ONE_MINUS_SRC_ALPHA;

	if (!aw->ps[0].tex)
		glGenTextures(1, &aw->ps[0].tex);
	glBindTexture(GL_TEXTURE_2D, aw->ps[0].tex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0,
				 GL_RGBA, GL_UNSIGNED_BYTE, fireTex);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (!aw->ps[1].tex)
		glGenTextures(1, &aw->ps[1].tex);
	glBindTexture(GL_TEXTURE_2D, aw->ps[1].tex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0,
				 GL_RGBA, GL_UNSIGNED_BYTE, fireTex);
	glBindTexture(GL_TEXTURE_2D, 0);

}

static void
fxBeamUpGenNewFire(CompScreen * s, ParticleSystem * ps, int x, int y,
				   int width, int height, float size, float time)
{
	ANIM_SCREEN(s);

	ps->numParticles =
			width / as->opt[ANIM_SCREEN_OPTION_BEAMUP_SPACING].value.i;
	float max_new =
			ps->numParticles * (time / 50) * (1.05 -
											  as->
											  opt
											  [ANIM_SCREEN_OPTION_BEAMUP_LIFE].
											  value.f);
	int i;
	Particle *part;
	float rVal;

	for (i = 0; i < ps->numParticles && max_new > 0; i++)
	{
		part = &ps->particles[i];
		if (part->life <= 0.0f)
		{
			// give gt new life
			rVal = (float)(random() & 0xff) / 255.0;
			part->life = 1.0f;
			part->fade = (rVal * (1 - as->opt[ANIM_SCREEN_OPTION_BEAMUP_LIFE].value.f)) + 
				         (0.2f * (1.01 - as->opt[ANIM_SCREEN_OPTION_BEAMUP_LIFE].value.f));	// Random Fade Value

			// set size
			part->width = 2.5 * as->opt[ANIM_SCREEN_OPTION_BEAMUP_SIZE].value.f;
			part->height = height;
			part->w_mod = size * 0.2;
			part->h_mod = size * 0.02;

			// choose random x position
			rVal = (float)(random() & 0xff) / 255.0;
			part->x = x + ((width > 1) ? (rVal * width) : 0);
			part->y = y;
			part->z = 0.0;
			part->xo = part->x;
			part->yo = part->y;
			part->zo = part->z;

			// set speed and direction
			part->xi = 0.0f;
			part->yi = 0.0f;
			part->zi = 0.0f;

			// set color ABAB as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.f
			part->r = (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[0] / 0xffff -
					  (rVal / 1.7 * (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[0] / 0xffff);
			part->g = (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[1] / 0xffff -
					  (rVal / 1.7 * (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[1] / 0xffff);
			part->b = (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[2] / 0xffff -
					  (rVal / 1.7 * (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[2] / 0xffff);
			part->a = (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[3] / 0xffff;

			// set gravity
			part->xg = 0.0f;
			part->yg = 0.0f;
			part->zg = 0.0f;

			ps->active = TRUE;
			max_new -= 1;
		}
		else
		{
			part->xg = (part->x < part->xo) ? 1.0 : -1.0;
		}
	}

}

void fxBeamUpModelStep(CompScreen * s, CompWindow * w, float time)
{
	int steps;
	int creating = 0;

	defaultAnimStep(s, w, time);

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP_INTENSE].value.i);
	float old = 1 - (aw->animRemainingTime) / (aw->animTotalTime);
	float stepSize;

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;
	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	aw->animRemainingTime -= timestep;
	if (aw->animRemainingTime <= 0)
		aw->animRemainingTime = 0;	// avoid sub-zero values
	float new = 1 - (aw->animRemainingTime) / (aw->animTotalTime);

	stepSize = new - old;

	if (aw->curWindowEvent == WindowEventCreate ||
		aw->curWindowEvent == WindowEventUnminimize ||
		aw->curWindowEvent == WindowEventUnshade)
	{
		old = 1 - old;
		new = 1 - new;
		creating = 1;
	}

	if (!aw->drawRegion)
		aw->drawRegion = XCreateRegion();
	if (aw->animRemainingTime > 0)
	{
		XRectangle rect;

		rect.x = ((old / 2) * WIN_W(w));
		rect.width = WIN_W(w) - (old * WIN_W(w));
		rect.y = ((old / 2) * WIN_H(w));
		rect.height = WIN_H(w) - (old * WIN_H(w));
		XUnionRectWithRegion(&rect, &emptyRegion, aw->drawRegion);
	}
	else
	{
		XUnionRegion(&emptyRegion, &emptyRegion, aw->drawRegion);
	}
	if (new != 0)
		aw->useDrawRegion = TRUE;
	else
		aw->useDrawRegion = FALSE;

	if (aw->animRemainingTime > 0 && aw->ps)
	{
		fxBeamUpGenNewFire(s, &aw->ps[1], 
			WIN_X(w), WIN_Y(w) + (WIN_H(w) / 2), WIN_W(w),
			creating ? WIN_H(w) - (old / 2 * WIN_H(w)) : 
			           (WIN_H(w) -   (old * WIN_H(w))),
		    WIN_W(w) / 40.0, time);

	}
	if (aw->animRemainingTime <= 0 && aw->numPs
		&& (aw->ps[0].active || aw->ps[1].active))
		aw->animRemainingTime = timestep;

	if (!aw->numPs)
	{
		if (aw->ps)
		{
			finiParticles(aw->ps);
			free(aw->ps);
			aw->ps = NULL;
		}
		return;					// FIXME - is this correct behaviour?
	}

	int i;
	Particle *part;

	aw->ps[0].x = WIN_X(w);
	aw->ps[0].y = WIN_Y(w);

	for (i = 0; i < aw->ps[1].numParticles && aw->animRemainingTime > 0; i++)
	{
		part = &aw->ps[1].particles[i];
		part->xg = (part->x < part->xo) ? 1.0 : -1.0;
	}
	aw->ps[1].x = WIN_X(w);
	aw->ps[1].y = WIN_Y(w);

	modelCalcBounds(model);
}

void
fxBeamupUpdateWindowAttrib(AnimScreen * as,
						   AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	float forwardProgress = 0;
	if (aw->animTotalTime - aw->timestep != 0)
		forwardProgress =
			1 - (aw->animRemainingTime - aw->timestep) /
			(aw->animTotalTime - aw->timestep);
	forwardProgress = MIN(forwardProgress, 1);
	forwardProgress = MAX(forwardProgress, 0);

	if (aw->curWindowEvent == WindowEventCreate ||
		aw->curWindowEvent == WindowEventUnminimize)
	{
		forwardProgress = forwardProgress * forwardProgress;
		forwardProgress = forwardProgress * forwardProgress;
		forwardProgress = 1 - forwardProgress;
	}

	wAttrib->opacity = (GLushort) (aw->storedOpacity * (1 - forwardProgress));
}

