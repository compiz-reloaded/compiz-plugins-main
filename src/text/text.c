/*
 * text.c - adds text image support to beryl.
 * Copyright: (C) 2006 Patrick Niklaus
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

#include <compiz.h>
#include "text.h"

static int displayPrivateIndex;

typedef struct _TextDisplay
{
	FileToImageProc fileToImage;
} TextDisplay;

#define GET_TEXT_DISPLAY(d)				    \
    ((TextDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define TEXT_DISPLAY(d)			 \
    TextDisplay *td = GET_TEXT_DISPLAY (d)

static char* textGetUtf8Property(CompDisplay *d, Window id, Atom atom)
{
	Atom type;
	int format;
	unsigned long nitems;
	unsigned long bytes_after;
	char *val;
	int result;
	char *retval;

	type = None;
	val = NULL;
	result = XGetWindowProperty (d->display, id, atom, 0L, 65536, False,
								 d->utf8StringAtom,	&type, &format, &nitems,
								 &bytes_after, (unsigned char **)&val);

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

static char* textGetTextProperty(CompDisplay *d, Window id, Atom atom)
{
	XTextProperty text;
	char *retval;

	text.nitems = 0;
	if (XGetTextProperty (d->display, id, &text, atom))
	{
		retval = strndup ((char *)text.value,text.nitems);
		if (text.value)
			XFree (text.value);
	}
	else
	{
		retval = NULL;
	}
	return retval;
}

static char* textGetWindowName(CompDisplay *d, Window id)
{
	char *name = NULL;

	Atom visName = XInternAtom(d->display, "_NET_WM_VISIBLE_NAME", FALSE);

	name = textGetUtf8Property (d, id, visName);

	if (!name)
		name = textGetUtf8Property(d, id, d->wmNameAtom);

	if (!name)
		name = textGetTextProperty (d, id, XA_WM_NAME);

	return name;
}


static Bool textFileToImage(CompDisplay *d,
			   const char *path,
			   const char *name,
			   int *width, int *height, int *stride, void **data)
{
	Bool status = FALSE;

	if (path && name &&	strcmp(path, TEXT_ID) == 0)
	{
		cairo_t *cr;
		cairo_surface_t *surface;
		PangoLayout *layout;
		Pixmap pixmap;
		XRenderPictFormat *format;
		CompTextAttrib *text_attrib = (CompTextAttrib*) name; // get it through the name
		int w,h;

		Display *display = d->display;
		Screen *screen = ScreenOfDisplay(display, text_attrib->screen->screenNum);

		if (!screen) {
			compLogMessage (d, "text", CompLogLevelError,
							"Couldn't get screen for %d.", 
							text_attrib->screen->screenNum);
			return FALSE;
		}

		format = XRenderFindStandardFormat(display, PictStandardARGB32);
		if (!format) {
			compLogMessage (d, "text", CompLogLevelError,
							"Couldn't get format.");
			return FALSE;
		}

		pixmap = XCreatePixmap(display, text_attrib->screen->root, 1, 1, 32);
		if (!pixmap) {
			compLogMessage (d, "text", CompLogLevelError,
							"Couldn't create pixmap.");
			return FALSE;
		}

		surface = cairo_xlib_surface_create_with_xrender_format(
						display, pixmap, screen, format, 1, 1);

		if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
			compLogMessage (d, "text", CompLogLevelError,
							"Couldn't create surface.");
			XFreePixmap(display, pixmap);
			return FALSE;
		}

		cr = cairo_create(surface);
		if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
			compLogMessage (d, "text", CompLogLevelError,
							"Couldn't create cairo context.");
			XFreePixmap(display, pixmap);
			return FALSE;
		}

		// init pango
		layout = pango_cairo_create_layout(cr);
		if (!layout) {
			compLogMessage (d, "text", CompLogLevelError,
							"Couldn't create pango layout.");
			XFreePixmap(display, pixmap);
			return FALSE;
		}

		PangoFontDescription *font = pango_font_description_new();


		pango_font_description_set_family(font, text_attrib->family);
		pango_font_description_set_absolute_size(font, text_attrib->size * PANGO_SCALE);

		pango_font_description_set_style(font, PANGO_STYLE_NORMAL);

		if (text_attrib->style & TEXT_STYLE_BOLD)
			pango_font_description_set_weight(font, PANGO_WEIGHT_BOLD);

		if (text_attrib->style & TEXT_STYLE_ITALIC)
			pango_font_description_set_style(font, PANGO_STYLE_ITALIC);

		pango_layout_set_font_description(layout, font);

		if (text_attrib->ellipsize)
			pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

		switch (text_attrib->renderMode) {
		case TextRenderNormal:
			{
				char *text = (char*) text_attrib->data;
				pango_layout_set_text(layout, text, -1);
			}
			break;
		case TextRenderWindowTitle:
			{
				Window xid = (Window) text_attrib->data;
				char *text = textGetWindowName(d, xid);

				if (text) {
					pango_layout_set_text(layout, text, -1);
					free(text);
				} else {
					pango_font_description_free(font);
					g_object_unref(layout);
					cairo_surface_destroy(surface);
					cairo_destroy(cr);
					XFreePixmap(display, pixmap);
					return FALSE;
				}
			}
			break;
		default:
			break;
		}

		pango_layout_get_pixel_size(layout, &w, &h);

		w = MIN(text_attrib->maxwidth, w);
		h = MIN(text_attrib->maxheight, h);
	
		// update the size of the pango layout
		pango_layout_set_width(layout, text_attrib->maxwidth * PANGO_SCALE);

		cairo_surface_destroy(surface);
		cairo_destroy(cr);
		XFreePixmap(display, pixmap);

		pixmap = XCreatePixmap(display, text_attrib->screen->root, w, h, 32);

		surface = cairo_xlib_surface_create_with_xrender_format(display, pixmap,
				  screen, format, w, h);

		if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
			compLogMessage (d, "text", CompLogLevelError,
							"Couldn't create surface.");
			XFreePixmap(display, pixmap);
			return FALSE;
		}

		cr = cairo_create(surface);
		if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
			compLogMessage (d, "text", CompLogLevelError,
							"Couldn't create cairo context.");
			XFreePixmap(display, pixmap);
			return FALSE;
		}

		pango_cairo_update_layout(cr,layout);

		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cr);
		cairo_restore(cr);

		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

		cairo_set_source_rgba(cr,
							  text_attrib->color[0] / 65535.0,
							  text_attrib->color[1] / 65535.0,
							  text_attrib->color[2] / 65535.0,
							  text_attrib->color[3] / 65535.0);
		pango_cairo_show_layout(cr, layout);

		g_object_unref(layout);
		cairo_surface_destroy(surface);
		cairo_destroy(cr);
		pango_font_description_free(font);

		*width = w;
		*height = h;
		*data = (void *)pixmap;

		return TRUE;
	}

	TEXT_DISPLAY(d);

	UNWRAP(td, d, fileToImage);
	status = (*d->fileToImage) (d, path, name, width, height, stride, data);
	WRAP(td, d, fileToImage, textFileToImage);

	return status;
}

static Bool textInitDisplay(CompPlugin *p, CompDisplay *d)
{
	TextDisplay *td;

	td = malloc(sizeof(TextDisplay));
	if (!td)
		return FALSE;

	WRAP(td, d, fileToImage, textFileToImage);

	d->privates[displayPrivateIndex].ptr = td;

	return TRUE;
}

static void textFiniDisplay(CompPlugin *p, CompDisplay *d)
{
	TEXT_DISPLAY(d);

	UNWRAP(td, d, fileToImage);

	free(td);
}


static Bool textInit(CompPlugin *p)
{
	displayPrivateIndex = allocateDisplayPrivateIndex();
	if (displayPrivateIndex < 0)
		return FALSE;

	return TRUE;
}

static void textFini(CompPlugin *p)
{
	if (displayPrivateIndex >= 0)
		freeDisplayPrivateIndex(displayPrivateIndex);
}

static int textGetVersion (CompPlugin *p, int version)
{
    return ABIVERSION;
}

CompPluginVTable textVTable = {
	"text",
	textGetVersion,
	0,
	textInit,
	textFini,
	textInitDisplay,
	textFiniDisplay,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0
};

CompPluginVTable *getCompPluginInfo(void)
{
	return &textVTable;
}
