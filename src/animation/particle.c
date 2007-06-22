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

/*
 * TODO:
 *
 * - Auto direction option: Close in opposite direction of opening
 * - Proper side surface normals for lighting
 * - decoration shadows
 *   - shadow quad generation
 *   - shadow texture coords (from clip tex. matrices)
 *   - draw shadows
 *   - fade in shadows
 *
 * - Voronoi tessellation
 * - Brick tessellation
 * - Triangle tessellation
 * - Hexagonal tessellation
 *
 * Effects:
 * - Circular action for tornado type fx
 * - Tornado 3D (especially for minimize)
 * - Helix 3D (hor. strips descend while they rotate and fade in)
 * - Glass breaking 3D
 *   - Gaussian distr. points (for gradually increasing polygon size
 *                           starting from center or near mouse pointer)
 *   - Drawing cracks
 *   - Gradual cracking
 *
 * - fix slowness during transparent cube with <100 opacity
 * - fix occasional wrong side color in some windows
 * - fix on top windows and panels
 *   (These two only matter for viewing during Rotate Cube.
 *    All windows should be painted with depth test on
 *    like 3d-plugin does)
 * - play better with rotate (fix cube face drawn on top of polygons
 *   after 45 deg. rotation)
 *
 */

#include "animation-internal.h"

void initParticles(int numParticles, ParticleSystem * ps)
{
	if (ps->particles)
		free(ps->particles);
	ps->particles = calloc(1, sizeof(Particle) * numParticles);
	ps->tex = 0;
	ps->numParticles = numParticles;
	ps->slowdown = 1;
	ps->active = FALSE;

	// Initialize cache
	ps->vertices_cache = NULL;
	ps->colors_cache = NULL;
	ps->coords_cache = NULL;
	ps->dcolors_cache = NULL;
	ps->vertex_cache_count = 0;
	ps->color_cache_count = 0;
	ps->coords_cache_count = 0;
	ps->dcolors_cache_count = 0;

	int i;

	for (i = 0; i < numParticles; i++)
	{
		ps->particles[i].life = 0.0f;
	}
}

void drawParticles(CompScreen * s, CompWindow * w, ParticleSystem * ps)
{
	glPushMatrix();
	if (w)
		glTranslated(WIN_X(w) - ps->x, WIN_Y(w) - ps->y, 0);

	glEnable(GL_BLEND);
	if (ps->tex)
	{
		glBindTexture(GL_TEXTURE_2D, ps->tex);
		glEnable(GL_TEXTURE_2D);
	}
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	int i;
	Particle *part;

	/* Check that the cache is big enough */
	if (ps->numParticles > ps->vertex_cache_count)
	{
		ps->vertices_cache =
				realloc(ps->vertices_cache,
						ps->numParticles * 4 * 3 * sizeof(GLfloat));
		ps->vertex_cache_count = ps->numParticles;
	}

	if (ps->numParticles > ps->coords_cache_count)
	{
		ps->coords_cache =
				realloc(ps->coords_cache,
						ps->numParticles * 4 * 2 * sizeof(GLfloat));
		ps->coords_cache_count = ps->numParticles;
	}

	if (ps->numParticles > ps->color_cache_count)
	{
		ps->colors_cache =
				realloc(ps->colors_cache,
						ps->numParticles * 4 * 4 * sizeof(GLfloat));
		ps->color_cache_count = ps->numParticles;
	}

	if (ps->darken > 0)
	{
		if (ps->dcolors_cache_count < ps->numParticles)
		{
			ps->dcolors_cache =
					realloc(ps->dcolors_cache,
							ps->numParticles * 4 * 4 * sizeof(GLfloat));
			ps->dcolors_cache_count = ps->numParticles;
		}
	}

	GLfloat *dcolors = ps->dcolors_cache;
	GLfloat *vertices = ps->vertices_cache;
	GLfloat *coords = ps->coords_cache;
	GLfloat *colors = ps->colors_cache;

	int numActive = 0;

	for (i = 0; i < ps->numParticles; i++)
	{
		part = &ps->particles[i];
		if (part->life > 0.0f)
		{
			numActive += 4;

			float w = part->width / 2;
			float h = part->height / 2;

			w += (w * part->w_mod) * part->life;
			h += (h * part->h_mod) * part->life;

			vertices[0] = part->x - w;
			vertices[1] = part->y - h;
			vertices[2] = part->z;

			vertices[3] = part->x - w;
			vertices[4] = part->y + h;
			vertices[5] = part->z;

			vertices[6] = part->x + w;
			vertices[7] = part->y + h;
			vertices[8] = part->z;

			vertices[9] = part->x + w;
			vertices[10] = part->y - h;
			vertices[11] = part->z;

			vertices += 12;

			coords[0] = 0.0;
			coords[1] = 0.0;

			coords[2] = 0.0;
			coords[3] = 1.0;

			coords[4] = 1.0;
			coords[5] = 1.0;

			coords[6] = 1.0;
			coords[7] = 0.0;

			coords += 8;

			colors[0] = part->r;
			colors[1] = part->g;
			colors[2] = part->b;
			colors[3] = part->life * part->a;
			colors[4] = part->r;
			colors[5] = part->g;
			colors[6] = part->b;
			colors[7] = part->life * part->a;
			colors[8] = part->r;
			colors[9] = part->g;
			colors[10] = part->b;
			colors[11] = part->life * part->a;
			colors[12] = part->r;
			colors[13] = part->g;
			colors[14] = part->b;
			colors[15] = part->life * part->a;

			colors += 16;

			if (ps->darken > 0)
			{

				dcolors[0] = part->r;
				dcolors[1] = part->g;
				dcolors[2] = part->b;
				dcolors[3] = part->life * part->a * ps->darken;
				dcolors[4] = part->r;
				dcolors[5] = part->g;
				dcolors[6] = part->b;
				dcolors[7] = part->life * part->a * ps->darken;
				dcolors[8] = part->r;
				dcolors[9] = part->g;
				dcolors[10] = part->b;
				dcolors[11] = part->life * part->a * ps->darken;
				dcolors[12] = part->r;
				dcolors[13] = part->g;
				dcolors[14] = part->b;
				dcolors[15] = part->life * part->a * ps->darken;

				dcolors += 16;
			}
		}
	}

	glEnableClientState(GL_COLOR_ARRAY);

	glTexCoordPointer(2, GL_FLOAT, 2 * sizeof(GLfloat), ps->coords_cache);
	glVertexPointer(3, GL_FLOAT, 3 * sizeof(GLfloat), ps->vertices_cache);

	// darken the background
	if (ps->darken > 0)
	{
		glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
		glColorPointer(4, GL_FLOAT, 4 * sizeof(GLfloat), ps->dcolors_cache);
		glDrawArrays(GL_QUADS, 0, numActive);
	}
	// draw particles
	glBlendFunc(GL_SRC_ALPHA, ps->blendMode);

	glColorPointer(4, GL_FLOAT, 4 * sizeof(GLfloat), ps->colors_cache);

	glDrawArrays(GL_QUADS, 0, numActive);

	glDisableClientState(GL_COLOR_ARRAY);

	glPopMatrix();
	glColor4usv(defaultColor);
	screenTexEnvMode(s, GL_REPLACE);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
}

void updateParticles(ParticleSystem * ps, float time)
{
	int i;
	Particle *part;
	float speed = (time / 50.0);
	float slowdown = ps->slowdown * (1 - MAX(0.99, time / 1000.0)) * 1000;

	ps->active = FALSE;

	for (i = 0; i < ps->numParticles; i++)
	{
		part = &ps->particles[i];
		if (part->life > 0.0f)
		{
			// move particle
			part->x += part->xi / slowdown;
			part->y += part->yi / slowdown;
			part->z += part->zi / slowdown;

			// modify speed
			part->xi += part->xg * speed;
			part->yi += part->yg * speed;
			part->zi += part->zg * speed;

			// modify life
			part->life -= part->fade * speed;
			ps->active = TRUE;
		}
	}
}

void finiParticles(ParticleSystem * ps)
{
	free(ps->particles);
	if (ps->tex)
		glDeleteTextures(1, &ps->tex);

	if (ps->vertices_cache)
		free(ps->vertices_cache);
	if (ps->colors_cache)
		free(ps->colors_cache);
	if (ps->coords_cache)
		free(ps->coords_cache);
	if (ps->dcolors_cache)
		free(ps->dcolors_cache);
}

