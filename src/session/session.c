/**
 *
 * Compiz session plugin
 *
 * session.c
 *
 * Copyright (c) 2008 Travis Watkins <amaranth@ubuntu.com>
 * Copyright (c) 2008 Danny Baumann <maniac@opencompositing.org>
 * Copyright (c) 2006 Patrick Niklaus
 *
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
 * Authors: Travis Watkins <amaranth@ubuntu.com>
 *          Patrick Niklaus
 **/

#define _GNU_SOURCE
#include <X11/Xatom.h>

#include <compiz-core.h>

#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#define SESSION_DISPLAY_OPTION_SAVE_LEGACY 0
#define SESSION_DISPLAY_OPTION_NUM         1

typedef struct _SessionWindowList
{
    struct _SessionWindowList *next;

    char *clientId;
    char *title;
    char *resName;
    char *resClass;
    char *role;
    char *command;

    XRectangle   geometry;
    Bool         geometryValid;
    unsigned int state;
    Bool         minimized;
    int          workspace;
} SessionWindowList;

typedef struct _SessionCore
{
    SessionWindowList *windowList;

    SessionSaveYourselfProc sessionSaveYourself;
    ObjectAddProc           objectAdd;
} SessionCore;

typedef struct _SessionDisplay
{
    CompTimeoutHandle windowAddTimeout;

    Atom visibleNameAtom;
    Atom clientIdAtom;
    Atom embedInfoAtom;
    Atom roleAtom;
    Atom commandAtom;

    HandleEventProc handleEvent;

    CompOption opt[SESSION_DISPLAY_OPTION_NUM];
} SessionDisplay;

#define GET_SESSION_CORE(c)				     \
    ((SessionCore *) (c)->base.privates[corePrivateIndex].ptr)

#define SESSION_CORE(c)		       \
    SessionCore *sc = GET_SESSION_CORE (c)

#define GET_SESSION_DISPLAY(d)                                 \
    ((SessionDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define SESSION_DISPLAY(d)                  \
    SessionDisplay *sd = GET_SESSION_DISPLAY (d)

#define NUM_OPTIONS(x) (sizeof ((x)->opt) / sizeof (CompOption))

static int corePrivateIndex;
static int displayPrivateIndex;

static CompMetadata      sessionMetadata;

static void
sessionFreeWindowListItem (SessionWindowList *item)
{
    if (item->clientId)
	free (item->clientId);

    if (item->title)
	free (item->title);

    if (item->resName)
	free (item->resName);

    if (item->resClass)
	free (item->resClass);

    if (item->role)
	free (item->role);

    if (item->command)
	free (item->command);

    free (item);
}

static void
sessionAddWindowListItem (SessionWindowList *item)
{
    SessionWindowList *run;

    SESSION_CORE (&core);

    run = sc->windowList;
    if (!run)
	sc->windowList = item;
    else
    {
	for (; run->next; run = run->next);
	run->next = item;
    }
}

static void
sessionRemoveWindowListItem (SessionWindowList *item)
{
    SessionWindowList *run;

    SESSION_CORE (&core);

    if (!sc->windowList)
	return;

    if (sc->windowList == item)
	sc->windowList = item->next;
    else
    {
	for (run = sc->windowList; run->next; run = run->next)
	{
	    if (run->next == item)
	    {
		run->next = item->next;
		sessionFreeWindowListItem (item);
		break;
	    }
	}
    }
}

static char*
sessionGetUtf8Property (CompDisplay *d,
			Window      id,
			Atom        atom)
{
    Atom          type;
    int           format;
    unsigned long nitems;
    unsigned long bytesAfter;
    char          *val;
    int           result;
    char          *retval;

    result = XGetWindowProperty (d->display, id, atom, 0L, 65536, False,
                                 d->utf8StringAtom, &type, &format, &nitems,
                                 &bytesAfter, (unsigned char **)&val);

    if (result != Success)
	return NULL;

    if (type != d->utf8StringAtom || format != 8 || nitems == 0)
    {
	if (val)
	    XFree (val);
	return NULL;
    }

    retval = strndup (val, nitems);
    XFree (val);

    return retval;
}

static char*
sessionGetTextProperty (CompDisplay *d,
			Window      id,
			Atom        atom)
{
    XTextProperty text;
    char          *retval = NULL;

    text.nitems = 0;
    if (XGetTextProperty (d->display, id, &text, atom))
    {
	if (text.value) {
	    retval = strndup ((char *)text.value,text.nitems);
	    XFree (text.value);
	}
    }

    return retval;
}

static char*
sessionGetWindowTitle (CompWindow *w)
{
    CompDisplay *d = w->screen->display;
    char        *name;

    SESSION_DISPLAY (d);

    name = sessionGetUtf8Property (d, w->id, sd->visibleNameAtom);

    if (!name)
	name = sessionGetUtf8Property(d, w->id, d->wmNameAtom);

    if (!name)
	name = sessionGetTextProperty (d, w->id, XA_WM_NAME);

    return name;
}

static Bool
sessionGetIsEmbedded (CompDisplay *d,
		      Window      id)
{
    Atom          type;
    int           format;
    unsigned long nitems;
    unsigned long bytesAfter;
    unsigned char *val;
    int           result;

    SESSION_DISPLAY (d);

    result = XGetWindowProperty (d->display, id, sd->embedInfoAtom, 0L, 65536,
				 FALSE, XA_CARDINAL, &type, &format, &nitems,
                                 &bytesAfter, &val);

    if (result != Success)
	return FALSE;

    if (val)
	XFree (val);

    return (nitems > 1);
}

static char*
sessionGetClientLeaderProperty (CompWindow *w,
				Atom       atom)
{
    Window        clientLeader;

    clientLeader = w->clientLeader;

    /* try to find clientLeader on transient parents */
    if (!clientLeader)
    {
	CompWindow *window = w;

	while (window && window->transientFor)
	{
	    if (window->transientFor == window->id)
		break;

	    window = findWindowAtScreen (w->screen, window->transientFor);
	    if (window && window->clientLeader)
	    {
		clientLeader = window->clientLeader;
		break;
	    }
	}
    }

    if (!clientLeader)
	clientLeader = w->id;

    return sessionGetTextProperty (w->screen->display, clientLeader, atom);
}

static int
sessionGetIntForProp (xmlNodePtr node,
		      char       *prop)
{
    xmlChar *temp;
    int      num;

    temp = xmlGetProp (node, BAD_CAST prop);
    if (temp)
    {
	num = xmlXPathCastStringToNumber (temp);
	xmlFree (temp);
	return num;
    }

    return -1;
}

static char *
sessionGetStringForProp (xmlNodePtr node,
			 char       *prop)
{
    xmlChar *text;
    char    *retval = NULL;

    text = xmlGetProp (node, BAD_CAST prop);
    if (text)
    {
	retval = strdup ((char*) text);
	xmlFree (text);
    }

    return retval;
}

static Bool
isSessionWindow (CompWindow *w)
{
    if (w->attrib.override_redirect)
	return FALSE;

    /* filter out embedded windows (notification icons) */
    if (sessionGetIsEmbedded (w->screen->display, w->id))
	return FALSE;

    return TRUE;
}

static void
sessionWriteWindow (CompWindow *w,
		    FILE       *outfile)
{
    char *string, *clientId, *command;
    int  x, y;

    SESSION_DISPLAY (w->screen->display);

    clientId = sessionGetClientLeaderProperty (w, sd->clientIdAtom);
    if (!clientId && !sd->opt[SESSION_DISPLAY_OPTION_SAVE_LEGACY].value.b)
	return;

    command  = sessionGetClientLeaderProperty (w, sd->commandAtom);

    if (!clientId && !command)
	return;

    fprintf (outfile, "  <window ");
    if (clientId)
    {
	fprintf (outfile, "id=\"%s\"", clientId);
	free (clientId);
    }

    string = sessionGetWindowTitle (w);
    if (string)
    {
	fprintf (outfile, " title=\"%s\"", string);
	free (string);
    }

    if (w->resClass)
	fprintf (outfile, " class=\"%s\"", w->resClass);
    if (w->resName)
	fprintf (outfile, " name=\"%s\"", w->resName);

    string = sessionGetTextProperty (w->screen->display, w->id, sd->roleAtom);
    if (string)
    {
	fprintf (outfile, " role=\"%s\"", string);
	free (string);
    }

    if (command)
    {
	fprintf (outfile, " command=\"%s\"", command);
	free (command);
    }
    fprintf (outfile, ">\n");

    /* save geometry, relative to viewport 0, 0 */
    x = (w->saveMask & CWX) ? w->saveWc.x : w->serverX;
    y = (w->saveMask & CWY) ? w->saveWc.y : w->serverY;
    if (!windowOnAllViewports (w))
    {
	x += w->screen->x * w->screen->width;
	y += w->screen->y * w->screen->height;
    }

    fprintf (outfile,
	     "    <geometry x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\"/>\n",
	     x, y,
	     (w->saveMask & CWWidth) ? w->saveWc.width : w->serverWidth,
	     (w->saveMask & CWHeight) ? w->saveWc.height : w->serverHeight);

    /* save various window states */
    if (w->state & CompWindowStateShadedMask)
	fprintf (outfile, "    <shaded/>\n");
    if (w->state & CompWindowStateStickyMask)
	fprintf (outfile, "    <sticky/>\n");
    if (w->state & CompWindowStateFullscreenMask)
	fprintf (outfile, "    <fullscreen/>\n");
    if (w->minimized)
	fprintf (outfile, "    <minimized/>\n");
    if (w->state & MAXIMIZE_STATE)
    {
	fprintf (outfile, "    <maximized ");
	if (w->state & CompWindowStateMaximizedVertMask)
	    fprintf (outfile, "vert=\"yes\" ");
	if (w->state & CompWindowStateMaximizedHorzMask)
	    fprintf (outfile, "horiz=\"yes\"");
	fprintf (outfile, "/>\n");
    }

    /* save workspace */
    if (!(w->type & CompWindowTypeDesktopMask ||
	  w->type & CompWindowTypeDockMask))
	    fprintf (outfile, "    <workspace index=\"%d\"/>\n",
		     w->desktop);

    fprintf (outfile, "  </window>\n");
}

static void
saveState (CompDisplay *d,
	   const char  *clientId)
{
    char           filename[1024];
    FILE          *outfile;
    struct passwd *p = getpwuid (geteuid ());
    CompScreen    *s;

    //setup filename and create directories as needed
    strncpy (filename, p->pw_dir, 1024);
    strncat (filename, "/.compiz", 1024);
    if (mkdir (filename, 0700) == 0 || errno == EEXIST)
    {
	strncat (filename, "/session", 1024);
	if (mkdir (filename, 0700) == 0 || errno == EEXIST)
	{
	    strncat (filename, "/", 1024);
	    strncat (filename, clientId, 1024);
	}
	else
	{
	    return;
	}
    }
    else
    {
	return;
    }

    outfile = fopen (filename, "w");
    if (!outfile)
	return;

    fprintf (outfile, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    fprintf (outfile, "<compiz_session id=\"%s\">\n", clientId);

    /* write out all windows on this display */
    for (s = d->screens; s; s = s->next)
    {
	CompWindow *w;

	for (w = s->windows; w; w = w->next)
	{
	    if (!isSessionWindow (w))
		continue;

	    /* skip invisible windows that we didn't unmap */
	    if (w->attrib.map_state != IsViewable &&
		!(w->minimized || w->shaded ||
		  w->inShowDesktopMode || w->hidden))
	    {
		continue;
	    }

	    sessionWriteWindow (w, outfile);
	}
    }

    fprintf (outfile, "</compiz_session>\n");
    fclose (outfile);
}

static Bool
sessionMatchWindowClass (CompWindow        *w,
			 SessionWindowList *info)
{
    if (!w->resName || !info->resName)
	return FALSE;

    if (!w->resClass || !info->resClass)
	return FALSE;

    if (strcmp (w->resName, info->resName) != 0)
	return FALSE;

    if (strcmp (w->resClass, info->resClass) != 0)
	return FALSE;

    return TRUE;
}

static void
sessionReadWindow (CompWindow *w)
{
    CompDisplay        *d = w->screen->display;
    XWindowChanges     xwc;
    unsigned int       xwcm = 0;
    char               *title, *role, *clientId, *command;
    SessionWindowList  *cur;

    SESSION_CORE (&core);
    SESSION_DISPLAY (d);

    /* optimization: don't mess around with getting X properties
       if there is nothing to match */
    if (!sc->windowList)
	return;

    if (!isSessionWindow (w))
	return;

    clientId = sessionGetClientLeaderProperty (w, sd->clientIdAtom);
    if (!clientId && !sd->opt[SESSION_DISPLAY_OPTION_SAVE_LEGACY].value.b)
	return;

    command  = sessionGetClientLeaderProperty (w, sd->commandAtom);
    title    = sessionGetWindowTitle (w);
    role     = sessionGetTextProperty (d, w->id, sd->roleAtom);

    for (cur = sc->windowList; cur; cur = cur->next)
    {
	if (clientId && cur->clientId && strcmp (clientId, cur->clientId) == 0)
	{
	    /* try to match role as well if possible (see ICCCM 5.1) */
	    if (role && cur->role)
	    {
		if (strcmp (role, cur->role) == 0)
		    break;
	    }
	    else
	    {
		if (sessionMatchWindowClass (w, cur))
		    break;
	    }
	}
	else if (sd->opt[SESSION_DISPLAY_OPTION_SAVE_LEGACY].value.b)
	{
	    if (cur->command && command && sessionMatchWindowClass (w, cur))
	    {
		/* match by command, class and name as second try */
		break;
	    }
	    else if (title && cur->title && strcmp (title, cur->title) == 0)
	    {
		/* last resort: match by window title */
		break;
	    }
	}
    }

    if (clientId)
	free (clientId);
    if (command)
	free (command);
    if (title)
	free (title);
    if (role)
	free (role);

    if (cur)
    {
	/* found a window */
	if (cur->geometryValid)
	{
	    xwcm = CWX | CWY | CWWidth | CWHeight;

	    xwc.x = cur->geometry.x;
	    xwc.y = cur->geometry.y;
	    xwc.width = cur->geometry.width;
	    xwc.height = cur->geometry.height;

	    configureXWindow (w, xwcm, &xwc);
	    w->placed = TRUE;
	}

	if (cur->minimized)
	    minimizeWindow (w);

	if (cur->workspace != -1)
	    setDesktopForWindow (w, cur->workspace);

	if (cur->state)
	{
	    changeWindowState (w, w->state | cur->state);
	    recalcWindowType (w);
	    recalcWindowActions (w);
	    updateWindowAttributes (w, CompStackingUpdateModeNone);
	}

	/* remove item from list */
	sessionRemoveWindowListItem (cur);
    }
}

static void
readState (xmlNodePtr root)
{
    xmlNodePtr cur, attrib;

    for (cur = root->xmlChildrenNode; cur; cur = cur->next)
    {
	SessionWindowList *item;
	
	item = calloc (1, sizeof (SessionWindowList));
	if (!item)
	    return;
	item->geometryValid = FALSE;

	if (xmlStrcmp (cur->name, BAD_CAST "window") == 0)
	{
	    item->clientId = sessionGetStringForProp (cur, "id");
	    item->title = sessionGetStringForProp (cur, "title");
	    item->resName = sessionGetStringForProp (cur, "name");
	    item->resClass = sessionGetStringForProp (cur, "class");
	    item->role = sessionGetStringForProp (cur, "role");
	    item->command = sessionGetStringForProp (cur, "command");
	}

	if (!item->clientId && !item->title &&
	    (!item->resName || !item->resClass))
	{
	    free (item);
	    continue;
	}

	for (attrib = cur->xmlChildrenNode; attrib; attrib = attrib->next)
	{
	    if (xmlStrcmp (attrib->name, BAD_CAST "geometry") == 0)
	    {
		item->geometryValid = TRUE;
		item->geometry.x = sessionGetIntForProp (attrib, "x");
		item->geometry.y = sessionGetIntForProp (attrib, "y");
		item->geometry.width  = sessionGetIntForProp (attrib, "width");
		item->geometry.height = sessionGetIntForProp (attrib, "height");
	    }

	    if (xmlStrcmp (attrib->name, BAD_CAST "shaded") == 0)
		item->state |= CompWindowStateShadedMask;
	    if (xmlStrcmp (attrib->name, BAD_CAST "sticky") == 0)
		item->state |= CompWindowStateStickyMask;
	    if (xmlStrcmp (attrib->name, BAD_CAST "fullscreen") == 0)
		item->state |= CompWindowStateFullscreenMask;
	    if (xmlStrcmp (attrib->name, BAD_CAST "minimized") == 0)
		item->minimized = TRUE;

	    if (xmlStrcmp (attrib->name, BAD_CAST "maximized") == 0)
	    {
		xmlChar *vert, *horiz;
		vert = xmlGetProp (attrib, BAD_CAST "vert");
		if (vert)
		{
		    item->state |= CompWindowStateMaximizedVertMask;
		    xmlFree (vert);
		}

		horiz = xmlGetProp (attrib, BAD_CAST "horiz");
		if (horiz)
		{
		    item->state |= CompWindowStateMaximizedHorzMask;
		    xmlFree (horiz);
		}
	    }

	    if (xmlStrcmp (attrib->name, BAD_CAST "workspace") == 0)
	    {
		int desktop;
		
		desktop = sessionGetIntForProp (attrib, "index");
		item->workspace = desktop;
	    }
	}

	sessionAddWindowListItem (item);
    }
}

static void
loadState (CompDisplay *d,
	   char        *previousId)
{
    xmlDocPtr          doc;
    xmlNodePtr         root;
    char               filename[1024];
    struct passwd *p = getpwuid (geteuid ());

    //setup filename and create directories as needed
    strncpy (filename, p->pw_dir, 1024);
    strncat (filename, "/.compiz/", 1024);
    strncat (filename, "session/", 1024);
    strncat (filename, previousId, 1024);

    doc = xmlParseFile (filename);
    if (!doc)
	return;

    root = xmlDocGetRootElement (doc);
    if (root && xmlStrcmp (root->name, BAD_CAST "compiz_session") == 0)
	readState (root);

    xmlFreeDoc (doc);
    xmlCleanupParser ();
}


static void
sessionWindowAdd (CompScreen *s,
		  CompWindow *w)
{
    if (!w->attrib.override_redirect && w->attrib.map_state == IsViewable)
    	sessionReadWindow (w);
}

static void
sessionHandleEvent (CompDisplay *d,
		    XEvent      *event)
{
    SESSION_DISPLAY (d);

    switch (event->type) {
    case MapRequest:
	{
	    CompWindow *w;
	    w = findWindowAtDisplay (d, event->xmaprequest.window);
	    if (w)
		sessionReadWindow (w);
	}
	break;
    }

    UNWRAP (sd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (sd, d, handleEvent, sessionHandleEvent);
}

static void
sessionSessionSaveYourself (CompCore   *c,
			    const char *clientId,
			    int         saveType,
			    int         interactStyle,
			    Bool        shutdown,
			    Bool        fast)
{
    CompObject *object;

    SESSION_CORE (c);

    object = compObjectFind (&c->base, COMP_OBJECT_TYPE_DISPLAY, NULL);
    if (object)
    {
	CompDisplay *d = (CompDisplay *) object;
	saveState (d, clientId);
    }

    UNWRAP (sc, c, sessionSaveYourself);
    (*c->sessionSaveYourself) (c, clientId, saveType,
			       interactStyle, shutdown, fast);
    WRAP (sc, c, sessionSaveYourself, sessionSessionSaveYourself);
}

static void
sessionObjectAdd (CompObject *parent,
		  CompObject *object)
{
    static ObjectAddProc dispTab[] = {
	(ObjectAddProc) 0, /* CoreAdd */
	(ObjectAddProc) 0, /* DisplayAdd */
	(ObjectAddProc) 0, /* ScreenAdd */
	(ObjectAddProc) sessionWindowAdd
    };

    SESSION_CORE (&core);

    UNWRAP (sc, &core, objectAdd);
    (*core.objectAdd) (parent, object);
    WRAP (sc, &core, objectAdd, sessionObjectAdd);
	
    DISPATCH (object, dispTab, ARRAY_SIZE (dispTab), (parent, object));
}

static Bool
sessionWindowAddTimeout (void *closure)
{
    CompDisplay *d = (CompDisplay *) closure;
    CompScreen  *s;
    CompWindow  *w;

    SESSION_DISPLAY (d);

    for (s = d->screens; s; s = s->next)
	for (w = s->windows; w; w = w->next)
	    sessionWindowAdd (s, w);

    sd->windowAddTimeout = 0;

    return FALSE;
}

static CompOption *
sessionGetDisplayOptions (CompPlugin  *p,
			  CompDisplay *d,
			  int         *count)
{
    SESSION_DISPLAY (d);

    *count = NUM_OPTIONS (sd);
    return sd->opt;
}

static Bool
sessionSetDisplayOption (CompPlugin      *p,
			 CompDisplay     *d,
			 const char      *name,
			 CompOptionValue *value)
{
    CompOption *o;

    SESSION_DISPLAY (d);

    o = compFindOption (sd->opt, NUM_OPTIONS (sd), name, NULL);
    if (!o)
	return FALSE;

    return compSetOption (o, value);
}

static CompOption *
sessionGetObjectOptions (CompPlugin *plugin,
			 CompObject *object,
			 int	    *count)
{
    static GetPluginObjectOptionsProc dispTab[] = {
	(GetPluginObjectOptionsProc) 0, /* GetCoreOptions */
	(GetPluginObjectOptionsProc) sessionGetDisplayOptions
    };

    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab),
		     (void *) (*count = 0), (plugin, object, count));
}

static CompBool
sessionSetObjectOption (CompPlugin      *plugin,
			CompObject      *object,
			const char      *name,
			CompOptionValue *value)
{
    static SetPluginObjectOptionProc dispTab[] = {
	(SetPluginObjectOptionProc) 0, /* SetCoreOption */
	(SetPluginObjectOptionProc) sessionSetDisplayOption
    };

    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab), FALSE,
		     (plugin, object, name, value));
}

static const CompMetadataOptionInfo sessionDisplayOptionInfo[] = {
    { "save_legacy", "bool", 0, 0, 0 }
};

static int
sessionInit (CompPlugin *p)
{
    if (!compInitPluginMetadataFromInfo (&sessionMetadata, p->vTable->name,
					 sessionDisplayOptionInfo,
					 SESSION_DISPLAY_OPTION_NUM,
					 0, 0))
	return FALSE;

    corePrivateIndex = allocateCorePrivateIndex ();
    if (corePrivateIndex < 0)
    {
	compFiniMetadata (&sessionMetadata);
	return FALSE;
    }

    compAddMetadataFromFile (&sessionMetadata, p->vTable->name);

    return TRUE;
}

static void
sessionFini (CompPlugin *p)
{
    freeCorePrivateIndex (corePrivateIndex);
    compFiniMetadata (&sessionMetadata);
}

static Bool
sessionInitCore (CompPlugin *p,
		 CompCore   *c)
{
    SessionCore *sc;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    sc = malloc (sizeof (SessionCore));
    if (!sc)
	return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
    {
	free (sc);
	return FALSE;
    }

    sc->windowList = NULL;

    /* FIXME: don't use WindowAdd for now as it's not called for windows
       that are present before Compiz start - TODO: find out why and remove
       the timeout hack */
    /* WRAP (sc, c, objectAdd, sessionObjectAdd); */
    WRAP (sc, c, sessionSaveYourself, sessionSessionSaveYourself);

    c->base.privates[corePrivateIndex].ptr = sc;

    return TRUE;
}

static void
sessionFiniCore (CompPlugin *p,
		 CompCore   *c)
{
    SessionWindowList *run, *next;

    SESSION_CORE (c);

    freeDisplayPrivateIndex (displayPrivateIndex);

    /* FIXME: see above */
    /* UNWRAP (sc, c, objectAdd); */
    UNWRAP (sc, c, sessionSaveYourself);

    run = sc->windowList;
    while (run)
    {
	next = run->next;
	sessionFreeWindowListItem (run);
	run = next;
    }

    free (sc);
}

static int
sessionInitDisplay (CompPlugin  *p,
		    CompDisplay *d)
{
    SessionDisplay *sd;
    char           *previousId = NULL;
    int            i;

    sd = malloc (sizeof (SessionDisplay));
    if (!sd)
	return FALSE;

    if (!compInitDisplayOptionsFromMetadata (d,
					     &sessionMetadata,
					     sessionDisplayOptionInfo,
					     sd->opt,
					     SESSION_DISPLAY_OPTION_NUM))
    {
	compFiniDisplayOptions (d, sd->opt, SESSION_DISPLAY_OPTION_NUM);
	free (sd);
	return FALSE;
    }

    d->base.privates[displayPrivateIndex].ptr = sd;

    sd->visibleNameAtom = XInternAtom (d->display, "_NET_WM_VISIBLE_NAME", 0);
    sd->clientIdAtom = XInternAtom (d->display, "SM_CLIENT_ID", 0);
    sd->embedInfoAtom = XInternAtom (d->display, "_XEMBED_INFO", 0);
    sd->roleAtom = XInternAtom (d->display, "WM_WINDOW_ROLE", 0);
    sd->commandAtom = XInternAtom (d->display, "WM_COMMAND", 0);

    for (i = 0; i < programArgc; i++)
    {
	if (strcmp (programArgv[i], "--sm-disable") == 0)
	{
	    if (previousId)
	    {
		free (previousId);
		previousId = NULL;
		break;
	    }
	}
	else if (strcmp (programArgv[i], "--sm-client-id") == 0)
	{
	    previousId = strdup (programArgv[i + 1]);
	}
    }

    if (previousId)
    {
	loadState (d, previousId);
	free (previousId);
    }

    sd->windowAddTimeout = compAddTimeout (0, sessionWindowAddTimeout, d);

    WRAP (sd, d, handleEvent, sessionHandleEvent);

    d->base.privates[displayPrivateIndex].ptr = sd;

    return TRUE;
}

static void
sessionFiniDisplay (CompPlugin  *p,
		    CompDisplay *d)
{
    SESSION_DISPLAY (d);

    UNWRAP (sd, d, handleEvent);

    if (sd->windowAddTimeout)
	compRemoveTimeout (sd->windowAddTimeout);

    compFiniDisplayOptions (d, sd->opt, SESSION_DISPLAY_OPTION_NUM);

    free (sd);
}

static CompBool
sessionInitObject (CompPlugin *p,
		   CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) sessionInitCore,
	(InitPluginObjectProc) sessionInitDisplay
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
sessionFiniObject (CompPlugin *p,
		   CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) sessionFiniCore,
	(FiniPluginObjectProc) sessionFiniDisplay
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static CompMetadata *
sessionGetMetadata (CompPlugin *plugin)
{
    return &sessionMetadata;
}

static CompPluginVTable sessionVTable =
{
    "session",
    sessionGetMetadata,
    sessionInit,
    sessionFini,
    sessionInitObject,
    sessionFiniObject,
    sessionGetObjectOptions,
    sessionSetObjectOption
};

CompPluginVTable *
getCompPluginInfo20070830 (void)
{
    return &sessionVTable;
}

