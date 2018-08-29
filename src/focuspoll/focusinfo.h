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

#ifndef FOCUS_INFO_H
#define FOCUS_INFO_H

#include <string>
#include <sstream>
#include <glib.h>

class CompPoint {
public:
    int x;
    int y;

    CompPoint(int x, int y) {
      this->x = x;
      this->y = y;
    }
};

class CompSize {
public:
    int width;
    int height;

    CompSize(int width, int height) {
      this->width = width;
      this->height = height;
    }
};

class CompRect {
public:
    int x;
    int y;
    int width;
    int height;

    CompRect(int x, int y, int width, int height) {
      this->x = x;
      this->y = y;
      this->width = width;
      this->height = height;
    }
};


class FocusInfo
{
    public:

	FocusInfo (const gchar * type = "",
		   gchar * name = g_strdup (""),
		   gchar * label = g_strdup (""),
		   gchar * role = g_strdup (""),
		   gchar * application = g_strdup (""),
		   int x = -1,
		   int y = -1,
		   int width = -1,
		   int height = -1);

	FocusInfo (const FocusInfo &dup);

	~FocusInfo ();

	int x, y, w, h;
	int xAlt, yAlt, wAlt, hAlt;
	const gchar * type;
	gchar * name;
	gchar * label;
	gchar * role;
	gchar * application;

	// AT-SPI events that are interesting to know about the event
	bool active;
	bool focused;
	bool selected;

	const gchar * getType (void);
	CompPoint getPosition (void);
	CompSize getSize (void);
	CompRect getBBox (void);

	bool operator== (const FocusInfo &other) const;
	bool operator!= (const FocusInfo &other) const;
};

#endif
