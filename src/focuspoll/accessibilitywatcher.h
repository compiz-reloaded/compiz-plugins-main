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

#ifndef ACCESSIBILITY_WATCHER_H
#define ACCESSIBILITY_WATCHER_H

#include <deque>
#include <vector>

#include "focusinfo.h"

#include <atspi/atspi.h>

class AccessibilityWatcher
{
    public:
	AccessibilityWatcher ();
	~AccessibilityWatcher ();

	void setActive (bool);

	void setIgnoreLinks (bool);
	void setScreenLimits (int, int);
	int getScreenWidth (void);
	int getScreenHeight (void);

	std::deque<FocusInfo *> getFocusQueue (void);
	void resetFocusQueue (void);
	bool returnToPrevMenu (void);

	void activityEvent (const AtspiEvent *event, const gchar *type);
	void readingEvent (const AtspiEvent *event, const gchar *type);

    private:
	bool mActive;
	int screenWidth;
	int screenHeight;
	static bool ignoreLinks;
	std::deque<FocusInfo*> focusList;
	std::vector<FocusInfo*> previouslyActiveMenus;

	AtspiEventListener *focusListener;
	AtspiEventListener *caretMoveListener;
	AtspiEventListener *selectedListener;
	AtspiEventListener *windowCreateListener;
	AtspiEventListener *descendantChangedListener;
	AtspiEventListener *readingListener;

	void addWatches (void);
	void removeWatches (void);

	bool appSpecificFilter (FocusInfo *focusInfo, const AtspiEvent* event);
	bool filterBadEvents (const FocusInfo *event);
	void getAlternativeCaret (FocusInfo *focus, const AtspiEvent* event);
};

#endif
