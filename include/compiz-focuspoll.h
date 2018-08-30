/*
 *
 * Compiz focus position polling plugin
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
 *
 */

#ifndef _COMPIZ_FOCUSPOLL_H
#define _COMPIZ_FOCUSPOLL_H

#define FOCUSPOLL_ABIVERSION 20080116

typedef int FocusPollingHandle;

typedef void (*FocusUpdateProc) (CompScreen *s,
				 int        x,
				 int        y,
				 int        with,
				 int        height);

typedef FocusPollingHandle
(*AddFocusPollingProc) (CompScreen         *s,
			FocusUpdateProc update);

typedef void
(*RemoveFocusPollingProc) (CompScreen            *s,
			   FocusPollingHandle id);

typedef struct _FocusPollFunc {
   AddFocusPollingProc    addFocusPolling;
   RemoveFocusPollingProc removeFocusPolling;
} FocusPollFunc;

#endif
