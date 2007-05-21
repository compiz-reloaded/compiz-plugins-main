/**
 *
 * Compiz expo plugin
 *
 * expo.c
 *
 * Copyright (c) 2006 Robert Carr <racarr@beryl-project.org>
 *
 * Authors:
 * Robert Carr <racarr@beryl-project.org>
 * Dennis Kasprzyk <onestone@beryl-project.org>
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
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <compiz.h>
#include "expo_options.h"

#include <GL/glu.h>

#define PI 3.14159265359f

#define WIN_X(w) ((w)->attrib.x - (w)->input.left)
#define WIN_Y(w) ((w)->attrib.y - (w)->input.top)
#define WIN_W(w) ((w)->width + (w)->input.left + (w)->input.right)
#define WIN_H(w) ((w)->height + (w)->input.top + (w)->input.bottom)

static int displayPrivateIndex;

enum DnDState
{
	DnDNone = 0,
	DnDDuring,
	DnDStart
};

typedef struct _ExpoDisplay
{
	int screenPrivateIndex;
	HandleEventProc handleEvent;
} ExpoDisplay;

typedef struct _ExpoScreen
{
	int windowPrivateIndex;

	DonePaintScreenProc donePaintScreen;
	PaintScreenProc paintScreen;
	PreparePaintScreenProc preparePaintScreen;
	PaintTransformedScreenProc paintTransformedScreen;
	PaintWindowProc paintWindow;
	DamageWindowRectProc damageWindowRect;
	SetScreenOptionProc setScreenOption;

 	float expoCam;				// Used for expo zoom animation
 	Bool expoActive;
 	Bool expoMode;				// In expo mode?

 	int pointerX;				// Updated in donePaintScreen with XQueryPointer if pointer location is neccesary i.e. moving miniview
 	int pointerY;

 	int grabIndex;				// For expo grab

 	GLint viewport[4];

 	int dndState;
 	CompWindow *dndWindow;		// Window being dragged in expo mode
 	int prevCursorX, prevCursorY;
 	int newCursorX, newCursorY;
 	int origVX;
 	int origVY;
 	int rorigx;
 	int rorigy;
 	int mouseOverViewX;
 	int mouseOverViewY;

	Bool anyClick;

} ExpoScreen;

typedef struct _ExpoWindow
{
	Bool hovered;				// Is a window hovered over in expo mode?
	Bool skipNotify;
	int origx;
	int origy;
} ExpoWindow;

typedef struct _xyz_tuple
{
	float x, y, z;
} Point3d;


/* Helpers */
#define GET_EXPO_DISPLAY(d) \
		((ExpoDisplay *) (d)->privates[displayPrivateIndex].ptr)
#define EXPO_DISPLAY(d) \
		ExpoDisplay *ed = GET_EXPO_DISPLAY(d);

#define GET_EXPO_SCREEN(s, ed) \
		((ExpoScreen *) (s)->privates[(ed)->screenPrivateIndex].ptr)
#define EXPO_SCREEN(s) \
		ExpoScreen *es = GET_EXPO_SCREEN(s, GET_EXPO_DISPLAY(s->display))

#define GET_EXPO_WINDOW(w, es)                                     \
        ((ExpoWindow *) (w)->privates[(es)->windowPrivateIndex].ptr)
#define EXPO_WINDOW(w)    \
        ExpoWindow *ew = GET_EXPO_WINDOW  (w,                     \
                GET_EXPO_SCREEN  (w->screen,             \
                        GET_EXPO_DISPLAY (w->screen->display)))

#define GET_SCREEN \
 	CompScreen *s;\
	Window xid; \
	xid = getIntOptionNamed(option, nOption, "root", 0); \
	s = findScreenAtDisplay(d, xid); \
        if (!s) \
            return FALSE;

#define sigmoid(x) (1.0f/(1.0f+exp(-5.5f*2*((x)-0.5))))
#define sigmoidProgress(x) ((sigmoid(x) - sigmoid(0)) / \
							(sigmoid(1) - sigmoid(0)))

static void expoHandleEvent(CompDisplay * d, XEvent * event)
{
	EXPO_DISPLAY(d);
	CompScreen *s;

	ExpoScreen *es;

	switch (event->type)
	{
	case ButtonPress:
		s = findScreenAtDisplay(d, event->xbutton.root);
		es = GET_EXPO_SCREEN(s, ed);
		if (es->expoMode)
		{
			es->anyClick = TRUE;
			damageScreen(s);
			if (event->xbutton.button == Button1)
				es->dndState = DnDStart;
			else if (event->xbutton.button != Button5)
			{
				CompWindow *w;

				for (w = s->windows; w; w = w->next)
					syncWindowPosition(w);
				if (es->grabIndex)
				{
					removeScreenGrab(s, es->grabIndex, 0);
					es->grabIndex = 0;
				}
				damageScreen(s);
				es->origVX = es->mouseOverViewX;
				es->origVY = es->mouseOverViewY;
				es->expoMode = FALSE;

				while (s->x != es->mouseOverViewX)
					moveScreenViewport(s, 1, 0, TRUE);
				while (s->y != es->mouseOverViewY)
					moveScreenViewport(s, 0, 1, TRUE);

				focusDefaultWindow(s->display);
			}
			damageScreen(s);
		}
		break;
	case ButtonRelease:
		s = findScreenAtDisplay(d, event->xbutton.root);
		es = GET_EXPO_SCREEN(s, ed);

		if (es->dndState == DnDDuring || es->dndState == DnDStart)
		{
			if (es->dndWindow)
			{
				syncWindowPosition(es->dndWindow);
				(*s->windowUngrabNotify)(es->dndWindow);
				/* update window attibutes to make sure a
				   moved maximized window is properly snapped
				   to the work area */
				updateWindowAttributes(es->dndWindow,
					CompStackingUpdateModeNone);
			}

			es->dndState = DnDNone;
			es->dndWindow = NULL;
		}
		break;
	}

	UNWRAP(ed, d, handleEvent);
	(*d->handleEvent) (d, event);
	WRAP(ed, d, handleEvent, expoHandleEvent);
}

static Bool expoExpo(CompDisplay * d, CompAction * action,
					 CompActionState state, CompOption * option, int nOption)
{
	GET_SCREEN;
	EXPO_SCREEN(s);

	if (otherScreenGrabExist(s, "expo", 0))
		return FALSE;

	es->expoMode = !es->expoMode;
	es->anyClick = FALSE;
	if (es->expoMode)
		es->grabIndex =	pushScreenGrab(s, None, "expo");
	else if (es->grabIndex)
		removeScreenGrab(s, es->grabIndex, 0);

	if (es->dndWindow)
		syncWindowPosition(es->dndWindow);
	es->dndState = DnDNone;
	es->dndWindow = 0;

	if (!es->expoMode && es->origVX >= 0 && es->origVY >= 0)
	{
		while (s->x != es->origVX)
			moveScreenViewport(s, 1, 0, TRUE);
		while (s->y != es->origVY)
			moveScreenViewport(s, 0, 1, TRUE);
	}
	if (es->expoMode)
	{
		es->origVX = -1;
		es->origVY = -1;
		es->rorigx = s->x;
		es->rorigy = s->y;
	}
	damageScreen(s);

	focusDefaultWindow(s->display);

	return TRUE;
}


//Other way around
static void invertTransformedVertex(CompScreen * s, const ScreenPaintAttrib * sAttrib,
						   const CompTransform *transform,
						   int output, int vertex[2])
{
	//Projection Matrix.
	EXPO_SCREEN(s);

	CompTransform     sTransform = *transform;

	(s->applyScreenTransform) (s, sAttrib, output, &sTransform);
	transformToScreenSpace (s, output, -sAttrib->zTranslate, &sTransform);

	glGetIntegerv(GL_VIEWPORT, es->viewport);

	GLdouble p1[3];
	GLdouble p2[3];
	GLdouble v[3];
	GLdouble alpha;
	int i;

	GLdouble mvm[16];
	GLdouble pm[16];

	for (i = 0; i < 16; i++)
	{
		mvm[i] = sTransform.m[i];
		pm[i] = s->projection[i];
	}

 	gluUnProject(vertex[0], s->height - vertex[1], 0, mvm, pm,
				 es->viewport, &p1[0], &p1[1], &p1[2]);
	gluUnProject(vertex[0], s->height - vertex[1], -1.0, mvm, pm,
				 es->viewport, &p2[0], &p2[1], &p2[2]);

	for (i = 0; i < 3; i++)
		v[i] = p1[i] - p2[i];

	alpha = -p1[2] / v[2];

	vertex[0] = ceil(p1[0] + (alpha * v[0]));
	vertex[1] = ceil(p1[1] + (alpha * v[1]));
}


static Bool expoDamageWindowRect(CompWindow *w, Bool initial, BoxPtr rect)
{
	EXPO_SCREEN(w->screen);
	Bool status;

	UNWRAP(es, w->screen, damageWindowRect);
	status = (*w->screen->damageWindowRect) (w, initial, rect);
	WRAP(es, w->screen, damageWindowRect, expoDamageWindowRect);

	if (es->expoCam > 0.0f)
		damageScreen(w->screen);

	return status;
}

static Bool expoPaintScreen(CompScreen * s,
							const ScreenPaintAttrib * sAttrib,
							const CompTransform    *transform,
							Region region, int output, unsigned int mask)
{
	Bool status;

	EXPO_SCREEN(s);

	if (es->expoCam > 0.0)
	{
		mask |= PAINT_SCREEN_TRANSFORMED_MASK;
	}

	UNWRAP(es, s, paintScreen);
	status = (*s->paintScreen) (s, sAttrib, transform, region, output, mask);
	WRAP(es, s, paintScreen, expoPaintScreen);

	if (es->expoMode)
	{
		CompTransform     sTransform = *transform;
		transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);
	}

	return status;
}

static void expoPreparePaintScreen(CompScreen * s, int ms)
{
	EXPO_SCREEN(s);

	if (es->expoMode)	// Do we need the mouse location?
	{
		int winX, winY;
		int rootX, rootY;
		unsigned int mask_return;
		Window root_return;
		Window child_return;

		XQueryPointer(s->display->display, s->root,
					  &root_return, &child_return,
					  &rootX, &rootY, &winX, &winY, &mask_return);

		es->pointerX = rootX;
		es->pointerY = rootY;
		es->expoCam = MIN(1.0, es->expoCam + ((float)ms / 500.0));
	}
	else
	{
		es->expoCam = MAX(0.0, es->expoCam - ((float)ms / 500.0));
	}

	UNWRAP(es, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, ms);
	WRAP(es, s, preparePaintScreen, expoPreparePaintScreen);
}

static void expoPaintTransformedScreen(CompScreen * s,
									   const ScreenPaintAttrib * sAttrib,
									   const CompTransform    *transform,
									   Region region, int output,
									   unsigned int mask)
{
	EXPO_SCREEN(s);

	CompTransform     sTransform = *transform;


	UNWRAP(es, s, paintTransformedScreen);

	es->expoActive = FALSE;

	if (es->expoCam > 0)
	    mask |= PAINT_SCREEN_CLEAR_MASK;

    (*s->paintTransformedScreen) (s, sAttrib, &sTransform, region, output, mask);

	mask &= ~PAINT_SCREEN_CLEAR_MASK;

	if (es->expoCam > 0.0)
	{
		int oldFilter = s->display->textureFilter;

		if (es->expoCam == 1 && expoGetMipmaps(s->display))
			s->display->textureFilter = GL_LINEAR_MIPMAP_LINEAR;

		int origVX = s->x;
		int origVY = s->y;

		const float gapy = 0.01f * es->expoCam; // amount of gap between viewports
		const float gapx = 0.01f * s->height / s->width * es->expoCam;

		// Zoom animation stuff

		Point3d vpCamPos = {0, 0, 0};   // camera position for the selected viewport
		Point3d expoCamPos = {0, 0, 0}; // camera position during expo mode

		float hss = (float)s->width / s->outputDev[output].width;
		float vss = (float)s->height / s->outputDev[output].height;

		vpCamPos.x = s->hsize * hss * ((s->x + 0.5) / s->hsize - 0.5) + gapx * (s->x);
		vpCamPos.y = -s->vsize * vss * ((s->y + 0.5) / s->vsize - 0.5) - gapy * (s->y);
		vpCamPos.z = 0;

		float biasz = 0;
		if (expoGetRotate(s->display))
			biasz = MAX(s->hsize, s->vsize) * 0.15;

		expoCamPos.x = gapx * (s->hsize - 1) * 0.5;
		expoCamPos.y = -gapy * (s->vsize - 1) * 0.5;
		expoCamPos.z = -DEFAULT_Z_CAMERA +
			DEFAULT_Z_CAMERA * (MAX(s->hsize +
									(s->hsize - 1) * gapx /
									(s->width / s->outputDev[output].width),
									s->vsize +
									(s->vsize - 1) * gapy /
									(s->height / s->outputDev[output].height))
								+ biasz);

		float progress = sigmoidProgress(es->expoCam);

		// interpolate between vpCamPos and expoCamPos
		float camx = vpCamPos.x * (1 - progress) + expoCamPos.x * progress;
		float camy = vpCamPos.y * (1 - progress) + expoCamPos.y * progress;
		float camz = vpCamPos.z * (1 - progress) + expoCamPos.z * progress;

		// End of Zoom animation stuff


		moveScreenViewport(s, s->x, s->y, FALSE);

		float rotation = 0.0;

		if (expoGetRotate(s->display))
		{
			if (expoGetExpoAnimationIndex(s->display) == ExpoAnimationZoom)
				rotation = 10.0 * sigmoidProgress(es->expoCam);
			else
				rotation = 10.0 * es->expoCam;
		}
		// ALL TRANSFORMATION ARE EXECUTED FROM BOTTOM TO TOP


		// move each screen to the correct output position
		matrixTranslate(&sTransform, -s->outputDev[output].region.extents.x1 /
					 s->outputDev[output].width,
					 s->outputDev[output].region.extents.y1 /
					 s->outputDev[output].height, 0.0f);
		matrixTranslate(&sTransform, 0.0f, 0.0f, -DEFAULT_Z_CAMERA);

		// zoom out
		matrixTranslate(&sTransform, -camx, -camy, -camz);

		// move orgin to top left
		matrixTranslate(&sTransform, -0.5f, 0.5f, 0.0f);

		// translate expo to orgin
		matrixTranslate(&sTransform, (s->width / s->outputDev[output].width) * 0.5,
					 -(s->height / s->outputDev[output].height) * 0.5, 0.0f);

		// rotate
		matrixRotate(&sTransform, rotation, 0.0f, 1.0f, 0.0f);

		// translate expo to center
		matrixTranslate(&sTransform, -(s->width / s->outputDev[output].width) * s->hsize *
					 0.5,
					 (s->height / s->outputDev[output].height) * s->vsize *
					 0.5, 0.0f);

		// revert prepareXCoords region shift. Now all screens display the same
		matrixTranslate(&sTransform, 0.5f, 0.5f, DEFAULT_Z_CAMERA);
		matrixTranslate(&sTransform, s->outputDev[output].region.extents.x1 /
					 s->outputDev[output].width,
					 -s->outputDev[output].region.extents.y2 /
					 s->outputDev[output].height, 0.0f);

		int i, j;

		es->expoActive = TRUE;

		for (j = 0; j < s->vsize; j++)
		{

			CompTransform  sTransform2 = sTransform;
			for (i = 0; i < s->hsize; i++)
			{
				if (expoGetExpoAnimationIndex(s->display) == ExpoAnimationVortex)
 					matrixRotate(&sTransform2, 360 * es->expoCam, 0.0f, 1.0f,
							  2.0f * es->expoCam);

				(*s->paintTransformedScreen) (s, sAttrib, &sTransform2, &s->region, output,
											  mask);


				if ((es->pointerX >= s->outputDev[output].region.extents.x1)
					&& (es->pointerX < s->outputDev[output].region.extents.x2)
					&& (es->pointerY >=
						s->outputDev[output].region.extents.y1) &&
					(es->pointerY < s->outputDev[output].region.extents.y2))
				{
					int cursor[2] = { es->pointerX, es->pointerY };

					invertTransformedVertex(s, sAttrib, &sTransform2, output, cursor);

					if ((cursor[0] > 0) && (cursor[0] < s->width) &&
						(cursor[1] > 0) && (cursor[1] < s->height))
					{
						es->mouseOverViewX = i;
						es->mouseOverViewY = j;
						es->newCursorX = i * s->width + cursor[0];
						es->newCursorY = j * s->height + cursor[1];
						if (es->anyClick || es->dndState != DnDNone)
						{

							es->origVX = i;	// Used to save last viewport interaction was in
							es->origVY = j;
							es->anyClick = FALSE;
						}
					}
				}


				// not sure this will work with different resolutions
				matrixTranslate(&sTransform2,(s->width / s->outputDev[output].width + gapx),
							 0.0f, 0.0);

				moveScreenViewport(s, -1, 0, FALSE);
			}

			// not sure this will work with different resolutions
 			matrixTranslate(&sTransform, 0,
						 (-s->height / s->outputDev[output].height - gapy),
						 0.0f);

			moveScreenViewport(s, 0, -1, FALSE);
		}

		es->expoActive = FALSE;
		moveScreenViewport(s, -origVX, -origVY, FALSE);

		s->filter[SCREEN_TRANS_FILTER] = oldFilter;
		s->display->textureFilter = oldFilter;
	}

	WRAP(es, s, paintTransformedScreen, expoPaintTransformedScreen);
}

static Bool
expoPaintWindow(CompWindow * w,
			   const WindowPaintAttrib *attrib,
			   const CompTransform    *transform,
			   Region region, unsigned int mask)
{
	EXPO_SCREEN(w->screen);
	Bool status;

	WindowPaintAttrib pA = *attrib;

	if (es->expoCam > 0.0)
	{
		if (es->expoActive)
		{
			if (expoGetExpoAnimationIndex(w->screen->display) != ExpoAnimationZoom)
				pA.opacity = attrib->opacity * es->expoCam;

			if (w->wmType & CompWindowTypeDockMask)
			{
				if (expoGetExpoAnimationIndex(w->screen->display) == ExpoAnimationZoom &&
					((w->screen->x == es->origVX &&
					  w->screen->y == es->origVY) ||
					 (w->screen->x == es->rorigx &&
					  w->screen->y == es->rorigy &&
					  es->origVY < 0 &&
					  es->origVX < 0)))
					pA.opacity = attrib->opacity * (1 - sigmoidProgress(es->expoCam));
				else
					pA.opacity = 0;
			}

			if (w->screen->x == es->origVX && w->screen->y == es->origVY)
			{
				pA.brightness = attrib->brightness;
			}
			else if (w->screen->x == es->rorigx && w->screen->y == es->rorigy &&
				es->origVY < 0 && es->origVX < 0)
			{
				pA.brightness = attrib->brightness;
			}
			else
				pA.brightness = attrib->brightness * .75;
		}
		else
		{
			if (expoGetExpoAnimationIndex(w->screen->display) == ExpoAnimationZoom)
				pA.brightness = 0;
			else
				pA.brightness = attrib->brightness * (1 - sigmoidProgress(es->expoCam));
		}
	}

	if (!pA.opacity || !pA.brightness)
		mask |= PAINT_WINDOW_NO_CORE_INSTANCE_MASK;

	UNWRAP(es, w->screen, paintWindow);
	status = (*w->screen->paintWindow) (w, &pA, transform, region, mask);
	WRAP(es, w->screen, paintWindow, expoPaintWindow);

	return status;
}

static void expoDonePaintScreen(CompScreen * s)
{
	EXPO_SCREEN(s);

	if ((es->expoCam > 0.0f && es->expoCam < 1.0f) || es->dndState != DnDNone)
		damageScreen(s);

	UNWRAP(es, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP(es, s, donePaintScreen, expoDonePaintScreen);

	if (es->dndState == DnDDuring)
	{
		int dx = es->newCursorX - es->prevCursorX;
		int dy = es->newCursorY - es->prevCursorY;

		if (es->dndWindow)
			moveWindow(es->dndWindow, dx, dy, TRUE, expoGetExpoImmediateMove(s->display));

		es->prevCursorX = es->newCursorX;
		es->prevCursorY = es->newCursorY;

		damageScreen(s);
	}

	if (es->dndState != DnDStart)
		return;

	int origView = s->x;
	int origViewY = s->y;

	// needs to be moved into handle event
	moveScreenViewport(s, s->x, s->y, FALSE);

	CompWindow *w;

	for (w = s->reverseWindows; w; w = w->prev)
	{
		if (w->destroyed)
			continue;

		if (!w->shaded)
		{
			if (w->attrib.map_state != IsViewable || !w->damaged)
				continue;
		}

		if (w->type & CompWindowTypeNormalMask)
		{
			if (es->newCursorX < WIN_X(w) ||
				es->newCursorX > WIN_X(w) + WIN_W(w))
				continue;
			if (es->newCursorY < WIN_Y(w) ||
				es->newCursorY > WIN_Y(w) + WIN_H(w))
				continue;
			es->dndState = DnDDuring;
			es->dndWindow = w;
			(*w->screen->windowGrabNotify) (w, es->newCursorX, es->newCursorY, 0,
					CompWindowGrabMoveMask | CompWindowGrabButtonMask);
			break;
		}
	}
	if (es->dndWindow)
	{
		raiseWindow(es->dndWindow);
		moveInputFocusToWindow(es->dndWindow);
	}

	moveScreenViewport(s, -origView, -origViewY, FALSE);

	for (w = s->windows; w; w = w->next)
	{
		EXPO_WINDOW(w);
		ew->hovered = FALSE;
	}

	if (es->dndState == DnDStart)	// No window is hovered
		es->dndState = DnDNone;

	es->prevCursorX = es->newCursorX;
	es->prevCursorY = es->newCursorY;
}

static Bool expoInitDisplay(CompPlugin * p, CompDisplay * d)
{
	ExpoDisplay *ed;

	ed = malloc(sizeof(ExpoDisplay));
	if (!ed)
		return FALSE;

	ed->screenPrivateIndex = allocateScreenPrivateIndex(d);

	if (ed->screenPrivateIndex < 0)
	{
		free(ed);
		return FALSE;
	}

	expoSetExpoInitiate(d, expoExpo);

	WRAP(ed, d, handleEvent, expoHandleEvent);
	d->privates[displayPrivateIndex].ptr = ed;

	return TRUE;
}

static void expoFiniDisplay(CompPlugin * p, CompDisplay * d)
{
	EXPO_DISPLAY(d);

	UNWRAP(ed, d, handleEvent);

	freeScreenPrivateIndex(d, ed->screenPrivateIndex);
	free(ed);
}

static Bool expoInitScreen(CompPlugin * p, CompScreen * s)
{
	ExpoScreen *es;

	EXPO_DISPLAY(s->display);

	es = malloc(sizeof(ExpoScreen));

	if (!es)
		return FALSE;

	es->windowPrivateIndex = allocateWindowPrivateIndex(s);

	es->anyClick = FALSE;

	es->mouseOverViewX = 0;
	es->mouseOverViewY = 0;
	es->origVX = 0;
	es->origVY = 0;

	es->grabIndex = 0;

	es->pointerX = 0;
	es->pointerY = 0;

	es->expoCam = 0.0f;
	es->expoMode = 0;

	es->dndState = DnDNone;
	es->dndWindow = NULL;

	WRAP(es, s, paintScreen, expoPaintScreen);
	WRAP(es, s, donePaintScreen, expoDonePaintScreen);
	WRAP(es, s, paintTransformedScreen, expoPaintTransformedScreen);
	WRAP(es, s, preparePaintScreen, expoPreparePaintScreen);
	WRAP(es, s, paintWindow, expoPaintWindow);
	WRAP(es, s, damageWindowRect, expoDamageWindowRect);

	s->privates[ed->screenPrivateIndex].ptr = es;

	return TRUE;
}

static void expoFiniScreen(CompPlugin * p, CompScreen * s)
{
	EXPO_SCREEN(s);

	UNWRAP(es, s, paintScreen);
	UNWRAP(es, s, donePaintScreen);
	UNWRAP(es, s, paintTransformedScreen);
	UNWRAP(es, s, preparePaintScreen);
	UNWRAP(es, s, paintWindow);
	UNWRAP(es, s, damageWindowRect);
	UNWRAP(es, s, setScreenOption);

	free(es);
}

static Bool expoInitWindow(CompPlugin * p, CompWindow * w)
{
	ExpoWindow *ew;

	EXPO_SCREEN(w->screen);

	ew = malloc(sizeof(ExpoWindow));
	if (!ew)
		return FALSE;

	ew->skipNotify = ew->hovered = FALSE;
	ew->origx = ew->origy = 0;

	w->privates[es->windowPrivateIndex].ptr = ew;

	return TRUE;
}

static void expoFiniWindow(CompPlugin * p, CompWindow * w)
{
	EXPO_WINDOW(w);

	free(ew);
}

static Bool expoInit(CompPlugin * p)
{
	displayPrivateIndex = allocateDisplayPrivateIndex();

	if (displayPrivateIndex < 0)
		return FALSE;

	return TRUE;
}

static void expoFini(CompPlugin * p)
{
	if (displayPrivateIndex >= 0)
		freeDisplayPrivateIndex(displayPrivateIndex);
}

static int expoGetVersion(CompPlugin * p, int version)
{
	return ABIVERSION;
}

CompPluginVTable expoVTable = {
	"expo",
	expoGetVersion,
	0,
	expoInit,
	expoFini,
	expoInitDisplay,
	expoFiniDisplay,
	expoInitScreen,
	expoFiniScreen,
	expoInitWindow,
	expoFiniWindow,
	NULL,
	NULL,
	NULL,
	NULL,
	0,
	0,
	0,
	0
};

CompPluginVTable *getCompPluginInfo(void)
{
	return &expoVTable;
}
