/*
 * Copyright (C) 2016 Auboyneau Vincent <ksamak@riseup.net>
 *
 *   This file is part of compiz.
 *
 *   this program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by the Free
 *   Software Foundation, either version 3 of the License, or (at your option) any
 *   later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *   details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "focusinfo.h"

#include <stdio.h>
#include <string.h>

FocusInfo::FocusInfo (const gchar * type,
		      gchar * name,
		      gchar * label,
		      gchar * role,
		      gchar * application,
		      int x,
		      int y,
		      int width,
		      int height) :
    x (x),
    y (y),
    w (width),
    h (height),
    xAlt (0),
    yAlt (0),
    wAlt (0),
    hAlt (0),
    type (type),
    name (name),
    label (label),
    role (role),
    application (application),
    active (false),
    focused (false),
    selected (false)
{
}

FocusInfo::FocusInfo (const FocusInfo &dup)
{
    x = dup.x;
    y = dup.y;
    w = dup.w;
    h = dup.h;
    xAlt = dup.xAlt;
    yAlt = dup.yAlt;
    wAlt = dup.wAlt;
    hAlt = dup.hAlt;
    type = dup.type;
    name = strdup(dup.name);
    label = strdup(dup.label);
    role = strdup(dup.role);
    application = strdup(dup.application);
    active = dup.active;
    focused = dup.focused;
    selected = dup.selected;
}

FocusInfo::~FocusInfo (void)
{
    g_free (name);
    g_free (label);
    g_free (role);
    g_free (application);
}

const gchar *
FocusInfo::FocusInfo::getType (void)
{
    return type;
}

CompPoint
FocusInfo::getPosition (void)
{
    return CompPoint (x, y);
}

CompSize
FocusInfo::getSize (void)
{
    return CompSize (w, h);
}

CompRect
FocusInfo::getBBox (void)
{
    return CompRect (x, y, w, h);
}

bool
FocusInfo::operator== (const FocusInfo& other) const
{
    return (other.x == x &&
	    other.y == y &&
	    other.w == w &&
	    other.h == h &&
	    !strcmp (other.type, type) &&
	    !strcmp (other.name, name) &&
	    !strcmp (other.label, label) &&
	    !strcmp (other.application, application) &&
	    !strcmp (other.role, role));
};

bool
FocusInfo::operator!= (const FocusInfo& other) const
{
    return !(*this == other);
};
