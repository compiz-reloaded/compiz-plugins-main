/*
 * winrules plugin for compiz
 *
 * Copyright (C) 2007 Bellegarde Cedric (gnumdk (at) gmail.com)
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

#include <compiz.h>

#include <X11/Xatom.h>

#define WINRULES_TARGET_WINDOWS (CompWindowTypeNormalMask |	\
				 CompWindowTypeDialogMask |	\
				 CompWindowTypeModalDialogMask |\
				 CompWindowTypeFullscreenMask |	\
				 CompWindowTypeUnknownMask)


#define WINRULES_SCREEN_OPTION_SKIPTASKBAR_MATCH  0
#define WINRULES_SCREEN_OPTION_SKIPPAGER_MATCH	  1
#define WINRULES_SCREEN_OPTION_ABOVE_MATCH	  2
#define WINRULES_SCREEN_OPTION_BELOW_MATCH        3
#define WINRULES_SCREEN_OPTION_STICKY_MATCH       4
#define WINRULES_SCREEN_OPTION_FULLSCREEN_MATCH   5
#define WINRULES_SCREEN_OPTION_WIDGET_MATCH       6
#define WINRULES_SCREEN_OPTION_NOMOVE_MATCH       7
#define WINRULES_SCREEN_OPTION_NORESIZE_MATCH     8
#define WINRULES_SCREEN_OPTION_NOMINIMIZE_MATCH   9
#define WINRULES_SCREEN_OPTION_NOMAXIMIZE_MATCH   10
#define WINRULES_SCREEN_OPTION_NOCLOSE_MATCH      11
#define WINRULES_SCREEN_OPTION_NOFOCUS_MATCH      12
#define WINRULES_SCREEN_OPTION_SIZE_MATCHES	  13
#define WINRULES_SCREEN_OPTION_SIZE_WIDTH_VALUES  14
#define WINRULES_SCREEN_OPTION_SIZE_HEIGHT_VALUES 15
#define WINRULES_SCREEN_OPTION_NUM		  16

static CompMetadata winrulesMetadata;

static int displayPrivateIndex;

typedef struct _WinrulesWindow {

    unsigned int allowedActions;
    unsigned int stateSetMask;
    unsigned int protocolSetMask;
    Bool widgetSet;

    Bool firstMap;
} WinrulesWindow;

typedef struct _WinrulesDisplay {
    int screenPrivateIndex;
    HandleEventProc handleEvent;
} WinrulesDisplay;

typedef struct _WinrulesScreen {
    int windowPrivateIndex;
    GetAllowedActionsForWindowProc getAllowedActionsForWindow;
    CompOption opt[WINRULES_SCREEN_OPTION_NUM];
} WinrulesScreen;

#define GET_WINRULES_DISPLAY(d)				     			   \
    ((WinrulesDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define WINRULES_DISPLAY(d)			   				   \
    WinrulesDisplay *wd = GET_WINRULES_DISPLAY (d)

#define GET_WINRULES_SCREEN(s, wd)					 	   \
    ((WinrulesScreen *) (s)->privates[(wd)->screenPrivateIndex].ptr)

#define WINRULES_SCREEN(s)							   \
    WinrulesScreen *ws = GET_WINRULES_SCREEN (s, GET_WINRULES_DISPLAY (s->display))

#define GET_WINRULES_WINDOW(w, ws)                                   		   \
    ((WinrulesWindow *) (w)->privates[(ws)->windowPrivateIndex].ptr)
    
#define WINRULES_WINDOW(w)							   \
    WinrulesWindow *ww = GET_WINRULES_WINDOW  (w,				   \
					    GET_WINRULES_SCREEN  (w->screen,	   \
					    GET_WINRULES_DISPLAY (w->screen->display)))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))


static void
winrulesSetProtocols (CompDisplay *display,
		      unsigned int protocols,
		      Window id)
{
    Atom *protocol = NULL;
    int count = 0;

    if (protocols & CompWindowProtocolDeleteMask)
    {
	protocol = realloc (protocol, (count + 1) * sizeof(Atom));
	protocol[count++] = display->wmDeleteWindowAtom;
    }
    if (protocols & CompWindowProtocolTakeFocusMask)
    {
	protocol = realloc (protocol, (count + 1) * sizeof(Atom));
	protocol[count++] = display->wmTakeFocusAtom;
    }
    if (protocols & CompWindowProtocolPingMask)
    {
	protocol = realloc (protocol, (count + 1) * sizeof(Atom));
	protocol[count++] = display->wmPingAtom;
    }
    if (protocols & CompWindowProtocolSyncRequestMask)
    {
	protocol = realloc (protocol, (count + 1) * sizeof(Atom));
	protocol[count] = display->wmSyncRequestAtom;
    }
    XSetWMProtocols (display->display,
		     id,
		     protocol,
		     count);

    XFree (protocol);
}

/* FIXME? Directly set inputHint, not a problem for now */
static void
winrulesSetNoFocus (CompWindow *w,
		    int optNum)
{
    unsigned int newProtocol = w->protocols;

    WINRULES_SCREEN (w->screen);
    WINRULES_WINDOW (w);

    if (matchEval (&ws->opt[optNum].value.match, w))
    {
	if (w->protocols & CompWindowProtocolTakeFocusMask)
	{
    	    newProtocol = w->protocols & ~CompWindowProtocolTakeFocusMask;
    	    ww->protocolSetMask |= CompWindowProtocolTakeFocusMask;
    	    w->inputHint = FALSE;
	}
    }
    else if (ww->protocolSetMask & CompWindowProtocolTakeFocusMask)
    {
	newProtocol = w->protocols & CompWindowProtocolTakeFocusMask;
	ww->protocolSetMask &= ~CompWindowProtocolTakeFocusMask;
	w->inputHint = TRUE;
    }

   if (newProtocol != w->protocols)
	winrulesSetProtocols (w->screen->display,
		      w->protocols,
		      w->id);
}

static void
winrulesUpdateState (CompWindow *w,
		     int optNum,
		     int mask)
{
    unsigned int newState = w->state;

    WINRULES_SCREEN (w->screen);
    WINRULES_WINDOW (w);

    if (matchEval (&ws->opt[optNum].value.match, w))
    {
	newState |= mask;
	newState = constrainWindowState (newState, w->actions);
	ww->stateSetMask |= (newState & mask);
    }
    else if (ww->stateSetMask & mask)
    {
	newState &= ~mask;
	ww->stateSetMask &= ~mask;
    }

    if (newState != w->state)
    {
	changeWindowState (w, newState);

	recalcWindowType (w);
	recalcWindowActions (w);

	if (mask & (CompWindowStateFullscreenMask |
		    CompWindowStateAboveMask      |
		    CompWindowStateBelowMask       ))
	    updateWindowAttributes (w, CompStackingUpdateModeNormal);
	else
	    updateWindowAttributes (w, CompStackingUpdateModeNone);
    }
}

static void
winrulesUpdateWidget (CompWindow *w)
{
    Atom compizWidget = XInternAtom (w->screen->display->display,
				     "_COMPIZ_WIDGET",
				     FALSE);

    WINRULES_SCREEN (w->screen);
    WINRULES_WINDOW (w);

    if (matchEval
	(&ws->opt[WINRULES_SCREEN_OPTION_WIDGET_MATCH].value.match, w))
    {
	if (w->inShowDesktopMode || w->mapNum ||
                w->attrib.map_state == IsViewable || w->minimized)
	{
	    if (w->minimized || w->inShowDesktopMode)
		unminimizeWindow (w);
	    XChangeProperty (w->screen->display->display, w->id, compizWidget,
			     XA_STRING, 8, PropModeReplace,
			     (unsigned char *)(int[]){-2}, 1);
	    ww->widgetSet = TRUE;
	}
    }
    else if (ww->widgetSet)
    {
	XDeleteProperty (w->screen->display->display, w->id, compizWidget);
	ww->widgetSet = FALSE;
    }
}

static void
winrulesSetAllowedActions (CompWindow *w,
			   int optNum,
			   int action)
{
    WINRULES_SCREEN (w->screen);
    WINRULES_WINDOW (w);

    if (matchEval (&ws->opt[optNum].value.match, w))
	ww->allowedActions &= ~action;
    else if ( !(ww->allowedActions & action))
	ww->allowedActions |= action;

    recalcWindowActions (w);
}

static Bool
winrulesMatchSizeValue (CompWindow *w,
			CompOption *matches,
			CompOption *widthValues,
			CompOption *heightValues,
			int	   *width,
			int	   *height)
{
    int i, min;

    if (w->type & CompWindowTypeDesktopMask)
	return FALSE;

    min = MIN (matches->value.list.nValue, widthValues->value.list.nValue);
    min = MIN (min, heightValues->value.list.nValue);

    for (i = 0; i < min; i++)
    {
	if (matchEval (&matches->value.list.value[i].match, w))
	{
	    *width = widthValues->value.list.value[i].i;
	    *height = heightValues->value.list.value[i].i;
	
	    return TRUE;
	}
    }

    return FALSE;
}

static Bool
winrulesMatchSize (CompWindow *w,
		   int	      *width,
		   int	      *height)
{
    WINRULES_SCREEN (w->screen);

    return winrulesMatchSizeValue (w,
				   &ws->opt[WINRULES_SCREEN_OPTION_SIZE_MATCHES],
				   &ws->opt[WINRULES_SCREEN_OPTION_SIZE_WIDTH_VALUES],
				   &ws->opt[WINRULES_SCREEN_OPTION_SIZE_HEIGHT_VALUES],
				   width,
				   height);
}

static void
winrulesUpdateWindowSize (CompWindow *w,
			  int width,
			  int height)
{
    XWindowChanges xwc;
    unsigned int xwcm = 0;

    if (width != w->serverWidth)
	xwcm |= CWWidth;
    if (height != w->serverHeight)
	xwcm |= CWHeight;

    xwc.x = w->serverX;
    xwc.y = w->serverY;
    xwc.width = width;
    xwc.height = height;

    configureXWindow (w, xwcm, &xwc);
}

static CompOption *
winrulesGetScreenOptions (CompPlugin *plugin,
			  CompScreen *screen,
                          int        *count)
{
    WINRULES_SCREEN (screen);

    *count = NUM_OPTIONS (ws);
    return ws->opt;
}

static Bool
winrulesSetScreenOption (CompPlugin *plugin,
			 CompScreen      *screen,
                         char            *name, 
                         CompOptionValue *value)
{
    CompOption *o;
    CompWindow *w;
    int index;

    WINRULES_SCREEN (screen);

    o = compFindOption (ws->opt, NUM_OPTIONS (ws), name, &index);
    if (!o)
        return FALSE;

    switch (index)
    {
    case WINRULES_SCREEN_OPTION_SKIPTASKBAR_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_SKIPTASKBAR_MATCH,
				     CompWindowStateSkipTaskbarMask);
	    }
				
	    return TRUE;
	}
	break;

    case WINRULES_SCREEN_OPTION_SKIPPAGER_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_SKIPPAGER_MATCH,
				     CompWindowStateSkipPagerMask);
	    }
	    return TRUE;
	}
	break;

    case WINRULES_SCREEN_OPTION_ABOVE_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_ABOVE_MATCH,
				     CompWindowStateAboveMask);
	    }
	    return TRUE;
	}
	break;
	
    case WINRULES_SCREEN_OPTION_BELOW_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_BELOW_MATCH,
				     CompWindowStateBelowMask);
	    }
	    return TRUE;
	}
	break;
	
    case WINRULES_SCREEN_OPTION_STICKY_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_STICKY_MATCH,
				     CompWindowStateStickyMask);
	    }
	    return TRUE;
	}
	break;
	
    case WINRULES_SCREEN_OPTION_FULLSCREEN_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_FULLSCREEN_MATCH,
				     CompWindowStateFullscreenMask);
	    }
	    return TRUE;
	}
	break;

    case WINRULES_SCREEN_OPTION_NOMOVE_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

		winrulesSetAllowedActions (w,
					   WINRULES_SCREEN_OPTION_NOMOVE_MATCH,
					   CompWindowActionMoveMask);
	    }
	    return TRUE;
	}
	break;

    case WINRULES_SCREEN_OPTION_NORESIZE_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

		winrulesSetAllowedActions (w,
					   WINRULES_SCREEN_OPTION_NORESIZE_MATCH,
					   CompWindowActionResizeMask);
	    }
	    return TRUE;
	}
	break;

    case WINRULES_SCREEN_OPTION_NOMINIMIZE_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

		winrulesSetAllowedActions (w, 
					   WINRULES_SCREEN_OPTION_NOMINIMIZE_MATCH,
					   CompWindowActionMinimizeMask);
	    }
	    return TRUE;
	}
	break;

    case WINRULES_SCREEN_OPTION_NOMAXIMIZE_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

		winrulesSetAllowedActions (w, 
					   WINRULES_SCREEN_OPTION_NOMAXIMIZE_MATCH,
					   CompWindowActionMaximizeVertMask|
					   CompWindowActionMaximizeHorzMask);
	    }
	    return TRUE;
	}
	break;

    case WINRULES_SCREEN_OPTION_NOCLOSE_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

		winrulesSetAllowedActions (w, 
					   WINRULES_SCREEN_OPTION_NOCLOSE_MATCH,
					   CompWindowActionCloseMask);
	    }
	    return TRUE;
	}
	break;
    case WINRULES_SCREEN_OPTION_NOFOCUS_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;
		
		winrulesSetNoFocus (w, WINRULES_SCREEN_OPTION_NOFOCUS_MATCH);
	    }
	    return TRUE;
	}
	break;
    case WINRULES_SCREEN_OPTION_WIDGET_MATCH:
	if (compSetMatchOption (o, value))
	{
	    for (w = screen->windows; w; w = w->next)
	    {
		if (!w->type & WINRULES_TARGET_WINDOWS)
		    continue;

	    	winrulesUpdateWidget (w);
	    }
	    return TRUE;
	}
	break;
    case WINRULES_SCREEN_OPTION_SIZE_MATCHES:
	if (compSetOptionList (o, value))
	{
	    int i;

	    for (i = 0; i < o->value.list.nValue; i++)
		matchUpdate (screen->display, &o->value.list.value[i].match);

	    return TRUE;
	}
	break;
    default:
	if (compSetOption (o, value))
	    return TRUE;
        break;
    }

    return FALSE;
}

static void
winrulesHandleEvent (CompDisplay *d,
                     XEvent      *event)
{
    CompWindow *w;

    WINRULES_DISPLAY (d);

    if (event->type == MapNotify)
    {
	w = findWindowAtDisplay (d, event->xmap.window);
	if (w && w->type & WINRULES_TARGET_WINDOWS)
	{
	    WINRULES_WINDOW (w);
	    /* Only apply at window creation.
	     * Using CreateNotify not working.
	     */
	    if (ww->firstMap)
	    {
		int width, height;
		
		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_SKIPTASKBAR_MATCH,
				     CompWindowStateSkipTaskbarMask);

		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_SKIPPAGER_MATCH,
				     CompWindowStateSkipPagerMask);

		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_ABOVE_MATCH,
				     CompWindowStateAboveMask);

		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_BELOW_MATCH,
				     CompWindowStateBelowMask);

		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_STICKY_MATCH,
				     CompWindowStateStickyMask);

		winrulesUpdateState (w,
				     WINRULES_SCREEN_OPTION_FULLSCREEN_MATCH,
				     CompWindowStateFullscreenMask);

		winrulesUpdateWidget (w);

		winrulesSetAllowedActions (w,
					   WINRULES_SCREEN_OPTION_NOMOVE_MATCH,
					   CompWindowActionMoveMask);

		winrulesSetAllowedActions (w,
					   WINRULES_SCREEN_OPTION_NORESIZE_MATCH,
					   CompWindowActionResizeMask);

		winrulesSetAllowedActions (w,
					   WINRULES_SCREEN_OPTION_NOMINIMIZE_MATCH,
					   CompWindowActionMinimizeMask);

		winrulesSetAllowedActions (w,
					   WINRULES_SCREEN_OPTION_NOMAXIMIZE_MATCH,
					   CompWindowActionMaximizeVertMask|
					   CompWindowActionMaximizeHorzMask);
	
		winrulesSetAllowedActions (w,
					   WINRULES_SCREEN_OPTION_NOCLOSE_MATCH,
					   CompWindowActionCloseMask);
		
		if (winrulesMatchSize (w, &width, &height))
		    winrulesUpdateWindowSize (w, width, height);
	    }

	    ww->firstMap = FALSE;
	}
    }
    else if (event->type == MapRequest)
    {
	w = findWindowAtDisplay (d, event->xmap.window);
	if (w && w->type & WINRULES_TARGET_WINDOWS)
	    winrulesSetNoFocus (w,WINRULES_SCREEN_OPTION_NOFOCUS_MATCH);
    }

    UNWRAP (wd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (wd, d, handleEvent, winrulesHandleEvent);
}

static unsigned int
winrulesGetAllowedActionsForWindow (CompWindow *w)
{
    unsigned int actions;

    WINRULES_SCREEN (w->screen);
    WINRULES_WINDOW (w);

    UNWRAP (ws, w->screen, getAllowedActionsForWindow);
    actions = (*w->screen->getAllowedActionsForWindow) (w);
    WRAP (ws, w->screen, getAllowedActionsForWindow,
          winrulesGetAllowedActionsForWindow);

    return actions & ww->allowedActions;

}

static Bool
winrulesInitDisplay (CompPlugin  *p, 
		     CompDisplay *d)
{
    WinrulesDisplay *wd;

    wd = malloc (sizeof (WinrulesDisplay));
    if (!wd)
        return FALSE;

    wd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (wd->screenPrivateIndex < 0)
    {
        free (wd);
        return FALSE;
    }
    WRAP (wd, d, handleEvent, winrulesHandleEvent);
    d->privates[displayPrivateIndex].ptr = wd;

    return TRUE;
}

static void
winrulesFiniDisplay (CompPlugin  *p, 
		     CompDisplay *d)
{
    WINRULES_DISPLAY (d);

    freeScreenPrivateIndex (d, wd->screenPrivateIndex);
    UNWRAP (wd, d, handleEvent);
    free (wd);
}

static const CompMetadataOptionInfo winrulesScreenOptionInfo[] = {
    { "skiptaskbar_match", "match", 0, 0, 0 },
    { "skippager_match", "match", 0, 0, 0 },
    { "above_match", "match", 0, 0, 0 },
    { "below_match", "match", 0, 0, 0 },
    { "sticky_match", "match", 0, 0, 0 },
    { "fullscreen_match", "match", 0, 0, 0 },
    { "widget_match", "match", 0, 0, 0 },
    { "no_move_match", "match", 0, 0, 0 },
    { "no_resize_match", "match", 0, 0, 0 },
    { "no_minimize_match", "match", 0, 0, 0 },
    { "no_maximize_match", "match", 0, 0, 0 },
    { "no_close_match", "match", 0, 0, 0 },
    { "no_focus_match", "match", 0, 0, 0 },
    { "size_matches", "list", "<type>match</type>", 0, 0 },
    { "size_width_values", "list", "<type>int</type>", 0, 0 },
    { "size_height_values", "list", "<type>int</type>", 0, 0 }
};

static Bool
winrulesInitScreen (CompPlugin *p, 
		    CompScreen *s)
{
    WinrulesScreen *ws;

    WINRULES_DISPLAY (s->display);

    ws = malloc (sizeof (WinrulesScreen));
    if (!ws)
        return FALSE;

    if (!compInitScreenOptionsFromMetadata (s,
					    &winrulesMetadata,
					    winrulesScreenOptionInfo,
					    ws->opt,
					    WINRULES_SCREEN_OPTION_NUM))
    {
	free (ws);
	return FALSE;
    }

    ws->windowPrivateIndex = allocateWindowPrivateIndex(s);
    if (ws->windowPrivateIndex < 0)
    {
	compFiniScreenOptions (s, ws->opt, WINRULES_SCREEN_OPTION_NUM);
	free (ws);
	return FALSE;
    }

    WRAP (ws, s, getAllowedActionsForWindow,
	  winrulesGetAllowedActionsForWindow);

    s->privates[wd->screenPrivateIndex].ptr = ws;

    return TRUE;
}

static void
winrulesFiniScreen (CompPlugin *p, 
                    CompScreen *s)
{
    WINRULES_SCREEN (s);

    UNWRAP (ws, s, getAllowedActionsForWindow);

    freeWindowPrivateIndex(s, ws->windowPrivateIndex);

    compFiniScreenOptions (s, ws->opt, WINRULES_SCREEN_OPTION_NUM);

    free (ws);
}

static Bool
winrulesInitWindow (CompPlugin *p, 
		    CompWindow *w)
{
    WINRULES_SCREEN (w->screen);

    WinrulesWindow *ww = malloc (sizeof (WinrulesWindow));
    if (!ww)
    {
        return FALSE;
    }

    ww->widgetSet       = FALSE;
    ww->stateSetMask    = 0;
    ww->protocolSetMask = 0;

    ww->allowedActions = ~0;

    ww->firstMap = TRUE;

    w->privates[ws->windowPrivateIndex].ptr = ww;

    return TRUE;
}

static void
winrulesFiniWindow (CompPlugin *p, 
                    CompWindow *w)
{
    WINRULES_WINDOW (w);

    free (ww);
}

static Bool
winrulesInit (CompPlugin *p)
{
    if (!compInitPluginMetadataFromInfo (&winrulesMetadata,
					 p->vTable->name,
					 0, 0,
					 winrulesScreenOptionInfo,
					 WINRULES_SCREEN_OPTION_NUM))
	return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
    {
	compFiniMetadata (&winrulesMetadata);
        return FALSE;
    }

    compAddMetadataFromFile (&winrulesMetadata, p->vTable->name);

    return TRUE;
}

static void
winrulesFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);

    compFiniMetadata (&winrulesMetadata);
}

static int
winrulesGetVersion (CompPlugin *plugin,
		int	    version)
{
    return ABIVERSION;
}

static CompMetadata *
winrulesGetMetadata (CompPlugin *plugin)
{
    return &winrulesMetadata;
}

static CompPluginVTable winrulesVTable = {
    "winrules",
    winrulesGetVersion,
    winrulesGetMetadata,
    winrulesInit,
    winrulesFini,
    winrulesInitDisplay,
    winrulesFiniDisplay,
    winrulesInitScreen,
    winrulesFiniScreen,
    winrulesInitWindow,
    winrulesFiniWindow,
    0, /* winrulesGetDisplayOptions, */
    0, /* winrulesSetDisplayOption,  */
    winrulesGetScreenOptions,
    winrulesSetScreenOption
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &winrulesVTable;
}
