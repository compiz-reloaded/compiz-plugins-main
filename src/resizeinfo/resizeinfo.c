/**
 *
 * Compiz metacity like info during resize
 *
 * resizeinfo.c
 *
 * Copyright (c) 2007 Robert Carr <racarr@opencompositing.org>
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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <compiz.h>
#include <cairo-xlib-xrender.h>
#include <math.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include "resizeinfo_options.h"

#define PI 3.1415926

static int displayPrivateIndex;

typedef struct _InfoDisplay 
{
    int screenPrivateIndex;

    HandleEventProc handleEvent;
	
    Atom resizeNotifyAtom;
} InfoDisplay;

typedef struct _InfoLayer
{
    Pixmap          pixmap;
    CompTexture     texture;
    cairo_surface_t *surface;
    cairo_t         *cr;
} InfoLayer;

typedef struct _InfoScreen
{
    WindowGrabNotifyProc   windowGrabNotify;
    WindowUngrabNotifyProc windowUngrabNotify;
    PaintOutputProc        paintOutput;
    PreparePaintScreenProc preparePaintScreen;
    DonePaintScreenProc    donePaintScreen;
  
    CompWindow *pWindow;
  
    Bool drawing;
    int  fadeTime;

    InfoLayer backgroundLayer;
    InfoLayer textLayer;
  
    XRectangle resizeGeometry;
} InfoScreen;

#define RESIZE_POPUP_WIDTH 75
#define RESIZE_POPUP_HEIGHT 50

#define GET_INFO_DISPLAY(d)				    \
    ((InfoDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define INFO_DISPLAY(d)			  \
    InfoDisplay *id = GET_INFO_DISPLAY (d)

#define GET_INFO_SCREEN(s, id)					\
    ((InfoScreen *) (s)->privates[(id)->screenPrivateIndex].ptr)

#define INFO_SCREEN(s)						       \
    InfoScreen *is = GET_INFO_SCREEN (s, GET_INFO_DISPLAY (s->display))

/* Set up an InfoLayer to build a cairo->opengl texture pipeline */
static void
setupCairoLayer (CompScreen *s,
		 InfoLayer  *il)
{
    XRenderPictFormat *format;
    Screen            *screen;
    int               w, h;
	
    screen = ScreenOfDisplay (s->display->display, s->screenNum);
	
    w = RESIZE_POPUP_WIDTH;
    h = RESIZE_POPUP_HEIGHT;
	
    format = XRenderFindStandardFormat (s->display->display,
	    				PictStandardARGB32);

    il->pixmap = XCreatePixmap (s->display->display, s->root, w, h, 32);
    if (!bindPixmapToTexture (s, &il->texture, il->pixmap, w, h, 32))
    {
	compLogMessage (s->display, "resizeinfo", CompLogLevelWarn,
			"Bind Pixmap to Texture failure");
	XFreePixmap (s->display->display, il->pixmap);
	return;
    }
	
    il->surface =
	cairo_xlib_surface_create_with_xrender_format (s->display->display,
						       il->pixmap, screen,
						       format, w, h);
    il->cr = cairo_create (il->surface);
    if (cairo_status (il->cr) != CAIRO_STATUS_SUCCESS)
    {
	compLogMessage (s->display, "resizeinfo", CompLogLevelWarn,
			"Could not create cairo context");
	cairo_destroy (il->cr);
	cairo_surface_destroy (il->surface);
	XFreePixmap (s->display->display, il->pixmap);
	il->cr = NULL;
	return;
    }
}

/* Draw the window "size" derived from the window hints.
   We calculate width or height - base_width or base_height and divide
   it by the increment in each direction. For windows like terminals
   setting the proper size hints this gives us the number of columns/rows. */
static void
updateTextLayer (CompScreen *s)
{
    int                  baseWidth, baseHeight;
    int                  widthInc, heightInc;
    int                  width, height, xv, yv;
    unsigned short       *color;
    char                 *info;
    cairo_t              *cr;
    PangoLayout          *layout;
    PangoFontDescription *font;
    int                  w, h;

    INFO_SCREEN (s);

    if (!is->textLayer.cr)
	return;

    baseWidth = is->pWindow->sizeHints.base_width;
    baseHeight = is->pWindow->sizeHints.base_height;
    widthInc = is->pWindow->sizeHints.width_inc;
    heightInc = is->pWindow->sizeHints.height_inc;
    width = is->resizeGeometry.width;
    height = is->resizeGeometry.height;
	
    color = resizeinfoGetTextColor (s->display);

    xv = (widthInc > 1) ? (width - baseWidth) / widthInc : width;
    yv = (heightInc > 1) ? (height - baseHeight) / heightInc : height;
  
    cr = is->textLayer.cr;

    /* Clear the context. */
    cairo_save (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint (cr);
    cairo_restore (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

    asprintf (&info, "%d x %d", xv, yv);
 
    font = pango_font_description_new ();
    layout = pango_cairo_create_layout (is->textLayer.cr);
  
    pango_font_description_set_family (font,"Sans");
    pango_font_description_set_absolute_size (font, 12 * PANGO_SCALE);
    pango_font_description_set_style (font, PANGO_STYLE_NORMAL);
    pango_font_description_set_weight (font, PANGO_WEIGHT_BOLD);
 
    pango_layout_set_font_description (layout, font);
    pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_text (layout, info, -1);
  
    pango_layout_get_pixel_size (layout, &w, &h);
  
    cairo_move_to (cr, 
		   RESIZE_POPUP_WIDTH / 2.0f - w / 2.0f, 
		   RESIZE_POPUP_HEIGHT / 2.0f - h / 2.0f);
  
    pango_layout_set_width (layout, RESIZE_POPUP_WIDTH * PANGO_SCALE);
    pango_cairo_update_layout (cr, layout);
  
    cairo_set_source_rgba (cr, 
			   *(color)     / (float)0xffff,
			   *(color + 1) / (float)0xffff,
			   *(color + 2) / (float)0xffff,
			   *(color + 3) / (float)0xffff);

    pango_cairo_show_layout (cr, layout);

    pango_font_description_free (font);
    g_object_unref (layout);
}

/* Draw the background. We draw this on a second layer so that we do
   not have to draw it each time we have to update. Granted we could
   use some cairo trickery for this... */
static void
drawCairoBackground (CompScreen *s)
{
    cairo_t         *cr;
    cairo_pattern_t *pattern;	
    float           border = 7.5;
    int             height = RESIZE_POPUP_HEIGHT;
    int             width = RESIZE_POPUP_WIDTH;
    float           r, g, b, a;

    INFO_SCREEN (s);

    cr = is->backgroundLayer.cr;
    if (!cr)
	return;
	
    cairo_set_line_width (cr, 1.0f);

    /* Clear */
    cairo_save (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint (cr);
    cairo_restore (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

    /* Setup Gradient */
    pattern = cairo_pattern_create_linear (0, 0, width, height);

    r = resizeinfoGetGradient1Red (s->display) / (float)0xffff;
    g = resizeinfoGetGradient1Green (s->display) / (float)0xffff;
    b = resizeinfoGetGradient1Blue (s->display) / (float)0xffff;
    a = resizeinfoGetGradient1Alpha (s->display) / (float)0xffff;
    cairo_pattern_add_color_stop_rgba (pattern, 0.00f, r, g, b, a);

    r = resizeinfoGetGradient1Red (s->display) / (float)0xffff;
    g = resizeinfoGetGradient1Green (s->display) / (float)0xffff;
    b = resizeinfoGetGradient1Blue (s->display) / (float)0xffff;
    a = resizeinfoGetGradient1Alpha (s->display) / (float)0xffff;
    cairo_pattern_add_color_stop_rgba (pattern, 0.65f, r, g, b, a);

    r = resizeinfoGetGradient1Red (s->display) / (float)0xffff;
    g = resizeinfoGetGradient1Green (s->display) / (float)0xffff;
    b = resizeinfoGetGradient1Blue (s->display) / (float)0xffff;
    a = resizeinfoGetGradient1Alpha (s->display) / (float)0xffff;
    cairo_pattern_add_color_stop_rgba (pattern, 0.85f, r, g, b, a);
    cairo_set_source (cr, pattern);
	
    /* Rounded Rectangle! */
    cairo_arc (cr, border, border, border, PI, 1.5f * PI);
    cairo_arc (cr, border + width - 2 * border, border, border,
	       1.5f * PI, 2.0 * PI);
    cairo_arc (cr, width - border, height - border, border, 0, PI / 2.0f);
    cairo_arc (cr, border, height - border, border,  PI / 2.0f, PI);
    cairo_close_path (cr);
    cairo_fill_preserve (cr);
	
    /* Outline */
    cairo_set_source_rgba (cr, 0.9f, 0.9f, 0.9f, 1.0f);
    cairo_stroke (cr);
	
    cairo_pattern_destroy (pattern);
}

static void
gradientChanged (CompDisplay              *d,
		 CompOption               *o, 
		 ResizeinfoDisplayOptions num)
{
    CompScreen *s;

    for (s = d->screens; s; s = s->next)
	drawCairoBackground (s);
}

/*  Handle the fade in /fade out. */
static void
infoPreparePaintScreen (CompScreen *s,
			int        ms)
{
    INFO_SCREEN (s);
	
    if (is->fadeTime)
    {
	is->fadeTime -= ms;
	if (is->fadeTime < 0)
	    is->fadeTime = 0;
    }
	
    UNWRAP (is, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, ms);
    WRAP (is, s, preparePaintScreen, infoPreparePaintScreen);
}

static void
infoDonePaintScreen (CompScreen *s)
{
    INFO_SCREEN (s);

    if (is->pWindow)
    {
	if (is->fadeTime || is->drawing)
	{
	    REGION reg;
	    int    x, y;

	    x = is->resizeGeometry.x + is->resizeGeometry.width / 2.0f -
		RESIZE_POPUP_WIDTH / 2.0f;
    	    y = is->resizeGeometry.y + is->resizeGeometry.height / 2.0f - 
		RESIZE_POPUP_HEIGHT / 2.0f;

	    reg.rects    = &reg.extents;
	    reg.numRects = 1;

	    reg.extents.x1 = x - 5;
	    reg.extents.y1 = y - 5;
	    reg.extents.x2 = x + RESIZE_POPUP_WIDTH + 5;
	    reg.extents.y2 = y + RESIZE_POPUP_HEIGHT + 5;

	    damageScreenRegion (s, &reg);
	}
	
	if (!is->fadeTime && !is->drawing)
	    is->pWindow = 0;
    }

    UNWRAP (is, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (is, s, donePaintScreen, infoDonePaintScreen);
}

static void
infoWindowGrabNotify (CompWindow   *w,
    		      int          x,
		      int          y,
		      unsigned int state,
		      unsigned int mask)
{
    CompScreen * s = w->screen;

    INFO_SCREEN (s);

    if (!is->pWindow && !(w->state & MAXIMIZE_STATE))
    {
	Bool showInfo;
	showInfo = ((w->sizeHints.width_inc != 1) && 
		    (w->sizeHints.height_inc != 1)) ||
	           resizeinfoGetAlwaysShow (s->display);

	if (showInfo && (mask & CompWindowGrabResizeMask))
	{
	    is->pWindow  = w;
	    is->drawing  = TRUE;
	    is->fadeTime = resizeinfoGetFadeTime (s->display);

	    is->resizeGeometry.x      = w->attrib.x;
	    is->resizeGeometry.y      = w->attrib.y;
	    is->resizeGeometry.width  = w->attrib.width;
	    is->resizeGeometry.height = w->attrib.height;
	}
    }
	
    UNWRAP (is, s, windowGrabNotify);
    (*s->windowGrabNotify) (w, x, y, state, mask);
    WRAP (is, s, windowGrabNotify, infoWindowGrabNotify);
}

static void
infoWindowUngrabNotify (CompWindow *w)
{
    CompScreen *s = w->screen;

    INFO_SCREEN (s);

    if (w == is->pWindow)
    {
	is->drawing = FALSE;
	is->fadeTime = resizeinfoGetFadeTime (s->display);
	damageScreen (s);
    }
	
    UNWRAP (is, s, windowUngrabNotify);
    (*s->windowUngrabNotify) (w);
    WRAP (is, s, windowUngrabNotify, infoWindowUngrabNotify);
}

/* Draw a texture at x/y on a quad of RESIZE_POPUP_WIDTH /
   RESIZE_POPUP_HEIGHT with the opacity in InfoScreen. */
static void
drawLayer (CompScreen  *s,
	   int         x,
	   int         y, 
	   CompMatrix  matrix,
	   CompTexture *t)
{
    BOX   box;
    float opacity;

    INFO_SCREEN (s);

    enableTexture (s, t, COMP_TEXTURE_FILTER_FAST);
    matrix.x0 -= x * matrix.xx;
    matrix.y0 -= y * matrix.yy;

    box.x1 = x;
    box.x2 = x + RESIZE_POPUP_WIDTH;
    box.y1 = y;
    box.y2 = y + RESIZE_POPUP_HEIGHT;

    opacity = (float)is->fadeTime / resizeinfoGetFadeTime (s->display);
    if (is->drawing)
	opacity = 1.0f - opacity;

    glColor4f (opacity, opacity, opacity, opacity); 
    glBegin (GL_QUADS);
    glTexCoord2f (COMP_TEX_COORD_X (&matrix, box.x1), 
		  COMP_TEX_COORD_Y (&matrix, box.y2));
    glVertex2i (box.x1, box.y2);
    glTexCoord2f (COMP_TEX_COORD_X (&matrix, box.x2), 
		  COMP_TEX_COORD_Y (&matrix, box.y2));
    glVertex2i (box.x2, box.y2);
    glTexCoord2f (COMP_TEX_COORD_X (&matrix, box.x2), 
		  COMP_TEX_COORD_Y (&matrix, box.y1));
    glVertex2i (box.x2, box.y1);
    glTexCoord2f (COMP_TEX_COORD_X (&matrix, box.x1), 
		  COMP_TEX_COORD_Y (&matrix, box.y1));
    glVertex2i (box.x1, box.y1);	
    glEnd ();
    glColor4usv (defaultColor);

    disableTexture (s, t);
}

static Bool
infoPaintOutput (CompScreen              *s,
		 const ScreenPaintAttrib *sAttrib,
		 const CompTransform     *transform,
		 Region                  region,
		 CompOutput              *output,
		 unsigned int            mask)
{
    Bool status;

    INFO_SCREEN (s);
  
    UNWRAP (is, s, paintOutput);
    status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
    WRAP (is, s, paintOutput, infoPaintOutput);

    if ((is->drawing || is->fadeTime) && is->pWindow)
    {
	CompMatrix    matrix;
	CompTransform sTransform = *transform;
	int           x, y;

	x = is->resizeGeometry.x + is->resizeGeometry.width / 2.0f - 
	    RESIZE_POPUP_WIDTH / 2.0f;
	y = is->resizeGeometry.y + is->resizeGeometry.height / 2.0f - 
	    RESIZE_POPUP_HEIGHT / 2.0f;

	matrix = is->backgroundLayer.texture.matrix;

	transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);
      
	glPushMatrix ();
	glLoadMatrixf (sTransform.m);

	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	glEnable (GL_BLEND);
	screenTexEnvMode (s, GL_MODULATE);
  
	drawLayer (s, x, y, matrix, &is->backgroundLayer.texture);
	drawLayer (s, x, y, is->textLayer.texture.matrix,
		   &is->textLayer.texture);
  
	glDisable (GL_BLEND);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);

	glPopMatrix ();
    }

    return status;
}

static void
infoHandleEvent (CompDisplay *d,
		 XEvent      *event)
{
    INFO_DISPLAY (d);

    switch (event->type) {
    case ClientMessage:
	if (event->xclient.message_type == id->resizeNotifyAtom)
	{
	    CompWindow *w;

	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w)
	    {
		INFO_SCREEN (w->screen);

		if (w == is->pWindow)
		{
		    is->resizeGeometry.x = event->xclient.data.l[0];
		    is->resizeGeometry.y = event->xclient.data.l[1];
		    is->resizeGeometry.width = event->xclient.data.l[2];
		    is->resizeGeometry.height = event->xclient.data.l[3];

		    updateTextLayer (w->screen);
		}
	    }
	}
	break;
    default:
	break;
    }

    UNWRAP (id, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (id, d, handleEvent, infoHandleEvent);
}

static Bool
infoInitDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    InfoDisplay *id;

    id = malloc (sizeof (InfoDisplay));
    if (!id)
	return FALSE;

    id->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (id->screenPrivateIndex < 0)
    {
	free (id);
	return FALSE;
    }

    resizeinfoSetGradient1Notify (d, gradientChanged);
    resizeinfoSetGradient2Notify (d, gradientChanged);
    resizeinfoSetGradient3Notify (d, gradientChanged);

    id->resizeNotifyAtom = XInternAtom (d->display, "_COMPIZ_RESIZE_NOTIFY", 0);

    d->privates[displayPrivateIndex].ptr = id;

    WRAP (id, d, handleEvent, infoHandleEvent);

    return TRUE;
}

static void
infoFiniDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    INFO_DISPLAY (d);

    freeScreenPrivateIndex (d, id->screenPrivateIndex);

    UNWRAP (id, d, handleEvent);
	
    free (id);
}

static Bool 
infoInitScreen (CompPlugin *p,
		CompScreen *s)
{
    InfoScreen * is;

    INFO_DISPLAY (s->display);

    is = malloc (sizeof (InfoScreen));
    if (!is)
	return FALSE;

    is->pWindow  = 0;
    is->fadeTime = 0;
    is->drawing  = FALSE;

    is->resizeGeometry.x      = 0;
    is->resizeGeometry.y      = 0;
    is->resizeGeometry.width  = 0;
    is->resizeGeometry.height = 0;

    initTexture (s, &is->backgroundLayer.texture);
    initTexture (s, &is->textLayer.texture);
	
    WRAP (is, s, windowGrabNotify, infoWindowGrabNotify);
    WRAP (is, s, windowUngrabNotify, infoWindowUngrabNotify);
    WRAP (is, s, preparePaintScreen, infoPreparePaintScreen);
    WRAP (is, s, paintOutput, infoPaintOutput);
    WRAP (is, s, donePaintScreen, infoDonePaintScreen);

    s->privates[id->screenPrivateIndex].ptr = is;

    /* setup and draw cairo background */
    setupCairoLayer (s, &is->backgroundLayer);
    drawCairoBackground (s);
	
    /* setup text layer */
    setupCairoLayer (s, &is->textLayer);
  
    return TRUE;
}

static void
freeInfoLayer (CompScreen *s,
	       InfoLayer  *is)
{
    if (is->cr)
	cairo_destroy (is->cr);
    if (is->surface)
	cairo_surface_destroy (is->surface);
  
    finiTexture (s, &is->texture);
  
    if (is->pixmap)
	XFreePixmap (s->display->display, is->pixmap);
}

static void
infoFiniScreen (CompPlugin *p,
		CompScreen *s)
{
    INFO_SCREEN (s);
	
    freeInfoLayer (s, &is->backgroundLayer);
    freeInfoLayer (s, &is->textLayer);
         
    UNWRAP (is, s, windowGrabNotify);
    UNWRAP (is, s, windowUngrabNotify);
    UNWRAP (is, s, preparePaintScreen);
    UNWRAP (is, s, paintOutput);
    UNWRAP (is, s, donePaintScreen);

    free (is);
}

static Bool
infoInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;
	
    return TRUE;
}

static void
infoFini (CompPlugin *p)
{
    freeDisplayPrivateIndex(displayPrivateIndex);
}

static int
infoGetVersion(CompPlugin *plugin,
	       int        version)
{
    return ABIVERSION;
}

static CompPluginVTable infoVTable = {
    "resizeinfo",
    infoGetVersion,
    0,
    infoInit,
    infoFini,
    infoInitDisplay,
    infoFiniDisplay,
    infoInitScreen,
    infoFiniScreen,
    0, /* InitWindow */
    0, /* FiniWindow */
    0, /* GetDisplayOptions */
    0, /* SetDisplayOption */
    0, /* GetScreenOptions */
    0 /* SetScreenOption */
};

CompPluginVTable*
getCompPluginInfo (void)
{
    return &infoVTable;
}
