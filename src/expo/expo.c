/**
 *
 * Compiz expo plugin
 *
 * expo.c
 *
 * Copyright (c) 2008 Dennis Kasprzyk <racarr@opencompositing.org>
 * Copyright (c) 2006 Robert Carr <racarr@beryl-project.org>
 *
 * Authors:
 * Robert Carr <racarr@beryl-project.org>
 * Dennis Kasprzyk <onestone@opencompositing.org>
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

#include <compiz-core.h>
#include "expo_options.h"

#include <GL/glu.h>

#define PI 3.14159265359f

#define WIN_X(w) ((w)->attrib.x - (w)->input.left)
#define WIN_Y(w) ((w)->attrib.y - (w)->input.top)
#define WIN_W(w) ((w)->width + (w)->input.left + (w)->input.right)
#define WIN_H(w) ((w)->height + (w)->input.top + (w)->input.bottom)

static int displayPrivateIndex;

typedef enum
{
    DnDNone = 0,
    DnDDuring,
    DnDStart
} DnDState;

typedef enum
{
    VPUpdateNone = 0,
    VPUpdateMouseOver,
    VPUpdatePrevious
} VPUpdateMode;

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
    DonePaintScreenProc        donePaintScreen;
    PaintOutputProc            paintOutput;
    PaintScreenProc            paintScreen;
    PreparePaintScreenProc     preparePaintScreen;
    PaintTransformedOutputProc paintTransformedOutput;
    PaintWindowProc            paintWindow;
    DrawWindowProc             drawWindow;
    DrawWindowTextureProc      drawWindowTexture;
    AddWindowGeometryProc      addWindowGeometry;
    DamageWindowRectProc       damageWindowRect;

    /*  Used for expo zoom animation */
    float expoCam;
    Bool  expoActive;
    /* In expo mode? */
    Bool  expoMode;

    /* For expo grab */
    int grabIndex;

    /* Window being dragged in expo mode */
    DnDState   dndState;
    CompWindow *dndWindow;
    
    int prevCursorX, prevCursorY;
    int newCursorX, newCursorY;

    int origVX;
    int origVY;
    int selectedVX;
    int selectedVY;
    int paintingVX;
    int paintingVY;

    float *vpActivity;
    float vpActivitySize;

    float vpBrightness;
    float vpSaturation;

    VPUpdateMode vpUpdateMode;

    Bool anyClick;

    unsigned int clickTime;
    Bool         doubleClick;

    Region tmpRegion;

    float curveAngle;
    float curveDistance;
    float curveRadius;

} ExpoScreen;

typedef struct _xyz_tuple
{
    float x, y, z;
}
Point3d;

/* Helpers */
#define GET_EXPO_DISPLAY(d) \
    ((ExpoDisplay *) (d)->base.privates[displayPrivateIndex].ptr)
#define EXPO_DISPLAY(d) \
    ExpoDisplay *ed = GET_EXPO_DISPLAY(d);

#define GET_EXPO_SCREEN(s, ed) \
    ((ExpoScreen *) (s)->base.privates[(ed)->screenPrivateIndex].ptr)
#define EXPO_SCREEN(s) \
    ExpoScreen *es = GET_EXPO_SCREEN(s, GET_EXPO_DISPLAY(s->display))

#define sigmoid(x) (1.0f / (1.0f + exp (-5.5f * 2 * ((x) - 0.5))))
#define sigmoidProgress(x) ((sigmoid (x) - sigmoid (0)) / \
			    (sigmoid (1) - sigmoid (0)))

#define interpolate(a,b,val) (((val) * a) + ((1 - (val)) * (b)))

static void
expoMoveFocusViewport (CompScreen *s,
		       int        dx,
		       int        dy)
{
    EXPO_SCREEN (s);

    es->selectedVX += dx;
    es->selectedVY += dy;

    es->selectedVX = MIN (s->hsize - 1, es->selectedVX);
    es->selectedVX = MAX (0, es->selectedVX);
    es->selectedVY = MIN (s->vsize - 1, es->selectedVY);
    es->selectedVY = MAX (0, es->selectedVY);

    damageScreen (s);
}

static void
expoFinishWindowMovement (CompWindow *w)
{
    CompScreen *s = w->screen;

    EXPO_SCREEN (s);

    syncWindowPosition (w);
    (*s->windowUngrabNotify) (w);
    
    moveScreenViewport (s, s->x - es->selectedVX,
    			s->y - es->selectedVY, TRUE);

    /* update saved window attributes in case we moved the
       window to a new viewport */
    if (w->saveMask & CWX)
    {
	w->saveWc.x = w->saveWc.x % s->width;
	if (w->saveWc.x < 0)
	    w->saveWc.x += s->width;
    }
    if (w->saveMask & CWY)
    {
	w->saveWc.y = w->saveWc.y % s->height;
	if (w->saveWc.y < 0)
	    w->saveWc.y += s->height;
    }

    /* update window attibutes to make sure a
       moved maximized window is properly snapped
       to the work area */
    if (w->state & MAXIMIZE_STATE)
    {
    	int lastOutput;
	int centerX, centerY;

	/* make sure we snap to the correct output */
	lastOutput = s->currentOutputDev;
	centerX = (WIN_X (w) + WIN_W (w) / 2) % s->width;
	if (centerX < 0)
	    centerX += s->width;
	centerY = (WIN_Y (w) + WIN_H (w) / 2) % s->height;
	if (centerY < 0)
	    centerY += s->height;

	s->currentOutputDev = outputDeviceForPoint (s, centerX, centerY);

	updateWindowAttributes (w, CompStackingUpdateModeNone);

	s->currentOutputDev = lastOutput;
    }
}

static Bool
expoDnDInit (CompDisplay     *d,
	     CompAction      *action,
	     CompActionState state,
	     CompOption      *option,
	     int             nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);
    s   = findScreenAtDisplay (d, xid);

    if (s)
    {
    	EXPO_SCREEN (s);

	if (es->expoMode)
	{
	    es->dndState = DnDStart;
	    action->state |= CompActionStateTermButton;
	    damageScreen(s);
	}
	else
	    return FALSE;

	return TRUE;
    }

    return FALSE;
}

static Bool
expoDnDFini (CompDisplay     *d,
	     CompAction      *action,
	     CompActionState state,
	     CompOption      *option,
	     int             nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    for (s = d->screens; s; s = s->next)
    {
    	EXPO_SCREEN (s);

	if (xid && (s->root != xid))
	    continue;

	if (es->dndState == DnDDuring || es->dndState == DnDStart)
	{
	    if (es->dndWindow)
		expoFinishWindowMovement (es->dndWindow);

	    es->dndState = DnDNone;
	    es->dndWindow = NULL;
	    action->state &= ~CompActionStateTermButton;
	    damageScreen (s);

	    return TRUE;
	}
    }

    return FALSE;
}

static Bool
expoTermExpo (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption)
{
    CompScreen *s;

    for (s = d->screens; s; s = s->next)
    {
	EXPO_SCREEN (s);

	if (!es->expoMode)
	    continue;

	es->expoMode = FALSE;

	if (es->dndState != DnDNone)
	    expoDnDFini (d, action, state, option, nOption);

	if (state & CompActionStateCancel)
	    es->vpUpdateMode = VPUpdatePrevious;
	else
	    es->vpUpdateMode = VPUpdateMouseOver;

	es->dndState  = DnDNone;
	es->dndWindow = 0;

	removeScreenAction (s, expoGetDndButton (d));
	removeScreenAction (s, expoGetExitButton (d));
	removeScreenAction (s, expoGetNextVpButton (d));
	removeScreenAction (s, expoGetPrevVpButton (d));

	damageScreen (s);
	focusDefaultWindow (s);
    }

    return TRUE;
}

static Bool
expoExpo (CompDisplay     *d,
	  CompAction      *action,
	  CompActionState state,
	  CompOption      *option,
	  int             nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);
    s   = findScreenAtDisplay (d, xid);

    if (s)
    {
	EXPO_SCREEN (s);

	if (otherScreenGrabExist (s, "expo", 0))
	    return FALSE;

	if (!es->expoMode)
	{
	    if (!es->grabIndex)
		es->grabIndex = pushScreenGrab (s, None, "expo");

	    es->expoMode    = TRUE;
	    es->anyClick    = FALSE;
	    es->doubleClick = FALSE;
	    es->clickTime   = 0;

	    es->dndState  = DnDNone;
	    es->dndWindow = None;

	    es->selectedVX = es->origVX = s->x;
	    es->selectedVY = es->origVY = s->y;

	    addScreenAction (s, expoGetDndButton (d));
	    addScreenAction (s, expoGetExitButton (d));
	    addScreenAction (s, expoGetNextVpButton (d));
	    addScreenAction (s, expoGetPrevVpButton (d));

	    damageScreen (s);
	}
	else
	{
	    expoTermExpo (d, action, state, option, nOption);
	}

	return TRUE;
    }

    return FALSE;
}

static Bool
expoExitExpo (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);
    s   = findScreenAtDisplay (d, xid);

    if (s)
    {
    	EXPO_SCREEN (s);

	if (es->expoMode)
	{
	    expoTermExpo (d, action, 0, NULL, 0);
	    es->anyClick = TRUE;
	    damageScreen(s);
	}
	else
	    return FALSE;

	return TRUE;
    }

    return FALSE;
}

static Bool
expoNextVp (CompDisplay     *d,
	    CompAction      *action,
	    CompActionState state,
	    CompOption      *option,
	    int             nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);
    s   = findScreenAtDisplay (d, xid);

    if (s)
    {
    	EXPO_SCREEN (s);

	if (es->expoMode)
	{
	    int newX = es->selectedVX + 1;
	    int newY = es->selectedVY;

	    if (newX >= s->hsize)
	    {
		newX = 0;
		newY = newY + 1;
		if (newY >= s->vsize)
		    newY = 0;
	    }

	    expoMoveFocusViewport (s, newX - es->selectedVX,
				   newY - es->selectedVY);
	    damageScreen(s);
	}
	else
	    return FALSE;

	return TRUE;
    }

    return FALSE;
}

static Bool
expoPrevVp (CompDisplay     *d,
	    CompAction      *action,
	    CompActionState state,
	    CompOption      *option,
	    int             nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);
    s   = findScreenAtDisplay (d, xid);

    if (s)
    {
    	EXPO_SCREEN (s);

	if (es->expoMode)
	{
	    int newX = es->selectedVX - 1;
	    int newY = es->selectedVY;

	    if (newX < 0)
	    {
		newX = s->hsize - 1;
		newY = newY - 1;
		if (newY < 0)
		    newY = s->vsize - 1;
	    }

	    expoMoveFocusViewport (s, newX - es->selectedVX,
				   newY - es->selectedVY);
	    damageScreen(s);
	}
	else
	    return FALSE;

	return TRUE;
    }

    return FALSE;
}

static void
expoHandleEvent (CompDisplay *d,
		 XEvent      *event)
{
    EXPO_DISPLAY (d);
    CompScreen *s;

    switch (event->type)
    {
    case KeyPress:
	s = findScreenAtDisplay (d, event->xkey.root);

	if (s)
	{
	    EXPO_SCREEN (s);

	    if (es->expoMode)
	    {
		if (event->xkey.keycode == ed->leftKey)
		    expoMoveFocusViewport (s, -1, 0);
		else if (event->xkey.keycode == ed->rightKey)
		    expoMoveFocusViewport (s, 1, 0);
		else if (event->xkey.keycode == ed->upKey)
		    expoMoveFocusViewport (s, 0, -1);
		else if (event->xkey.keycode == ed->downKey)
		    expoMoveFocusViewport (s, 0, 1);
	    }
	}

	break;

    case ButtonPress:
	s = findScreenAtDisplay (d, event->xbutton.root);

	if (s)
	{
	    EXPO_SCREEN (s);

	    if (es->expoMode && event->xbutton.button == Button1)
	    {
		es->anyClick = TRUE;
		if (es->clickTime == 0)
		{
		    es->clickTime = event->xbutton.time;
		}
		else if (event->xbutton.time - es->clickTime <=
			 expoGetDoubleClickTime (d))
		{
		    es->doubleClick = TRUE;
		}
		else
		{
		    es->clickTime   = event->xbutton.time;
		    es->doubleClick = FALSE;
		}
		damageScreen(s);
	    }
	}
	break;

    case ButtonRelease:
	s = findScreenAtDisplay (d, event->xbutton.root);

	if (s)
	{
	    EXPO_SCREEN (s);
	    
	    if (es->expoMode)
	    {
		if (event->xbutton.button == Button1)
		{
		    if (event->xbutton.time - es->clickTime >
			     expoGetDoubleClickTime (d))
		    {
			es->clickTime   = 0;
			es->doubleClick = FALSE;
		    }
		    else if (es->doubleClick)
		    {
			CompAction *action;
			
			es->clickTime   = 0;
			es->doubleClick = FALSE;
			
    			action = expoGetExpoKey (d);
    			expoTermExpo (d, action, 0, NULL, 0);
			es->anyClick = TRUE;
		    }
		    break;
		}
	    }
	}
	break;
    }

    UNWRAP (ed, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (ed, d, handleEvent, expoHandleEvent);
}

static void
invertTransformedVertex (CompScreen              *s,
			 const ScreenPaintAttrib *sAttrib,
			 const CompTransform     *transform,
			 CompOutput              *output,
			 int                     vertex[2])
{
    CompTransform sTransform = *transform;
    GLdouble p1[3], p2[3], v[3], alpha;
    GLdouble mvm[16], pm[16];
    GLint    viewport[4];
    int      i;

    EXPO_SCREEN (s);

    (*s->applyScreenTransform) (s, sAttrib, output, &sTransform);
    transformToScreenSpace (s, output, -sAttrib->zTranslate, &sTransform);

    glGetIntegerv (GL_VIEWPORT, viewport);

    for (i = 0; i < 16; i++)
    {
	mvm[i] = sTransform.m[i];
	pm[i] = s->projection[i];
    }

    gluUnProject (vertex[0], s->height - vertex[1], 0, mvm, pm,
		  viewport, &p1[0], &p1[1], &p1[2]);
    gluUnProject (vertex[0], s->height - vertex[1], -1.0, mvm, pm,
		  viewport, &p2[0], &p2[1], &p2[2]);

    for (i = 0; i < 3; i++)
	v[i] = p1[i] - p2[i];

    alpha = -p1[2] / v[2];


    if (expoGetDeform (s->display) == DeformCurve)
    {
	/* If would be nice if someone would find a faster solution */
	const float gapX  = expoGetVpDistance (s->display) * 0.1f * s->height /
		            s->width * es->expoCam;
	const float angle = es->curveAngle * DEG2RAD * (1.0 / (1.0 + gapX));
	const float p     = sigmoidProgress (es->expoCam);

	int   i = 10;
	float a1, a2, *aM, x1, x2, *xM, d1, d2, *dM;

	a1 = -p1[2] / v[2];
	a2 = (es->curveDistance - es->curveRadius - p1[2]) / v[2];

	x1 = p1[0] + (a1 * v[0]);
	x2 = p1[0] + (a2 * v[0]);

	d1 = (es->curveDistance - (cos (angle / 2.0 - (x1 /
		(float)s->width * angle)) * es->curveRadius)) * p;
	d2 = (es->curveDistance - (cos (angle / 2.0 - (x2 /
		(float)s->width * angle)) * es->curveRadius)) * p;

	d1 = (d1 - p1[2]) / v[2];
	d2 = (d2 - p1[2]) / v[2];

	d1 = fabs (a1 - d1);
	d2 = fabs (a2 - d2);

	while (fabs(x1 - x2) > 1.0 && i--)
	{
	    if (d1 < d2)
	    {
		aM = &a2;
		dM = &d2;
		xM = &x2;
	    }
	    else
	    {
		aM = &a1;
		dM = &d1;
		xM = &x1;
	    }

	    *aM = (a1 + a2) / 2.0;
	    *xM = p1[0] + ((*aM) * v[0]);

	    *dM = (es->curveDistance - (cos (angle / 2.0 - (*xM /
		   (float)s->width * angle)) * es->curveRadius)) * p;
	    *dM = (*dM - p1[2]) / v[2];

	    *dM = fabs (*aM - *dM);
	}

	if (d1 < d2)
	    alpha = a1;
	else
	    alpha = a2;
    }

    
    vertex[0] = ceil (p1[0] + (alpha * v[0]));
    vertex[1] = ceil (p1[1] + (alpha * v[1]));
}

static Bool
expoDamageWindowRect (CompWindow *w,
		      Bool       initial,
		      BoxPtr     rect)
{
    Bool status;
    EXPO_SCREEN (w->screen);

    UNWRAP (es, w->screen, damageWindowRect);
    status = (*w->screen->damageWindowRect) (w, initial, rect);
    WRAP (es, w->screen, damageWindowRect, expoDamageWindowRect);

    if (es->expoCam > 0.0f)
	damageScreen (w->screen);

    return status;
}

static void
expoPaintScreen (CompScreen   *s,
		 CompOutput   *outputs,
		 int          numOutputs,
		 unsigned int mask)
{
    EXPO_SCREEN (s);

    if (es->expoCam > 0.0 && numOutputs > 1 &&
        expoGetMultioutputMode (s->display) == MultioutputModeOneBigWall)
    {
	outputs = &s->fullscreenOutput;
	numOutputs = 1;
    }

    UNWRAP (es, s, paintScreen);
    (*s->paintScreen) (s, outputs, numOutputs, mask);
    WRAP (es, s, paintScreen, expoPaintScreen);
}

static Bool
expoPaintOutput (CompScreen              *s,
		 const ScreenPaintAttrib *sAttrib,
		 const CompTransform     *transform,
		 Region                  region,
		 CompOutput              *output,
		 unsigned int            mask)
{
    Bool status;

    EXPO_SCREEN (s);

    if (es->expoCam > 0.0)
	mask |= PAINT_SCREEN_TRANSFORMED_MASK | PAINT_SCREEN_CLEAR_MASK;

    UNWRAP (es, s, paintOutput);
    status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
    WRAP (es, s, paintOutput, expoPaintOutput);

    return status;
}

static void
expoPreparePaintScreen (CompScreen *s,
			int        ms)
{
    EXPO_SCREEN (s);

    float val = ((float) ms / 1000.0) / expoGetZoomTime (s->display);

    if (es->expoMode)
	es->expoCam = MIN (1.0, es->expoCam + val);
    else
	es->expoCam = MAX (0.0, es->expoCam - val);

    if (es->expoCam)
    {
	int i, j, vp;

	if (es->vpActivitySize < s->hsize * s->vsize)
	{
	    es->vpActivity     = malloc (s->hsize * s->vsize *
					 sizeof (float));
	    if (!es->vpActivity)
		es->vpActivitySize = 0;
	    else
		es->vpActivitySize = s->hsize * s->vsize;
		
	    for (i = 0; i < es->vpActivitySize; i++)
		es->vpActivity [i] = 1.0;
	}
	
	for (i = 0; i < s->hsize; i++)
	    for (j = 0; j < s->vsize; j++)
	    {
		vp = (j * s->hsize) + i;

		if (i == es->selectedVX && j == es->selectedVY)
		    es->vpActivity[vp] = MIN (1.0, es->vpActivity[vp] + val);
		else
		    es->vpActivity[vp] = MAX (0.0, es->vpActivity[vp] - val);
	    }
    }
    else if (es->vpActivitySize)
    {
	free (es->vpActivity);
	es->vpActivity     = NULL;
	es->vpActivitySize = 0;
    }

    UNWRAP (es, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, ms);
    WRAP (es, s, preparePaintScreen, expoPreparePaintScreen);
}

static void
expoPaintWall (CompScreen              *s,
	       const ScreenPaintAttrib *sAttrib,
	       const CompTransform     *transform,
	       Region                  region,
	       CompOutput              *output,
	       unsigned int            mask,
	       Bool                    reflection)
{
    EXPO_SCREEN (s);

    CompTransform sTransformW, sTransform = *transform;
    int           i, j, vp;
    int           oldFilter = s->display->textureFilter;

    float sx = (float)s->width / output->width;
    float sy = (float)s->height / output->height;
    float biasZ;
    float oScale, rotation = 0.0f, progress, vpp;
    float aspectX = 1.0f, aspectY = 1.0f;
    float camX, camY, camZ;

    /* amount of gap between viewports */
    const float gapY = expoGetVpDistance (s->display) * 0.1f * es->expoCam;
    const float gapX = expoGetVpDistance (s->display) * 0.1f * s->height /
		       s->width * es->expoCam;

    /* Zoom animation stuff */
    /* camera position for the selected viewport */
    Point3d vpCamPos   = { 0, 0, 0 };

    /* camera position during expo mode */
    Point3d expoCamPos = { 0, 0, 0 };

    if (expoGetDeform (s->display) == DeformCurve)
    {
	vpCamPos.x = -sx * (0.5 - (( (float)output->region.extents.x1 +
		     (output->width / 2.0)) / (float)s->width));
    }
    else
    {
	vpCamPos.x = ((s->x * sx) + 0.5 +
		     (output->region.extents.x1 / output->width)) -
		     (s->hsize * 0.5 * sx) + gapX * (s->x);
    }
    vpCamPos.y = -((s->y * sy) + 0.5 +
		 ( output->region.extents.y1 / output->height)) +
		 (s->vsize * 0.5 * sy) - gapY * (s->y);
    vpCamPos.z = 0;

    if (expoGetDeform (s->display) == DeformTilt ||
	expoGetReflection (s->display))
	biasZ = MAX (s->hsize * sx, s->vsize * sy) *
		(0.15 + expoGetDistance (s->display));
    else
	biasZ = MAX (s->hsize * sx, s->vsize * sy) *
		expoGetDistance (s->display);

    progress = sigmoidProgress (es->expoCam);

    if (expoGetDeform (s->display) == DeformCurve)
    {
	expoCamPos.x = 0.0;
    }
    else
    {
	expoCamPos.x = gapX * (s->hsize - 1) * 0.5;
    }
    
    expoCamPos.y = -gapY * (s->vsize - 1) * 0.5;
    expoCamPos.z = -DEFAULT_Z_CAMERA + DEFAULT_Z_CAMERA *
		   (MAX (s->hsize + (s->hsize - 1) * gapX,
			 s->vsize + (s->vsize - 1) * gapY) + biasZ);

    /* interpolate between vpCamPos and expoCamPos */
    camX = vpCamPos.x * (1 - progress) + expoCamPos.x * progress;
    camY = vpCamPos.y * (1 - progress) + expoCamPos.y * progress;
    camZ = vpCamPos.z * (1 - progress) + expoCamPos.z * progress;

    if (s->hsize > s->vsize)
    {
	aspectY = (float) s->hsize / (float) s->vsize;
	aspectY -= 1.0;
	aspectY *= -expoGetAspectRatio (s->display) + 1.0;
	aspectY *= progress;
	aspectY += 1.0;
    }
    else
    {
	aspectX = (float) s->vsize / (float) s->hsize;
	aspectX -= 1.0;
	aspectX *= -expoGetAspectRatio (s->display) + 1.0;
	aspectX *= progress;
	aspectX += 1.0;
    }

    /* End of Zoom animation stuff */

    if (expoGetDeform (s->display) == DeformTilt)
    {
	if (expoGetExpoAnimation (s->display) == ExpoAnimationZoom)
	    rotation = 10.0 * sigmoidProgress (es->expoCam);
	else
	    rotation = 10.0 * es->expoCam;
    }

    if (expoGetMipmaps (s->display))
	s->display->textureFilter = GL_LINEAR_MIPMAP_LINEAR;

    /* ALL TRANSFORMATION ARE EXECUTED FROM BOTTOM TO TOP */

    oScale = 1 / (1 + ((MAX (sx,sy) - 1) * progress));

    matrixScale (&sTransform, oScale, oScale, 1.0);

    if (reflection)
    {
	float scaleFactor = expoGetScaleFactor (s->display);

	matrixTranslate (&sTransform, 0.0, -s->vsize * sy * aspectY, 0.0);
	matrixScale (&sTransform, 1.0, -1.0, 1.0);
	matrixTranslate (&sTransform, 0.0,
			 - (1 - scaleFactor) / 2 * s->vsize * sy * aspectY,
			 0.0);
	matrixScale (&sTransform, 1.0, scaleFactor, 1.0);
	glCullFace (GL_FRONT);
    }

    /* zoom out */
    matrixTranslate (&sTransform, -camX, -camY, -camZ - DEFAULT_Z_CAMERA);

    /* rotate */
    matrixRotate (&sTransform, rotation, 0.0f, 1.0f, 0.0f);
    matrixScale (&sTransform, aspectX, aspectY, 1.0);

    /* translate expo to center */
    matrixTranslate (&sTransform, s->hsize * sx * -0.5,
		     s->vsize * sy * 0.5, 0.0f);

    if (expoGetDeform (s->display) == DeformCurve)
    {
	matrixTranslate (&sTransform, (s->hsize - 1) * sx * 0.5,
    		     0, 0.0f);
    }

    sTransformW = sTransform;

    /* revert prepareXCoords region shift. Now all screens display the same */
    matrixTranslate (&sTransform, 0.5f, -0.5f, DEFAULT_Z_CAMERA);

    if (s->hsize > 2)
    {
	/* we can't have 90 degree for the left/right most viewport */
    	es->curveAngle = interpolate (359 / ((s->hsize - 1) * 2), 1,
				      expoGetCurve (s->display));
    }
    else
    {
	es->curveAngle = interpolate (180 / s->hsize, 1,
				      expoGetCurve (s->display));
    }

    es->curveDistance = ((0.5f * sx) + (gapX / 2.0)) /
			tanf (DEG2RAD * es->curveAngle / 2.0);
    es->curveRadius   = ((0.5f * sx) + (gapX / 2.0)) /
			sinf (DEG2RAD * es->curveAngle / 2.0);

    es->expoActive = TRUE;

    for (j = 0; j < s->vsize; j++)
    {

	CompTransform  sTransform2 = sTransform;
	CompTransform  sTransform3;

	for (i = 0; i < s->hsize; i++)
	{
	    if (expoGetExpoAnimation (s->display) == ExpoAnimationVortex)
		matrixRotate (&sTransform2, 360 * es->expoCam, 0.0f, 1.0f,
			      2.0f * es->expoCam);

	    sTransform3 = sTransform2;
	    matrixTranslate (&sTransform3,
			     output->region.extents.x1 / output->width,
			     -output->region.extents.y1 / output->height, 0.0);

	    setWindowPaintOffset (s, (s->x - i) * s->width,
				  (s->y - j) * s->height);

	    vp = (j * s->hsize) + i;

	    if (vp < es->vpActivitySize)
	    {
		vpp = sigmoidProgress (es->vpActivity[vp]);

		es->vpBrightness = vpp + ((1.0 - vpp) *
				   expoGetVpBrightness (s->display) / 100.0);
		es->vpSaturation = vpp + ((1.0 - vpp) *
				   expoGetVpSaturation (s->display) / 100.0);
	    }
	    else
	    {
		es->vpBrightness = 1.0;
		es->vpSaturation = 1.0;
	    }

	    es->paintingVX = i;
	    es->paintingVY = j;

	    if (expoGetDeform (s->display) == DeformCurve)
	    {
		matrixTranslate (&sTransform3, -vpCamPos.x, 0.0f,
				 es->curveDistance - DEFAULT_Z_CAMERA);

		matrixRotate (&sTransform3,es->curveAngle * (-i +
			      interpolate ((((float)s->hsize / 2.0) - 0.5),
					   s->x, progress)) , 0.0f, 1.0f, 0.0);

		matrixTranslate (&sTransform3, vpCamPos.x, 0.0f,
				 DEFAULT_Z_CAMERA - es->curveDistance);
	    }

	    paintTransformedOutput (s, sAttrib, &sTransform3, &s->region,
				    output, mask);

	    if (!reflection)
	    {
	    	int cursor[2] = { pointerX, pointerY };

    		invertTransformedVertex (s, sAttrib, &sTransform3,
					 output, cursor);

		if ((cursor[0] > 0) && (cursor[0] < s->width) &&
	   	    (cursor[1] > 0) && (cursor[1] < s->height))
		{
		    es->newCursorX = i * s->width + cursor[0];
		    es->newCursorY = j * s->height + cursor[1];

		    if (es->anyClick || es->dndState != DnDNone)
		    {
			/* Used to save last viewport interaction was in */
		    	es->selectedVX = i;
	    		es->selectedVY = j;
			es->anyClick = FALSE;
		    }
		}
	    }

	    /* not sure this will work with different resolutions */
	    if (expoGetDeform (s->display) != DeformCurve)
		matrixTranslate (&sTransform2, ((1.0 * sx) + gapX), 0.0f, 0.0);
	}

	/* not sure this will work with different resolutions */
	matrixTranslate (&sTransform, 0, - ((1.0 * sy) + gapY), 0.0f);
    }

    if (reflection)
    {
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glPushMatrix();

	if (expoGetDeform (s->display) != DeformCurve)
	{
	    glLoadMatrixf (sTransformW.m);

	    glBegin (GL_QUADS);
	    glColor4f (0.0, 0.0, 0.0, 1.0);
	    glVertex2f (0.0, 0.0);
	    glColor4f (0.0, 0.0, 0.0, 0.5);
	    glVertex2f (0.0, -s->vsize * (1.0 * sy + gapY));
	    glVertex2f (s->hsize * sx * (1.0 + gapX),
			-s->vsize * sy * (1.0 + gapY));
	    glColor4f (0.0, 0.0, 0.0, 1.0);
	    glVertex2f (s->hsize * sx * (1.0 + gapX), 0.0);
	    glEnd ();
	}
	else
	{
	    glCullFace (GL_BACK);
	    glLoadIdentity ();
	    glTranslatef (0.0, 0.0, -DEFAULT_Z_CAMERA);

	    glBegin (GL_QUADS);
	    glColor4f (0.0, 0.0, 0.0, 1.0 * es->expoCam);
	    glVertex2f (-0.5, -0.5);
	    glVertex2f (0.5, -0.5);
	    glColor4f (0.0, 0.0, 0.0, 0.5 * es->expoCam);
	    glVertex2f (0.5, 0.0);
	    glVertex2f (-0.5, 0.0);
	    glColor4f (0.0, 0.0, 0.0, 0.5 * es->expoCam);
	    glVertex2f (-0.5, 0.0);
	    glVertex2f (0.5, 0.0);
	    glColor4f (0.0, 0.0, 0.0, 0.0);
	    glVertex2f (0.5, 0.5);
	    glVertex2f (-0.5, 0.5);
	    glEnd ();
	}
	glCullFace (GL_BACK);

	glLoadIdentity ();
	glTranslatef (0.0, 0.0, -DEFAULT_Z_CAMERA);

	if (expoGetGroundSize (s->display) > 0.0)
	{
	    glBegin (GL_QUADS);
	    glColor4usv (expoGetGroundColor1 (s->display));
	    glVertex2f (-0.5, -0.5);
	    glVertex2f (0.5, -0.5);
	    glColor4usv (expoGetGroundColor2 (s->display));
	    glVertex2f (0.5, -0.5 + expoGetGroundSize (s->display));
	    glVertex2f (-0.5, -0.5 + expoGetGroundSize (s->display));
	    glEnd ();
	}

	glColor4usv (defaultColor);

	glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable (GL_BLEND);
	glPopMatrix ();
    }

    es->expoActive = FALSE;

    setWindowPaintOffset (s, 0, 0);

    s->filter[SCREEN_TRANS_FILTER] = oldFilter;
    s->display->textureFilter = oldFilter;
}

static void
expoPaintTransformedOutput (CompScreen              *s,
			    const ScreenPaintAttrib *sAttrib,
			    const CompTransform     *transform,
			    Region                  region,
			    CompOutput              *output,
			    unsigned int            mask)
{
    EXPO_SCREEN (s);

    UNWRAP (es, s, paintTransformedOutput);

    es->expoActive = FALSE;

    if (es->expoCam > 0)
	mask |= PAINT_SCREEN_CLEAR_MASK;

    if (es->expoCam <= 0 || (es->expoCam < 1.0 && es->expoCam > 0.0 &&
	expoGetExpoAnimation (s->display) != ExpoAnimationZoom))
    {
	(*s->paintTransformedOutput) (s, sAttrib, transform, region,
				      output, mask);
    }
    else
	clearScreenOutput (s, output, GL_COLOR_BUFFER_BIT);

    mask &= ~PAINT_SCREEN_CLEAR_MASK;

    if (es->expoCam > 0.0)
    {
	if (expoGetReflection (s->display))
	    expoPaintWall (s, sAttrib, transform, region, output, mask, TRUE);

	expoPaintWall (s, sAttrib, transform, region, output, mask, FALSE);
    }

    WRAP (es, s, paintTransformedOutput, expoPaintTransformedOutput);
}

static Bool
expoDrawWindow (CompWindow           *w,
		const CompTransform  *transform,
		const FragmentAttrib *fragment,
		Region	             region,
		unsigned int	     mask)
{
    Bool       status;
    CompScreen *s = w->screen;

    EXPO_SCREEN (s);

    if (es->expoCam > 0.0)
    {
	FragmentAttrib fA = *fragment;
	ExpoExpoAnimationEnum expoAnimation;

	expoAnimation = expoGetExpoAnimation (s->display);

	if (es->expoActive)
	{
	    if (expoAnimation != ExpoAnimationZoom)
		fA.opacity = fragment->opacity * es->expoCam;

	    if (w->wmType & CompWindowTypeDockMask &&
		expoGetHideDocks (s->display))
	    {
		if (expoAnimation == ExpoAnimationZoom &&
		    (s->x == es->selectedVX && s->y == es->selectedVY))
		{
		    fA.opacity = fragment->opacity *
				 (1 - sigmoidProgress (es->expoCam));
		}
		else
		    fA.opacity = 0;
	    }

	    fA.brightness = fragment->brightness * es->vpBrightness;
	    fA.saturation = fragment->saturation * es->vpSaturation;
	}
	else
	{
	    if (expoAnimation == ExpoAnimationZoom)
		fA.brightness = 0;
	    else
		fA.brightness = fragment->brightness *
				(1 - sigmoidProgress (es->expoCam));
	}

    	UNWRAP (es, s, drawWindow);
	status = (*s->drawWindow) (w, transform, &fA, region, mask);
	WRAP (es, s, drawWindow, expoDrawWindow);
    }
    else
    {
    	UNWRAP (es, s, drawWindow);
	status = (*s->drawWindow) (w, transform, fragment, region, mask);
	WRAP (es, s, drawWindow, expoDrawWindow);
    }

    return status;
}

#define EXPO_GRID_SIZE 100

static void
expoAddWindowGeometry (CompWindow *w,
		       CompMatrix *matrix,
		       int	  nMatrix,
		       Region     region,
		       Region     clip)
{
    CompScreen *s = w->screen;

    EXPO_SCREEN (s);

    if (es->expoCam > 0.0 && expoGetDeform (s->display) == DeformCurve)
    {
	int x1, x2;
	REGION reg;

	reg.numRects = 1;
	reg.rects = &reg.extents;

	reg.extents.y1 = region->extents.y1;
	reg.extents.y2 = region->extents.y2;
	
	x1 = (region->extents.x1 / EXPO_GRID_SIZE) * EXPO_GRID_SIZE;
	x1 = region->extents.x1;
	x2 = MIN (x1 + EXPO_GRID_SIZE, region->extents.x2);
	
	UNWRAP (es, s, addWindowGeometry);
	while (x1 < region->extents.x2)
	{
	    reg.extents.x1 = x1;
	    reg.extents.x2 = x2;

	    XIntersectRegion (region, &reg, es->tmpRegion);

	    if (!XEmptyRegion (es->tmpRegion))
	    {
		(*w->screen->addWindowGeometry) (w, matrix, nMatrix,
						 es->tmpRegion, clip);
	    }

	    x1 = x2;
	    x2 = MIN (x2 + EXPO_GRID_SIZE, region->extents.x2);
	}
	WRAP (es, s, addWindowGeometry, expoAddWindowGeometry);
    }
    else
    {
	UNWRAP (es, s, addWindowGeometry);
	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, region, clip);
	WRAP (es, s, addWindowGeometry, expoAddWindowGeometry);
    }
}

static void
expoDrawWindowTexture (CompWindow	    *w,
		       CompTexture	    *texture,
		       const FragmentAttrib *attrib,
		       unsigned int	    mask)
{
    CompScreen *s = w->screen;

    EXPO_SCREEN (s);

    if (es->expoCam > 0.0 && expoGetDeform (s->display) == DeformCurve)
    {
	int     i = 0;
	GLfloat *v = w->vertices + (w->vertexStride - 3);

	int     offX = 0, offY = 0;
	const float gapX = expoGetVpDistance (s->display) * 0.1f * s->height /
		           s->width * es->expoCam;
	const float angle = es->curveAngle * DEG2RAD * (1.0 / (1.0 + gapX));
	
	if (!windowOnAllViewports (w))
	{
	    getWindowMovementForOffset (w, s->windowOffsetX,
                                        s->windowOffsetY, &offX, &offY);
	}

	while (i < w->vCount)
	{
	    v[2] = es->curveDistance - (cos (angle / 2.0 - ((v[0] + offX) /
		   (float)s->width * angle)) * es->curveRadius);
	    v[2] *= sigmoidProgress (es->expoCam);

	    v += w->vertexStride;
	    i++;
	}
    }

    UNWRAP (es, s, drawWindowTexture);
    (*s->drawWindowTexture) (w, texture, attrib, mask);
    WRAP (es, s, drawWindowTexture, expoDrawWindowTexture);
}

static Bool
expoPaintWindow (CompWindow              *w,
		 const WindowPaintAttrib *attrib,
		 const CompTransform     *transform,
		 Region                  region,
		 unsigned int            mask)
{
    CompScreen *s = w->screen;
    Bool       status;

    EXPO_SCREEN (s);

    if (es->expoActive)
    {
	float opacity = 1.0;
	Bool  hideDocks;

	ExpoExpoAnimationEnum expoAnimation;

	expoAnimation = expoGetExpoAnimation (s->display);

	hideDocks = expoGetHideDocks (s->display);

	if (es->expoCam > 0.0 && es->expoCam < 1.0 &&
	    expoAnimation != ExpoAnimationZoom)
	    mask |= PAINT_WINDOW_TRANSLUCENT_MASK;

	if (es->expoCam > 0.0 && hideDocks &&
	    w->wmType & CompWindowTypeDockMask)
	    mask |= PAINT_WINDOW_TRANSLUCENT_MASK;
	
	if (expoAnimation != ExpoAnimationZoom)
	    opacity = attrib->opacity * es->expoCam;

	if (w->wmType & CompWindowTypeDockMask &&
		expoGetHideDocks (s->display))
	{
	    if (expoAnimation == ExpoAnimationZoom &&
		(s->x == es->selectedVX && s->y == es->selectedVY))
	    {
		opacity = attrib->opacity *
			  (1 - sigmoidProgress (es->expoCam));
	    }
	    else
		opacity = 0;

	    if (opacity <= 0)
		mask |= PAINT_WINDOW_NO_CORE_INSTANCE_MASK;
	}
    }

    UNWRAP (es, s, paintWindow);
    status = (*s->paintWindow) (w, attrib, transform, region, mask);
    WRAP (es, s, paintWindow, expoPaintWindow);

    return status;
}

static void
expoDonePaintScreen (CompScreen * s)
{
    EXPO_SCREEN (s);

    int i;

    switch (es->vpUpdateMode) {
    case VPUpdateMouseOver:
    	moveScreenViewport (s, s->x - es->selectedVX, 
			    s->y - es->selectedVY, TRUE);
	focusDefaultWindow (s);
	es->vpUpdateMode = VPUpdateNone;
	break;
    case VPUpdatePrevious:
	moveScreenViewport (s, s->x - es->origVX, s->y - es->origVY, TRUE);
	es->selectedVX = es->origVX;
	es->selectedVY = es->origVY;
	focusDefaultWindow (s);
	es->vpUpdateMode = VPUpdateNone;
	break;
    default:
	break;
    }

    if ((es->expoCam > 0.0f && es->expoCam < 1.0f) || es->dndState != DnDNone)
	damageScreen (s);

    if (es->expoCam == 1.0f)
	for (i = 0; i < es->vpActivitySize; i++)
	    if (es->vpActivity[i] != 0.0 || es->vpActivity[i] != 1.0)
		damageScreen (s);

    if (es->grabIndex && es->expoCam <= 0.0f && !es->expoMode)
    {
	removeScreenGrab (s, es->grabIndex, 0);
	es->grabIndex = 0;
    }

    UNWRAP (es, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (es, s, donePaintScreen, expoDonePaintScreen);

    switch (es->dndState) {
    case DnDDuring:
	{
	    int dx = es->newCursorX - es->prevCursorX;
	    int dy = es->newCursorY - es->prevCursorY;

	    if (es->dndWindow)
		moveWindow (es->dndWindow, dx, dy, TRUE,
			    expoGetExpoImmediateMove (s->display));

	    es->prevCursorX = es->newCursorX;
	    es->prevCursorY = es->newCursorY;

	    damageScreen (s);
	}
	break;

    case DnDStart:
	{
	    CompWindow *w;

	    for (w = s->reverseWindows; w; w = w->prev)
	    {
		Bool inWindow;
		int xOffset, yOffset;
		int nx,ny;

		if (w->destroyed)
		    continue;

		if (!w->shaded)
		{
		    if (w->attrib.map_state != IsViewable || !w->damaged)
			continue;
		}

		if (!(w->type & (CompWindowTypeNormalMask |
				 CompWindowTypeFullscreenMask)))
		    continue;

		xOffset = s->hsize * s->width;
		yOffset = s->vsize * s->height;

		nx = es->newCursorX - (s->x * s->width);
		ny = es->newCursorY - (s->y * s->height);
		
		inWindow = ((nx >= WIN_X (w)) &&
			    (nx <= WIN_X (w) + WIN_W (w))) ||
		           ((nx >= (WIN_X (w) + xOffset)) &&
			    (nx <= (WIN_X (w) + WIN_W (w) +
						xOffset)));

		inWindow &= ((ny >= WIN_Y (w)) &&
			     (ny <= WIN_Y (w) + WIN_H (w))) ||
		            ((ny >= (WIN_Y (w) + yOffset)) &&
		    	     (ny <= (WIN_Y (w) + WIN_H (w) +
						 yOffset)));

		if (!inWindow)
		    continue;

		es->dndState  = DnDDuring;
		es->dndWindow = w;

		(*s->windowGrabNotify) (w, nx, ny, 0,
					CompWindowGrabMoveMask |
					CompWindowGrabButtonMask);
		break;
	    }

	    if (w)
	    {
		raiseWindow (es->dndWindow);
		moveInputFocusToWindow (es->dndWindow);
	    }
	    else
	    {
		/* no window was hovered */
		es->dndState = DnDNone;
	    }

	    es->prevCursorX = es->newCursorX;
	    es->prevCursorY = es->newCursorY;
	}
	break;
    default:
	break;
    }
}

static Bool
expoInitDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    ExpoDisplay *ed;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    ed = malloc (sizeof (ExpoDisplay));
    if (!ed)
	return FALSE;

    ed->screenPrivateIndex = allocateScreenPrivateIndex (d);

    if (ed->screenPrivateIndex < 0)
    {
	free (ed);
	return FALSE;
    }

    expoSetExpoKeyInitiate (d, expoExpo);
    expoSetExpoKeyTerminate (d, expoTermExpo);
    expoSetExpoButtonInitiate (d, expoExpo);
    expoSetExpoButtonTerminate (d, expoTermExpo);
    expoSetExpoEdgeInitiate (d, expoExpo);
    expoSetExpoEdgeTerminate (d, expoTermExpo);

    expoSetDndButtonInitiate (d, expoDnDInit);
    expoSetDndButtonTerminate (d, expoDnDFini);
    expoSetExitButtonInitiate (d, expoExitExpo);
    expoSetNextVpButtonInitiate (d, expoNextVp);
    expoSetPrevVpButtonInitiate (d, expoPrevVp);


    ed->leftKey  = XKeysymToKeycode (d->display, XStringToKeysym ("Left"));
    ed->rightKey = XKeysymToKeycode (d->display, XStringToKeysym ("Right"));
    ed->upKey    = XKeysymToKeycode (d->display, XStringToKeysym ("Up"));
    ed->downKey  = XKeysymToKeycode (d->display, XStringToKeysym ("Down"));

    WRAP (ed, d, handleEvent, expoHandleEvent);
    d->base.privates[displayPrivateIndex].ptr = ed;

    return TRUE;
}

static void
expoFiniDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    EXPO_DISPLAY (d);

    UNWRAP (ed, d, handleEvent);

    freeScreenPrivateIndex (d, ed->screenPrivateIndex);
    free (ed);
}

static Bool
expoInitScreen (CompPlugin *p,
		CompScreen *s)
{
    ExpoScreen *es;

    EXPO_DISPLAY (s->display);

    es = malloc (sizeof (ExpoScreen));

    if (!es)
	return FALSE;

    es->tmpRegion = XCreateRegion ();
    if (!es->tmpRegion)
	return FALSE;

    es->anyClick  = FALSE;
    es->vpUpdateMode = VPUpdateNone;

    es->selectedVX = es->origVX = s->x;
    es->selectedVY = es->origVY = s->y;

    es->grabIndex = 0;

    es->expoCam  = 0.0f;
    es->expoMode = 0;

    es->dndState  = DnDNone;
    es->dndWindow = NULL;

    es->clickTime   = 0;
    es->doubleClick = FALSE;

    es->vpActivity     = NULL;
    es->vpActivitySize = 0;

    WRAP (es, s, paintOutput, expoPaintOutput);
    WRAP (es, s, paintScreen, expoPaintScreen);
    WRAP (es, s, donePaintScreen, expoDonePaintScreen);
    WRAP (es, s, paintTransformedOutput, expoPaintTransformedOutput);
    WRAP (es, s, preparePaintScreen, expoPreparePaintScreen);
    WRAP (es, s, drawWindow, expoDrawWindow);
    WRAP (es, s, damageWindowRect, expoDamageWindowRect);
    WRAP (es, s, paintWindow, expoPaintWindow);
    WRAP (es, s, addWindowGeometry, expoAddWindowGeometry);
    WRAP (es, s, drawWindowTexture, expoDrawWindowTexture);

    s->base.privates[ed->screenPrivateIndex].ptr = es;

    return TRUE;
}

static void
expoFiniScreen (CompPlugin *p,
		CompScreen *s)
{
    EXPO_SCREEN (s);

    if (es->grabIndex)
    {
	removeScreenGrab (s, es->grabIndex, 0);
	es->grabIndex = 0;
    }

    XDestroyRegion (es->tmpRegion);

    UNWRAP (es, s, paintOutput);
    UNWRAP (es, s, paintScreen);
    UNWRAP (es, s, donePaintScreen);
    UNWRAP (es, s, paintTransformedOutput);
    UNWRAP (es, s, preparePaintScreen);
    UNWRAP (es, s, drawWindow);
    UNWRAP (es, s, damageWindowRect);
    UNWRAP (es, s, paintWindow);
    UNWRAP (es, s, addWindowGeometry);
    UNWRAP (es, s, drawWindowTexture);

    free (es);
}

static CompBool
expoInitObject (CompPlugin *p,
		CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) expoInitDisplay,
	(InitPluginObjectProc) expoInitScreen
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
expoFiniObject (CompPlugin *p,
		CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) expoFiniDisplay,
	(FiniPluginObjectProc) expoFiniScreen
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}


static Bool
expoInit (CompPlugin * p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex();

    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
expoFini (CompPlugin * p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable expoVTable = {
    "expo",
    0,
    expoInit,
    expoFini,
    expoInitObject,
    expoFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &expoVTable;
}
