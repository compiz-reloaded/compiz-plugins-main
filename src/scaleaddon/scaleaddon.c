/*
 *
 * Compiz scale plugin addon plugin
 *
 * scaleaddon.c
 *
 * Copyright : (C) 2007 by Danny Baumann
 * E-mail    : dannybaumann@web.de
 *
 * Organic scale mode taken from Beryl's scale.c, written by
 * Copyright : (C) 2006 Diogo Ferreira
 * E-mail    : diogo@underdev.org
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
 *
 */

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <X11/Xatom.h>

#include <compiz-core.h>
#include <compiz-scale.h>
#include <compiz-text.h>

#include "scaleaddon_options.h"

static int displayPrivateIndex;
static int scaleDisplayPrivateIndex;

#define WIN_X(w) ((w)->attrib.x - (w)->input.left)
#define WIN_Y(w) ((w)->attrib.y - (w)->input.top)
#define WIN_W(w) ((w)->width + (w)->input.left + (w)->input.right)
#define WIN_H(w) ((w)->height + (w)->input.top + (w)->input.bottom)

typedef struct _ScaleAddonDisplay {
    int screenPrivateIndex;

    HandleEventProc       handleEvent;
    HandleCompizEventProc handleCompizEvent;

    TextFunc *textFunc;

    Window highlightedWindow;
    Window lastHighlightedWindow;
} ScaleAddonDisplay;

typedef struct _ScaleAddonScreen {
    int windowPrivateIndex;

    ScaleLayoutSlotsAndAssignWindowsProc layoutSlotsAndAssignWindows;
    ScalePaintDecorationProc		 scalePaintDecoration;
    ScaleSelectWindowProc                selectWindow;
    DonePaintScreenProc                  donePaintScreen;

    int lastState;

    float scale;
} ScaleAddonScreen;

typedef struct _ScaleAddonWindow {
    ScaleSlot origSlot;

    CompTextData *textData;

    Bool rescaled;

    CompWindow *oldAbove;
} ScaleAddonWindow;

#define GET_ADDON_DISPLAY(d)				          \
    ((ScaleAddonDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define ADDON_DISPLAY(d)		          \
    ScaleAddonDisplay *ad = GET_ADDON_DISPLAY (d)

#define GET_ADDON_SCREEN(s, ad)				              \
    ((ScaleAddonScreen *) (s)->base.privates[(ad)->screenPrivateIndex].ptr)

#define ADDON_SCREEN(s)						               \
    ScaleAddonScreen *as = GET_ADDON_SCREEN (s, GET_ADDON_DISPLAY (s->display))

#define GET_ADDON_WINDOW(w, as)				              \
    ((ScaleAddonWindow *) (w)->base.privates[(as)->windowPrivateIndex].ptr)

#define ADDON_WINDOW(w)						   \
    ScaleAddonWindow *aw = GET_ADDON_WINDOW (w,                    \
	                   GET_ADDON_SCREEN (w->screen,            \
			   GET_ADDON_DISPLAY (w->screen->display)))


static void
scaleaddonFreeWindowTitle (CompWindow *w)
{
    CompScreen *s = w->screen;

    ADDON_WINDOW (w);
    ADDON_DISPLAY (s->display);

    if (!aw->textData)
	return;

    (ad->textFunc->finiTextData) (s, aw->textData);
    aw->textData = NULL;
}

static void
scaleaddonRenderWindowTitle (CompWindow *w)
{
    CompTextAttrib            attrib;
    float                     scale;
    CompScreen                *s = w->screen;
    ScaleaddonWindowTitleEnum winTitleMode;

    ADDON_DISPLAY (s->display);
    SCALE_SCREEN (s);
    SCALE_WINDOW (w);
    ADDON_WINDOW (w);

    scaleaddonFreeWindowTitle (w);

    if (!ad->textFunc)
	return;

    if (!sw->slot)
	return;

    winTitleMode = scaleaddonGetWindowTitle (s);
    if (winTitleMode == WindowTitleNoDisplay)
	return;

    if (winTitleMode == WindowTitleHighlightedWindowOnly &&
	ad->highlightedWindow != w->id)
    {
	return;
    }

    scale = sw->slot->scale;
    attrib.maxWidth = w->attrib.width * scale;
    attrib.maxHeight = w->attrib.height * scale;

    attrib.family = scaleaddonGetTitleFont(s);
    attrib.size = scaleaddonGetTitleSize (s);
    attrib.color[0] = scaleaddonGetFontColorRed (s);
    attrib.color[1] = scaleaddonGetFontColorGreen (s);
    attrib.color[2] = scaleaddonGetFontColorBlue (s);
    attrib.color[3] = scaleaddonGetFontColorAlpha (s);

    attrib.flags = CompTextFlagWithBackground | CompTextFlagEllipsized;
    if (scaleaddonGetTitleBold (s))
	attrib.flags |= CompTextFlagStyleBold;

    attrib.bgHMargin = scaleaddonGetBorderSize (s);
    attrib.bgVMargin = scaleaddonGetBorderSize (s);
    attrib.bgColor[0] = scaleaddonGetBackColorRed (s);
    attrib.bgColor[1] = scaleaddonGetBackColorGreen (s);
    attrib.bgColor[2] = scaleaddonGetBackColorBlue (s);
    attrib.bgColor[3] = scaleaddonGetBackColorAlpha (s);

    aw->textData = (ad->textFunc->renderWindowTitle) (s, w->id,
						      ss->type == ScaleTypeAll,
						      &attrib);
}

static void
scaleaddonDrawWindowTitle (CompWindow *w)
{
    float      x, y, width, height;
    CompScreen *s = w->screen;

    SCALE_WINDOW (w);
    ADDON_WINDOW (w);
    ADDON_DISPLAY (s->display);

    width = aw->textData->width;
    height = aw->textData->height;

    x = sw->tx + w->attrib.x + ((WIN_W (w) * sw->scale) / 2) - (width / 2);
    y = sw->ty + w->attrib.y + ((WIN_H (w) * sw->scale) / 2) - (height / 2);

    (ad->textFunc->drawText) (s, aw->textData, floor (x), floor (y), 1.0f);
}

static void
scaleaddonDrawWindowHighlight (CompWindow *w)
{
    GLboolean  wasBlend;
    GLint      oldBlendSrc, oldBlendDst;
    float      x, y, width, height;
    CompScreen *s = w->screen;

    SCALE_WINDOW (w);
    ADDON_WINDOW (w);

    if (aw->rescaled)
	return;

    x      = sw->tx + w->attrib.x - (w->input.left * sw->scale);
    y      = sw->ty + w->attrib.y - (w->input.top * sw->scale);
    width  = WIN_W (w) * sw->scale;
    height = WIN_H (w) * sw->scale;

    /* we use a poor replacement for roundf()
     * (available in C99 only) here */
    x = floor (x + 0.5f);
    y = floor (y + 0.5f);

    wasBlend = glIsEnabled (GL_BLEND);
    glGetIntegerv (GL_BLEND_SRC, &oldBlendSrc);
    glGetIntegerv (GL_BLEND_DST, &oldBlendDst);

    if (!wasBlend)
	glEnable (GL_BLEND);

    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4us (scaleaddonGetHighlightColorRed (s),
		scaleaddonGetHighlightColorGreen (s),
		scaleaddonGetHighlightColorBlue (s),
		scaleaddonGetHighlightColorAlpha (s));

    glRectf (x, y + height, x + width, y);

    glColor4usv (defaultColor);

    if (!wasBlend)
	glDisable (GL_BLEND);
    glBlendFunc (oldBlendSrc, oldBlendDst);
}

static void
scaleaddonCheckWindowHighlight (CompScreen *s)
{
    CompDisplay *d = s->display;

    ADDON_DISPLAY (d);

    if (ad->highlightedWindow != ad->lastHighlightedWindow)
    {
	CompWindow *w;

	w = findWindowAtDisplay (d, ad->highlightedWindow);
	if (w)
	{
	    scaleaddonRenderWindowTitle (w);
	    addWindowDamage (w);
	}

	w = findWindowAtDisplay (d, ad->lastHighlightedWindow);
	if (w)
	{
	    scaleaddonRenderWindowTitle (w);
	    addWindowDamage (w);
	}

	ad->lastHighlightedWindow = ad->highlightedWindow;
    }
}

static Bool
scaleaddonCloseWindow (CompDisplay     *d,
	               CompAction      *action,
		       CompActionState state,
		       CompOption      *option,
		       int             nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    s = findScreenAtDisplay (d, xid);
    if (s)
    {
	CompWindow *w;

	SCALE_SCREEN (s);
	ADDON_DISPLAY (d);

	if (!ss->grabIndex)
	    return FALSE;

	w = findWindowAtDisplay (d, ad->highlightedWindow);
        if (w)
	{
	    closeWindow (w, getCurrentTimeFromDisplay (d));
	    return TRUE;
	}
    }

    return FALSE;
}

static Bool
scaleaddonPullWindow  (CompDisplay     *d,
	               CompAction      *action,
		       CompActionState state,
		       CompOption      *option,
		       int             nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    s = findScreenAtDisplay (d, xid);
    if (s)
    {
	CompWindow *w;

	SCALE_SCREEN (s);
	ADDON_DISPLAY (d);

	if (!ss->grabIndex)
	    return FALSE;

	w = findWindowAtDisplay (d, ad->highlightedWindow);
        if (w)
	{
	    int x, y, vx, vy;

	    defaultViewportForWindow (w, &vx, &vy);

	    x = w->attrib.x + (s->x - vx) * s->width;
	    y = w->attrib.y + (s->y - vy) * s->height;

	    if (scaleaddonGetConstrainPullToScreen (s))
	    {
	        XRectangle        workArea;
		CompWindowExtents extents;

		getWorkareaForOutput (s, outputDeviceForWindow (w), &workArea);

		extents.left   = x - w->input.left;
		extents.right  = x + w->width + w->input.right;
		extents.top    = y - w->input.top;
		extents.bottom = y + w->height + w->input.bottom;

		if (extents.left < workArea.x)
		    x += workArea.x - extents.left;
		else if (extents.right > workArea.x + workArea.width)
		    x += workArea.x + workArea.width - extents.right;

		if (extents.top < workArea.y)
		    y += workArea.y - extents.top;
		else if (extents.bottom > workArea.y + workArea.height)
		    y += workArea.y + workArea.height - extents.bottom;
	    }

	    if (x != w->attrib.x || y != w->attrib.y)
	    {
		SCALE_WINDOW (w);

		moveWindowToViewportPosition (w, x, y, TRUE);

		/* Select this window when ending scale */
		(*ss->selectWindow) (w);

		/* stop scaled window dissapearing */
		sw->tx -= (s->x - vx) * s->width;
		sw->ty -= (s->y - vy) * s->height;

		if (scaleaddonGetExitAfterPull (s))
		{
		    int        opt;
		    CompAction *action;
		    CompOption o[1];

		    SCALE_DISPLAY (d);

		    opt = SCALE_DISPLAY_OPTION_INITIATE_KEY;
		    action = &sd->opt[opt].value.action;

		    o[0].type    = CompOptionTypeInt;
		    o[0].name    = "root";
		    o[0].value.i = s->root;

		    if (action->terminate)
			(*action->terminate) (d, action, 0, o, 1);
		}
		else
		{
		    /* provide a simple animation */
		    addWindowDamage (w);

		    sw->tx -= (sw->slot->x2 - sw->slot->x1) / 20;
		    sw->ty -= (sw->slot->y2 - sw->slot->y1) / 20;
		    sw->scale *= 1.1f;
		    sw->adjust = TRUE;

		    ss->state = SCALE_STATE_OUT;
		    addWindowDamage (w);
		}
		
		return TRUE;
	    }
	}
    }

    return FALSE;
}

static Bool
scaleaddonZoomWindow (CompDisplay     *d,
		      CompAction      *action,
		      CompActionState state,
		      CompOption      *option,
		      int             nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    s = findScreenAtDisplay (d, xid);
    if (s)
    {
	CompWindow *w;

	SCALE_SCREEN (s);
	ADDON_DISPLAY (d);

	if (!ss->grabIndex)
	    return FALSE;

	w = findWindowAtDisplay (d, ad->highlightedWindow);
        if (w)
	{
	    SCALE_WINDOW (w);
	    ADDON_WINDOW (w);

	    XRectangle outputRect;
	    BOX        outputBox;
	    int        head;

	    if (!sw->slot)
		return FALSE;

	    head      = outputDeviceForPoint (s, sw->slot->x1, sw->slot->y1);
	    outputBox = w->screen->outputDev[head].region.extents;

	    outputRect.x      = outputBox.x1;
	    outputRect.y      = outputBox.y1;
	    outputRect.width  = outputBox.x2 - outputBox.x1;
	    outputRect.height = outputBox.y2 - outputBox.y1;

	    /* damage old rect */
	    addWindowDamage (w);

	    if (!aw->rescaled)
	    {
		aw->oldAbove = w->next;
		raiseWindow (w);
	
		/* backup old values */
		aw->origSlot = *sw->slot;

		aw->rescaled = TRUE;

		sw->slot->x1 = (outputRect.width / 2) - (WIN_W(w) / 2) +
			       w->input.left + outputRect.x;
		sw->slot->y1 = (outputRect.height / 2) - (WIN_H(w) / 2) +
			       w->input.top + outputRect.y;
		sw->slot->x2 = sw->slot->x1 + WIN_W(w);
		sw->slot->y2 = sw->slot->y1 + WIN_H(w);
		sw->slot->scale = 1.0f;
	    }
	    else
	    {
		if (aw->oldAbove)
		    restackWindowBelow (w, aw->oldAbove);

		aw->rescaled = FALSE;
		*(sw->slot) = aw->origSlot;
	    }

	    sw->adjust = TRUE;
	    ss->state = SCALE_STATE_OUT;

	    /* slot size may have changed, so
	     * update window title */
	    scaleaddonRenderWindowTitle (w);

	    addWindowDamage (w);

	    return TRUE;
	}
    }

    return FALSE;
}

static void
scaleaddonHandleEvent (CompDisplay *d,
		       XEvent      *event)
{
    ADDON_DISPLAY (d);

    UNWRAP (ad, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (ad, d, handleEvent, scaleaddonHandleEvent);

    switch (event->type)
    {
    case PropertyNotify:
	{
	    if (event->xproperty.atom == XA_WM_NAME)
	    {
		CompWindow *w;

		w = findWindowAtDisplay (d, event->xproperty.window);
		if (w)
		{
		    SCALE_SCREEN (w->screen);
		    if (ss->grabIndex)
		    {
			scaleaddonRenderWindowTitle (w);
			addWindowDamage (w);
		    }
		}
	    }
	}
	break;
    case MotionNotify:
	{
	    CompScreen *s;
	    s = findScreenAtDisplay (d, event->xmotion.root);

	    if (s)
	    {
		SCALE_SCREEN (s);
		if (ss->grabIndex)
		{
		    SCALE_DISPLAY (d);

		    ad->highlightedWindow = sd->hoveredWindow;
		    scaleaddonCheckWindowHighlight (s);
		}
	    }
	}
	break;
    default:
	break;
    }
}

static void
scaleaddonScalePaintDecoration (CompWindow              *w,
				const WindowPaintAttrib *attrib,
				const CompTransform     *transform,
				Region                  region,
				unsigned int            mask)
{
    CompScreen *s = w->screen;

    ADDON_SCREEN (s);
    SCALE_SCREEN (s);
    ADDON_WINDOW (w);

    UNWRAP (as, ss, scalePaintDecoration);
    (*ss->scalePaintDecoration) (w, attrib, transform, region, mask);
    WRAP (as, ss, scalePaintDecoration, scaleaddonScalePaintDecoration);

    if ((ss->state == SCALE_STATE_WAIT) || (ss->state == SCALE_STATE_OUT))
    {
	if (scaleaddonGetWindowHighlight (s))
	{
	    ADDON_DISPLAY (s->display);

	    if (w->id == ad->highlightedWindow)
		scaleaddonDrawWindowHighlight (w);
	}

	if (aw->textData)
	    scaleaddonDrawWindowTitle (w);
    }
}

static void
scaleaddonSelectWindow (CompWindow *w)
{
    CompScreen *s = w->screen;

    ADDON_DISPLAY (s->display);
    ADDON_SCREEN (s);
    SCALE_SCREEN (s);

    ad->highlightedWindow     = w->id;

    scaleaddonCheckWindowHighlight (s);

    UNWRAP (as, ss, selectWindow);
    (*ss->selectWindow) (w);
    WRAP (as, ss, selectWindow, scaleaddonSelectWindow);
}

static void
scaleaddonDonePaintScreen (CompScreen *s)
{
    ADDON_SCREEN (s);
    SCALE_SCREEN (s);

    if (ss->state != SCALE_STATE_NONE &&
	as->lastState == SCALE_STATE_NONE)
    {
	CompWindow *w;

	for (w = s->windows; w; w = w->next)
	    scaleaddonRenderWindowTitle (w);
    }
    else if (ss->state == SCALE_STATE_NONE &&
	     as->lastState != SCALE_STATE_NONE)
    {
	CompWindow *w;

	for (w = s->windows; w; w = w->next)
	    scaleaddonFreeWindowTitle (w);
    }

    if (ss->state == SCALE_STATE_OUT &&
	as->lastState != SCALE_STATE_OUT)
    {
	ADDON_DISPLAY (s->display);

	ad->lastHighlightedWindow = None;
	scaleaddonCheckWindowHighlight (s);
    }

    as->lastState = ss->state;

    UNWRAP (as, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (as, s, donePaintScreen, scaleaddonDonePaintScreen);
}

static void
scaleaddonHandleCompizEvent (CompDisplay *d,
			     const char  *pluginName,
			     const char  *eventName,
			     CompOption  *option,
			     int         nOption)
{
    ADDON_DISPLAY (d);

    UNWRAP (ad, d, handleCompizEvent);
    (*d->handleCompizEvent) (d, pluginName, eventName, option, nOption);
    WRAP (ad, d, handleCompizEvent, scaleaddonHandleCompizEvent);

    if ((strcmp (pluginName, "scale") == 0) &&
	(strcmp (eventName, "activate") == 0))
    {
	Window     xid;
	Bool       activated;
	CompScreen *s;
	
	xid = getIntOptionNamed (option, nOption, "root", 0);
	activated = getBoolOptionNamed (option, nOption, "active", FALSE);
	s = findScreenAtDisplay (d, xid);

	if (s)
	{
	    if (activated)
	    {
		addScreenAction (s, scaleaddonGetCloseKey (d));
		addScreenAction (s, scaleaddonGetZoomKey (d));
		addScreenAction (s, scaleaddonGetPullKey (d));
		addScreenAction (s, scaleaddonGetCloseButton (d));
		addScreenAction (s, scaleaddonGetZoomButton (d));
		addScreenAction (s, scaleaddonGetPullButton (d));

		/* TODO: or better
		   ad->highlightedWindow     = sd->selectedWindow;
		   here? do we want to show up the highlight without
		   mouse move initially? */

		ad->highlightedWindow     = None;
		ad->lastHighlightedWindow = None;
		scaleaddonCheckWindowHighlight (s);
	    }
	    else
	    {
		CompWindow *w;

		for (w = s->windows; w; w = w->next)
		{
		    ADDON_WINDOW (w);
		    aw->rescaled = FALSE;
		}

		removeScreenAction (s, scaleaddonGetCloseKey (d));
		removeScreenAction (s, scaleaddonGetZoomKey (d));
		removeScreenAction (s, scaleaddonGetPullKey (d));
		removeScreenAction (s, scaleaddonGetCloseButton (d));
		removeScreenAction (s, scaleaddonGetZoomButton (d));
		removeScreenAction (s, scaleaddonGetPullButton (d));
	    }
	}
    }
}

/**
 * experimental organic layout method
 * inspired by smallwindows (smallwindows.sf.net) by Jens Egeblad
 * */
#define ORGANIC_STEP 0.05

static int
organicCompareWindows (const void *elem1,
		       const void *elem2)
{
    CompWindow *w1 = *((CompWindow **) elem1);
    CompWindow *w2 = *((CompWindow **) elem2);

    return (WIN_X (w1) + WIN_Y (w1)) - (WIN_X (w2) + WIN_Y (w2));
}

static double
layoutOrganicCalculateOverlap (CompScreen *s,
			       int        win,
			       int        x,
			       int        y)
{
    int    i;
    int    x1, y1, x2, y2;
    int    overlapX, overlapY;
    int    xMin, xMax, yMin, yMax;
    double result = -0.01;

    SCALE_SCREEN (s);
    ADDON_SCREEN (s);

    x1 = x;
    y1 = y;
    x2 = x1 + WIN_W (ss->windows[win]) * as->scale;
    y2 = y1 + WIN_H (ss->windows[win]) * as->scale;

    for (i = 0; i < ss->nWindows; i++)
    {
	if (i == win)
	    continue;

	overlapX = overlapY = 0;
	xMax = MAX (ss->slots[i].x1, x1);
	xMin = MIN (ss->slots[i].x1 + WIN_W (ss->windows[i]) * as->scale, x2);
	if (xMax <= xMin)
	    overlapX = xMin - xMax;

	yMax = MAX (ss->slots[i].y1, y1);
	yMin = MIN (ss->slots[i].y1 + WIN_H (ss->windows[i]) * as->scale, y2);

	if (yMax <= yMin)
	    overlapY = yMin - yMax;

	result += (double)overlapX * overlapY;
    }

    return result;
}

static double
layoutOrganicFindBestHorizontalPosition (CompScreen *s,
					 int        win,
					 int        *bestX,
					 int        areaWidth)
{
    int    i, y1, y2, w;
    double bestOverlap = 1e31, overlap;

    SCALE_SCREEN (s);
    ADDON_SCREEN (s);

    y1 = ss->slots[win].y1;
    y2 = ss->slots[win].y1 + WIN_H (ss->windows[win]) * as->scale;

    w = WIN_W (ss->windows[win]) * as->scale;
    *bestX = ss->slots[win].x1;

    for (i = 0; i < ss->nWindows; i++)
    {
	CompWindow *lw = ss->windows[i];
	if (i == win)
	    continue;

	if (ss->slots[i].y1 < y2 &&
	    ss->slots[i].y1 + WIN_H (lw) * as->scale > y1)
	{
	    if (ss->slots[i].x1 - w >= 0)
	    {
		double overlap;
		
		overlap = layoutOrganicCalculateOverlap (s, win,
		 					 ss->slots[i].x1 - w,
							 y1);

		if (overlap < bestOverlap)
		{
		    *bestX = ss->slots[i].x1 - w;
		    bestOverlap = overlap;
		}
	    }
	    if (WIN_W (lw) * as->scale + ss->slots[i].x1 + w < areaWidth)
	    {
		double overlap;
		
		overlap = layoutOrganicCalculateOverlap (s, win,
		 					 ss->slots[i].x1 +
		 					 WIN_W (lw) * as->scale,
		 					 y1);

		if (overlap < bestOverlap)
		{
		    *bestX = ss->slots[i].x1 + WIN_W (lw) * as->scale;
		    bestOverlap = overlap;
		}
	    }
	}
    }

    overlap = layoutOrganicCalculateOverlap (s, win, 0, y1);
    if (overlap < bestOverlap)
    {
	*bestX = 0;
	bestOverlap = overlap;
    }

    overlap = layoutOrganicCalculateOverlap (s, win, areaWidth - w, y1);
    if (overlap < bestOverlap)
    {
	*bestX = areaWidth - w;
	bestOverlap = overlap;
    }

    return bestOverlap;
}

static double
layoutOrganicFindBestVerticalPosition (CompScreen *s,
				       int        win,
				       int        *bestY,
				       int        areaHeight)
{
    int    i, x1, x2, h;
    double bestOverlap = 1e31, overlap;

    SCALE_SCREEN (s);
    ADDON_SCREEN (s);

    x1 = ss->slots[win].x1;
    x2 = ss->slots[win].x1 + WIN_W (ss->windows[win]) * as->scale;
    h = WIN_H (ss->windows[win]) * as->scale;
    *bestY = ss->slots[win].y1;

    for (i = 0; i < ss->nWindows; i++)
    {
	CompWindow *w = ss->windows[i];

	if (i == win)
	    continue;

	if (ss->slots[i].x1 < x2 &&
	    ss->slots[i].x1 + WIN_W (w) * as->scale > x1)
	{
	    if (ss->slots[i].y1 - h >= 0 && ss->slots[i].y1 < areaHeight)
	    {
		double overlap;
		overlap = layoutOrganicCalculateOverlap (s, win, x1,
	 						 ss->slots[i].y1 - h);
		if (overlap < bestOverlap)
		{
		    *bestY = ss->slots[i].y1 - h;
		    bestOverlap = overlap;
		}
	    }
	    if (WIN_H (w) * as->scale + ss->slots[i].y1 > 0 &&
		WIN_H (w) * as->scale + h + ss->slots[i].y1 < areaHeight)
	    {
		double overlap;
		
		overlap = layoutOrganicCalculateOverlap (s, win, x1,
		 					 WIN_H (w) * as->scale +
							 ss->slots[i].y1);

		if (overlap < bestOverlap)
		{
		    *bestY = ss->slots[i].y1 + WIN_H(w) * as->scale;
		    bestOverlap = overlap;
		}
	    }
	}
    }

    overlap = layoutOrganicCalculateOverlap (s, win, x1, 0);
    if (overlap < bestOverlap)
    {
	*bestY = 0;
	bestOverlap = overlap;
    }

    overlap = layoutOrganicCalculateOverlap (s, win, x1, areaHeight - h);
    if (overlap < bestOverlap)
    {
	*bestY = areaHeight - h;
	bestOverlap = overlap;
    }

    return bestOverlap;
}

static Bool
layoutOrganicLocalSearch (CompScreen *s,
			  int        areaWidth,
			  int        areaHeight)
{
    Bool   improvement;
    int    i;
    double totalOverlap;

    SCALE_SCREEN (s);

    do
    {
	improvement = FALSE;
	for (i = 0; i < ss->nWindows; i++)
	{
	    Bool improved;

	    do
	    {
		int    newX, newY;
		double oldOverlap, overlapH, overlapV;

		improved = FALSE;
		oldOverlap = layoutOrganicCalculateOverlap (s, i,
 							    ss->slots[i].x1,
							    ss->slots[i].y1);

		overlapH = layoutOrganicFindBestHorizontalPosition (s, i,
								    &newX,
								    areaWidth);
		overlapV = layoutOrganicFindBestVerticalPosition (s, i,
								  &newY,
								  areaHeight);

		if (overlapH < oldOverlap - 0.1 ||
		    overlapV < oldOverlap - 0.1)
		{
		    improved = TRUE;
		    improvement = TRUE;
		    if (overlapV > overlapH)
			ss->slots[i].x1 = newX;
		    else
			ss->slots[i].y1 = newY;
		}
    	    }
	    while (improved);
	}
    }
    while (improvement);

    totalOverlap = 0.0;
    for (i = 0; i < ss->nWindows; i++)
    {
	totalOverlap += layoutOrganicCalculateOverlap (s, i,
						       ss->slots[i].x1,
						       ss->slots[i].y1);
    }

    return (totalOverlap > 0.1);
}

static void
layoutOrganicRemoveOverlap (CompScreen *s,
			    int        areaWidth,
			    int        areaHeight)
{
    int        i, spacing;
    CompWindow *w;

    SCALE_SCREEN (s);
    ADDON_SCREEN (s);

    spacing = ss->opt[SCALE_SCREEN_OPTION_SPACING].value.i;

    while (layoutOrganicLocalSearch (s, areaWidth, areaHeight))
    {
	for (i = 0; i < ss->nWindows; i++)
	{
	    int centerX, centerY;
	    int newX, newY, newWidth, newHeight;

	    w = ss->windows[i];

	    centerX = ss->slots[i].x1 + WIN_W (w) / 2;
	    centerY = ss->slots[i].y1 + WIN_H (w) / 2;

	    newWidth = (int)((1.0 - ORGANIC_STEP) *
			     (double)WIN_W (w)) - spacing / 2;
	    newHeight = (int)((1.0 - ORGANIC_STEP) *
			      (double)WIN_H (w)) - spacing / 2;
	    newX = centerX - (newWidth / 2);
	    newY = centerY - (newHeight / 2);

	    ss->slots[i].x1 = newX;
	    ss->slots[i].y1 = newY;
	    ss->slots[i].x2 = newX + WIN_W (w);
	    ss->slots[i].y2 = newY + WIN_H (w);
	}
	as->scale -= ORGANIC_STEP;
    }
}

static Bool
layoutOrganicThumbs (CompScreen *s)
{
    CompWindow *w;
    int        i, moMode;
    XRectangle workArea;

    SCALE_SCREEN (s);
    ADDON_SCREEN (s);

    moMode = ss->opt[SCALE_SCREEN_OPTION_MULTIOUTPUT_MODE].value.i;

    switch (moMode) {
    case SCALE_MOMODE_ALL:
	workArea = s->workArea;
	break;
    case SCALE_MOMODE_CURRENT:
    default:
	workArea = s->outputDev[s->currentOutputDev].workArea;
	break;
    }

    as->scale = 1.0f;

    qsort (ss->windows, ss->nWindows, sizeof(CompWindow *),
	   organicCompareWindows);

    for (i = 0; i < ss->nWindows; i++)
    {
	w = ss->windows[i];
	SCALE_WINDOW (w);

	sw->slot = &ss->slots[i];
	ss->slots[i].x1 = WIN_X (w) - workArea.x;
	ss->slots[i].y1 = WIN_Y (w) - workArea.y;
	ss->slots[i].x2 = WIN_X (w) + WIN_W (w) - workArea.x;
	ss->slots[i].y2 = WIN_Y (w) + WIN_H (w) - workArea.y;

	if (ss->slots[i].x1 < 0)
	{
	    ss->slots[i].x2 += abs (ss->slots[i].x1);
	    ss->slots[i].x1 = 0;
	}
	if (ss->slots[i].x2 > workArea.width - workArea.x)
	{
	    ss->slots[i].x1 -= abs (ss->slots[i].x2 - workArea.width);
	    ss->slots[i].x2 = workArea.width - workArea.x;
	}

	if (ss->slots[i].y1 < 0)
	{
	    ss->slots[i].y2 += abs (ss->slots[i].y1);
	    ss->slots[i].y1 = 0;
	}
	if (ss->slots[i].y2 > workArea.height - workArea.y)
	{
	    ss->slots[i].y1 -= abs (ss->slots[i].y2 -
				    workArea.height - workArea.y);
	    ss->slots[i].y2 = workArea.height - workArea.y;
	}
    }

    ss->nSlots = ss->nWindows;

    layoutOrganicRemoveOverlap (s, workArea.width - workArea.x,
				workArea.height - workArea.y);
    for (i = 0; i < ss->nWindows; i++)
    {
	w = ss->windows[i];
	SCALE_WINDOW (w);

	if (ss->type == ScaleTypeGroup)
	    raiseWindow (ss->windows[i]);

	ss->slots[i].x1 += w->input.left + workArea.x;
	ss->slots[i].x2 += w->input.left + workArea.x;
	ss->slots[i].y1 += w->input.top + workArea.y;
	ss->slots[i].y2 += w->input.top + workArea.y;
	sw->adjust = TRUE;
    }

    return TRUE;
}

static Bool
scaleaddonLayoutSlotsAndAssignWindows (CompScreen *s)
{
    Bool status;

    ADDON_SCREEN (s);
    SCALE_SCREEN (s);

    switch (scaleaddonGetLayoutMode (s))
    {
    case LayoutModeOrganicExperimental:
	status = layoutOrganicThumbs (s);
	break;
    case LayoutModeNormal:
    default:
	UNWRAP (as, ss, layoutSlotsAndAssignWindows);
	status = (*ss->layoutSlotsAndAssignWindows) (s);
	WRAP (as, ss, layoutSlotsAndAssignWindows,
	      scaleaddonLayoutSlotsAndAssignWindows);
	break;
    }

    return status;
}

static void
scaleaddonScreenOptionChanged (CompScreen              *s,
			       CompOption              *opt,
			       ScaleaddonScreenOptions num)
{
    switch (num)
    {
	case ScaleaddonScreenOptionWindowTitle:
	case ScaleaddonScreenOptionTitleFont:
	case ScaleaddonScreenOptionTitleBold:
	case ScaleaddonScreenOptionTitleSize:
	case ScaleaddonScreenOptionBorderSize:
	case ScaleaddonScreenOptionFontColor:
	case ScaleaddonScreenOptionBackColor:
	    {
		CompWindow *w;

		for (w = s->windows; w; w = w->next)
		{
		    ADDON_WINDOW (w);

		    if (aw->textData)
			scaleaddonRenderWindowTitle (w);
		}
	    }
	    break;
	default:
	    break;
    }
}

static Bool
scaleaddonInitDisplay (CompPlugin  *p,
	      	       CompDisplay *d)
{
    ScaleAddonDisplay *ad;
    int               index;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    if (!checkPluginABI ("scale", SCALE_ABIVERSION))
	return FALSE;

    if (!getPluginDisplayIndex (d, "scale", &scaleDisplayPrivateIndex))
	return FALSE;

    ad = malloc (sizeof (ScaleAddonDisplay));
    if (!ad)
	return FALSE;

    ad->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (ad->screenPrivateIndex < 0)
    {
	free (ad);
	return FALSE;
    }

    if (checkPluginABI ("text", TEXT_ABIVERSION) &&
	getPluginDisplayIndex (d, "text", &index))
    {
	ad->textFunc = d->base.privates[index].ptr;
    }
    else
    {
	compLogMessage ("scaleaddon", CompLogLevelWarn,
			"No compatible text plugin found.");
	ad->textFunc = NULL;
    }

    WRAP (ad, d, handleEvent, scaleaddonHandleEvent);
    WRAP (ad, d, handleCompizEvent, scaleaddonHandleCompizEvent);

    d->base.privates[displayPrivateIndex].ptr = ad;

    ad->highlightedWindow     = None;
    ad->lastHighlightedWindow = None;

    scaleaddonSetCloseKeyInitiate (d, scaleaddonCloseWindow);
    scaleaddonSetZoomKeyInitiate (d, scaleaddonZoomWindow);
    scaleaddonSetPullKeyInitiate (d, scaleaddonPullWindow);
    scaleaddonSetCloseButtonInitiate (d, scaleaddonCloseWindow);
    scaleaddonSetZoomButtonInitiate (d, scaleaddonZoomWindow);
    scaleaddonSetPullButtonInitiate (d, scaleaddonPullWindow);

    return TRUE;
}

static void
scaleaddonFiniDisplay (CompPlugin  *p,
	      	       CompDisplay *d)
{
    ADDON_DISPLAY (d);

    UNWRAP (ad, d, handleEvent);
    UNWRAP (ad, d, handleCompizEvent);

    freeScreenPrivateIndex (d, ad->screenPrivateIndex);

    free (ad);
}

static Bool
scaleaddonInitScreen (CompPlugin *p,
		      CompScreen *s)
{
    ScaleAddonScreen *as;

    ADDON_DISPLAY (s->display);
    SCALE_SCREEN (s);

    as = malloc (sizeof (ScaleAddonScreen));
    if (!as)
	return FALSE;

    as->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (as->windowPrivateIndex < 0)
    {
	free (as);
	return FALSE;
    }

    as->scale = 1.0f;
    as->lastState = SCALE_STATE_NONE;

    WRAP (as, s, donePaintScreen, scaleaddonDonePaintScreen);
    WRAP (as, ss, scalePaintDecoration, scaleaddonScalePaintDecoration);
    WRAP (as, ss, selectWindow, scaleaddonSelectWindow);
    WRAP (as, ss, layoutSlotsAndAssignWindows,
	  scaleaddonLayoutSlotsAndAssignWindows);

    scaleaddonSetWindowTitleNotify (s, scaleaddonScreenOptionChanged);
    scaleaddonSetTitleFontNotify (s, scaleaddonScreenOptionChanged);
    scaleaddonSetTitleBoldNotify (s, scaleaddonScreenOptionChanged);
    scaleaddonSetTitleSizeNotify (s, scaleaddonScreenOptionChanged);
    scaleaddonSetBorderSizeNotify (s, scaleaddonScreenOptionChanged);
    scaleaddonSetFontColorNotify (s, scaleaddonScreenOptionChanged);
    scaleaddonSetBackColorNotify (s, scaleaddonScreenOptionChanged);

    s->base.privates[ad->screenPrivateIndex].ptr = as;

    return TRUE;
}

static void
scaleaddonFiniScreen (CompPlugin *p,
	      	      CompScreen *s)
{
    ADDON_SCREEN (s);
    SCALE_SCREEN (s);

    UNWRAP (as, s, donePaintScreen);
    UNWRAP (as, ss, scalePaintDecoration);
    UNWRAP (as, ss, selectWindow);
    UNWRAP (as, ss, layoutSlotsAndAssignWindows);

    freeWindowPrivateIndex (s, as->windowPrivateIndex);
    free (as);
}

static Bool
scaleaddonInitWindow (CompPlugin *p,
		      CompWindow *w)
{
    ScaleAddonWindow *aw;

    ADDON_SCREEN (w->screen);

    aw = malloc (sizeof (ScaleAddonWindow));
    if (!aw)
	return FALSE;

    aw->rescaled = FALSE;

    w->base.privates[as->windowPrivateIndex].ptr = aw;

    aw->textData = NULL;

    return TRUE;
}

static void
scaleaddonFiniWindow (CompPlugin *p,
		      CompWindow *w)
{
    ADDON_WINDOW (w);

    scaleaddonFreeWindowTitle (w);

    free (aw);
}

static CompBool
scaleaddonInitObject (CompPlugin *p,
		      CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) scaleaddonInitDisplay,
	(InitPluginObjectProc) scaleaddonInitScreen,
	(InitPluginObjectProc) scaleaddonInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
scaleaddonFiniObject (CompPlugin *p,
		      CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) scaleaddonFiniDisplay,
	(FiniPluginObjectProc) scaleaddonFiniScreen,
	(FiniPluginObjectProc) scaleaddonFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
scaleaddonInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
scaleaddonFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable scaleaddonVTable = {
    "scaleaddon",
    0,
    scaleaddonInit,
    scaleaddonFini,
    scaleaddonInitObject,
    scaleaddonFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &scaleaddonVTable;
}
