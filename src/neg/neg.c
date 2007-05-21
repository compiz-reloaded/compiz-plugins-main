/*
 * Copyright (c) 2006 Darryll Truchan <moppsy@comcast.net>
 *
 * Pixel shader negating by Dennis Kasprzyk <onestone@beryl-project.org>
 * Usage of matches by Danny Baumann <maniac@beryl-project.org>
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

#include <compiz.h>
#include "neg_options.h"

static int displayPrivateIndex;

typedef struct _NEGDisplay
{
	HandleEventProc handleEvent;
	int screenPrivateIndex;
} NEGDisplay;


typedef struct _NEGSCreen
{
	int windowPrivateIndex;

	DrawWindowTextureProc drawWindowTexture;
	DamageWindowRectProc damageWindowRect;
	Bool isNeg;					/* negative screen flag */
	int negFunction;
	int negAlphaFunction;
} NEGScreen;

typedef struct _NEGWindow
{
	Bool isNeg;					/* negative window flag */
	Bool createEvent;
} NEGWindow;

#define GET_NEG_DISPLAY(d) ((NEGDisplay *) (d)->privates[displayPrivateIndex].ptr)
#define NEG_DISPLAY(d) NEGDisplay *nd = GET_NEG_DISPLAY (d)
#define GET_NEG_SCREEN(s, nd) ((NEGScreen *) (s)->privates[(nd)->screenPrivateIndex].ptr)
#define NEG_SCREEN(s) NEGScreen *ns = GET_NEG_SCREEN (s, GET_NEG_DISPLAY (s->display))
#define GET_NEG_WINDOW(w, ns) ((NEGWindow *) (w)->privates[(ns)->windowPrivateIndex].ptr)
#define NEG_WINDOW(w) NEGWindow *nw = GET_NEG_WINDOW  (w, GET_NEG_SCREEN  (w->screen, GET_NEG_DISPLAY (w->screen->display)))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static void NEGToggle(CompWindow * w)
{
	NEG_WINDOW(w);

	/* toggle window negative flag */
	nw->isNeg = !nw->isNeg;

	/* check exclude list */
	if (matchEval(negGetExcludeMatch(w->screen), w))
	    nw->isNeg = FALSE;

	/* cause repainting */
	addWindowDamage(w);
}

static void NEGToggleScreen(CompScreen * s)
{
	CompWindow *w;

	NEG_SCREEN(s);

	/* toggle screen negative flag */
	ns->isNeg = !ns->isNeg;

	/* toggle every window */
	for (w = s->windows; w; w = w->next)
		if (w)
			NEGToggle(w);
}

static Bool
negToggle(CompDisplay * d, CompAction * action, CompActionState state,
		  CompOption * option, int nOption)
{
	CompWindow *w;
	Window xid;

	xid = getIntOptionNamed(option, nOption, "window", 0);

	w = findWindowAtDisplay(d, xid);

	if (w)
		NEGToggle(w);

	return TRUE;
}

static Bool
negToggleAll(CompDisplay * d, CompAction * action, CompActionState state,
			 CompOption * option, int nOption)
{
	CompScreen *s;
	Window xid;

	xid = getIntOptionNamed(option, nOption, "root", 0);

	s = findScreenAtDisplay(d, xid);

	if (s)
		NEGToggleScreen(s);

	return TRUE;
}


static int
getNegFragmentFunction (CompScreen  *s, CompTexture *texture, Bool alpha)
{
    CompFunctionData *data;

    NEG_SCREEN (s);

	int target;

	if (texture->target == GL_TEXTURE_2D)
		target = COMP_FETCH_TARGET_2D;
    else
		target = COMP_FETCH_TARGET_RECT;

	if (alpha)
	{
		if (ns->negAlphaFunction)
			return ns->negAlphaFunction;
	}
	else
	{
		if (ns->negFunction)
			return ns->negFunction;
	}


    data = createFunctionData ();
    if (data)
    {
		Bool ok = TRUE;
		int	 handle = 0;

		if (alpha)
		{
			ok &= addTempHeaderOpToFunctionData (data, "neg" );
		}

		ok &= addFetchOpToFunctionData (data, "output", NULL, target);
 		if (alpha)
 		{
 			ok &= addDataOpToFunctionData (data, "RCP neg.a, output.a;");
 			ok &= addDataOpToFunctionData (data, "MAD output.rgb, -neg.a, output, 1.0;");
 		}
		else
			ok &= addDataOpToFunctionData (data, "SUB output.rgb, 1.0, output;");
 		if (alpha)
 		{
 			ok &= addDataOpToFunctionData (data, "MUL output.rgb, output.a, output;");
 		}
		ok &= addColorOpToFunctionData (data, "output", "output");

		if (!ok)
		{
			destroyFunctionData (data);
			return 0;
		}

		handle = createFragmentFunction (s, "neg", data);

		if (alpha)
			ns->negAlphaFunction = handle;
		else
			ns->negFunction = handle;

		destroyFunctionData (data);

		return handle;
    }

    return 0;
}

static void
NEGDrawWindowTexture(CompWindow * w,
					 CompTexture * texture,
					 const FragmentAttrib *attrib,
					 unsigned int mask)
{
	int filter;

	NEG_SCREEN(w->screen);
	NEG_WINDOW(w);

	/* only negate window contents; that's the only case
	   where w->texture->name == texture->name */
	if (nw->isNeg && (texture->name == w->texture->name))
	{
		if (w->screen->fragmentProgram)
		{
			FragmentAttrib fa = *attrib;
			int function;
			function = getNegFragmentFunction (w->screen, texture, w->alpha);
	    	if (function)
	    	{
				addFragmentFunction (&fa, function);
			}
			UNWRAP(ns, w->screen, drawWindowTexture);
			(*w->screen->drawWindowTexture) (w, texture, &fa, mask);
			WRAP(ns, w->screen, drawWindowTexture, NEGDrawWindowTexture);
		}
		else
		{
			/* this is for the most part taken from paint.c */

			if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
			{
					filter = w->screen->filter[WINDOW_TRANS_FILTER];
			}
			else if (mask & PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK)
			{
					filter = w->screen->filter[SCREEN_TRANS_FILTER];
			}
			else
			{
					filter = w->screen->filter[NOTHING_TRANS_FILTER];
			}


			/* if we can addjust saturation, even if it's just on and off */
			if (w->screen->canDoSaturated && attrib->saturation != COLOR)
			{
				GLfloat constant[4];

				/* if the paint mask has this set we want to blend */
				if (mask & PAINT_WINDOW_TRANSLUCENT_MASK)
					glEnable(GL_BLEND);

				/* enable the texture */
				enableTexture(w->screen, texture, filter);

				/* texture combiner */
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);

				glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
				glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
				glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
				glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_PRIMARY_COLOR);
				glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_ONE_MINUS_SRC_COLOR);	/* negate */
				glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
				glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);

				glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
				glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
				glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

				glColor4f(1.0f, 1.0f, 1.0f, 0.5f);

				/* make another texture active */
				w->screen->activeTexture(GL_TEXTURE1_ARB);

				/* enable that texture */
				enableTexture(w->screen, texture, filter);

				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);

				glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_DOT3_RGB);
				glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
				glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
				glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
				glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

				/* if we can do saturation that is in between min and max */
				if (w->screen->canDoSlightlySaturated && attrib->saturation > 0)
				{
					glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

					constant[0] = 0.5f + 0.5f * RED_SATURATION_WEIGHT;
					constant[1] = 0.5f + 0.5f * GREEN_SATURATION_WEIGHT;
					constant[2] = 0.5f + 0.5f * BLUE_SATURATION_WEIGHT;
					constant[3] = 1.0;

					glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);

					/* mack another texture active */
					w->screen->activeTexture(GL_TEXTURE2_ARB);

					/* enable that texture */
					enableTexture(w->screen, texture, filter);

					glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);

					glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE0);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PREVIOUS);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_CONSTANT);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_ONE_MINUS_SRC_COLOR);	/* negate */
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);

					glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

					/* color constant */
					constant[3] = attrib->saturation / 65535.0f;

					glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);

					/* if we are not opaque or not fully bright */
					if (attrib->opacity < OPAQUE || attrib->brightness != BRIGHT)
					{
						/* activate a new texture */
						w->screen->activeTexture(GL_TEXTURE3_ARB);

						/* enable that texture */
						enableTexture(w->screen, texture, filter);

						/* color constant */
						constant[3] = attrib->opacity / 65535.0f;
						constant[0] = constant[1] =
								constant[2] =
								constant[3] * attrib->brightness / 65535.0f;

						glTexEnvfv(GL_TEXTURE_ENV,
								GL_TEXTURE_ENV_COLOR, constant);

						glTexEnvf(GL_TEXTURE_ENV,
								GL_TEXTURE_ENV_MODE, GL_COMBINE);

						glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
						glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
						glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
						glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
						glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

						glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
						glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
						glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_CONSTANT);
						glTexEnvf(GL_TEXTURE_ENV,
								GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
						glTexEnvf(GL_TEXTURE_ENV,
								GL_OPERAND1_ALPHA, GL_SRC_ALPHA);

						/* draw the window geometry */
						(*w->drawWindowGeometry) (w);

						/* disable the current texture */
						disableTexture(w->screen, texture);

						/* set texture mode back to replace */
						glTexEnvi(GL_TEXTURE_ENV,
								GL_TEXTURE_ENV_MODE, GL_REPLACE);

						/* re-activate last texture */
						w->screen->activeTexture(GL_TEXTURE2_ARB);
					}
					else
					{
						/* fully opaque and bright */

						/* draw the window geometry */
						(*w->drawWindowGeometry) (w);
					}

					/* disable the current texture */
					disableTexture(w->screen, texture);

					/* set the texture mode back to replace */
					glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

					/* re-activate last texture */
					w->screen->activeTexture(GL_TEXTURE1_ARB);
				}
				else
				{
					/* fully saturated or fully unsaturated */

					glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_CONSTANT);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);

					/* color constant */
					constant[3] = attrib->opacity / 65535.0f;
					constant[0] = constant[1] = constant[2] =
							constant[3] * attrib->brightness / 65535.0f;

					constant[0] =
							0.5f + 0.5f * RED_SATURATION_WEIGHT * constant[0];
					constant[1] =
							0.5f + 0.5f * GREEN_SATURATION_WEIGHT * constant[1];
					constant[2] =
							0.5f + 0.5f * BLUE_SATURATION_WEIGHT * constant[2];

					glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);

					/* draw the window geometry */
					(*w->drawWindowGeometry) (w);
				}

				/* disable the current texture */
				disableTexture(w->screen, texture);

				/* set the texture mode back to replace */
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

				/* re-activate last texture */
				w->screen->activeTexture(GL_TEXTURE0_ARB);

				/* disable that texture */
				disableTexture(w->screen, texture);

				/* set the default color */
				glColor4usv(defaultColor);

				/* set screens texture mode back to replace */
				screenTexEnvMode(w->screen, GL_REPLACE);

				/* if it's a translucent window, disable blending */
				if (mask & PAINT_WINDOW_TRANSLUCENT_MASK)
					glDisable(GL_BLEND);
			}
			else
			{
				/* no saturation adjustments */

				/* enable the current texture */
				enableTexture(w->screen, texture, filter);

				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
				glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
				glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
				glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_ONE_MINUS_SRC_COLOR);	/* negate */


				/* we are not opaque or fully bright */
				if ((mask & PAINT_WINDOW_TRANSLUCENT_MASK)
					|| attrib->brightness != BRIGHT)
				{
					GLfloat constant[4];

					/* enable blending */
					glEnable(GL_BLEND);

					/* color constant */
					constant[3] = attrib->opacity / 65535.0f;
					constant[0] = constant[3] * attrib->brightness / 65535.0f;
					constant[1] = constant[3] * attrib->brightness / 65535.0f;
					constant[2] = constant[3] * attrib->brightness / 65535.0f;
					glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);

					glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
					glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_ONE_MINUS_SRC_COLOR);	/* negate */
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

					glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
					glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_CONSTANT);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);

					/* draw the window geometry */
					(*w->drawWindowGeometry) (w);

					/* disable blending */
					glDisable(GL_BLEND);

				}
				else
				{
					/* no adjustments to saturation, brightness or opacity */

					/* draw the window geometry */
					(*w->drawWindowGeometry) (w);
				}

				/* disable the current texture */
				disableTexture(w->screen, texture);

				/* set the screens texture mode back to replace */
				screenTexEnvMode(w->screen, GL_REPLACE);
			}
		}
	}
	else
	{
		/* not negative */
		UNWRAP(ns, w->screen, drawWindowTexture);
		(*w->screen->drawWindowTexture) (w, texture, attrib, mask);
		WRAP(ns, w->screen, drawWindowTexture, NEGDrawWindowTexture);
	}
}
static Bool NEGDamageWindowRect(CompWindow * w, Bool initial, BoxPtr rect)
{
	int status;

	NEG_SCREEN(w->screen);
	NEG_WINDOW(w);

	/* the window is initial when it is being mapped */
	if (initial)
	{
		/* if the screen is negative, negate the new window */
		if (ns->isNeg && !nw->isNeg)
			NEGToggle(w);
	}

	UNWRAP(ns, w->screen, damageWindowRect);
	status = (*w->screen->damageWindowRect) (w, initial, rect);
	WRAP(ns, w->screen, damageWindowRect, NEGDamageWindowRect);

	return status;
}

static void
NEGHandleEvent (CompDisplay *d,
		XEvent      *event)
{
	CompWindow *w;

	NEG_DISPLAY (d);
	/* Only apply at window creation.
	 * Using CreateNotify not working.
	 */
	if (event->type == MapNotify)
	{
		w = findWindowAtDisplay (d, event->xmap.window);
		if (w)
		{
			NEG_WINDOW (w);
			if (nw->createEvent)
			{
				if (w && matchEval(negGetNegMatch(w->screen), w))
					NEGToggle (w);
				nw->createEvent = FALSE;
			}	
		}
	}
	
	UNWRAP (nd, d, handleEvent);
	(*d->handleEvent) (d, event);
	WRAP (nd, d, handleEvent, NEGHandleEvent);
}

static void NEGScreenOptionChanged(CompScreen *s, CompOption *opt, NegScreenOptions num)
{
	switch (num)
	{
	    case NegScreenOptionNegMatch:
		{
			CompWindow *w;
 			for (w = s->windows; w; w = w->next)
	    		{
				NEG_WINDOW (w);
				if (matchEval(negGetNegMatch(s), w) && !nw->isNeg)
					NEGToggle (w);
				else if (! matchEval(negGetNegMatch(s), w) && nw->isNeg)
					NEGToggle (w);
	    		}
		}
		break;
		
	    default:
		break;
		
	}
}

static Bool NEGInitDisplay(CompPlugin * p, CompDisplay * d)
{
	NEGDisplay *nd;

	nd = malloc(sizeof(NEGDisplay));
	if (!nd)
		return FALSE;

	nd->screenPrivateIndex = allocateScreenPrivateIndex(d);
	if (nd->screenPrivateIndex < 0)
	{
		free(nd);
		return FALSE;
	}

	negSetWindowToggleInitiate(d, negToggle);
	negSetScreenToggleInitiate(d, negToggleAll);

	WRAP (nd, d, handleEvent, NEGHandleEvent);
	d->privates[displayPrivateIndex].ptr = nd;

	return TRUE;
}

static void NEGFiniDisplay(CompPlugin * p, CompDisplay * d)
{
	NEG_DISPLAY(d);
	freeScreenPrivateIndex(d, nd->screenPrivateIndex);
	UNWRAP (nd, d, handleEvent);
	free(nd);
}

static Bool NEGInitScreen(CompPlugin * p, CompScreen * s)
{
	NEGScreen *ns;

	NEG_DISPLAY(s->display);

	ns = malloc(sizeof(NEGScreen));
	if (!ns)
		return FALSE;

	ns->windowPrivateIndex = allocateWindowPrivateIndex(s);
	if (ns->windowPrivateIndex < 0)
	{
		free(ns);
		return FALSE;
	}

	/* initialize the screen variables
	 * you know what happens if you don't
	 */
	ns->isNeg = FALSE;
	
	negSetNegMatchNotify (s, NEGScreenOptionChanged);
	
	/* wrap overloaded functions */
	WRAP(ns, s, drawWindowTexture, NEGDrawWindowTexture);
	WRAP(ns, s, damageWindowRect, NEGDamageWindowRect);

	s->privates[nd->screenPrivateIndex].ptr = ns;

	ns->negFunction = 0;
	ns->negAlphaFunction = 0;

	return TRUE;
}


static void NEGFiniScreen(CompPlugin * p, CompScreen * s)
{
	NEG_SCREEN(s);

	freeWindowPrivateIndex(s, ns->windowPrivateIndex);
	UNWRAP(ns, s, drawWindowTexture);
	UNWRAP(ns, s, damageWindowRect);

	if (ns->negFunction)
		destroyFragmentFunction (s, ns->negFunction);
	if (ns->negAlphaFunction)
		destroyFragmentFunction (s, ns->negAlphaFunction);

	free(ns);
}

static Bool NEGInitWindow(CompPlugin * p, CompWindow * w)
{
	NEGWindow *nw;

	NEG_SCREEN(w->screen);

	nw = malloc(sizeof(NEGWindow));
	if (!nw)
		return FALSE;

	nw->isNeg = FALSE;
	nw->createEvent = TRUE;
	
	w->privates[ns->windowPrivateIndex].ptr = nw;
	
	return TRUE;
}

static void NEGFiniWindow(CompPlugin * p, CompWindow * w)
{
	NEG_WINDOW(w);
	free(nw);
}

static Bool NEGInit(CompPlugin * p)
{
	displayPrivateIndex = allocateDisplayPrivateIndex();
	if (displayPrivateIndex < 0)
		return FALSE;

	return TRUE;
}

static void NEGFini(CompPlugin * p)
{
	if (displayPrivateIndex >= 0)
		freeDisplayPrivateIndex(displayPrivateIndex);
}

static int NEGGetVersion(CompPlugin * p, int version)
{
	return ABIVERSION;
}

CompPluginVTable NEGVTable = {
	"neg",
	NEGGetVersion,
	0,
	NEGInit,
	NEGFini,
	NEGInitDisplay,
	NEGFiniDisplay,
	NEGInitScreen,
	NEGFiniScreen,
	NEGInitWindow,
	NEGFiniWindow,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	0,
	0,
	0
};

CompPluginVTable *getCompPluginInfo(void)
{
	return &NEGVTable;
}
