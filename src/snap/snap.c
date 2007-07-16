/*
 * Beryl Snap Plugin
 * Author : Guillaume "iXce" Seguin
 * Email  : ixce@beryl-project.org
 *
 * Ported by : Patrick "marex" Niklaus
 * Email     : marex@beryl-project.org
 *
 * Copyright (C) 2007 Guillaume Seguin
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
 */

/*
 * TODO
 *  - Apply Edge Resistance to resize
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <compiz.h>

#include "snap_options.h"

/*
 * The neat window coordinates macros
 */
#define WIN_X(w) ((w)->attrib.x - (w)->input.left)
#define WIN_Y(w) ((w)->attrib.y - (w)->input.top)
#define WIN_W(w) ((w)->width + (w)->input.left + (w)->input.right)
#define WIN_H(w) ((w)->height + (w)->input.top + (w)->input.bottom)

/*
 * The window we should snap too if snapping to windows
 */
#define SNAP_WINDOW_TYPE (CompWindowTypeNormalMask  | \
			  CompWindowTypeToolbarMask | \
			  CompWindowTypeMenuMask    | \
			  CompWindowTypeUtilMask)

#define VerticalSnap	(1L << 0)
#define HorizontalSnap	(1L << 1)

#define MoveGrab		(1L << 0)
#define ResizeGrab		(1L << 1)

typedef enum
{
	LeftEdge = 0,
	RightEdge,
	TopEdge,
	BottomEdge
} EdgeType;

typedef struct _Edge Edge;

/* Custom Edge struct, linked list
 * Position, start, end meanings are specific to type :
 *  - LeftEdge/RightEdge : position : x, start/end : y1/y2
 *  - TopEdge/BottomEdge : position : y, start/end : x1/x2
 * id/passed are used during visibility detection when adding edges
 * snapped is straight forward
 */
struct _Edge
{
	Edge *prev;
	Edge *next;

	int position;
	int start;
	int end;
	EdgeType type;
	Bool screenEdge;

	Window id;
	Bool passed;

	Bool snapped;
};

static int displayPrivateIndex;

typedef struct _SnapDisplay
{
	int screenPrivateIndex;
	HandleEventProc handleEvent;

	int avoidSnapMask;

	// Used to check is avoidSnapMask is currently matched
	Bool snapping;
} SnapDisplay;

#define SNAP_SCREEN_OPTION_SNAP_TYPE		0
#define SNAP_SCREEN_OPTION_EDGES_CATEGORIES	1
#define SNAP_SCREEN_OPTION_RESISTANCE_DISTANCE	2
#define SNAP_SCREEN_OPTION_ATTRACTION_DISTANCE	3
#define SNAP_SCREEN_OPTION_NUM			4

typedef struct _SnapScreen
{
	int windowPrivateIndex;

	WindowResizeNotifyProc windowResizeNotify;
	WindowMoveNotifyProc windowMoveNotify;
	WindowGrabNotifyProc windowGrabNotify;
	WindowUngrabNotifyProc windowUngrabNotify;
} SnapScreen;

typedef struct _SnapWindow
{
	// Linked lists
	Edge *edges;
	Edge *reverseEdges;

	// bitfield
	int snapDirection;

	// dx/dy/dw/dh when a window is resisting to user
	int dx;
	int dy;
	int dw;
	int dh;

	// Internals
	Bool snapped;
	int grabbed;

	// Internal, avoids infinite notify loops
	Bool skipNotify;
} SnapWindow;

#define GET_SNAP_DISPLAY(d) \
    ((SnapDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define SNAP_DISPLAY(d) \
    SnapDisplay *sd = GET_SNAP_DISPLAY (d)

#define GET_SNAP_SCREEN(s, sd) \
    ((SnapScreen *) (s)->privates[(sd)->screenPrivateIndex].ptr)

#define SNAP_SCREEN(s) \
    SnapScreen *ss = GET_SNAP_SCREEN (s, GET_SNAP_DISPLAY (s->display))

#define GET_SNAP_WINDOW(w, ss) \
    ((SnapWindow *) (w)->privates[(ss)->windowPrivateIndex].ptr)

#define SNAP_WINDOW(w)                                   \
    SnapWindow *sw = GET_SNAP_WINDOW  (w,                \
            GET_SNAP_SCREEN  (w->screen,                 \
                GET_SNAP_DISPLAY (w->screen->display)))

// Helper functions ------------------------------------------------------------

/*
 * copied from Beryl's core
 */
static void snapScreenGetOutputDevRect(CompScreen * s, int outputDev, XRectangle * outputRect)
{
	if (!outputRect)
		return;

	if (outputDev >= s->nOutputDev)
		outputDev = 0;

	outputRect->x = s->outputDev[outputDev].region.extents.x1;
	outputRect->y = s->outputDev[outputDev].region.extents.y1;
	outputRect->width =
			s->outputDev[outputDev].region.extents.x2 -
			s->outputDev[outputDev].region.extents.x1;
	outputRect->height =
			s->outputDev[outputDev].region.extents.y2 -
			s->outputDev[outputDev].region.extents.y1;
}

/*
 * Wrapper functions to avoid infinite notify loops
 */
static void snapMoveWindow(CompWindow * w, int dx, int dy)
{
	SNAP_WINDOW(w);
	sw->skipNotify = TRUE;
	moveWindow(w, dx, dy, TRUE, TRUE);
	warpPointer(w->screen, dx, dy);
	sw->skipNotify = FALSE;
}

static void snapResizeWindow(CompWindow * w, int dx, int dy, int dw, int dh)
{
	SNAP_WINDOW(w);
	//fprintf (stderr, "Resizing: %d, %d, %d, %d\n", dx, dy, dw, dh);
	sw->skipNotify = TRUE;
	resizeWindow(w, w->attrib.x + dx, w->attrib.y + dy,
				 w->attrib.width + dw, w->attrib.height + dh,
				 w->attrib.border_width);
	sw->skipNotify = FALSE;
}

static void snapFreeEdges(CompWindow * w)
{
	SNAP_WINDOW(w);
	Edge *current = sw->edges, *next;

	while (current)
	{
		next = current->next;
		free(current);
		current = next;
	}
	sw->edges = sw->reverseEdges = NULL;
}

static void snapRemoveEdge(Edge * edge)
{
	if (edge == NULL)
		return;
	if (edge->prev)
		edge->prev->next = edge->next;
	if (edge->next)
		edge->next->prev = edge->prev;
	free(edge);
}

static Edge *snapAddEdge(Edge ** edges, Edge ** reverseEdges, Window id,
						 int position, int start, int end, EdgeType type,
						 Bool screenEdge)
{
	Edge *edge = malloc(sizeof(Edge));

	if (!edge)
		return NULL;
	edge->next = NULL;
	edge->position = position;
	edge->start = start;
	edge->end = end;
	edge->type = type;
	edge->screenEdge = screenEdge;
	edge->snapped = FALSE;
	edge->passed = FALSE;
	edge->id = id;
	if (!*edges)
	{
		edge->prev = NULL;
		*reverseEdges = *edges = edge;
	}
	else
	{
		edge->prev = *reverseEdges;
		edge->prev->next = edge;
		*reverseEdges = edge;
	}
	return edge;
}

/*
 * Add an edge for each rectangle of the region
 */
static void snapAddRegionEdges(SnapWindow * sw, Edge * parent, Region region)
{
	Edge *edge;
	int i, position, start, end;

	for (i = 0; i < region->numRects; i++)
	{
		switch (parent->type)
		{
		case LeftEdge:
		case RightEdge:
			position = region->rects[i].x1;
			start = region->rects[i].y1;
			end = region->rects[i].y2;
			break;
		case TopEdge:
		case BottomEdge:
		default:
			position = region->rects[i].y1;
			start = region->rects[i].x1;
			end = region->rects[i].x2;
		}
		edge = snapAddEdge(&sw->edges, &sw->reverseEdges, parent->id,
						   position, start, end, parent->type,
						   parent->screenEdge);
		if (edge)
			edge->passed = parent->passed;
	}
}

/* Checks if a window is considered a snap window. If it's
 * not visible, returns false. If it's a panel and we're
 * snapping to screen edges, it's considered a snap-window.
 */

#define UNLIKELY(x) __builtin_expect(!!(x),0)

static inline Bool isSnapWindow(CompWindow * w)
{
	//SNAP_SCREEN(w->screen);
	
	if (UNLIKELY(!w))
		return FALSE;
	if (w->invisible || w->hidden || w->minimized)
		return FALSE;
	if ((w->type & SNAP_WINDOW_TYPE) && 
		(snapGetEdgesCategoriesMask(w->screen) & EdgesCategoriesWindowEdgesMask))
		return TRUE;
	if (w->struts && 
		(snapGetEdgesCategoriesMask(w->screen) & EdgesCategoriesScreenEdgesMask))
		return TRUE;
	return FALSE;
}

// Edges update functions ------------------------------------------------------
/*
 * Detect visible windows edges
 */
static void snapUpdateWindowsEdges(CompWindow * w)
{
	CompWindow *c = NULL;
	Edge *e = NULL, *next = NULL;

	SNAP_WINDOW(w);
	Region edgeRegion, resultRegion;
	XRectangle rect;
	Bool remove = FALSE;

	// First add all the windows
	c = w->screen->windows;
	while (c)
	{
		// Just check that we're not trying to snap to current window,
		// that the window is not invisible and of a valid type
		if (c == w || !isSnapWindow(c))
		{
			c = c->next;
			continue;
		}
		snapAddEdge(&sw->edges, &sw->reverseEdges, c->id,
					WIN_Y(c), WIN_X(c), WIN_X(c) + WIN_W(c), TopEdge, FALSE);
		snapAddEdge(&sw->edges, &sw->reverseEdges, c->id,
					WIN_Y(c) + WIN_H(c), WIN_X(c), WIN_X(c) + WIN_W(c),
					BottomEdge, FALSE);
		snapAddEdge(&sw->edges, &sw->reverseEdges, c->id,
					WIN_X(c), WIN_Y(c), WIN_Y(c) + WIN_H(c), LeftEdge, FALSE);
		snapAddEdge(&sw->edges, &sw->reverseEdges, c->id,
					WIN_X(c) + WIN_W(c), WIN_Y(c), WIN_Y(c) + WIN_H(c),
					RightEdge, FALSE);
		c = c->next;
	}

	// Now strip invisible edges
	// Loop through all the windows stack, and through all the edges
	// If an edge has been passed, check if it's in the region window,
	// if the edge is fully under the window, drop it, or if it's only
	// partly covered, cut it/split it in one/two smaller visible edges
	for (c = w->screen->windows; c; c = c->next)
	{
		if (c == w || !isSnapWindow(c))
			continue;
		for (e = sw->edges; e; e = next)
		{
			if (!e->passed)
			{
				if (e->id == c->id)
					e->passed = TRUE;
				next = e->next;
				continue;
			}
			switch (e->type)
			{
				case LeftEdge:
				case RightEdge:
					rect.x = e->position;
					rect.y = e->start;
					rect.width = 1;
					rect.height = e->end - e->start;
					break;
				case TopEdge:
				case BottomEdge:
				default:
					rect.x = e->start;
					rect.y = e->position;
					rect.width = e->end - e->start;
					rect.height = 1;
			}
			// If the edge is in the window region, remove it,
			// if it's partly in the region, split it
			edgeRegion = XCreateRegion();
			resultRegion = XCreateRegion();
			XUnionRectWithRegion(&rect, edgeRegion, edgeRegion);
			XSubtractRegion(edgeRegion, c->region, resultRegion);
			if (XEmptyRegion(resultRegion))
				remove = TRUE;
			else if (!XEqualRegion(edgeRegion, resultRegion))
			{
				snapAddRegionEdges(sw, e, resultRegion);
				remove = TRUE;
			}
			next = e->next;
			if (remove)
			{
				if (e->prev == NULL)
					sw->edges = e->next;
				if (e->next == NULL)
					sw->reverseEdges = e->prev;
				snapRemoveEdge(e);
				remove = FALSE;
			}
			XDestroyRegion(resultRegion);
			XDestroyRegion(edgeRegion);
		}
	}
}

/*
 * Loop on outputDevs and add the extents as edges
 * Note that left side is a right edge, right side a left edge,
 * top side a bottom edge and bottom side a top edge,
 * since they will be snapped as the right/left/bottom/top edge of a window
 */
static void snapUpdateScreenEdges(CompWindow * w)
{
	CompWindow *c = NULL;
	Edge *e = NULL, *next = NULL;

	SNAP_WINDOW(w);
	Region edgeRegion, resultRegion;
	XRectangle rect;
	Bool remove = FALSE;

	XRectangle area;
	int i;

	for (i = 0; i < w->screen->nOutputDev; i++)
	{
		snapScreenGetOutputDevRect(w->screen, i, &area);
		snapAddEdge(&sw->edges, &sw->reverseEdges, 0,
					area.y, area.x, area.x + area.width - 1, BottomEdge, TRUE);
		snapAddEdge(&sw->edges, &sw->reverseEdges, 0,
					area.y + area.height, area.x,
					area.x + area.width - 1, TopEdge, TRUE);
		snapAddEdge(&sw->edges, &sw->reverseEdges, 0,
					area.x, area.y, area.y + area.height - 1, RightEdge, TRUE);
		snapAddEdge(&sw->edges, &sw->reverseEdges, 0,
					area.x + area.width, area.y,
					area.y + area.height - 1, LeftEdge, TRUE);
	}

	// Drop screen edges parts that are under struts, basically apply the
	// same strategy than for windows edges visibility
	for (c = w->screen->windows; c; c = c->next)
	{
		if (c == w || !c->struts)
			continue;
		for (e = sw->edges; e; e = next)
		{
			if (!e->screenEdge)
			{
				next = e->next;
				continue;
			}
			switch (e->type)
			{
				case LeftEdge:
				case RightEdge:
					rect.x = e->position;
					rect.y = e->start;
					rect.width = 1;
					rect.height = e->end - e->start;
					break;
				case TopEdge:
				case BottomEdge:
				default:
					rect.x = e->start;
					rect.y = e->position;
					rect.width = e->end - e->start;
					rect.height = 1;
			}
			edgeRegion = XCreateRegion();
			resultRegion = XCreateRegion();
			XUnionRectWithRegion(&rect, edgeRegion, edgeRegion);
			XSubtractRegion(edgeRegion, c->region, resultRegion);
			if (XEmptyRegion(resultRegion))
				remove = TRUE;
			else if (!XEqualRegion(edgeRegion, resultRegion))
			{
				snapAddRegionEdges(sw, e, resultRegion);
				remove = TRUE;
			}
			next = e->next;
			if (remove)
			{
				if (e->prev == NULL)
					sw->edges = e->next;
				if (e->next == NULL)
					sw->reverseEdges = e->prev;
				snapRemoveEdge(e);
				remove = FALSE;
			}
			XDestroyRegion(resultRegion);
			XDestroyRegion(edgeRegion);
		}
	}
}

/*
 * Clean edges and fill it again with appropriate edges
 */
static void snapUpdateEdges(CompWindow * w)
{
	//SNAP_SCREEN(w->screen);

	snapFreeEdges(w);

	snapUpdateWindowsEdges(w);

	if (snapGetEdgesCategoriesMask(w->screen) & EdgesCategoriesScreenEdgesMask)
		snapUpdateScreenEdges(w);
}

// Edges checking functions (move) ---------------------------------------------

/*
 * Find nearest edge in the direction set by "type",
 * w is the grabbed window, position/start/end are the window edges coordinates
 * before : if true the window has to be before the edge (top/left being origin)
 * snapDirection : just an helper, related to type
 */
static void
snapMoveCheckNearestEdge(CompWindow * w, int position, int start, int end,
						 Bool before, EdgeType type, int snapDirection)
{
	//SNAP_SCREEN(w->screen);
	SNAP_WINDOW(w);
	Edge *current = sw->edges;
	Edge *edge = current;
	int dist, min = 65535;

	while (current)
	{
		// Skip wrong type or outbound edges
		if (current->type != type
			|| current->end < start || current->start > end)
		{
			current = current->next;
			continue;
		}
		// Compute distance
		dist = before ? position - current->position
				: current->position - position;
		// Update minimum distance if needed
		if (dist < min && dist >= 0)
		{
			min = dist;
			edge = current;
		}
		// 0-dist edge, just break
		if (dist == 0)
			break;
		// Unsnap edges that aren't snapped anymore
		if (current->snapped && dist > snapGetResistanceDistance(w->screen))
			current->snapped = FALSE;
		current = current->next;
	}
	// We found a 0-dist edge, or we have a snapping candidate
	if (min == 0 || (min <= snapGetAttractionDistance(w->screen)
					 && snapGetSnapTypeMask(w->screen) & SnapTypeEdgeAttractionMask))
	{
		// Update snapping data
		if (snapGetSnapTypeMask(w->screen) & SnapTypeEdgeResistanceMask)
		{
			sw->snapped = TRUE;
			sw->snapDirection |= snapDirection;
		}
		// Attract the window if needed, moving it of the correct dist
		if (min != 0 && !edge->snapped)
		{
			edge->snapped = TRUE;
			switch (type)
			{
			case LeftEdge:
				snapMoveWindow(w, min, 0);
				break;
			case RightEdge:
				snapMoveWindow(w, -min, 0);
				break;
			case TopEdge:
				snapMoveWindow(w, 0, min);
				break;
			case BottomEdge:
				snapMoveWindow(w, 0, -min);
				break;
			default:
				break;
			}
		}
	}
}

/*
 * Call the previous function for each of the 4 sides of the window
 */
static void snapMoveCheckEdges(CompWindow * w)
{
	snapMoveCheckNearestEdge(w, WIN_X(w),
							 WIN_Y(w), WIN_Y(w) + WIN_H(w),
							 TRUE, RightEdge, HorizontalSnap);
	snapMoveCheckNearestEdge(w, WIN_X(w) + WIN_W(w),
							 WIN_Y(w), WIN_Y(w) + WIN_H(w),
							 FALSE, LeftEdge, HorizontalSnap);
	snapMoveCheckNearestEdge(w, WIN_Y(w),
							 WIN_X(w), WIN_X(w) + WIN_W(w),
							 TRUE, BottomEdge, VerticalSnap);
	snapMoveCheckNearestEdge(w, WIN_Y(w) + WIN_H(w),
							 WIN_X(w), WIN_X(w) + WIN_W(w),
							 FALSE, TopEdge, VerticalSnap);
}

// Edges checking functions (resize) -------------------------------------------

/*
 * Similar function for Snap on Resize
 */
static void
snapResizeCheckNearestEdge(CompWindow * w, int position, int start, int end,
						   Bool before, EdgeType type, int snapDirection)
{
	//SNAP_SCREEN(w->screen);
	SNAP_WINDOW(w);
	Edge *current = sw->edges;
	Edge *edge = current;
	int dist, min = 65535;

	while (current)
	{
		// Skip wrong type or outbound edges
		if (current->type != type
			|| current->end < start || current->start > end)
		{
			current = current->next;
			continue;
		}
		// Compute distance
		dist = before ? position - current->position
				: current->position - position;
		// Update minimum distance if needed
		if (dist < min && dist >= 0)
		{
			min = dist;
			edge = current;
		}
		// 0-dist edge, just break
		if (dist == 0)
			break;
		// Unsnap edges that aren't snapped anymore
		if (current->snapped && dist > snapGetResistanceDistance(w->screen))
			current->snapped = FALSE;
		current = current->next;
	}
	// We found a 0-dist edge, or we have a snapping candidate
	if (min == 0 || (min <= snapGetAttractionDistance(w->screen)
					 && snapGetSnapTypeMask(w->screen) & SnapTypeEdgeAttractionMask))
	{
		// Update snapping data
		if (snapGetSnapTypeMask(w->screen) & SnapTypeEdgeResistanceMask)
		{
			sw->snapped = TRUE;
			sw->snapDirection |= snapDirection;
		}
		// FIXME : this needs resize-specific code.
		// Attract the window if needed, moving it of the correct dist
		if (min != 0 && !edge->snapped)
		{
			edge->snapped = TRUE;
			switch (type)
			{
			case LeftEdge:
				snapResizeWindow(w, min, 0, 0, 0);
				break;
			case RightEdge:
				snapResizeWindow(w, -min, 0, 0, 0);
				break;
			case TopEdge:
				snapResizeWindow(w, 0, min, 0, 0);
				break;
			case BottomEdge:
				snapResizeWindow(w, 0, -min, 0, 0);
				break;
			default:
				break;
			}
		}
	}
}

/*
 * Call the previous function for each of the 4 sides of the window
 */
static void snapResizeCheckEdges(CompWindow * w, int dx, int dy, int dw, int dh)
{
	int x, y, width, height;
	x =  WIN_W(w);
	y =  WIN_Y(w);
	width = WIN_W(w);
	height = WIN_H(w);

	snapResizeCheckNearestEdge(w, x, y, y + height, TRUE, RightEdge,
							   HorizontalSnap);
	snapResizeCheckNearestEdge(w, x + width, y, y + height, FALSE, LeftEdge,
							   HorizontalSnap);
	snapResizeCheckNearestEdge(w, y, x, x + width, TRUE, BottomEdge,
							   VerticalSnap);
	snapResizeCheckNearestEdge(w, y + height, x, x + width, FALSE, TopEdge,
							   VerticalSnap);
}

// avoidSnap functions ---------------------------------------------------------

static Bool
snapEnableSnapping(CompDisplay * d,
				   CompAction * action,
				   CompActionState state, CompOption * option, int nOption)
{
	SNAP_DISPLAY(d);
	sd->snapping = TRUE;
	return FALSE;
}

static Bool
snapDisableSnapping(CompDisplay * d,
					CompAction * action,
					CompActionState state, CompOption * option, int nOption)
{
	SNAP_DISPLAY(d);
	sd->snapping = FALSE;
	return FALSE;
}

// Check if avoidSnap is matched, and enable/disable snap consequently
static void snapHandleEvent(CompDisplay * d, XEvent * event)
{
	SNAP_DISPLAY(d);

	if (event->type == d->xkbEvent)
	{
		XkbAnyEvent *xkbEvent = (XkbAnyEvent *) event;

		if (xkbEvent->xkb_type == XkbStateNotify)
		{
			XkbStateNotifyEvent *stateEvent = (XkbStateNotifyEvent *) event;
			unsigned int mods = 0xffffffff;

			if (sd->avoidSnapMask)
				mods = sd->avoidSnapMask;

			if ((stateEvent->mods & mods) == mods)
				snapDisableSnapping(d, NULL, 0, NULL, 0);
			else
				snapEnableSnapping(d, NULL, 0, NULL, 0);
		}
	}

	UNWRAP(sd, d, handleEvent);
	(*d->handleEvent) (d, event);
	WRAP(sd, d, handleEvent, snapHandleEvent);
}

// Events notifications --------------------------------------------------------

static void
snapWindowResizeNotify(CompWindow * w, int dx, int dy, int dw, int dh)
{
	SNAP_DISPLAY (w->screen->display);
	SNAP_SCREEN(w->screen);
	SNAP_WINDOW (w);

	UNWRAP(ss, w->screen, windowResizeNotify);
	(*w->screen->windowResizeNotify) (w, dx, dy, dw, dh);
	WRAP(ss, w->screen, windowResizeNotify, snapWindowResizeNotify);

	// avoid-infinite-notify-loop mode/not grabbed
	if (sw->skipNotify || !(sw->grabbed & ResizeGrab))
		return;

	// we have to avoid snapping but there's still some buffered moving
	if (!sd->snapping && (sw->dx || sw->dy || sw->dw || sw->dh))
	{
		snapResizeWindow(w, sw->dx, sw->dy, sw->dw, sw->dh);
		sw->dx = sw->dy = sw->dw = sw->dh = 0;
		return;
	}

	// avoiding snap, nothing buffered
	if (!sd->snapping)
		return;

	// apply edge resistance
	if (snapGetSnapTypeMask(w->screen) & SnapTypeEdgeResistanceMask)
	{
		// If there's horizontal snapping, add dx to current buffered
		// dx and resist (move by -dx) or release the window and move
		// by buffered dx - dx, same for dh
		if (sw->snapped && sw->snapDirection & HorizontalSnap)
		{
			sw->dx += dx;
			if (sw->dx < snapGetResistanceDistance(w->screen)
				&& sw->dx > -snapGetResistanceDistance(w->screen))
				snapResizeWindow(w, -dx, 0, 0, 0);
			else
			{
				snapResizeWindow(w, sw->dx - dx, 0, 0, 0);
				sw->dx = 0;
				if (!sw->dw)
					sw->snapDirection &= VerticalSnap;
			}
			sw->dw += dw;
			if (sw->dw < snapGetResistanceDistance(w->screen)
				&& sw->dw > -snapGetResistanceDistance(w->screen))
				snapResizeWindow(w, 0, 0, -dw, 0);
			else
			{
				snapResizeWindow(w, 0, 0, sw->dw - dw, 0);
				sw->dw = 0;
				if (!sw->dx)
					sw->snapDirection &= VerticalSnap;
			}
		}
		// Same for vertical snapping and dy/dh
		if (sw->snapped && sw->snapDirection & VerticalSnap)
		{
			sw->dy += dy;
			if (sw->dy < snapGetResistanceDistance(w->screen)
				&& sw->dy > -snapGetResistanceDistance(w->screen))
				snapResizeWindow(w, 0, -dy, 0, 0);
			else
			{
				snapResizeWindow(w, 0, sw->dy - dy, 0, 0);
				sw->dy = 0;
				if (!sw->dh)
					sw->snapDirection &= HorizontalSnap;
			}
			sw->dh += dh;
			if (sw->dh < snapGetResistanceDistance(w->screen)
				&& sw->dh > -snapGetResistanceDistance(w->screen))
				snapResizeWindow(w, 0, 0, 0, -dh);
			else
			{
				snapResizeWindow(w, 0, 0, 0, sw->dh - dh);
				sw->dh = 0;
				if (!sw->dy)
					sw->snapDirection &= HorizontalSnap;
			}
		}
		// If we are no longer snapping in any direction, reset snapped
		if (sw->snapped && !sw->snapDirection)
			sw->snapped = FALSE;
	}

	// If we don't already snap vertically and horizontally,
	// check edges status
	if (sw->snapDirection != (VerticalSnap | HorizontalSnap))
		snapResizeCheckEdges(w, dx, dy, dw, dh);
}

static void
snapWindowMoveNotify(CompWindow * w, int dx, int dy, Bool immediate)
{
	SNAP_DISPLAY(w->screen->display);
	SNAP_SCREEN(w->screen);
	SNAP_WINDOW(w);

	UNWRAP(ss, w->screen, windowMoveNotify);
	(*w->screen->windowMoveNotify) (w, dx, dy, immediate);
	WRAP(ss, w->screen, windowMoveNotify, snapWindowMoveNotify);

	// avoid-infinite-notify-loop mode/not grabbed
	if (sw->skipNotify || !(sw->grabbed & MoveGrab))
		return;

	// we have to avoid snapping but there's still some buffered moving
	if (!sd->snapping && (sw->dx || sw->dy))
	{
		snapMoveWindow(w, sw->dx, sw->dy);
		sw->dx = sw->dy = 0;
		return;
	}

	// avoiding snap, nothing buffered
	if (!sd->snapping)
		return;

	// apply edge resistance
	if (snapGetSnapTypeMask(w->screen) & SnapTypeEdgeResistanceMask)
	{
		// If there's horizontal snapping, add dx to current buffered
		// dx and resist (move by -dx) or release the window and move
		// by buffered dx - dx
		if (sw->snapped && sw->snapDirection & HorizontalSnap)
		{
			sw->dx += dx;
			if (sw->dx < snapGetResistanceDistance(w->screen)
				&& sw->dx > -snapGetResistanceDistance(w->screen))
				snapMoveWindow(w, -dx, 0);
			else
			{
				snapMoveWindow(w, sw->dx - dx, 0);
				sw->dx = 0;
				sw->snapDirection &= VerticalSnap;
			}
		}
		// Same for vertical snapping and dy
		if (sw->snapped && sw->snapDirection & VerticalSnap)
		{
			sw->dy += dy;
			if (sw->dy < snapGetResistanceDistance(w->screen)
				&& sw->dy > -snapGetResistanceDistance(w->screen))
				snapMoveWindow(w, 0, -dy);
			else
			{
				snapMoveWindow(w, 0, sw->dy - dy);
				sw->dy = 0;
				sw->snapDirection &= HorizontalSnap;
			}
		}
		// If we are no longer snapping in any direction, reset snapped
		if (sw->snapped && !sw->snapDirection)
			sw->snapped = FALSE;
	}
	// If we don't already snap vertically and horizontally,
	// check edges status
	if (sw->snapDirection != (VerticalSnap | HorizontalSnap))
		snapMoveCheckEdges(w);
}

/*
 * Initiate snap, get edges
 */
static void
snapWindowGrabNotify(CompWindow * w,
					 int x, int y, unsigned int state, unsigned int mask)
{
	SNAP_SCREEN(w->screen);
	SNAP_WINDOW(w);

	sw->grabbed = (mask & CompWindowGrabResizeMask) ? ResizeGrab : MoveGrab;
	snapUpdateEdges(w);

	UNWRAP(ss, w->screen, windowGrabNotify);
	(*w->screen->windowGrabNotify) (w, x, y, state, mask);
	WRAP(ss, w->screen, windowGrabNotify, snapWindowGrabNotify);
}

/*
 * Clean edges data, reset dx/dy to avoid buggy moves
 * when snap will be triggered again.
 */
static void snapWindowUngrabNotify(CompWindow * w)
{
	SNAP_SCREEN(w->screen);
	SNAP_WINDOW(w);

	snapFreeEdges(w);
	sw->snapped = FALSE;
	sw->snapDirection = 0;
	sw->grabbed = 0;
	sw->dx = sw->dy = sw->dw = sw->dh = 0;

	UNWRAP(ss, w->screen, windowUngrabNotify);
	(*w->screen->windowUngrabNotify) (w);
	WRAP(ss, w->screen, windowUngrabNotify, snapWindowUngrabNotify);
}

// Internal stuff --------------------------------------------------------------

static void snapDisplayOptionChanged(CompDisplay *d, CompOption *opt, SnapDisplayOptions num)
{
	SNAP_DISPLAY(d);

	switch (num)
	{
		case SnapDisplayOptionAvoidSnap:
		{
			unsigned int mask = snapGetAvoidSnapMask(d);
			sd->avoidSnapMask = 0;
			if (mask & AvoidSnapShiftMask)
				sd->avoidSnapMask |= ShiftMask;
			if (mask & AvoidSnapAltMask)
				sd->avoidSnapMask |= CompAltMask;
			if (mask & AvoidSnapControlMask)
				sd->avoidSnapMask |= ControlMask;
			if (mask & AvoidSnapMetaMask)
				sd->avoidSnapMask |= CompMetaMask;
		}

		default:
			break;
	}
}

static Bool snapInitDisplay(CompPlugin * p, CompDisplay * d)
{
	SnapDisplay *sd;

	sd = malloc(sizeof(SnapDisplay));
	if (!sd)
		return FALSE;

	sd->screenPrivateIndex = allocateScreenPrivateIndex(d);
	if (sd->screenPrivateIndex < 0)
	{
		free(sd);
		return FALSE;
	}

	WRAP(sd, d, handleEvent, snapHandleEvent);

	snapSetAvoidSnapNotify(d, snapDisplayOptionChanged);

	sd->avoidSnapMask = 0;
	sd->snapping = TRUE;

	d->privates[displayPrivateIndex].ptr = sd;

	return TRUE;
}

static void snapFiniDisplay(CompPlugin * p, CompDisplay * d)
{
	SNAP_DISPLAY(d);

	freeScreenPrivateIndex(d, sd->screenPrivateIndex);

	UNWRAP(sd, d, handleEvent);

	free(sd);
}

static Bool snapInitScreen(CompPlugin * p, CompScreen * s)
{
	SnapScreen *ss;

	SNAP_DISPLAY(s->display);

	ss = malloc(sizeof(SnapScreen));
	if (!ss)
		return FALSE;

	ss->windowPrivateIndex = allocateWindowPrivateIndex(s);
	if (ss->windowPrivateIndex < 0)
	{
		free(ss);
		return FALSE;
	}

	//WRAP(ss, s, windowResizeNotify, snapWindowResizeNotify);
	WRAP(ss, s, windowMoveNotify, snapWindowMoveNotify);
	WRAP(ss, s, windowGrabNotify, snapWindowGrabNotify);
	WRAP(ss, s, windowUngrabNotify, snapWindowUngrabNotify);

	s->privates[sd->screenPrivateIndex].ptr = ss;

	return TRUE;
}

static void snapFiniScreen(CompPlugin * p, CompScreen * s)
{
	SNAP_SCREEN(s);

	freeWindowPrivateIndex(s, ss->windowPrivateIndex);

	//UNWRAP(ss, s, windowResizeNotify);
	UNWRAP(ss, s, windowMoveNotify);
	UNWRAP(ss, s, windowGrabNotify);
	UNWRAP(ss, s, windowUngrabNotify);

	free(ss);
}

static Bool snapInitWindow(CompPlugin * p, CompWindow * w)
{
	SnapWindow *sw;

	SNAP_SCREEN(w->screen);

	sw = malloc(sizeof(SnapWindow));
	if (!sw)
		return FALSE;

	sw->edges = sw->reverseEdges = NULL;
	sw->snapDirection = 0;
	sw->dx = sw->dy = sw->dw = sw->dh = 0;
	sw->grabbed = 0;
	sw->snapped = FALSE;
	sw->skipNotify = FALSE;

	w->privates[ss->windowPrivateIndex].ptr = sw;

	return TRUE;
}

static void snapFiniWindow(CompPlugin * p, CompWindow * w)
{
	SNAP_WINDOW(w);

	snapFreeEdges(w);

	free(sw);
}

static Bool snapInit(CompPlugin * p)
{
	displayPrivateIndex = allocateDisplayPrivateIndex();
	if (displayPrivateIndex < 0)
		return FALSE;
	return TRUE;
}

static void snapFini(CompPlugin * p)
{
	if (displayPrivateIndex >= 0)
		freeDisplayPrivateIndex(displayPrivateIndex);
}

static int
snapGetVersion(CompPlugin * plugin, int version)
{
	return ABIVERSION;
}

CompPluginVTable snapVTable = {
	"snap",
	snapGetVersion,
	0,
	snapInit,
	snapFini,
	snapInitDisplay,
	snapFiniDisplay,
	snapInitScreen,
	snapFiniScreen,
	snapInitWindow,
	snapFiniWindow,
	0,
	0,
	0,
	0
};

CompPluginVTable *getCompPluginInfo(void)
{
	return &snapVTable;
}
