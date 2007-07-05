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
  
	KeyCode leftKey;
	KeyCode rightKey;
	KeyCode upKey;
	KeyCode downKey;
} ExpoDisplay;

typedef struct _ExpoScreen
{
	int windowPrivateIndex;

	DonePaintScreenProc donePaintScreen;
	PaintOutputProc paintOutput;
	PaintScreenProc paintScreen;
	PreparePaintScreenProc preparePaintScreen;
	PaintTransformedOutputProc paintTransformedOutput;
	PaintWindowProc paintWindow;
	DrawWindowProc drawWindow;
	DamageWindowRectProc damageWindowRect;

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

	Bool leaveExpo;
	Bool updateVP;

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

static void expoMoveFocusViewport(CompScreen *s, int dx, int dy)
{
	EXPO_SCREEN(s);
	
	es->origVX += dx;
	es->origVY += dy;
	
	es->origVX = MIN(s->hsize-1, es->origVX);
	es->origVX = MAX(0, es->origVX);
	es->origVY = MIN(s->vsize-1, es->origVY);
	es->origVY = MAX(0, es->origVY);
	
	damageScreen(s);
}


static void expoHandleEvent(CompDisplay * d, XEvent * event)
{
	EXPO_DISPLAY(d);
	CompScreen *s;

	switch (event->type)
	{
	case KeyPress:
		s = findScreenAtDisplay(d,event->xkey.root);
		if (s)
		{
			EXPO_SCREEN(s);
			if (es->expoMode)
			{
				if (event->xkey.keycode == ed->leftKey)
					expoMoveFocusViewport(s, -1, 0);
				else if (event->xkey.keycode == ed->rightKey)
					expoMoveFocusViewport(s, 1, 0);
				else if (event->xkey.keycode == ed->upKey)
					expoMoveFocusViewport(s, 0, -1);
				else if (event->xkey.keycode == ed->downKey)
					expoMoveFocusViewport(s, 0, 1);
			}
		}
		break;
	case ButtonPress:
		s = findScreenAtDisplay(d, event->xbutton.root);
		if (s)
		{
			EXPO_SCREEN(s);
			if (es->expoMode)
			{
				es->anyClick = TRUE;
				es->updateVP = TRUE;
				damageScreen(s);
				if (event->xbutton.button == Button1)
				{
					es->updateVP = FALSE;
					es->dndState = DnDStart;
				}
				else if (event->xbutton.button != Button5)
					es->leaveExpo = TRUE;
			}
			es->pointerX = event->xbutton.x_root;
			es->pointerY = event->xbutton.y_root;
		}
		break;
	case ButtonRelease:
		s = findScreenAtDisplay(d, event->xbutton.root);
		if (s)
		{
			EXPO_SCREEN(s);

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
			es->pointerX = event->xbutton.x_root;
			es->pointerY = event->xbutton.y_root;
		}
		break;
	case MotionNotify:
		s = findScreenAtDisplay(d, event->xbutton.root);
		if (s)
		{
			EXPO_SCREEN(s);
			es->pointerX = event->xmotion.x_root;
			es->pointerY = event->xmotion.y_root;
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
	if (es->expoMode && !es->grabIndex)
		es->grabIndex =	pushScreenGrab(s, None, "expo");

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

static Bool expoTermExpo(CompDisplay * d, CompAction * action,
					 CompActionState state, CompOption * option, int nOption)
{
	CompScreen *s;

	if (state != CompActionStateCancel)
		return FALSE;

	for (s = d->screens; s; s = s->next)
	{
		EXPO_SCREEN(s);
		if (!es->expoMode)
			continue;

		es->expoMode = FALSE;	
		es->anyClick = FALSE;

		if (es->dndWindow)
			syncWindowPosition(es->dndWindow);
		es->dndState = DnDNone;
		es->dndWindow = 0;

		if (es->origVX >= 0 && es->origVY >= 0)
		{
			while (s->x != es->origVX)
				moveScreenViewport(s, 1, 0, TRUE);
			while (s->y != es->origVY)
				moveScreenViewport(s, 0, 1, TRUE);
		}
		damageScreen(s);

		focusDefaultWindow(s->display);
	}
	
	return TRUE;
}

//Other way around
static void invertTransformedVertex(CompScreen * s, const ScreenPaintAttrib * sAttrib,
						   const CompTransform *transform,
						   CompOutput *output, int vertex[2])
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

static void expoPaintScreen(CompScreen * s,
							CompOutput * outputs,
							int          numOutputs,
							unsigned int mask)
{
	EXPO_SCREEN(s);

	if (es->expoCam > 0.0 && numOutputs > 1)
	{
		UNWRAP(es, s, paintScreen);
		(*s->paintScreen) (s, &s->fullscreenOutput, 1, mask);
		WRAP(es, s, paintScreen, expoPaintScreen);
	}
	else
	{
		UNWRAP(es, s, paintScreen);
		(*s->paintScreen) (s, outputs, numOutputs, mask);
		WRAP(es, s, paintScreen, expoPaintScreen);
	}
}

static Bool expoPaintOutput(CompScreen * s,
							const ScreenPaintAttrib * sAttrib,
							const CompTransform    *transform,
							Region region, CompOutput *output,
							unsigned int mask)
{
	Bool status;

	EXPO_SCREEN(s);

	if (es->expoCam > 0.0)
	{
		mask |= PAINT_SCREEN_TRANSFORMED_MASK | PAINT_SCREEN_CLEAR_MASK;
	}

	UNWRAP(es, s, paintOutput);
	status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
	WRAP(es, s, paintOutput, expoPaintOutput);

	if (es->expoMode && es->updateVP)
	{
		CompWindow *w;

		for (w = s->windows; w; w = w->next)
			syncWindowPosition(w);

		damageScreen(s);
		es->origVX = es->mouseOverViewX;
		es->origVY = es->mouseOverViewY;
		if (es->leaveExpo)
		{
			es->expoMode = FALSE;
			es->leaveExpo = FALSE;
		}
		es->updateVP = FALSE;
		while (s->x != es->mouseOverViewX)
			moveScreenViewport(s, 1, 0, TRUE);
		while (s->y != es->mouseOverViewY)
			moveScreenViewport(s, 0, 1, TRUE);

		focusDefaultWindow(s->display);
	}	

	return status;
}

static void expoPreparePaintScreen(CompScreen * s, int ms)
{
	EXPO_SCREEN(s);

	float val = ((float)ms / 1000.0) / expoGetZoomTime(s->display);

	if (es->expoMode)
		es->expoCam = MIN(1.0, es->expoCam + val);
	else
		es->expoCam = MAX(0.0, es->expoCam - val);

	UNWRAP(es, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, ms);
	WRAP(es, s, preparePaintScreen, expoPreparePaintScreen);
}

static void expoPaintWall(CompScreen * s,
						  const ScreenPaintAttrib * sAttrib,
						  const CompTransform    *transform,
						  Region region, CompOutput *output,
						  unsigned int mask, Bool reflection)
{
	EXPO_SCREEN(s);

	CompTransform sTransformW, sTransform = *transform;

	int oldFilter = s->display->textureFilter;

	if (expoGetMipmaps(s->display))
		s->display->textureFilter = GL_LINEAR_MIPMAP_LINEAR;

	int origVX = s->x;
	int origVY = s->y;

	const float gapy = 0.01f * es->expoCam; // amount of gap between viewports
	const float gapx = 0.01f * s->height / s->width * es->expoCam;

	// Zoom animation stuff

	Point3d vpCamPos = {0, 0, 0};   // camera position for the selected viewport
	Point3d expoCamPos = {0, 0, 0}; // camera position during expo mode

	vpCamPos.x = s->hsize * ((s->x + 0.5) / s->hsize - 0.5) + gapx * (s->x);
	vpCamPos.y = -s->vsize * ((s->y + 0.5) / s->vsize - 0.5) - gapy * (s->y);
	vpCamPos.z = 0;

	float biasz = 0;
	if (expoGetRotate(s->display) || expoGetReflection(s->display))
	    biasz = MAX(s->hsize, s->vsize) * (0.15 + expoGetDistance(s->display));
	else
		biasz = MAX(s->hsize, s->vsize) * expoGetDistance(s->display);
	
	expoCamPos.x = gapx * (s->hsize - 1) * 0.5;
	expoCamPos.y = -gapy * (s->vsize - 1) * 0.5;
	expoCamPos.z = -DEFAULT_Z_CAMERA + DEFAULT_Z_CAMERA *
					(MAX(s->hsize +	(s->hsize - 1) * gapx,
						 s->vsize +	(s->vsize - 1) * gapy) + biasz);

	float progress = sigmoidProgress(es->expoCam);

	// interpolate between vpCamPos and expoCamPos
	float camx = vpCamPos.x * (1 - progress) + expoCamPos.x * progress;
	float camy = vpCamPos.y * (1 - progress) + expoCamPos.y * progress;
	float camz = vpCamPos.z * (1 - progress) + expoCamPos.z * progress;

	float aspectx = 1.0;
	float aspecty = 1.0;

	if (s->hsize > s->vsize)
	{
		aspecty = (float)s->hsize / (float)s->vsize;
		aspecty -= 1.0;
		aspecty *= -expoGetAspectRatio(s->display) + 1.0;
		aspecty *= progress;
		aspecty += 1.0;
	}
	else
	{
		aspectx = (float)s->vsize / (float)s->hsize;
		aspectx -= 1.0;
		aspectx *= -expoGetAspectRatio(s->display) + 1.0;
		aspectx *= progress;
		aspectx += 1.0;
	}
    
	// End of Zoom animation stuff

	moveScreenViewport(s, s->x, s->y, FALSE);

	float rotation = 0.0;

	if (expoGetRotate(s->display))
	{
		if (expoGetExpoAnimation(s->display) == ExpoAnimationZoom)
			rotation = 10.0 * sigmoidProgress(es->expoCam);
		else
			rotation = 10.0 * es->expoCam;
	}

	// ALL TRANSFORMATION ARE EXECUTED FROM BOTTOM TO TOP

	if (reflection)
	{
#define SCALE_FACTOR expoGetScaleFactor(s->display)
		matrixTranslate(&sTransform, 0.0, -s->vsize, 0.0);
		matrixScale(&sTransform, 1.0, -1.0, 1.0);
		matrixTranslate(&sTransform, 0.0, -(1 - SCALE_FACTOR)/2*s->vsize, 0.0);
		matrixScale(&sTransform, 1.0, SCALE_FACTOR, 1.0);
		glCullFace(GL_FRONT);
#undef SCALE_FACTOR
	}

	// zoom out
	matrixTranslate(&sTransform, -camx, -camy, -camz - DEFAULT_Z_CAMERA);

	// rotate

	matrixRotate(&sTransform, rotation, 0.0f, 1.0f, 0.0f);

	matrixScale(&sTransform, aspectx, aspecty, 1.0);
    
	// translate expo to center
	matrixTranslate(&sTransform, s->hsize * -0.5, s->vsize * 0.5, 0.0f);
    
	sTransformW = sTransform;

	// revert prepareXCoords region shift. Now all screens display the same
	matrixTranslate(&sTransform, 0.5f, -0.5f, DEFAULT_Z_CAMERA);

	int i, j;

	es->expoActive = TRUE;

	for (j = 0; j < s->vsize; j++)
	{

		CompTransform  sTransform2 = sTransform;
		for (i = 0; i < s->hsize; i++)
		{
			if (expoGetExpoAnimation(s->display) == ExpoAnimationVortex)
				matrixRotate(&sTransform2, 360 * es->expoCam, 0.0f, 1.0f,
						  2.0f * es->expoCam);

			paintTransformedOutput (s, sAttrib, &sTransform2, &s->region,
									output, mask);

			if (!reflection)
			{
				if ((es->pointerX >= 0)	&& (es->pointerX < s->width)
					&& (es->pointerY >= 0) && (es->pointerY < s->height))
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
			}
			
			// not sure this will work with different resolutions
			matrixTranslate(&sTransform2, (1.0 + gapx), 0.0f, 0.0);
			moveScreenViewport(s, -1, 0, FALSE);
		}

		// not sure this will work with different resolutions
		matrixTranslate(&sTransform, 0, -(1.0 + gapy), 0.0f);
		moveScreenViewport(s, 0, -1, FALSE);
	}

	if (reflection)
	{
		
		glPushMatrix();
		glLoadMatrixf(sTransformW.m);
		glEnable(GL_BLEND);
		
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBegin(GL_QUADS);
			glColor4f(0.0, 0.0, 0.0, 1.0);
			glVertex2f(0.0, 0.0);
			glColor4f(0.0, 0.0, 0.0, 0.5);
			glVertex2f(0.0, -s->vsize * (1.0 + gapy));
			glVertex2f(s->hsize * (1.0 + gapx), -s->vsize * (1.0 + gapy));
			glColor4f(0.0, 0.0, 0.0, 1.0);
			glVertex2f(s->hsize * (1.0 + gapx), 0.0);
		glEnd();
		glCullFace(GL_BACK);
		
		glLoadIdentity();
		glTranslatef(0.0, 0.0, -DEFAULT_Z_CAMERA);

		if (expoGetGroundSize(s->display) > 0.0)
		{
			glBegin(GL_QUADS);
				glColor4usv(expoGetGroundColor1(s->display));
				glVertex2f(-0.5, -0.5);
				glVertex2f(0.5, -0.5);
				glColor4usv(expoGetGroundColor2(s->display));
				glVertex2f(0.5, -0.5 + expoGetGroundSize(s->display));
				glVertex2f(-0.5,-0.5 + expoGetGroundSize(s->display));
			glEnd();
		}
		
		glColor4usv(defaultColor);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);
		glPopMatrix();
	}
	
	es->expoActive = FALSE;
	moveScreenViewport(s, -origVX, -origVY, FALSE);

	s->filter[SCREEN_TRANS_FILTER] = oldFilter;
	s->display->textureFilter = oldFilter;
	sTransform = *transform;
}

static void expoPaintTransformedOutput(CompScreen * s,
									   const ScreenPaintAttrib * sAttrib,
									   const CompTransform    *transform,
									   Region region, CompOutput *output,
									   unsigned int mask)
{
	EXPO_SCREEN(s);

	UNWRAP(es, s, paintTransformedOutput);

	es->expoActive = FALSE;

	if (es->expoCam > 0)
	    mask |= PAINT_SCREEN_CLEAR_MASK;
	if (es->expoCam <= 0 || (es->expoCam < 1.0 && es->expoCam > 0.0 &&
		expoGetExpoAnimation(s->display) != ExpoAnimationZoom))
		(*s->paintTransformedOutput) (s, sAttrib, transform, region,
		  							  output, mask);
	else
		glClear(GL_COLOR_BUFFER_BIT);
	mask &= ~PAINT_SCREEN_CLEAR_MASK;

	if (es->expoCam > 0.0)
	{
		if (expoGetReflection(s->display))
			expoPaintWall(s, sAttrib, transform, region, output, mask, TRUE);
		expoPaintWall(s, sAttrib, transform, region, output, mask, FALSE);
	}

	WRAP(es, s, paintTransformedOutput, expoPaintTransformedOutput);
}

static Bool
expoDrawWindow (CompWindow			 *w,
	    		const CompTransform  *transform,
	    		const FragmentAttrib *fragment,
	    		Region				 region,
	    		unsigned int	 	 mask)
{
	EXPO_SCREEN(w->screen);
	Bool status;

	FragmentAttrib fA = *fragment;

	if (es->expoCam > 0.0)
	{
		if (es->expoActive)
		{
			if (expoGetExpoAnimation(w->screen->display) != ExpoAnimationZoom)
				fA.opacity = fragment->opacity * es->expoCam;

			if (w->wmType & CompWindowTypeDockMask)
			{
				if (expoGetExpoAnimation(w->screen->display) == ExpoAnimationZoom &&
					((w->screen->x == es->origVX &&
					  w->screen->y == es->origVY) ||
					 (w->screen->x == es->rorigx &&
					  w->screen->y == es->rorigy &&
					  es->origVY < 0 &&
					  es->origVX < 0)))
					fA.opacity = fragment->opacity * (1 - sigmoidProgress(es->expoCam));
				else
					fA.opacity = 0;
			}

			if (!(w->screen->x == es->origVX && w->screen->y == es->origVY)
				&& !(w->screen->x == es->rorigx && w->screen->y == es->rorigy
					 &&	es->origVY < 0 && es->origVX < 0))
				fA.brightness = fragment->brightness * .75;
		}
		else
		{
			if (expoGetExpoAnimation(w->screen->display) == ExpoAnimationZoom)
				fA.brightness = 0;
			else
				fA.brightness = fragment->brightness * (1 - sigmoidProgress(es->expoCam));
		}
	}

	UNWRAP(es, w->screen, drawWindow);
	status = (*w->screen->drawWindow) (w, transform, &fA, region, mask);
	WRAP(es, w->screen, drawWindow, expoDrawWindow);

	return status;
}

static Bool
expoPaintWindow (CompWindow * w,
				 const WindowPaintAttrib * attrib,
				 const CompTransform * transform,
				 Region region, unsigned int mask)
{
	CompScreen *s = w->screen;
	Bool status;

	EXPO_SCREEN (s);

	if (es->expoCam > 0.0 && es->expoActive &&
	    (es->expoCam < 1.0 || (w->wmType & CompWindowTypeDockMask)))
		mask |= PAINT_WINDOW_TRANSLUCENT_MASK;

	UNWRAP(es, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
	WRAP(es, s, paintWindow, expoPaintWindow);

	return status;
}

static void expoDonePaintScreen(CompScreen * s)
{
	EXPO_SCREEN(s);

	if ((es->expoCam > 0.0f && es->expoCam < 1.0f) || es->dndState != DnDNone)
		damageScreen(s);

	if (es->grabIndex && es->expoCam <= 0.0f && !es->expoMode)
	{
		removeScreenGrab(s, es->grabIndex, 0);
		es->grabIndex = 0;
	}

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
			if ((es->newCursorX < WIN_X(w) ||
				es->newCursorX > WIN_X(w) + WIN_W(w)) &&
				(es->newCursorX < WIN_X(w) + (s->hsize * s->width) ||
				es->newCursorX > WIN_X(w) + WIN_W(w) + (s->hsize * s->width)))
				continue;
			if ((es->newCursorY < WIN_Y(w) ||
				es->newCursorY > WIN_Y(w) + WIN_H(w)) &&
			    (es->newCursorY < WIN_Y(w) + (s->vsize * s->height) ||
				es->newCursorY > WIN_Y(w) + WIN_H(w) + (s->vsize * s->height)))
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
	expoSetExpoTerminate(d, expoTermExpo);
	
	ed->leftKey = XKeysymToKeycode(d->display, XStringToKeysym("Left"));
	ed->rightKey = XKeysymToKeycode(d->display, XStringToKeysym("Right"));
	ed->upKey = XKeysymToKeycode(d->display, XStringToKeysym("Up"));
	ed->downKey = XKeysymToKeycode(d->display, XStringToKeysym("Down"));
	

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

	es->anyClick  = FALSE;
	es->leaveExpo = FALSE;
	es->updateVP  = FALSE;
	
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

	WRAP(es, s, paintOutput, expoPaintOutput);
	WRAP(es, s, paintScreen, expoPaintScreen);
	WRAP(es, s, donePaintScreen, expoDonePaintScreen);
	WRAP(es, s, paintTransformedOutput, expoPaintTransformedOutput);
	WRAP(es, s, preparePaintScreen, expoPreparePaintScreen);
	WRAP(es, s, drawWindow, expoDrawWindow);
	WRAP(es, s, damageWindowRect, expoDamageWindowRect);
	WRAP(es, s, paintWindow, expoPaintWindow);

	s->privates[ed->screenPrivateIndex].ptr = es;

	return TRUE;
}

static void expoFiniScreen(CompPlugin * p, CompScreen * s)
{
	EXPO_SCREEN(s);

	if (es->grabIndex)
	{
		removeScreenGrab(s, es->grabIndex, 0);
		es->grabIndex = 0;
	}

	UNWRAP(es, s, paintOutput);
	UNWRAP(es, s, paintScreen);
	UNWRAP(es, s, donePaintScreen);
	UNWRAP(es, s, paintTransformedOutput);
	UNWRAP(es, s, preparePaintScreen);
	UNWRAP(es, s, drawWindow);
	UNWRAP(es, s, damageWindowRect);
	UNWRAP(es, s, paintWindow);

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
