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

/*
 * TODO:
 *
 * - Auto direction option: Close in opposite direction of opening
 * - Proper side surface normals for lighting
 * - decoration shadows
 *   - shadow quad generation
 *   - shadow texture coords (from clip tex. matrices)
 *   - draw shadows
 *   - fade in shadows
 *
 * - Voronoi tessellation
 * - Brick tessellation
 * - Triangle tessellation
 * - Hexagonal tessellation
 *
 * Effects:
 * - Circular action for tornado type fx
 * - Tornado 3D (especially for minimize)
 * - Helix 3D (hor. strips descend while they rotate and fade in)
 * - Glass breaking 3D
 *   - Gaussian distr. points (for gradually increasing polygon size
 *                           starting from center or near mouse pointer)
 *   - Drawing cracks
 *   - Gradual cracking
 *
 * - fix slowness during transparent cube with <100 opacity
 * - fix occasional wrong side color in some windows
 * - fix on top windows and panels
 *   (These two only matter for viewing during Rotate Cube.
 *    All windows should be painted with depth test on
 *    like 3d-plugin does)
 * - play better with rotate (fix cube face drawn on top of polygons
 *   after 45 deg. rotation)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "animation-internal.h"

int animDisplayPrivateIndex;
CompMetadata animMetadata;

/*
 * NOTE:
 * Be sure to always update the NUM_EFFECT_TYPE
 * macro definitions in animation-internal.h whenever
 * elements of these arrays are added or removed.
 */

static AnimEffect minimizeEffectType[] = {
	AnimEffectNone,
	AnimEffectRandom,
	AnimEffectBeamUp,
	AnimEffectBurn,
	AnimEffectCurvedFold,
	AnimEffectDomino3D,
	AnimEffectDream,
	AnimEffectExplode3D,
	AnimEffectFade,
	AnimEffectGlide3D1,
	AnimEffectGlide3D2,
	AnimEffectHorizontalFolds,
	AnimEffectLeafSpread3D,
	AnimEffectMagicLamp,
	AnimEffectRazr3D,
	AnimEffectSidekick,
	AnimEffectZoom
};

static AnimEffect closeEffectType[] = {
	AnimEffectNone,
	AnimEffectRandom,
	AnimEffectBeamUp,
	AnimEffectBurn,
	AnimEffectCurvedFold,
	AnimEffectDomino3D,
	AnimEffectDream,
	AnimEffectExplode3D,
	AnimEffectFade,
	AnimEffectGlide3D1,
	AnimEffectGlide3D2,
	AnimEffectHorizontalFolds,
	AnimEffectLeafSpread3D,
	AnimEffectMagicLamp,
	AnimEffectMagicLampVacuum,
	AnimEffectRazr3D,
	AnimEffectSidekick,
	AnimEffectWave,
	AnimEffectZoom
};

static AnimEffect focusEffectType[] = {
	AnimEffectNone,
	AnimEffectDodge,
	AnimEffectFocusFade,
	AnimEffectWave
};

static AnimEffect shadeEffectType[] = {
	AnimEffectNone,
	AnimEffectRandom,
	AnimEffectCurvedFold,
	AnimEffectHorizontalFolds,
	AnimEffectRollUp
};


// iterate over given list
// check if given effect name matches any implemented effect
// Check if it was already in the stored list
// if not, store the effect
// if no valid effect is given, use the default effect

void defaultAnimInit(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	// store window opacity
	aw->storedOpacity = w->paint.opacity;

	aw->timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);
}

static Bool
defaultLetOthersDrawGeoms (CompScreen *s, CompWindow *w)
{
	return TRUE;
}

static void
animStoreRandomEffectList (CompOptionValue *value,
						   AnimEffect *allowedEffects,
						   unsigned int numAllowedEffects,
						   AnimEffect *targetList,
						   unsigned int *targetCount)
{
	CompOptionValue *effect = value->list.value;
	AnimEffect listEffect;
	int nItems = value->list.nValue;
	int i, j, count;

	count = 0;

	for (i = 0; i < nItems; i++, effect++)
	{
		if ((effect->i < 0) || (effect->i >= numAllowedEffects))
			continue;

		listEffect = allowedEffects[effect->i];

		for (j = 0; j < count; j++)
		{
			if (targetList[j] == listEffect)
				break;
		}

		if (j < count)
			continue;

		targetList[count] = listEffect;
		count++;
	}

	*targetCount = count;
}

static inline AnimEffect
animGetAnimEffect (AnimEffect effect,
				   AnimEffect *randomEffects,
				   unsigned int nRandomEffects,
				   Bool allRandom)
{
	if ((effect == AnimEffectRandom) || allRandom)
	{
		if (nRandomEffects == 0)
			return AnimEffectNone;
		else
		{
			unsigned int index;
			index = (unsigned int)(nRandomEffects * (double)rand() / RAND_MAX);
			return randomEffects[index];
		}
	}
	else
		return effect;
}

void modelCalcBounds(Model * model)
{
	int i;

	model->topLeft.x = MAXSHORT;
	model->topLeft.y = MAXSHORT;
	model->bottomRight.x = MINSHORT;
	model->bottomRight.y = MINSHORT;

	for (i = 0; i < model->numObjects; i++)
	{
		if (model->objects[i].position.x < model->topLeft.x)
			model->topLeft.x = model->objects[i].position.x;
		else if (model->objects[i].position.x > model->bottomRight.x)
			model->bottomRight.x = model->objects[i].position.x;

		if (model->objects[i].position.y < model->topLeft.y)
			model->topLeft.y = model->objects[i].position.y;
		else if (model->objects[i].position.y > model->bottomRight.y)
			model->bottomRight.y = model->objects[i].position.y;
	}
}

// Converts animation direction string to an integer direction
// (up, down, left, or right)
AnimDirection getAnimationDirection(CompWindow * w,
										   CompOptionValue *value, 
										   Bool openDir)
{
	ANIM_WINDOW(w);

	AnimDirection dir;

	dir = value->i;

	if (dir == AnimDirectionRandom)
	{
		dir = rand() % 4;
	}
	else if (dir == AnimDirectionAuto)
	{
		// away from icon
		int centerX = BORDER_X(w) + BORDER_W(w) / 2;
		int centerY = BORDER_Y(w) + BORDER_H(w) / 2;
		float relDiffX = ((float)centerX - aw->icon.x) / BORDER_W(w);
		float relDiffY = ((float)centerY - aw->icon.y) / BORDER_H(w);

		if (openDir)
		{
			if (aw->curWindowEvent == WindowEventMinimize ||
				aw->curWindowEvent == WindowEventUnminimize)
				// min/unmin. should always result in +/- y direction
				dir = aw->icon.y < w->screen->height - aw->icon.y ?
						AnimDirectionDown : AnimDirectionUp;
			else if (fabs(relDiffY) > fabs(relDiffX))
				dir = relDiffY > 0 ? AnimDirectionDown : AnimDirectionUp;
			else
				dir = relDiffX > 0 ? AnimDirectionRight : AnimDirectionLeft;
		}
		else
		{
			if (aw->curWindowEvent == WindowEventMinimize ||
				aw->curWindowEvent == WindowEventUnminimize)
				// min/unmin. should always result in +/- y direction
				dir = aw->icon.y < w->screen->height - aw->icon.y ?
						AnimDirectionUp : AnimDirectionDown;
			else if (fabs(relDiffY) > fabs(relDiffX))
				dir = relDiffY > 0 ? AnimDirectionUp : AnimDirectionDown;
			else
				dir = relDiffX > 0 ? AnimDirectionLeft : AnimDirectionRight;
		}
	}
	return dir;
}

float defaultAnimProgress(AnimWindow * aw)
{
	float forwardProgress =
		1 - aw->animRemainingTime /	(aw->animTotalTime - aw->timestep);
	forwardProgress = MIN(forwardProgress, 1);
	forwardProgress = MAX(forwardProgress, 0);

	if (aw->curWindowEvent == WindowEventCreate ||
		aw->curWindowEvent == WindowEventUnminimize ||
		aw->curWindowEvent == WindowEventUnshade ||
		aw->curWindowEvent == WindowEventFocus)
		forwardProgress = 1 - forwardProgress;

	return forwardProgress;
}

float sigmoidAnimProgress(AnimWindow * aw)
{
	float forwardProgress =
		1 - aw->animRemainingTime /	(aw->animTotalTime - aw->timestep);
	forwardProgress = MIN(forwardProgress, 1);
	forwardProgress = MAX(forwardProgress, 0);

	// Apply sigmoid and normalize
	forwardProgress =
		(sigmoid(forwardProgress) - sigmoid(0)) /
		(sigmoid(1) - sigmoid(0));

	if (aw->curWindowEvent == WindowEventCreate ||
		aw->curWindowEvent == WindowEventUnminimize ||
		aw->curWindowEvent == WindowEventUnshade ||
		aw->curWindowEvent == WindowEventFocus)
		forwardProgress = 1 - forwardProgress;

	return forwardProgress;
}

// Gives some acceleration (when closing a window)
// or deceleration (when opening a window)
// Applies a sigmoid with slope s,
// where minx and maxx are the
// starting and ending points on the sigmoid
float decelerateProgressCustom(float progress, float minx, float maxx)
{
	float x = 1 - progress;
	float s = 8;

	return (1 -
			((sigmoid2(minx + (x * (maxx - minx)), s) - sigmoid2(minx, s)) /
			 (sigmoid2(maxx, s) - sigmoid2(minx, s))));
}

float decelerateProgress2(float progress)
{
	return decelerateProgressCustom(progress, 0.5, 0.75);
}

Bool defaultAnimStep(CompScreen * s, CompWindow * w, float time)
{
	int steps;

	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->timestep = timestep;

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;

	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return FALSE;
	steps = MAX(1, steps);

	aw->animRemainingTime -= timestep * steps;

	// avoid sub-zero values
	aw->animRemainingTime = MAX(aw->animRemainingTime, 0);

	return TRUE;
}


AnimEffectProperties animEffectProperties[AnimEffectNum] = {
	// AnimEffectNone
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	// AnimEffectRandom
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	// AnimEffectBeamUp
	{fxBeamupUpdateWindowAttrib, 0, drawParticleSystems, fxBeamUpModelStep,
	 fxBeamUpInit, 0, 0, 0, 1, 0, 0, 0, 0},
	// AnimEffectBurn
	{0, 0, drawParticleSystems, fxBurnModelStep, fxBurnInit, 0, 0, 0, 1, 0,
	 0, 0, 0},
	// AnimEffectCurvedFold
	{0, 0, 0, fxCurvedFoldModelStep, 0, fxMagicLampInitGrid, 0, 0, 0, 0, 0,
	 0, 0},
	// AnimEffectDodge
	{0, 0, 0, fxDodgeAnimStep, defaultAnimInit, 0, 0, 0, 0, 0,
	 defaultLetOthersDrawGeoms,
	 fxDodgeUpdateWindowTransform, fxDodgePostPreparePaintScreen},
	// AnimEffectDomino3D
	{0, polygonsPrePaintWindow, polygonsPostPaintWindow, polygonsAnimStep,
	 fxDomino3DInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsLinearAnimStepPolygon, 0, 0, 0},
	// AnimEffectDream
	{fxDreamUpdateWindowAttrib, 0, 0, fxDreamModelStep, defaultAnimInit,
	 fxMagicLampInitGrid, 0, 0, 0, 0, 0, 0, 0},
	// AnimEffectExplode3D
	{0, polygonsPrePaintWindow, polygonsPostPaintWindow, polygonsAnimStep,
	 fxExplode3DInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsLinearAnimStepPolygon, 0, 0, 0},
	// AnimEffectFade
	{fxFadeUpdateWindowAttrib, 0, 0, defaultAnimStep, defaultAnimInit, 0, 0,
	 0, 0, 0, defaultLetOthersDrawGeoms, 0, 0},
	// AnimEffectFocusFade
	{fxFocusFadeUpdateWindowAttrib, 0, 0, defaultAnimStep, defaultAnimInit,
	 0, 0, 0, 0, 0, defaultLetOthersDrawGeoms, 0, 0},
	// AnimEffectGlide3D1
	{fxGlideUpdateWindowAttrib, fxGlidePrePaintWindow,
	 fxGlidePostPaintWindow, fxGlideAnimStep,
	 fxGlideInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsDeceleratingAnimStepPolygon,
	 fxGlideLetOthersDrawGeoms, fxGlideUpdateWindowTransform, 0},
	// AnimEffectGlide3D2
	{fxGlideUpdateWindowAttrib, fxGlidePrePaintWindow,
	 fxGlidePostPaintWindow, fxGlideAnimStep,
	 fxGlideInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsDeceleratingAnimStepPolygon,
	 fxGlideLetOthersDrawGeoms, fxGlideUpdateWindowTransform, 0},
	// AnimEffectHorizontalFolds
	{0, 0, 0, fxHorizontalFoldsModelStep, 0, fxHorizontalFoldsInitGrid,
	 0, 0, 0, 0, 0, 0, 0},
	// AnimEffectLeafSpread3D
	{0, polygonsPrePaintWindow, polygonsPostPaintWindow, polygonsAnimStep,
	 fxLeafSpread3DInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsLinearAnimStepPolygon, 0, 0, 0},
	// AnimEffectMagicLamp
	{0, 0, 0, fxMagicLampModelStep, fxMagicLampInit, fxMagicLampInitGrid,
	 0, 0, 0, 0, 0, 0, 0},
	// AnimEffectMagicLampVacuum
	{0, 0, 0, fxMagicLampModelStep, fxMagicLampInit,
	 fxMagicLampVacuumInitGrid, 0, 0, 0, 0, 0, 0, 0},
	// AnimEffectRazr3D
	{0, polygonsPrePaintWindow, polygonsPostPaintWindow, polygonsAnimStep,
	 fxDomino3DInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsLinearAnimStepPolygon, 0, 0, 0},
	// AnimEffectRollUp
	{0, 0, 0, fxRollUpModelStep, 0, fxRollUpInitGrid, 0, 0, 1, 0, 0, 0, 0},
	// AnimEffectSidekick
	{fxZoomUpdateWindowAttrib, 0, 0, defaultAnimStep, fxSidekickInit,
	 0, 0, 0, 1, 0, defaultLetOthersDrawGeoms, fxZoomUpdateWindowTransform,
	 0},
	// AnimEffectWave
	{0, 0, 0, fxWaveModelStep, 0, fxMagicLampInitGrid, 0, 0, 0, 0, 0, 0, 0},
	// AnimEffectZoom
	{fxZoomUpdateWindowAttrib, 0, 0, defaultAnimStep, defaultAnimInit,
	 0, 0, 0, 1, 0, defaultLetOthersDrawGeoms, fxZoomUpdateWindowTransform,
	 0}
};


static Bool getMousePointerXY(CompScreen * s, short *x, short *y)
{
	Window w1, w2;
	int xp, yp, xj, yj;
	unsigned int m;

	if (XQueryPointer
		(s->display->display, s->root, &w1, &w2, &xj, &yj, &xp, &yp, &m))
	{
		*x = xp;
		*y = yp;
		return TRUE;
	}
	return FALSE;
}

static int animGetWindowState(CompWindow * w)
{
	Atom actual;
	int result, format;
	unsigned long n, left;
	unsigned char *data;

	result = XGetWindowProperty(w->screen->display->display, w->id,
								w->screen->display->wmStateAtom, 0L,
								1L, FALSE,
								w->screen->display->wmStateAtom,
								&actual, &format, &n, &left, &data);

	if (result == Success && n && data)
	{
		int state;

		memcpy(&state, data, sizeof(int));
		XFree((void *)data);

		return state;
	}

	return WithdrawnState;
}

static Bool
animSetScreenOptions(CompPlugin *plugin,
					CompScreen * screen,
					char *name,
					CompOptionValue * value)
{
	CompOption *o;
	int index;

	ANIM_SCREEN(screen);

	o = compFindOption(as->opt, NUM_OPTIONS(as), name, &index);
	if (!o)
		return FALSE;

	switch (index)
	{
	case ANIM_SCREEN_OPTION_MINIMIZE_EFFECT:
		if (compSetIntOption(o, value))
		{
			as->minimizeEffect = minimizeEffectType[o->value.i];
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CLOSE1_EFFECT:
		if (compSetIntOption(o, value))
		{
			as->close1Effect = closeEffectType[o->value.i];
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CLOSE2_EFFECT:
		if (compSetIntOption(o, value))
		{
			as->close2Effect = closeEffectType[o->value.i];
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CREATE1_EFFECT:
		if (compSetIntOption(o, value))
		{
			as->create1Effect = closeEffectType[o->value.i];
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CREATE2_EFFECT:
		if (compSetIntOption(o, value))
		{
			as->create2Effect = closeEffectType[o->value.i];
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_FOCUS_EFFECT:
		if (compSetIntOption(o, value))
		{
			as->focusEffect = focusEffectType[o->value.i];
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_SHADE_EFFECT:
		if (compSetIntOption(o, value))
		{
			as->shadeEffect = shadeEffectType[o->value.i];
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_MINIMIZE_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
						   minimizeEffectType + RANDOM_EFFECT_OFFSET,
						   NUM_MINIMIZE_EFFECT - RANDOM_EFFECT_OFFSET,
						   as->minimizeRandomEffects,
						   &as->nMinimizeRandomEffects);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CLOSE1_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
						   closeEffectType + RANDOM_EFFECT_OFFSET,
						   NUM_CLOSE_EFFECT - RANDOM_EFFECT_OFFSET,
						   as->close1RandomEffects,
						   &as->nClose1RandomEffects);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CLOSE2_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
						   closeEffectType + RANDOM_EFFECT_OFFSET,
						   NUM_CLOSE_EFFECT - RANDOM_EFFECT_OFFSET,
						   as->close2RandomEffects,
						   &as->nClose2RandomEffects);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CREATE1_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
						   closeEffectType + RANDOM_EFFECT_OFFSET,
						   NUM_CLOSE_EFFECT - RANDOM_EFFECT_OFFSET,
						   as->create1RandomEffects,
						   &as->nCreate1RandomEffects);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CREATE2_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
						   closeEffectType + RANDOM_EFFECT_OFFSET,
						   NUM_CLOSE_EFFECT - RANDOM_EFFECT_OFFSET,
						   as->create2RandomEffects,
						   &as->nCreate2RandomEffects);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_SHADE_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
						   shadeEffectType + RANDOM_EFFECT_OFFSET,
						   NUM_SHADE_EFFECT - RANDOM_EFFECT_OFFSET,
						   as->shadeRandomEffects,
						   &as->nShadeRandomEffects);
			return TRUE;
		}
		break;
	default:
		return compSetScreenOption (screen, o, value);
		break;
	}

	return FALSE;
}

static const CompMetadataOptionInfo animScreenOptionInfo[] = {
	{ "minimize_match", "match", 0, 0, 0 },
	{ "close1_match", "match", 0, 0, 0 },
	{ "close2_match", "match", 0, 0, 0 },
	{ "create1_match", "match", 0, 0, 0 },
	{ "create2_match", "match", 0, 0, 0 },
	{ "focus_match", "match", 0, 0, 0 },
	{ "shade_match", "match", 0, 0, 0 },
	{ "minimize_effect", "int", RESTOSTRING (0, LAST_MINIMIZE_EFFECT), 0, 0 },
	{ "minimize_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "minimize_random_effects", "list", "<type>int</type>" RESTOSTRING (0, LAST_RANDOM_MINIMIZE_EFFECT), 0, 0 },
	{ "close1_effect", "int", RESTOSTRING (0, LAST_CLOSE_EFFECT), 0, 0 },
	{ "close1_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "close1_random_effects", "list", "<type>int</type>" RESTOSTRING (0, LAST_RANDOM_CLOSE_EFFECT), 0, 0 },
	{ "create1_effect", "int", RESTOSTRING (0, LAST_CLOSE_EFFECT), 0, 0 },
	{ "create1_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "create1_random_effects", "list", "<type>int</type>" RESTOSTRING (0, LAST_RANDOM_CLOSE_EFFECT), 0, 0 },
	{ "close2_effect", "int", RESTOSTRING (0, LAST_CLOSE_EFFECT), 0, 0 },
	{ "close2_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "close2_random_effects", "list", "<type>int</type>" RESTOSTRING (0, LAST_RANDOM_CLOSE_EFFECT), 0, 0 },
	{ "create2_effect", "int", RESTOSTRING (0, LAST_CLOSE_EFFECT), 0, 0 },
	{ "create2_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "create2_random_effects", "list", "<type>int</type>" RESTOSTRING (0, LAST_RANDOM_CLOSE_EFFECT), 0, 0 },
	{ "focus_effect", "int", RESTOSTRING (0, LAST_FOCUS_EFFECT), 0, 0 },
	{ "focus_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "shade_effect", "int", RESTOSTRING (0, LAST_SHADE_EFFECT), 0, 0 },
	{ "shade_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "shade_random_effects", "list", "<type>int</type>" RESTOSTRING (0, LAST_RANDOM_SHADE_EFFECT), 0, 0 },
	{ "rollup_fixed_interior", "bool", 0, 0, 0 },
	{ "all_random", "bool", 0, 0, 0 },
	{ "time_step", "int", "<min>1</min>", 0, 0 },
	{ "time_step_intense", "int", "<min>1</min>", 0, 0 },
	{ "beam_size", "float", "<min>0.1</min>", 0, 0 },
	{ "beam_spacing", "int", "<min>1</min>", 0, 0 },
	{ "beam_color", "color", 0, 0, 0 },
	{ "beam_slowdown", "float", "<min>0.1</min>", 0, 0 },
	{ "beam_life", "float", "<min>0.1</min>", 0, 0 },
	{ "curved_fold_amp", "float", "<min>-0.5</min><max>0.5</max>", 0, 0 },
	{ "dodge_gap_ratio", "float", "<min>0.0</min><max>1.0</max>", 0, 0 },
	{ "domino_direction", "int", RESTOSTRING (0, LAST_ANIM_DIRECTION), 0, 0 },
	{ "razr_direction", "int", RESTOSTRING (0, LAST_ANIM_DIRECTION), 0, 0 },
	{ "explode_thickness", "float", "<min>0</min>", 0, 0 },
	{ "explode_gridx", "int", "<min>1</min>", 0, 0 },
	{ "explode_gridy", "int", "<min>1</min>", 0, 0 },
	{ "explode_tesselation", "int", RESTOSTRING (0, LAST_POLYGON_TESS), 0, 0 },
	{ "fire_particles", "int", "<min>0</min>", 0, 0 },
	{ "fire_size", "float", "<min>0.1</min>", 0, 0 },
	{ "fire_slowdown", "float", "<min>0.1</min>", 0, 0 },
	{ "fire_life", "float", "<min>0.1</min>", 0, 0 },
	{ "fire_color", "color", 0, 0, 0 },
	{ "fire_direction", "int", RESTOSTRING (0, LAST_ANIM_DIRECTION), 0, 0 },
	{ "fire_constant_speed", "bool", 0, 0, 0 },
	{ "fire_smoke", "bool", 0, 0, 0 },
	{ "fire_mystical", "bool", 0, 0, 0 },
	{ "glide1_away_position", "float", 0, 0, 0 },
	{ "glide1_away_angle", "float", 0, 0, 0 },
	{ "glide1_thickness", "float", "<min>0</min>", 0, 0 },
	{ "glide2_away_position", "float", 0, 0, 0 },
	{ "glide2_away_angle", "float", 0, 0, 0 },
	{ "glide2_thickness", "float", "<min>0</min>", 0, 0 },
	{ "horizontal_folds_amp", "float", "<min>-0.5</min><max>0.5</max>", 0, 0 },
	{ "horizontal_folds_num_folds", "int", "<min>1</min>", 0, 0 },
	{ "magic_lamp_grid_res", "int", "<min>4</min>", 0, 0 },
	{ "magic_lamp_max_waves", "int", "<min>3</min>", 0, 0 },
	{ "magic_lamp_amp_min", "float", "<min>200</min>", 0, 0 },
	{ "magic_lamp_amp_max", "float", "<min>200</min>", 0, 0 },
	{ "magic_lamp_create_start_width", "int", "<min>0</min>", 0, 0 },
	{ "magic_lamp_vacuum_grid_res", "int", "<min>4</min>", 0, 0 },
	{ "magic_lamp_vacuum_max_waves", "int", "<min>0</min>", 0, 0 },
	{ "magic_lamp_vacuum_amp_min", "float", "<min>200</min>", 0, 0 },
	{ "magic_lamp_vacuum_amp_max", "float", "<min>200</min>", 0, 0 },
	{ "magic_lamp_vacuum_create_start_width", "int", "<min>0</min>", 0, 0 },
	{ "sidekick_num_rotations", "float", "<min>0</min>", 0, 0 },
	{ "sidekick_springiness", "float", "<min>0</min><max>1</max>", 0, 0 },
	{ "wave_width", "float", "<min>0</min>", 0, 0 },
	{ "wave_amp", "float", "<min>0</min>", 0, 0 },
	{ "zoom_from_center", "int", RESTOSTRING (0, LAST_ZOOM_FROM_CENTER), 0, 0 },
	{ "zoom_springiness", "float", "<min>0</min><max>1</max>", 0, 0 }
};

static CompOption *
animGetScreenOptions(CompPlugin *plugin, CompScreen * screen, int *count)
{
	ANIM_SCREEN(screen);

	*count = NUM_OPTIONS(as);
	return as->opt;
}

static void
objectInit(Object * object,
		   float positionX, float positionY,
		   float gridPositionX, float gridPositionY)
{
	object->gridPosition.x = gridPositionX;
	object->gridPosition.y = gridPositionY;

	object->position.x = positionX;
	object->position.y = positionY;

	object->offsetTexCoordForQuadBefore.x = 0;
	object->offsetTexCoordForQuadBefore.y = 0;
	object->offsetTexCoordForQuadAfter.x = 0;
	object->offsetTexCoordForQuadAfter.y = 0;
}

void
modelInitObjects(Model * model, int x, int y, int width, int height)
{
	int gridX, gridY;
	int nGridCellsX, nGridCellsY;
	float x0, y0;

	x0 = model->scaleOrigin.x;
	y0 = model->scaleOrigin.y;

	// number of grid cells in x direction
	nGridCellsX = model->gridWidth - 1;

	if (model->forWindowEvent == WindowEventShade ||
		model->forWindowEvent == WindowEventUnshade)
	{
		// number of grid cells in y direction
		nGridCellsY = model->gridHeight - 3;	// One allocated for top, one for bottom

		float winContentsHeight =
				height - model->topHeight - model->bottomHeight;

		//Top
		float objectY = y + (0 - y0) * model->scale.y + y0;

		for (gridX = 0; gridX < model->gridWidth; gridX++)
		{
			objectInit(&model->objects[gridX],
					   x + ((gridX * width / nGridCellsX) - x0) * 
					   model->scale.x + x0, objectY,
					   (float)gridX / nGridCellsX, 0);
		}

		// Window contents
		for (gridY = 1; gridY < model->gridHeight - 1; gridY++)
		{
			float inWinY =
					(gridY - 1) * winContentsHeight / nGridCellsY +
					model->topHeight;
			float gridPosY = inWinY / height;

			objectY = y + (inWinY - y0) * model->scale.y + y0;

			for (gridX = 0; gridX < model->gridWidth; gridX++)
			{
				objectInit(&model->objects[gridY * model->gridWidth + gridX],
						   x + ((gridX * width / nGridCellsX) - x0) * 
						   model->scale.x + x0,
						   objectY, (float)gridX / nGridCellsX, gridPosY);
			}
		}

		// Bottom (gridY is model->gridHeight-1 now)
		objectY = y + (height - y0) * model->scale.y + y0;

		for (gridX = 0; gridX < model->gridWidth; gridX++)
		{
			objectInit(&model->objects[gridY * model->gridWidth + gridX],
					   x + ((gridX * width / nGridCellsX) - x0) * 
					   model->scale.x + x0, objectY,
					   (float)gridX / nGridCellsX, 1);
		}
	}
	else
	{
		// number of grid cells in y direction
		nGridCellsY = model->gridHeight - 1;

		int i = 0;

		for (gridY = 0; gridY < model->gridHeight; gridY++)
		{
			float objectY =
					y + ((gridY * height / nGridCellsY) -
						 y0) * model->scale.y + y0;
			for (gridX = 0; gridX < model->gridWidth; gridX++)
			{
				objectInit(&model->objects[i],
						   x + ((gridX * width / nGridCellsX) - x0) * 
						   model->scale.x + x0,
						   objectY,
						   (float)gridX / nGridCellsX,
						   (float)gridY / nGridCellsY);
				i++;
			}
		}
	}
}

static Model *createModel(CompWindow * w,
						  WindowEvent forWindowEvent,
						  AnimEffect forAnimEffect, int gridWidth,
						  int gridHeight)
{
	int x = WIN_X(w);
	int y = WIN_Y(w);
	int width = WIN_W(w);
	int height = WIN_H(w);

	Model *model;

	model = calloc(1, sizeof(Model));
	if (!model)
	{
		compLogMessage (w->screen->display, "animation", CompLogLevelError,
						"%s: Not enough memory at line %d!",
						__FILE__, __LINE__);
		return 0;
	}
	model->magicLampWaveCount = 0;
	model->magicLampWaves = NULL;

	model->gridWidth = gridWidth;
	model->gridHeight = gridHeight;
	model->numObjects = gridWidth * gridHeight;
	model->objects = calloc(1, sizeof(Object) * model->numObjects);
	if (!model->objects)
	{
		compLogMessage (w->screen->display, "animation", CompLogLevelError,
						"%s: Not enough memory at line %d!",
						__FILE__, __LINE__);
		free(model);
		return 0;
	}

	// Store win. size to check later
	model->winWidth = width;
	model->winHeight = height;

	// For shading
	model->forWindowEvent = forWindowEvent;
	model->topHeight = w->output.top;
	model->bottomHeight = w->output.bottom;

	model->scale.x = 1.0f;
	model->scale.y = 1.0f;

	model->scaleOrigin.x = 0.0f;
	model->scaleOrigin.y = 0.0f;

	modelInitObjects(model, x, y, width, height);

	modelCalcBounds(model);

	return model;
}

static Bool
animEnsureModel(CompWindow * w,
				WindowEvent forWindowEvent, AnimEffect forAnimEffect)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(w->screen);

	int gridWidth = 2;
	int gridHeight = 2;

	if (animEffectProperties[forAnimEffect].initGridFunc)
		animEffectProperties[forAnimEffect].initGridFunc(as,
														 forWindowEvent,
														 &gridWidth,
														 &gridHeight);

	Bool isShadeUnshadeEvent =
			(forWindowEvent == WindowEventShade ||
			 forWindowEvent == WindowEventUnshade);

	Bool wasShadeUnshadeEvent = aw->model &&
			(aw->model->forWindowEvent == WindowEventShade ||
			 aw->model->forWindowEvent == WindowEventUnshade);

	if (!aw->model ||
		gridWidth != aw->model->gridWidth ||
		gridHeight != aw->model->gridHeight ||
		(isShadeUnshadeEvent != wasShadeUnshadeEvent) ||
		aw->model->winWidth != WIN_W(w) || aw->model->winHeight != WIN_H(w))
	{
		if (aw->model)
		{
			if (aw->model->magicLampWaves)
				free(aw->model->magicLampWaves);
			free(aw->model->objects);
			free(aw->model);
		}
		aw->model = createModel(w,
								forWindowEvent, forAnimEffect,
								gridWidth, gridHeight);
		if (!aw->model)
			return FALSE;
	}

	return TRUE;
}

static Bool playingPolygonEffect(AnimScreen *as, AnimWindow *aw)
{
	if (!animEffectProperties[aw->curAnimEffect].
		 addCustomGeometryFunc)
		return FALSE;

	if (!(aw->curAnimEffect == AnimEffectGlide3D1 ||
		  aw->curAnimEffect == AnimEffectGlide3D2))
		return TRUE;

	return (fxGlideIsPolygonBased(as, aw));
}

static void cleanUpParentChildChainItem(AnimScreen *as, AnimWindow *aw)
{
	if (aw->winThisIsPaintedBefore && !aw->winThisIsPaintedBefore->destroyed)
	{
		AnimWindow *aw2 =
			GET_ANIM_WINDOW(aw->winThisIsPaintedBefore, as);
		if (aw2)
			aw2->winToBePaintedBeforeThis = NULL;
	}
	aw->winThisIsPaintedBefore = NULL;
	aw->moreToBePaintedPrev = NULL;
	aw->moreToBePaintedNext = NULL;
	aw->isDodgeSubject = FALSE;
	aw->skipPostPrepareScreen = FALSE;
}

void postAnimationCleanup(CompWindow * w, Bool resetAnimation)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(w->screen);

	if (resetAnimation)
	{
		aw->curWindowEvent = WindowEventNone;
		aw->curAnimEffect = AnimEffectNone;
		aw->animOverrideProgressDir = 0;

		if (aw->model)
		{
			if (aw->model->magicLampWaves)
				free(aw->model->magicLampWaves);
			aw->model->magicLampWaves = 0;
		}
	}

	Bool thereIsUnfinishedChainElem = FALSE;

	// Look for still playing windows in parent-child chain
	CompWindow *wCur = aw->moreToBePaintedNext;
	while (wCur)
	{
		AnimWindow *awCur = GET_ANIM_WINDOW(wCur, as);

		if (awCur->animRemainingTime > 0)
		{
			thereIsUnfinishedChainElem = TRUE;
			break;
		}
		wCur = awCur->moreToBePaintedNext;
	}
	if (!thereIsUnfinishedChainElem)
	{
		wCur = aw->moreToBePaintedPrev;
		while (wCur)
		{
			AnimWindow *awCur = GET_ANIM_WINDOW(wCur, as);

			if (awCur->animRemainingTime > 0)
			{
				thereIsUnfinishedChainElem = TRUE;
				break;
			}
			wCur = awCur->moreToBePaintedPrev;
		}
	}
	if (!thereIsUnfinishedChainElem)
	{
		// Finish off all windows in parent-child chain
		CompWindow *wCur = aw->moreToBePaintedNext;
		while (wCur)
		{
			AnimWindow *awCur = GET_ANIM_WINDOW(wCur, as);
			wCur = awCur->moreToBePaintedNext;
			cleanUpParentChildChainItem(as, awCur);
		}
		wCur = w;
		while (wCur)
		{
			AnimWindow *awCur = GET_ANIM_WINDOW(wCur, as);
			wCur = awCur->moreToBePaintedPrev;
			cleanUpParentChildChainItem(as, awCur);
		}
	}

	aw->state = aw->newState;

	if (aw->drawRegion)
		XDestroyRegion(aw->drawRegion);
	aw->drawRegion = NULL;
	aw->useDrawRegion = FALSE;

	if (aw->numPs)
	{
		int i = 0;

		for (i = 0; i < aw->numPs; i++)
			finiParticles(aw->ps + i);
		free(aw->ps);
		aw->ps = NULL;
		aw->numPs = 0;
	}

	if (aw->polygonSet)
	{
		freePolygonSet(aw);
		//aw->polygonSet->nClips = 0;
	}
	aw->animInitialized = FALSE;
	aw->remainderSteps = 0;

	// Reset dodge parameters
	aw->dodgeMaxAmount = 0;
	if (!(aw->moreToBePaintedPrev ||
		  aw->moreToBePaintedNext))
	{
		aw->isDodgeSubject = FALSE;
		aw->skipPostPrepareScreen = FALSE;
	}

	if (aw->restackInfo)
	{
		free(aw->restackInfo);
		aw->restackInfo = NULL;
	}

	//if (aw->unmapCnt || aw->destroyCnt)
	//    releaseWindow (w);
	while (aw->unmapCnt)
	{
		unmapWindow(w);
		aw->unmapCnt--;
	}
	while (aw->destroyCnt)
	{
		destroyWindow(w);
		aw->destroyCnt--;
	}
}

static inline Bool
isWinVisible(CompWindow *w)
{
	return (!w->destroyed &&
			!(!w->shaded &&
			  (w->attrib.map_state != IsViewable)));
}

static inline void
getHostedOnWin (AnimScreen *as,
				CompWindow *w,
				CompWindow *wHost)
{
	ANIM_WINDOW(w);
	AnimWindow *awHost = GET_ANIM_WINDOW(wHost, as);
	awHost->winToBePaintedBeforeThis = w;
	aw->winThisIsPaintedBefore = wHost;
}

static void
initiateFocusAnimation(CompWindow *w)
{
	CompScreen *s = w->screen;
	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	if (aw->curWindowEvent != WindowEventNone ||
		as->scaleActive || as->switcherActive || 
		as->groupTabChangeActive)
	{
		if (aw->restackInfo)
		{
			free(aw->restackInfo);
			aw->restackInfo = NULL;
		}
		return;
	}

	if (matchEval (&as->opt[ANIM_SCREEN_OPTION_FOCUS_MATCH].value.match, w) &&
		as->focusEffect &&
		// On unminimization, focus event is fired first.
		// When this happens and minimize is in progress,
		// don't prevent rewinding of minimize when unminimize is fired
		// right after this focus event.
		aw->curWindowEvent != WindowEventMinimize &&
		animEnsureModel(w, WindowEventFocus, as->focusEffect))
	{
		CompWindow *wStart = NULL;
		CompWindow *wEnd = NULL;
		CompWindow *wOldAbove = NULL;

		RestackInfo *restackInfo = aw->restackInfo;
		Bool raised = TRUE;

		if (restackInfo)
		{
			wStart = restackInfo->wStart;
			wEnd = restackInfo->wEnd;
			wOldAbove = restackInfo->wOldAbove;
			raised = restackInfo->raised;
		}

		if (as->focusEffect == AnimEffectFocusFade ||
			as->focusEffect == AnimEffectDodge)
		{
			// Find union region of all windows that will be
			// faded through by w. If the region is empty, don't
			// run focus fade effect.

			Region fadeRegion = XCreateRegion();
			Region thisAndSubjectIntersection = XCreateRegion();
			Region thisWinRegion = XCreateRegion();
			Region subjectWinRegion = XCreateRegion();
			XRectangle rect;

			int numDodgingWins = 0;

			// Compute subject win. region
			rect.x = WIN_X(w);
			rect.y = WIN_Y(w);
			rect.width = WIN_W(w);
			rect.height = WIN_H(w);
			XUnionRectWithRegion(&rect, &emptyRegion, subjectWinRegion);

			// Dodge candidate window
			CompWindow *dw;
			for (dw = wStart; dw && dw != wEnd->next; dw = dw->next)
			{
				if (!isWinVisible(dw) ||
					dw->wmType & CompWindowTypeDockMask)
					continue;

				// Compute intersection of this with subject
				rect.x = WIN_X(dw);
				rect.y = WIN_Y(dw);
				rect.width = WIN_W(dw);
				rect.height = WIN_H(dw);
				XUnionRectWithRegion(&rect, &emptyRegion, thisWinRegion);
				XIntersectRegion(subjectWinRegion, thisWinRegion,
								 thisAndSubjectIntersection);
				XUnionRegion(fadeRegion, thisAndSubjectIntersection,
							 fadeRegion);

				if (as->focusEffect == AnimEffectDodge &&
					!XEmptyRegion(thisAndSubjectIntersection))
				{
					AnimWindow *adw = GET_ANIM_WINDOW(dw, as);
					if ((adw->curAnimEffect == AnimEffectNone ||
						 (adw->curAnimEffect == AnimEffectDodge)) &&
						dw->id != w->id) // don't let the subject dodge itself
					{
						// Mark this window for dodge

						numDodgingWins++;
						adw->dodgeOrder = numDodgingWins;
					}
				}
			}

			if (XEmptyRegion(fadeRegion))
				return; // empty -> won't be drawn

			if ((as->focusEffect == AnimEffectFocusFade ||
				 as->focusEffect == AnimEffectDodge) && wOldAbove)
			{
				// Store this window in the next window
				// so that this is drawn before that,
				// i.e. in its old place
				getHostedOnWin(as, w, wOldAbove);
			}

			float dodgeMaxStartProgress =
				numDodgingWins *
				as->opt[ANIM_SCREEN_OPTION_DODGE_GAP_RATIO].value.f *
				as->opt[ANIM_SCREEN_OPTION_FOCUS_DURATION].value.f;

			if (as->focusEffect == AnimEffectDodge)
			{
				CompWindow *wDodgeChainLastVisited = NULL;

				as->animInProgress = TRUE;

				aw->isDodgeSubject = TRUE;
				aw->dodgeChainStart = NULL;
				float maxTransformTotalProgress = 0;

				for (dw = wStart; dw && dw != wEnd->next; dw = dw->next)
				{
					AnimWindow *adw = GET_ANIM_WINDOW(dw, as);

					// Skip non-dodgers
					if (adw->dodgeOrder == 0)
						continue;

					// Initiate dodge for this window

					adw->dodgeSubjectWin = w;
					adw->curAnimEffect = AnimEffectDodge;

					// Slight change in dodge movement start
					// to reflect stacking order of dodgy windows
					if (raised)
						adw->transformStartProgress =
							dodgeMaxStartProgress *
							(adw->dodgeOrder - 1) / numDodgingWins;
					else
						adw->transformStartProgress =
							dodgeMaxStartProgress *
							(1 - (float)adw->dodgeOrder / numDodgingWins);

					float transformTotalProgress =
						1 + adw->transformStartProgress;

					// normalize
					adw->transformStartProgress /=
						transformTotalProgress;

					adw->animTotalTime =
						transformTotalProgress * 1000 *
						as->opt[ANIM_SCREEN_OPTION_FOCUS_DURATION].value.f;
					adw->animRemainingTime = adw->animTotalTime;

					if (maxTransformTotalProgress < transformTotalProgress)
						maxTransformTotalProgress = transformTotalProgress;

					// Put window on dodge chain

					// if dodge chain was started before
					if (wDodgeChainLastVisited)
					{
						AnimWindow *awDodgeChainLastVisited =
							GET_ANIM_WINDOW(wDodgeChainLastVisited, as);
						if (raised)
							awDodgeChainLastVisited->dodgeChainNext = dw;
						else
							awDodgeChainLastVisited->dodgeChainPrev = dw;
					}
					else if (raised) // mark chain start
					{
						aw->dodgeChainStart = dw;
					}
					if (raised)
					{
						adw->dodgeChainPrev = wDodgeChainLastVisited;
						adw->dodgeChainNext = NULL;
					}
					else
					{
						adw->dodgeChainPrev = NULL;
						adw->dodgeChainNext = wDodgeChainLastVisited;
					}

					// Find direction (left, right, up, down)
					// that minimizes dodge amount

					// Dodge amount (dodge shadows as well)

					int dodgeAmount[4];

					int i;
					for (i = 0; i < 4; i++)
						dodgeAmount[i] = DODGE_AMOUNT(w, dw, i);

					int amountMin = abs(dodgeAmount[0]);
					int iMin = 0;
					for (i=1; i<4; i++)
					{
						int absAmount = abs(dodgeAmount[i]);
						if (absAmount < amountMin)
						{
							amountMin = absAmount;
							iMin = i;
						}
					}
					adw->dodgeMaxAmount = dodgeAmount[iMin];
					adw->dodgeDirection = iMin;

					wDodgeChainLastVisited = dw;

					// Reset back to 0 for the next dodge calculation
					adw->dodgeOrder = 0;
				}
				if (aw->isDodgeSubject)
					aw->dodgeMaxAmount = 0;

				// if subject is being lowered,
				// point chain-start to the topmost doding window
				if (!raised)
				{
					aw->dodgeChainStart = wDodgeChainLastVisited;
				}

				aw->animTotalTime = maxTransformTotalProgress * 1000 *
					as->opt[ANIM_SCREEN_OPTION_FOCUS_DURATION].value.f;
			}
		}

		// FOCUS event!

		//printf("FOCUS event! %X\n", (unsigned)w->id);

		if (aw->curWindowEvent != WindowEventNone)
		{
			postAnimationCleanup(w, TRUE);
		}

		as->animInProgress = TRUE;
		aw->curWindowEvent = WindowEventFocus;
		aw->curAnimEffect = as->focusEffect;
		if (as->focusEffect != AnimEffectDodge)
			aw->animTotalTime =
				as->opt[ANIM_SCREEN_OPTION_FOCUS_DURATION].value.f * 1000;
		aw->animRemainingTime = aw->animTotalTime;

		// Store coords in this viewport to omit 3d effect
		// painting in other viewports
		aw->lastKnownCoords.x = w->attrib.x;
		aw->lastKnownCoords.y = w->attrib.y;

		damageScreen(w->screen);
	}
}

// returns whether this window is relevant for fade focus
static Bool
relevantForFadeFocus(CompWindow *nw)
{
	ANIM_SCREEN(nw->screen);

	if (!((nw->type &
		   // these two are to be used as "host" windows
		   // to host the painting of windows being focused
		   // at a stacking order lower than them
		   (CompWindowTypeDockMask | CompWindowTypeSplashMask)) ||
		  matchEval(&as->opt[ANIM_SCREEN_OPTION_FOCUS_MATCH].value.match, nw)))
	{
		return FALSE;
	}
	return isWinVisible(nw);
}

static void animPreparePaintScreen(CompScreen * s, int msSinceLastPaint)
{
	CompWindow *w;

	ANIM_SCREEN(s);

	if (as->focusEffect == AnimEffectFocusFade ||
		as->focusEffect == AnimEffectDodge)
	{
		if (as->aWinWasRestackedJustNow)
		{
			// do in reverse order so that focus-fading chains are handled
			// properly
			for (w = s->reverseWindows; w; w = w->prev)
			{
				ANIM_WINDOW(w);
				if (aw->restackInfo)
				{
					// Check if above window is focus-fading
					// (like a dialog of an app. window)
					// if so, focus-fade this together with the one above
					// (link to it)
					CompWindow *nw;
					for (nw = w->next; nw; nw = nw->next)
					{
						if (relevantForFadeFocus(nw))
							break;
					}
					if (!nw)
						continue;

					AnimWindow *awNext = GET_ANIM_WINDOW(nw, as);
					if (awNext && awNext->winThisIsPaintedBefore)
					{
						awNext->moreToBePaintedPrev = w;
						aw->moreToBePaintedNext = nw;
						aw->restackInfo->wOldAbove =
							awNext->winThisIsPaintedBefore;
					}
					initiateFocusAnimation(w);
				}
			}
			if (as->focusEffect == AnimEffectDodge)
			{
				for (w = s->reverseWindows; w; w = w->prev)
				{
					ANIM_WINDOW(w);

					if (!aw->isDodgeSubject)
						continue;
					Bool dodgersAreOnlySubjects = TRUE;
					CompWindow *dw;
					AnimWindow *adw;
					for (dw = aw->dodgeChainStart; dw; dw = adw->dodgeChainNext)
					{
						adw = GET_ANIM_WINDOW(dw, as);
						if (!adw)
							break;
						if (!adw->isDodgeSubject)
							dodgersAreOnlySubjects = FALSE;
					}
					if (dodgersAreOnlySubjects)
						aw->skipPostPrepareScreen = TRUE;
				}
			}
		}
	}

	if (as->animInProgress)
	{
		AnimWindow *aw;
		BoxRec box;
		Point topLeft = {0, 0}, bottomRight = {0, 0};

		as->animInProgress = FALSE;
		for (w = s->windows; w; w = w->next)
		{
			aw = GET_ANIM_WINDOW(w, as);

			if (aw->numPs)
			{
				int i = 0;

				for (i = 0; i < aw->numPs; i++)
				{
					if (aw->ps[i].active)
					{
						updateParticles(&aw->ps[i], msSinceLastPaint);
						as->animInProgress = TRUE;
					}
				}
			}

			if (aw->animRemainingTime > 0)
			{
				if (!aw->animInitialized)	// if animation is just starting
				{
					aw->deceleratingMotion =
							animEffectProperties[aw->curAnimEffect].
							animStepPolygonFunc ==
							polygonsDeceleratingAnimStepPolygon;

					if (playingPolygonEffect(as, aw))
					{
						// Allocate polygon set if null
						if (!aw->polygonSet)
						{
							aw->polygonSet = calloc(1, sizeof(PolygonSet));
						}
						if (!aw->polygonSet)
						{
							compLogMessage (w->screen->display, 
											"animation", CompLogLevelError,
											"%s: Not enough memory at line %d!",
											__FILE__, __LINE__);
							return;
						}
						aw->polygonSet->allFadeDuration = -1.0f;
					}
				}

				// if 3d polygon fx
				if (playingPolygonEffect(as, aw))
				{
					aw->nClipsPassed = 0;
					aw->clipsUpdated = FALSE;
				}

				// If just starting, call fx init func.
				if (!aw->animInitialized &&
					animEffectProperties[aw->curAnimEffect].initFunc)
					animEffectProperties[aw->
										 curAnimEffect].initFunc(s, w);
				aw->animInitialized = TRUE;

				if (aw->model)
				{
					topLeft = aw->model->topLeft;
					bottomRight = aw->model->bottomRight;

					if (aw->model->winWidth != WIN_W(w) ||
						aw->model->winHeight != WIN_H(w))
					{
						// model needs update
						// re-create model
						if (!animEnsureModel
								(w, aw->curWindowEvent, aw->curAnimEffect))
							continue;	// skip this window
					}
				}

				// Call fx step func.
				if (animEffectProperties[aw->curAnimEffect].animStepFunc)
				{
					animEffectProperties[aw->curAnimEffect].
							animStepFunc(s, w, msSinceLastPaint);
				}
				if (aw->animRemainingTime <= 0)
				{
					// Animation done
					postAnimationCleanup(w, TRUE);
				}

				// Damage the union of the window regions
				// before and after animStepFunc does its job
				if (!(s->damageMask & COMP_SCREEN_DAMAGE_ALL_MASK))
				{
					if (aw->animRemainingTime > 0 && aw->model)
					{
						if (aw->model->topLeft.x < topLeft.x)
							topLeft.x = aw->model->topLeft.x;
						if (aw->model->topLeft.y < topLeft.y)
							topLeft.y = aw->model->topLeft.y;
						if (aw->model->bottomRight.x > bottomRight.x)
							bottomRight.x = aw->model->bottomRight.x;
						if (aw->model->bottomRight.y > bottomRight.y)
							bottomRight.y = aw->model->bottomRight.y;
					}
					else
					{
						addWindowDamage(w);
					}
					if (aw->model)
					{
						box.x1 = topLeft.x;
						box.y1 = topLeft.y;
						box.x2 = bottomRight.x + 0.5f;
						box.y2 = bottomRight.y + 0.5f;

						box.x1 -= w->attrib.x + w->attrib.border_width;
						box.y1 -= w->attrib.y + w->attrib.border_width;
						box.x2 -= w->attrib.x + w->attrib.border_width;
						box.y2 -= w->attrib.y + w->attrib.border_width;

						addWindowDamageRect(w, &box);
					}
				}
				as->animInProgress |= (aw->animRemainingTime > 0);
			}

			if (aw->animRemainingTime <= 0)
			{
				if (aw->curAnimEffect != AnimEffectNone ||
					aw->unmapCnt > 0 || aw->destroyCnt > 0)
					postAnimationCleanup(w, TRUE);
				aw->curWindowEvent = WindowEventNone;
				aw->curAnimEffect = AnimEffectNone;
			}
		}

		for (w = s->windows; w; w = w->next)
		{
			aw = GET_ANIM_WINDOW(w, as);
			if (aw && animEffectProperties[aw->curAnimEffect].
				postPreparePaintScreenFunc)
			{
				animEffectProperties[aw->curAnimEffect].
					postPreparePaintScreenFunc(s, w);
			}
		}
	}

	UNWRAP(as, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, msSinceLastPaint);
	WRAP(as, s, preparePaintScreen, animPreparePaintScreen);
}

static void animDonePaintScreen(CompScreen * s)
{
	ANIM_SCREEN(s);

	if (as->animInProgress)
	{
		damageScreen(s);
	}
	UNWRAP(as, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP(as, s, donePaintScreen, animDonePaintScreen);
}

static void
animAddWindowGeometry(CompWindow * w,
					  CompMatrix * matrix,
					  int nMatrix, Region region, Region clip)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(w->screen);

	// if model is lost during animation (e.g. when plugin just reloaded)
	/*if (aw->animRemainingTime > 0 && !aw->model)
	{
		aw->animRemainingTime = 0;
		postAnimationCleanup(w, TRUE);
	}*/
	// if window is being animated
	if (aw->animRemainingTime > 0 && aw->model &&
		!(animEffectProperties[aw->curAnimEffect].letOthersDrawGeoms &&
		  animEffectProperties[aw->curAnimEffect].letOthersDrawGeoms
		  (w->screen, w)))
	{
		BoxPtr pClip;
		int nClip;
		int nVertices, nIndices;
		GLushort *i;
		GLfloat *v;
		int x1, y1, x2, y2;
		float width, height;
		float winContentsY, winContentsHeight;
		float deformedX, deformedY;
		int nVertX, nVertY, wx, wy;
		int vSize, it;
		float gridW, gridH, x, y;
		Bool rect = TRUE;
		Bool useTextureQ = TRUE;
		Model *model = aw->model;
		Region awRegion = NULL;

		// Use Q texture coordinate to avoid jagged-looking quads
		// http://www.r3.nu/~cass/qcoord/
		if (animEffectProperties[aw->curAnimEffect].dontUseQTexCoord)
			useTextureQ = FALSE;

		if (aw->useDrawRegion)
		{
			awRegion = XCreateRegion();
			XOffsetRegion(aw->drawRegion, WIN_X(w), WIN_Y(w));
			XIntersectRegion(region, aw->drawRegion, awRegion);
			XOffsetRegion(aw->drawRegion, -WIN_X(w), -WIN_Y(w));
			nClip = awRegion->numRects;
			pClip = awRegion->rects;
		}
		else
		{
			nClip = region->numRects;
			pClip = region->rects;
		}

		if (nClip == 0)			// nothing to do
		{
			if (awRegion)
				XDestroyRegion(awRegion);
			return;
		}

		for (it = 0; it < nMatrix; it++)
		{
			if (matrix[it].xy != 0.0f || matrix[it].yx != 0.0f)
			{
				rect = FALSE;
				break;
			}
		}

		w->drawWindowGeometry = animDrawWindowGeometry;

		if (aw->polygonSet &&
			animEffectProperties[aw->curAnimEffect].addCustomGeometryFunc)
		{
			/*int nClip2 = nClip;
			   BoxPtr pClip2 = pClip;

			   // For each clip passed to this function
			   for (; nClip2--; pClip2++)
			   {
			   x1 = pClip2->x1;
			   y1 = pClip2->y1;
			   x2 = pClip2->x2;
			   y2 = pClip2->y2;

			   printf("x1: %4d, y1: %4d, x2: %4d, y2: %4d", x1, y1, x2, y2);
			   printf("\tm: %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f\n",
			   matrix[0].xx, matrix[0].xy, matrix[0].yx, matrix[0].yy,
			   matrix[0].x0, matrix[0].y0);
			   } */
			if (nMatrix == 0)
			    return;
			animEffectProperties[aw->curAnimEffect].
					addCustomGeometryFunc(w->screen, w, nClip, pClip,
										  nMatrix, matrix);

			// If addGeometryFunc exists, it is expected to do everthing
			// to add geometries (instead of the rest of this function).

			if (w->vCount == 0)	// if there is no vertex
			{
				// put a dummy quad in vertices and indices
				if (4 > w->indexSize)
				{
					if (!moreWindowIndices(w, 4))
						return;
				}
				if (4 > w->vertexSize)
				{
					if (!moreWindowVertices(w, 4))
						return;
				}
				w->vCount = 4;
				w->indexCount = 4;
				w->texUnits = 1;
				memset(w->vertices, 0, sizeof(GLfloat) * 4);
				memset(w->indices, 0, sizeof(GLushort) * 4);
			}
			return;				// We're done here.
		}

		// window coordinates and size
		wx = WIN_X(w);
		wy = WIN_Y(w);
		width = WIN_W(w);
		height = WIN_H(w);

		// to be used if event is shade/unshade
		winContentsY = w->attrib.y;
		winContentsHeight = w->height;

		w->texUnits = nMatrix;

		if (w->vCount == 0)
		{
			// reset
			w->indexCount = 0;
			w->texCoordSize = 4;
		}
		vSize = 2 + w->texUnits * w->texCoordSize;

		nVertices = w->vCount;
		nIndices = w->indexCount;

		v = w->vertices + (nVertices * vSize);
		i = w->indices + nIndices;

		// For each clip passed to this function
		for (; nClip--; pClip++)
		{
			x1 = pClip->x1;
			y1 = pClip->y1;
			x2 = pClip->x2;
			y2 = pClip->y2;
			/*
			   printf("x1: %4d, y1: %4d, x2: %4d, y2: %4d", x1, y1, x2, y2);
			   printf("\tm: %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f\n",
			   matrix[0].xx, matrix[0].xy, matrix[0].yx, matrix[0].yy,
			   matrix[0].x0, matrix[0].y0);
			 */
			gridW = (float)width / (model->gridWidth - 1);

			if (aw->curWindowEvent == WindowEventShade ||
				aw->curWindowEvent == WindowEventUnshade)
			{
				if (y1 < w->attrib.y)	// if at top part
				{
					gridH = model->topHeight;
				}
				else if (y2 > w->attrib.y + w->height)	// if at bottom
				{
					gridH = model->bottomHeight;
				}
				else			// in window contents (only in Y coords)
				{
					float winContentsHeight =
							height - model->topHeight - model->bottomHeight;
					gridH = winContentsHeight / (model->gridHeight - 3);
				}
			}
			else
				gridH = (float)height / (model->gridHeight - 1);

			// nVertX, nVertY: number of vertices for this clip in x and y dimensions
			// + 2 to avoid running short of vertices in some cases
			nVertX = ceil((x2 - x1) / gridW) + 2;
			nVertY = (gridH ? ceil((y2 - y1) / gridH) : 0) + 2;

			// Allocate 4 indices for each quad
			int newIndexSize = nIndices + ((nVertX - 1) * (nVertY - 1) * 4);

			if (newIndexSize > w->indexSize)
			{
				if (!moreWindowIndices(w, newIndexSize))
					return;

				i = w->indices + nIndices;
			}
			// Assign quad vertices to indices
			int jx, jy;

			for (jy = 0; jy < nVertY - 1; jy++)
			{
				for (jx = 0; jx < nVertX - 1; jx++)
				{
					*i++ = nVertices + nVertX * (2 * jy + 1) + jx;
					*i++ = nVertices + nVertX * (2 * jy + 1) + jx + 1;
					*i++ = nVertices + nVertX * 2 * jy + jx + 1;
					*i++ = nVertices + nVertX * 2 * jy + jx;

					nIndices += 4;
				}
			}

			// Allocate vertices
			int newVertexSize =
					(nVertices + nVertX * (2 * nVertY - 2)) * vSize;
			if (newVertexSize > w->vertexSize)
			{
				if (!moreWindowVertices(w, newVertexSize))
					return;

				v = w->vertices + (nVertices * vSize);
			}

			float rowTexCoordQ = 1;
			float prevRowCellWidth = 0;	// this initial value won't be used
			float rowCellWidth = 0;

			// For each vertex
			for (jy = 0, y = y1; jy < nVertY; jy++)
			{
				if (y > y2)
					y = y2;

				// Do calculations for y here to avoid repeating
				// them unnecessarily in the x loop

				float topiyFloat;
				Bool applyOffsets = TRUE;

				if (aw->curWindowEvent == WindowEventShade
					|| aw->curWindowEvent == WindowEventUnshade)
				{
					if (y1 < w->attrib.y)	// if at top part
					{
						topiyFloat = (y - WIN_Y(w)) / model->topHeight;
						topiyFloat = MIN(topiyFloat, 0.999);	// avoid 1.0
						applyOffsets = FALSE;
					}
					else if (y2 > w->attrib.y + w->height)	// if at bottom
					{
						topiyFloat = (model->gridHeight - 2) +
							         (model->bottomHeight ? (y - winContentsY -
															 winContentsHeight) /
									                         model->bottomHeight : 0);
						applyOffsets = FALSE;
					}
					else		// in window contents (only in Y coords)
					{
						topiyFloat = (model->gridHeight - 3) * 
							         (y - winContentsY) / winContentsHeight + 1;
					}
				}
				else
				{
					topiyFloat = (model->gridHeight - 1) * (y - wy) / height;
				}
				// topiy should be at most (model->gridHeight - 2)
				int topiy = (int)(topiyFloat + 1e-4);

				if (topiy == model->gridHeight - 1)
					topiy--;
				int bottomiy = topiy + 1;
				float iny = topiyFloat - topiy;

				// End of calculations for y

				for (jx = 0, x = x1; jx < nVertX; jx++)
				{
					if (x > x2)
						x = x2;

					// find containing grid cell (leftix rightix) x (topiy bottomiy)
					float leftixFloat =
							(model->gridWidth - 1) * (x - wx) / width;
					int leftix = (int)(leftixFloat + 1e-4);

					if (leftix == model->gridWidth - 1)
						leftix--;
					int rightix = leftix + 1;

					// Objects that are at top, bottom, left, right corners of quad
					Object *objToTopLeft =
							&(model->objects[topiy * model->gridWidth + leftix]);
					Object *objToTopRight =
							&(model->objects[topiy * model->gridWidth + rightix]);
					Object *objToBottomLeft =
							&(model->objects[bottomiy * model->gridWidth + leftix]);
					Object *objToBottomRight =
							&(model->objects[bottomiy * model->gridWidth + rightix]);

					// find position in cell by taking remainder of flooring
					float inx = leftixFloat - leftix;

					// Interpolate to find deformed coordinates

					float hor1x = (1 - inx) *
						    objToTopLeft->position.x +
							inx * objToTopRight->position.x;
					float hor1y = (1 - inx) *
							objToTopLeft->position.y +
							inx * objToTopRight->position.y;
					float hor2x = (1 - inx) *
							objToBottomLeft->position.x +
							inx * objToBottomRight->position.x;
					float hor2y = (1 - inx) *
							objToBottomLeft->position.y +
							inx * objToBottomRight->position.y;

					deformedX = (1 - iny) * hor1x + iny * hor2x;
					deformedY = (1 - iny) * hor1y + iny * hor2y;

					if (useTextureQ)
					{
						if (jx == 1)
							rowCellWidth = deformedX - v[-2];

						if (jy > 0 && jx == 1)	// do only once per row for all rows except row 0
						{
							rowTexCoordQ = (rowCellWidth / prevRowCellWidth);

							v[-3] = rowTexCoordQ;	// update first column
							v[-6] *= rowTexCoordQ;	// (since we didn't know rowTexCoordQ before)
							v[-5] *= rowTexCoordQ;
						}
					}
					if (rect)
					{
						for (it = 0; it < nMatrix; it++)
						{
							float offsetY = 0;

							if (applyOffsets && y < y2)
								offsetY = objToTopLeft->offsetTexCoordForQuadAfter.y;

							*v++ = COMP_TEX_COORD_X(&matrix[it], x);
							*v++ = COMP_TEX_COORD_Y(&matrix[it], y + offsetY);
							*v++ = 0;
							if (useTextureQ)
							{
								*v++ = rowTexCoordQ;	// Q texture coordinate

								if (0 < jy && jy < nVertY - 1)
								{
									// copy first 3 texture coords to duplicate row
									memcpy(v - 4 + nVertX * vSize, 
										   v - 4, 3 * sizeof(GLfloat));
									*(v - 1 + nVertX * vSize) = 1;	// Q texture coordinate
								}
								if (applyOffsets &&	/*0 < jy && */
									objToTopLeft->
									offsetTexCoordForQuadBefore.y != 0)
								{
									// After copying to next row, update texture y coord
									// by following object's offset
									offsetY = objToTopLeft-> offsetTexCoordForQuadBefore.y;
									v[-3] = COMP_TEX_COORD_Y(&matrix[it], y + offsetY);
								}
								if (jx > 0)	// since column 0 is updated when jx == 1
								{
									v[-4] *= rowTexCoordQ;
									v[-3] *= rowTexCoordQ;
								}
							}
							else
							{
								*v++ = 1;

								if (0 < jy && jy < nVertY - 1)
								{
									// copy first 3 texture coords to duplicate row
									memcpy(v - 4 + nVertX * vSize, 
										   v - 4, 3 * sizeof(GLfloat));

									*(v - 1 + nVertX * vSize) = 1;	// Q texture coordinate
								}
								if (applyOffsets && 
									objToTopLeft->offsetTexCoordForQuadBefore.y != 0)
								{
									// After copying to next row, update texture y coord
									// by following object's offset
									offsetY = objToTopLeft->offsetTexCoordForQuadBefore.y;
									v[-3] = COMP_TEX_COORD_Y(&matrix[it], y + offsetY);
								}
							}
						}
					}
					else
					{
						for (it = 0; it < nMatrix; it++)
						{
							float offsetY = 0;

							if (applyOffsets && y < y2)
							{
								// FIXME:
								// the correct value below doesn't work for some reason
								offsetY = 0;
								//		objToTopLeft->
								//		offsetTexCoordForQuadAfter.y;
							}

							*v++ = COMP_TEX_COORD_XY(&matrix[it], x, y + offsetY);
							*v++ = COMP_TEX_COORD_YX(&matrix[it], x, y + offsetY);
							*v++ = 0;
							if (useTextureQ)
							{
								*v++ = rowTexCoordQ;	// Q texture coordinate

								if (0 < jy && jy < nVertY - 1)
								{
									// copy first 3 texture coords to duplicate row
									memcpy(v - 4 + nVertX * vSize, 
										   v - 4, 3 * sizeof(GLfloat));
									*(v - 1 + nVertX * vSize) = 1;	// Q texture coordinate
								}
								if (applyOffsets && 
									objToTopLeft->offsetTexCoordForQuadBefore.y != 0)
								{
									// After copying to next row, update texture y coord
									// by following object's offset
									offsetY = objToTopLeft->offsetTexCoordForQuadBefore.y;
									v[-4] = COMP_TEX_COORD_XY(&matrix[it], x,
															  y + offsetY);
									v[-3] = COMP_TEX_COORD_YX(&matrix[it], x,
															  y + offsetY);
								}
								if (jx > 0)	// column t should be updated when jx is t+1
								{
									v[-4] *= rowTexCoordQ;
									v[-3] *= rowTexCoordQ;
								}
							}
							else
							{
								*v++ = 1;

								if (0 < jy && jy < nVertY - 1)
								{
									// copy first 3 texture coords to duplicate row
									memcpy(v - 4 + nVertX * vSize, 
										   v - 4, 3 * sizeof(GLfloat));
									*(v - 1 + nVertX * vSize) = 1;	// Q texture coordinate
								}
								if (applyOffsets && 
									objToTopLeft->offsetTexCoordForQuadBefore.y != 0)
								{
									// After copying to next row, update texture y coord
									// by following object's offset
									offsetY =
											objToTopLeft->
											offsetTexCoordForQuadBefore.y;
									v[-4] = COMP_TEX_COORD_XY(&matrix[it], x,
															  y + offsetY);
									v[-3] = COMP_TEX_COORD_YX(&matrix[it], x,
															  y + offsetY);
								}
							}
						}
					}
					*v++ = deformedX;
					*v++ = deformedY;

					if (0 < jy && jy < nVertY - 1)
						memcpy(v - 2 + nVertX * vSize, v - 2, 2 * sizeof(GLfloat));

					nVertices++;

					// increment x properly (so that coordinates fall on grid intersections)
					x = rightix * gridW + wx;
				}
				if (useTextureQ)
					prevRowCellWidth = rowCellWidth;

				if (0 < jy && jy < nVertY - 1)
				{
					v += nVertX * vSize;	// skip the duplicate row
					nVertices += nVertX;
				}
				// increment y properly (so that coordinates fall on grid intersections)
				if (aw->curWindowEvent == WindowEventShade
					|| aw->curWindowEvent == WindowEventUnshade)
				{
					y += gridH;
				}
				else
				{
					y = bottomiy * gridH + wy;
				}
			}
		}
		w->vCount = nVertices;
		w->indexCount = nIndices;
		if (awRegion)
		{
			XDestroyRegion(awRegion);
			awRegion = NULL;
		}
	}
	else
	{
		UNWRAP(as, w->screen, addWindowGeometry);
		(*w->screen->addWindowGeometry) (w, matrix, nMatrix, region, clip);
		WRAP(as, w->screen, addWindowGeometry, animAddWindowGeometry);
	}
}

static void
animDrawWindowTexture(CompWindow * w, CompTexture * texture,
					  const FragmentAttrib *attrib,
					  unsigned int mask)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(w->screen);

	if (aw->animRemainingTime > 0)	// if animation in progress, store texture
	{
		aw->curTexture = texture;
		aw->curPaintAttrib = *attrib;
	}

	UNWRAP(as, w->screen, drawWindowTexture);
	(*w->screen->drawWindowTexture) (w, texture, attrib, mask);
	WRAP(as, w->screen, drawWindowTexture, animDrawWindowTexture);
}

void
animDrawWindowGeometry(CompWindow * w)
{
	ANIM_WINDOW(w);

	//if (aw->animRemainingTime > 0 &&
	//	!animEffectProperties[aw->curAnimEffect].letOthersDrawGeoms)
	//{
	//printf("animDrawWindowGeometry: %X: coords: %d, %d, %f\n",
	//       (unsigned)w->id, WIN_X (w), WIN_Y (w), aw->animRemainingTime);
	aw->nDrawGeometryCalls++;

	ANIM_SCREEN(w->screen);

	if (playingPolygonEffect(as, aw) &&
		animEffectProperties[aw->curAnimEffect].drawCustomGeometryFunc)
	{
		animEffectProperties[aw->curAnimEffect].drawCustomGeometryFunc (w->screen, w);
		return;
	}
	int texUnit = w->texUnits;
	int currentTexUnit = 0;
	int stride = 2 + texUnit * w->texCoordSize;
	GLfloat *vertices = w->vertices + (stride - 2);

	stride *= sizeof(GLfloat);

	glVertexPointer(2, GL_FLOAT, stride, vertices);

	while (texUnit--)
	{
		if (texUnit != currentTexUnit)
		{
			w->screen->clientActiveTexture(GL_TEXTURE0_ARB + texUnit);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			currentTexUnit = texUnit;
		}
		vertices -= w->texCoordSize;
		glTexCoordPointer(w->texCoordSize, GL_FLOAT, stride, vertices);
	}

	glDrawElements(GL_QUADS, w->indexCount, GL_UNSIGNED_SHORT,
				   w->indices);

	// disable all texture coordinate arrays except 0
	texUnit = w->texUnits;
	if (texUnit > 1)
	{
		while (--texUnit)
		{
			(*w->screen->clientActiveTexture) (GL_TEXTURE0_ARB + texUnit);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}

		(*w->screen->clientActiveTexture) (GL_TEXTURE0_ARB);
	}
}

static Bool
animPaintWindow(CompWindow * w,
				const WindowPaintAttrib * attrib,
				const CompTransform    *transform,
				Region region, unsigned int mask)
{
	Bool status;

	ANIM_SCREEN(w->screen);
	ANIM_WINDOW(w);

	// For Focus Fade && Focus Dodge
	if (aw->winToBePaintedBeforeThis)
	{
		CompWindow *w2 = aw->winToBePaintedBeforeThis;
		// ========= Paint w2 on host w =========

		// Go to the bottommost window in this "focus chain"
		// This chain is used to handle some cases: e.g when Find dialog
		// of an app is open, both windows should be faded when the Find
		// dialog is raised.
		CompWindow *bottommost = w2;
		CompWindow *wPrev = GET_ANIM_WINDOW(bottommost, as)->
			moreToBePaintedPrev;
		while (wPrev)
		{
			bottommost = wPrev;
			wPrev = GET_ANIM_WINDOW(wPrev, as)->moreToBePaintedPrev;
		}

		// Paint each window in the chain going to the topmost
		for (w2 = bottommost; w2;
			 w2 = GET_ANIM_WINDOW(w2, as)->moreToBePaintedNext)
		{
			AnimWindow *aw2 = GET_ANIM_WINDOW(w2, as);
			if (!aw2)
				continue;

			if (aw2->animTotalTime < 1e-4)
			{
				aw2->drawnOnHostSkip = TRUE;
			}
			w2->indexCount = 0;
			WindowPaintAttrib wAttrib2 = w2->paint;

			wAttrib2.xScale = 1.0f;
			wAttrib2.yScale = 1.0f;

			if (aw2->curAnimEffect == AnimEffectFocusFade)
				fxFocusFadeUpdateWindowAttrib2(as, aw2, &wAttrib2);
			else // if dodge
				wAttrib2.opacity = aw2->storedOpacity;

			unsigned int mask2 = mask;
			mask2 |= PAINT_WINDOW_TRANSFORMED_MASK;

			aw2->nDrawGeometryCalls = 0;
			UNWRAP(as, w2->screen, paintWindow);
			status = (*w2->screen->paintWindow)
				(w2, &wAttrib2, transform, region, mask2);
			WRAP(as, w2->screen, paintWindow, animPaintWindow);
		}
	}
	if (aw->drawnOnHostSkip)
	{
		aw->drawnOnHostSkip = FALSE;
		return FALSE;
	}
	if (aw->animRemainingTime > 0)
	{
		if (aw->curAnimEffect == AnimEffectDodge &&
			aw->isDodgeSubject &&
			aw->winThisIsPaintedBefore)
		{
			// if aw is to be painted somewhere other than in its
			// original stacking order, it will only be painted with
			// the code above (but in animPaintWindow call for
			// the window aw->winThisIsPaintedBefore), so we don't
			// need to paint aw below
			return FALSE;
		}

		if (playingPolygonEffect(as, aw))
		{
			if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
			{
				aw->curTextureFilter = w->screen->filter[WINDOW_TRANS_FILTER];
			}
			else if (mask & PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK)
			{
				aw->curTextureFilter = w->screen->filter[SCREEN_TRANS_FILTER];
			}
			else
			{
				aw->curTextureFilter = w->screen->filter[NOTHING_TRANS_FILTER];
			}
		}
		w->indexCount = 0;

		WindowPaintAttrib wAttrib = *attrib;
		CompTransform wTransform = *transform;

		//if (mask & PAINT_WINDOW_SOLID_MASK)
		//	return FALSE;

		// TODO: should only happen for distorting effects
		mask |= PAINT_WINDOW_TRANSFORMED_MASK;

		wAttrib.xScale = 1.0f;
		wAttrib.yScale = 1.0f;

		aw->nDrawGeometryCalls = 0;

		if (animEffectProperties[aw->curAnimEffect].updateWindowAttribFunc)
			animEffectProperties[aw->curAnimEffect].
					updateWindowAttribFunc(as, aw, &wAttrib);

		if (animEffectProperties[aw->curAnimEffect].updateWindowTransformFunc)
			animEffectProperties[aw->curAnimEffect].
					updateWindowTransformFunc(w->screen, w, &wTransform);

		if (animEffectProperties[aw->curAnimEffect].prePaintWindowFunc)
			animEffectProperties[aw->curAnimEffect].
					prePaintWindowFunc(w->screen, w);

		UNWRAP(as, w->screen, paintWindow);
		status = (*w->screen->paintWindow) (w, &wAttrib, &wTransform, region, mask);
		WRAP(as, w->screen, paintWindow, animPaintWindow);

		if (animEffectProperties[aw->curAnimEffect].postPaintWindowFunc)
			animEffectProperties[aw->curAnimEffect].
					postPaintWindowFunc(w->screen, w);
	}
	else
	{
		UNWRAP(as, w->screen, paintWindow);
		status = (*w->screen->paintWindow) (w, attrib, transform, region, mask);
		WRAP(as, w->screen, paintWindow, animPaintWindow);
	}

	return status;
}

static Bool animGetWindowIconGeometry(CompWindow * w, XRectangle * rect)
{
	Atom actual;
	int result, format;
	unsigned long n, left;
	unsigned char *data;

	ANIM_DISPLAY(w->screen->display);

	result = XGetWindowProperty(w->screen->display->display, w->id,
								ad->winIconGeometryAtom,
								0L, 4L, FALSE, XA_CARDINAL, &actual,
								&format, &n, &left, &data);

	if (result == Success && n && data)
	{
		if (n == 4)
		{
			unsigned long *geometry = (unsigned long *)data;

			rect->x = geometry[0];
			rect->y = geometry[1];
			rect->width = geometry[2];
			rect->height = geometry[3];

			XFree(data);

			return TRUE;
		}

		XFree(data);
	}

	return FALSE;
}

static void animHandleCompizEvent(CompDisplay * d, char *pluginName,
				 char *eventName, CompOption * option, int nOption)
{
	ANIM_DISPLAY(d);

	UNWRAP (ad, d, handleCompizEvent);
	(*d->handleCompizEvent) (d, pluginName, eventName, option, nOption);
	WRAP (ad, d, handleCompizEvent, animHandleCompizEvent);

	if (strcmp(pluginName, "switcher") == 0)
	{
		if (strcmp(eventName, "activate") == 0)
		{
			Window xid = getIntOptionNamed(option, nOption, "root", 0);
			CompScreen *s = findScreenAtDisplay(d, xid);

			if (s)
			{
				ANIM_SCREEN(s);
				as->switcherActive = getBoolOptionNamed(option, nOption, "active", FALSE);
				as->switcherWinOpeningSuppressed = FALSE;
			}
		}
	}
	else if (strcmp(pluginName, "group") == 0)
	{
		if (strcmp(eventName, "tabChangeActivate") == 0)
		{
			Window xid = getIntOptionNamed(option, nOption, "root", 0);
			CompScreen *s = findScreenAtDisplay(d, xid);

			if (s)
			{
				ANIM_SCREEN(s);
				as->groupTabChangeActive = getBoolOptionNamed(option, nOption, "active", FALSE);
			}
		}
	}
	else if (strcmp(pluginName, "scale") == 0)
	{
		if (strcmp(eventName, "activate") == 0)
		{
			Window xid = getIntOptionNamed(option, nOption, "root", 0);
			CompScreen *s = findScreenAtDisplay(d, xid);

			if (s)
			{
				ANIM_SCREEN(s);
				as->scaleActive = getBoolOptionNamed(option, nOption, "active", FALSE);
			}
		}
	}
}

static void
updateLastClientListStacking(CompScreen *s)
{
	ANIM_SCREEN(s);
	int n = s->nClientList;
	Window *clientListStacking = (Window *) (s->clientList + n) + n;

	if (as->nLastClientListStacking != n) // the number of windows has changed
	{
		Window *list;

		list = realloc (as->lastClientListStacking, sizeof (Window) * n);
		if (!list)
			return;

		as->lastClientListStacking  = list;
		as->nLastClientListStacking = n;
	}

	// Store new client stack listing
	memcpy(as->lastClientListStacking, clientListStacking,
		   sizeof (Window) * n);
}

static void animHandleEvent(CompDisplay * d, XEvent * event)
{
	CompWindow *w;

	ANIM_DISPLAY(d);

	switch (event->type)
	{
	case PropertyNotify:
		if (event->xproperty.atom == d->clientListStackingAtom)
		{
			CompScreen *s = findScreenAtDisplay (d, event->xproperty.window);
			if (s)
				updateLastClientListStacking(s);
		}
		break;
	case MapNotify:
		w = findWindowAtDisplay(d, event->xmap.window);
		if (w)
		{
			ANIM_WINDOW(w);

			if (aw->animRemainingTime > 0)
			{
				aw->state = aw->newState;
			}
			while (aw->unmapCnt)
			{
				unmapWindow(w);
				aw->unmapCnt--;
			}
		}
		break;
	case DestroyNotify:
		w = findWindowAtDisplay(d, event->xunmap.window);
		if (w)
		{
			ANIM_WINDOW(w);
			aw->destroyCnt++;
			w->destroyRefCnt++;
			addWindowDamage(w);
		}
		break;
	case UnmapNotify:
		w = findWindowAtDisplay(d, event->xunmap.window);
		if (w)
		{
			ANIM_SCREEN(w->screen);

			if (w->pendingUnmaps && onCurrentDesktop(w))	// Normal -> Iconic
			{
				ANIM_WINDOW(w);
				if (w->shaded)
				{
					// SHADE event!

					//printf("SHADE event! %X\n", (unsigned)w->id);

					aw->nowShaded = TRUE;

					if (as->shadeEffect && 
					    matchEval (&as->opt[ANIM_SCREEN_OPTION_SHADE_MATCH].value.match, w))
					{
						//IPCS_SetBool(IPCS_OBJECT(w), aw->animatedAtom, TRUE);
						Bool startingNew = TRUE;

						if (aw->curWindowEvent != WindowEventNone)
						{
							if (aw->curWindowEvent != WindowEventUnshade)
								postAnimationCleanup(w, TRUE);
							else
							{
								// Play the unshade effect backwards from where it left
								aw->animRemainingTime =
										aw->animTotalTime -
										aw->animRemainingTime;

								// avoid window remains
								if (aw->animRemainingTime <= 0)
									aw->animRemainingTime = 1;

								startingNew = FALSE;
								if (aw->animOverrideProgressDir == 0)
									aw->animOverrideProgressDir = 2;
								else if (aw->animOverrideProgressDir == 1)
									aw->animOverrideProgressDir = 0;
							}
						}

						if (startingNew)
						{
							AnimEffect effectToBePlayed;
							effectToBePlayed = animGetAnimEffect(
								as->shadeEffect,
								as->shadeRandomEffects,
								as->nShadeRandomEffects,
								as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

							// handle empty random effect list
							if (effectToBePlayed == AnimEffectNone)
								break;
						
							aw->curAnimEffect = effectToBePlayed;

							aw->animTotalTime =
									as->opt[ANIM_SCREEN_OPTION_SHADE_DURATION].value.f * 1000;
							aw->animRemainingTime = aw->animTotalTime;
						}

						as->animInProgress = TRUE;
						aw->curWindowEvent = WindowEventShade;

						// Store coords in this viewport to omit 3d effect
						// painting in other viewports
						aw->lastKnownCoords.x = w->attrib.x;
						aw->lastKnownCoords.y = w->attrib.y;

						if (!animEnsureModel
							(w, WindowEventShade, aw->curAnimEffect))
						{
							postAnimationCleanup(w, TRUE);
						}

						aw->unmapCnt++;
						w->unmapRefCnt++;

						addWindowDamage(w);
					}
				}
				else if (!w->invisible
						 && as->minimizeEffect
						 && animGetWindowIconGeometry(w, &aw->icon)
						 && matchEval (&as->opt[ANIM_SCREEN_OPTION_MINIMIZE_MATCH].value.match, w))
				{
					// MINIMIZE event!

					//printf("MINIMIZE event! %X\n", (unsigned)w->id);

					Bool startingNew = TRUE;

					if (aw->curWindowEvent != WindowEventNone)
					{
						if (aw->curWindowEvent != WindowEventUnminimize)
							postAnimationCleanup(w, TRUE);
						else
						{
							// Play the unminimize effect backwards from where it left
							aw->animRemainingTime =
									aw->animTotalTime - aw->animRemainingTime;

							// avoid window remains
							if (aw->animRemainingTime == 0)
								aw->animRemainingTime = 1;

							startingNew = FALSE;
							if (aw->animOverrideProgressDir == 0)
								aw->animOverrideProgressDir = 2;
							else if (aw->animOverrideProgressDir == 1)
								aw->animOverrideProgressDir = 0;
						}
					}

					if (startingNew)
					{
						AnimEffect effectToBePlayed;
						
						effectToBePlayed = animGetAnimEffect(
							as->minimizeEffect,
							as->minimizeRandomEffects,
							as->nMinimizeRandomEffects,
							as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

						// handle empty random effect list
						if (effectToBePlayed == AnimEffectNone)
							break;

						aw->curAnimEffect = effectToBePlayed;

						aw->animTotalTime =
								as->opt[ANIM_SCREEN_OPTION_MINIMIZE_DURATION].value.f * 1000;

						// allow extra time for spring damping
						if (effectToBePlayed == AnimEffectZoom ||
							effectToBePlayed == AnimEffectSidekick)
							aw->animTotalTime /= SPRING_PERCEIVED_T;

						aw->animRemainingTime = aw->animTotalTime;
					}

					aw->newState = IconicState;
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventMinimize;

					// Store coords in this viewport to omit 3d effect
					// painting in other viewports
					aw->lastKnownCoords.x = w->attrib.x;
					aw->lastKnownCoords.y = w->attrib.y;

					if (!animEnsureModel
						(w, WindowEventMinimize, aw->curAnimEffect))
					{
						postAnimationCleanup(w, TRUE);
					}
					else
					{
						/*
						if (!animGetWindowIconGeometry(w, &aw->icon))
						{
							// minimize to bottom-center if there is no window list
							aw->icon.x = w->screen->width / 2;
							aw->icon.y = w->screen->height;
							aw->icon.width = 100;
							aw->icon.height = 20;
						}
						*/
						if ((aw->curAnimEffect == AnimEffectZoom || 
							 aw->curAnimEffect == AnimEffectSidekick) &&
							(as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterOn ||
							 as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterMin))
						{
							aw->icon.x =
									WIN_X(w) +
									WIN_W(w) / 2 - aw->icon.width / 2;
							aw->icon.y =
									WIN_Y(w) +
									WIN_H(w) / 2 - aw->icon.height / 2;
						}

						aw->unmapCnt++;
						w->unmapRefCnt++;

						addWindowDamage(w);
					}
				}
			}
			else				// X -> Withdrawn
			{
				ANIM_WINDOW(w);

				AnimEffect windowsCloseEffect = AnimEffectNone;
				int whichClose = 1;	// either 1 or 2

				if (as->close1Effect && 
				    matchEval (&as->opt[ANIM_SCREEN_OPTION_CLOSE1_MATCH].value.match, w))
					windowsCloseEffect = as->close1Effect;
				else if (as->close2Effect && 
					 matchEval (&as->opt[ANIM_SCREEN_OPTION_CLOSE2_MATCH].value.match, w))
				{
					windowsCloseEffect = as->close2Effect;
					whichClose = 2;
				}
				// CLOSE event!

				if (windowsCloseEffect)
				{
					int tmpSteps = 0;

					//printf("CLOSE event! %X\n", (unsigned)w->id);

					Bool startingNew = TRUE;

					if (aw->animRemainingTime > 0 &&
						aw->curWindowEvent != WindowEventCreate)
					{
						tmpSteps = aw->animRemainingTime;
						aw->animRemainingTime = 0;
					}
					if (aw->curWindowEvent != WindowEventNone)
					{
						if (aw->curWindowEvent == WindowEventCreate)
						{
							// Play the create effect backward from where it left
							aw->animRemainingTime =
									aw->animTotalTime - aw->animRemainingTime;

							// avoid window remains
							if (aw->animRemainingTime <= 0)
								aw->animRemainingTime = 1;

							startingNew = FALSE;
							if (aw->animOverrideProgressDir == 0)
								aw->animOverrideProgressDir = 2;
							else if (aw->animOverrideProgressDir == 1)
								aw->animOverrideProgressDir = 0;
						}
						else if (aw->curWindowEvent == WindowEventClose)
						{
							if (aw->animOverrideProgressDir == 2)
							{
								aw->animRemainingTime = tmpSteps;
								startingNew = FALSE;
							}
						}
						else
						{
							postAnimationCleanup(w, TRUE);
						}
					}

					if (startingNew)
					{
						AnimEffect effectToBePlayed;
						effectToBePlayed = animGetAnimEffect(
							windowsCloseEffect,
							(whichClose == 1) ? as->close1RandomEffects : 
							                    as->close2RandomEffects,
							(whichClose == 1) ? as->nClose1RandomEffects :
							                    as->nClose2RandomEffects,
							as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

						// handle empty random effect list
						if (effectToBePlayed == AnimEffectNone)
							break;
					
						aw->curAnimEffect = effectToBePlayed;

						aw->animTotalTime =	
							as->opt[whichClose == 1 ?
									ANIM_SCREEN_OPTION_CLOSE1_DURATION :
									ANIM_SCREEN_OPTION_CLOSE2_DURATION].value.f * 1000;

						// allow extra time for spring damping
						if (effectToBePlayed == AnimEffectZoom ||
							effectToBePlayed == AnimEffectSidekick)
							aw->animTotalTime /= SPRING_PERCEIVED_T;

						aw->animRemainingTime = aw->animTotalTime;
					}

					aw->state = NormalState;
					aw->newState = WithdrawnState;
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventClose;

					// Store coords in this viewport to omit 3d effect
					// painting in other viewports
					aw->lastKnownCoords.x = w->attrib.x;
					aw->lastKnownCoords.y = w->attrib.y;

					if (!animEnsureModel
						(w, WindowEventClose, aw->curAnimEffect))
					{
						postAnimationCleanup(w, TRUE);
					}
					else if (getMousePointerXY
							 (w->screen, &aw->icon.x, &aw->icon.y))
					{
						aw->icon.width = FAKE_ICON_SIZE;
						aw->icon.height = FAKE_ICON_SIZE;

						if (aw->curAnimEffect == AnimEffectMagicLamp)
							aw->icon.width = 
								MAX(aw->icon.width,
									as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_CREATE_START_WIDTH].value.i);
						else if (aw->curAnimEffect == AnimEffectMagicLampVacuum)
							aw->icon.width =
								MAX(aw->icon.width,
									as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_CREATE_START_WIDTH].value.i);

						if ((aw->curAnimEffect == AnimEffectZoom || 
							 aw->curAnimEffect == AnimEffectSidekick) &&
							(as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterOn ||
							 as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterCreate))
						{
							aw->icon.x =
									WIN_X(w) +
									WIN_W(w) / 2 - aw->icon.width / 2;
							aw->icon.y =
									WIN_Y(w) +
									WIN_H(w) / 2 - aw->icon.height / 2;
						}

						aw->unmapCnt++;
						w->unmapRefCnt++;

						addWindowDamage(w);
					}
				}
				else if ((as->create1Effect &&
						  matchEval (&as->opt[ANIM_SCREEN_OPTION_CREATE1_MATCH].value.match, w)) ||
						 (as->create2Effect &&
						  matchEval (&as->opt[ANIM_SCREEN_OPTION_CREATE2_MATCH].value.match, w)))
				{
					// stop the current animation and prevent it from rewinding

					if (aw->animRemainingTime > 0 &&
						aw->curWindowEvent != WindowEventCreate)
					{
						aw->animRemainingTime = 0;
					}
					if ((aw->curWindowEvent != WindowEventNone) &&
						(aw->curWindowEvent != WindowEventClose))
					{
						postAnimationCleanup(w, TRUE);
					}
					// set some properties to make sure this window will use the
					// correct create effect the next time it's "created"

					aw->state = NormalState;
					aw->newState = WithdrawnState;
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventClose;

					aw->unmapCnt++;
					w->unmapRefCnt++;

					addWindowDamage(w);
				}
			}
		}
		break;
	case ConfigureNotify:
		{
			XConfigureEvent *ce = &event->xconfigure;
			w = findWindowAtDisplay (d, ce->window);
			if (!w)
				break;
			if (w->prev)
			{
				if (ce->above && ce->above == w->prev->id)
				    break;
			}
			else if (ce->above == None)
				break;
			CompScreen *s = findScreenAtDisplay (d, event->xproperty.window);
			if (!s)
				break;

			ANIM_SCREEN(s);
			int i;
			int n = s->nClientList;
			Bool winOpenedClosed = FALSE;

			Window *clientList = (Window *) (s->clientList + n);
			Window *clientListStacking = clientList + n;

			if (n != as->nLastClientListStacking)
				winOpenedClosed = TRUE;

			// if restacking occurred and not window open/close
			if (!winOpenedClosed)
			{
				ANIM_WINDOW(w);
				aw->configureNotified = TRUE;

				// Find which window is restacked 
				// e.g. here 8507730 was raised:
				// 54526074 8507730 48234499 14680072 6291497
				// 54526074 48234499 14680072 8507730 6291497
				// compare first changed win. of row 1 with last
				// changed win. of row 2, and vica versa
				// the matching one is the restacked one
				CompWindow *wRestacked = 0;
				CompWindow *wStart = 0;
				CompWindow *wEnd = 0;
				CompWindow *wOldAbove = 0;
				CompWindow *wChangeStart = 0;
				CompWindow *wChangeEnd = 0;

				Bool raised = FALSE;
				int changeStart = -1;
				int changeEnd = -1;
				for (i = 0; i < n; i++)
				{
					CompWindow *wi =
						findWindowAtScreen (s, clientListStacking[i]);

					// skip if minimized (prevents flashing problem)
					if (!wi || !isWinVisible(wi))
						continue;

					// skip if (tabbed and) hidden by Group plugin
					if (wi->state & (CompWindowStateSkipPagerMask |
									 CompWindowStateSkipTaskbarMask))
						continue;

					if (clientListStacking[i] !=
					    as->lastClientListStacking[i])
					{
						if (changeStart < 0)
						{
							changeStart = i;
							wChangeStart = wi; // make use of already found w
						}
						else
						{
							changeEnd = i;
							wChangeEnd = wi;
						}
					}
					else if (changeStart >= 0) // found some change earlier
						break;
				}

				// if restacking occurred
				if (changeStart >= 0 && changeEnd >= 0)
				{
					CompWindow *w2;

					// if we have only 2 windows changed, 
					// choose the one clicked on
					Bool preferRaised = FALSE;
					Bool onlyTwo = FALSE;

					if (wChangeEnd &&
						clientListStacking[changeEnd] ==
					    as->lastClientListStacking[changeStart] &&
						clientListStacking[changeStart] ==
					    as->lastClientListStacking[changeEnd])
					{
						// Check if the window coming on top was
						// configureNotified (clicked on)
						AnimWindow *aw2 = GET_ANIM_WINDOW(wChangeEnd, as);
						if (aw2->configureNotified)
						{
							preferRaised = TRUE;
						}
						onlyTwo = TRUE;
					}
					// Clear all configureNotified's
					for (w2 = s->windows; w2; w2 = w2->next)
					{
						AnimWindow *aw2 = GET_ANIM_WINDOW(w2, as);
						aw2->configureNotified = FALSE;
					}

					if (preferRaised ||
						(!onlyTwo &&
						 clientListStacking[changeEnd] ==
					     as->lastClientListStacking[changeStart]))
					{
						// raised
						raised = TRUE;
						wRestacked = wChangeEnd;
						wStart = wChangeStart;
						wEnd = wRestacked;
						wOldAbove = wStart;
					}
					else if (clientListStacking[changeStart] ==
					    as->lastClientListStacking[changeEnd])
					{
						// lowered
						wRestacked = wChangeStart;
						wStart = wRestacked;
						wEnd = wChangeEnd;
						wOldAbove = findWindowAtScreen
							(s, as->lastClientListStacking[changeEnd+1]);
					}
					for (; wOldAbove && !isWinVisible(wOldAbove);
						 wOldAbove = wOldAbove->next)
						;
				}
				if (wRestacked && wStart && wEnd && wOldAbove)
				{
					//printf("--- raised: %d, wRestacked: %X\n",
					//	   raised, wRestacked->id);
					//printf("=== wStart: %X, wEnd: %X, wOldAbove: %X\n",
					//		wStart->id, wEnd->id, wOldAbove->id);
					AnimWindow *awRestacked = GET_ANIM_WINDOW(wRestacked, as);
					if (awRestacked->created)
					{
						RestackInfo *restackInfo = calloc(sizeof(RestackInfo), 1);
						if (restackInfo)
						{
							restackInfo->wRestacked = wRestacked;
							restackInfo->wStart = wStart;
							restackInfo->wEnd = wEnd;
							restackInfo->wOldAbove = wOldAbove;
							restackInfo->raised = raised;

							if (awRestacked->restackInfo)
								free(awRestacked->restackInfo);

							awRestacked->restackInfo = restackInfo;
							as->aWinWasRestackedJustNow = TRUE;
						}
					}
				}
			}
			updateLastClientListStacking(s);
		}
		break;
	default:
		break;
	}

	UNWRAP(ad, d, handleEvent);
	(*d->handleEvent) (d, event);
	WRAP(ad, d, handleEvent, animHandleEvent);

	switch (event->type)
	{
	case PropertyNotify:
		if (event->xproperty.atom == d->winActiveAtom &&
			d->activeWindow != ad->activeWindow)
		{
			ad->activeWindow = d->activeWindow;
			w = findWindowAtDisplay(d, d->activeWindow);
			
			if (w)
			{
				ANIM_SCREEN(w->screen);
				if (!(as->focusEffect == AnimEffectFocusFade ||
					  as->focusEffect == AnimEffectDodge))
					initiateFocusAnimation(w);
			}
		}
		break;
	default:
		break;
	}
}

static Bool animDamageWindowRect(CompWindow * w, Bool initial, BoxPtr rect)
{
	Bool status;

	ANIM_SCREEN(w->screen);

	if (initial)				// Unminimize or Create
	{
		ANIM_WINDOW(w);

		if (aw->state == IconicState)
		{
			if (!w->invisible && as->minimizeEffect &&
			    matchEval (&as->opt[ANIM_SCREEN_OPTION_MINIMIZE_MATCH].value.match, w))
			{
				// UNMINIMIZE event!

				//printf("UNMINIMIZE event! %X\n", (unsigned)w->id);

				//IPCS_SetBool(IPCS_OBJECT(w), aw->animatedAtom, TRUE);
				Bool startingNew = TRUE;

				if (aw->curWindowEvent != WindowEventNone)
				{
					if (aw->curWindowEvent != WindowEventMinimize)
						postAnimationCleanup(w, TRUE);
					else
					{
						// Play the minimize effect backwards from where it left
						aw->animRemainingTime =
								aw->animTotalTime - aw->animRemainingTime;

						// avoid window remains
						if (aw->animRemainingTime <= 0)
							aw->animRemainingTime = 1;

						startingNew = FALSE;
						if (aw->animOverrideProgressDir == 0)
							aw->animOverrideProgressDir = 1;
						else if (aw->animOverrideProgressDir == 2)
							aw->animOverrideProgressDir = 0;
					}
				}

				Bool playEffect = TRUE;

				if (startingNew)
				{
					AnimEffect effectToBePlayed;
					effectToBePlayed = animGetAnimEffect(
						as->minimizeEffect,
						as->minimizeRandomEffects,
						as->nMinimizeRandomEffects,
						as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

					// handle empty random effect list
					if (effectToBePlayed == AnimEffectNone)
						playEffect = FALSE;

					if (playEffect)
					{
						aw->curAnimEffect = effectToBePlayed;

						aw->animTotalTime =
							as->opt[ANIM_SCREEN_OPTION_MINIMIZE_DURATION].value.f * 1000;

						// allow extra time for spring damping
						if (effectToBePlayed == AnimEffectZoom ||
							effectToBePlayed == AnimEffectSidekick)
							aw->animTotalTime /= SPRING_PERCEIVED_T;

						aw->animRemainingTime = aw->animTotalTime;
					}
				}

				if (playEffect)
				{
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventUnminimize;

					// Store coords in this viewport to omit 3d effect
					// painting in other viewports
					aw->lastKnownCoords.x = w->attrib.x;
					aw->lastKnownCoords.y = w->attrib.y;

					if (animEnsureModel
						(w, WindowEventUnminimize, aw->curAnimEffect))
					{
						if (!animGetWindowIconGeometry(w, &aw->icon))
						{
							// minimize to bottom-center if there is no window list
							aw->icon.x = w->screen->width / 2;
							aw->icon.y = w->screen->height;
							aw->icon.width = 100;
							aw->icon.height = 20;
						}
						if ((aw->curAnimEffect == AnimEffectZoom || 
							 aw->curAnimEffect == AnimEffectSidekick) &&
							(as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterOn ||
							 as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterMin))
						{
							aw->icon.x =
									WIN_X(w) + WIN_W(w) / 2 - aw->icon.width / 2;
							aw->icon.y =
									WIN_Y(w) + WIN_H(w) / 2 - aw->icon.height / 2;
						}
						addWindowDamage(w);
					}
					else
						postAnimationCleanup(w, TRUE);
				}
			}
		}
		else if (aw->nowShaded)
		{
			// UNSHADE event!
			//printf("UNSHADE event! %X\n", (unsigned)w->id);

			//IPCS_SetBool(IPCS_OBJECT(w), aw->animatedAtom, TRUE);
			aw->nowShaded = FALSE;

			if (as->shadeEffect && 
			    matchEval (&as->opt[ANIM_SCREEN_OPTION_SHADE_MATCH].value.match, w))
			{
				Bool startingNew = TRUE;

				if (aw->curWindowEvent != WindowEventNone)
				{
					if (aw->curWindowEvent != WindowEventShade)
						postAnimationCleanup(w, TRUE);
					else
					{
						// Play the shade effect backwards from where it left
						aw->animRemainingTime =
								aw->animTotalTime - aw->animRemainingTime;

						// avoid window remains
						if (aw->animRemainingTime <= 0)
							aw->animRemainingTime = 1;

						startingNew = FALSE;
						if (aw->animOverrideProgressDir == 0)
							aw->animOverrideProgressDir = 1;
						else if (aw->animOverrideProgressDir == 2)
							aw->animOverrideProgressDir = 0;
					}
				}

				Bool playEffect = TRUE;

				if (startingNew)
				{
					AnimEffect effectToBePlayed;
					effectToBePlayed = animGetAnimEffect(
						as->shadeEffect,
						as->shadeRandomEffects,
						as->nShadeRandomEffects,
						as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

					// handle empty random effect list
					if (effectToBePlayed == AnimEffectNone)
						playEffect = FALSE;

					if (playEffect)
					{
						aw->curAnimEffect = effectToBePlayed;

						aw->animTotalTime =
							as->opt[ANIM_SCREEN_OPTION_SHADE_DURATION].value.f * 1000;
						aw->animRemainingTime = aw->animTotalTime;
					}
				}

				if (playEffect)
				{
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventUnshade;

					// Store coords in this viewport to omit 3d effect
					// painting in other viewports
					aw->lastKnownCoords.x = w->attrib.x;
					aw->lastKnownCoords.y = w->attrib.y;

					if (animEnsureModel(w, WindowEventUnshade, aw->curAnimEffect))
						addWindowDamage(w);
					else
						postAnimationCleanup(w, TRUE);
				}
			}
		}
		else if (!w->invisible)
		{
			aw->created = TRUE;

			AnimEffect windowsCreateEffect = AnimEffectNone;

			int whichCreate = 1;	// either 1 or 2

			if (as->create1Effect &&
			    matchEval (&as->opt[ANIM_SCREEN_OPTION_CREATE1_MATCH].value.match, w))
				windowsCreateEffect = as->create1Effect;
			else if (as->create2Effect &&
					 matchEval (&as->opt[ANIM_SCREEN_OPTION_CREATE2_MATCH].value.match, w))
			{
				windowsCreateEffect = as->create2Effect;
				whichCreate = 2;
			}

			if (windowsCreateEffect &&
				// suppress switcher window
				// (1st window that opens after switcher becomes active)
				(!as->switcherActive || as->switcherWinOpeningSuppressed) &&
				getMousePointerXY(w->screen, &aw->icon.x, &aw->icon.y))
			{
				// CREATE event!

				//printf("CREATE event! %X\n", (unsigned)w->id);

				Bool startingNew = TRUE;

				if (aw->curWindowEvent != WindowEventNone)
				{
					if (aw->curWindowEvent != WindowEventClose)
						postAnimationCleanup(w, TRUE);
					else
					{
						// Play the close effect backwards from where it left
						aw->animRemainingTime =
								aw->animTotalTime - aw->animRemainingTime;

						// avoid window remains
						if (aw->animRemainingTime == 0)
							aw->animRemainingTime = 1;

						startingNew = FALSE;
						if (aw->animOverrideProgressDir == 0)
							aw->animOverrideProgressDir = 1;
						else if (aw->animOverrideProgressDir == 2)
							aw->animOverrideProgressDir = 0;
					}
				}

				Bool playEffect = TRUE;

				if (startingNew)
				{
					AnimEffect effectToBePlayed;
					effectToBePlayed = animGetAnimEffect(
						windowsCreateEffect,
						(whichCreate == 1) ? as->create1RandomEffects :
						                     as->create2RandomEffects,
						(whichCreate == 1) ? as->nCreate1RandomEffects :
						                     as->nCreate2RandomEffects,
						as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

					// handle empty random effect list
					if (effectToBePlayed == AnimEffectNone)
						playEffect = FALSE;

					if (playEffect)
					{
						aw->curAnimEffect = effectToBePlayed;

						aw->animTotalTime =
							as->opt[whichCreate == 1 ?
									ANIM_SCREEN_OPTION_CREATE1_DURATION	:
									ANIM_SCREEN_OPTION_CREATE2_DURATION].value.f * 1000;

						// allow extra time for spring damping
						if (effectToBePlayed == AnimEffectZoom ||
							effectToBePlayed == AnimEffectSidekick)
							aw->animTotalTime /= SPRING_PERCEIVED_T;

						aw->animRemainingTime = aw->animTotalTime;
					}
				}

				if (playEffect)
				{
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventCreate;

					aw->icon.width = FAKE_ICON_SIZE;
					aw->icon.height = FAKE_ICON_SIZE;

					if (aw->curAnimEffect == AnimEffectMagicLamp)
						aw->icon.width = 
							MAX(aw->icon.width,
								as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_CREATE_START_WIDTH].value.i);
					else if (aw->curAnimEffect == AnimEffectMagicLampVacuum)
						aw->icon.width =
							MAX(aw->icon.width,
								as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_CREATE_START_WIDTH].value.i);

					aw->icon.x -= aw->icon.width / 2;
					aw->icon.y -= aw->icon.height / 2;

					if ((aw->curAnimEffect == AnimEffectZoom ||
						 aw->curAnimEffect == AnimEffectSidekick) &&
						(as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterOn ||
						 as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterCreate))
					{
						aw->icon.x = WIN_X(w) + WIN_W(w) / 2 - aw->icon.width / 2;
						aw->icon.y = WIN_Y(w) + WIN_H(w) / 2 - aw->icon.height / 2;
					}
					aw->state = IconicState;	// we're doing this as a hack, it may not be necessary

					// Store coords in this viewport to omit 3d effect
					// painting in other viewports
					if (aw->lastKnownCoords.x != NOT_INITIALIZED)
					{
						aw->lastKnownCoords.x = w->attrib.x;
						aw->lastKnownCoords.y = w->attrib.y;
					}
					if (animEnsureModel(w, WindowEventCreate, aw->curAnimEffect))
						addWindowDamage(w);
					else
						postAnimationCleanup(w, TRUE);
				}
			}
			else if (as->switcherActive && !as->switcherWinOpeningSuppressed)
			{
				// done suppressing open animation
				as->switcherWinOpeningSuppressed = TRUE;
			}
		}

		aw->newState = NormalState;
	}

	UNWRAP(as, w->screen, damageWindowRect);
	status = (*w->screen->damageWindowRect) (w, initial, rect);
	WRAP(as, w->screen, damageWindowRect, animDamageWindowRect);

	return status;
}

static void animWindowResizeNotify(CompWindow * w, int dx, int dy, int dwidth, int dheight)//, Bool preview)
{
	ANIM_SCREEN(w->screen);
	ANIM_WINDOW(w);

	if (aw->polygonSet && !aw->animInitialized)
	{
		// to refresh polygon coords
		freePolygonSet(aw);
	}

	//if (!preview && (dx || dy || dwidth || dheight))
	//{
	if (aw->animRemainingTime > 0)
	{
		aw->animRemainingTime = 0;
		postAnimationCleanup(w, TRUE);
	}

	if (aw->model)
	{
		modelInitObjects(aw->model, 
						 WIN_X(w), WIN_Y(w), 
						 WIN_W(w), WIN_H(w));
	}
	//}

	aw->state = w->state;

	UNWRAP(as, w->screen, windowResizeNotify);
	(*w->screen->windowResizeNotify) (w, dx, dy, dwidth, dheight);//, preview);
	WRAP(as, w->screen, windowResizeNotify, animWindowResizeNotify);
}

static void
animWindowMoveNotify(CompWindow * w, int dx, int dy, Bool immediate)
{
	ANIM_SCREEN(w->screen);
	ANIM_WINDOW(w);

	if (!(aw->animRemainingTime > 0 &&
		  (aw->curAnimEffect == AnimEffectFocusFade ||
		   aw->curAnimEffect == AnimEffectDodge)))
	{
		CompWindow *w2;

		if (aw->polygonSet && !aw->animInitialized)
		{
			// to refresh polygon coords
			freePolygonSet(aw);
		}
		if (aw->animRemainingTime > 0 && aw->grabbed)
		{
			aw->animRemainingTime = 0;
			if (as->animInProgress)
			{
				as->animInProgress = FALSE;
				for (w2 = w->screen->windows; w2; w2 = w2->next)
				{
					AnimWindow *aw2;

					aw2 = GET_ANIM_WINDOW(w2, as);
					if (aw2->animRemainingTime > 0)
					{
						as->animInProgress = TRUE;
						break;
					}
				}
			}
			postAnimationCleanup(w, TRUE);
		}

		if (aw->model)
		{
			modelInitObjects(aw->model, WIN_X(w), WIN_Y(w), WIN_W(w),
							 WIN_H(w));
		}
	}
	UNWRAP(as, w->screen, windowMoveNotify);
	(*w->screen->windowMoveNotify) (w, dx, dy, immediate);
	WRAP(as, w->screen, windowMoveNotify, animWindowMoveNotify);
}

static void
animWindowGrabNotify(CompWindow * w,
					 int x, int y, unsigned int state, unsigned int mask)
{
	ANIM_SCREEN(w->screen);
	ANIM_WINDOW(w);

	aw->grabbed = TRUE;

	UNWRAP(as, w->screen, windowGrabNotify);
	(*w->screen->windowGrabNotify) (w, x, y, state, mask);
	WRAP(as, w->screen, windowGrabNotify, animWindowGrabNotify);
}

static void animWindowUngrabNotify(CompWindow * w)
{
	ANIM_SCREEN(w->screen);
	ANIM_WINDOW(w);

	aw->grabbed = FALSE;

	UNWRAP(as, w->screen, windowUngrabNotify);
	(*w->screen->windowUngrabNotify) (w);
	WRAP(as, w->screen, windowUngrabNotify, animWindowUngrabNotify);
}

static Bool
animPaintOutput(CompScreen * s,
				const ScreenPaintAttrib * sAttrib,
				const CompTransform    *transform,
				Region region, CompOutput *output, 
				unsigned int mask)
{
	Bool status;

	ANIM_SCREEN(s);

	if (as->animInProgress)
	{
		// Find out if an animation running now uses depth test
		Bool depthUsed = FALSE;
		CompWindow *w;
		for (w = s->windows; w; w = w->next)
		{
			ANIM_WINDOW(w);
			if (aw->animRemainingTime > 0 &&
				aw->polygonSet &&
				aw->polygonSet->doDepthTest)
			{
				depthUsed = TRUE;
				break;
			}
		}
		if (depthUsed)
		{
			glClearDepth(1000.0f);
			glClear(GL_DEPTH_BUFFER_BIT);
		}
		mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;
	}
	UNWRAP(as, s, paintOutput);
 	status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
	WRAP(as, s, paintOutput, animPaintOutput);

	CompWindow *w;
	if (as->aWinWasRestackedJustNow)
	{
		as->aWinWasRestackedJustNow = FALSE;
	}
	if (as->markAllWinCreatedCountdown > 0)
	{
		if (as->markAllWinCreatedCountdown == 1)
		{
			// Mark all windows as "created"
			for (w = s->windows; w; w = w->next)
			{
				ANIM_WINDOW(w);
				aw->created = TRUE;
			}
		}
		as->markAllWinCreatedCountdown--;
	}
	return status;
}

static Bool animInitDisplay(CompPlugin * p, CompDisplay * d)
{
	AnimDisplay *ad;

	ad = calloc(1, sizeof(AnimDisplay));
	if (!ad)
		return FALSE;

	ad->screenPrivateIndex = allocateScreenPrivateIndex(d);
	if (ad->screenPrivateIndex < 0)
	{
		free(ad);
		return FALSE;
	}

	ad->wmHintsAtom = XInternAtom(d->display, "WM_HINTS", FALSE);
	ad->winIconGeometryAtom =
			XInternAtom(d->display, "_NET_WM_ICON_GEOMETRY", 0);

	WRAP(ad, d, handleEvent, animHandleEvent);
	WRAP(ad, d, handleCompizEvent, animHandleCompizEvent);

	d->privates[animDisplayPrivateIndex].ptr = ad;

	return TRUE;
}

static void animFiniDisplay(CompPlugin * p, CompDisplay * d)
{
	ANIM_DISPLAY(d);

	freeScreenPrivateIndex(d, ad->screenPrivateIndex);

	UNWRAP(ad, d, handleCompizEvent);
	UNWRAP(ad, d, handleEvent);

	free(ad);
}

static Bool animInitScreen(CompPlugin * p, CompScreen * s)
{
	AnimScreen *as;
	
	ANIM_DISPLAY(s->display);

	as = calloc(1, sizeof(AnimScreen));
	if (!as)
		return FALSE;

	if (!compInitScreenOptionsFromMetadata (s,
											&animMetadata,
											animScreenOptionInfo,
											as->opt,
											ANIM_SCREEN_OPTION_NUM))
	{
		free (as);
		return FALSE;
	}

	as->windowPrivateIndex = allocateWindowPrivateIndex(s);
	if (as->windowPrivateIndex < 0)
	{
		compFiniScreenOptions (s, as->opt, ANIM_SCREEN_OPTION_NUM);
		free(as);
		return FALSE;
	}

	as->animInProgress = FALSE;

	as->minimizeEffect = minimizeEffectType[
			as->opt[ANIM_SCREEN_OPTION_MINIMIZE_EFFECT].value.i];
	as->create1Effect = closeEffectType[
			as->opt[ANIM_SCREEN_OPTION_CREATE1_EFFECT].value.i];
	as->create2Effect = closeEffectType[
			as->opt[ANIM_SCREEN_OPTION_CREATE2_EFFECT].value.i];
	as->close1Effect = closeEffectType[
			as->opt[ANIM_SCREEN_OPTION_CLOSE1_EFFECT].value.i];
	as->close2Effect = closeEffectType[
			as->opt[ANIM_SCREEN_OPTION_CLOSE2_EFFECT].value.i];
	as->focusEffect = focusEffectType[
			as->opt[ANIM_SCREEN_OPTION_FOCUS_EFFECT].value.i];
	as->shadeEffect = shadeEffectType[
			as->opt[ANIM_SCREEN_OPTION_SHADE_EFFECT].value.i];
	
	animStoreRandomEffectList (
		&as->opt[ANIM_SCREEN_OPTION_MINIMIZE_RANDOM_EFFECTS].value,
		minimizeEffectType + RANDOM_EFFECT_OFFSET,
		NUM_MINIMIZE_EFFECT - RANDOM_EFFECT_OFFSET,
		as->minimizeRandomEffects,
		&as->nMinimizeRandomEffects);
	animStoreRandomEffectList (
		&as->opt[ANIM_SCREEN_OPTION_CLOSE1_RANDOM_EFFECTS].value,
		closeEffectType + RANDOM_EFFECT_OFFSET,
		NUM_CLOSE_EFFECT - RANDOM_EFFECT_OFFSET,
		as->close1RandomEffects,
		&as->nClose1RandomEffects);
	animStoreRandomEffectList (
		&as->opt[ANIM_SCREEN_OPTION_CLOSE2_RANDOM_EFFECTS].value,
		closeEffectType + RANDOM_EFFECT_OFFSET,
		NUM_CLOSE_EFFECT - RANDOM_EFFECT_OFFSET,
		as->close2RandomEffects,
		&as->nClose2RandomEffects);
	animStoreRandomEffectList (
		&as->opt[ANIM_SCREEN_OPTION_CREATE1_RANDOM_EFFECTS].value,
		closeEffectType + RANDOM_EFFECT_OFFSET,
		NUM_CLOSE_EFFECT - RANDOM_EFFECT_OFFSET,
		as->create1RandomEffects,
		&as->nCreate1RandomEffects);
	animStoreRandomEffectList (
		&as->opt[ANIM_SCREEN_OPTION_CREATE2_RANDOM_EFFECTS].value,
		closeEffectType + RANDOM_EFFECT_OFFSET,
		NUM_CLOSE_EFFECT - RANDOM_EFFECT_OFFSET,
		as->create2RandomEffects,
		&as->nCreate2RandomEffects);
	animStoreRandomEffectList (
		&as->opt[ANIM_SCREEN_OPTION_SHADE_RANDOM_EFFECTS].value,
		shadeEffectType + RANDOM_EFFECT_OFFSET,
		NUM_SHADE_EFFECT - RANDOM_EFFECT_OFFSET,
		as->shadeRandomEffects,
		&as->nShadeRandomEffects);

	as->switcherActive = FALSE;
	as->groupTabChangeActive = FALSE;
	as->scaleActive = FALSE;

	WRAP(as, s, preparePaintScreen, animPreparePaintScreen);
	WRAP(as, s, donePaintScreen, animDonePaintScreen);
	WRAP(as, s, paintOutput, animPaintOutput);
	WRAP(as, s, paintWindow, animPaintWindow);
	WRAP(as, s, damageWindowRect, animDamageWindowRect);
	WRAP(as, s, addWindowGeometry, animAddWindowGeometry);
	WRAP(as, s, drawWindowTexture, animDrawWindowTexture);
	//WRAP(as, s, drawWindowGeometry, animDrawWindowGeometry);
	WRAP(as, s, windowResizeNotify, animWindowResizeNotify);
	WRAP(as, s, windowMoveNotify, animWindowMoveNotify);
	WRAP(as, s, windowGrabNotify, animWindowGrabNotify);
	WRAP(as, s, windowUngrabNotify, animWindowUngrabNotify);

	as->markAllWinCreatedCountdown = 5; // start countdown

	s->privates[ad->screenPrivateIndex].ptr = as;

	return TRUE;
}

static void animFiniScreen(CompPlugin * p, CompScreen * s)
{
	ANIM_SCREEN(s);

	freeWindowPrivateIndex(s, as->windowPrivateIndex);

	if (as->lastClientListStacking)
		free(as->lastClientListStacking);

	UNWRAP(as, s, preparePaintScreen);
	UNWRAP(as, s, donePaintScreen);
	UNWRAP(as, s, paintOutput);
	UNWRAP(as, s, paintWindow);
	UNWRAP(as, s, damageWindowRect);
	UNWRAP(as, s, addWindowGeometry);
	UNWRAP(as, s, drawWindowTexture);
	//UNWRAP(as, s, drawWindowGeometry);
	UNWRAP(as, s, windowResizeNotify);
	UNWRAP(as, s, windowMoveNotify);
	UNWRAP(as, s, windowGrabNotify);
	UNWRAP(as, s, windowUngrabNotify);

	compFiniScreenOptions (s, as->opt, ANIM_SCREEN_OPTION_NUM);

	free(as);
}

static Bool animInitWindow(CompPlugin * p, CompWindow * w)
{
	AnimWindow *aw;

	ANIM_SCREEN(w->screen);

	aw = calloc(1, sizeof(AnimWindow));
	if (!aw)
		return FALSE;

	//aw->animatedAtom = IPCS_GetAtom(IPCS_OBJECT(w), IPCS_BOOL,
	//								"IS_ANIMATED", TRUE);
	aw->model = 0;
	aw->state = w->state;
	aw->animRemainingTime = 0;
	aw->animInitialized = FALSE;
	aw->curAnimEffect = AnimEffectNone;
	aw->curWindowEvent = WindowEventNone;
	aw->animOverrideProgressDir = 0;
	w->indexCount = 0;

	aw->polygonSet = NULL;
	aw->lastKnownCoords.x = NOT_INITIALIZED;
	aw->lastKnownCoords.y = NOT_INITIALIZED;

	aw->unmapCnt = 0;
	aw->destroyCnt = 0;

	aw->grabbed = FALSE;

	aw->useDrawRegion = FALSE;
	aw->drawRegion = NULL;

	if (w->shaded)
	{
		aw->state = aw->newState = NormalState;
		aw->nowShaded = TRUE;
	}
	else
	{
		aw->state = aw->newState = animGetWindowState(w);
		aw->nowShaded = FALSE;
	}

	w->privates[as->windowPrivateIndex].ptr = aw;

	return TRUE;
}

static void animFiniWindow(CompPlugin * p, CompWindow * w)
{
	ANIM_WINDOW(w);

	postAnimationCleanup(w, FALSE);

	if (aw->model)
	{
		if (aw->model->magicLampWaves)
			free(aw->model->magicLampWaves);
		aw->model->magicLampWaves = 0;
		free(aw->model->objects);
		aw->model->objects = 0;
		free(aw->model);
		aw->model = 0;
	}
	if (aw->restackInfo)
	{
		free(aw->restackInfo);
		aw->restackInfo = NULL;
	}

	while (aw->unmapCnt--)
		unmapWindow(w);

	free(aw);
}

static Bool animInit(CompPlugin * p)
{
	if (!compInitPluginMetadataFromInfo (&animMetadata,
										  p->vTable->name,
										   0, 0,
										    animScreenOptionInfo,
											 ANIM_SCREEN_OPTION_NUM))
		return FALSE;
		
	animDisplayPrivateIndex = allocateDisplayPrivateIndex();
	if (animDisplayPrivateIndex < 0)
	{
		compFiniMetadata (&animMetadata);
		return FALSE;
	}

	compAddMetadataFromFile (&animMetadata, p->vTable->name);

	animEffectPropertiesTmp = animEffectProperties;
	return TRUE;
}

static void animFini(CompPlugin * p)
{
	freeDisplayPrivateIndex(animDisplayPrivateIndex);
	compFiniMetadata (&animMetadata);
}

static int
animGetVersion (CompPlugin *plugin,
		int	   version)
{
    return ABIVERSION;
}

static CompMetadata *
animGetMetadata (CompPlugin *plugin)
{
	return &animMetadata;
}

CompPluginVTable animVTable = {
	"animation",
	animGetVersion,
	animGetMetadata,
	animInit,
	animFini,
	animInitDisplay,
	animFiniDisplay,
	animInitScreen,
	animFiniScreen,
	animInitWindow,
	animFiniWindow,
	0,
	0,
	animGetScreenOptions,
	animSetScreenOptions,
	0,
	0,
	0,
	0
};

CompPluginVTable *getCompPluginInfo(void)
{
	return &animVTable;
}

