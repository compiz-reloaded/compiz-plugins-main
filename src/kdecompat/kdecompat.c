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
    Atom kdeSlideAtom;
} KdeCompatDisplay;

typedef struct _KdeCompatScreen {
    int windowPrivateIndex;

    Bool hasSlidingPopups;

    PreparePaintScreenProc preparePaintScreen;
    PaintOutputProc        paintOutput;
    DonePaintScreenProc    donePaintScreen;
    PaintWindowProc        paintWindow;
    DamageWindowRectProc   damageWindowRect;
} KdeCompatScreen;

typedef struct _Thumb {
    Window     id;
    XRectangle thumb;
} Thumb;

typedef enum {
    West  = 0,
    North = 1,
    East  = 2,
    South = 3
} SlidePosition;

typedef struct {
    SlidePosition position;
    int           start;
    Bool          appearing;
    int           remaining;
    int           duration;
} SlideData;

typedef struct _KdeCompatWindow {
    Thumb        *previews;
    unsigned int nPreviews;
    Bool         isPreview;

    SlideData *slideData;

    int destroyCnt;
    int unmapCnt;
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

static void
kdecompatStopCloseAnimation (CompWindow *w)
{
    KDECOMPAT_WINDOW (w);

    while (kw->unmapCnt)
    {
	unmapWindow (w);
	kw->unmapCnt--;
    }

    while (kw->destroyCnt)
    {
	destroyWindow (w);
	kw->destroyCnt--;
    }
}

static void
kdecompatSendSlideEvent (CompWindow *w,
			 Bool       start)
{
    CompOption  o[2];
    CompDisplay *d = w->screen->display;

    o[0].type    = CompOptionTypeInt;
    o[0].name    = "window";
    o[0].value.i = w->id;

    o[1].type    = CompOptionTypeBool;
    o[1].name    = "active";
    o[1].value.b = start;

    (*d->handleCompizEvent) (d, "kdecompat", "slide", o, 2);
}

static void
kdecompatStartSlideAnimation (CompWindow *w,
			      Bool       appearing)
{
    KDECOMPAT_WINDOW (w);

    if (kw->slideData)
    {
	SlideData *data = kw->slideData;

	KDECOMPAT_SCREEN (w->screen);

	if (appearing)
	    data->duration = kdecompatGetSlideInDuration (w->screen);
	else
	    data->duration = kdecompatGetSlideOutDuration (w->screen);

	if (data->remaining > data->duration)
	    data->remaining = data->duration;
	else
	    data->remaining = data->duration - data->remaining;

	data->appearing      = appearing;
	ks->hasSlidingPopups = TRUE;
	addWindowDamage (w);
	kdecompatSendSlideEvent (w, TRUE);
    }
}

static void
kdecompatEndSlideAnimation (CompWindow *w)
{
    KDECOMPAT_WINDOW (w);

    if (kw->slideData)
    {
	kw->slideData->remaining = 0;
	kdecompatStopCloseAnimation (w);
	kdecompatSendSlideEvent (w, FALSE);
    }
}

static void
kdecompatPreparePaintScreen (CompScreen *s,
			     int        msSinceLastPaint)
{
    KDECOMPAT_SCREEN (s);

    if (ks->hasSlidingPopups)
    {
	CompWindow *w;

	for (w = s->windows; w; w = w->next)
	{
	    KdeCompatWindow *kw = GET_KDECOMPAT_WINDOW (w, ks);

	    if (!kw->slideData)
		continue;

	    kw->slideData->remaining -= msSinceLastPaint;
	    if (kw->slideData->remaining <= 0)
		kdecompatEndSlideAnimation (w);
	}
    }

    UNWRAP (ks, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (ks, s, preparePaintScreen, kdecompatPreparePaintScreen);
}

static Bool
kdecompatPaintOutput (CompScreen              *s,
		      const ScreenPaintAttrib *attrib,
		      const CompTransform     *transform,
		      Region                  region,
		      CompOutput              *output,
		      unsigned int            mask)
{
    Bool status;

    KDECOMPAT_SCREEN (s);

    if (ks->hasSlidingPopups)
	mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;

    UNWRAP (ks, s, paintOutput);
    status = (*s->paintOutput) (s, attrib, transform, region, output, mask);
    WRAP (ks, s, paintOutput, kdecompatPaintOutput);

    return status;
}

static void
kdecompatDonePaintScreen (CompScreen *s)
{
    KDECOMPAT_SCREEN (s);

    if (ks->hasSlidingPopups)
    {
	CompWindow *w;

	ks->hasSlidingPopups = FALSE;

	for (w = s->windows; w; w = w->next)
	{
	    KdeCompatWindow *kw = GET_KDECOMPAT_WINDOW (w, ks);

	    if (kw->slideData && kw->slideData->remaining)
	    {
		addWindowDamage (w);
		ks->hasSlidingPopups = TRUE;
	    }
	}
    }

    UNWRAP (ks, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (ks, s, donePaintScreen, kdecompatDonePaintScreen);
}

static Bool
kdecompatPaintWindow (CompWindow		 *w,
		      const WindowPaintAttrib	 *attrib,
		      const CompTransform	 *transform,
		      Region		         region,
		      unsigned int		 mask)
{
    CompScreen   *s = w->screen;
    Bool         status;
    unsigned int i;

    KDECOMPAT_SCREEN (s);
    KDECOMPAT_WINDOW (w);

    if (kw->slideData && kw->slideData->remaining)
    {
	FragmentAttrib fragment;
	CompTransform  wTransform = *transform;
	SlideData      *data = kw->slideData;
	float          xTranslate = 0, yTranslate = 0, remainder;
	BOX            clipBox;

	if (mask & PAINT_WINDOW_OCCLUSION_DETECTION_MASK)
	    return FALSE;

	remainder = (float) data->remaining / data->duration;
	if (!data->appearing)
	    remainder = 1.0 - remainder;

	clipBox.x1 = w->attrib.x;
	clipBox.y1 = w->attrib.y;
	clipBox.x2 = clipBox.x1 + w->attrib.width;
	clipBox.y2 = clipBox.y1 + w->attrib.height;

	switch (data->position) {
	case East:
	    xTranslate = (data->start - w->attrib.x) * remainder;
	    clipBox.x2 = data->start;
	    break;
	case West:
	    xTranslate = (data->start - w->attrib.width) * remainder;
	    clipBox.x1 = data->start;
	    break;
	case North:
	    yTranslate = (data->start - w->attrib.height) * remainder;
	    clipBox.y1 = data->start;
	    break;
	case South:
	default:
	    yTranslate = (data->start - w->attrib.y) * remainder;
	    clipBox.y2 = data->start;
	    break;
	}

	UNWRAP (ks, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region,
				    mask | PAINT_WINDOW_NO_CORE_INSTANCE_MASK);
	WRAP (ks, s, paintWindow, kdecompatPaintWindow);

	initFragmentAttrib (&fragment, &w->lastPaint);

	if (w->alpha || fragment.opacity != OPAQUE)
	    mask |= PAINT_WINDOW_TRANSLUCENT_MASK;

	matrixTranslate (&wTransform, xTranslate, yTranslate, 0.0f);

	glPushMatrix ();
	glLoadMatrixf (wTransform.m);

	glPushAttrib (GL_SCISSOR_BIT);
	glEnable (GL_SCISSOR_TEST);

	glScissor (clipBox.x1, s->height - clipBox.y2,
		   clipBox.x2 - clipBox.x1, clipBox.y2 - clipBox.y1);

	(*s->drawWindow) (w, &wTransform, &fragment, region,
			  mask | PAINT_WINDOW_TRANSFORMED_MASK);

	glDisable (GL_SCISSOR_TEST);
	glPopAttrib ();
	glPopMatrix ();
    }
    else
    {
	UNWRAP (ks, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
	WRAP (ks, s, paintWindow, kdecompatPaintWindow);
    }

    if (!status                           ||
	!kdecompatGetPlasmaThumbnails (s) ||
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
    CompWindow    *cw;
    CompScreen    *s = w->screen;
    CompDisplay   *d = s->display;
    Atom          actual;
    int           result, format;
    unsigned long n, left;
    unsigned char *propData;

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
kdecompatUpdateSlidePosition (CompWindow *w)
{
    CompDisplay   *d = w->screen->display;
    Atom	  actual;
    int		  result, format;
    unsigned long n, left;
    unsigned char *propData;

    KDECOMPAT_DISPLAY (d);
    KDECOMPAT_WINDOW (w);

    if (kw->slideData)
    {
	free (kw->slideData);
	kw->slideData = NULL;
    }

    result = XGetWindowProperty (d->display, w->id, kd->kdeSlideAtom, 0,
				 32768, FALSE, AnyPropertyType, &actual,
				 &format, &n, &left, &propData);

    if (result == Success && propData)
    {
	if (format == 32 && actual == kd->kdeSlideAtom && n == 2)
	{
	    long *data = (long *) propData;

	    kw->slideData = malloc (sizeof (SlideData));
	    if (kw->slideData)
	    {
		kw->slideData->remaining = 0;
		kw->slideData->start     = data[0];
		kw->slideData->position  = data[1];
	    }
	}

	XFree (propData);
    }
}

static void
kdecompatHandleWindowClose (CompWindow *w,
			    Bool       destroy)
{
    KDECOMPAT_WINDOW (w);

    if (kw->slideData && kdecompatGetSlidingPopups (w->screen))
    {
	if (destroy)
	{
	    kw->destroyCnt++;
	    w->destroyRefCnt++;
	}
	else
	{
	    kw->unmapCnt++;
	    w->unmapRefCnt++;
	}

	if (kw->slideData->appearing || !kw->slideData->remaining)
	    kdecompatStartSlideAnimation (w, FALSE);
    }
}

static void
kdecompatHandleEvent (CompDisplay *d,
		      XEvent      *event)
{
    CompWindow *w;

    KDECOMPAT_DISPLAY (d);

    switch (event->type) {
    case DestroyNotify:
	w = findWindowAtDisplay (d, event->xdestroywindow.window);
	if (w)
	    kdecompatHandleWindowClose (w, TRUE);
	break;
    case UnmapNotify:
	w = findWindowAtDisplay (d, event->xunmap.window);
	if (w && !w->pendingUnmaps)
	    kdecompatHandleWindowClose (w, FALSE);
	break;
    case MapNotify:
	w = findWindowAtDisplay (d, event->xmap.window);
	if (w)
	    kdecompatStopCloseAnimation (w);
	break;
    }

    UNWRAP (kd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (kd, d, handleEvent, kdecompatHandleEvent);

    switch (event->type) {
    case PropertyNotify:
	if (event->xproperty.atom == kd->kdePreviewAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		kdecompatUpdatePreviews (w);
	}
	else if (event->xproperty.atom == kd->kdeSlideAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		kdecompatUpdateSlidePosition (w);
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

    if (initial && kdecompatGetSlidingPopups (s))
	kdecompatStartSlideAnimation (w, TRUE);

    UNWRAP (ks, s, damageWindowRect);
    status = (*s->damageWindowRect) (w, initial, rect);
    WRAP (ks, s, damageWindowRect, kdecompatDamageWindowRect);

    return status;
}

static void
kdecompatAdvertiseSupport (CompScreen *s,
			   Atom       atom,
			   Bool       enable)
{
    if (enable)
    {
	unsigned char value = 0;

	XChangeProperty (s->display->display, s->root, atom, atom,
			 8, PropModeReplace, &value, 1);
    }
    else
    {
	XDeleteProperty (s->display->display, s->root, atom);
    }
}

static void
kdecompatScreenOptionChanged (CompScreen             *s,
			      CompOption             *opt,
			      KdecompatScreenOptions num)
{
    KDECOMPAT_DISPLAY (s->display);

    if (num == KdecompatScreenOptionPlasmaThumbnails)
	kdecompatAdvertiseSupport (s, kd->kdePreviewAtom, opt->value.b);
    else if (num == KdecompatScreenOptionSlidingPopups)
	kdecompatAdvertiseSupport (s, kd->kdeSlideAtom, opt->value.b);
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
    kd->kdeSlideAtom = XInternAtom (d->display, "_KDE_SLIDE", 0);

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

    ks->hasSlidingPopups = FALSE;

    kdecompatAdvertiseSupport (s, kd->kdePreviewAtom,
			       kdecompatGetPlasmaThumbnails (s));
    kdecompatAdvertiseSupport (s, kd->kdeSlideAtom,
			       kdecompatGetSlidingPopups (s));
    kdecompatSetPlasmaThumbnailsNotify (s, kdecompatScreenOptionChanged);
    kdecompatSetSlidingPopupsNotify (s, kdecompatScreenOptionChanged);

    WRAP (ks, s, preparePaintScreen, kdecompatPreparePaintScreen);
    WRAP (ks, s, paintOutput, kdecompatPaintOutput);
    WRAP (ks, s, donePaintScreen, kdecompatDonePaintScreen);
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
    KDECOMPAT_DISPLAY (s->display);

    freeWindowPrivateIndex (s, ks->windowPrivateIndex);

    kdecompatAdvertiseSupport (s, kd->kdePreviewAtom, FALSE);
    kdecompatAdvertiseSupport (s, kd->kdeSlideAtom, FALSE);

    UNWRAP (ks, s, preparePaintScreen);
    UNWRAP (ks, s, paintOutput);
    UNWRAP (ks, s, donePaintScreen);
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

    kw->slideData = NULL;

    kw->unmapCnt   = 0;
    kw->destroyCnt = 0;

    w->base.privates[ks->windowPrivateIndex].ptr = kw;

    return TRUE;
}

static void
kdecompatFiniWindow (CompPlugin *p,
		     CompWindow *w)
{
    KDECOMPAT_WINDOW (w);

    kdecompatStopCloseAnimation (w);

    if (kw->previews)
	free (kw->previews);

    if (kw->slideData)
	free (kw->slideData);

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

