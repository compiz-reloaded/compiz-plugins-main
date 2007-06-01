/*
 *
 * Compiz thumbnail plugin
 *
 * thumbnail.c
 *
 * Copyright : (C) 2007 by Dennis Kasprzyk
 * E-mail    : onestone@beryl-project.org
 *
 * Based on thumbnail.c:
 * Copyright : (C) 2007 Stjepan Glavina
 * E-mail    : stjepang@gmail.com
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
#include <sys/types.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>

#include <compiz.h>

#include "text-plugin.h"
#include "thumbnail_tex.h"
#include "thumbnail_options.h"

#define GET_THUMB_DISPLAY(d)				       \
    ((ThumbDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define THUMB_DISPLAY(d)		       \
    ThumbDisplay *td = GET_THUMB_DISPLAY (d)

#define GET_THUMB_SCREEN(s, td)				   \
    ((ThumbScreen *) (s)->privates[(td)->screenPrivateIndex].ptr)

#define THUMB_SCREEN(s)						      \
    ThumbScreen *ts = GET_THUMB_SCREEN (s, GET_THUMB_DISPLAY (s->display))

#define GET_THUMB_WINDOW(w, ts)                               \
    ((ThumbWindow *) (w)->privates[(ts)->windowPrivateIndex].ptr)

#define THUMB_WINDOW_PTR(w)                                   \
    GET_THUMB_WINDOW  (w,                                    \
            GET_THUMB_SCREEN  (w->screen,                            \
                GET_THUMB_DISPLAY (w->screen->display)))

#define THUMB_WINDOW(w)                                       \
    ThumbWindow *tw = THUMB_WINDOW_PTR(w)

#define WIN_X(w) ((w)->attrib.x - (w)->input.left)
#define WIN_Y(w) ((w)->attrib.y - (w)->input.top)
#define WIN_W(w) ((w)->width + (w)->input.left + (w)->input.right)
#define WIN_H(w) ((w)->height + (w)->input.top + (w)->input.bottom)

#define THUMB_MOUSE_UPDATE_SPEED 100

#define TEXT_DISTANCE 10

static int displayPrivateIndex;

typedef struct _ThumbDisplay
{
	int screenPrivateIndex;

	HandleEventProc handleEvent;

	Atom winIconGeometryAtom;

} ThumbDisplay;

typedef struct _Thumbnail
{
	int x;
	int y;
	int width;
	int height;
	float scale;
	float opacity;
	int offset;
	CompWindow *win;
	CompWindow *dock;
	CompTexture textTexture;
	Pixmap textPixmap;
	int tWidth;
	int tHeight;
} Thumbnail;

typedef struct _ThumbScreen
{
	int windowPrivateIndex;

	CompTimeoutHandle mouseTimeout;
	CompTimeoutHandle displayTimeout;

	PreparePaintScreenProc preparePaintScreen;
	PaintOutputProc paintOutput;
	PaintWindowProc paintWindow;
	DonePaintScreenProc donePaintScreen;
	DamageWindowRectProc damageWindowRect;
	WindowResizeNotifyProc windowResizeNotify;
	PaintTransformedOutputProc paintTransformedOutput;

	CompWindow *dock;
	CompWindow *pointedWin;
	Bool showingThumb;
	Thumbnail thumb;
	Thumbnail oldThumb;
	Bool painted;

	CompTexture glowTexture;
	CompTexture windowTexture;

	int x;
	int y;

} ThumbScreen;

typedef struct _IconGeometry
{
	int x;
	int y;
	int width;
	int height;
	Bool isSet;
} IconGeometry;

typedef struct _ThumbWindow
{
	IconGeometry ig;
} ThumbWindow;

static void
freeThumbText (CompScreen * s, Thumbnail * t)
{
	if (!t->textPixmap)
		return;
	releasePixmapFromTexture (s, &t->textTexture);
	initTexture (s, &t->textTexture);
	XFreePixmap (s->display->display, t->textPixmap);
	t->textPixmap = None;
}

static void
renderThumbText (CompScreen * s, Thumbnail * t, Bool freeThumb)
{
	if (freeThumb)
		freeThumbText (s, t);

	CompTextAttrib tA;

	tA.maxwidth = t->width;
	tA.maxheight = 100;
	tA.screen = s;
	tA.size = thumbnailGetFontSize (s);
	tA.color[0] = thumbnailGetFontColorRed (s);
	tA.color[1] = thumbnailGetFontColorGreen (s);
	tA.color[2] = thumbnailGetFontColorBlue (s);
	tA.color[3] = thumbnailGetFontColorAlpha (s);
	tA.style = (thumbnailGetFontBold (s)) ? TEXT_STYLE_BOLD : TEXT_STYLE_NORMAL;
	tA.family = "Sans";
	tA.ellipsize = TRUE;

	tA.renderMode = TextRenderWindowTitle;
	tA.data = (void *)(t->win->id);


	int stride;
	void *data;

	initTexture (s, &t->textTexture);

	if ((*s->display->fileToImage) (s->display, TEXT_ID, (char *)&tA,
									&t->tWidth, &t->tHeight, &stride, &data))
	{
		t->textPixmap = (Pixmap) data;
		bindPixmapToTexture (s, &t->textTexture, t->textPixmap,
							 t->tWidth, t->tHeight, 32);
	}
	else
	{
		t->textPixmap = None;
		t->tWidth = 0;
		t->tHeight = 0;
	}


}


static void
updateWindowIconGeometry (CompWindow * w)
{
	THUMB_DISPLAY (w->screen->display);
	THUMB_WINDOW (w);

	Atom actual;
	int result, format;
	unsigned long n, left;
	unsigned char *data;
	unsigned long *mydata;

	result = XGetWindowProperty (w->screen->display->display,
								 w->id, td->winIconGeometryAtom, 0L, 4L,
								 FALSE, XA_CARDINAL, &actual, &format, &n,
								 &left, &data);

	mydata = (unsigned long *)data;

	tw->ig.isSet = FALSE;

	if (result == Success && actual == XA_CARDINAL && n == 4)
	{
		tw->ig.x = mydata[0];
		tw->ig.y = mydata[1];
		tw->ig.width = mydata[2];
		tw->ig.height = mydata[3];
		tw->ig.isSet = TRUE;
		XFree (data);
	}
}

static void
damageThumbRegion (CompScreen * s, Thumbnail * t)
{
	REGION region;
	region.extents.x1 = t->x - t->offset;
	region.extents.y1 = t->y - t->offset;
	region.extents.x2 = region.extents.x1 + t->width + (t->offset * 2);
	region.extents.y2 = region.extents.y1 + t->height + (t->offset * 2);
	if (t->textPixmap)
		region.extents.y2 += t->tHeight + TEXT_DISTANCE;
	region.rects = &region.extents;
	region.numRects = region.size = 1;

	damageScreenRegion (s, &region);
}

#define GET_DISTANCE(a,b) \
	(sqrt((((a)[0] - (b)[0]) * ((a)[0] - (b)[0])) + \
		  (((a)[1] - (b)[1]) * ((a)[1] - (b)[1]))))

static void
thumbUpdateThumbnail (CompScreen * s)
{
	THUMB_SCREEN (s);

	if (ts->thumb.win == ts->pointedWin)
		return;

	if (ts->thumb.opacity > 0.0 && ts->oldThumb.opacity > 0.0)
		return;

	if (ts->thumb.win)
		damageThumbRegion (s, &ts->thumb);

	freeThumbText (s, &ts->oldThumb);
	ts->oldThumb = ts->thumb;

	ts->thumb.win = ts->pointedWin;
	ts->thumb.dock = ts->dock;

	if (!ts->thumb.win)
		return;

	float maxSize = thumbnailGetThumbSize (s);
	double scale = 1.0;
	// do we nee to scale the window down?
	if (WIN_W (ts->thumb.win) > maxSize || WIN_H (ts->thumb.win) > maxSize)
	{
		if (WIN_W (ts->thumb.win) >= WIN_H (ts->thumb.win))
			scale = maxSize / WIN_W (ts->thumb.win);
		else
			scale = maxSize / WIN_H (ts->thumb.win);
	}
	ts->thumb.width = WIN_W (ts->thumb.win) * scale;
	ts->thumb.height = WIN_H (ts->thumb.win) * scale;

	ts->thumb.scale = scale;

	THUMB_WINDOW (ts->thumb.win);

	if (thumbnailGetTitleEnabled (s))
		renderThumbText (s, &ts->thumb, FALSE);
	else
		freeThumbText (s, &ts->thumb);

	int igMidPoint[2] = { tw->ig.x + (tw->ig.width / 2),
		tw->ig.y + (tw->ig.height / 2)
	};

	int tMidPoint[2];
	int tPos[2];
	int tmpPos[2];
	float distance = 1000000;
	int off = thumbnailGetBorder (s);
	int oDev = outputDeviceForPoint (s, tw->ig.x + (tw->ig.width / 2),
									 tw->ig.y + (tw->ig.height / 2));
	int ox1, oy1, ox2, oy2, ow, oh;
	if (s->nOutputDev == 1 || oDev > s->nOutputDev)
	{
		ox1 = 0;
		oy1 = 0;
		ox2 = s->width;
		oy2 = s->height;
		ow = s->width;
		oh = s->height;
	}
	else
	{
		ox1 = s->outputDev[oDev].region.extents.x1;
		ox2 = s->outputDev[oDev].region.extents.x2;
		oy1 = s->outputDev[oDev].region.extents.y1;
		oy2 = s->outputDev[oDev].region.extents.y2;
		ow = ox2 - ox1;
		oh = oy2 - oy1;
	}

	int tHeight = ts->thumb.height;
	if (ts->thumb.textPixmap)
		tHeight += ts->thumb.tHeight + TEXT_DISTANCE;

	// failsave position
	tPos[0] = igMidPoint[0] - (ts->thumb.width / 2.0);
	if (tw->ig.y - tHeight >= 0)
	{
		tPos[1] = tw->ig.y - tHeight;
	}
	else
	{
		tPos[1] = tw->ig.y + tw->ig.height;
	}

	// above
	tmpPos[0] = igMidPoint[0] - (ts->thumb.width / 2.0);
	if (tmpPos[0] - off < ox1)
		tmpPos[0] = ox1 + off;
	if (tmpPos[0] + off + ts->thumb.width > ox2)
	{
		if (ts->thumb.width + (2 * off) <= ow)
			tmpPos[0] = ox2 - ts->thumb.width - off;
		else
			tmpPos[0] = ox1 + off;
	}
	tMidPoint[0] = tmpPos[0] + (ts->thumb.width / 2.0);

	tmpPos[1] = WIN_Y (ts->dock) - tHeight - off;
	tMidPoint[1] = tmpPos[1] + (tHeight / 2.0);
	if (tmpPos[1] > oy1)
	{
		tPos[0] = tmpPos[0];
		tPos[1] = tmpPos[1];
		distance = GET_DISTANCE (igMidPoint, tMidPoint);
	}

	// below
	tmpPos[1] = WIN_Y (ts->dock) + WIN_H (ts->dock) + off;
	tMidPoint[1] = tmpPos[1] + (tHeight / 2.0);
	if (tmpPos[1] + tHeight + off < oy2 &&
		GET_DISTANCE (igMidPoint, tMidPoint) < distance)
	{
		tPos[0] = tmpPos[0];
		tPos[1] = tmpPos[1];
		distance = GET_DISTANCE (igMidPoint, tMidPoint);
	}

	// left
	tmpPos[1] = igMidPoint[1] - (tHeight / 2.0);
	if (tmpPos[1] - off < oy1)
		tmpPos[1] = oy1 + off;
	if (tmpPos[1] + off + tHeight > oy2)
	{
		if (tHeight + (2 * off) <= oh)
			tmpPos[1] = oy2 - ts->thumb.height - off;
		else
			tmpPos[1] = oy1 + off;
	}
	tMidPoint[1] = tmpPos[1] + (tHeight / 2.0);

	tmpPos[0] = WIN_X (ts->dock) - ts->thumb.width - off;
	tMidPoint[0] = tmpPos[0] + (ts->thumb.width / 2.0);
	if (tmpPos[0] > ox1 && GET_DISTANCE (igMidPoint, tMidPoint) < distance)
	{
		tPos[0] = tmpPos[0];
		tPos[1] = tmpPos[1];
		distance = GET_DISTANCE (igMidPoint, tMidPoint);
	}

	// right
	tmpPos[0] = WIN_X (ts->dock) + WIN_W (ts->dock) + off;
	tMidPoint[0] = tmpPos[0] + (ts->thumb.width / 2.0);
	if (tmpPos[0] + ts->thumb.width + off < ox2 &&
		GET_DISTANCE (igMidPoint, tMidPoint) < distance)
	{
		tPos[0] = tmpPos[0];
		tPos[1] = tmpPos[1];
		distance = GET_DISTANCE (igMidPoint, tMidPoint);
	}

	ts->thumb.x = tPos[0];
	ts->thumb.y = tPos[1];

	ts->thumb.offset = off;

	ts->thumb.opacity = 0.0;

	damageThumbRegion (s, &ts->thumb);
}

static Bool
thumbShowThumbnail (void *vs)
{
	CompScreen *s = (CompScreen *) vs;
	THUMB_SCREEN (s);
	ts->showingThumb = TRUE;
	thumbUpdateThumbnail (s);
	damageThumbRegion (s, &ts->thumb);
	return TRUE;
}

static Bool
checkPosition (CompWindow * w)
{
	if (thumbnailGetCurrentViewport (w->screen))
	{
		// TODO: We need a faster calculation here
		Bool onViewport = FALSE;
		Region reg = XCreateRegion ();
		XIntersectRegion (w->region, &w->screen->region, reg);
		if (reg->numRects)
			onViewport = TRUE;
		XDestroyRegion (reg);
		if (!onViewport)
			return FALSE;
	}

	return TRUE;
}

static Bool
thumbUpdateMouse (void *vs)
{
	CompScreen *s = (CompScreen *) vs;

	THUMB_SCREEN (s);

	int winX, winY;
	int rootX, rootY;
	unsigned int mask_return;
	Window root_return;
	Window child_return;

	XQueryPointer (s->display->display, s->root,
				   &root_return, &child_return,
				   &rootX, &rootY, &winX, &winY, &mask_return);

	CompWindow *w = findWindowAtDisplay (s->display, child_return);

	if (w && w->type & CompWindowTypeDockMask)
	{
		if (ts->dock != w)
		{
			ts->dock = w;
			if (ts->displayTimeout)
			{
				compRemoveTimeout (ts->displayTimeout);
				ts->displayTimeout = 0;
			}
			ts->pointedWin = NULL;
			ts->showingThumb = FALSE;
		}

		CompWindow *cw = s->windows;
		CompWindow *found = NULL;
		for (; cw && !found; cw = cw->next)
		{
			THUMB_WINDOW (cw);
			if (!tw->ig.isSet)
				continue;
			if (cw->attrib.map_state != IsViewable)
				continue;
			if (cw->state & CompWindowStateSkipTaskbarMask)
				continue;
			if (cw->state & CompWindowStateSkipPagerMask)
				continue;
			if (!cw->managed)
				continue;
			if (!w->texture->pixmap)
				continue;

			if (rootX >= tw->ig.x && rootX < tw->ig.x + tw->ig.width &&
				rootY >= tw->ig.y && rootY < tw->ig.y + tw->ig.height &&
				checkPosition (cw))
			{
				found = cw;
			}
		}

		if (found)
		{
			if (!ts->showingThumb &&
				!(ts->thumb.opacity != 0.0 && ts->thumb.win == found))
			{
				if (ts->displayTimeout)
				{
					if (ts->pointedWin != found)
					{
						compRemoveTimeout (ts->displayTimeout);
						ts->displayTimeout =
							compAddTimeout (thumbnailGetShowDelay (s),
											thumbShowThumbnail, s);
					}
				}
				else
				{
					ts->displayTimeout =
						compAddTimeout (thumbnailGetShowDelay (s),
										thumbShowThumbnail, s);
				}
			}
			ts->pointedWin = found;
			thumbUpdateThumbnail (s);
		}
		else
		{
			ts->dock = NULL;
			if (ts->displayTimeout)
			{
				compRemoveTimeout (ts->displayTimeout);
				ts->displayTimeout = 0;
			}
			ts->pointedWin = 0;
			ts->showingThumb = FALSE;
		}

	}
	else
	{
		ts->dock = NULL;
		if (ts->displayTimeout)
		{
			compRemoveTimeout (ts->displayTimeout);
			ts->displayTimeout = 0;
		}
		ts->pointedWin = NULL;
		ts->showingThumb = FALSE;
	}

	ts->mouseTimeout =
		compAddTimeout (THUMB_MOUSE_UPDATE_SPEED, thumbUpdateMouse, s);
	return FALSE;
}


static void
thumbWindowResizeNotify (CompWindow * w, int dx, int dy, int dwidth,
						 int dheight)
{
	THUMB_SCREEN (w->screen);

	thumbUpdateThumbnail (w->screen);

	UNWRAP (ts, w->screen, windowResizeNotify);
	(*w->screen->windowResizeNotify) (w, dx, dy, dwidth, dheight);
	WRAP (ts, w->screen, windowResizeNotify, thumbWindowResizeNotify);
}

static void
thumbHandleEvent (CompDisplay * d, XEvent * event)
{
	THUMB_DISPLAY (d);

	UNWRAP (td, d, handleEvent);
	(*d->handleEvent) (d, event);
	WRAP (td, d, handleEvent, thumbHandleEvent);

	CompWindow *w;

	switch (event->type)
	{
	case PropertyNotify:
		if (event->xproperty.atom == td->winIconGeometryAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
			{
				updateWindowIconGeometry (w);
			}
		}
		else if (event->xproperty.atom == d->wmNameAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);

			if (w)
			{
				THUMB_SCREEN (w->screen);
				if (ts->thumb.win)
					renderThumbText (w->screen, &ts->thumb, TRUE);
			}
		}
		break;
	case ButtonPress:
	{
		CompScreen *s = findScreenAtDisplay (d, event->xbutton.root);
		if (!s)
			break;

		THUMB_SCREEN (s);
		ts->dock = NULL;
		if (ts->displayTimeout)
		{
			compRemoveTimeout (ts->displayTimeout);
			ts->displayTimeout = 0;
		}
		ts->pointedWin = 0;
		ts->showingThumb = FALSE;
	}
		break;
	default:
		break;
	}
}


static void
thumbPaintThumb (CompScreen * s, Thumbnail * t,
				 const CompTransform * transform)
{
	THUMB_SCREEN (s);
	AddWindowGeometryProc oldAddWindowGeometry;
	CompWindow *w = t->win;

	if (!w)
		return;

	int wx = t->x;
	int wy = t->y;
	float width = t->width;
	float height = t->height;
	WindowPaintAttrib sAttrib = w->paint;

	if (t->textPixmap)
		height += t->tHeight + TEXT_DISTANCE;

	/* Wrap drawWindowGeometry to make sure the general
	   drawWindowGeometry function is used */
	oldAddWindowGeometry = w->screen->addWindowGeometry;
	w->screen->addWindowGeometry = addWindowGeometry;

	unsigned int mask = PAINT_WINDOW_TRANSFORMED_MASK |
		PAINT_WINDOW_TRANSLUCENT_MASK;

	if (w->texture->pixmap)
	{

		int off = t->offset;

		glEnable (GL_BLEND);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		if (thumbnailGetWindowLike (s))
		{
			glColor4f (1.0, 1.0, 1.0, t->opacity);
			enableTexture (s, &ts->windowTexture, COMP_TEXTURE_FILTER_GOOD);
		}
		else
		{
			glColor4us (thumbnailGetThumbColorRed (s),
						thumbnailGetThumbColorGreen (s),
						thumbnailGetThumbColorBlue (s),
						thumbnailGetThumbColorAlpha (s) * t->opacity);

			enableTexture (s, &ts->glowTexture, COMP_TEXTURE_FILTER_GOOD);
		}

		glBegin (GL_QUADS);

		glTexCoord2f (1, 0);
		glVertex2f (wx, wy);
		glVertex2f (wx, wy + height);
		glVertex2f (wx + width, wy + height);
		glVertex2f (wx + width, wy);

		glTexCoord2f (0, 1);
		glVertex2f (wx - off, wy - off);
		glTexCoord2f (0, 0);
		glVertex2f (wx - off, wy);
		glTexCoord2f (1, 0);
		glVertex2f (wx, wy);
		glTexCoord2f (1, 1);
		glVertex2f (wx, wy - off);

		glTexCoord2f (1, 1);
		glVertex2f (wx + width, wy - off);
		glTexCoord2f (1, 0);
		glVertex2f (wx + width, wy);
		glTexCoord2f (0, 0);
		glVertex2f (wx + width + off, wy);
		glTexCoord2f (0, 1);
		glVertex2f (wx + width + off, wy - off);

		glTexCoord2f (0, 0);
		glVertex2f (wx - off, wy + height);
		glTexCoord2f (0, 1);
		glVertex2f (wx - off, wy + height + off);
		glTexCoord2f (1, 1);
		glVertex2f (wx, wy + height + off);
		glTexCoord2f (1, 0);
		glVertex2f (wx, wy + height);

		glTexCoord2f (1, 0);
		glVertex2f (wx + width, wy + height);
		glTexCoord2f (1, 1);
		glVertex2f (wx + width, wy + height + off);
		glTexCoord2f (0, 1);
		glVertex2f (wx + width + off, wy + height + off);
		glTexCoord2f (0, 0);
		glVertex2f (wx + width + off, wy + height);

		glTexCoord2f (1, 1);
		glVertex2f (wx, wy - off);
		glTexCoord2f (1, 0);
		glVertex2f (wx, wy);
		glTexCoord2f (1, 0);
		glVertex2f (wx + width, wy);
		glTexCoord2f (1, 1);
		glVertex2f (wx + width, wy - off);

		glTexCoord2f (1, 0);
		glVertex2f (wx, wy + height);
		glTexCoord2f (1, 1);
		glVertex2f (wx, wy + height + off);
		glTexCoord2f (1, 1);
		glVertex2f (wx + width, wy + height + off);
		glTexCoord2f (1, 0);
		glVertex2f (wx + width, wy + height);

		glTexCoord2f (0, 0);
		glVertex2f (wx - off, wy);
		glTexCoord2f (0, 0);
		glVertex2f (wx - off, wy + height);
		glTexCoord2f (1, 0);
		glVertex2f (wx, wy + height);
		glTexCoord2f (1, 0);
		glVertex2f (wx, wy);

		glTexCoord2f (1, 0);
		glVertex2f (wx + width, wy);
		glTexCoord2f (1, 0);
		glVertex2f (wx + width, wy + height);
		glTexCoord2f (0, 0);
		glVertex2f (wx + width + off, wy + height);
		glTexCoord2f (0, 0);
		glVertex2f (wx + width + off, wy);

		glEnd ();

		if (thumbnailGetWindowLike (s))
		{
			disableTexture (s, &ts->windowTexture);
		}
		else
		{
			disableTexture (s, &ts->glowTexture);
		}

		glColor4usv (defaultColor);
		glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		if (t->textPixmap)
		{
			glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor4f (1.0, 1.0, 1.0, t->opacity);

			enableTexture (s, &t->textTexture, COMP_TEXTURE_FILTER_GOOD);

			CompMatrix *m = &t->textTexture.matrix;

			float ox = 0.0;
			if (t->tWidth < width)
				ox = (width - t->tWidth) / 2.0;

			float w = MIN (width, t->tWidth);
			float h = t->tHeight;

			glBegin (GL_QUADS);

			glTexCoord2f (COMP_TEX_COORD_X (m, 0), COMP_TEX_COORD_Y (m, 0));
			glVertex2f (wx + ox, wy + height - h);
			glTexCoord2f (COMP_TEX_COORD_X (m, 0), COMP_TEX_COORD_Y (m, h));
			glVertex2f (wx + ox, wy + height);
			glTexCoord2f (COMP_TEX_COORD_X (m, w), COMP_TEX_COORD_Y (m, h));
			glVertex2f (wx + ox + w, wy + height);
			glTexCoord2f (COMP_TEX_COORD_X (m, w), COMP_TEX_COORD_Y (m, 0));
			glVertex2f (wx + ox + w, wy + height - h);

			glEnd ();

			disableTexture (s, &t->textTexture);
			glColor4usv (defaultColor);
		}

		glDisable (GL_BLEND);
		screenTexEnvMode (s, GL_REPLACE);

		glColor4usv (defaultColor);

		sAttrib.opacity *= t->opacity;
		sAttrib.yScale = t->scale;
		sAttrib.xScale = t->scale;

		sAttrib.xTranslate =
			wx - w->attrib.x + w->input.left * sAttrib.xScale;
		sAttrib.yTranslate = wy - w->attrib.y + w->input.top * sAttrib.yScale;

		GLenum filter = s->display->textureFilter;

		if (thumbnailGetMipmap (s))
			s->display->textureFilter = GL_LINEAR_MIPMAP_LINEAR;

		FragmentAttrib fragment;
		CompTransform wTransform = *transform;

		initFragmentAttrib (&fragment, &sAttrib);

		matrixTranslate (&wTransform, w->attrib.x, w->attrib.y, 0.0f);
		matrixScale (&wTransform, sAttrib.xScale, sAttrib.yScale, 0.0f);
		matrixTranslate (&wTransform,
						 sAttrib.xTranslate / sAttrib.xScale - w->attrib.x,
						 sAttrib.yTranslate / sAttrib.yScale - w->attrib.y,
						 0.0f);

		glPushMatrix ();
		glLoadMatrixf (wTransform.m);

		(w->screen->drawWindow) (w, &wTransform, &fragment, &infiniteRegion,
								 mask);
		glPopMatrix ();
		s->display->textureFilter = filter;
	}

	w->screen->addWindowGeometry = oldAddWindowGeometry;
}

static void
thumbPreparePaintScreen (CompScreen * s, int ms)
{
	THUMB_SCREEN (s);

	float val = ms;
	val /= 1000;
	val /= thumbnailGetFadeSpeed (s);

	if (otherScreenGrabExist (s, 0))
	{
		ts->dock = NULL;
		if (ts->displayTimeout)
		{
			compRemoveTimeout (ts->displayTimeout);
			ts->displayTimeout = 0;
		}
		ts->pointedWin = 0;
		ts->showingThumb = FALSE;
	}

	if (ts->showingThumb && ts->thumb.win == ts->pointedWin)
	{
		ts->thumb.opacity = MIN (1.0, ts->thumb.opacity + val);
	}
	if (!ts->showingThumb || ts->thumb.win != ts->pointedWin)
	{
		ts->thumb.opacity = MAX (0.0, ts->thumb.opacity - val);
	}
	ts->oldThumb.opacity = MAX (0.0, ts->oldThumb.opacity - val);

	UNWRAP (ts, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, ms);
	WRAP (ts, s, preparePaintScreen, thumbPreparePaintScreen);
}

static void
thumbDonePaintScreen (CompScreen * s)
{
	THUMB_SCREEN (s);

	if (ts->thumb.opacity > 0.0 && ts->thumb.opacity < 1.0)
		damageThumbRegion (s, &ts->thumb);
	if (ts->oldThumb.opacity > 0.0 && ts->oldThumb.opacity < 1.0)
		damageThumbRegion (s, &ts->oldThumb);

	UNWRAP (ts, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP (ts, s, donePaintScreen, thumbDonePaintScreen);
}

static Bool
thumbPaintOutput (CompScreen * s,
				  const ScreenPaintAttrib * sAttrib,
				  const CompTransform * transform,
				  Region region, CompOutput *output, 
				  unsigned int mask)
{
	Bool status;

	THUMB_SCREEN (s);

	ts->painted = FALSE;

	ts->x = s->x;
	ts->y = s->y;

	unsigned int newMask = mask;

	if ((ts->oldThumb.opacity > 0.0 && ts->oldThumb.win) ||
		(ts->thumb.opacity > 0.0 && ts->thumb.win))
		newMask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;

	UNWRAP (ts, s, paintOutput);
	status =
		(*s->paintOutput) (s, sAttrib, transform, region, output, newMask);
	WRAP (ts, s, paintOutput, thumbPaintOutput);

	if (thumbnailGetAlwaysOnTop (s) && !ts->painted)
	{
		if (ts->oldThumb.opacity > 0.0 && ts->oldThumb.win)
		{
			CompTransform sTransform = *transform;

			transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA,
									&sTransform);

			glPushMatrix ();
			glLoadMatrixf (sTransform.m);
			thumbPaintThumb (s, &ts->oldThumb, &sTransform);
			glPopMatrix ();
		}
		if (ts->thumb.opacity > 0.0 && ts->thumb.win)
		{
			CompTransform sTransform = *transform;

			transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA,
									&sTransform);

			glPushMatrix ();
			glLoadMatrixf (sTransform.m);
			thumbPaintThumb (s, &ts->thumb, &sTransform);
			glPopMatrix ();
		}
	}

	return status;
}

static void
thumbPaintTransformedOutput (CompScreen * s,
							 const ScreenPaintAttrib * sAttrib,
							 const CompTransform * transform,
							 Region region, CompOutput *output, 
							 unsigned int mask)
{

	THUMB_SCREEN (s);

	UNWRAP (ts, s, paintTransformedOutput);
	(*s->paintTransformedOutput) (s, sAttrib, transform, region, output,
								  mask);
	WRAP (ts, s, paintTransformedOutput, thumbPaintTransformedOutput);

	if (thumbnailGetAlwaysOnTop (s) && ts->x == s->x && ts->y == s->y)
	{
		ts->painted = TRUE;
		if (ts->oldThumb.opacity > 0.0 && ts->oldThumb.win)
		{
			CompTransform sTransform = *transform;

			(s->applyScreenTransform) (s, sAttrib, output, &sTransform);
			transformToScreenSpace (s, output, -sAttrib->zTranslate,
									&sTransform);

			glPushMatrix ();
			glLoadMatrixf (sTransform.m);
			thumbPaintThumb (s, &ts->oldThumb, &sTransform);
			glPopMatrix ();
		}
		if (ts->thumb.opacity > 0.0 && ts->thumb.win)
		{
			CompTransform sTransform = *transform;

			(s->applyScreenTransform) (s, sAttrib, output, &sTransform);
			transformToScreenSpace (s, output, -sAttrib->zTranslate,
									&sTransform);

			glPushMatrix ();
			glLoadMatrixf (sTransform.m);
			thumbPaintThumb (s, &ts->thumb, &sTransform);
			glPopMatrix ();
		}
	}
}

static Bool
thumbPaintWindow (CompWindow * w,
				  const WindowPaintAttrib * attrib,
				  const CompTransform * transform,
				  Region region, unsigned int mask)
{
	CompScreen *s = w->screen;
	Bool status;

	THUMB_SCREEN (s);

	UNWRAP (ts, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
	WRAP (ts, s, paintWindow, thumbPaintWindow);

	if (!thumbnailGetAlwaysOnTop (s) && ts->x == s->x && ts->y == s->y)
	{
		if (ts->oldThumb.opacity > 0.0 && ts->oldThumb.win &&
			ts->oldThumb.dock == w)
		{
			thumbPaintThumb (s, &ts->oldThumb, transform);
		}
		if (ts->thumb.opacity > 0.0 && ts->thumb.win && ts->thumb.dock == w)
		{
			thumbPaintThumb (s, &ts->thumb, transform);
		}
	}

	return status;
}

static Bool
thumbDamageWindowRect (CompWindow * w, Bool initial, BoxPtr rect)
{
	Bool status;

	THUMB_SCREEN (w->screen);

	if (ts->thumb.win == w && ts->thumb.opacity > 0.0)
		damageThumbRegion (w->screen, &ts->thumb);
	if (ts->oldThumb.win == w && ts->oldThumb.opacity > 0.0)
		damageThumbRegion (w->screen, &ts->oldThumb);

	UNWRAP (ts, w->screen, damageWindowRect);
	status = (*w->screen->damageWindowRect) (w, initial, rect);
	WRAP (ts, w->screen, damageWindowRect, thumbDamageWindowRect);

	return status;
}

static Bool
thumbInitDisplay (CompPlugin * p, CompDisplay * d)
{
	ThumbDisplay *td;

	td = malloc (sizeof (ThumbDisplay));
	if (!td)
		return FALSE;

	td->screenPrivateIndex = allocateScreenPrivateIndex (d);
	if (td->screenPrivateIndex < 0)
	{
		free (td);
		return FALSE;
	}

	td->winIconGeometryAtom = XInternAtom (d->display,
										   "_NET_WM_ICON_GEOMETRY", FALSE);

	WRAP (td, d, handleEvent, thumbHandleEvent);

	d->privates[displayPrivateIndex].ptr = td;

	return TRUE;
}


static void
thumbFiniDisplay (CompPlugin * p, CompDisplay * d)
{
	THUMB_DISPLAY (d);

	freeScreenPrivateIndex (d, td->screenPrivateIndex);

	UNWRAP (td, d, handleEvent);

	free (td);
}

static Bool
thumbInitWindow (CompPlugin * p, CompWindow * w)
{
	ThumbWindow *tw;

	THUMB_SCREEN (w->screen);

	// create window
	tw = calloc (1, sizeof (ThumbWindow));
	if (!tw)
		return FALSE;

	w->privates[ts->windowPrivateIndex].ptr = tw;

	updateWindowIconGeometry (w);

	return TRUE;
}

static void
thumbFiniWindow (CompPlugin * p, CompWindow * w)
{
	THUMB_WINDOW (w);
	THUMB_SCREEN (w->screen);

	if (ts->thumb.win == w)
	{
		damageThumbRegion (w->screen, &ts->thumb);
		ts->thumb.win = NULL;
		ts->thumb.opacity = 0;
	}

	if (ts->oldThumb.win == w)
	{
		damageThumbRegion (w->screen, &ts->oldThumb);
		ts->oldThumb.win = NULL;
		ts->oldThumb.opacity = 0;
	}


	// free window pointer
	free (tw);
}

static Bool
thumbRGBAimageToTexture (CompScreen * screen,
						 CompTexture * texture,
						 char *image, unsigned int width, unsigned int height)
{
	char *data;
	int i;

	makeScreenCurrent (screen);

	data = malloc (4 * width * height);
	if (!data)
		return FALSE;

	for (i = 0; i < height; i++)
		memcpy (&data[i * width * 4],
				&image[(height - i - 1) * width * 4], width * 4);

	releasePixmapFromTexture (screen, texture);

	if (screen->textureNonPowerOfTwo ||
		(POWER_OF_TWO (width) && POWER_OF_TWO (height)))
	{
		texture->target = GL_TEXTURE_2D;
		texture->matrix.xx = 1.0f / width;
		texture->matrix.yy = -1.0f / height;
		texture->matrix.y0 = 1.0f;
	}
	else
	{
		texture->target = GL_TEXTURE_RECTANGLE_NV;
		texture->matrix.xx = 1.0f;
		texture->matrix.yy = -1.0f;
		texture->matrix.y0 = height;
	}

	if (!texture->name)
		glGenTextures (1, &texture->name);

	glBindTexture (texture->target, texture->name);

	glTexImage2D (texture->target, 0, GL_RGBA, width, height, 0, GL_RGBA,
				  GL_UNSIGNED_BYTE, data);

	texture->filter = GL_NEAREST;

	glTexParameteri (texture->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri (texture->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameteri (texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	texture->wrap = GL_CLAMP_TO_EDGE;
	texture->mipmap = TRUE;

	glBindTexture (texture->target, 0);

	free (data);

	return TRUE;
}

static Bool
thumbInitScreen (CompPlugin * p, CompScreen * s)
{
	ThumbScreen *ts;

	THUMB_DISPLAY (s->display);

	ts = calloc (1, sizeof (ThumbScreen));
	if (!ts)
		return FALSE;

	ts->windowPrivateIndex = allocateWindowPrivateIndex (s);

	WRAP (ts, s, paintOutput, thumbPaintOutput);
	WRAP (ts, s, damageWindowRect, thumbDamageWindowRect);
	WRAP (ts, s, preparePaintScreen, thumbPreparePaintScreen);
	WRAP (ts, s, donePaintScreen, thumbDonePaintScreen);
	WRAP (ts, s, paintWindow, thumbPaintWindow);
	WRAP (ts, s, windowResizeNotify, thumbWindowResizeNotify);
	WRAP (ts, s, paintTransformedOutput, thumbPaintTransformedOutput);

	ts->dock = NULL;
	ts->pointedWin = NULL;
	ts->displayTimeout = 0;
	ts->thumb.win = NULL;
	ts->oldThumb.win = NULL;
	ts->showingThumb = FALSE;

	s->privates[td->screenPrivateIndex].ptr = ts;

	ts->mouseTimeout =
		compAddTimeout (THUMB_MOUSE_UPDATE_SPEED, thumbUpdateMouse, s);

	initTexture (s, &ts->glowTexture);
	initTexture (s, &ts->windowTexture);

	thumbRGBAimageToTexture (s, &ts->glowTexture, glowTex, 32, 32);
	thumbRGBAimageToTexture (s, &ts->windowTexture, windowTex, 32, 32);

	ts->thumb.textPixmap = None;
	ts->oldThumb.textPixmap = None;

	return TRUE;
}


static void
thumbFiniScreen (CompPlugin * p, CompScreen * s)
{
	THUMB_SCREEN (s);

	UNWRAP (ts, s, paintOutput);
	UNWRAP (ts, s, damageWindowRect);
	UNWRAP (ts, s, preparePaintScreen);
	UNWRAP (ts, s, donePaintScreen);
	UNWRAP (ts, s, paintWindow);
	UNWRAP (ts, s, windowResizeNotify);
	UNWRAP (ts, s, paintTransformedOutput);

	if (ts->mouseTimeout)
		compRemoveTimeout (ts->mouseTimeout);

	freeThumbText (s, &ts->thumb);
	freeThumbText (s, &ts->oldThumb);

	finiTexture (s, &ts->glowTexture);
	finiTexture (s, &ts->windowTexture);

	free (ts);
}


static Bool
thumbInit (CompPlugin * p)
{
	displayPrivateIndex = allocateDisplayPrivateIndex ();
	if (displayPrivateIndex < 0)
		return FALSE;

	return TRUE;
}


static void
thumbFini (CompPlugin * p)
{
	if (displayPrivateIndex >= 0)
		freeDisplayPrivateIndex (displayPrivateIndex);
}

static int
thumbnailGetVersion (CompPlugin * plugin, int version)
{
	return ABIVERSION;
}


CompPluginVTable thumbVTable = {
	"thumbnail",
	thumbnailGetVersion,
	0,
	thumbInit,
	thumbFini,
	thumbInitDisplay,
	thumbFiniDisplay,
	thumbInitScreen,
	thumbFiniScreen,
	thumbInitWindow,
	thumbFiniWindow,
	0,							/* thumbGetDisplayOptions */
	0,							/* thumbSetDisplayOption */
	0,
	0,
	0,							/* Deps */
	0,							/* nDeps */
	0,							/* Features */
	0,							/* nFeatures */
};


CompPluginVTable *
getCompPluginInfo (void)
{
	return &thumbVTable;
}
