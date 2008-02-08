/*
 * Copyright (c) 2006 Darryll Truchan <moppsy@comcast.net>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>

#include <compiz-core.h>


#define BS_SATURATION_STEP_DEFAULT                  5
#define BS_BRIGHTNESS_STEP_DEFAULT                  5

#define BS_DISPLAY_OPTION_SATURATION_INCREASE_KEY	0
#define BS_DISPLAY_OPTION_SATURATION_INCREASE_BUTTON	1
#define BS_DISPLAY_OPTION_SATURATION_DECREASE_KEY	2
#define BS_DISPLAY_OPTION_SATURATION_DECREASE_BUTTON	3
#define BS_DISPLAY_OPTION_BRIGHTNESS_INCREASE_KEY	4
#define BS_DISPLAY_OPTION_BRIGHTNESS_INCREASE_BUTTON	5
#define BS_DISPLAY_OPTION_BRIGHTNESS_DECREASE_KEY	6
#define BS_DISPLAY_OPTION_BRIGHTNESS_DECREASE_BUTTON	7
#define BS_DISPLAY_OPTION_NUM				8

#define BS_SCREEN_OPTION_SATURATION_STEP        0
#define BS_SCREEN_OPTION_BRIGHTNESS_STEP        1
#define BS_SCREEN_OPTION_SATURATION_MATCHES     2
#define BS_SCREEN_OPTION_SATURATION_VALUES      3
#define BS_SCREEN_OPTION_BRIGHTNESS_MATCHES     4
#define BS_SCREEN_OPTION_BRIGHTNESS_VALUES      5
#define BS_SCREEN_OPTION_NUM                    6

static int displayPrivateIndex;

static CompMetadata bsMetadata;

typedef struct _BSDisplay
{
    int screenPrivateIndex;
    HandleEventProc handleEvent;
    CompOption opt[BS_DISPLAY_OPTION_NUM];
} BSDisplay;


typedef struct _BSScreen
{
    int windowPrivateIndex;

    int brightnessStep;
    int saturationStep;

    int  brightnessFactor;
    int  saturationFactor;

/* TODO Add this to core (look at opacityPropSet) ?
    Bool saturationPropSet;
    Bool brightnessPropSet;
*/
    PaintWindowProc        paintWindow;

    CompOption opt[BS_SCREEN_OPTION_NUM];
} BSScreen;

typedef struct _BSWindow
{
    int brightness;
    int saturation;
} BSWindow;

#define GET_BS_DISPLAY(d) ((BSDisplay *) (d)->base.privates[displayPrivateIndex].ptr)
#define BS_DISPLAY(d) BSDisplay *bd = GET_BS_DISPLAY (d)
#define GET_BS_SCREEN(s, bd) ((BSScreen *) (s)->base.privates[(bd)->screenPrivateIndex].ptr)
#define BS_SCREEN(s) BSScreen *bs = GET_BS_SCREEN (s, GET_BS_DISPLAY (s->display))
#define GET_BS_WINDOW(w, bs) ((BSWindow *) (w)->base.privates[(bs)->windowPrivateIndex].ptr)
#define BS_WINDOW(w)                                       \
    BSWindow *bw = GET_BS_WINDOW  (w,                    \
                     GET_BS_SCREEN  (w->screen,            \
                     GET_BS_DISPLAY (w->screen->display)))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static void
changeWindowSaturation (CompWindow * w, int direction)
{
    int step, saturation;

    BS_SCREEN (w->screen);
    BS_WINDOW (w);

    if (w->attrib.override_redirect)
	return;

    if (w->type & CompWindowTypeDesktopMask)
	return;

    step = (COLOR * bs->saturationStep) / 100;
    saturation = bw->saturation;

    saturation = saturation + step * direction;
    if (saturation > COLOR)
    {
	saturation = COLOR;
    }
    else if (saturation < step)
    {
	saturation = step;
    }

    if (bw->saturation != saturation)
    {
	bw->saturation = saturation;
	addWindowDamage (w);
    }
}

static void
changeWindowBrightness (CompWindow * w, int direction)
{
    int step, brightness;

    BS_SCREEN (w->screen);
    BS_WINDOW (w);

    if (w->attrib.override_redirect)
	return;

    if (w->type & CompWindowTypeDesktopMask)
	return;

    step = (BRIGHT * bs->brightnessStep) / 100;
    brightness = bw->brightness;

    brightness = brightness + step * direction;
    if (brightness > BRIGHT)
    {
	brightness = BRIGHT;
    }
    else if (brightness < step)
    {
	brightness = step;
    }

    if (bw->brightness != brightness)
    {
	bw->brightness = brightness;
	addWindowDamage (w);
    }
}

static void
updateWindowSaturation (CompWindow *w)
{
    int	saturation;

    BS_SCREEN (w->screen);
    BS_WINDOW (w);

    saturation = bw->saturation;

    if (!(w->type & CompWindowTypeDesktopMask))
    {
	CompOption *matches = &bs->opt[BS_SCREEN_OPTION_SATURATION_MATCHES];
	CompOption *values = &bs->opt[BS_SCREEN_OPTION_SATURATION_VALUES];
	int	   i, min;

	min = MIN (matches->value.list.nValue, values->value.list.nValue);

	for (i = 0; i < min; i++)
	{
	    if (matchEval (&matches->value.list.value[i].match, w))
	    {
		saturation = (values->value.list.value[i].i * COLOR) / 100;
		break;
	    }
	}
    }

    if (saturation != w->paint.saturation)
    {
	bw->saturation = saturation;
	addWindowDamage (w);
    }
}

static void
updateWindowBrightness (CompWindow *w)
{
    int	brightness;

    BS_SCREEN (w->screen);
    BS_WINDOW (w);

    brightness = bw->brightness;

    if (!(w->type & CompWindowTypeDesktopMask))
    {
	CompOption *matches = &bs->opt[BS_SCREEN_OPTION_BRIGHTNESS_MATCHES];
	CompOption *values = &bs->opt[BS_SCREEN_OPTION_BRIGHTNESS_VALUES];
	int	   i, min;

	min = MIN (matches->value.list.nValue, values->value.list.nValue);
	
	for (i = 0; i < min; i++)
	{
	    if (matchEval (&matches->value.list.value[i].match, w))
	    {
		brightness = (values->value.list.value[i].i * BRIGHT) / 100;
		break;
	    }
	}
    }

    if (brightness != w->paint.brightness)
    {
	bw->brightness = brightness;
	addWindowDamage (w);
    }
}

static Bool
alterSaturation (CompDisplay * d, CompAction * action,
                    CompActionState state, CompOption * option, int nOption)
{
    CompWindow *w;
    Window xid;

    xid = getIntOptionNamed (option, nOption, "window", 0);

    w = findTopLevelWindowAtDisplay (d, xid);
    if (w)
        changeWindowSaturation (w, action->priv.val);

    return TRUE;
}

static Bool
alterBrightness (CompDisplay * d, CompAction * action,
                    CompActionState state, CompOption * option, int nOption)
{
    CompWindow *w;
    Window xid;

    xid = getIntOptionNamed (option, nOption, "window", 0);

    w = findTopLevelWindowAtDisplay (d, xid);
    if (w)
        changeWindowBrightness (w, action->priv.val);

    return TRUE;
}

static CompOption *
BSGetDisplayOptions (CompPlugin *p, CompDisplay * display, int *count)
{
        BS_DISPLAY (display);

        *count = NUM_OPTIONS (bd);
        return bd->opt;
}

static Bool
BSSetDisplayOption (CompPlugin *p, CompDisplay * display, char *name,
                    CompOptionValue * value)
{
    CompOption *o;
    int index;

    BS_DISPLAY (display);

    o = compFindOption (bd->opt, NUM_OPTIONS (bd), name, &index);
    if (!o)
        return FALSE;

    switch (index)
    {
    case BS_DISPLAY_OPTION_SATURATION_INCREASE_BUTTON:
    case BS_DISPLAY_OPTION_SATURATION_DECREASE_BUTTON:
    case BS_DISPLAY_OPTION_BRIGHTNESS_INCREASE_BUTTON:
    case BS_DISPLAY_OPTION_BRIGHTNESS_DECREASE_BUTTON:
        if (setDisplayAction (display, o, value))
            return TRUE;
        break;

    default:
        break;
    }
    return FALSE;
}

static CompOption *
BSGetScreenOptions (CompPlugin *p, CompScreen * screen, int *count)
{
    BS_SCREEN (screen);

    *count = NUM_OPTIONS (bs);
    return bs->opt;
}

static Bool
BSSetScreenOption (CompPlugin *p, CompScreen * screen, char *name, CompOptionValue * value)
{
    CompOption *o;
    int index;

    BS_SCREEN (screen);

    o = compFindOption (bs->opt, NUM_OPTIONS (bs), name, &index);
    if (!o)
        return FALSE;

    switch (index)
    {
    case BS_SCREEN_OPTION_BRIGHTNESS_STEP:
        if (compSetIntOption (o, value))
        {
            bs->brightnessStep = o->value.i;
            return TRUE;
        }
        break;

    case BS_SCREEN_OPTION_SATURATION_STEP:
        if (compSetIntOption (o, value))
        {
            bs->saturationStep = o->value.i;
            return TRUE;
        }
        break;
    case BS_SCREEN_OPTION_SATURATION_MATCHES:
	if (compSetOptionList (o, value))
	{
	    CompWindow *w;
	    int	       i;

	    for (i = 0; i < o->value.list.nValue; i++)
		matchUpdate (screen->display, &o->value.list.value[i].match);

	    for (w = screen->windows; w; w = w->next)
		updateWindowSaturation (w);

	    return TRUE;
	}
	break;
    case BS_SCREEN_OPTION_SATURATION_VALUES:
	if (compSetOptionList (o, value))
	{
	    CompWindow *w;

	    for (w = screen->windows; w; w = w->next)
		updateWindowSaturation (w);

	    return TRUE;
	}
    case BS_SCREEN_OPTION_BRIGHTNESS_MATCHES:
	if (compSetOptionList (o, value))
	{
	    CompWindow *w;
	    int	       i;

	    for (i = 0; i < o->value.list.nValue; i++)
		matchUpdate (screen->display, &o->value.list.value[i].match);

	    for (w = screen->windows; w; w = w->next)
		updateWindowBrightness (w);

	    return TRUE;
	}
	break;
    case BS_SCREEN_OPTION_BRIGHTNESS_VALUES:
	if (compSetOptionList (o, value))
	{
	    CompWindow *w;

	    for (w = screen->windows; w; w = w->next)
		updateWindowBrightness (w);

	    return TRUE;
	}
    default:
        break;
    }
    return FALSE;
}

static Bool
BSPaintWindow (CompWindow              *w,
	       const WindowPaintAttrib *attrib,
	       const CompTransform     *transform,
	       Region                  region,
	       unsigned int            mask)
{
    Bool status;

    BS_SCREEN (w->screen);
    BS_WINDOW (w);

    WindowPaintAttrib wAttrib = *attrib;

    if (w->paint.saturation != bw->saturation)
	wAttrib.saturation = bw->saturation;

    if (w->paint.brightness != bw->brightness)
	wAttrib.brightness = bw->brightness;

    UNWRAP (bs, w->screen, paintWindow);
    status = (*w->screen->paintWindow) (w, &wAttrib, transform, region, mask);
    WRAP (bs, w->screen, paintWindow, BSPaintWindow);

    return status;
}

static const CompMetadataOptionInfo bsDisplayOptionInfo[] = {
    { "saturation_increase_key", "key", 0, alterSaturation, 0 },
    { "saturation_increase_button", "button", 0, alterSaturation, 0 },
    { "saturation_decrease_key", "key", 0, alterSaturation, 0 },
    { "saturation_decrease_button", "button", 0, alterSaturation, 0 },
    { "brightness_increase_key", "key", 0, alterBrightness, 0 },
    { "brightness_increase_button", "button", 0, alterBrightness, 0 },
    { "brightness_decrease_key", "key", 0, alterBrightness, 0 },
    { "brightness_decrease_button", "button", 0, alterBrightness, 0 }
};

static Bool
BSInitDisplay (CompPlugin * p, CompDisplay * d)
{
    BSDisplay *bd;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    bd = malloc (sizeof (BSDisplay));
    if (!bd)
        return FALSE;

    if (!compInitDisplayOptionsFromMetadata (d,
					     &bsMetadata,
					     bsDisplayOptionInfo,
					     bd->opt,
					     BS_DISPLAY_OPTION_NUM))
    {
	free (bd);
	return FALSE;
    }

    bd->opt[BS_DISPLAY_OPTION_SATURATION_INCREASE_KEY].value.action.priv.val = 1;
    bd->opt[BS_DISPLAY_OPTION_SATURATION_DECREASE_KEY].value.action.priv.val = -1;
    bd->opt[BS_DISPLAY_OPTION_BRIGHTNESS_INCREASE_KEY].value.action.priv.val = 1;
    bd->opt[BS_DISPLAY_OPTION_BRIGHTNESS_DECREASE_KEY].value.action.priv.val = -1;
    bd->opt[BS_DISPLAY_OPTION_SATURATION_INCREASE_BUTTON].value.action.priv.val = 1;
    bd->opt[BS_DISPLAY_OPTION_SATURATION_DECREASE_BUTTON].value.action.priv.val = -1;
    bd->opt[BS_DISPLAY_OPTION_BRIGHTNESS_INCREASE_BUTTON].value.action.priv.val = 1;
    bd->opt[BS_DISPLAY_OPTION_BRIGHTNESS_DECREASE_BUTTON].value.action.priv.val = -1;

    bd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (bd->screenPrivateIndex < 0)
    {
	compFiniDisplayOptions (d, bd->opt, BS_DISPLAY_OPTION_NUM);
        free (bd);
        return FALSE;
    }

    d->base.privates[displayPrivateIndex].ptr = bd;

    return TRUE;
}

static void
BSFiniDisplay (CompPlugin * p, CompDisplay * d)
{
    BS_DISPLAY (d);

    freeScreenPrivateIndex (d, bd->screenPrivateIndex);

    compFiniDisplayOptions (d, bd->opt, BS_DISPLAY_OPTION_NUM);

    free (bd);
}

static const CompMetadataOptionInfo bsScreenOptionInfo[] = {
    { "brightness_step", "int", 0, 0, 0 },
    { "saturation_step", "int", 0, 0, 0 },
    { "saturation_matches", "list", "<type>match</type>", 0, 0 },
    { "saturation_values", "list", "<type>int</type>", 0, 0 },
    { "brightness_matches", "list", "<type>match</type>", 0, 0 },
    { "brightness_values", "list", "<type>int</type>", 0, 0 }
};

static Bool
BSInitScreen (CompPlugin * p, CompScreen * s)
{
    BSScreen *bs;
    BS_DISPLAY (s->display);

    bs = malloc (sizeof (BSScreen));
    if (!bs)
        return FALSE;

    if (!compInitScreenOptionsFromMetadata (s,
					    &bsMetadata,
					    bsScreenOptionInfo,
					    bs->opt,
					    BS_SCREEN_OPTION_NUM))
    {
	free (bs);
	return FALSE;
    }

    bs->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (bs->windowPrivateIndex < 0)
    {
	compFiniScreenOptions (s, bs->opt, BS_SCREEN_OPTION_NUM);
        free (bs);
        return FALSE;
    }

    bs->saturationStep = BS_SATURATION_STEP_DEFAULT;
    bs->brightnessStep = BS_BRIGHTNESS_STEP_DEFAULT;
    bs->saturationFactor = COLOR;
    bs->brightnessFactor = COLOR;

    s->base.privates[bd->screenPrivateIndex].ptr = bs;

    WRAP (bs, s, paintWindow, BSPaintWindow);

    return TRUE;
}

static void
BSFiniScreen (CompPlugin * p, CompScreen * s)
{
    BS_SCREEN (s);

    compFiniScreenOptions (s, bs->opt, BS_SCREEN_OPTION_NUM);

    UNWRAP (bs, s, paintWindow);

    damageScreen (s);

    free (bs);
}

static Bool
BSInitWindow (CompPlugin *p,
              CompWindow *w)
{
    BSWindow *bw;

    BS_SCREEN (w->screen);

    bw = malloc (sizeof (BSWindow));
    if (!bw)
        return FALSE;

    w->base.privates[bs->windowPrivateIndex].ptr = bw;

    bw->brightness = BRIGHT;
    bw->saturation = COLOR;

    updateWindowBrightness (w);
    updateWindowSaturation (w);

    return TRUE;
}

static void
BSFiniWindow (CompPlugin *p,
              CompWindow *w)
{
    BS_WINDOW (w);
    free (bw);
}

static CompBool

BSInitObject (CompPlugin *p,
              CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
       (InitPluginObjectProc) 0, /* FiniCore */
       (InitPluginObjectProc) BSInitDisplay,
       (InitPluginObjectProc) BSInitScreen,
       (InitPluginObjectProc) BSInitWindow
	};

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));

}

static void
BSFiniObject (CompPlugin *p,
               CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
       (FiniPluginObjectProc) 0, /* FiniCore */
       (FiniPluginObjectProc) BSFiniDisplay,
       (FiniPluginObjectProc) BSFiniScreen,
       (FiniPluginObjectProc) BSFiniWindow

    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static CompOption *
BSGetObjectOptions (CompPlugin *plugin,
                          CompObject *object,

                          int        *count)

{
    static GetPluginObjectOptionsProc dispTab[] = {
       (GetPluginObjectOptionsProc) 0, /*GetCoreOptions*/
       (GetPluginObjectOptionsProc) BSGetDisplayOptions,
       (GetPluginObjectOptionsProc) BSGetScreenOptions
    };


    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab),
                    (void *) (*count = 0), (plugin, object, count));

}

static CompBool
BSSetObjectOption (CompPlugin      *plugin,
                         CompObject      *object,
                         const char      *name,
                         CompOptionValue *value)

{
    static SetPluginObjectOptionProc dispTab[] = {
       (SetPluginObjectOptionProc) 0, /*SetCoreOption*/
       (SetPluginObjectOptionProc) BSSetDisplayOption,
       (SetPluginObjectOptionProc) BSSetScreenOption

    };

    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab), FALSE,
                    (plugin, object, name, value));
}

static Bool
BSInit (CompPlugin * p)
{
    if (!compInitPluginMetadataFromInfo (&bsMetadata,
					 p->vTable->name,
					 bsDisplayOptionInfo,
					 BS_DISPLAY_OPTION_NUM,
					 bsScreenOptionInfo,
					 BS_SCREEN_OPTION_NUM))
	return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
    {
	compFiniMetadata (&bsMetadata);
	return FALSE;
    }

    compAddMetadataFromFile (&bsMetadata, p->vTable->name);

    return TRUE;
}

static void
BSFini (CompPlugin * p)
{
    if (displayPrivateIndex >= 0)
        freeDisplayPrivateIndex (displayPrivateIndex);

    compFiniMetadata (&bsMetadata);
}

static CompMetadata *
BSGetMetadata (CompPlugin *plugin)
{
    return &bsMetadata;
}

CompPluginVTable BSVTable = {
    "bs",
    BSGetMetadata,
    BSInit,
    BSFini,
    BSInitObject,
    BSFiniObject,
    BSGetObjectOptions,
    BSSetObjectOption
};

CompPluginVTable *
getCompPluginInfo20070830 (void)
{
    return &BSVTable;
}
