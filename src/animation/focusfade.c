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
 * Particle system added by : (C) 2006 Dennis Kasprzyk
 * E-mail                   : onestone@beryl-project.org
 *
 * Beam-Up added by : Florencio Guimaraes
 * E-mail           : florencio@nexcorp.com.br
 *
 * Hexagon tessellator added by : Mike Slegeir
 * E-mail                       : mikeslegeir@mail.utexas.edu>
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

// =====================  Effect: Focus Fade  =========================


void
fxFocusFadeUpdateWindowAttrib(AnimScreen * as,
			      AnimWindow * aw,
			      WindowPaintAttrib * wAttrib)
{
    float forwardProgress = 0;
    if (aw->animTotalTime - aw->timestep != 0)
	forwardProgress =
	    1 - (aw->animRemainingTime - aw->timestep) /
	    (aw->animTotalTime - aw->timestep);
    forwardProgress = MIN(forwardProgress, 1);
    forwardProgress = MAX(forwardProgress, 0);

    wAttrib->opacity = (GLushort)
	(aw->storedOpacity *
	 (1 - decelerateProgressCustom(1 - forwardProgress, 0.50, 0.75)));
}

void
fxFocusFadeUpdateWindowAttrib2(AnimScreen * as,
			       AnimWindow * aw,
			       WindowPaintAttrib * wAttrib)
{
    float forwardProgress = 0;
    if (aw->animTotalTime - aw->timestep != 0)
	forwardProgress =
	    1 - (aw->animRemainingTime - aw->timestep) /
	    (aw->animTotalTime - aw->timestep);
    forwardProgress = MIN(forwardProgress, 1);
    forwardProgress = MAX(forwardProgress, 0);

    wAttrib->opacity = (GLushort)
	(aw->storedOpacity *
	 (1 - decelerateProgressCustom(forwardProgress, 0.50, 0.75)));
}
