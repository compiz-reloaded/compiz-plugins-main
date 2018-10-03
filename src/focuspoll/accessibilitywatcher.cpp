/*
 * Copyright (C) 2016 Auboyneau Vincent <ksamak@riseup.net>
 * Copyright (C) 2018 Samuel Thibault <sthibault@hypra.fr>
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

#include <iostream>
#include <memory>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "accessibilitywatcher.h"

// When getting a bounding box bigger than this size, consider that this is not
// a precise caret bounding box, and try to find another source of precise caret position
// TODO: make them options
namespace {
    const int A11YWATCHER_MAX_CARET_WIDTH = 50;
    const int A11YWATCHER_MAX_CARET_HEIGHT = 70;
}

/*
 * Wrapper that automatically calls g_object_unref on pointer scope release.
 *
 * Instead of
 *
 * AtspiFoo x = atspi_... ()
 * atspi_... (x);
 * g_object_unref (x);
 *
 * use
 *
 * auto x = unique_gobject (atspi_... ());;
 * atspi_... (x.get ());
 *
 * */

struct unique_gobject_deleter {
    void operator () (gpointer obj)
    {
	if (obj)
	    g_object_unref (obj);
    }
};

template <typename T>
std::unique_ptr <T, unique_gobject_deleter>
unique_gobject (T* ptr)
{

    return std::unique_ptr <T, unique_gobject_deleter> (ptr);
}

/*
 * Similarly for a g_array of gobjects.
 */

struct unique_gobject_garray_deleter {
    void operator () (GArray *array)
    {
	for (guint i = 0; i < array->len; ++i)
	{
	    gpointer obj = g_array_index (array, gpointer, i);
	    if (obj)
		g_object_unref (obj);
	}
	g_array_unref (array);
    }
};

template <typename T>
std::unique_ptr <T, unique_gobject_garray_deleter>
unique_gobject_garray (T* ptr)
{

    return std::unique_ptr <T, unique_gobject_garray_deleter> (ptr);
}

/*
 * Similarly for calling g_free on pointer scope release.
 */

struct unique_gmem_deleter {
    void operator () (gpointer obj)
    {
	g_free (obj);
    }
};

template <typename T>
std::unique_ptr <T, unique_gmem_deleter>
unique_gmem (T* ptr)
{

    return std::unique_ptr <T, unique_gmem_deleter> (ptr);
}



bool AccessibilityWatcher::ignoreLinks = false;

static void
onCaretMove (const AtspiEvent *event, void *data)
{
    AccessibilityWatcher *watcher = (AccessibilityWatcher *) data;
    watcher->registerEvent (event, "caret");
}

static void
onSelectedChange (const AtspiEvent *event, void *data)
{
    AccessibilityWatcher *watcher = (AccessibilityWatcher *) data;
    watcher->registerEvent (event, "state-changed:selected");
}

static void
onFocus (const AtspiEvent *event, void *data)
{
    /* We only care about focus/selection gain
     * there's no detail1 on focus loss in AT-SPI specs */
    if (!event->detail1)
	return;

    AccessibilityWatcher *watcher = (AccessibilityWatcher *) data;
    watcher->registerEvent (event, "focus");
}

static void
onDescendantChanged (const AtspiEvent *event, void *data)
{
    AccessibilityWatcher *watcher = (AccessibilityWatcher *) data;
    watcher->registerEvent (event, "active-descendant-changed");
}

AccessibilityWatcher::AccessibilityWatcher () :
    mActive (false),
    screenWidth (0),
    screenHeight (0),
    focusListener (NULL),
    caretMoveListener (NULL),
    selectedListener (NULL),
    descendantChangedListener (NULL)
{
    atspi_init ();
    atspi_set_main_context (g_main_context_default ());

    focusListener = atspi_event_listener_new (reinterpret_cast <AtspiEventListenerCB> (onFocus), this, NULL);
    caretMoveListener = atspi_event_listener_new (reinterpret_cast <AtspiEventListenerCB> (onCaretMove), this, NULL);
    selectedListener = atspi_event_listener_new (reinterpret_cast <AtspiEventListenerCB> (onSelectedChange), this, NULL);
    descendantChangedListener = atspi_event_listener_new (reinterpret_cast <AtspiEventListenerCB> (onDescendantChanged), this, NULL);

    addWatches ();
}

AccessibilityWatcher::~AccessibilityWatcher ()
{
    removeWatches ();
    g_object_unref (focusListener);
    g_object_unref (caretMoveListener);
    g_object_unref (selectedListener);
    g_object_unref (descendantChangedListener);
};

static gchar *
getLabel (AtspiAccessible *accessible)
{
    auto relations = unique_gobject_garray (atspi_accessible_get_relation_set (accessible, NULL));
    if (relations.get () == NULL)
    {
	return g_strdup ("");
    }

    for (guint i = 0; i < relations.get ()->len; ++i) {
	AtspiRelation *relation = g_array_index (relations.get (), AtspiRelation*, i);
	if (relation == NULL)
	    continue;
	if (atspi_relation_get_relation_type (relation) == ATSPI_RELATION_LABELLED_BY)
	{
	    auto target = unique_gobject (atspi_relation_get_target (relation, 0));
	    gchar * res_label = atspi_accessible_get_name (target.get (), NULL);
	    return (res_label == NULL) ? g_strdup ("") : res_label;
	}
    }
    return g_strdup ("");
}

void
AccessibilityWatcher::setIgnoreLinks (bool val)
{
    ignoreLinks = val;
}

void
AccessibilityWatcher::setScreenLimits (int x, int y)
{
    screenWidth = x;
    screenHeight = y;
}

int
AccessibilityWatcher::getScreenWidth ()
{
    return screenWidth;
}

int
AccessibilityWatcher::getScreenHeight ()
{
    return screenHeight;
}

void
AccessibilityWatcher::registerEvent (const AtspiEvent *event, const gchar *type)
{
    // type is registered from filter on calling event
    auto application = unique_gobject (atspi_accessible_get_application (event->source, NULL));
    FocusInfo *res = new FocusInfo (type,
		   atspi_accessible_get_name (event->source, NULL),
		   getLabel (event->source),
		   atspi_accessible_get_role_name (event->source, NULL),
		   atspi_accessible_get_name (application.get (), NULL));

    if (!res->active)
    {
	// prevents skipping events that are not designated as active. we check the activeness of parents.
	auto parent = unique_gobject (atspi_accessible_get_parent (event->source, NULL));
	while (parent.get ())
	{
	    auto stateSet = unique_gobject (atspi_accessible_get_state_set (parent.get ()));
	    if (atspi_state_set_contains (stateSet.get (), ATSPI_STATE_ACTIVE))
	    {
		res->active = true;
	    }
	    if (atspi_state_set_contains (stateSet.get (), ATSPI_STATE_EXPANDABLE))
	    {
		if (!atspi_state_set_contains (stateSet.get (), ATSPI_STATE_EXPANDED))
		{
		    auto role = unique_gmem (atspi_accessible_get_role_name (parent.get (), NULL));
		    if (!strcmp (role.get (), "menu"))
		    {
			// parent is expandable but not expanded, we do not want to track what is happening inside
			delete (res);
			return;
		    }
		}
	    }
	    auto child = unique_gobject (atspi_accessible_get_parent (parent.get (), NULL));
	    if (child.get () == parent.get ())
	    {
		// parent loop !? escape this trap...
		break;
	    }
	    parent = unique_gobject (child.get ());
	}
    }

    auto component_target = unique_gobject (event->source);

    if (strcmp (res->type, "active-descendant-changed") == 0)
    {
	component_target = unique_gobject (atspi_accessible_get_child_at_index (component_target.get (), event->detail1, NULL));
	if (!component_target.get ())
	{
	    delete (res);
	    return;
	}
    }

    if (strcmp (unique_gmem (atspi_accessible_get_role_name (component_target.get (), NULL)).get (), "tree table") == 0)
    {
        // This is a table, it is not generally useful to look at the whole
        // table, but rather just at the selected file, if any, otherwise the first
        // file, if any.
	decltype (component_target) sub_target = NULL;
	auto selection = unique_gobject (atspi_accessible_get_selection_iface (component_target.get ()));

	if (selection.get ())
	{
	    sub_target = unique_gobject (atspi_selection_get_selected_child (selection.get (), 0, NULL));
	}

	if (sub_target.get () == NULL)
	{
	    // No selection, try to get first real child.
	    unsigned i = 0;

	    while (1) {
		sub_target = unique_gobject (atspi_accessible_get_child_at_index (component_target.get (), i, NULL));
		i++;

		if (sub_target.get () == NULL)
		    // No other children than table column header
		    break;

		if (strcmp (unique_gmem (atspi_accessible_get_role_name (sub_target.get (), NULL)).get (), "table column header") != 0)
		    // Found a real child
		    break;
	    }
	}

	if (sub_target.get ())
	    component_target = unique_gobject (sub_target.get ());
    }

    auto component = unique_gobject (atspi_accessible_get_component (component_target.get ()));

    if (component.get ())
    {
	auto size = unique_gmem (atspi_component_get_extents (component.get (), ATSPI_COORD_TYPE_SCREEN, NULL));
	res->x = size.get ()->x;
	res->y = size.get ()->y;
	res->w = size.get ()->width;
	res->h = size.get ()->height;
    }
    // let's get the caret offset, and then its position for a caret event
    if (strcmp (type, "caret") == 0)
    {
	auto text = unique_gobject (atspi_accessible_get_text (event->source));
	if (!text.get ())
	{
	    delete (res);
	    return;
	}
	auto offset = atspi_text_get_caret_offset (text.get (), NULL);
	// if we are not at the beginning of the text, take the extent of the character under caret
	// otherwise keep the whole widget
	if (event->detail1)
	{
	    auto size = unique_gmem (atspi_text_get_character_extents (text.get (), offset, ATSPI_COORD_TYPE_SCREEN, NULL));
	    res->x = size.get ()->x;
	    res->y = size.get ()->y;
	    res->w = size.get ()->width;
	    res->h = size.get ()->height;
	}
	// correcting a missing offset when caret is at end of text
	if (((res->x == 0 && res->y == 0) ||
	     res->x + res->w < 0 ||
	     res->y + res->h < 0)
	    && offset > 0)
	{
	    auto size = unique_gmem (atspi_text_get_character_extents (text.get (), offset-1, ATSPI_COORD_TYPE_SCREEN, NULL));
	    res->x = size.get ()->x;
	    res->y = size.get ()->y;
	    res->w = size.get ()->width;
	    res->h = size.get ()->height;
	}
	// when result is obviously not a caret size
	if (strcmp (event->type, "object:text-caret-moved") == 0 && (res->w > A11YWATCHER_MAX_CARET_WIDTH || res->h > A11YWATCHER_MAX_CARET_HEIGHT))
	{
	    auto size = unique_gmem (atspi_text_get_character_extents (text.get (), offset, ATSPI_COORD_TYPE_SCREEN, NULL));
	    res->x = size.get ()->x;
	    res->y = size.get ()->y;
	    res->w = size.get ()->width;
	    res->h = size.get ()->height;
	    if (strcmp (type, "caret") == 0 && strcmp (event->type, "object:text-caret-moved") == 0 && (res->w > A11YWATCHER_MAX_CARET_WIDTH || res->h > A11YWATCHER_MAX_CARET_HEIGHT))
	    {
		res->x = 0;
		res->y = 0;
	    }
	}

	// still no offset, it's probably a newline and we're at bugzilla #1319273 (with new paragraph obj)
	if (res->x == 0 && res->y == 0 &&
	    (strcmp (event->type, "object:text-changed:insert") == 0 ||
	     strcmp (event->type, "object:text-changed:removed") == 0 ||
	     strcmp (event->type, "object:text-caret-moved") == 0)) {
	    res->x = res->xAlt;
	    res->y = res->yAlt;
	    res->w = res->wAlt;
	    res->h = res->hAlt;
	}
    }

    // getting the states on event
    auto stateSet = unique_gobject (atspi_accessible_get_state_set (event->source));
    if (atspi_state_set_contains (stateSet.get (), ATSPI_STATE_FOCUSED))
    {
	res->focused = true;
	// reset potential menu stack
	for (auto info: previouslyActiveMenus) {
	   delete (info);
	}
	previouslyActiveMenus.clear ();
    }
    if (atspi_state_set_contains (stateSet.get (), ATSPI_STATE_SELECTED))
    {
	res->selected = true;
    }
    if (strcmp (res->type, "state-changed:selected") == 0 && event->detail1 == 1)
    {
	res->selected = true;
	if (strcmp (res->role, "paragraph") == 0)
	    // E.g. LO selects the paragraph object when making a selection
	    // inside the paragraph, which makes us jump to the beginning of
	    // the paragraph. We do not actually care about this, the selection
	    // inside the paragraph is what is interesting.
	    return;
	FocusInfo *dup = new FocusInfo (*res);
	// add to stack of menus
	previouslyActiveMenus.push_back (dup);
    }

    if (appSpecificFilter (res, event))
    {
	return;
    }
    if (filterBadEvents (res))
    {
	delete (res);
	return;
    }
    while (focusList.size () >= 5) { // don't keep the whole history
       auto iter = focusList.begin ();
       auto info = *iter;
       focusList.erase (iter);
       delete (info);
    }
    focusList.push_back (res);
}

bool
AccessibilityWatcher::appSpecificFilter (FocusInfo *focus, const AtspiEvent* event)
{
    if (strcmp (focus->type, "state-changed:selected") == 0 && // emulates on-change:selected missing event for menus
	(strcmp (focus->role, "menu item") == 0 ||
	 strcmp (focus->role, "menu") == 0 ||
	 strcmp (focus->role, "check menu item") == 0 ||
	 strcmp (focus->role, "radio menu item") == 0 ||
	 strcmp (focus->role, "tearoff menu item") == 0) &&
	strcmp (focus->application, "mate-panel") != 0)
    {
	if (!focus->selected && returnToPrevMenu ())
	{
	    // The submenu item told us that he lost selection.  We have thus
	    // returned to the parent menu (which unfortunately won't tell us
	    // anything)
	    delete (focus);
	    return true;
	}
	focus->active = true;
    }
    if (strcmp (focus->application, "soffice") == 0 && strcmp (focus->role, "paragraph") == 0)
    { // LO-calc: avoid spam event from main edit line
	auto parent = unique_gobject (atspi_accessible_get_parent (event->source, NULL));
	auto parentLabel = unique_gmem (atspi_accessible_get_name (parent.get (), NULL));
	if (!strcmp (parentLabel.get (), "Input line") ||
	    !strcmp (parentLabel.get (), "Ligne de saisie"))
	{
	    delete (focus);
	    return true;
	}
    }
    if (strcmp (focus->application, "Icedove") == 0 || strcmp (focus->application, "Thunderbird") == 0)
    {
	if (strcmp (focus->type, "caret") == 0)
	{
	    auto text = unique_gobject (atspi_accessible_get_text (event->source)); // next if deals with a special newline char, that remained buggy. hypra issue #430
	    auto offset = atspi_text_get_caret_offset (text.get (), NULL);
	    auto string = unique_gmem (atspi_text_get_string_at_offset (text.get (), offset, ATSPI_TEXT_GRANULARITY_CHAR, NULL));
	    auto stringM1 = unique_gmem (atspi_text_get_string_at_offset (text.get (), offset - 1, ATSPI_TEXT_GRANULARITY_CHAR, NULL));
	    gchar character = string.get ()->content[0];
	    gchar characterM1 = stringM1.get ()->content[0];

	    if (offset == atspi_text_get_character_count (text.get (), NULL) && character == '\0' && characterM1 == '\n')
	    {
		getAlternativeCaret (focus, event);
		focus->x = focus->xAlt;
		focus->y = focus->yAlt + focus->hAlt;
		focus->w = focus->wAlt;
		focus->h = focus->hAlt;
	    }
	    if (!(focus->x == 0 && focus->y == 0))
	    { // prevents compose window loss of tracking in HTML mode (active flag ok, but no focused flag)
		focusList.push_back (focus);
		return true;
	    }
	    auto component = unique_gobject (atspi_accessible_get_component (event->source));
	    if (component.get ())
	    {
		auto size = unique_gmem (atspi_component_get_extents (component.get (), ATSPI_COORD_TYPE_SCREEN, NULL));
		focus->x = size.get ()->x;
		focus->y = size.get ()->y;
		focus->w = 7;
		focus->h = size.get ()->height;
		focusList.push_back (focus);
		return true;
	    }
	}
    }
    if (strcmp (focus->application, "Firefox") == 0)
    {
	if (ignoreLinks && strcmp (focus->type, "caret") != 0 && strcmp (focus->role, "link") == 0)
	{
	    delete (focus);
	    return true;
	}
	// prevents status bar focus in firefox
	if (strcmp (focus->type, "caret") == 0 &&
	    (strcmp (event->type, "object:text-changed:insert:system") == 0 ||
	     strcmp (event->type, "object:text-changed:delete:system") == 0)) {
	    delete (focus);
	    return true;
	}
	if (strcmp (focus->type, "focus") == 0 && strcmp (focus->role, "document frame") == 0)
	{ // general page parasite event
	    delete (focus);
	    return true;
	}
	if (strcmp (focus->type, "caret") == 0 && !(focus->x == 0 && focus->y == 0))
	{
	    focusList.push_back (focus);
	    return true;
	}
	getAlternativeCaret (focus, event);
	if (strcmp (focus->type, "caret") == 0 && !(focus->xAlt == 0 && focus->yAlt == 0))
	{
	    focus->x = focus->xAlt;
	    focus->y = focus->yAlt + focus->hAlt;
	    focus->w = focus->wAlt;
	    focus->h = focus->hAlt;
	    focusList.push_back (focus);
	    return true;
	}
    }
    if (strcmp (focus->application, "evince") == 0 && strcmp (focus->type, "state-changed:selected") == 0 && strcmp (focus->role, "icon") == 0)
    { // LO-calc: avoid spam event from main edit line
	delete (focus);
	return true; // ignores the parasite event from evince icon
    }
    return false;
}

bool
AccessibilityWatcher::filterBadEvents (const FocusInfo *event)
{
    if (strcmp (event->type, "caret") == 0 && event->x ==0 && event->y == 0)
    {
	return true;
    }
    if (!event->active)
    {
	return true;
    }
    if (!event->focused && !event->selected)
    {
	return true;
    }
    if (event->w < 0 ||
	event->h < 0)
    {
	return true;
    }
    if (event->x == 0 &&
	event->y == 0 &&
	event->w == 0 &&
	event->h == 0)
    {
	return true;
    }
    if (event->x + event->w < 0 ||
	event->y + event->h < 0)
    {
	return true;
    }
    if (getScreenWidth () != 0 && getScreenHeight () != 0 &&
	(event->x > getScreenWidth () ||
	 event->y > getScreenHeight () ||
	 event->w > getScreenWidth () ||
	 event->h > getScreenHeight ()))
    {
	return true;
    }
    return false;
}

/*
 * This simulates a "selected" event from the parent menu when closing
 * a submenu.
 */
bool
AccessibilityWatcher::returnToPrevMenu ()
{
    if (previouslyActiveMenus.size () > 1)
    {
	previouslyActiveMenus.pop_back ();
	FocusInfo *dup = new FocusInfo (*previouslyActiveMenus.back ());
	focusList.push_back (dup);
	return true;
    }
    return false;
}

/*
 * Tries to extrapolate a missing caret position from other text characters.
 * is used as last resort when application doesn't respect at-spi standarts,
 * or at-spi bugs.
 */
void
AccessibilityWatcher::getAlternativeCaret (FocusInfo *focus, const AtspiEvent* event)
{
    auto text = unique_gobject (atspi_accessible_get_text (event->source));
    if (!text.get ())
	return;
    auto offset = atspi_text_get_caret_offset (text.get (), NULL);
    auto string = unique_gmem (atspi_text_get_string_at_offset (text.get (), offset, ATSPI_TEXT_GRANULARITY_CHAR, NULL));
    gchar caretChar = string.get ()->content[0];

    // if we're at a newline, sometimes at-spi isn't giving us a caret position. unknown bug in some apps.
    if (caretChar == '\n' || caretChar == '\0')
    {
	// gives the last empty line the right focus.
	int lines = atspi_text_get_character_count (text.get (), NULL) == offset ? 1 : 0;
	int charIndex = 1;
	bool charExtentsFound = false;

	auto size = unique_gmem (atspi_text_get_character_extents (text.get (), offset, ATSPI_COORD_TYPE_SCREEN, NULL));
	// try and find the character on upper line to extrapolate position from. no more that 300 char, we risk lag.
	while (!charExtentsFound && charIndex <= offset && charIndex < 300) {
	    size = unique_gmem (atspi_text_get_character_extents (text.get (), offset - charIndex, ATSPI_COORD_TYPE_SCREEN, NULL));
	    string = unique_gmem (atspi_text_get_string_at_offset (text.get (), offset - charIndex, ATSPI_TEXT_GRANULARITY_CHAR, NULL));
	    caretChar = string.get ()->content[0];
	    // if we found a caret, check we're at beginning of line (or of text) to extrapolate position
	    if (size.get ()->x != 0 || size.get ()->y != 0)
	    {
		if (offset - charIndex -1 >= 0 && unique_gmem (atspi_text_get_string_at_offset (text.get (), offset - charIndex -1, ATSPI_TEXT_GRANULARITY_CHAR, NULL)).get ()->content[0] == '\n')
		{
		    charExtentsFound = true; // first character of upper line has been found
		}
		else if (offset - charIndex -1 == 0)
		{
		    size = unique_gmem (atspi_text_get_character_extents (text.get (), 0, ATSPI_COORD_TYPE_SCREEN, NULL));
		    // first character of upper line has been found
		    charExtentsFound = true;
		}
	    }
	    else if (caretChar == '\n')
	    {
		++lines;
	    }
	    ++charIndex;
	}
	focus->xAlt = size.get ()->x;
	focus->yAlt = size.get ()->y + (lines-1) * size.get ()->height;
	focus->wAlt = size.get ()->width;
	focus->hAlt = size.get ()->height;
    }
}


/* Register to events */
void
AccessibilityWatcher::addWatches ()
{
    atspi_event_listener_register (focusListener, "object:state-changed:focused", NULL);
    atspi_event_listener_register (caretMoveListener, "object:text-caret-moved", NULL);
    atspi_event_listener_register (caretMoveListener, "object:text-changed:inserted", NULL);
    atspi_event_listener_register (caretMoveListener, "object:text-changed:removed", NULL);
    atspi_event_listener_register (selectedListener, "object:state-changed:selected", NULL);
    atspi_event_listener_register (descendantChangedListener, "object:active-descendant-changed", NULL);
    mActive = true;
}

void
AccessibilityWatcher::removeWatches ()
{
    atspi_event_listener_deregister (focusListener, "object:state-changed:focused", NULL);
    atspi_event_listener_deregister (caretMoveListener, "object:text-caret-moved", NULL);
    atspi_event_listener_deregister (caretMoveListener, "object:text-changed:inserted", NULL);
    atspi_event_listener_deregister (caretMoveListener, "object:text-changed:removed", NULL);
    atspi_event_listener_deregister (selectedListener, "object:state-changed:selected", NULL);
    atspi_event_listener_deregister (descendantChangedListener, "object:active-descendant-changed", NULL);
    mActive = false;
}

void
AccessibilityWatcher::setActive (bool activate)
{
    if (mActive && !activate)
    {
	removeWatches ();
    }
    else if (!mActive && activate)
    {
	addWatches ();
    }
}

std::deque <FocusInfo *>
AccessibilityWatcher::getFocusQueue ()
{
    return focusList;
}

void
AccessibilityWatcher::resetFocusQueue ()
{
    for (auto info: focusList) {
       delete (info);
    }
    focusList.clear ();
}

