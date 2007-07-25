/*
 * Animation plugin for compiz/beryl
 *
 * animation.c
 *
 * Copyright : (C) 2006 Erkin Bahceci
 * E-mail    : erkinbah@gmail.com
 *
 * Based on Wobbly and Minimize plugins by
 *           : David Reveman
 * E-mail    : davidr@novell.com>
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

#include "animation-internal.h"

// =================  Option Related Functions  =================

CompOptionValue *
animGetOptVal(AnimScreen *as,
	      AnimWindow *aw,
	      int optionId)
{
    OptionSet *os =
	&as->eventOptionSets[aw->curWindowEvent]->sets[aw->curAnimSelectionRow];
    IdValuePair *pair = os->pairs;

    int i;
    for (i = 0; i < os->nPairs; i++, pair++)
	if (pair->id == optionId)
	    return &pair->value;
    return &as->opt[optionId].value;
}

inline Bool
animGetB(AnimScreen *as,
	 AnimWindow *aw,
	 int optionId)
{
    return animGetOptVal(as, aw, optionId)->b;
}

inline int
animGetI(AnimScreen *as,
	 AnimWindow *aw,
	 int optionId)
{
    return animGetOptVal(as, aw, optionId)->i;
}

inline float
animGetF(AnimScreen *as,
	 AnimWindow *aw,
	 int optionId)
{
    return animGetOptVal(as, aw, optionId)->f;
}

inline char *
animGetS(AnimScreen *as,
	 AnimWindow *aw,
	 int optionId)
{
    return animGetOptVal(as, aw, optionId)->s;
}

inline unsigned short *
animGetC(AnimScreen *as,
	 AnimWindow *aw,
	 int optionId)
{
    return animGetOptVal(as, aw, optionId)->c;
}

static
void freeSingleEventOptionSets(OptionSets *oss)
{
    int j;
    for (j = 0; j < oss->nSets; j++)
	if (oss->sets[j].pairs)
	    free(oss->sets[j].pairs);
    free (oss->sets);
    oss->sets = NULL;
}

void
freeAllOptionSets(OptionSets **eventsOss)
{
    int i;
    for (i = 1; i < NUM_EVENTS; i++)
    {
	OptionSets *oss = eventsOss[i];
	if (!oss->sets)
	    continue;
	freeSingleEventOptionSets(oss);
    }
    free (eventsOss[WindowEventOpen]);
    free (eventsOss[WindowEventClose]);
    free (eventsOss[WindowEventMinimize]);
    free (eventsOss[WindowEventFocus]);
    free (eventsOss[WindowEventShade]);

    for (i = 1; i < NUM_EVENTS; i++)
	eventsOss[i] = NULL;
}

static void
updateOptionSet(CompScreen *s, OptionSet *os, char *optNamesValuesOrig)
{
    ANIM_SCREEN(s);
    int len = strlen(optNamesValuesOrig);
    char *optNamesValues = calloc(len + 1, 1);

    // Find the first substring with no spaces in it
    sscanf(optNamesValuesOrig, " %s ", optNamesValues);
    if (strlen(optNamesValues) == 0)
    {
	free(optNamesValues);
	return;
    }
    // Backup original, since strtok is destructive
    strcpy(optNamesValues, optNamesValuesOrig);

    char *name;
    char *nameTrimmed = calloc(len + 1, 1);
    char *valueStr;
    char *betweenPairs = ",";
    char *betweenOptVal = "=";

    // Count number of pairs
    char *pairToken = optNamesValuesOrig;
    int nPairs = 1;
	
    while ((pairToken = strchr(pairToken, betweenPairs[0])))
    {
	pairToken++; // skip delimiter
	nPairs++;
    }

    if (os->pairs)
	free(os->pairs);
    os->pairs = calloc(nPairs, sizeof(IdValuePair));
    if (!os->pairs)
    {
	os->nPairs = 0;
	compLogMessage (s->display, "animation", CompLogLevelError,
			"Not enough memory");
	return;
    }
    os->nPairs = nPairs;

    // Tokenize pairs
    name = strtok(optNamesValues, betweenOptVal);
    int errorNo = -1;
    int i;
    for (i = 0; name && i < nPairs; i++)
    {
	errorNo = 0;
	if (strchr(name, betweenPairs[0])) // handle "a, b=4" case
	{
	    errorNo = 1;
	    break;
	}

	sscanf(name, " %s ", nameTrimmed);
	if (strlen(nameTrimmed) == 0)
	{
	    errorNo = 2;
	    break;
	}
	valueStr = strtok(NULL, betweenPairs);
	if (!valueStr)
	{
	    errorNo = 3;
	    break;
	}

	CompOption *o;
	int j;

	// Skip non-effect options
	for (j = 0; j < ANIM_SCREEN_OPTION_NUM; j++)
	{
	    o = &as->opt[j];
	    if (strcasecmp(nameTrimmed, o->name) == 0)
		break;
	}
	if (j == ANIM_SCREEN_OPTION_NUM) // no match
	{
	    errorNo = 4;
	    break;
	}
	else if (j < NUM_NONEFFECT_OPTIONS)
	{
	    errorNo = 5;
	    break;
	}

	CompOptionValue v;

	os->pairs[i].id = j;
	int valueRead = -1;
	switch (o->type)
	{
	case CompOptionTypeBool:
	    valueRead = sscanf(valueStr, " %d ", &os->pairs[i].value.b);
	    break;
	case CompOptionTypeInt:
	    valueRead = sscanf(valueStr, " %d ", &v.i);
	    if (valueRead > 0)
	    {
		// Store option's original value
		int backup = o->value.i;
		if (compSetIntOption (o, &v))
		    os->pairs[i].value = v;
		else
		    errorNo = 7;
		// Restore value
		o->value.i = backup;
	    }
	    break;
	case CompOptionTypeFloat:
	    valueRead = sscanf(valueStr, " %f ", &v.f);
	    if (valueRead > 0)
	    {
		// Store option's original value
		float backup = o->value.f;
		if (compSetFloatOption (o, &v))
		    os->pairs[i].value = v;
		else
		    errorNo = 7;
		// Restore value
		o->value.f = backup;
	    }
	    break;
	case CompOptionTypeString:
	    v.s = calloc (strlen(valueStr) + 1, 1); // TODO: not freed
	    if (!v.s)
	    {
		compLogMessage (s->display, "animation", CompLogLevelError,
				"Not enough memory");
		return;
	    }
	    strcpy(v.s, valueStr);
	    valueRead = 1;
	    break;
	case CompOptionTypeColor:
	{
	    unsigned int c[4];
	    valueRead = sscanf (valueStr, " #%2x%2x%2x%2x ",
				&c[0], &c[1], &c[2], &c[3]);
	    if (valueRead == 4)
	    {
		CompOptionValue * pv = &os->pairs[i].value;
		int j;
		for (j = 0; j < 4; j++)
		    pv->c[j] = c[j] << 8 | c[j];
	    }
	    else
		errorNo = 6;
	    break;
	}
	default:
	    break;
	}
	if (valueRead == 0)
	    errorNo = 6;
	if (errorNo > 0)
	    break;
	// If valueRead is -1 here, then it must be a
	// non-(int/float/string) option, which is not supported yet.
	// Such an option doesn't currently exist anyway.

	errorNo = -1;
	name = strtok(NULL, betweenOptVal);
    }

    if (i < nPairs)
    {
	switch (errorNo)
	{
	case -1:
	case 2:
	    compLogMessage (s->display, "animation", CompLogLevelError,
			    "Option name missing in \"%s\"",
			    optNamesValuesOrig);
	    break;
	case 1:
	case 3:
	    compLogMessage (s->display, "animation", CompLogLevelError,
			    "Option value missing in \"%s\"",
			    optNamesValuesOrig);
	    break;
	case 4:
	    compLogMessage (s->display, "animation", CompLogLevelError,
			    "Unknown option \"%s\" in \"%s\"",
			    nameTrimmed, optNamesValuesOrig);
	    break;
	case 5:
	    compLogMessage (s->display, "animation", CompLogLevelError,
			    "Option \"%s\" cannot be changed like this.",
			    nameTrimmed);
	    break;
	case 6:
	    compLogMessage (s->display, "animation", CompLogLevelError,
			    "Invalid value \"%s\" in \"%s\"",
			    valueStr, optNamesValuesOrig);
	    break;
	case 7:
	    compLogMessage (s->display, "animation", CompLogLevelError,
			    "Value \"%s\" out of range in \"%s\"",
			    valueStr, optNamesValuesOrig);
	    break;
	default:
	    break;
	}
	free(os->pairs);
	os->pairs = 0;
	os->nPairs = 0;
    }
    free(optNamesValues);
}

void
updateOptionSets(CompScreen *s,
		 OptionSets *oss,
		 CompListValue *listVal)
{
    int n = listVal->nValue;

    if (oss->sets)
	freeSingleEventOptionSets(oss);

    oss->sets = calloc(n, sizeof(OptionSet));
    if (!oss->sets)
    {
	compLogMessage (s->display, "animation", CompLogLevelError,
			"Not enough memory");
	return;
    }
    oss->nSets = n;

    int i;
    for (i = 0; i < n; i++)
	updateOptionSet(s, &oss->sets[i], listVal->value[i].s);
}
