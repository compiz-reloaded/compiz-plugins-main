/*
 * Compiz text plugin
 * Description: Adds text to pixmap support to Compiz.
 *
 * text.c
 *
 * Copyright: (C) 2006-2007 Patrick Niklaus, Danny Baumann, Dennis Kasprzyk
 * Authors: Patrick Niklaus <marex@opencompsiting.org>
 *	    Danny Baumann   <maniac@opencompositing.org>
 *	    Dennis Kasprzyk <onestone@opencompositing.org>
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

#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <X11/Xatom.h>

#include <cairo-xlib-xrender.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include <compiz-core.h>
#include "compiz-text.h"
#include "text_options.h"

#define PI 3.14159265359f

static int displayPrivateIndex;

typedef struct _TextDisplay
{
    FileToImageProc fileToImage;

    Atom visibleNameAtom;
} TextDisplay;

#define GET_TEXT_DISPLAY(d)				    \
    ((TextDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define TEXT_DISPLAY(d)			 \
    TextDisplay *td = GET_TEXT_DISPLAY (d)

static char*
textGetUtf8Property (CompDisplay *d,
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

    retval = malloc (sizeof (char) * (nitems + 1));
    if (retval)
    {
	strncpy (retval, val, nitems);
	retval[nitems] = 0;
    }

    XFree (val);

    return retval;
}

static char*
textGetTextProperty (CompDisplay *d,
		     Window      id,
		     Atom        atom)
{
    XTextProperty text;
    char          *retval = NULL;

    text.nitems = 0;
    if (XGetTextProperty (d->display, id, &text, atom))
    {
        if (text.value)
	{
	    retval = malloc (sizeof (char) * (text.nitems + 1));
	    if (retval)
	    {
		strncpy (retval, (char *) text.value, text.nitems);
		retval[text.nitems] = 0;
	    }

	    XFree (text.value);
	}
    }

    return retval;
}

static char*
textGetWindowName (CompDisplay *d,
		   Window      id)
{
    char *name;

    TEXT_DISPLAY (d);

    name = textGetUtf8Property (d, id, td->visibleNameAtom);

    if (!name)
	name = textGetUtf8Property(d, id, d->wmNameAtom);

    if (!name)
	name = textGetTextProperty (d, id, XA_WM_NAME);

    return name;
}

/*
 * Draw a rounded rectangle path
 */
static void
textDrawTextBackground (cairo_t *cr, int x, int y, int width, int height,
			int radius)
{
    int x0, y0, x1, y1;

    x0 = x;
    y0 = y;
    x1 = x + width;
    y1 = y + height;

    cairo_new_path (cr);
    cairo_arc (cr, x0 + radius, y1 - radius, radius, PI / 2, PI);
    cairo_line_to (cr, x0, y0 + radius);
    cairo_arc (cr, x0 + radius, y0 + radius, radius, PI, 3 * PI / 2);
    cairo_line_to (cr, x1 - radius, y0);
    cairo_arc (cr, x1 - radius, y0 + radius, radius, 3 * PI / 2, 2 * PI);
    cairo_line_to (cr, x1, y1 - radius);
    cairo_arc (cr, x1 - radius, y1 - radius, radius, 0, PI / 2);
    cairo_close_path (cr);
}


static Bool
textFileToImage (CompDisplay *d,
      		 const char  *path,
		 const char  *name,
		 int         *width,
		 int         *height,
		 int         *stride,
		 void        **data)
{
    Bool status = FALSE;

    TEXT_DISPLAY (d);

    if (path && name && strcmp(path, TEXT_ID) == 0)
    {
	cairo_t              *cr;
	cairo_surface_t      *surface;
	PangoLayout          *layout;
	Pixmap               pixmap;
	XRenderPictFormat    *format;
	PangoFontDescription *font;
	CompTextAttrib       *textAttrib;
	Display              *dpy = d->display;
	Screen               *screen;
	int                  w, h;
	int		    layoutWidth;
	char                 *text = NULL;
	
	textAttrib = (CompTextAttrib*) name; /* get it through the name */
	screen = ScreenOfDisplay (dpy, textAttrib->screen->screenNum);

	if (!screen)
	{
	    compLogMessage ("text", CompLogLevelError,
			    "Couldn't get screen for %d.",
			    textAttrib->screen->screenNum);
	    return FALSE;
	}

	format = XRenderFindStandardFormat (dpy, PictStandardARGB32);
	if (!format)
	{
	    compLogMessage ("text", CompLogLevelError,
			    "Couldn't get format.");
	    return FALSE;
	}

	pixmap = XCreatePixmap (dpy, textAttrib->screen->root, 1, 1, 32);
	if (!pixmap)
	{
	    compLogMessage ("text", CompLogLevelError,
			    "Couldn't create pixmap.");
	    return FALSE;
	}

	surface = cairo_xlib_surface_create_with_xrender_format (dpy,
								 pixmap,
								 screen,
								 format, 1, 1);

	if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
	{
	    compLogMessage ("text", CompLogLevelError,
			    "Couldn't create surface.");
	    XFreePixmap (dpy, pixmap);
	    return FALSE;
	}

	cr = cairo_create (surface);
	if (cairo_status (cr) != CAIRO_STATUS_SUCCESS)
	{
	    compLogMessage ("text", CompLogLevelError,
			    "Couldn't create cairo context.");
	    cairo_surface_destroy (surface);
	    XFreePixmap (dpy, pixmap);
	    return FALSE;
	}

	/* init pango */
	layout = pango_cairo_create_layout (cr);
	if (!layout)
	{
	    compLogMessage ("text", CompLogLevelError,
			    "Couldn't create pango layout.");
	    cairo_destroy (cr);
	    cairo_surface_destroy (surface);
	    XFreePixmap (dpy, pixmap);
	    return FALSE;
	}

	font = pango_font_description_new ();
	if (!font)
	{
	    compLogMessage ("text", CompLogLevelError,
			    "Couldn't create font description.");
	    g_object_unref (layout);
	    cairo_destroy (cr);
	    cairo_surface_destroy (surface);
	    XFreePixmap (dpy, pixmap);
	    return FALSE;
	}

	pango_font_description_set_family (font, textAttrib->family);
	pango_font_description_set_absolute_size (font,
						  textAttrib->size *
						  PANGO_SCALE);

	pango_font_description_set_style (font, PANGO_STYLE_NORMAL);

	if (textAttrib->style & TEXT_STYLE_BOLD)
	    pango_font_description_set_weight (font, PANGO_WEIGHT_BOLD);

	if (textAttrib->style & TEXT_STYLE_ITALIC)
    	    pango_font_description_set_style (font, PANGO_STYLE_ITALIC);

	pango_layout_set_font_description (layout, font);

	if (textAttrib->ellipsize)
    	    pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);

	switch (textAttrib->renderMode) {
	case TextRenderNormal:
	    text = strdup ((char*) textAttrib->data);
	    break;
	case TextRenderWindowTitle:
	    {
		Window xid = (Window) textAttrib->data;
		text = textGetWindowName (d, xid);
	    }
	    break;
	case TextRenderWindowTitleWithViewport:
	    {
		Window xid = (Window) textAttrib->data;
		char *title = textGetWindowName (d, xid);

		if (title)
		{
		    CompWindow *w;

		    w = findWindowAtDisplay (d, xid);
		    if (w)
		    {
			int vx, vy, viewport;

			defaultViewportForWindow (w, &vx, &vy);
			viewport = vy * w->screen->hsize + vx + 1;
			asprintf (&text, "%s -[%d]-", title, viewport);
			free (title);
		    }
		    else
			text = title;
		}
	    }
	    break;
	default:
	    break;
	}

	if (text)
	{
	    pango_layout_set_auto_dir (layout, FALSE);
	    pango_layout_set_text (layout, text, -1);
	    free (text);
	}
	else
	{
	    pango_font_description_free (font);
	    g_object_unref (layout);
	    cairo_destroy (cr);
	    cairo_surface_destroy (surface);
	    XFreePixmap (dpy, pixmap);
	    return FALSE;
	}

	pango_layout_get_pixel_size (layout, &w, &h);

	if (textAttrib->style & TEXT_STYLE_BACKGROUND)
	{
	    w += 2 * textAttrib->backgroundHMargin;
	    h += 2 * textAttrib->backgroundVMargin;
	}

	w = MIN (textAttrib->maxWidth, w);
	h = MIN (textAttrib->maxHeight, h);

	/* update the size of the pango layout */
	layoutWidth = textAttrib->maxWidth;
	if (textAttrib->style & TEXT_STYLE_BACKGROUND)
	    layoutWidth -= 2 * textAttrib->backgroundHMargin;
	pango_layout_set_width (layout, layoutWidth * PANGO_SCALE);

	cairo_surface_destroy (surface);
	cairo_destroy (cr);
	XFreePixmap (dpy, pixmap);
	pixmap = None;

	if (w > 0 && h > 0)
	    pixmap = XCreatePixmap (dpy, textAttrib->screen->root, w, h, 32);

	if (!pixmap)
	{
	    compLogMessage ("text", CompLogLevelError,
			    "Couldn't create %d x %d pixmap.", w, h);
	    pango_font_description_free (font);
	    g_object_unref (layout);
	    return FALSE;
	}

	surface = cairo_xlib_surface_create_with_xrender_format (dpy, pixmap,
								 screen, format,
								 w, h);
	if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
	{
	    compLogMessage ("text", CompLogLevelError,
			    "Couldn't create surface.");
	    pango_font_description_free (font);
	    g_object_unref (layout);
	    XFreePixmap (dpy, pixmap);
	    return FALSE;
	}

	cr = cairo_create (surface);
	if (cairo_status (cr) != CAIRO_STATUS_SUCCESS)
	{
	    compLogMessage ("text", CompLogLevelError,
			    "Couldn't create cairo context.");
	    cairo_surface_destroy (surface);
	    pango_font_description_free (font);
	    g_object_unref (layout);
	    XFreePixmap (dpy, pixmap);
	    return FALSE;
	}

	pango_cairo_update_layout (cr, layout);

	cairo_save (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint (cr);
	cairo_restore (cr);

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	if (textAttrib->style & TEXT_STYLE_BACKGROUND)
	{
	    textDrawTextBackground (cr, 0, 0, w, h,
				    MIN (textAttrib->backgroundHMargin,
					 textAttrib->backgroundVMargin));
	    cairo_set_source_rgba (cr,
				   textAttrib->backgroundColor[0] / 65535.0,
				   textAttrib->backgroundColor[1] / 65535.0,
				   textAttrib->backgroundColor[2] / 65535.0,
				   textAttrib->backgroundColor[3] / 65535.0);
	    cairo_fill (cr);
	    cairo_move_to (cr, textAttrib->backgroundHMargin,
			   textAttrib->backgroundVMargin);
	}
	cairo_set_source_rgba (cr,
			       textAttrib->color[0] / 65535.0,
     			       textAttrib->color[1] / 65535.0,
			       textAttrib->color[2] / 65535.0,
			       textAttrib->color[3] / 65535.0);
	pango_cairo_show_layout (cr, layout);

	g_object_unref (layout);
	cairo_surface_destroy (surface);
	cairo_destroy (cr);
	pango_font_description_free (font);

	*width  = w;
	*height = h;
	*data   = (void *)pixmap;

	return TRUE;
    }

    UNWRAP (td, d, fileToImage);
    status = (*d->fileToImage) (d, path, name, width, height, stride, data);
    WRAP (td, d, fileToImage, textFileToImage);

    return status;
}

static Bool
textInitDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    TextDisplay *td;
    CompOption  *o;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    td = malloc (sizeof (TextDisplay));
    if (!td)
	return FALSE;

    td->visibleNameAtom = XInternAtom (d->display,
				       "_NET_WM_VISIBLE_NAME", 0);

    WRAP (td, d, fileToImage, textFileToImage);

    d->base.privates[displayPrivateIndex].ptr = td;

    o = textGetAbiOption (d);
    o->value.i = TEXT_ABIVERSION;

    return TRUE;
}

static void
textFiniDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    TEXT_DISPLAY (d);

    UNWRAP (td, d, fileToImage);

    free (td);
}

static CompBool
textInitObject (CompPlugin *p,
		CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) textInitDisplay
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
textFiniObject (CompPlugin *p,
		CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) textFiniDisplay
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
textInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
textFini (CompPlugin *p)
{
    freeDisplayPrivateIndex(displayPrivateIndex);
}

CompPluginVTable textVTable = {
    "text",
    0,
    textInit,
    textFini,
    textInitObject,
    textFiniObject,
    0,
    0
};

CompPluginVTable*
getCompPluginInfo (void)
{
    return &textVTable;
}

