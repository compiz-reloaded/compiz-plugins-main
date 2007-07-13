/**
 * Beryl Opacify 
 *
 * Copyright (c) 2006 Kristian Lyngstøl <kristian@beryl-project.org>
 * Ported to Compiz and BCOP usage by Danny Baumann <maniac@beryl-project.org>
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
 * Opacify increases opacity on targeted windows and reduces it on
 * blocking windows, making whatever window you are targeting easily
 * visible. 
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>

#include <compiz.h>
#include "opacify_options.h"

#define GET_OPACIFY_DISPLAY(d)                            \
	((OpacifyDisplay *) (d)->privates[displayPrivateIndex].ptr)
#define OPACIFY_DISPLAY(d)                                \
	OpacifyDisplay *od = GET_OPACIFY_DISPLAY (d)
#define GET_OPACIFY_SCREEN(s, od)                         \
	((OpacifyScreen *) (s)->privates[(od)->screenPrivateIndex].ptr)
#define OPACIFY_SCREEN(s)                                 \
	OpacifyScreen *os = GET_OPACIFY_SCREEN (s, GET_OPACIFY_DISPLAY (s->display))
#define GET_OPACIFY_WINDOW(w, os)                         \
	((OpacifyWindow *) (w)->privates[(os)->windowPrivateIndex].ptr)
#define OPACIFY_WINDOW(s)                                 \
	OpacifyWindow *ow = GET_OPACIFY_WINDOW(w, GET_OPACIFY_SCREEN (w->screen, GET_OPACIFY_DISPLAY (w->screen->display)))

/* Size of the Window array storing passive windows. */
#define MAX_WINDOWS 64

static int displayPrivateIndex = 0;

typedef struct _OpacifyDisplay
{
	int screenPrivateIndex;
	HandleEventProc handleEvent;
	Bool toggle;
	int active_screen;
	CompTimeoutHandle timeout_handle;
} OpacifyDisplay;

typedef struct _OpacifyScreen
{
	int windowPrivateIndex;
	PaintWindowProc paintWindow;

	CompWindow *new_active;
	Window active;
	Window passive[MAX_WINDOWS];
	Region intersect;
	unsigned short int passive_num;
	Bool just_moved;
} OpacifyScreen;

typedef struct _OpacifyWindow
{
	Bool opacified;
	int opacity;
} OpacifyWindow;

/* Core opacify functions. These do the real work. ---------------------*/

/* Sets the real opacity and damages the window if actual opacity and 
 * requested opacity differs. */
static void set_opacity(CompWindow * w, int opacity)
{
	OPACIFY_WINDOW(w);
	
	if (!ow->opacified || (w->paint.opacity != opacity))
		addWindowDamage(w);

	ow->opacified = TRUE;
	ow->opacity = opacity;

	//setWindowOpacity(w, opacity, PL_TEMP_HELLO);
}

/* Resets the Window to the original opacity if it still exists.
 */
static void reset_opacity(CompScreen * s, Window id)
{
	CompWindow *w;

	w = findWindowAtScreen(s, id);
	if (!w)
		return;

	OPACIFY_WINDOW(w);
	ow->opacified = FALSE;
	addWindowDamage(w);
	//resetWindowOpacity(w, PL_TEMP_HELLO);
}

/* Resets the opacity of windows on the passive list.
 */
static void clear_passive(CompScreen * s)
{
	OPACIFY_SCREEN(s);
	int i;

	for (i = 0; i < os->passive_num; i++)
		reset_opacity(s, os->passive[i]);
	os->passive_num = 0;
}

/* Dim an (inactive) window. Place it on the passive list and
 * update passive_num. Then change the opacity.
 */
static void dim_window(CompScreen * s, CompWindow * w)
{
	OPACIFY_SCREEN(s);
	if (os->passive_num >= MAX_WINDOWS - 1)
	{
		compLogMessage (s->display, "opacify", CompLogLevelWarn,
						"Trying to store information "
						"about too many windows, or you hit a bug.\nIf "
						"you don't have around %d windows blocking the "
						"currently targeted window, please report this.",
						MAX_WINDOWS);
		return;
	}
	os->passive[os->passive_num++] = w->id;
	set_opacity(w, MIN(OPAQUE * opacifyGetPassiveOpacity(s) / 100, w->paint.opacity));
}

/* Walk through all windows, skip until we've passed the active
 * window, skip if it's invisible, hidden or minimized, skip if
 * it's not a window type we're looking for. 
 * Dim it if it intersects. 
 *
 * Returns number of changed windows.
 */
static int passive_windows(CompScreen * s, Region a_region)
{
	CompWindow *w;

	OPACIFY_SCREEN(s);
	Bool flag = FALSE;
	int i = 0;

	for (w = s->windows; w; w = w->next)
	{
		if (w->id == os->active)
		{
			flag = TRUE;
			continue;
		}
		if (!flag)
			continue;
		if (!matchEval(opacifyGetWindowMatch(s), w))
			continue;
		if (w->invisible || w->hidden || w->minimized)
			continue;
		XIntersectRegion(w->region, a_region, os->intersect);
		if (!XEmptyRegion(os->intersect))
		{
			dim_window(s, w);
			i++;
		}
	}
	return i;
}

/* Check if we switched active window, reset the old passive windows
 * if we did. If we have an active window and switched: reset that too.
 * If we have a window (w is true), update the active id and
 * passive list. just_moved is to make sure we recalculate opacity after
 * moving. We can't reset before moving because if we're using a delay
 * and the window being moved is not the active but overlapping, it will
 * be reset, which would conflict with move's opacity change. 
 * FIXME: A more final solution should be to use IPCS to signal 
 * which window is being moved. 
 */
static void opacify_handle_enter(CompScreen * s, CompWindow * w)
{
	OPACIFY_SCREEN(s);
	int num;

	if (otherScreenGrabExist(s, 0))
	{
		if (!otherScreenGrabExist(s, "move", 0))
		{
			os->just_moved = True;
			return;
		}
		clear_passive(s);
		reset_opacity(s, os->active);
		os->active = 0;
		return;
	}
	if (!w || os->active != w->id || os->just_moved)
	{
		os->just_moved = False;
		clear_passive(s);
		reset_opacity(s, os->active);
		os->active = 0;
	}
	if (!w)
		return;
	if (w->id != os->active && !w->shaded &&
	    matchEval(opacifyGetWindowMatch(s), w))
	{
		os->active = w->id;
		num = passive_windows(s, w->region);
		if (num || opacifyGetOnlyIfBlock(s))
			set_opacity(w, MAX(OPAQUE * opacifyGetActiveOpacity(s) / 100, w->paint.opacity));
	}
}

/* Check if we are on the same screen. We only want opacify active on
 * one screen, so if we are on a diffrent screen, we reset the old one.
 * Returns True if the screen has switched.
 */
static Bool check_screen_switch(CompScreen *s)
{
	OPACIFY_DISPLAY(s->display);
	if (od->active_screen == s->screenNum)
		return False;
	CompScreen * tmp;
	for (tmp = s->display->screens; tmp; tmp = tmp->next)
	{
		OPACIFY_SCREEN(tmp);
		clear_passive(tmp);
		reset_opacity(tmp, os->active);
		os->active = 0;
	}
	od->active_screen = s->screenNum;
	return True;
}

/* Decides what to do after a timeout occured. 
 * Either we reset the opacity because we just toggled,
 * or we handle the event.
 */
static Bool handle_timeout(void *data)
{
	CompScreen *s = (CompScreen *) data;

	OPACIFY_SCREEN(s);
	OPACIFY_DISPLAY(s->display);

	od->timeout_handle = 0;
	check_screen_switch(s);
	if (!od->toggle)
	{
		clear_passive(s);
		reset_opacity(s, os->active);
		os->active = 0;
	}
	opacify_handle_enter(s, os->new_active);

	return FALSE;
}

/* Checks whether we should delay or not.
 * Returns true if immediate execution.
 */
static inline Bool check_delay(CompScreen *s)
{
	CompDisplay * d = s->display;
	OPACIFY_SCREEN(s);

	if (opacifyGetFocusInstant(s) && os->new_active && 
	    (os->new_active->id == d->activeWindow))
		 return True;
	if (!opacifyGetTimeout(d))
	     return True;
	if (!os->new_active || (os->new_active->id == s->root))
		return False;
	if (os->new_active->type & (CompWindowTypeDesktopMask | CompWindowTypeDockMask))
		return False;
	if (opacifyGetNoDelayChange(s) && os->passive_num)
		return True;
	return False;


}

static Bool opacifyPaintWindow (CompWindow *w, const WindowPaintAttrib *attrib,
				const CompTransform *transform, Region region,
				unsigned int mask)
{
	Bool status;
	CompScreen *s = w->screen;
	OPACIFY_SCREEN(s);
	OPACIFY_WINDOW(w);

	if (ow->opacified)
	{
		WindowPaintAttrib wAttrib = *attrib;
		wAttrib.opacity = ow->opacity;

		UNWRAP(os, s, paintWindow);
		status = (*s->paintWindow) (w, &wAttrib, transform, region, mask);
		WRAP(os, s, paintWindow, opacifyPaintWindow);
	}
	else
	{
		UNWRAP(os, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region, mask);
		WRAP(os, s, paintWindow, opacifyPaintWindow);
	}

	return status;
}

/* Takes the inital event. 
 * If we were configured, recalculate the opacify-windows if 
 * it was our window. 
 * If a window was entered: call upon handle_timeout after od->timeout 
 * micro seconds, or directly if od->timeout is 0 (no delay).
 *
 * FIXME: In the perfect world, toggle-resetting is done in the action
 * handler that does the actual toggling.
 */
static void opacifyHandleEvent(CompDisplay * d, XEvent * event)
{
	CompScreen *s;
	CompWindow *w = NULL;

	OPACIFY_DISPLAY(d);

	UNWRAP(od, d, handleEvent);
	(*d->handleEvent) (d, event);
	WRAP(od, d, handleEvent, opacifyHandleEvent);

	if (!od->toggle && !opacifyGetToggleReset(d))
		return;

	switch (event->type)
	{
	case EnterNotify:
		s = findScreenAtDisplay(d, event->xcrossing.root);
		if (s)
		{
			OPACIFY_SCREEN(s);
			if (!od->toggle && !os->active)
				return;
			os->new_active = findTopLevelWindowAtScreen(s, event->xcrossing.window);
			if (od->timeout_handle)
				compRemoveTimeout(od->timeout_handle);
			if (check_delay(s))
				handle_timeout(s);
			else
				od->timeout_handle =
						compAddTimeout(opacifyGetTimeout(d), handle_timeout, s);
		}
		break;
	case ConfigureNotify:
		s = findScreenAtDisplay(d, event->xconfigure.event);
		if (s)
		{
			OPACIFY_SCREEN(s);
			if (os->active != event->xconfigure.window)
				break;
			clear_passive(s);
			if (os->active)
				w = findWindowAtScreen(s, os->active);
			if (w)
				passive_windows(s, w->region);
		}
		break;
	default:
		break;
	}
}


/* Configuration, initialization, boring stuff. ----------------------- */

static Bool opacifyInitWindow(CompPlugin * p, CompWindow * w)
{
	OPACIFY_SCREEN(w->screen);
	OpacifyWindow *ow = (OpacifyWindow *) calloc(1, sizeof(OpacifyWindow));

	ow->opacified = FALSE;
	
	w->privates[os->windowPrivateIndex].ptr = ow;

	return TRUE;
}

static void opacifyFiniWindow(CompPlugin * p, CompWindow * w)
{
	OPACIFY_WINDOW(w);

	free(ow);
}

static void opacifyFiniScreen(CompPlugin * p, CompScreen * s)
{
	OPACIFY_SCREEN(s);

	UNWRAP(os, s, paintWindow);

	XDestroyRegion(os->intersect);
	free(os);
}

static Bool opacifyInitScreen(CompPlugin * p, CompScreen * s)
{
	OPACIFY_DISPLAY(s->display);
	OpacifyScreen *os = (OpacifyScreen *) calloc(1, sizeof(OpacifyScreen));

	os->windowPrivateIndex = allocateWindowPrivateIndex(s);
	if (os->windowPrivateIndex < 0)
	{
		free(os);
		return FALSE;
	}

	WRAP(os, s, paintWindow, opacifyPaintWindow);

	s->privates[od->screenPrivateIndex].ptr = os;
	os->intersect = XCreateRegion();
	os->just_moved = False;
	
	return TRUE;
}

static Bool opacify_toggle(CompDisplay * d, CompAction * ac,
						   CompActionState state, CompOption * option,
						   int nOption)
{
	OPACIFY_DISPLAY(d);
	od->toggle = !od->toggle;
	return TRUE;
}

static void opacifyDisplayOptionChanged(CompDisplay *d, CompOption *opt, OpacifyDisplayOptions num)
{
	OPACIFY_DISPLAY(d);
	
	switch (num)
	{
		case OpacifyDisplayOptionInitToggle:
			od->toggle = opt->value.b;
			break;
		default:
			break;
	}
}

static Bool opacifyInitDisplay(CompPlugin * p, CompDisplay * d)
{
	OpacifyDisplay *od = (OpacifyDisplay *) malloc(sizeof(OpacifyDisplay));

	if (!od)
	    return FALSE;

	od->screenPrivateIndex = allocateScreenPrivateIndex(d);
	if (od->screenPrivateIndex < 0)
	{
		free(od);
		return FALSE;
	}
	d->privates[displayPrivateIndex].ptr = od;
	od->active_screen = d->screens->screenNum;
	od->toggle = TRUE;

	opacifySetToggleInitiate (d, opacify_toggle);	
	opacifySetInitToggleNotify(d, opacifyDisplayOptionChanged);

	WRAP(od, d, handleEvent, opacifyHandleEvent);
	return TRUE;
}

static void opacifyFiniDisplay(CompPlugin * p, CompDisplay * d)
{
	OPACIFY_DISPLAY(d);
	UNWRAP(od, d, handleEvent);
	if (od->timeout_handle)
		compRemoveTimeout(od->timeout_handle);
	freeScreenPrivateIndex(d, od->screenPrivateIndex);
	free(od);
}

static Bool opacifyInit(CompPlugin * p)
{
	displayPrivateIndex = allocateDisplayPrivateIndex();
	if (displayPrivateIndex < 0)
		return FALSE;
	return TRUE;
}

static void opacifyFini(CompPlugin * p)
{
	if (displayPrivateIndex >= 0)
		freeDisplayPrivateIndex(displayPrivateIndex);
}

static int opacifyGetVersion(CompPlugin * p, int version)
{
	return ABIVERSION;
}

CompPluginVTable opacifyVTable = {
	"opacify",
	opacifyGetVersion,
	0,
	opacifyInit,
	opacifyFini,
	opacifyInitDisplay,
	opacifyFiniDisplay,
	opacifyInitScreen,
	opacifyFiniScreen,
	opacifyInitWindow,
	opacifyFiniWindow,
	0,
	0,
	0,
	0,
	NULL,
	0,
	0,
	0
};

CompPluginVTable *getCompPluginInfo(void)
{
	return &opacifyVTable;
}
