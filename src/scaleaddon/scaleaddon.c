/*
 *
 * Compiz scale plugin addon plugin
 *
 * scaleaddon.c
 *
 * Copyright : (C) 2007 by Danny Baumann
 * E-mail    : maniac@opencompositing.org
 *
 * Organic scale mode taken from Beryl's scale.c, written by
 * Copyright : (C) 2006 Diogo Ferreira
 * E-mail    : diogo@underdev.org
 *
 * Rounded corner drawing taken from wall.c:
 * Copyright : (C) 2007 Robert Carr
 * E-mail    : racarr@beryl-project.org
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

#include <math.h>
#include <string.h>

#include <compiz.h>
#include <scale.h>
#include <text.h>

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

    Window lastHoveredWindow;
} ScaleAddonDisplay;

typedef struct _ScaleAddonScreen {
    int windowPrivateIndex;

    ScaleLayoutSlotsAndAssignWindowsProc layoutSlotsAndAssignWindows;
    ScalePaintDecorationProc		 scalePaintDecoration;

    Pixmap      textPixmap;
    CompTexture textTexture;

    int textWidth;
    int textHeight;

    float scale;
} ScaleAddonScreen;

typedef struct _ScaleAddonWindow {
    ScaleSlot origSlot;

    Bool rescaled;

    CompWindow *oldAbove;
} ScaleAddonWindow;

#define GET_ADDON_DISPLAY(d)				          \
    ((ScaleAddonDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define ADDON_DISPLAY(d)		          \
    ScaleAddonDisplay *ad = GET_ADDON_DISPLAY (d)

#define GET_ADDON_SCREEN(s, ad)				              \
    ((ScaleAddonScreen *) (s)->privates[(ad)->screenPrivateIndex].ptr)

#define ADDON_SCREEN(s)						               \
    ScaleAddonScreen *as = GET_ADDON_SCREEN (s, GET_ADDON_DISPLAY (s->display))

#define GET_ADDON_WINDOW(w, as)				              \
    ((ScaleAddonWindow *) (w)->privates[(as)->windowPrivateIndex].ptr)

#define ADDON_WINDOW(w)						                 \
    ScaleAddonWindow *aw = GET_ADDON_WINDOW (w,                                  \
	    GET_ADDON_SCREEN (w->screen, GET_ADDON_DISPLAY (w->screen->display)))


static void
scaleaddonFreeWindowTitle (CompScreen *s)
{
    ADDON_SCREEN (s);

    if (!as->textPixmap)
	return;

    releasePixmapFromTexture(s, &as->textTexture);
    initTexture (s, &as->textTexture);
    XFreePixmap (s->display->display, as->textPixmap);
    as->textPixmap = None;
}

static void
scaleaddonRenderWindowTitle (CompWindow *w)
{
    CompTextAttrib tA;
    float          scale;
    int            stride;
    void*          data;

    ADDON_SCREEN (w->screen);
    SCALE_WINDOW (w);

    scaleaddonFreeWindowTitle(w->screen);

    if (!scaleaddonGetWindowTitle (w->screen))
	return;

    scale = sw->slot ? sw->slot->scale : sw->scale;
    tA.maxwidth = (w->attrib.width * scale) -
	          (2 * scaleaddonGetBorderSize (w->screen));
    tA.maxheight = (w->attrib.height * scale) -
     	           (2 * scaleaddonGetBorderSize (w->screen));
    tA.screen = w->screen;
    tA.size = scaleaddonGetTitleSize (w->screen);
    tA.color[0] = scaleaddonGetFontColorRed (w->screen);
    tA.color[1] = scaleaddonGetFontColorGreen (w->screen);
    tA.color[2] = scaleaddonGetFontColorBlue (w->screen);
    tA.color[3] = scaleaddonGetFontColorAlpha (w->screen);
    tA.style = (scaleaddonGetTitleBold (w->screen)) ?
	       TEXT_STYLE_BOLD : TEXT_STYLE_NORMAL;
    tA.family = "Sans";
    tA.ellipsize = TRUE;

    tA.renderMode = TextRenderWindowTitle;
    tA.data = (void*)w->id;

    if ((*w->screen->display->fileToImage) (w->screen->display, TEXT_ID, (char *)&tA,
					    &as->textWidth, &as->textHeight, &stride, &data))
    {
	as->textPixmap = (Pixmap)data;
	bindPixmapToTexture (w->screen, &as->textTexture, as->textPixmap,
			     as->textWidth, as->textHeight, 32);
    }
    else
    {
	as->textPixmap = None;
	as->textWidth = 0;
	as->textHeight = 0;
    }
}

static void
scaleaddonDrawWindowTitle (CompWindow *w)
{
    ADDON_SCREEN (w->screen);
    SCALE_WINDOW (w);

    GLboolean wasBlend;
    GLint     oldBlendSrc, oldBlendDst;

    float width = as->textWidth;
    float height = as->textHeight;
    float border = scaleaddonGetBorderSize (w->screen);

    float x = sw->tx + w->attrib.x +
	      ((w->attrib.width * sw->scale) / 2) - (as->textWidth / 2);
    float y = sw->ty + w->attrib.y +
	      ((w->attrib.height * sw->scale) / 2) - (as->textHeight / 2);

    x = floor (x);
    y = floor (y);
	
    wasBlend = glIsEnabled (GL_BLEND);
    glGetIntegerv (GL_BLEND_SRC, &oldBlendSrc);
    glGetIntegerv (GL_BLEND_DST, &oldBlendDst);

    if (!wasBlend)
	glEnable (GL_BLEND);

    glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glColor4us (scaleaddonGetBackColorRed (w->screen),
		scaleaddonGetBackColorGreen (w->screen),
		scaleaddonGetBackColorBlue (w->screen),
		scaleaddonGetBackColorAlpha (w->screen));

    glPushMatrix ();

    glTranslatef (x, y - height, 0.0f);
    glRectf (0.0f, height, width, 0.0f);
    glRectf (0.0f, 0.0f, width, -border);
    glRectf (0.0f, height + border, width, height);
    glRectf (-border, height, 0.0f, 0.0f);
    glRectf (width, height, width + border, 0.0f);
    glTranslatef (-border, -border, 0.0f);

#define CORNER(a,b) \
    for (k = a; k < b; k++) \
    {\
	float rad = k* (3.14159 / 180.0f);\
	glVertex2f (0.0f, 0.0f);\
	glVertex2f (cos (rad) * border, sin (rad) * border);\
	glVertex2f (cos ((k - 1) * (3.14159 / 180.0f)) * border, \
		    sin ((k - 1) * (3.14159 / 180.0f)) * border);\
    }

    /* Rounded corners */
    int k;

    glTranslatef (border, border, 0.0f);
    glBegin (GL_TRIANGLES);
    CORNER (180, 270) glEnd();
    glTranslatef (-border, -border, 0.0f);

    glTranslatef (width + border, border, 0.0f);
    glBegin (GL_TRIANGLES);
    CORNER (270, 360) glEnd();
    glTranslatef (-(width + border), -border, 0.0f);

    glTranslatef (border, height + border, 0.0f);
    glBegin (GL_TRIANGLES);
    CORNER (90, 180) glEnd();
    glTranslatef (-border, -(height + border), 0.0f);

    glTranslatef (width + border, height + border, 0.0f);
    glBegin (GL_TRIANGLES);
    CORNER (0, 90) glEnd();
    glTranslatef (-(width + border), -(height + border), 0.0f);

    glPopMatrix ();

#undef CORNER

    glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f (1.0, 1.0, 1.0, 1.0);

    enableTexture (w->screen, &as->textTexture, COMP_TEXTURE_FILTER_GOOD);

    CompMatrix *m = &as->textTexture.matrix;

    glBegin (GL_QUADS);

    glTexCoord2f (COMP_TEX_COORD_X(m, 0),COMP_TEX_COORD_Y(m ,0));
    glVertex2f (x, y - height);
    glTexCoord2f (COMP_TEX_COORD_X(m, 0),COMP_TEX_COORD_Y(m, height));
    glVertex2f (x, y);
    glTexCoord2f (COMP_TEX_COORD_X(m, width),COMP_TEX_COORD_Y(m, height));
    glVertex2f (x + width, y);
    glTexCoord2f (COMP_TEX_COORD_X(m, width),COMP_TEX_COORD_Y(m, 0));
    glVertex2f (x + width, y - height);

    glEnd ();

    disableTexture (w->screen, &as->textTexture);
    glColor4usv (defaultColor);

    if (!wasBlend)
	glDisable (GL_BLEND);
    glBlendFunc (oldBlendSrc, oldBlendDst);
}

static void
scaleaddonDrawWindowHighlight (CompWindow *w)
{
    SCALE_WINDOW (w);
    ADDON_WINDOW (w);

    GLboolean wasBlend;
    GLint     oldBlendSrc, oldBlendDst;

    if (aw->rescaled)
	return;

    float x      = sw->tx + w->attrib.x - (w->input.left * sw->scale);
    float y      = sw->ty + w->attrib.y - (w->input.top * sw->scale);
    float width  = WIN_W(w) * sw->scale;
    float height = WIN_H(w) * sw->scale;

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

    glColor4us (scaleaddonGetHighlightColorRed (w->screen),
		scaleaddonGetHighlightColorGreen (w->screen),
		scaleaddonGetHighlightColorBlue (w->screen),
		scaleaddonGetHighlightColorAlpha (w->screen));

    glRectf (x, y + height, x + width, y);

    glColor4usv (defaultColor);

    if (!wasBlend)
	glDisable (GL_BLEND);
    glBlendFunc (oldBlendSrc, oldBlendDst);
}

static void
scaleaddonCheckHoveredWindow (CompScreen *s)
{
    CompDisplay *d = s->display;

    ADDON_DISPLAY (d);
    SCALE_DISPLAY (d);

    if (sd->hoveredWindow != ad->lastHoveredWindow)
    {
	CompWindow *w, *lw;

	w = findWindowAtDisplay (d, sd->hoveredWindow);
	if (w)
	{
	    scaleaddonRenderWindowTitle (w);
	    addWindowDamage (w);
	}
	else
	    scaleaddonFreeWindowTitle (s);

	lw = findWindowAtDisplay (d, ad->lastHoveredWindow);
	if (lw)
	    addWindowDamage (lw);

	ad->lastHoveredWindow = sd->hoveredWindow;
    }
}

static CompWindow *
scaleaddonCheckForWindowAt (CompScreen * s, int x, int y)
{
    int x1, y1, x2, y2;
    CompWindow *w;

    for (w = s->reverseWindows; w; w = w->prev)
    {
        SCALE_WINDOW(w);

        if (sw->slot)
	{
	    x1 = w->attrib.x - w->input.left * sw->scale;
            y1 = w->attrib.y - w->input.top * sw->scale;
            x2 = w->attrib.x + (w->width + w->input.right) * sw->scale;
            y2 = w->attrib.y + (w->height + w->input.bottom) * sw->scale;

            x1 += sw->tx;
            y1 += sw->ty;
            x2 += sw->tx;
            y2 += sw->ty;

            if (x1 <= x && y1 <= y && x2 > x && y2 > y)
                return w;
        }
    }

    return NULL;
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

	if (!ss->grabIndex)
	    return FALSE;

	if (state & CompActionStateInitKey)
	{
	    SCALE_DISPLAY (d);
	    w = findWindowAtDisplay (d, sd->hoveredWindow);
	}
	else
	    w = scaleaddonCheckForWindowAt (s, pointerX, pointerY);

        if (w)
	{
	    closeWindow (w, getCurrentTimeFromDisplay (d));
	    return TRUE;
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

	if (!ss->grabIndex)
	    return FALSE;

	if (state & CompActionStateInitKey)
	{
	    SCALE_DISPLAY (d);
	    w = findWindowAtDisplay (d, sd->hoveredWindow);
	}
	else
	    w = scaleaddonCheckForWindowAt (s, pointerX, pointerY);

        if (w)
	{
	    SCALE_WINDOW (w);
	    ADDON_WINDOW (w);

	    XRectangle outputRect;
	    BOX outputBox;
	    int head;

	    if (!sw->slot)
		return FALSE;

	    head = outputDeviceForPoint (s, sw->slot->x1, sw->slot->y1);
	    outputBox = w->screen->outputDev[head].region.extents;

	    outputRect.x      = outputBox.x1;
	    outputRect.y      = outputBox.y1;
	    outputRect.width  = outputBox.x2 - outputBox.x1;
	    outputRect.height = outputBox.y2 - outputBox.y1;

	    if (!aw->rescaled)
	    {
		aw->oldAbove = w->next;
		raiseWindow(w);
	
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
	    damageScreen (w->screen);

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
    case MotionNotify:
	{
	    CompScreen *s;
	    s = findScreenAtDisplay (d, event->xmotion.root);

	    if (s)
	    {
		SCALE_SCREEN (s);
		if (ss->grabIndex)
		    scaleaddonCheckHoveredWindow (s);
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
    ADDON_SCREEN (w->screen);
    SCALE_SCREEN (w->screen);
    SCALE_DISPLAY (w->screen->display);

    UNWRAP (as, ss, scalePaintDecoration);
    (*ss->scalePaintDecoration) (w, attrib, transform, region, mask);
    WRAP (as, ss, scalePaintDecoration, scaleaddonScalePaintDecoration);

    scaleaddonCheckHoveredWindow (w->screen);

    if ((w->id == sd->hoveredWindow) &&
	((ss->state == SCALE_STATE_WAIT) || (ss->state == SCALE_STATE_OUT)))
    {
	if (scaleaddonGetWindowHighlight (w->screen))
	    scaleaddonDrawWindowHighlight (w);

	if (as->textPixmap)
	    scaleaddonDrawWindowTitle (w);
    }
}

static void
scaleaddonHandleCompizEvent (CompDisplay *d,
			     char        *pluginName,
			     char        *eventName,
			     CompOption  *option,
			     int         nOption)
{
    ADDON_DISPLAY(d);

    UNWRAP (ad, d, handleCompizEvent);
    (*d->handleCompizEvent) (d, pluginName, eventName, option, nOption);
    WRAP (ad, d, handleCompizEvent, scaleaddonHandleCompizEvent);

    if ((strcmp (pluginName, "scale") == 0) &&
	(strcmp (eventName, "activate") == 0))
    {
	Window xid = getIntOptionNamed (option, nOption, "root", 0);
	Bool activated = getIntOptionNamed (option, nOption, "activated", FALSE);
	CompScreen *s = findScreenAtDisplay (d, xid);

	if (s)
	{
	    if (activated)
	    {
		addScreenAction (s, scaleaddonGetClose (s->display));
		addScreenAction (s, scaleaddonGetZoom (s->display));
	    }
	    else
	    {
		CompWindow *w;

		for (w = s->windows; w; w = w->next)
		{
		    ADDON_WINDOW (w);
		    aw->rescaled = FALSE;
		}

		removeScreenAction (s, scaleaddonGetClose (s->display));
		removeScreenAction (s, scaleaddonGetZoom (s->display));
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
organicCompareWindows (const void *elem1, const void *elem2)
{
    CompWindow *w1 = *((CompWindow **) elem1);
    CompWindow *w2 = *((CompWindow **) elem2);

    return (WIN_X(w1) + WIN_Y(w1)) - (WIN_X(w2) + WIN_Y(w2));
}

static double
layoutOrganicCalculateOverlap (CompScreen * s, int win, int x, int y)
{
    SCALE_SCREEN (s);
    ADDON_SCREEN (s);

    int i;
    int x1 = x;
    int y1 = y;
    int x2 = x1 + WIN_W (ss->windows[win]) * as->scale;
    int y2 = y1 + WIN_H (ss->windows[win]) * as->scale;
    int overlapx = 0, overlapy = 0;
    int xmin, xmax;
    int ymin, ymax;

    double result = -0.01;

    for (i = 0; i < ss->nWindows; i++)
    {
	if (i == win)
	    continue;
	overlapx = overlapy = 0;
	xmax = MAX (ss->slots[i].x1, x1);
	xmin = MIN (ss->slots[i].x1 + WIN_W(ss->windows[i]) * as->scale, x2);
	if (xmax <= xmin)
	    overlapx = xmin - xmax;

	ymax = MAX (ss->slots[i].y1, y1);
	ymin = MIN (ss->slots[i].y1 + WIN_H(ss->windows[i]) * as->scale, y2);

	if (ymax <= ymin)
	    overlapy = ymin - ymax;

	result += (double)overlapx * overlapy;
    }
    return result;
}

static double
layoutOrganicFindBestHorizontalPosition (CompScreen * s, int win,
					 int *bestx, int areaWidth)
{
    SCALE_SCREEN (s);
    ADDON_SCREEN (s);

    int i;
    int y1 = ss->slots[win].y1;
    int y2 = ss->slots[win].y1 + WIN_H(ss->windows[win]) * as->scale;

    double bestoverlap = 1e31, overlap;
    int w = WIN_W(ss->windows[win]) * as->scale;

    *bestx = ss->slots[win].x1;

    for (i = 0; i < ss->nWindows; i++)
    {
	if (i == win)
	    continue;

	if (ss->slots[i].y1 < y2 &&
	    ss->slots[i].y1 + WIN_H(ss->windows[i]) * as->scale > y1)
	{
	    if (ss->slots[i].x1 - w >= 0)
	    {
		double overlap = layoutOrganicCalculateOverlap (s, win,
								ss->slots[i].x1 - w, y1);

		if (overlap < bestoverlap)
		{
		    *bestx = ss->slots[i].x1 - w;
		    bestoverlap = overlap;
		}
	    }
	    if (WIN_W(ss->windows[i]) * as->scale +
		ss->slots[i].x1 + w < areaWidth)
	    {
		double overlap = layoutOrganicCalculateOverlap (s, win,
								ss->slots[i].x1 +
								WIN_W(ss->windows[i]) *
		     						as->scale, y1);

		if (overlap < bestoverlap)
		{
		    *bestx = ss->slots[i].x1 + WIN_W(ss->windows[i]) * as->scale;
		    bestoverlap = overlap;
		}
	    }
	}
    }

    overlap = layoutOrganicCalculateOverlap (s, win, 0, y1);
    if (overlap < bestoverlap)
    {
	*bestx = 0;
	bestoverlap = overlap;
    }

    overlap = layoutOrganicCalculateOverlap (s, win, areaWidth - w, y1);
    if (overlap < bestoverlap)
    {
	*bestx = areaWidth - w;
	bestoverlap = overlap;
    }

    return bestoverlap;
}

static double
layoutOrganicFindBestVerticalPosition (CompScreen * s, int win,
				       int *besty, int areaHeight)
{
    SCALE_SCREEN (s);
    ADDON_SCREEN (s);

    int i;
    int x1 = ss->slots[win].x1;
    int x2 = ss->slots[win].x1 + WIN_W(ss->windows[win]) * as->scale;

    double bestoverlap = 1e31, overlap;
    int h = WIN_H(ss->windows[win]) * as->scale;

    *besty = ss->slots[win].y1;

    for (i = 0; i < ss->nWindows; i++)
    {
	if (i == win)
	    continue;

	if (ss->slots[i].x1 < x2 &&
	    ss->slots[i].x1 + WIN_W(ss->windows[i]) * as->scale > x1)
	{
	    if (ss->slots[i].y1 - h >= 0 && ss->slots[i].y1 < areaHeight)
	    {
		double overlap = layoutOrganicCalculateOverlap (s, win, x1,
								ss->slots[i].y1 - h);
		if (overlap < bestoverlap)
		{
		    *besty = ss->slots[i].y1 - h;
		    bestoverlap = overlap;
		}
	    }
	    if (WIN_H(ss->windows[i]) * as->scale + ss->slots[i].y1 > 0 &&
		WIN_H(ss->windows[i]) * as->scale + h + ss->slots[i].y1 < areaHeight)
	    {
		double overlap = layoutOrganicCalculateOverlap (s, win, x1,
								WIN_H(ss->windows[i]) *
		     						as->scale +
								ss->slots[i].y1);

		if (overlap < bestoverlap)
		{
		    *besty = ss->slots[i].y1 + WIN_H(ss->windows[i]) * as->scale;
		    bestoverlap = overlap;
		}
	    }
	}
    }

    overlap = layoutOrganicCalculateOverlap (s, win, x1, 0);
    if (overlap < bestoverlap)
    {
	*besty = 0;
	bestoverlap = overlap;
    }

    overlap = layoutOrganicCalculateOverlap (s, win, x1, areaHeight - h);
    if (overlap < bestoverlap)
    {
	*besty = areaHeight - h;
	bestoverlap = overlap;
    }

    return bestoverlap;
}

static Bool
layoutOrganicLocalSearch (CompScreen * s, int areaWidth, int areaHeight)
{
    SCALE_SCREEN(s);
    Bool improvement;
    int i;
    double totaloverlap;

    do
    {
	improvement = FALSE;
	for (i = 0; i < ss->nWindows; i++)
	{
	    Bool improved;

	    do
	    {
		int newx, newy;
		double oldoverlap, overlaph, overlapv;

		improved = FALSE;
		oldoverlap = layoutOrganicCalculateOverlap (s, i,
 							    ss->slots[i].x1,
							    ss->slots[i].y1);

		overlaph =
		    layoutOrganicFindBestHorizontalPosition (s, i, &newx, areaWidth);
		overlapv =
		    layoutOrganicFindBestVerticalPosition (s, i, &newy, areaHeight);

		if (overlaph < oldoverlap - 0.1 ||
		    overlapv < oldoverlap - 0.1)
		{
		    improved = TRUE;
		    improvement = TRUE;
		    if (overlapv > overlaph)
			ss->slots[i].x1 = newx;
		    else
			ss->slots[i].y1 = newy;
		}

    	    }
	    while (improved);
	}
    }
    while (improvement);

    totaloverlap = 0.0;
    for (i = 0; i < ss->nWindows; i++)
    {
	totaloverlap += layoutOrganicCalculateOverlap (s,
						       i, ss->slots[i].x1,
						       ss->slots[i].y1);
    }
    return (totaloverlap > 0.1);
}

static void
layoutOrganicRemoveOverlap (CompScreen * s, int areaWidth, int areaHeight)
{
    int i;
    int spacing;
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

	    centerX = ss->slots[i].x1 + WIN_W(w) / 2;
	    centerY = ss->slots[i].y1 + WIN_H(w) / 2;

	    newWidth = (int)((1.0 - ORGANIC_STEP) *
			     (double)WIN_W(w)) - spacing / 2;
	    newHeight = (int)((1.0 - ORGANIC_STEP) *
			      (double)WIN_H(w)) - spacing / 2;
	    newX = centerX - (newWidth / 2);
	    newY = centerY - (newHeight / 2);

	    ss->slots[i].x1 = newX;
	    ss->slots[i].y1 = newY;
	    ss->slots[i].x2 = newX + WIN_W(w);
	    ss->slots[i].y2 = newY + WIN_H(w);
	}
	as->scale -= ORGANIC_STEP;
    }
}

static Bool
layoutOrganicThumbs(CompScreen * s)
{
    CompWindow *w;

    int i;
    int cx, cy;
    int moMode;
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

    cx = cy = 0;

    qsort (ss->windows, ss->nWindows, sizeof(CompWindow *),
	   organicCompareWindows);

    for (i = 0; i < ss->nWindows; i++)
    {
	SCALE_WINDOW (ss->windows[i]);
	w = ss->windows[i];

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
	SCALE_WINDOW (ss->windows[i]);

	if (ss->type == ScaleTypeGroup)
	    raiseWindow (ss->windows[i]);

	ss->slots[i].x1 += ss->windows[i]->input.left + workArea.x;
	ss->slots[i].x2 += ss->windows[i]->input.left + workArea.x;
	ss->slots[i].y1 += ss->windows[i]->input.top + workArea.y;
	ss->slots[i].y2 += ss->windows[i]->input.top + workArea.y;
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
	case ScaleaddonScreenOptionTitleBold:
	case ScaleaddonScreenOptionTitleSize:
	case ScaleaddonScreenOptionBorderSize:
	case ScaleaddonScreenOptionFontColor:
	case ScaleaddonScreenOptionBackColor:
	    {
		ADDON_DISPLAY (s->display);

		scaleaddonFreeWindowTitle (s);
		ad->lastHoveredWindow = None;
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
    CompPlugin        *scale = findActivePlugin ("scale");
    CompOption        *option;
    int               nOption;

    if (!scale || !scale->vTable->getDisplayOptions)
	return FALSE;

    option = (*scale->vTable->getDisplayOptions) (scale, d, &nOption);

    if (getIntOptionNamed (option, nOption, "abi", 0) != SCALE_ABIVERSION)
    {
	compLogMessage (d, "scaleaddon", CompLogLevelError,
			"scale ABI version mismatch");
	return FALSE;
    }

    scaleDisplayPrivateIndex = getIntOptionNamed (option, nOption, "index", -1);
    if (scaleDisplayPrivateIndex < 0)
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

    WRAP (ad, d, handleEvent, scaleaddonHandleEvent);
    WRAP (ad, d, handleCompizEvent, scaleaddonHandleCompizEvent);

    d->privates[displayPrivateIndex].ptr = ad;

    ad->lastHoveredWindow = None;

    scaleaddonSetCloseInitiate (d, scaleaddonCloseWindow);
    scaleaddonSetZoomInitiate (d, scaleaddonZoomWindow);

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

    as->textPixmap = None;
    initTexture (s, &as->textTexture);

    WRAP (as, ss, scalePaintDecoration, scaleaddonScalePaintDecoration);
    WRAP (as, ss, layoutSlotsAndAssignWindows,
	  scaleaddonLayoutSlotsAndAssignWindows);

    scaleaddonSetTitleBoldNotify (s, scaleaddonScreenOptionChanged);
    scaleaddonSetTitleSizeNotify (s, scaleaddonScreenOptionChanged);
    scaleaddonSetBorderSizeNotify (s, scaleaddonScreenOptionChanged);
    scaleaddonSetFontColorNotify (s, scaleaddonScreenOptionChanged);
    scaleaddonSetBackColorNotify (s, scaleaddonScreenOptionChanged);

    s->privates[ad->screenPrivateIndex].ptr = as;

    return TRUE;
}

static void
scaleaddonFiniScreen (CompPlugin *p,
	      	      CompScreen *s)
{
    ADDON_SCREEN (s);
    SCALE_SCREEN (s);

    UNWRAP (as, ss, scalePaintDecoration);
    UNWRAP (as, ss, layoutSlotsAndAssignWindows);

    scaleaddonFreeWindowTitle (s);

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

    w->privates[as->windowPrivateIndex].ptr = aw;

    return TRUE;
}

static void
scaleaddonFiniWindow (CompPlugin *p,
		      CompWindow *w)
{
    ADDON_WINDOW (w);

    free (aw);
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

static int
scaleaddonGetVersion (CompPlugin *plugin,
	      	      int	 version)
{
    return ABIVERSION;
}

CompPluginVTable scaleaddonVTable = {
    "scaleaddon",
    scaleaddonGetVersion,
    0,
    scaleaddonInit,
    scaleaddonFini,
    scaleaddonInitDisplay,
    scaleaddonFiniDisplay,
    scaleaddonInitScreen,
    scaleaddonFiniScreen,
    scaleaddonInitWindow,
    scaleaddonFiniWindow,
    0,
    0,
    0,
    0,
    0, /* Deps */
    0, /* nDeps */
    0, /* Features */
    0  /* nFeatures */
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &scaleaddonVTable;
}
