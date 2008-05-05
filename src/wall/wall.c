/**
 *
 * Compiz wall plugin
 *
 * wall.c
 *
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
#include "wall_options.h"

#include <GL/glu.h>

#include <cairo-xlib-xrender.h>
#include <cairo.h>

#define PI 3.14159265359f
#define VIEWPORT_SWITCHER_SIZE   100
#define ARROW_SIZE 33

#define WIN_X(w) ((w)->attrib.x - (w)->input.left)
#define WIN_Y(w) ((w)->attrib.y - (w)->input.top)
#define WIN_W(w) ((w)->width + (w)->input.left + (w)->input.right)
#define WIN_H(w) ((w)->height + (w)->input.top + (w)->input.bottom)

#define getColorRGBA(name, _display) \
    r = wallGet##name##Red(_display) / 65535.0f;\
    g = wallGet##name##Green(_display) / 65535.0f; \
    b = wallGet##name##Blue(_display) / 65535.0f; \
    a = wallGet##name##Alpha(_display) / 65535.0f

static int displayPrivateIndex;
static int corePrivateIndex;

/* Enums */
typedef enum
{
    Up = 0,
    Left,
    Down,
    Right
} Direction;

typedef struct _WallCairoContext
{
    Pixmap      pixmap;
    CompTexture texture;

    cairo_surface_t *surface;
    cairo_t         *cr;

    int width;
    int height;
} WallCairoContext;

typedef struct _WallCore
{
    SetOptionForPluginProc setOptionForPlugin;
} WallCore;

typedef struct _WallDisplay
{
    int screenPrivateIndex;

    HandleEventProc handleEvent;
} WallDisplay;

typedef struct _WallScreen
{
    DonePaintScreenProc          donePaintScreen;
    PaintOutputProc              paintOutput;
    PaintScreenProc              paintScreen;
    PreparePaintScreenProc       preparePaintScreen;
    PaintTransformedOutputProc   paintTransformedOutput;
    PaintWindowProc              paintWindow;
    ActivateWindowProc           activateWindow;

    Bool moving; /* Used to track miniview movement */
    Bool showPreview;

    float curPosX;
    float curPosY;
    int   gotoX;
    int   gotoY;

    int boxTimeout;
    int boxOutputDevice;

    int grabIndex;
    int timer;

    Window moveWindow;

    Bool              miniScreen;
    WindowPaintAttrib mSAttribs;
    float             mSzCamera;

    int firstViewportX;
    int firstViewportY;
    int viewportWidth;
    int viewportHeight;
    int viewportBorder;

    int moveWindowX;
    int moveWindowY;

    WallCairoContext switcherContext;
    WallCairoContext thumbContext;
    WallCairoContext highlightContext;
    WallCairoContext arrowContext;
} WallScreen;

/* Helpers */
#define GET_WALL_CORE(c) \
    ((WallCore *) (c)->base.privates[corePrivateIndex].ptr)
#define WALL_CORE(c) \
    WallCore *wc = GET_WALL_CORE (c)
#define GET_WALL_DISPLAY(d)						\
    ((WallDisplay *) (d)->base.privates[displayPrivateIndex].ptr)
#define WALL_DISPLAY(d)				\
    WallDisplay *wd = GET_WALL_DISPLAY(d);

#define GET_WALL_SCREEN(s, wd)						\
    ((WallScreen *) (s)->base.privates[(wd)->screenPrivateIndex].ptr)
#define WALL_SCREEN(s)							\
    WallScreen *ws = GET_WALL_SCREEN(s, GET_WALL_DISPLAY(s->display))

#define GET_SCREEN					\
    CompScreen *s;					\
    Window xid;						\
    xid = getIntOptionNamed(option, nOption, "root", 0);\
    s = findScreenAtDisplay(d, xid);			\
    if (!s)						\
        return FALSE;

#define sigmoid(x) (1.0f / (1.0f + exp (-5.5f * 2 * ((x) - 0.5))))
#define sigmoidProgress(x) ((sigmoid (x) - sigmoid (0)) / \
			    (sigmoid (1) - sigmoid (0)))


static void
wallClearCairoLayer (cairo_t *cr)
{
    cairo_save (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint (cr);
    cairo_restore (cr);
}

static void
wallDrawSwitcherBackground (CompScreen *s)
{
    cairo_t         *cr;
    cairo_pattern_t *pattern;
    float           outline = 2.0f;
    int             width, height, radius;
    float           r, g, b, a;
    int             i, j;

    WALL_SCREEN (s);

    cr = ws->switcherContext.cr;
    wallClearCairoLayer (cr);

    width = ws->switcherContext.width - outline;
    height = ws->switcherContext.height - outline;

    cairo_save (cr);
    cairo_translate (cr, outline / 2.0f, outline / 2.0f);

    /* set the pattern for the switcher's background */
    pattern = cairo_pattern_create_linear (0, 0, width, height);
    getColorRGBA (BackgroundGradientBaseColor, s->display);
    cairo_pattern_add_color_stop_rgba (pattern, 0.00f, r, g, b, a);
    getColorRGBA (BackgroundGradientHighlightColor, s->display);
    cairo_pattern_add_color_stop_rgba (pattern, 0.65f, r, g, b, a);
    getColorRGBA (BackgroundGradientShadowColor, s->display);
    cairo_pattern_add_color_stop_rgba (pattern, 0.85f, r, g, b, a);
    cairo_set_source (cr, pattern);

    /* draw the border's shape */
    radius = wallGetEdgeRadius (s->display);
    if (radius)
    {
	cairo_arc (cr, radius, radius, radius, PI, 1.5f * PI);
	cairo_arc (cr, radius + width - 2 * radius,
		   radius, radius, 1.5f * PI, 2.0 * PI);
	cairo_arc (cr, width - radius, height - radius, radius, 0,  PI / 2.0f);
	cairo_arc (cr, radius, height - radius, radius,  PI / 2.0f, PI);
    }
    else
	cairo_rectangle (cr, 0, 0, width, height);

    cairo_close_path (cr);

    /* apply pattern to background... */
    cairo_fill_preserve (cr);

    /* ... and draw an outline */
    cairo_set_line_width (cr, outline);
    getColorRGBA (OutlineColor, s->display);
    cairo_set_source_rgba (cr, r, g, b, a);
    cairo_stroke(cr);

    cairo_pattern_destroy (pattern);
    cairo_restore (cr);

    cairo_save (cr);
    for (i = 0; i < s->vsize; i++)
    {
	cairo_translate (cr, 0.0, ws->viewportBorder);
	cairo_save (cr);
	for (j = 0; j < s->hsize; j++)
	{
	    cairo_translate (cr, ws->viewportBorder, 0.0);

	    /* this cuts a hole into our background */
	    cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
	    cairo_rectangle (cr, 0, 0, ws->viewportWidth, ws->viewportHeight);

	    cairo_fill_preserve (cr);
	    cairo_set_operator (cr, CAIRO_OPERATOR_XOR);
	    cairo_fill (cr);

	    cairo_translate (cr, ws->viewportWidth, 0.0);
	}
	cairo_restore(cr);

	cairo_translate (cr, 0.0, ws->viewportHeight);
    }
    cairo_restore (cr);
}

static void
wallDrawThumb (CompScreen *s)
{
    cairo_t         *cr;
    cairo_pattern_t *pattern;
    float           r, g, b, a;
    float           outline = 2.0f;
    int             width, height;

    WALL_SCREEN(s);

    cr = ws->thumbContext.cr;
    wallClearCairoLayer (cr);

    width  = ws->thumbContext.width - outline;
    height = ws->thumbContext.height - outline;

    cairo_translate (cr, outline / 2.0f, outline / 2.0f);

    pattern = cairo_pattern_create_linear (0, 0, width, height);
    getColorRGBA (ThumbGradientBaseColor, s->display);
    cairo_pattern_add_color_stop_rgba (pattern, 0.0f, r, g, b, a);
    getColorRGBA (ThumbGradientHighlightColor, s->display);
    cairo_pattern_add_color_stop_rgba (pattern, 1.0f, r, g, b, a);

    /* apply the pattern for thumb background */
    cairo_set_source (cr, pattern);
    cairo_rectangle (cr, 0, 0, width, height);
    cairo_fill_preserve (cr);

    cairo_set_line_width (cr, outline);
    getColorRGBA (OutlineColor, s->display);
    cairo_set_source_rgba (cr, r, g, b, a);
    cairo_stroke (cr);

    cairo_pattern_destroy (pattern);

    cairo_restore (cr);
}

static void
wallDrawHighlight(CompScreen *s)
{
    cairo_t         *cr;
    cairo_pattern_t *pattern;
    int             width, height;
    float           r, g, b, a;
    float           outline = 2.0f;


    WALL_SCREEN(s);

    cr = ws->highlightContext.cr;
    wallClearCairoLayer (cr);

    width  = ws->highlightContext.width - outline;
    height = ws->highlightContext.height - outline;

    cairo_translate (cr, outline / 2.0f, outline / 2.0f);

    pattern = cairo_pattern_create_linear (0, 0, width, height);
    getColorRGBA (ThumbHighlightGradientBaseColor, s->display);
    cairo_pattern_add_color_stop_rgba (pattern, 0.0f, r, g, b, a);
    getColorRGBA (ThumbHighlightGradientShadowColor, s->display);
    cairo_pattern_add_color_stop_rgba (pattern, 1.0f, r, g, b, a);

    /* apply the pattern for thumb background */
    cairo_set_source (cr, pattern);
    cairo_rectangle (cr, 0, 0, width, height);
    cairo_fill_preserve (cr);

    cairo_set_line_width (cr, outline);
    getColorRGBA (OutlineColor, s->display);
    cairo_set_source_rgba (cr, r, g, b, a);
    cairo_stroke (cr);

    cairo_pattern_destroy (pattern);

    cairo_restore (cr);
}

static void
wallDrawArrow (CompScreen *s)
{
    cairo_t *cr;
    float   outline = 2.0f;
    float   r, g, b, a;

    WALL_SCREEN(s);

    cr = ws->arrowContext.cr;
    wallClearCairoLayer (cr);

    cairo_translate (cr, outline / 2.0f, outline / 2.0f);

    /* apply the pattern for thumb background */
    cairo_set_line_width (cr, outline);

    /* draw top part of the arrow */
    getColorRGBA (ArrowBaseColor, s->display);
    cairo_set_source_rgba (cr, r, g, b, a);
    cairo_move_to (cr, 15, 0);
    cairo_line_to (cr, 30, 30);
    cairo_line_to (cr, 15, 24.5);
    cairo_line_to (cr, 15, 0);
    cairo_fill (cr);

    /* draw bottom part of the arrow */
    getColorRGBA (ArrowShadowColor, s->display);
    cairo_set_source_rgba (cr, r, g, b, a);
    cairo_move_to (cr, 15, 0);
    cairo_line_to (cr, 0, 30);
    cairo_line_to (cr, 15, 24.5);
    cairo_line_to (cr, 15, 0);
    cairo_fill (cr);

    /* draw the arrow outline */
    getColorRGBA (OutlineColor, s->display);
    cairo_set_source_rgba (cr, r, g, b, a);
    cairo_move_to (cr, 15, 0);
    cairo_line_to (cr, 30, 30);
    cairo_line_to (cr, 15, 24.5);
    cairo_line_to (cr, 0, 30);
    cairo_line_to (cr, 15, 0);
    cairo_stroke (cr);

    cairo_restore (cr);
}

static void
wallSetupCairoContext (CompScreen       *s,
		       WallCairoContext *context)
{
    XRenderPictFormat *format;
    Screen            *screen;
    int               width, height;

    screen = ScreenOfDisplay (s->display->display, s->screenNum);

    width = context->width;
    height = context->height;

    initTexture (s, &context->texture);

    format = XRenderFindStandardFormat (s->display->display,
					PictStandardARGB32);

    context->pixmap = XCreatePixmap (s->display->display, s->root,
				     width, height, 32);

    if (!bindPixmapToTexture(s, &context->texture, context->pixmap,
			     width, height, 32))
    {
	compLogMessage (s->display, "wall", CompLogLevelError,
			"Couldn't create cairo context for switcher");
    }

    context->surface =
	cairo_xlib_surface_create_with_xrender_format (s->display->display,
						       context->pixmap,
						       screen, format,
						       width, height);

    context->cr = cairo_create (context->surface);
    wallClearCairoLayer (context->cr);
}

static void
wallDestroyCairoContext (CompScreen       *s,
			 WallCairoContext *context)
{
    if (context->cr)
	cairo_destroy (context->cr);

    if (context->surface)
	cairo_surface_destroy (context->surface);

    finiTexture (s, &context->texture);

    if (context->pixmap)
	XFreePixmap (s->display->display, context->pixmap);
}

static Bool
wallCheckDestination (CompScreen *s,
		      int        destX,
		      int        destY)
{
    if (s->x - destX < 0)
	return FALSE;

    if (s->x - destX >= s->hsize)
	return FALSE;

    if (s->y - destY >= s->vsize)
	return FALSE;

    if (s->y - destY < 0)
	return FALSE;

    return TRUE;
}

static void
wallReleaseMoveWindow (CompScreen *s)
{
    CompWindow *w;
    WALL_SCREEN (s);

    w = findWindowAtScreen (s, ws->moveWindow);
    if (w)
	syncWindowPosition (w);
    ws->moveWindow = 0;
}

static void
wallComputeTranslation (CompScreen *s,
			float      *x,
			float      *y)
{
    float dx, dy, elapsed, duration;

    WALL_SCREEN (s);

    duration = wallGetSlideDuration (s->display) * 1000.0;
    if (duration != 0.0)
	elapsed = 1.0 - (ws->timer / duration);
    else
	elapsed = 1.0;

    if (elapsed < 0.0)
	elapsed = 0.0;
    if (elapsed > 1.0)
	elapsed = 1.0;

    /* Use temporary variables to you can pass in &ps->cur_x */
    dx = (ws->gotoX - ws->curPosX) * elapsed + ws->curPosX;
    dy = (ws->gotoY - ws->curPosY) * elapsed + ws->curPosY;

    *x = dx;
    *y = dy;
}

static Bool
wallMoveViewport (CompScreen *s,
		  int        x,
		  int        y,
		  Window     moveWindow)
{
    WALL_SCREEN (s);

    if (!x && !y)
	return FALSE;

    if (wallCheckDestination (s, x, y))
    {
	if (ws->moveWindow != moveWindow)
	{
	    CompWindow *w;

	    wallReleaseMoveWindow (s);
	    w = findWindowAtScreen (s, moveWindow);
	    if (w)
    	    {
		if (!(w->type & (CompWindowTypeDesktopMask |
				 CompWindowTypeDockMask)))
		{
		    if (!(w->state & CompWindowStateStickyMask))
		    {
			ws->moveWindow = w->id;
			ws->moveWindowX = w->attrib.x;
			ws->moveWindowY = w->attrib.y;
			raiseWindow (w);
		    }
		}
	    }
	}

	if (!ws->moving)
	{
	    ws->curPosX = s->x;
	    ws->curPosY = s->y;
	}
	ws->gotoX = s->x - x;
	ws->gotoY = s->y - y;

	if (!ws->grabIndex)
	    ws->grabIndex = pushScreenGrab (s, s->invisibleCursor, "wall");

	moveScreenViewport (s, x, y, TRUE);

	ws->moving = TRUE;
	ws->boxOutputDevice = outputDeviceForPoint (s, pointerX, pointerY);
    }

    if (ws->moving)
    {
	if (wallGetShowSwitcher (s->display))
	    ws->boxTimeout = wallGetPreviewTimeout (s->display) * 1000;
	else
	    ws->boxTimeout = 0;

	if (otherScreenGrabExist (s, "move", "scale", "switcher",
				  "group-drag", "wall", 0))
	{
	    ws->boxTimeout = 0;
	    ws->moving = FALSE;
	}

	ws->timer = wallGetSlideDuration (s->display) * 1000;
    }

    damageScreen (s);

    return ws->moving;
}

static void
wallHandleEvent (CompDisplay *d,
		 XEvent      *event)
{
    WALL_DISPLAY (d);

    switch (event->type)
    {
    case ClientMessage:
	if (event->xclient.message_type == d->desktopViewportAtom)
	{
	    int        dx, dy;
	    CompScreen *s;

    	    s = findScreenAtDisplay (d, event->xclient.window);
	    if (!s)
		break;

	    if (otherScreenGrabExist (s, "switcher", "scale", 0))
		break;

    	    dx = event->xclient.data.l[0] / s->width - s->x;
	    dy = event->xclient.data.l[1] / s->height - s->y;

	    if (!dx && !dy)
		break;

	    wallMoveViewport (s, -dx, -dy, None);
	}
	break;
    }

    UNWRAP (wd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (wd, d, handleEvent, wallHandleEvent);
}

static void
wallActivateWindow (CompWindow *w)
{
    CompScreen *s = w->screen;

    WALL_SCREEN (s);

    if (w->placed && !otherScreenGrabExist (s, "wall", "switcher", 0))
    {
	int dx, dy;

	defaultViewportForWindow (w, &dx, &dy);
	dx -= s->x;
	dy -= s->y;
	
	if (dx || dy)
	    wallMoveViewport (s, -dx, -dy, None);
    }

    UNWRAP (ws, s, activateWindow);
    (*s->activateWindow) (w);
    WRAP (ws, s, activateWindow, wallActivateWindow);
}

static Bool
wallNext (CompDisplay     *d,
	  CompAction      *action,
	  CompActionState state,
	  CompOption      *option,
	  int             nOption)
{
    GET_SCREEN;

    if ((s->x == s->hsize - 1) && (s->y == s->vsize - 1))
	wallMoveViewport (s, s->hsize - 1, s->vsize - 1, None);
    else if (s->x == s->hsize - 1)
	wallMoveViewport (s, s->hsize - 1, -1, None);
    else
	wallMoveViewport (s, -1, 0, None);

    return TRUE;
}

static Bool
wallPrev (CompDisplay     *d,
	  CompAction      *action,
	  CompActionState state,
	  CompOption      *option,
	  int             nOption)
{
    GET_SCREEN;

    if ((s->x == 0) && (s->y == 0))
	wallMoveViewport (s, -(s->hsize - 1), -(s->vsize - 1), None);
    else if (s->x == 0)
	wallMoveViewport (s, -(s->hsize - 1), 1, None);
    else
	wallMoveViewport (s, 1, 0, None);

    return TRUE;
}

static void
wallCheckAmount (CompScreen *s,
		 int        dx,
		 int        dy,
		 int        *amountX,
		 int        *amountY)
{
    *amountX = -dx;
    *amountY = -dy;

    if (wallGetAllowWraparound (s->display))
    {
	if ((s->x + dx) < 0)
	    *amountX = -(s->hsize + dx);
	else if ((s->x + dx) >= s->hsize)
	    *amountX = s->hsize - dx;

	if ((s->y + dy) < 0)
	    *amountY = -(s->vsize + dy);
	else if ((s->y + dy) >= s->vsize)
	    *amountY = s->vsize - dy;
    }
}

static Bool
wallInitiate (CompScreen      *s,
	      int             dx,
	      int             dy,
	      Window          win,
	      CompAction      *action,
	      CompActionState state)
{
    int amountX, amountY;

    WALL_SCREEN (s);

    wallCheckAmount (s, dx, dy, &amountX, &amountY);
    wallMoveViewport (s, amountX, amountY, win);

    if (state & CompActionStateInitKey)
	action->state |= CompActionStateTermKey;

    if (state & CompActionStateInitButton)
	action->state |= CompActionStateTermButton;

    ws->showPreview = TRUE;

    return TRUE;
}

static Bool
wallTerminate (CompDisplay     *d,
	       CompAction      *action,
	       CompActionState state,
	       CompOption      *option,
	       int             nOption)
{
    CompScreen *s;

    for (s = d->screens; s; s = s->next)
    {
	WALL_SCREEN (s);

	if (ws->showPreview)
	{
	    ws->showPreview = FALSE;
	    damageScreen (s);
	}
    }

    if (action)
	action->state &= ~(CompActionStateTermKey | CompActionStateTermButton);

    return FALSE;
}

static Bool
wallInitiateFlip (CompScreen *s,
		  Direction  direction,
		  Bool       dnd)
{
    int dx, dy;
    int amountX, amountY;

    if (otherScreenGrabExist (s, "wall", "move", "group-drag", 0))
	return FALSE;

    if (dnd)
    {
	if (!wallGetEdgeflipDnd (s))
	    return FALSE;

	if (otherScreenGrabExist (s, "wall", 0))
	    return FALSE;
    }
    else if (otherScreenGrabExist (s, "wall", "group-drag", 0))
    {
	/* not wall or group means move */
	if (!wallGetEdgeflipMove (s))
	    return FALSE;
    }
    else if (otherScreenGrabExist (s, "wall", 0))
    {
	/* move was ruled out before, so we have group */
	if (!wallGetEdgeflipDnd (s))
	    return FALSE;
    }
    else if (!wallGetEdgeflipPointer (s))
	return FALSE;

    switch (direction)
    {
    case Left:
	dx = -1; dy = 0;
	break;
    case Right:
	dx = 1; dy = 0;
	break;
    case Up:
	dx = 0; dy = -1;
	break;
    case Down:
	dx = 0; dy = 1;
	break;
    default:
	dx = 0; dy = 0;
	break;
    }

    wallCheckAmount (s, dx, dy, &amountX, &amountY);
    if (wallMoveViewport (s, amountX, amountY, None))
    {
	int offsetX, offsetY;
	int warpX, warpY;

	if (dx < 0)
	{
	    offsetX = s->width - 10;
	    warpX = pointerX + s->width;
	}
	else if (dx > 0)
	{
	    offsetX = 1- s->width;
	    warpX = pointerX - s->width;
	}
	else
	{
	    offsetX = 0;
	    warpX = lastPointerX;
	}

	if (dy < 0)
	{
	    offsetY = s->height - 10;
	    warpY = pointerY + s->height;
	}
	else if (dy > 0)
	{
	    offsetY = 1- s->height;
	    warpY = pointerY - s->height;
	}
	else
	{
	    offsetY = 0;
	    warpY = lastPointerY;
	}

	warpPointer (s, offsetX, offsetY);
	lastPointerX = warpX;
	lastPointerY = warpY;
    }

    return TRUE;
}

static Bool
wallLeft (CompDisplay     *d,
	  CompAction      *action,
	  CompActionState state,
	  CompOption      *option,
	  int             nOption)
{
    GET_SCREEN;

    return wallInitiate (s, -1, 0, None, action, state);
}

static Bool
wallRight (CompDisplay     *d,
	   CompAction      *action,
	   CompActionState state,
	   CompOption      *option,
	   int             nOption)
{
    GET_SCREEN;

    return wallInitiate (s, 1, 0, None, action, state);
}

static Bool
wallUp (CompDisplay     *d,
	CompAction      *action,
	CompActionState state,
	CompOption      *option,
	int             nOption)
{
    GET_SCREEN;

    return wallInitiate (s, 0, -1, None, action, state);
}

static Bool
wallDown (CompDisplay     *d,
	  CompAction      *action,
	  CompActionState state,
	  CompOption      *option,
	  int             nOption)
{
    GET_SCREEN;

    return wallInitiate (s, 0, 1, None, action, state);
}

static Bool
wallFlipLeft (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption)
{
    GET_SCREEN;

    return wallInitiateFlip (s, Left, (state & CompActionStateInitEdgeDnd));
}

static Bool
wallFlipRight (CompDisplay     *d,
	       CompAction      *action,
	       CompActionState state,
	       CompOption      *option,
	       int             nOption)
{
    GET_SCREEN;

    return wallInitiateFlip (s, Right, (state & CompActionStateInitEdgeDnd));
}

static Bool
wallFlipUp (CompDisplay     *d,
	    CompAction      *action,
	    CompActionState state,
	    CompOption      *option,
	    int             nOption)
{
    GET_SCREEN;

    return wallInitiateFlip (s, Up, (state & CompActionStateInitEdgeDnd));
}

static Bool
wallFlipDown (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption)
{
    GET_SCREEN;

    return wallInitiateFlip (s, Down, (state & CompActionStateInitEdgeDnd));
}

static Bool
wallLeftWithWindow (CompDisplay     *d,
		    CompAction      *action,
		    CompActionState state,
		    CompOption      *option,
		    int             nOption)
{
    GET_SCREEN;
    Window win = getIntOptionNamed (option, nOption, "window", 0);

    return wallInitiate (s, -1, 0, win, action, state);
}

static Bool
wallRightWithWindow (CompDisplay     *d,
		     CompAction      *action,
		     CompActionState state,
		     CompOption      *option,
		     int             nOption)
{
    GET_SCREEN;
    Window win = getIntOptionNamed (option, nOption, "window", 0);

    return wallInitiate (s, 1, 0, win, action, state);
}

static Bool
wallUpWithWindow (CompDisplay     *d,
		  CompAction      *action,
		  CompActionState state,
		  CompOption      *option,
		  int             nOption)
{
    GET_SCREEN;
    Window win = getIntOptionNamed (option, nOption, "window", 0);

    return wallInitiate (s, 0, -1, win, action, state);
}

static Bool
wallDownWithWindow (CompDisplay     *d,
		    CompAction      *action,
		    CompActionState state,
		    CompOption      *option,
		    int             nOption)
{
    GET_SCREEN;
    Window win = getIntOptionNamed (option, nOption, "window", 0);

    return wallInitiate (s, 0, 1, win, action, state);
}

static inline void
wallDrawQuad (CompMatrix *matrix, BOX *box)
{
    glTexCoord2f (COMP_TEX_COORD_X (matrix, box->x1),
		  COMP_TEX_COORD_Y (matrix, box->y2));
    glVertex2i (box->x1, box->y2);
    glTexCoord2f (COMP_TEX_COORD_X (matrix, box->x2),
		  COMP_TEX_COORD_Y (matrix, box->y2));
    glVertex2i (box->x2, box->y2);
    glTexCoord2f (COMP_TEX_COORD_X (matrix, box->x2),
		  COMP_TEX_COORD_Y (matrix, box->y1));
    glVertex2i (box->x2, box->y1);
    glTexCoord2f (COMP_TEX_COORD_X (matrix, box->x1),
		  COMP_TEX_COORD_Y (matrix, box->y1));
    glVertex2i (box->x1, box->y1);
}

static void
wallDrawCairoTextureOnScreen (CompScreen *s)
{
    float      centerX, centerY;
    float      width, height;
    float      topLeftX, topLeftY;
    float      border;
    int        i, j;
    CompMatrix matrix;
    BOX        box;

    WALL_SCREEN(s);

    glDisableClientState (GL_TEXTURE_COORD_ARRAY);
    glEnable (GL_BLEND);

    centerX = s->outputDev[ws->boxOutputDevice].region.extents.x1 +
	      (s->outputDev[ws->boxOutputDevice].width / 2.0f);
    centerY = s->outputDev[ws->boxOutputDevice].region.extents.y1 +
	      (s->outputDev[ws->boxOutputDevice].height / 2.0f);

    border = (float) ws->viewportBorder;
    width  = (float) ws->switcherContext.width;
    height = (float) ws->switcherContext.height;

    topLeftX = centerX - floor (width / 2.0f);
    topLeftY = centerY - floor (height / 2.0f);

    ws->firstViewportX = topLeftX + border;
    ws->firstViewportY = topLeftY + border;

    if (!ws->moving)
    {
	double left;
	left = (float)ws->boxTimeout /
	       (wallGetPreviewTimeout (s->display) * 1000.0f);

	if (left < 0)
    	    left = 0.0f;
	else if (left > 0.5)
	    left = 1.0f;
	else
	    left = 2 * left;

	screenTexEnvMode (s, GL_MODULATE);

	glColor4f (left, left, left, left);
	glTranslatef (0.0f,0.0f, -(1 - left));

	ws->mSzCamera = -(1 - left);
    }
    else
	ws->mSzCamera = 0.0f;

    /* draw background */

    matrix = ws->switcherContext.texture.matrix;
    matrix.x0 -= topLeftX * matrix.xx;
    matrix.y0 -= topLeftY * matrix.yy;

    box.x1 = topLeftX;
    box.x2 = box.x1 + width;
    box.y1 = topLeftY;
    box.y2 = box.y1 + height;

    enableTexture (s, &ws->switcherContext.texture, COMP_TEXTURE_FILTER_FAST);
    glBegin (GL_QUADS);
    wallDrawQuad (&matrix, &box);
    glEnd ();
    disableTexture (s, &ws->switcherContext.texture);

    /* draw thumb */
    width = (float) ws->thumbContext.width;
    height = (float) ws->thumbContext.height;

    enableTexture (s, &ws->thumbContext.texture, COMP_TEXTURE_FILTER_FAST);
    glBegin (GL_QUADS);
    for (i = 0; i < s->hsize; i++)
    {
	for (j = 0; j < s->vsize; j++)
	{
	    if (i == ws->gotoX && j == ws->gotoY && ws->moving)
		continue;

	    box.x1 = i * (width + border);
	    box.x1 += topLeftX + border;
    	    box.x2 = box.x1 + width;
	    box.y1 = j * (height + border);
	    box.y1 += topLeftY + border;
	    box.y2 = box.y1 + height;

	    matrix = ws->thumbContext.texture.matrix;
	    matrix.x0 -= box.x1 * matrix.xx;
	    matrix.y0 -= box.y1 * matrix.yy;

	    wallDrawQuad (&matrix, &box);
	}
    }
    glEnd ();
    disableTexture (s, &ws->thumbContext.texture);

    if (ws->moving)
    {
	/* draw highlight */
	float angle = 0.0f;
	float dx, dy;
	int   aW, aH;

	box.x1 = s->x * (width + border) + topLeftX + border;
	box.x2 = box.x1 + width;
	box.y1 = s->y * (height + border) + topLeftY + border;
	box.y2 = box.y1 + height;

	matrix = ws->highlightContext.texture.matrix;
	matrix.x0 -= box.x1 * matrix.xx;
	matrix.y0 -= box.y1 * matrix.yy;

	enableTexture (s, &ws->highlightContext.texture,
		       COMP_TEXTURE_FILTER_FAST);
	glBegin (GL_QUADS);
	wallDrawQuad (&matrix, &box);
	glEnd ();
	disableTexture (s, &ws->highlightContext.texture);

	/* draw arrow */
	enableTexture (s, &ws->arrowContext.texture, COMP_TEXTURE_FILTER_GOOD);

	dx = ws->gotoX - ws->curPosX;
	dy = ws->gotoY - ws->curPosY;

	if (dx > 0 && dy == 0)
	    angle = 90.0f;
	else if (dx < 0 && dy == 0)
	    angle = 270.0f;
	else if (dy > 0)
	{
	    angle = 180.0f;

	    if (dx < 0)
		angle += 45.0f;
	    else if (dx > 0)
		angle -= 45.0f;
	}
	else if (dy < 0)
	{
	    if (dx < 0)
		angle = -45.0f;
	    else if (dx > 0)
		angle = 45.0f;
	}

	aW = ws->arrowContext.width;
	aH = ws->arrowContext.height;

	/* if we have a viewport preview we just paint the
	   arrow outside the switcher */
	if (wallGetMiniscreen (s->display))
	{
	    width  = (float) ws->switcherContext.width;
	    height = (float) ws->switcherContext.height;

	    switch ((int)angle)
	    {
	    /* top left */
	    case -45:
		box.x1 = topLeftX - aW - border;
		box.y1 = topLeftY - aH - border;
		break;
	    /* up */
    	    case 0:
		box.x1 = topLeftX + width / 2.0f - aW / 2.0f;
		box.y1 = topLeftY - aH - border;
		break;
	    /* top right */
	    case 45:
		box.x1 = topLeftX + width + border;
		box.y1 = topLeftY - aH - border;
		break;
	    /* right */
	    case 90:
		box.x1 = topLeftX + width + border;
		box.y1 = topLeftY + height / 2.0f - aH / 2.0f;
		break;
	    /* bottom right */
	    case 135:
		box.x1 = topLeftX + width + border;
		box.y1 = topLeftY + height + border;
		break;
	    /* down */
	    case 180:
		box.x1 = topLeftX + width / 2.0f - aW / 2.0f;
		box.y1 = topLeftY + height + border;
		break;
	    /* bottom left */
	    case 225:
		box.x1 = topLeftX - aW - border;
		box.y1 = topLeftY + height + border;
		break;
	    /* left */
	    case 270:
		box.x1 = topLeftX - aW - border;
		box.y1 = topLeftY + height / 2.0f - aH / 2.0f;
		break;
	    default:
		break;
	    }
	}
	else
	{
	    /* arrow is visible (no preview is painted over it) */
	    box.x1 = s->x * (width + border) + topLeftX + border;
	    box.x1 += width / 2 - aW / 2;
	    box.y1 = s->y * (height + border) + topLeftY + border;
	    box.y1 += height / 2 - aH / 2;
	}

	box.x2 = box.x1 + aW;
	box.y2 = box.y1 + aH;

	glTranslatef (box.x1 + aW / 2, box.y1 + aH / 2, 0.0f);
	glRotatef (angle, 0.0f, 0.0f, 1.0f);
	glTranslatef (-box.x1 - aW / 2, -box.y1 - aH / 2, 0.0f);

	matrix = ws->arrowContext.texture.matrix;
	matrix.x0 -= box.x1 * matrix.xx;
	matrix.y0 -= box.y1 * matrix.yy;

	glBegin (GL_QUADS);
	wallDrawQuad (&matrix, &box);
	glEnd ();

	disableTexture (s, &ws->arrowContext.texture);
    }

    glDisable (GL_BLEND);
    glEnableClientState (GL_TEXTURE_COORD_ARRAY);
    screenTexEnvMode (s, GL_REPLACE);
    glColor4usv (defaultColor);
}

static void
wallPaintScreen (CompScreen   *s,
		 CompOutput   *outputs,
     		 int          numOutputs,
		 unsigned int mask)
{
    WALL_SCREEN (s);

    if (ws->moving && numOutputs > 1 && wallGetMmmode(s) == MmmodeSwitchAll)
    {
	outputs = &s->fullscreenOutput;
	numOutputs = 1;
    }

    UNWRAP (ws, s, paintScreen);
    (*s->paintScreen) (s, outputs, numOutputs, mask);
    WRAP (ws, s, paintScreen, wallPaintScreen);
}

static Bool
wallPaintOutput (CompScreen              *s,
		 const ScreenPaintAttrib *sAttrib,
		 const CompTransform     *transform,
		 Region                  region,
		 CompOutput              *output,
		 unsigned int            mask)
{
    Bool status;

    WALL_SCREEN (s);

    ws->miniScreen = FALSE;
    if (ws->moving)
	mask |= PAINT_SCREEN_TRANSFORMED_MASK;

    UNWRAP (ws, s, paintOutput);
    status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
    WRAP (ws, s, paintOutput, wallPaintOutput);

    if ((ws->moving || ws->boxTimeout) && wallGetShowSwitcher(s->display) &&
	(output->id == ws->boxOutputDevice || output == &s->fullscreenOutput))
    {
	CompTransform sTransform = *transform;

	transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);

	glPushMatrix ();
	glLoadMatrixf (sTransform.m);

	wallDrawCairoTextureOnScreen (s);

	glPopMatrix ();

	if (wallGetMiniscreen (s->display))
	{
	    int i, j;

	    for (j = 0; j < s->vsize; j++)
    	    {
		for (i = 0; i < s->hsize; i++)
		{
		    float mx, my, mw, mh;
		    unsigned int msMask;

		    ws->miniScreen = TRUE;
		    mx = ws->firstViewportX +
			 (i * (ws->viewportWidth + ws->viewportBorder));
    		    my = ws->firstViewportY +
			 (j * (ws->viewportHeight + ws->viewportBorder));
    		    mw = ws->viewportWidth;
		    mh = ws->viewportHeight;

		    ws->mSAttribs.xTranslate = mx / output->width;
		    ws->mSAttribs.yTranslate = -my / output->height;

		    ws->mSAttribs.xScale = mw / s->width;
    		    ws->mSAttribs.yScale = mh / s->height;
		    ws->mSAttribs.opacity = OPAQUE * (1.0 + ws->mSzCamera);
		    ws->mSAttribs.brightness = 0.4f * BRIGHT;
		    ws->mSAttribs.saturation = COLOR;

		    if (i == s->x && j == s->y && ws->moving)
			ws->mSAttribs.brightness = BRIGHT;

		    if (ws->boxTimeout && !ws->moving &&
			i == s->x && j == s->y)
		    {
			ws->mSAttribs.brightness = BRIGHT;
		    }

		    setWindowPaintOffset (s, (s->x - i) * s->width,
					  (s->y - j) * s->height);

		    msMask = mask | PAINT_SCREEN_TRANSFORMED_MASK;
		    (*s->paintTransformedOutput) (s, sAttrib, transform,
						  region, output, msMask);

		    ws->miniScreen = FALSE;

		}
	    }
	    setWindowPaintOffset (s, 0, 0);
	}
    }

    return status;
}

static void
wallPreparePaintScreen (CompScreen *s,
			int        msSinceLastPaint)
{
    WALL_SCREEN (s);

    if (!ws->moving && !ws->showPreview && ws->boxTimeout)
	ws->boxTimeout -= msSinceLastPaint;

    if (ws->timer)
	ws->timer -= msSinceLastPaint;

    if (ws->moving)
    {
	wallComputeTranslation (s, &ws->curPosX, &ws->curPosY);

	if (ws->moveWindow)
	{
	    CompWindow *w;

	    w = findWindowAtScreen (s, ws->moveWindow);
	    if (w)
    	    {
		float dx, dy;

		dx = ws->gotoX - ws->curPosX;
		dy = ws->gotoY - ws->curPosY;

		moveWindowToViewportPosition (w,
					      ws->moveWindowX - s->width * dx,
					      ws->moveWindowY - s->height * dy,
					      TRUE);
	    }
	}
    }

    if (ws->moving && ws->curPosX == ws->gotoX && ws->curPosY == ws->gotoY)
    {
	ws->moving = FALSE;
	ws->timer  = 0;

	if (ws->moveWindow)
	    wallReleaseMoveWindow (s);
	else
	    focusDefaultWindow (s);

	if (ws->grabIndex)
	{
	    removeScreenGrab (s, ws->grabIndex, NULL);
	    ws->grabIndex = 0;
	}
    }

    UNWRAP (ws, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (ws, s, preparePaintScreen, wallPreparePaintScreen);
}

static void
wallPaintTransformedOutput (CompScreen              *s,
	     		    const ScreenPaintAttrib *sAttrib,
			    const CompTransform     *transform,
			    Region                  region,
			    CompOutput              *output,
			    unsigned int            mask)
{
    WALL_SCREEN (s);

    if (ws->miniScreen)
    {
	CompTransform sTransform = *transform;

	/* move each screen to the correct output position */
	matrixTranslate (&sTransform,
			 -output->region.extents.x1 / output->width,
			 output->region.extents.y1 / output->height, 0.0f);
	matrixTranslate (&sTransform, 0.0f, 0.0f, -DEFAULT_Z_CAMERA);

	matrixTranslate (&sTransform,
			 ws->mSAttribs.xTranslate,
			 ws->mSAttribs.yTranslate,
			 ws->mSzCamera);

	/* move origin to top left */
	matrixTranslate (&sTransform, -0.5f, 0.5f, 0.0f);
	matrixScale (&sTransform,
		     ws->mSAttribs.xScale, ws->mSAttribs.yScale, 1.0);

	/* revert prepareXCoords region shift.
	   Now all screens display the same */
	matrixTranslate (&sTransform, 0.5f, 0.5f, DEFAULT_Z_CAMERA);
	matrixTranslate (&sTransform,
			 output->region.extents.x1 / output->width,
			 -output->region.extents.y2 / output->height, 0.0f);

	UNWRAP (ws, s, paintTransformedOutput);
	(*s->paintTransformedOutput) (s, sAttrib, &sTransform,
				      &s->region, output, mask);
	WRAP (ws, s, paintTransformedOutput, wallPaintTransformedOutput);
	return;
    }

    UNWRAP (ws, s, paintTransformedOutput);

    if (!ws->moving)
	(*s->paintTransformedOutput) (s, sAttrib, transform,
				      region, output, mask);

    mask &= ~PAINT_SCREEN_CLEAR_MASK;

    if (ws->moving)
    {
	CompTransform sTransform = *transform;
	float         xTranslate, yTranslate;
	float         px, py;
	int           tx, ty;

	clearTargetOutput (s->display, GL_COLOR_BUFFER_BIT);

	px = ws->curPosX;
	py = ws->curPosY;

	if (floor (py) != ceil (py))
	{
	    ty = ceil (py) - s->y;
	    yTranslate = fmod (py, 1) - 1;

	    matrixTranslate (&sTransform, 0.0f, yTranslate, 0.0f);

	    if (floor (px) != ceil (px))
	    {
		tx = ceil (px) - s->x;
		xTranslate = 1 - fmod (px, 1);

		setWindowPaintOffset (s, (s->x - ceil(px)) * s->width,
				      (s->y - ceil(py)) * s->height);
		
		matrixTranslate (&sTransform, xTranslate, 0.0f, 0.0f);

		(*s->paintTransformedOutput) (s, sAttrib, &sTransform,
					      &output->region, output, mask);

		matrixTranslate (&sTransform, -xTranslate, 0.0f, 0.0f);
	    }

	    tx = floor (px) - s->x;
	    xTranslate = -fmod (px, 1);

	    setWindowPaintOffset (s, (s->x - floor(px)) * s->width,
				  (s->y - ceil(py)) * s->height);

	    matrixTranslate (&sTransform, xTranslate, 0.0f, 0.0f);

	    (*s->paintTransformedOutput) (s, sAttrib, &sTransform,
					  &output->region, output, mask);
	    matrixTranslate (&sTransform, -xTranslate, -yTranslate, 0.0f);
	}

	ty = floor (py) - s->y;
	yTranslate = fmod (py, 1);

	matrixTranslate (&sTransform, 0.0f, yTranslate, 0.0f);

	if (floor (px) != ceil (px))
	{
	    tx = ceil (px) - s->x;
	    xTranslate = 1 - fmod (px, 1);

	    setWindowPaintOffset (s, (s->x - ceil(px)) * s->width,
				  (s->y - floor(py)) * s->height);

	    matrixTranslate (&sTransform, xTranslate, 0.0f, 0.0f);

	    (*s->paintTransformedOutput) (s, sAttrib, &sTransform,
					  &output->region, output, mask);

	    matrixTranslate (&sTransform, -xTranslate, 0.0f, 0.0f);
	}

	tx = floor (px) - s->x;
	xTranslate = -fmod (px, 1);

	setWindowPaintOffset (s, (s->x - floor(px)) * s->width,
			      (s->y - floor(py)) * s->height);
	
	matrixTranslate (&sTransform, xTranslate, 0.0f, 0.0f);
	(*s->paintTransformedOutput) (s, sAttrib, &sTransform,
				      &output->region, output, mask);

	setWindowPaintOffset (s, 0, 0);
    }

    WRAP (ws, s, paintTransformedOutput, wallPaintTransformedOutput);
}

static Bool
wallPaintWindow (CompWindow              *w,
		 const WindowPaintAttrib *attrib,
		 const CompTransform     *transform,
		 Region                  region,
		 unsigned int            mask)
{
    Bool       status;
    CompScreen *s = w->screen;

    WALL_SCREEN (s);

    if (ws->miniScreen)
    {
	WindowPaintAttrib pA = *attrib;

	pA.opacity = attrib->opacity * ((float)ws->mSAttribs.opacity / OPAQUE);
	pA.brightness = attrib->brightness *
	                ((float)ws->mSAttribs.brightness / BRIGHT);
	pA.saturation = attrib->saturation *
			((float)ws->mSAttribs.saturation / COLOR);

	if (!pA.opacity || !pA.brightness)
	    mask |= PAINT_WINDOW_NO_CORE_INSTANCE_MASK;

	UNWRAP (ws, s, paintWindow);
	status = (*s->paintWindow) (w, &pA, transform, region, mask);
    	WRAP (ws, s, paintWindow, wallPaintWindow);
    }
    else
    {
	UNWRAP (ws, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
	WRAP (ws, s, paintWindow, wallPaintWindow);
    }

    return status;
}

static void
wallDonePaintScreen (CompScreen *s)
{
    WALL_SCREEN (s);

    if (ws->moving || ws->boxTimeout)
	damageScreen (s);

    if (ws->boxTimeout < 0)
    {
	ws->boxTimeout = 0;
	damageScreen (s);
    }

    UNWRAP (ws, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (ws, s, donePaintScreen, wallDonePaintScreen);

}

static void
wallCreateCairoContexts (CompScreen *s,
			 Bool       initial)
{
    int width, height;

    WALL_SCREEN (s);

    ws->viewportWidth = VIEWPORT_SWITCHER_SIZE * (float)wallGetPreviewScale(s->display) / 100.0f;
    ws->viewportHeight = ws->viewportWidth * (float)s->height / (float)s->width;
    ws->viewportBorder = wallGetBorderWidth(s->display);
    width = s->hsize * (ws->viewportWidth + ws->viewportBorder) + ws->viewportBorder;
    height = s->vsize * (ws->viewportHeight + ws->viewportBorder) + ws->viewportBorder;

    wallDestroyCairoContext (s, &ws->switcherContext);
    ws->switcherContext.width = width;
    ws->switcherContext.height = height;
    wallSetupCairoContext (s, &ws->switcherContext);
    wallDrawSwitcherBackground (s);

    wallDestroyCairoContext (s, &ws->thumbContext);
    ws->thumbContext.width = ws->viewportWidth;
    ws->thumbContext.height = ws->viewportHeight;
    wallSetupCairoContext (s, &ws->thumbContext);
    wallDrawThumb (s);

    wallDestroyCairoContext (s, &ws->highlightContext);
    ws->highlightContext.width = ws->viewportWidth;
    ws->highlightContext.height = ws->viewportHeight;
    wallSetupCairoContext (s, &ws->highlightContext);
    wallDrawHighlight (s);

    if (initial)
    {
	ws->arrowContext.width = ARROW_SIZE;
	ws->arrowContext.height = ARROW_SIZE;
	wallSetupCairoContext (s, &ws->arrowContext);
	wallDrawArrow (s);
    }
}

static void
wallDisplayOptionChanged (CompDisplay        *display,
			  CompOption         *opt,
			  WallDisplayOptions num)
{
    CompScreen *s;

    switch(num)
    {
    case WallDisplayOptionOutlineColor:
	for (s = display->screens; s; s = s->next)
	{
	    wallDrawSwitcherBackground (s);
	    wallDrawHighlight (s);
	    wallDrawThumb (s);
	}
	break;

    case WallDisplayOptionEdgeRadius:
    case WallDisplayOptionBackgroundGradientBaseColor:
    case WallDisplayOptionBackgroundGradientHighlightColor:
    case WallDisplayOptionBackgroundGradientShadowColor:
	for (s = display->screens; s; s = s->next)
	    wallDrawSwitcherBackground (s);
	break;

    case WallDisplayOptionBorderWidth:
    case WallDisplayOptionPreviewScale:
	for (s = display->screens; s; s = s->next)
	    wallCreateCairoContexts (s, FALSE);
	break;

    case WallDisplayOptionThumbGradientBaseColor:
    case WallDisplayOptionThumbGradientHighlightColor:
	for (s = display->screens; s; s = s->next)
	    wallDrawThumb (s);
	break;

    case WallDisplayOptionThumbHighlightGradientBaseColor:
    case WallDisplayOptionThumbHighlightGradientShadowColor:
	for (s = display->screens; s; s = s->next)
	    wallDrawHighlight (s);
	break;

    case WallDisplayOptionArrowBaseColor:
    case WallDisplayOptionArrowShadowColor:
	for (s = display->screens; s; s = s->next)
	    wallDrawArrow (s);
	break;
    default:
	break;
    }
}

static Bool
wallSetOptionForPlugin (CompObject      *o,
			const char      *plugin,
			const char      *name,
			CompOptionValue *value)
{
    Bool status;

    WALL_CORE (&core);

    UNWRAP (wc, &core, setOptionForPlugin);
    status = (*core.setOptionForPlugin) (o, plugin, name, value);
    WRAP (wc, &core, setOptionForPlugin, wallSetOptionForPlugin);

    if (status && o->type == COMP_OBJECT_TYPE_SCREEN)
    {
	if (strcmp (plugin, "core") == 0)
	    if (strcmp (name, "hsize") == 0 || strcmp (name, "vsize") == 0)
	    {
		CompScreen *s = (CompScreen *) o;
		
		wallCreateCairoContexts (s, FALSE);
	    }
    }

    return status;
}

static Bool
wallInitCore (CompPlugin *p,
              CompCore   *c)
{
    WallCore *wc;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
        return FALSE;

    wc = malloc (sizeof (WallCore));
    if (!wc)
        return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
    {
        free (wc);
        return FALSE;
    }

    WRAP (wc, &core, setOptionForPlugin, wallSetOptionForPlugin);

    c->base.privates[corePrivateIndex].ptr = wc;

    return TRUE;
}

static void
wallFiniCore (CompPlugin *p,
              CompCore   *c)
{
    WALL_CORE (c);

    UNWRAP (wc, &core, setOptionForPlugin);

    freeDisplayPrivateIndex (displayPrivateIndex);

    free (wc);
}

static Bool
wallInitDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    WallDisplay *wd;

    wd = malloc (sizeof (WallDisplay));
    if (!wd)
	return FALSE;

    wd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (wd->screenPrivateIndex < 0)
    {
	free (wd);
 	return FALSE;
    }

    wallSetLeftKeyInitiate (d, wallLeft);
    wallSetLeftKeyTerminate (d, wallTerminate);
    wallSetRightKeyInitiate (d, wallRight);
    wallSetRightKeyTerminate (d, wallTerminate);
    wallSetUpKeyInitiate (d, wallUp);
    wallSetUpKeyTerminate (d, wallTerminate);
    wallSetDownKeyInitiate (d, wallDown);
    wallSetDownKeyTerminate (d, wallTerminate);
    wallSetNextKeyInitiate (d, wallNext);
    wallSetNextKeyTerminate (d, wallTerminate);
    wallSetPrevKeyInitiate (d, wallPrev);
    wallSetPrevKeyTerminate (d, wallTerminate);
    wallSetLeftButtonInitiate (d, wallLeft);
    wallSetLeftButtonTerminate (d, wallTerminate);
    wallSetRightButtonInitiate (d, wallRight);
    wallSetRightButtonTerminate (d, wallTerminate);
    wallSetUpButtonInitiate (d, wallUp);
    wallSetUpButtonTerminate (d, wallTerminate);
    wallSetDownButtonInitiate (d, wallDown);
    wallSetDownButtonTerminate (d, wallTerminate);
    wallSetNextButtonInitiate (d, wallNext);
    wallSetNextButtonTerminate (d, wallTerminate);
    wallSetPrevButtonInitiate (d, wallPrev);
    wallSetPrevButtonTerminate (d, wallTerminate);
    wallSetLeftWindowKeyInitiate (d, wallLeftWithWindow);
    wallSetLeftWindowKeyTerminate (d, wallTerminate);
    wallSetRightWindowKeyInitiate (d, wallRightWithWindow);
    wallSetRightWindowKeyTerminate (d, wallTerminate);
    wallSetUpWindowKeyInitiate (d, wallUpWithWindow);
    wallSetUpWindowKeyTerminate (d, wallTerminate);
    wallSetDownWindowKeyInitiate (d, wallDownWithWindow);
    wallSetDownWindowKeyTerminate (d, wallTerminate);
    wallSetFlipLeftEdgeInitiate (d, wallFlipLeft);
    wallSetFlipRightEdgeInitiate (d, wallFlipRight);
    wallSetFlipUpEdgeInitiate (d, wallFlipUp);
    wallSetFlipDownEdgeInitiate (d, wallFlipDown);

    wallSetEdgeRadiusNotify (d, wallDisplayOptionChanged);
    wallSetBorderWidthNotify (d, wallDisplayOptionChanged);
    wallSetPreviewScaleNotify (d, wallDisplayOptionChanged);
    wallSetOutlineColorNotify (d, wallDisplayOptionChanged);
    wallSetBackgroundGradientBaseColorNotify (d, wallDisplayOptionChanged);
    wallSetBackgroundGradientHighlightColorNotify (d, wallDisplayOptionChanged);
    wallSetBackgroundGradientShadowColorNotify (d, wallDisplayOptionChanged);
    wallSetThumbGradientBaseColorNotify (d, wallDisplayOptionChanged);
    wallSetThumbGradientHighlightColorNotify (d, wallDisplayOptionChanged);
    wallSetThumbHighlightGradientBaseColorNotify (d, wallDisplayOptionChanged);
    wallSetThumbHighlightGradientShadowColorNotify (d,
						    wallDisplayOptionChanged);
    wallSetArrowBaseColorNotify (d, wallDisplayOptionChanged);
    wallSetArrowShadowColorNotify (d, wallDisplayOptionChanged);

    WRAP (wd, d, handleEvent, wallHandleEvent);
    d->base.privates[displayPrivateIndex].ptr = wd;

    return TRUE;
}

static void
wallFiniDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    WALL_DISPLAY (d);

    UNWRAP (wd, d, handleEvent);

    freeScreenPrivateIndex (d, wd->screenPrivateIndex);
    free (wd);
}

static Bool
wallInitScreen (CompPlugin *p,
		CompScreen *s)
{
    WallScreen *ws;

    WALL_DISPLAY (s->display);

    ws = malloc (sizeof (WallScreen));
    if (!ws)
	return FALSE;

    ws->boxTimeout = 0;
    ws->grabIndex = 0;
    ws->moving = FALSE;
    ws->showPreview = FALSE;

    memset (&ws->switcherContext, 0, sizeof (WallCairoContext));
    memset (&ws->thumbContext, 0, sizeof (WallCairoContext));
    memset (&ws->highlightContext, 0, sizeof (WallCairoContext));
    memset (&ws->arrowContext, 0, sizeof (WallCairoContext));

    WRAP (ws, s, paintScreen, wallPaintScreen);
    WRAP (ws, s, paintOutput, wallPaintOutput);
    WRAP (ws, s, donePaintScreen, wallDonePaintScreen);
    WRAP (ws, s, paintTransformedOutput, wallPaintTransformedOutput);
    WRAP (ws, s, preparePaintScreen, wallPreparePaintScreen);
    WRAP (ws, s, paintWindow, wallPaintWindow);
    WRAP (ws, s, activateWindow, wallActivateWindow);

    s->base.privates[wd->screenPrivateIndex].ptr = ws;

    wallCreateCairoContexts (s, TRUE);

    return TRUE;
}

static void
wallFiniScreen (CompPlugin *p,
		CompScreen *s)
{
    WALL_SCREEN (s);

    wallDestroyCairoContext (s, &ws->switcherContext);
    wallDestroyCairoContext (s, &ws->thumbContext);
    wallDestroyCairoContext (s, &ws->highlightContext);
    wallDestroyCairoContext (s, &ws->arrowContext);

    UNWRAP (ws, s, paintScreen);
    UNWRAP (ws, s, paintOutput);
    UNWRAP (ws, s, donePaintScreen);
    UNWRAP (ws, s, paintTransformedOutput);
    UNWRAP (ws, s, preparePaintScreen);
    UNWRAP (ws, s, paintWindow);
    UNWRAP (ws, s, activateWindow);

    free(ws);
}

static CompBool
wallInitObject (CompPlugin *p,
		CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) wallInitCore,
	(InitPluginObjectProc) wallInitDisplay,
	(InitPluginObjectProc) wallInitScreen
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
wallFiniObject (CompPlugin *p,
		CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) wallFiniCore,
	(FiniPluginObjectProc) wallFiniDisplay,
	(FiniPluginObjectProc) wallFiniScreen
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
wallInit (CompPlugin *p)
{
    corePrivateIndex = allocateCorePrivateIndex ();
    if (corePrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
wallFini (CompPlugin *p)
{
    freeCorePrivateIndex (corePrivateIndex);
}

CompPluginVTable wallVTable = {
    "wall",
    0,
    wallInit,
    wallFini,
    wallInitObject,
    wallFiniObject,
    0,
    0
};

CompPluginVTable*
getCompPluginInfo (void)
{
    return &wallVTable;
}
