/*
 * Copyright (C) 2016 Auboyneau Vincent <ksamak@riseup.net>
 *
 *   This file is part of compiz.
 *
 *   this program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by the Free
 *   Software Foundation, either version 3 of the License, or (at your option) any
 *   later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *   details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on mousepoll:
 *
 * Copyright : (C) 2008 by Dennis Kasprzyk
 * E-mail    : onestone@opencompositing.org
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
 */

#include <compiz-core.h>

#include <compiz-focuspoll.h>
#include <accessibilitywatcher.h>

static CompMetadata focuspollMetadata;

static int displayPrivateIndex;
static int functionsPrivateIndex;

typedef struct _FocuspollClient FocuspollClient;

struct _FocuspollClient {
    FocuspollClient *next;
    FocuspollClient *prev;

    FocusPollingHandle id;
    FocusUpdateProc    update;
};

typedef enum _FocuspollDisplayOptions
{
    FP_DISPLAY_OPTION_ABI,
    FP_DISPLAY_OPTION_INDEX,
    FP_DISPLAY_OPTION_IGNORE_LINKS,
    FP_DISPLAY_OPTION_FOCUS_POLL_INTERVAL,
    FP_DISPLAY_OPTION_NUM
} FocuspollDisplayOptions;

typedef struct _FocuspollDisplay {
    int	screenPrivateIndex;

    CompOption opt[FP_DISPLAY_OPTION_NUM];
} FocuspollDisplay;

typedef struct _FocuspollScreen {

    FocuspollClient       *clients;
    FocusPollingHandle freeId;

    CompTimeoutHandle updateHandle;

    AccessibilityWatcher* a11ywatcher;
} FocuspollScreen;

#define GET_FOCUSPOLL_DISPLAY(d)				      \
    ((FocuspollDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define FOCUSPOLL_DISPLAY(d)		           \
    FocuspollDisplay *fd = GET_FOCUSPOLL_DISPLAY (d)

#define GET_FOCUSPOLL_SCREEN(s, fd)				         \
    ((FocuspollScreen *) (s)->base.privates[(fd)->screenPrivateIndex].ptr)

#define FOCUSPOLL_SCREEN(s)						        \
    FocuspollScreen *fs = GET_FOCUSPOLL_SCREEN (s, GET_FOCUSPOLL_DISPLAY (s->display))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static Bool
updatePosition (void *c)
{
    CompScreen      *s = (CompScreen *)c;
    FocuspollClient *fc;

    FOCUSPOLL_SCREEN (s);

    auto queue = fs->a11ywatcher->getFocusQueue ();
    FocusEventNode *cqueue = NULL;
    for (auto fit = queue.begin(); fit != queue.end(); ++fit)
    {
	auto current = new FocusEventNode;
	current->next = cqueue;
	cqueue = current;
	auto info = *fit;
	CompRect focusRect = info->getBBox ();
	current->type = info->type;
	current->x = focusRect.x;
	current->y = focusRect.y;
	current->width = focusRect.width;
	current->height = focusRect.height;
    }

    FocuspollClient *next;
    for (fc = fs->clients; fc; fc = next)
    {
	next = fc->next;
	if (fc->update)
	    (*fc->update) (s, cqueue);
    }

    for (FocusEventNode *cur = cqueue; cur;)
    {
      auto next = cur->next;
      delete cur;
      cur = next;
    }

    fs->a11ywatcher->resetFocusQueue ();
    return TRUE;
}

static FocusPollingHandle
focuspollAddFocusPolling (CompScreen         *s,
			  FocusUpdateProc update)
{
    FOCUSPOLL_SCREEN  (s);
    FOCUSPOLL_DISPLAY (s->display);

    Bool start = FALSE;

    FocuspollClient *fc = (FocuspollClient *) malloc (sizeof (FocuspollClient));

    if (!fc)
	return -1;

    if (!fs->clients)
	start = TRUE;

    fc->update = update;
    fc->id     = fs->freeId;
    fs->freeId++;

    fc->prev = NULL;
    fc->next = fs->clients;

    if (fs->clients)
	fs->clients->prev = fc;

    fs->clients = fc;

    if (start)
    {
	fs->a11ywatcher->setActive (true);
	fs->updateHandle =
	    compAddTimeout (
		fd->opt[FP_DISPLAY_OPTION_FOCUS_POLL_INTERVAL].value.i / 2,
		fd->opt[FP_DISPLAY_OPTION_FOCUS_POLL_INTERVAL].value.i,
		updatePosition, s);
    }

    return fc->id;
}

static void
focuspollRemoveFocusPolling (CompScreen            *s,
			     FocusPollingHandle id)
{
    FOCUSPOLL_SCREEN (s);

    FocuspollClient *fc = fs->clients;

    if (fs->clients && fs->clients->id == id)
    {
	fs->clients = fs->clients->next;
	if (fs->clients)
	    fs->clients->prev = NULL;

	free (fc);
	return;
    }

    for (fc = fs->clients; fc; fc = fc->next)
	if (fc->id == id)
	{
	    if (fc->next)
		fc->next->prev = fc->prev;
	    if (fc->prev)
		fc->prev->next = fc->next;
	    free (fc);
	    return;
	}

    if (!fs->clients && fs->updateHandle)
    {
	fs->a11ywatcher->setActive (false);
	compRemoveTimeout (fs->updateHandle);
	fs->updateHandle = 0;
    }
}

static CompSize
getScreenLimits (CompScreen *s) {
    int x =0, y = 0;
    unsigned out;
    for (out = 0; out < s->nOutputDev; out++) {
	x =  std::max (x, s->outputDev[out].region.extents.x1 + s->outputDev[out].width);
	y =  std::max (y, s->outputDev[out].region.extents.y1 + s->outputDev[out].height);
    }
    return CompSize (x, y);
}

static const CompMetadataOptionInfo focuspollDisplayOptionInfo[] = {
    { "abi", "int", 0, 0, 0 },
    { "index", "int", 0, 0, 0 },
    { "ignore_links", "bool", 0, 0, 0 },
    { "focus_poll_interval", "int", "<min>1</min><max>500</max><default>10</default>", 0, 0 }
};

static CompOption *
focuspollGetDisplayOptions (CompPlugin  *plugin,
			    CompDisplay *display,
			    int         *count)
{
    FOCUSPOLL_DISPLAY (display);
    *count = NUM_OPTIONS (fd);
    return fd->opt;
}

static Bool
focuspollSetDisplayOption (CompPlugin      *plugin,
			   CompDisplay     *display,
			   const char      *name,
			   CompOptionValue *value)
{
    CompOption      *o;
    CompScreen      *s;
    FocuspollScreen *fs;
    int	            index;
    Bool            status = FALSE;
    FOCUSPOLL_DISPLAY (display);
    o = compFindOption (fd->opt, NUM_OPTIONS (fd), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case FP_DISPLAY_OPTION_ABI:
    case FP_DISPLAY_OPTION_INDEX:
        break;
    case FP_DISPLAY_OPTION_IGNORE_LINKS:
	for (s = display->screens; s; s = s->next)
	{
	    fs = GET_FOCUSPOLL_SCREEN (s, fd);
	    fs->a11ywatcher->setIgnoreLinks (fd->opt[FP_DISPLAY_OPTION_IGNORE_LINKS].value.b);
	}
	break;
    case FP_DISPLAY_OPTION_FOCUS_POLL_INTERVAL:
	status = compSetDisplayOption (display, o, value);
	for (s = display->screens; s; s = s->next)
	{
	    fs = GET_FOCUSPOLL_SCREEN (s, fd);
	    if (fs->updateHandle)
	    {
		compRemoveTimeout (fs->updateHandle);
		fs->updateHandle =
		    compAddTimeout (
			fd->opt[FP_DISPLAY_OPTION_FOCUS_POLL_INTERVAL].value.i
			/ 2,
			fd->opt[FP_DISPLAY_OPTION_FOCUS_POLL_INTERVAL].value.i,
   			updatePosition, s);
	    }
	}
	return status;
	break;
    default:
        return compSetDisplayOption (display, o, value);
    }

    return FALSE;
}

static FocusPollFunc focuspollFunctions =
{
    .addFocusPolling    = focuspollAddFocusPolling,
    .removeFocusPolling = focuspollRemoveFocusPolling,
};

static Bool
focuspollInitDisplay (CompPlugin  *p,
		      CompDisplay *d)
{
    FocuspollDisplay *fd;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    fd = (FocuspollDisplay *) malloc (sizeof (FocuspollDisplay));
    if (!fd)
	return FALSE;
    if (!compInitDisplayOptionsFromMetadata (d,
					     &focuspollMetadata,
					     focuspollDisplayOptionInfo,
					     fd->opt,
					     FP_DISPLAY_OPTION_NUM))
    {
	free (fd);
	return FALSE;
    }

    fd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (fd->screenPrivateIndex < 0)
    {
	compFiniDisplayOptions (d, fd->opt, FP_DISPLAY_OPTION_NUM);
	free (fd);
	return FALSE;
    }

    fd->opt[FP_DISPLAY_OPTION_ABI].value.i   = FOCUSPOLL_ABIVERSION;
    fd->opt[FP_DISPLAY_OPTION_INDEX].value.i = functionsPrivateIndex;

    d->base.privates[displayPrivateIndex].ptr   = fd;
    d->base.privates[functionsPrivateIndex].ptr = &focuspollFunctions;
    return TRUE;
}

static void
focuspollFiniDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    FOCUSPOLL_DISPLAY (d);

    compFiniDisplayOptions (d, fd->opt, FP_DISPLAY_OPTION_NUM);
    free (fd);
}

static Bool
focuspollInitScreen (CompPlugin *p,
		     CompScreen *s)
{
    FocuspollScreen *fs;

    FOCUSPOLL_DISPLAY (s->display);

    fs = (FocuspollScreen *) malloc (sizeof (FocuspollScreen));
    if (!fs)
	return FALSE;

    fs->a11ywatcher = new AccessibilityWatcher ();

    CompSize screenLimit = getScreenLimits (s);
    fs->a11ywatcher->setScreenLimits (screenLimit.width, screenLimit.height);

    fs->clients = NULL;
    fs->freeId  = 1;

    fs->updateHandle = 0;

    s->base.privates[fd->screenPrivateIndex].ptr = fs;
    return TRUE;
}

static void
focuspollFiniScreen (CompPlugin *p,
		     CompScreen *s)
{
    FOCUSPOLL_SCREEN (s);

    delete fs->a11ywatcher;

    if (fs->updateHandle)
	compRemoveTimeout (fs->updateHandle);

    free (fs);
}

static CompBool
focuspollInitObject (CompPlugin *p,
		     CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) focuspollInitDisplay,
	(InitPluginObjectProc) focuspollInitScreen
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
focuspollFiniObject (CompPlugin *p,
		     CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) focuspollFiniDisplay,
	(FiniPluginObjectProc) focuspollFiniScreen
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
focuspollInit (CompPlugin *p)
{
    if (!compInitPluginMetadataFromInfo (&focuspollMetadata,
					 p->vTable->name,
					 focuspollDisplayOptionInfo,
					 FP_DISPLAY_OPTION_NUM,
					 NULL, 0))
	return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
    {
	compFiniMetadata (&focuspollMetadata);
	return FALSE;
    }

    functionsPrivateIndex = allocateDisplayPrivateIndex ();
    if (functionsPrivateIndex < 0)
    {
	freeDisplayPrivateIndex (displayPrivateIndex);
	compFiniMetadata (&focuspollMetadata);
	return FALSE;
    }

    compAddMetadataFromFile (&focuspollMetadata, p->vTable->name);
    return TRUE;
}

static CompOption *
focuspollGetObjectOptions (CompPlugin *plugin,
			   CompObject *object,
			   int        *count)
{
    static GetPluginObjectOptionsProc dispTab[] = {
	(GetPluginObjectOptionsProc) 0, /* GetCoreOptions */
	(GetPluginObjectOptionsProc) focuspollGetDisplayOptions
    };

    *count = 0;
    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab),
		     NULL, (plugin, object, count));
}

static CompBool
focuspollSetObjectOption (CompPlugin      *plugin,
			  CompObject      *object,
			  const char      *name,
			  CompOptionValue *value)
{
    static SetPluginObjectOptionProc dispTab[] = {
	(SetPluginObjectOptionProc) 0, /* SetCoreOption */
	(SetPluginObjectOptionProc) focuspollSetDisplayOption
    };

    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab), FALSE,
		     (plugin, object, name, value));
}

static void
focuspollFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
    freeDisplayPrivateIndex (functionsPrivateIndex);
    compFiniMetadata (&focuspollMetadata);
}

static CompMetadata *
focuspollGetMetadata (CompPlugin *plugin)
{
    return &focuspollMetadata;
}

CompPluginVTable focuspollVTable = {
    "focuspoll",
    focuspollGetMetadata,
    focuspollInit,
    focuspollFini,
    focuspollInitObject,
    focuspollFiniObject,
    focuspollGetObjectOptions,
    focuspollSetObjectOption
};

CompPluginVTable *
getCompPluginInfo20070830 (void)
{
    return &focuspollVTable;
}
