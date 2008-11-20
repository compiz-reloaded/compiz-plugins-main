/*
 * Compiz vpswitch plugin
 *
 * vpswitch.c
 *
 * Copyright (c) 2007 Dennis Kasprzyk <onestone@opencompositing.org>
 *
 * Go-to-numbered-viewport functionality by
 * 
 * Copyright (c) 2007 Robert Carr <racarr@opencompositing.org>
 *           (c) 2007 Danny Baumann <maniac@opencompositing.org>
 *
 * Go-to-specific-viewport functionality by
 *
 * Copyright (c) 2007 Michael Vogt <mvo@ubuntu.com>
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
 */

#include <compiz-core.h>
#include <string.h>
#include <X11/keysymdef.h>
#include "vpswitch_options.h"

static int displayPrivateIndex;

/* number-to-keysym mapping */
static const KeySym numberKeySyms[3][10] = {
    /* number key row */
    { XK_0, XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9 },
    /* number keypad with activated NumLock */
    { XK_KP_0, XK_KP_1, XK_KP_2, XK_KP_3, XK_KP_4,
      XK_KP_5, XK_KP_6, XK_KP_7, XK_KP_8, XK_KP_9 },
    /* number keypad without NumLock */
    { XK_KP_Insert, XK_KP_End, XK_KP_Down, XK_KP_Next, XK_KP_Left,
      XK_KP_Begin, XK_KP_Right, XK_KP_Home, XK_KP_Up, XK_KP_Prior }
};

typedef struct _VpSwitchDisplay
{
    HandleEventProc handleEvent;

    CompScreen *activeScreen;
    int destination;
} VpSwitchDisplay;

#define GET_VPSWITCH_DISPLAY(d)					\
    ((VpSwitchDisplay *) (d)->base.privates[displayPrivateIndex].ptr)
#define VPSWITCH_DISPLAY(d)			\
    VpSwitchDisplay * vd = GET_VPSWITCH_DISPLAY (d)

#define GET_DATA \
    CompScreen *s;\
    CompWindow *w;\
    Window     xid; \
    xid = getIntOptionNamed (option, nOption, "root", 0); \
    s = findScreenAtDisplay (d, xid); \
    if (!s) \
	return FALSE; \
    if (otherScreenGrabExist (s, "rotate", "wall", "plane", 0)) \
	return FALSE; \
    xid = getIntOptionNamed (option, nOption, "window", 0); \
    if (xid == s->grabWindow) \
	xid = d->below; \
    w = findWindowAtDisplay (d, xid); \
    if ((!w || (w->type & CompWindowTypeDesktopMask) == 0) && \
	xid != s->root) \
	return FALSE;

static Bool
vpswitchInitPlugin (CompDisplay    *d,
		   CompAction      *action,
		   CompActionState state,
		   CompOption      *option,
		   int             nOption)
{
    GET_DATA;

    CompObject *object;
    CompOption *tOption;
    int        nTOption;
    CompPlugin *plugin = findActivePlugin (vpswitchGetInitPlugin (d));
    Bool       rv = FALSE;

    if (!plugin || !plugin->vTable->getObjectOptions)
	return FALSE;

    object = compObjectFind (&core.base, COMP_OBJECT_TYPE_DISPLAY, NULL);
    if (!object)
	return FALSE;

    tOption = (*plugin->vTable->getObjectOptions) (plugin, object, &nTOption);
    while (nTOption--)
    {
	if (tOption->type == CompOptionTypeAction ||
	    tOption->type == CompOptionTypeKey ||
	    tOption->type == CompOptionTypeButton ||
	    tOption->type == CompOptionTypeEdge ||
	    tOption->type == CompOptionTypeBell)
	    if (strcmp (tOption->name, vpswitchGetInitAction (d)) == 0 &&
	        tOption->value.action.initiate)
	    {
		rv = (tOption->value.action.initiate)
		     (d, &tOption->value.action, state, option, nOption);
		break;
	    }

	tOption++;
    }

    if (rv)
	action->state |= CompActionStateTermButton;

    return rv;
}

static Bool
vpswitchTermPlugin (CompDisplay     *d,
		    CompAction      *action,
		    CompActionState state,
		    CompOption      *option,
		    int             nOption)
{
    CompObject *object;
    CompOption *tOption;
    int	       nTOption;
    CompPlugin *plugin = findActivePlugin (vpswitchGetInitPlugin (d));
    Bool       rv = FALSE;

    if (!plugin || !plugin->vTable->getObjectOptions)
	return FALSE;

    object = compObjectFind (&core.base, COMP_OBJECT_TYPE_DISPLAY, NULL);
    if (!object)
	return FALSE;

    tOption = (*plugin->vTable->getObjectOptions) (plugin, object, &nTOption);
    while (nTOption--)
    {
	if (tOption->type == CompOptionTypeAction ||
	    tOption->type == CompOptionTypeKey ||
	    tOption->type == CompOptionTypeButton ||
	    tOption->type == CompOptionTypeEdge ||
	    tOption->type == CompOptionTypeBell)
	    if (strcmp (tOption->name, vpswitchGetInitAction (d)) == 0 &&
	        tOption->value.action.terminate)
	    {
		rv = (tOption->value.action.terminate)
		     (d, &tOption->value.action, state, option, nOption);
		break;
	    }

	tOption++;
    }

    action->state &= ~CompActionStateTermButton;

    return rv;
}

static void
vpswitchGoto (CompScreen *s,
	      int        x,
	      int        y)
{
    XEvent xev;

    xev.xclient.type    = ClientMessage;
    xev.xclient.display = s->display->display;
    xev.xclient.format  = 32;

    xev.xclient.message_type = s->display->desktopViewportAtom;
    xev.xclient.window       = s->root;

    xev.xclient.data.l[0] = x * s->width;
    xev.xclient.data.l[1] = y * s->height;
    xev.xclient.data.l[2] = 0;
    xev.xclient.data.l[3] = 0;
    xev.xclient.data.l[4] = 0;

    XSendEvent (s->display->display, s->root, FALSE,
		SubstructureRedirectMask | SubstructureNotifyMask, &xev);
}

/* desktop mouse button initiated actions */

static Bool
vpswitchNext (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption)
{
    int targetX, targetY;

    GET_DATA;

    targetX = s->x + 1;
    targetY = s->y;

    if (targetX >= s->hsize)
    {
	targetX = 0;
	targetY++;
    }
    if (targetY >= s->vsize)
	targetY = 0;

    vpswitchGoto (s, targetX, targetY);

    return TRUE;
}

static Bool
vpswitchPrev (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption)
{
    int targetX, targetY;

    GET_DATA;

    targetX = s->x - 1;
    targetY = s->y;

    if (targetX < 0)
    {
	targetX = s->hsize - 1;
	targetY--;
    }
    if (targetY < 0)
	targetY = s->vsize - 1;

    vpswitchGoto (s, targetX, targetY);

    return TRUE;
}

static Bool
vpswitchLeft (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption)
{
    GET_DATA;

    if (s->x > 0)
	vpswitchGoto (s, s->x - 1, s->y);

    return TRUE;
}

static Bool
vpswitchRight (CompDisplay     *d,
	       CompAction      *action,
	       CompActionState state,
	       CompOption      *option,
	       int             nOption)
{
    GET_DATA;

    if (s->x < s->hsize - 1)
	vpswitchGoto (s, s->x + 1, s->y);

    return TRUE;
}

static Bool
vpswitchUp (CompDisplay     *d,
	    CompAction      *action,
	    CompActionState state,
	    CompOption      *option,
	    int             nOption)
{
    GET_DATA;

    if (s->y > 0)
	vpswitchGoto (s, s->x, s->y - 1);

    return TRUE;
}

static Bool
vpswitchDown (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption)
{
    GET_DATA;

    if (s->y < s->vsize - 1)
	vpswitchGoto (s, s->x, s->y + 1);

    return TRUE;
}

/* stuff for switching by entering the viewport number */
static void
vpswitchHandleEvent (CompDisplay *d,
	  	     XEvent      *event)
{
    CompScreen *s;

    VPSWITCH_DISPLAY (d);

    switch (event->type)
    {
    case KeyPress:
	s = findScreenAtDisplay (d, event->xkey.root);
	if (s && (s == vd->activeScreen))
	{
	    KeySym       pressedKeySym;
	    unsigned int mods;
	    int          i, row;

	    pressedKeySym = XLookupKeysym (&event->xkey, 0);
	    mods = keycodeToModifiers (d, event->xkey.keycode);
	    if (mods & CompNumLockMask)
		row = 1; /* use first row of lookup table */
	    else
		row = 2;

	    for (i = 0; i < 10; i++)
	    {
		/* first try to handle normal number keys */
		if (numberKeySyms[0][i] == pressedKeySym)
		{
		    vd->destination *= 10;
		    vd->destination += i;
		    break;
		}
		else
		{
		    if (numberKeySyms[row][i] == pressedKeySym)
		    {
			vd->destination *= 10;
			vd->destination += i;
			break;
		    }
		}
	    }
	}
    }

    UNWRAP (vd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (vd, d, handleEvent, vpswitchHandleEvent);
}


static Bool
vpswitchBeginNumbered (CompDisplay     *d,
		       CompAction      *action,
		       CompActionState state,
		       CompOption      *option,
		       int             nOption)
{
    VPSWITCH_DISPLAY (d);

    if (!vd->activeScreen)
    {
	Window xid;

	xid = getIntOptionNamed (option, nOption, "root", 0);
	vd->activeScreen = findScreenAtDisplay (d, xid);
	vd->destination = 0;

	if (state & CompActionStateInitKey)
	    action->state |= CompActionStateTermKey;

	return TRUE;
    }

    return FALSE;
}

static Bool
vpswitchTermNumbered (CompDisplay     *d,
		      CompAction      *action,
		      CompActionState state,
		      CompOption      *option,
		      int             nOption)
{
    CompScreen *s;
    int        nx, ny;

    VPSWITCH_DISPLAY (d);

    s = vd->activeScreen;
    if (!s)
	return FALSE;

    vd->activeScreen = 0;

    if (vd->destination < 1 || vd->destination > (s->hsize * s->vsize))
	return FALSE;

    nx = (vd->destination - 1 ) % s->hsize;
    ny = (vd->destination - 1 ) / s->hsize;

    vpswitchGoto (s, nx, ny);

    return FALSE;
}

/* switch-to-specific viewport stuff */

static Bool
vpswitchSwitchTo (CompDisplay     *d,
		  CompAction      *action,
		  CompActionState state,
		  CompOption      *option,
		  int             nOption)
{
    int i;

    VPSWITCH_DISPLAY (d);

    for (i = VpswitchDisplayOptionSwitchTo1Key;
	 i <= VpswitchDisplayOptionSwitchTo12Key; i++)
    {
	if (action == &vpswitchGetDisplayOption (d, i)->value.action)
	{
	    Window xid = getIntOptionNamed (option, nOption, "root", 0);

	    vd->activeScreen = findScreenAtDisplay (d, xid);
	    vd->destination  = i - VpswitchDisplayOptionSwitchTo1Key + 1;
	    break;
	}
    }

    return vpswitchTermNumbered (d, action, state, option, nOption);
}

static Bool
vpswitchInitDisplay (CompPlugin  *p,
		     CompDisplay *d)
{
    VpSwitchDisplay *vd;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    vd = malloc (sizeof (VpSwitchDisplay));
    if (!vd)
	return FALSE;

    vd->activeScreen = 0;

    WRAP (vd, d, handleEvent, vpswitchHandleEvent);

    d->base.privates[displayPrivateIndex].ptr = vd;

    vpswitchSetLeftButtonInitiate (d, vpswitchLeft);
    vpswitchSetRightButtonInitiate (d, vpswitchRight);
    vpswitchSetUpButtonInitiate (d, vpswitchUp);
    vpswitchSetDownButtonInitiate (d, vpswitchDown);
    vpswitchSetNextButtonInitiate (d, vpswitchNext);
    vpswitchSetPrevButtonInitiate (d, vpswitchPrev);
    vpswitchSetInitiateButtonInitiate (d, vpswitchInitPlugin);
    vpswitchSetInitiateButtonTerminate (d, vpswitchTermPlugin);

    vpswitchSetBeginKeyInitiate (d, vpswitchBeginNumbered);
    vpswitchSetBeginKeyTerminate (d, vpswitchTermNumbered);

    vpswitchSetSwitchTo1KeyInitiate (d, vpswitchSwitchTo);
    vpswitchSetSwitchTo2KeyInitiate (d, vpswitchSwitchTo);
    vpswitchSetSwitchTo3KeyInitiate (d, vpswitchSwitchTo);
    vpswitchSetSwitchTo4KeyInitiate (d, vpswitchSwitchTo);
    vpswitchSetSwitchTo5KeyInitiate (d, vpswitchSwitchTo);
    vpswitchSetSwitchTo6KeyInitiate (d, vpswitchSwitchTo);
    vpswitchSetSwitchTo7KeyInitiate (d, vpswitchSwitchTo);
    vpswitchSetSwitchTo8KeyInitiate (d, vpswitchSwitchTo);
    vpswitchSetSwitchTo9KeyInitiate (d, vpswitchSwitchTo);
    vpswitchSetSwitchTo10KeyInitiate (d, vpswitchSwitchTo);
    vpswitchSetSwitchTo11KeyInitiate (d, vpswitchSwitchTo);
    vpswitchSetSwitchTo12KeyInitiate (d, vpswitchSwitchTo);

    return TRUE;
}

static void
vpswitchFiniDisplay(CompPlugin  *p,
	  	    CompDisplay *d)
{
    VPSWITCH_DISPLAY (d);

    UNWRAP (vd, d, handleEvent);

    free (vd);
}

static CompBool
vpswitchInitObject (CompPlugin *p,
		    CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) vpswitchInitDisplay
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
vpswitchFiniObject (CompPlugin *p,
		    CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) vpswitchFiniDisplay,
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
vpswitchInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
vpswitchFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable vpswitchVTable = {
    "vpswitch",
    0,
    vpswitchInit,
    vpswitchFini,
    vpswitchInitObject,
    vpswitchFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &vpswitchVTable;
}
