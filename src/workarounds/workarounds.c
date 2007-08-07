/*
 * Copyright (C) 2007 Andrew Riedi <andrewriedi@gmail.com>
 *
 * Sticky window handling by Dennis Kasprzyk <onestone@opencompositing.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * This plug-in for Metacity-like workarounds.
 */

#include <string.h>
#include <limits.h>

#include <compiz.h>
#include <X11/Xatom.h>
#include <workarounds_options.h>

static int displayPrivateIndex;

typedef struct _WorkaroundsDisplay {
    int screenPrivateIndex;

    HandleEventProc handleEvent;

    Atom roleAtom;
} WorkaroundsDisplay;

typedef struct _WorkaroundsScreen {
    int windowPrivateIndex;

    WindowResizeNotifyProc  windowResizeNotify;
} WorkaroundsScreen;

typedef struct _WorkaroundsWindow {
    Bool madeSticky;
} WorkaroundsWindow;

#define GET_WORKAROUNDS_DISPLAY(d) \
    ((WorkaroundsDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define WORKAROUNDS_DISPLAY(d) \
    WorkaroundsDisplay *wd = GET_WORKAROUNDS_DISPLAY (d)

#define GET_WORKAROUNDS_SCREEN(s, wd) \
    ((WorkaroundsScreen *) (s)->privates[(wd)->screenPrivateIndex].ptr)

#define WORKAROUNDS_SCREEN(s) \
    WorkaroundsScreen *ws = GET_WORKAROUNDS_SCREEN (s, \
                            GET_WORKAROUNDS_DISPLAY (s->display))

#define GET_WORKAROUNDS_WINDOW(w, ns) \
    ((WorkaroundsWindow *) (w)->privates[(ns)->windowPrivateIndex].ptr)
#define WORKAROUNDS_WINDOW(w) \
    WorkaroundsWindow *ww = GET_WORKAROUNDS_WINDOW  (w, \
		    GET_WORKAROUNDS_SCREEN  (w->screen, \
		    GET_WORKAROUNDS_DISPLAY (w->screen->display)))

static char *
workaroundsGetWindowRoleAtom (CompWindow *w)
{
    CompDisplay *d = w->screen->display;
    Atom type;
    unsigned long nItems;
    unsigned long bytesAfter;
    unsigned char *str = NULL;
    int format, result;
    char *retval;

    WORKAROUNDS_DISPLAY (d);

    result = XGetWindowProperty (d->display, w->id, wd->roleAtom,
                                 0, LONG_MAX, FALSE, XA_STRING,
                                 &type, &format, &nItems, &bytesAfter,
                                 (unsigned char **) &str);

    if (result != Success)
        return NULL;

    if (type != XA_STRING)
    {
        XFree (str);
        return NULL;
    }

    retval = strdup ((char *) str);

    XFree (str);

    return retval;
}

static void
workaroundsRemoveSticky (CompWindow *w)
{
    WORKAROUNDS_WINDOW (w);

    if (w->state & CompWindowStateStickyMask && ww->madeSticky)
	changeWindowState (w, w->state & ~CompWindowStateStickyMask);
    ww->madeSticky = FALSE;
}

static void
workaroundsUpdateSticky (CompWindow *w)
{
    WORKAROUNDS_WINDOW (w);

    CompDisplay *d  = w->screen->display;
    Bool makeSticky = FALSE;

    if (workaroundsGetStickyAlldesktops (d) && w->desktop == 0xffffffff &&
	matchEval (workaroundsGetAlldesktopStickyMatch (d), w))
	makeSticky = TRUE;

    if (makeSticky)
    {
	if (!(w->state & CompWindowStateStickyMask))
	{
	    ww->madeSticky = TRUE;
	    changeWindowState (w, w->state | CompWindowStateStickyMask);
	}
    }
    else
	workaroundsRemoveSticky (w);

}

static void
workaroundsDoFixes (CompWindow *w)
{
    Bool appliedFix = FALSE;

    w->wmType = getWindowType (w->screen->display, w->id);

    /* FIXME: Is this the best way to detect a notification type window? */
    if (workaroundsGetNotificationDaemonFix (w->screen->display) && w->resName)
    {
        if (w->wmType == CompWindowTypeNormalMask &&
            w->attrib.override_redirect &&
            strcmp (w->resName, "notification-daemon") == 0)
        {
            w->wmType = CompWindowTypeNotificationMask;
            appliedFix = TRUE;
        }
    }

    if (workaroundsGetFirefoxMenuFix (w->screen->display) && !appliedFix)
    {
        if (w->wmType == CompWindowTypeNormalMask &&
            w->attrib.override_redirect)
        {
            w->wmType = CompWindowTypeDropdownMenuMask;
            appliedFix = TRUE;
        }
    }

    /* FIXME: Basic hack to get Java windows working correctly. */
    if (workaroundsGetJavaFix (w->screen->display) && !appliedFix && w->resName)
    {
        if ((strcmp (w->resName, "sun-awt-X11-XMenuWindow") == 0) ||
            (strcmp (w->resName, "sun-awt-X11-XWindowPeer") == 0))
        {
            w->wmType = CompWindowTypeDropdownMenuMask;
            appliedFix = TRUE;
        }
        else if (strcmp (w->resName, "sun-awt-X11-XDialogPeer") == 0)
        {
            w->wmType = CompWindowTypeDialogMask;
            appliedFix = TRUE;
        }
        else if (strcmp (w->resName, "sun-awt-X11-XFramePeer") == 0)
        {
            w->wmType = CompWindowTypeNormalMask;
            appliedFix = TRUE;
        }
    }

    if (workaroundsGetQtFix (w->screen->display) && !appliedFix)
    {
        char *windowRole;

        /* fix tooltips */
        windowRole = workaroundsGetWindowRoleAtom (w);
        if (windowRole)
        {
            if ((strcmp (windowRole, "toolTipTip") == 0) ||
                (strcmp (windowRole, "qtooltip_label") == 0))
            {
                w->wmType = CompWindowTypeTooltipMask;
                appliedFix = TRUE;
            }

            free (windowRole);
        }

        /* fix Qt transients - FIXME: is there a better way to detect them?
           Especially we have to take care of windows which get a class name
           later on */
        if (!appliedFix)
        {
            if (!w->resName && w->attrib.override_redirect &&
                (w->wmType == CompWindowTypeUnknownMask))
            {
                w->wmType = CompWindowTypeDropdownMenuMask;
                appliedFix = TRUE;
            }
        }
    }

    recalcWindowType (w);

    if (workaroundsGetLegacyFullscreen (w->screen->display))
    {
        /* Some code to make Wine and legacy applications work. */
        if (w->width == w->screen->width && w->height == w->screen->height &&
            !(w->type & CompWindowTypeDesktopMask))
                w->type = CompWindowTypeFullscreenMask;
    }
}

static void
workaroundsDisplayOptionChanged (CompDisplay               *d,
				 CompOption                *opt,
				 WorkaroundsDisplayOptions num)
{
    CompScreen *s;
    CompWindow *w;

    for (s = d->screens; s; s = s->next)
	for (w = s->windows; w; w = w->next)
	    workaroundsUpdateSticky (w);
}

static void
workaroundsHandleEvent (CompDisplay *d,
			XEvent      *event)
{
    CompWindow *w;

    WORKAROUNDS_DISPLAY (d);

    UNWRAP (wd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (wd, d, handleEvent, workaroundsHandleEvent);

    if (event->type == ClientMessage)
    {
	if (event->xclient.message_type == d->winDesktopAtom)
        {
            w = findWindowAtDisplay (d, event->xclient.window);
	    workaroundsUpdateSticky (w);
        }
    }
}

static void
workaroundsWindowResizeNotify (CompWindow *w, int dx, int dy,
                               int dwidth, int dheight)
{
    WORKAROUNDS_SCREEN (w->screen);

    if (workaroundsGetLegacyFullscreen (w->screen->display))
    {
        workaroundsDoFixes (w);
    }

    UNWRAP (ws, w->screen, windowResizeNotify);
    (*w->screen->windowResizeNotify) (w, dx, dy, dwidth, dheight);
    WRAP (ws, w->screen, windowResizeNotify, workaroundsWindowResizeNotify);
}

static Bool
workaroundsInitDisplay (CompPlugin *plugin, CompDisplay *d)
{
    WorkaroundsDisplay *wd;

    wd = malloc (sizeof (WorkaroundsDisplay));
    if (!wd)
        return FALSE;

    wd->screenPrivateIndex = allocateScreenPrivateIndex( d );
    if (wd->screenPrivateIndex < 0)
    {
        free (wd);
        return FALSE;
    }

    wd->roleAtom = XInternAtom (d->display, "WM_WINDOW_ROLE", 0);

    workaroundsSetStickyAlldesktopsNotify (d, workaroundsDisplayOptionChanged);
    workaroundsSetAlldesktopStickyMatchNotify (d,
					       workaroundsDisplayOptionChanged);

    d->privates[displayPrivateIndex].ptr = wd;

    WRAP (wd, d, handleEvent, workaroundsHandleEvent);

    return TRUE;
}

static void
workaroundsFiniDisplay (CompPlugin *plugin, CompDisplay *d)
{
    WORKAROUNDS_DISPLAY (d);

    freeScreenPrivateIndex (d, wd->screenPrivateIndex);

    UNWRAP (wd, d, handleEvent);

    free (wd);
}

static Bool
workaroundsInitScreen (CompPlugin *plugin, CompScreen *s)
{
    WorkaroundsScreen *ws;

    WORKAROUNDS_DISPLAY (s->display);

    ws = malloc (sizeof (WorkaroundsScreen));
    if (!ws)
        return FALSE;

    ws->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (ws->windowPrivateIndex < 0)
    {
	free (ws);
	return FALSE;
    }

    WRAP (ws, s, windowResizeNotify, workaroundsWindowResizeNotify);

    s->privates[wd->screenPrivateIndex].ptr = ws;

    return TRUE;
}

static void
workaroundsFiniScreen (CompPlugin *plugin, CompScreen *s)
{
    WORKAROUNDS_SCREEN (s);

    freeWindowPrivateIndex (s, ws->windowPrivateIndex);

    UNWRAP (ws, s, windowResizeNotify);

    free (ws);
}

static Bool
workaroundsInitWindow (CompPlugin *plugin, CompWindow *w)
{
    WORKAROUNDS_SCREEN (w->screen);
    WorkaroundsWindow *ww;

    ww = malloc (sizeof (WorkaroundsWindow));
    if (!ww)
	return FALSE;

    ww->madeSticky = FALSE;

    w->privates[ws->windowPrivateIndex].ptr = ww;

    workaroundsUpdateSticky (w);
    workaroundsDoFixes (w);

    return TRUE;
}

static void
workaroundsFiniWindow (CompPlugin *plugin, CompWindow *w)
{
    WORKAROUNDS_WINDOW (w);

    w->wmType = getWindowType (w->screen->display, w->id);
    recalcWindowType (w);
    workaroundsRemoveSticky (w);

    free (ww);
}

static Bool
workaroundsInit (CompPlugin *plugin)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
        return FALSE;

    return TRUE;
}

static void
workaroundsFini (CompPlugin *plugin)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

static int
workaroundsGetVersion (CompPlugin *plugin, int version)
{
    return ABIVERSION;
}

CompPluginVTable workaroundsVTable =
{
    "workarounds",
    workaroundsGetVersion,
    0,
    workaroundsInit,
    workaroundsFini,
    workaroundsInitDisplay,
    workaroundsFiniDisplay,
    workaroundsInitScreen,
    workaroundsFiniScreen,
    workaroundsInitWindow,
    workaroundsFiniWindow,
    0, /* GetDisplayOptions */
    0, /* SetDisplayOption */
    0, /* GetScreenOptions */
    0  /* SetScreenOption */
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &workaroundsVTable;
}

