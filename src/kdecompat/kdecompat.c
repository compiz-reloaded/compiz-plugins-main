/*
 *
 * Compiz KDE compatibility plugin
 *
 * kdecompat.c
 *
 * Copyright : (C) 2007 by Danny Baumann
 * E-mail    : maniac@opencompositing.org
 *
 * Based on scale.c and switcher.c:
 * Copyright : (C) 2007 David Reveman
 * E-mail    : davidr@novell.com
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <X11/Xatom.h>

#include <compiz-core.h>
#include "kdecompat_options.h"

static int displayPrivateIndex;

typedef struct _KdeCompatDisplay {
    int		    screenPrivateIndex;
    HandleEventProc handleEvent;

    Atom kdePreviewAtom;
} KdeCompatDisplay;

typedef struct _KdeCompatScreen {
    int windowPrivateIndex;

    PaintWindowProc      paintWindow;
    DamageWindowRectProc damageWindowRect;
} KdeCompatScreen;

typedef struct _Thumb {
    Window     id;
    XRectangle thumb;
} Thumb;

typedef struct _KdeCompatWindow {
    Thumb        *previews;
    unsigned int nPreviews;
    Bool         isPreview;
} KdeCompatWindow;

#define GET_KDECOMPAT_DISPLAY(d)				      \
    ((KdeCompatDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define KDECOMPAT_DISPLAY(d)		     \
    KdeCompatDisplay *kd = GET_KDECOMPAT_DISPLAY (d)

#define GET_KDECOMPAT_SCREEN(s, kd)					  \
    ((KdeCompatScreen *) (s)->base.privates[(kd)->screenPrivateIndex].ptr)

#define KDECOMPAT_SCREEN(s)						   \
    KdeCompatScreen *ks = GET_KDECOMPAT_SCREEN (s, 			   \
			  GET_KDECOMPAT_DISPLAY (s->display))

#define GET_KDECOMPAT_WINDOW(w, ks)					  \
    ((KdeCompatWindow *) (w)->base.privates[(ks)->windowPrivateIndex].ptr)

#define KDECOMPAT_WINDOW(w)					       \
    KdeCompatWindow *kw = GET_KDECOMPAT_WINDOW  (w,		       \
			  GET_KDECOMPAT_SCREEN  (w->screen,	       \
			  GET_KDECOMPAT_DISPLAY (w->screen->display)))

static Bool
kdecompatPaintWindow (CompWindow		 *w,
		      const WindowPaintAttrib *attrib,
		      const CompTransform	 *transform,
		      Region		         region,
		      unsigned int		 mask)
{
    CompScreen   *s = w->screen;
    Bool         status;
    unsigned int i;

    KDECOMPAT_SCREEN (s);
    KDECOMPAT_WINDOW (w);

    UNWRAP (ks, s, paintWindow);
    status = (*s->paintWindow) (w, attrib, transform, region, mask);
    WRAP (ks, s, paintWindow, kdecompatPaintWindow);

    if (!kdecompatGetPlasmaThumbnails (s) ||
	!kw->nPreviews                    ||
	!w->mapNum                        ||
	(mask & PAINT_WINDOW_OCCLUSION_DETECTION_MASK))
    {
	return status;
    }

    for (i = 0; i < kw->nPreviews; i++)
    {
	CompWindow   *tw = findWindowAtScreen (s, kw->previews[i].id);
	XRectangle   *rect = &kw->previews[i].thumb;
	unsigned int paintMask = mask | PAINT_WINDOW_TRANSFORMED_MASK;
	float        xScale = 1.0f, yScale = 1.0f, xTranslate, yTranslate;
	CompIcon     *icon = NULL;

	if (!tw)
	    continue;

	xTranslate = rect->x + w->attrib.x - tw->attrib.x;
	yTranslate = rect->y + w->attrib.y - tw->attrib.y;

	if (tw->texture->pixmap)
	{
	    unsigned int width, height;

	    width  = tw->attrib.width + tw->input.left + tw->input.right;
	    height = tw->attrib.height + tw->input.top + tw->input.bottom;

	    xScale = (float) rect->width / width;
	    yScale = (float) rect->height / height;

	    xTranslate += tw->input.left * xScale;
	    yTranslate += tw->input.top * yScale;
	}
	else
	{
	    icon = getWindowIcon (tw, 256, 256);
	    if (!icon)
		icon = s->defaultIcon;

	    if (icon)
		if (!icon->texture.name && !iconToTexture (s, icon))
		    icon = NULL;

	    if (icon)
	    {
		REGION     iconReg;
		CompMatrix matrix;

		paintMask |= PAINT_WINDOW_BLEND_MASK;

		if (icon->width >= rect->width || icon->height >= rect->height)
		{
		    xScale = (float) rect->width / icon->width;
		    yScale = (float) rect->height / icon->height;

		    if (xScale < yScale)
			yScale = xScale;
		    else
			xScale = yScale;
		}

		xTranslate += rect->width / 2 - (icon->width * xScale / 2);
		yTranslate += rect->height / 2 - (icon->height * yScale / 2);

		iconReg.rects    = &iconReg.extents;
		iconReg.numRects = 1;

		iconReg.extents.x1 = tw->attrib.x;
		iconReg.extents.y1 = tw->attrib.y;
		iconReg.extents.x2 = tw->attrib.x + icon->width;
		iconReg.extents.y2 = tw->attrib.y + icon->height;

		matrix = icon->texture.matrix;
		matrix.x0 -= (tw->attrib.x * icon->texture.matrix.xx);
		matrix.y0 -= (tw->attrib.y * icon->texture.matrix.yy);

		tw->vCount = tw->indexCount = 0;
		(*s->addWindowGeometry) (tw, &matrix, 1,
					 &iconReg, &infiniteRegion);

		if (!tw->vCount)
		    icon = NULL;
	    }
	}

	if (tw->texture->pixmap || icon)
	{
	    FragmentAttrib fragment;
	    CompTransform  wTransform = *transform;

	    initFragmentAttrib (&fragment, attrib);

	    if (tw->alpha || fragment.opacity != OPAQUE)
		paintMask |= PAINT_WINDOW_TRANSLUCENT_MASK;

	    matrixTranslate (&wTransform, tw->attrib.x, tw->attrib.y, 0.0f);
	    matrixScale (&wTransform, xScale, yScale, 1.0f);
	    matrixTranslate (&wTransform,
			     xTranslate / xScale - tw->attrib.x,
			     yTranslate / yScale - tw->attrib.y,
			     0.0f);

	    glPushMatrix ();
	    glLoadMatrixf (wTransform.m);

	    if (tw->texture->pixmap)
		(*s->drawWindow) (tw, &wTransform, &fragment,
				  &infiniteRegion, paintMask);
	    else if (icon)
		(*s->drawWindowTexture) (tw, &icon->texture,
					 &fragment, paintMask);

	    glPopMatrix ();
	}
    }

    return status;
}

static void
kdecompatUpdatePreviews (CompWindow *w)
{
    CompWindow      *cw;
    CompScreen      *s = w->screen;
    CompDisplay     *d = s->display;
    Atom	    actual;
    int		    result, format;
    unsigned long   n, left;
    unsigned char   *propData;

    KDECOMPAT_DISPLAY (d);
    KDECOMPAT_SCREEN (s);
    KDECOMPAT_WINDOW (w);

    kw->nPreviews = 0;

    result = XGetWindowProperty (d->display, w->id, kd->kdePreviewAtom, 0,
				 32768, FALSE, AnyPropertyType, &actual,
				 &format, &n, &left, &propData);

    if (result == Success && propData)
    {
	if (format == 32 && actual == kd->kdePreviewAtom)
	{
	    long *data    = (long *) propData;
	    int  nPreview = *data++;

	    if (n == (6 * nPreview + 1))
	    {
		kw->previews = realloc (kw->previews,
					nPreview * sizeof (Thumb));
		if (kw->previews)
		{
		    while (kw->nPreviews < nPreview)
		    {
			if (*data++ != 5)
			    break;

			kw->previews[kw->nPreviews].id = *data++;

			kw->previews[kw->nPreviews].thumb.x      = *data++;
			kw->previews[kw->nPreviews].thumb.y      = *data++;
			kw->previews[kw->nPreviews].thumb.width  = *data++;
			kw->previews[kw->nPreviews].thumb.height = *data++;

			kw->nPreviews++;
		    }
		}
	    }
	}

	XFree (propData);
    }

    for (cw = s->windows; cw; cw = cw->next)
    {
	CompWindow      *rw;
	KdeCompatWindow *kcw = GET_KDECOMPAT_WINDOW (cw, ks);

	kcw->isPreview = FALSE;
	for (rw = s->windows; rw; rw = rw->next)
	{
	    KdeCompatWindow *krw = GET_KDECOMPAT_WINDOW (rw, ks);
	    unsigned int    i;

	    for (i = 0; i < krw->nPreviews; i++)
	    {
		if (krw->previews[i].id == cw->id)
		{
		    kcw->isPreview = TRUE;
		    break;
		}
	    }

	    if (kcw->isPreview)
		break;
	}
    }
}

static void
kdecompatHandleEvent (CompDisplay *d,
		      XEvent      *event)
{
    KDECOMPAT_DISPLAY (d);

    UNWRAP (kd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (kd, d, handleEvent, kdecompatHandleEvent);

    switch (event->type) {
    case PropertyNotify:
	if (event->xproperty.atom == kd->kdePreviewAtom)
	{
	    CompWindow *w;

	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		kdecompatUpdatePreviews (w);
	}
	break;
    }
}

static Bool
kdecompatDamageWindowRect (CompWindow *w,
			   Bool	      initial,
			   BoxPtr     rect)
{
    Bool       status;
    CompScreen *s = w->screen;

    KDECOMPAT_SCREEN (s);
    KDECOMPAT_WINDOW (w);

    if (kw->isPreview && kdecompatGetPlasmaThumbnails (s))
    {
	CompWindow *cw;
	REGION     reg;

	reg.rects = &reg.extents;
	reg.numRects = 1;

	for (cw = s->windows; cw; cw = cw->next)
	{
	    KdeCompatWindow *kcw = GET_KDECOMPAT_WINDOW (cw, ks);
	    unsigned int    i;

	    for (i = 0; i < kcw->nPreviews; i++)
	    {
		if (kcw->previews[i].id != w->id)
		    continue;

		reg.extents.x1 = kcw->previews[i].thumb.x + cw->attrib.x;
		reg.extents.x2 = reg.extents.x1 + kcw->previews[i].thumb.width;
		reg.extents.y1 = kcw->previews[i].thumb.y + cw->attrib.y;
		reg.extents.y2 = reg.extents.y1 + kcw->previews[i].thumb.height;

		damageScreenRegion (s, &reg);
	    }
	}
    }

    UNWRAP (ks, s, damageWindowRect);
    status = (*s->damageWindowRect) (w, initial, rect);
    WRAP (ks, s, damageWindowRect, kdecompatDamageWindowRect);

    return status;
}

static void
kdecompatAdvertiseThumbSupport (CompScreen *s,
				Bool       supportThumbs)
{
    KDECOMPAT_DISPLAY (s->display);

    if (supportThumbs)
    {
	unsigned char value = 0;

	XChangeProperty (s->display->display, s->root, kd->kdePreviewAtom,
			 kd->kdePreviewAtom, 8, PropModeReplace, &value, 1);
    }
    else
    {
	XDeleteProperty (s->display->display, s->root, kd->kdePreviewAtom);
    }
}

static void
kdecompatScreenOptionChanged (CompScreen             *s,
			      CompOption             *opt,
			      KdecompatScreenOptions num)
{
    if (num == KdecompatScreenOptionPlasmaThumbnails)
	kdecompatAdvertiseThumbSupport (s, opt->value.b);
}

static Bool
kdecompatInitDisplay (CompPlugin  *p,
		      CompDisplay *d)
{
    KdeCompatDisplay *kd;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    kd = malloc (sizeof (KdeCompatDisplay));
    if (!kd)
	return FALSE;

    kd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (kd->screenPrivateIndex < 0)
    {
	free (kd);
	return FALSE;
    }

    kd->kdePreviewAtom = XInternAtom (d->display, "_KDE_WINDOW_PREVIEW", 0);

    WRAP (kd, d, handleEvent, kdecompatHandleEvent);

    d->base.privates[displayPrivateIndex].ptr = kd;

    return TRUE;
}

static void
kdecompatFiniDisplay (CompPlugin  *p,
		      CompDisplay *d)
{
    KDECOMPAT_DISPLAY (d);

    freeScreenPrivateIndex (d, kd->screenPrivateIndex);

    UNWRAP (kd, d, handleEvent);

    free (kd);
}

static Bool
kdecompatInitScreen (CompPlugin *p,
		     CompScreen *s)
{
    KdeCompatScreen *ks;

    KDECOMPAT_DISPLAY (s->display);

    ks = malloc (sizeof (KdeCompatScreen));
    if (!ks)
	return FALSE;

    ks->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (ks->windowPrivateIndex < 0)
    {
	free (ks);
	return FALSE;
    }

    kdecompatAdvertiseThumbSupport (s, kdecompatGetPlasmaThumbnails (s));
    kdecompatSetPlasmaThumbnailsNotify (s, kdecompatScreenOptionChanged);

    WRAP (ks, s, paintWindow, kdecompatPaintWindow);
    WRAP (ks, s, damageWindowRect, kdecompatDamageWindowRect);

    s->base.privates[kd->screenPrivateIndex].ptr = ks;

    return TRUE;
}

static void
kdecompatFiniScreen (CompPlugin *p,
		     CompScreen *s)
{
    KDECOMPAT_SCREEN (s);

    freeWindowPrivateIndex (s, ks->windowPrivateIndex);

    kdecompatAdvertiseThumbSupport (s, FALSE);

    UNWRAP (ks, s, paintWindow);
    UNWRAP (ks, s, damageWindowRect);

    free (ks);
}

static Bool
kdecompatInitWindow (CompPlugin *p,
		     CompWindow *w)
{
    KdeCompatWindow *kw;

    KDECOMPAT_SCREEN (w->screen);

    kw = malloc (sizeof (KdeCompatWindow));
    if (!kw)
	return FALSE;

    kw->previews  = NULL;
    kw->nPreviews = 0;
    kw->isPreview = FALSE;

    w->base.privates[ks->windowPrivateIndex].ptr = kw;

    return TRUE;
}

static void
kdecompatFiniWindow (CompPlugin *p,
		     CompWindow *w)
{
    KDECOMPAT_WINDOW (w);

    if (kw->previews)
	free (kw->previews);

    free (kw);
}

static CompBool
kdecompatInitObject (CompPlugin *p,
		     CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) kdecompatInitDisplay,
	(InitPluginObjectProc) kdecompatInitScreen,
	(InitPluginObjectProc) kdecompatInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
kdecompatFiniObject (CompPlugin *p,
		     CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) kdecompatFiniDisplay,
	(FiniPluginObjectProc) kdecompatFiniScreen,
	(FiniPluginObjectProc) kdecompatFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
kdecompatInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
kdecompatFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable kdecompatVTable = {
    "kdecompat",
    0,
    kdecompatInit,
    kdecompatFini,
    kdecompatInitObject,
    kdecompatFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &kdecompatVTable;
}

