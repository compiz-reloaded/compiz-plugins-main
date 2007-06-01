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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

#ifdef USE_LIBRSVG
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <librsvg/rsvg.h>
#include <librsvg/rsvg-cairo.h>
#endif

#include <X11/Xatom.h>
#include <X11/Xproto.h>

#include <compiz.h>

#include "animation_tex.h"

#define FAKE_ICON_SIZE 4

#define ANIM_TEXTURE_LIST_INCREMENT 5
#define ANIM_CLIP_LIST_INCREMENT 20
#define NOT_INITIALIZED -10000

#define WIN_X(w) ((w)->attrib.x - (w)->output.left)
#define WIN_Y(w) ((w)->attrib.y - (w)->output.top)
#define WIN_W(w) ((w)->width + (w)->output.left + (w)->output.right)
#define WIN_H(w) ((w)->height + (w)->output.top + (w)->output.bottom)

#define BORDER_X(w) ((w)->attrib.x - (w)->input.left)
#define BORDER_Y(w) ((w)->attrib.y - (w)->input.top)
#define BORDER_W(w) ((w)->width + (w)->input.left + (w)->input.right)
#define BORDER_H(w) ((w)->height + (w)->input.top + (w)->input.bottom)

#define RAND_FLOAT() ((float)rand() / RAND_MAX)
#define MIN_WINDOW_GRID_SIZE 10

#define LIST_SIZE(l) (sizeof (l) / sizeof (l[0]))

typedef struct _xy_pair
{
	float x, y;
} Point, Vector;

typedef struct
{
	float x1, x2, y1, y2;
} Boxf;

typedef struct _xyz_tuple
{
	float x, y, z;
} Point3d, Vector3d;

// This is intended to be a closed 3D piece of a window with convex polygon
// faces and quad-strip sides. Since decoration texture is separate from
// the window texture, it is more complicated than it would be with a single
// texture: we use glClipPlane with the rectangles (clips) to clip 3D space
// to the region falling within that clip.
// If the polygon is on an edge/corner, it also has 2D shadow quad(s)
// (to be faded out at the beginning of 3D animation if necessary).
typedef struct _PolygonObject
{
	int nVertices;				// number of total vertices (front + back)
	int nSides;					// number of sides
	GLfloat *vertices;			// Positions of vertices relative to center
	GLushort *sideIndices;		// Indices of quad strip for "sides"
	GLfloat *normals;			// Surface normals for 2+nSides faces

	Box boundingBox;			// Bound. box to test intersection with clips

	GLfloat *vertexTexCoords4Clips;
	// Tex coords for each intersecting clip and for each vertex
	// ordered as c1.v1.x, c1.v1.y, c1.v2.x, c1.v2.y, c2.v1.x, c2.v1.y, ...

	/*
	   int *vertexOnEdge;           // 1,2,3,4: W,E,N,S edge  0: not edge
	   // 5,6,7,8: NW,NE,SW,SE corner
	   // (used for shadow quad generation)
	   int nShadowQuads;
	   GLfloat *shadowVertices; // Shadow vertices positions relative to center
	   GLfloat *shadowTexCoords;    // Texture coords of shadow vertices
	 */
	// Animation effect parameters

	Point3d centerPosStart;		// Starting position of center
	float rotAngleStart;		// Starting rotation angle

	Point3d centerPos;			// Position of center
	Vector3d rotAxis;			// Rotation axis vector
	float rotAngle;				// Rotation angle
	Point3d rotAxisOffset;		// Rotation axis translate amount

	Point centerRelPos;			// Relative pos of center within the window

	Vector3d finalRelPos;		// Velocity factor for scripted movement
	float finalRotAng;			// Final rotation angle around rotAxis

	float moveStartTime;		// Movement starts at this time ([0-1] range)
	float moveDuration;			// Movement lasts this long     ([0-1] range)

	float fadeStartTime;		// Fade out starts at this time ([0,1] range)
	float fadeDuration;			// Duration of fade out         ([0,1] range)
} PolygonObject;

typedef struct _Clip4Polygons	// Rectangular clips
{								// (to hold clips passed to AddWindowGeometry)
	int id;						// clip id (what number this clip is among
	// passed clips)
	Box box;					// Coords
	Boxf boxf;					// Float coords (for small clipping adjustment)
	CompMatrix texMatrix;		// Corresponding texture coord. matrix
	int *intersectingPolygons;
	int nIntersectingPolygons;	// Clips (in PolygonSet) that intersect
	GLfloat *polygonVertexTexCoords;
	// Tex coords for each intersecting polygon and for each vertex
	// ordered as p1.v1.x, p1.v1.y, p1.v2.x, p1.v2.y, p2.v1.x, p2.v1.y, ...
} Clip4Polygons;

typedef struct _PolygonSet		// Polygon objects with same thickness
{
	int nClips;					// Rect. clips collected in AddWindowGeometries
	Clip4Polygons *clips;		// List of clips
	int clipCapacity;			// # of clips this list can hold
	int firstNondrawnClip;
	int *lastClipInGroup;		// index of the last clip in each group of clips
	// drawn in drawGeometry func.

	Bool doDepthTest;           // whether depth testing should be used in the effect
	Bool doLighting;            // whether lighting should be used in the effect
	Bool correctPerspective;    // Whether perspective look should be neutralized
	PolygonObject *polygons;	// The polygons in this set
	int nPolygons;
	float thickness;			// Window thickness (depth along z axis)
	int nTotalFrontVertices;	// Total # of polygon vertices on front faces
	float backAndSidesFadeDur;	// How long side and back faces should fade in/out
	float allFadeDuration;		// Duration of fade out at the end in [0,1] range
	// when all polygons fade out at the same time.
	// If >-1, this overrides fadeDuration in PolygonObject

	Bool includeShadows;        // include shadows in polygon
} PolygonSet;

typedef struct _WaveParam
{
	float halfWidth;
	float amp;
	float pos;
} WaveParam;

typedef enum
{
	WindowEventNone = 0,
	WindowEventMinimize,
	WindowEventUnminimize,
	WindowEventClose,
	WindowEventCreate,
	WindowEventFocus,
	WindowEventShade,
	WindowEventUnshade
} WindowEvent;

typedef struct _Object
{
	Point gridPosition;		// position on window in [0,1] range
	Point position;			// position on screen
	Point3d posRel3d;			// position relative to model center
	//						   (for 3d looking effects)

	// Texture x, y coordinates will be offset by given amounts
	// for quads that fall after and before this object in x and y directions.
	// Currently only y offset can be used.
	Point offsetTexCoordForQuadBefore;
	Point offsetTexCoordForQuadAfter;
} Object;

typedef struct _Model
{
	Object *objects;
	int numObjects;
	int gridWidth;
	int gridHeight;

	int winWidth;				// keeps win. size when model was created
	int winHeight;				//

	Vector scale;
	Point scaleOrigin;
	Point topLeft;
	Point bottomRight;

	int magicLampWaveCount;
	WaveParam *magicLampWaves;
	WindowEvent forWindowEvent;
	float topHeight;
	float bottomHeight;
} Model;

typedef struct _Particle
{
	float life;					// particle life
	float fade;					// fade speed
	float width;				// particle width
	float height;				// particle height
	float w_mod;				// particle size modification during life
	float h_mod;				// particle size modification during life
	float r;					// red value
	float g;					// green value
	float b;					// blue value
	float a;					// alpha value
	float x;					// X position
	float y;					// Y position
	float z;					// Z position
	float xi;					// X direction
	float yi;					// Y direction
	float zi;					// Z direction
	float xg;					// X gravity
	float yg;					// Y gravity
	float zg;					// Z gravity
	float xo;					// orginal X position
	float yo;					// orginal Y position
	float zo;					// orginal Z position
} Particle;

typedef struct _ParticleSystem
{
	int numParticles;
	Particle *particles;
	float slowdown;
	GLuint tex;
	Bool active;
	int x, y;
	float darken;
	GLuint blendMode;

	// Moved from drawParticles to get rid of spurious malloc's
	GLfloat *vertices_cache;
	int vertex_cache_count;
	GLfloat *coords_cache;
	int coords_cache_count;
	GLfloat *colors_cache;
	int color_cache_count;
	GLfloat *dcolors_cache;
	int dcolors_cache_count;
} ParticleSystem;

static void
modelInitObjects(Model * model, int x, int y, int width, int height);

static void
postAnimationCleanup(CompWindow * w, Bool resetAnimation);

static void
animDrawWindowGeometry(CompWindow * w);

typedef enum
{
	AnimDirectionDown = 0,
	AnimDirectionUp,
	AnimDirectionLeft,
	AnimDirectionRight,
	AnimDirectionRandom,
	AnimDirectionAuto,
	AnimDirectionNum
} AnimDirection;

typedef enum
{
	ZoomFromCenterOff = 0,
	ZoomFromCenterMin,
	ZoomFromCenterCreate,
	ZoomFromCenterOn,
	ZoomFromCenterNum
} ZoomFromCenter;

// Polygon tesselation type: Rectangular, Hexagonal
typedef enum
{
	PolygonTessRect = 0,
	PolygonTessHex,
	PolygonTessNum
} PolygonTess;

typedef enum
{
	AnimEffectNone = 0,
	AnimEffectRandom,
	AnimEffectBeamUp,
	AnimEffectBurn,
	AnimEffectCurvedFold,
	AnimEffectDomino3D,
	AnimEffectDream,
	AnimEffectExplode3D,
	AnimEffectFade,
	AnimEffectFocusFade,
	AnimEffectGlide3D1,
	AnimEffectGlide3D2,
	AnimEffectHorizontalFolds,
	AnimEffectLeafSpread3D,
	AnimEffectMagicLamp,
	AnimEffectMagicLampVacuum,
	AnimEffectRazr3D,
	AnimEffectRollUp,
	AnimEffectSidekick,
	AnimEffectWave,
	AnimEffectZoom,
	AnimEffectNum
} AnimEffect;

static AnimEffect minimizeEffectType[] = {
	AnimEffectNone,
	AnimEffectRandom,
	AnimEffectBeamUp,
	AnimEffectBurn,
	AnimEffectCurvedFold,
	AnimEffectDomino3D,
	AnimEffectDream,
	AnimEffectExplode3D,
	AnimEffectFade,
	AnimEffectGlide3D1,
	AnimEffectGlide3D2,
	AnimEffectHorizontalFolds,
	AnimEffectLeafSpread3D,
	AnimEffectMagicLamp,
	AnimEffectRazr3D,
	AnimEffectSidekick,
	AnimEffectZoom
};
#define NUM_MINIMIZE_EFFECT (LIST_SIZE(minimizeEffectType))

static AnimEffect closeEffectType[] = {
	AnimEffectNone,
	AnimEffectRandom,
	AnimEffectBeamUp,
	AnimEffectBurn,
	AnimEffectCurvedFold,
	AnimEffectDomino3D,
	AnimEffectDream,
	AnimEffectExplode3D,
	AnimEffectFade,
	AnimEffectGlide3D1,
	AnimEffectGlide3D2,
	AnimEffectHorizontalFolds,
	AnimEffectLeafSpread3D,
	AnimEffectMagicLamp,
	AnimEffectMagicLampVacuum,
	AnimEffectRazr3D,
	AnimEffectSidekick,
	AnimEffectWave,
	AnimEffectZoom
};
#define NUM_CLOSE_EFFECT (LIST_SIZE(closeEffectType))

static AnimEffect focusEffectType[] = {
	AnimEffectNone,
	AnimEffectFocusFade,
	AnimEffectWave
};
#define NUM_FOCUS_EFFECT (LIST_SIZE(focusEffectType))

static AnimEffect shadeEffectType[] = {
	AnimEffectNone,
	AnimEffectRandom,
	AnimEffectCurvedFold,
	AnimEffectHorizontalFolds,
	AnimEffectRollUp
};
#define NUM_SHADE_EFFECT (LIST_SIZE(shadeEffectType))

typedef struct RestackInfo
{
	CompWindow *wRestacked, *wStart, *wEnd, *wOldAbove;
	Bool raised;
} RestackInfo;

static CompMetadata animMetadata;
static int displayPrivateIndex;

typedef struct _AnimDisplay
{
	int screenPrivateIndex;
	Atom wmHintsAtom;
	Atom winIconGeometryAtom;
	HandleEventProc handleEvent;
	HandleCompizEventProc handleCompizEvent;
	int activeWindow;
} AnimDisplay;

typedef enum
{
	// Match settings
	ANIM_SCREEN_OPTION_MINIMIZE_MATCH = 0,
	ANIM_SCREEN_OPTION_CLOSE1_MATCH,
	ANIM_SCREEN_OPTION_CLOSE2_MATCH,
	ANIM_SCREEN_OPTION_CREATE1_MATCH,
	ANIM_SCREEN_OPTION_CREATE2_MATCH,
	ANIM_SCREEN_OPTION_FOCUS_MATCH,
	ANIM_SCREEN_OPTION_SHADE_MATCH,
	// Event settings
	ANIM_SCREEN_OPTION_MINIMIZE_EFFECT,
	ANIM_SCREEN_OPTION_MINIMIZE_DURATION,
	ANIM_SCREEN_OPTION_MINIMIZE_RANDOM_EFFECTS,
	ANIM_SCREEN_OPTION_CLOSE1_EFFECT,
	ANIM_SCREEN_OPTION_CLOSE1_DURATION,
	ANIM_SCREEN_OPTION_CLOSE1_RANDOM_EFFECTS,
	ANIM_SCREEN_OPTION_CREATE1_EFFECT,
	ANIM_SCREEN_OPTION_CREATE1_DURATION,
	ANIM_SCREEN_OPTION_CREATE1_RANDOM_EFFECTS,
	ANIM_SCREEN_OPTION_CLOSE2_EFFECT,
	ANIM_SCREEN_OPTION_CLOSE2_DURATION,
	ANIM_SCREEN_OPTION_CLOSE2_RANDOM_EFFECTS,
	ANIM_SCREEN_OPTION_CREATE2_EFFECT,
	ANIM_SCREEN_OPTION_CREATE2_DURATION,
	ANIM_SCREEN_OPTION_CREATE2_RANDOM_EFFECTS,
	ANIM_SCREEN_OPTION_FOCUS_EFFECT,
	ANIM_SCREEN_OPTION_FOCUS_DURATION,
	ANIM_SCREEN_OPTION_SHADE_EFFECT,
	ANIM_SCREEN_OPTION_SHADE_DURATION,
	ANIM_SCREEN_OPTION_SHADE_RANDOM_EFFECTS,
	ANIM_SCREEN_OPTION_ROLLUP_FIXED_INTERIOR,
	// Misc. settings
	ANIM_SCREEN_OPTION_ALL_RANDOM,
	ANIM_SCREEN_OPTION_TIME_STEP,
	ANIM_SCREEN_OPTION_TIME_STEP_INTENSE,
	// Effect settings
	ANIM_SCREEN_OPTION_BEAMUP_SIZE,
	ANIM_SCREEN_OPTION_BEAMUP_SPACING,
	ANIM_SCREEN_OPTION_BEAMUP_COLOR,
	ANIM_SCREEN_OPTION_BEAMUP_SLOWDOWN,
	ANIM_SCREEN_OPTION_BEAMUP_LIFE,
	ANIM_SCREEN_OPTION_CURVED_FOLD_AMP,
	ANIM_SCREEN_OPTION_DOMINO_DIRECTION,
	ANIM_SCREEN_OPTION_RAZR_DIRECTION,
	ANIM_SCREEN_OPTION_EXPLODE3D_THICKNESS,
	ANIM_SCREEN_OPTION_EXPLODE3D_GRIDSIZE_X,
	ANIM_SCREEN_OPTION_EXPLODE3D_GRIDSIZE_Y,
	ANIM_SCREEN_OPTION_EXPLODE3D_TESS,
	ANIM_SCREEN_OPTION_FIRE_PARTICLES,
	ANIM_SCREEN_OPTION_FIRE_SIZE,
	ANIM_SCREEN_OPTION_FIRE_SLOWDOWN,
	ANIM_SCREEN_OPTION_FIRE_LIFE,
	ANIM_SCREEN_OPTION_FIRE_COLOR,
	ANIM_SCREEN_OPTION_FIRE_DIRECTION,
	ANIM_SCREEN_OPTION_FIRE_CONSTANT_SPEED,
	ANIM_SCREEN_OPTION_FIRE_SMOKE,
	ANIM_SCREEN_OPTION_FIRE_MYSTICAL,
	ANIM_SCREEN_OPTION_GLIDE1_AWAY_POS,
	ANIM_SCREEN_OPTION_GLIDE1_AWAY_ANGLE,
	ANIM_SCREEN_OPTION_GLIDE1_THICKNESS,
	ANIM_SCREEN_OPTION_GLIDE2_AWAY_POS,
	ANIM_SCREEN_OPTION_GLIDE2_AWAY_ANGLE,
	ANIM_SCREEN_OPTION_GLIDE2_THICKNESS,
	ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_AMP,
	ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_NUM_FOLDS,
	ANIM_SCREEN_OPTION_MAGIC_LAMP_GRID_RES,
	ANIM_SCREEN_OPTION_MAGIC_LAMP_MAX_WAVES,
	ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MIN,
	ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MAX,
	ANIM_SCREEN_OPTION_MAGIC_LAMP_CREATE_START_WIDTH,
	ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_GRID_RES,
	ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_MAX_WAVES,
	ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_WAVE_AMP_MIN,
	ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_WAVE_AMP_MAX,
	ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_CREATE_START_WIDTH,
	ANIM_SCREEN_OPTION_SIDEKICK_NUM_ROTATIONS,
	ANIM_SCREEN_OPTION_SIDEKICK_SPRINGINESS,
	ANIM_SCREEN_OPTION_WAVE_WIDTH,
	ANIM_SCREEN_OPTION_WAVE_AMP,
	ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER,
	ANIM_SCREEN_OPTION_ZOOM_SPRINGINESS,

	ANIM_SCREEN_OPTION_NUM
} AnimScreenOptions;

typedef struct _AnimScreen
{
	int windowPrivateIndex;

	PreparePaintScreenProc preparePaintScreen;
	DonePaintScreenProc donePaintScreen;
	PaintOutputProc paintOutput;
	PaintWindowProc paintWindow;
	DamageWindowRectProc damageWindowRect;
	AddWindowGeometryProc addWindowGeometry;
	//DrawWindowGeometryProc drawWindowGeometry;
	DrawWindowTextureProc drawWindowTexture;

	WindowResizeNotifyProc windowResizeNotify;
	WindowMoveNotifyProc windowMoveNotify;
	WindowGrabNotifyProc windowGrabNotify;
	WindowUngrabNotifyProc windowUngrabNotify;

	CompOption opt[ANIM_SCREEN_OPTION_NUM];

	Bool aWinWasRestackedJustNow; // a window was restacked this paint round

	Bool switcherActive;
	Bool groupTabChangeActive;
	//Bool scaleActive;

	Window *lastClientListStacking; // to store last known stacking order
	int nLastClientListStacking;
	int markAllWinCreatedCountdown;
	// to mark windows as "created" if they were opened before compiz
	// was started

	Bool animInProgress;
	AnimEffect minimizeEffect;
	AnimEffect create1Effect;
	AnimEffect create2Effect;
	AnimEffect close1Effect;
	AnimEffect close2Effect;
	AnimEffect focusEffect;
	AnimEffect shadeEffect;

	AnimEffect close1RandomEffects[NUM_CLOSE_EFFECT];
	AnimEffect close2RandomEffects[NUM_CLOSE_EFFECT];
	AnimEffect create1RandomEffects[NUM_CLOSE_EFFECT];
	AnimEffect create2RandomEffects[NUM_CLOSE_EFFECT];
	AnimEffect minimizeRandomEffects[NUM_MINIMIZE_EFFECT];
	AnimEffect shadeRandomEffects[NUM_SHADE_EFFECT];
	unsigned int nClose1RandomEffects;
	unsigned int nClose2RandomEffects;
	unsigned int nCreate1RandomEffects;
	unsigned int nCreate2RandomEffects;
	unsigned int nMinimizeRandomEffects;
	unsigned int nShadeRandomEffects;
} AnimScreen;

typedef struct _AnimWindow
{
	Model *model;
	int numPs;
	ParticleSystem *ps;
	unsigned int state;
	unsigned int newState;

	PolygonSet *polygonSet;
	float mvm[16];

	Region drawRegion;
	Bool useDrawRegion;

	XRectangle icon;
	XRectangle origWindowRect;

	XRectangle lastKnownCoords;	// used to determine if paintWindow is drawing
								// on the viewport that the animation started

	float numZoomRotations;
	GLushort storedOpacity;
	float timestep;				// to be used in updateWindowAttribFunc

	int nDrawGeometryCalls;

	Bool animInitialized;		// whether the animation effect (not the window) is initialized
	float animTotalTime;
	float animRemainingTime;
	float remainderSteps;
	int animOverrideProgressDir;	// 0: default dir, 1: forward, 2: backward

	Bool nowShaded;
	Bool grabbed;

	WindowEvent curWindowEvent;
	AnimEffect curAnimEffect;

	int unmapCnt;
	int destroyCnt;

	int nClipsPassed;			// # of clips passed to animAddWindowGeometry so far
	// in this draw step
	Bool clipsUpdated;          // whether stored clips are updated in this anim step
	FragmentAttrib curPaintAttrib;
	CompTexture *curTexture;
	int curTextureFilter;
	int animatedAtom;

	int animFireDirection;
	int subEffectNo;			// For effects that share same functions
	Bool deceleratingMotion;	// For effects that have decel. motion

	// for focus fade effect:
	RestackInfo *restackInfo;   // restack info if window was restacked this paint round
	CompWindow *winToBePaintedBeforeThis; // Window which should be painted before this
	CompWindow *winThisIsPaintedBefore; // the inverse relation of the above
	CompWindow *moreToBePaintedPrev; // doubly linked list for windows underneath that
	CompWindow *moreToBePaintedNext; //   raise together with this one
	Bool created;
} AnimWindow;

typedef struct _AnimEffectProperties
{
	void (*updateWindowAttribFunc) (AnimScreen *, AnimWindow *,
									WindowPaintAttrib *);
	void (*prePaintWindowFunc) (CompScreen *, CompWindow *);
	void (*postPaintWindowFunc) (CompScreen *, CompWindow *);
	void (*animStepFunc) (CompScreen *, CompWindow *, float time);
	void (*initFunc) (CompScreen *, CompWindow *);
	void (*initGridFunc) (AnimScreen *, WindowEvent, int *, int *);
	void (*addCustomGeometryFunc) (CompScreen *, CompWindow *, int, Box *,
								   int, CompMatrix *);
	void (*drawCustomGeometryFunc) (CompScreen *, CompWindow *);
	Bool dontUseQTexCoord;		// TRUE if effect doesn't need Q coord.
	void (*animStepPolygonFunc) (CompWindow *, PolygonObject *, float);
	int subEffectNo;			// For effects that share same functions
	Bool letOthersDrawGeoms;	// TRUE if we won't draw geometries
} AnimEffectProperties;

AnimEffectProperties *animEffectPropertiesTmp;

#define GET_ANIM_DISPLAY(d)                                       \
    ((AnimDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define ANIM_DISPLAY(d)                       \
    AnimDisplay *ad = GET_ANIM_DISPLAY (d)

#define GET_ANIM_SCREEN(s, ad)                                   \
    ((AnimScreen *) (s)->privates[(ad)->screenPrivateIndex].ptr)

#define ANIM_SCREEN(s)                                                      \
    AnimScreen *as = GET_ANIM_SCREEN (s, GET_ANIM_DISPLAY (s->display))

#define GET_ANIM_WINDOW(w, as)                                   \
    ((AnimWindow *) (w)->privates[(as)->windowPrivateIndex].ptr)

#define ANIM_WINDOW(w)                                         \
    AnimWindow *aw = GET_ANIM_WINDOW  (w,                         \
            GET_ANIM_SCREEN  (w->screen,                 \
                GET_ANIM_DISPLAY (w->screen->display)))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))



static AnimEffect
animEffectFromString (CompOptionValue *value, 
					  AnimEffect *allowedEffects,
					  unsigned int allowedEffectsNum)
{
	AnimEffect effect;
	int i;

	/* first check if string is valid */
	if (strcasecmp (value->s, "random") == 0)
		effect = AnimEffectRandom;
	else if (strcasecmp (value->s, "beam up") == 0)
		effect = AnimEffectBeamUp;
	else if (strcasecmp (value->s, "burn") == 0)
		effect = AnimEffectBurn;
	else if (strcasecmp (value->s, "curved fold") == 0)
		effect = AnimEffectCurvedFold;
	else if (strcasecmp (value->s, "domino") == 0)
		effect = AnimEffectDomino3D;
	else if (strcasecmp (value->s, "dream") == 0)
		effect = AnimEffectDream;
	else if (strcasecmp (value->s, "explode") == 0)
		effect = AnimEffectExplode3D;
	else if (strcasecmp (value->s, "fade") == 0)
	{
		if (allowedEffects == focusEffectType)
			effect = AnimEffectFocusFade;
		else
			effect = AnimEffectFade;
	}
	else if (strcasecmp (value->s, "glide 1") == 0)
		effect = AnimEffectGlide3D1;
	else if (strcasecmp (value->s, "glide 2") == 0)
		effect = AnimEffectGlide3D2;
	else if (strcasecmp (value->s, "horizontal folds") == 0)
		effect = AnimEffectHorizontalFolds;
	else if (strcasecmp (value->s, "leaf spread") == 0)
		effect = AnimEffectLeafSpread3D;
	else if (strcasecmp (value->s, "magic lamp") == 0)
		effect = AnimEffectMagicLamp;
	else if (strcasecmp (value->s, "magic lamp vacuum") == 0)
		effect = AnimEffectMagicLampVacuum;
	else if (strcasecmp (value->s, "razr") == 0)
		effect = AnimEffectRazr3D;
	else if (strcasecmp (value->s, "roll up") == 0)
		effect = AnimEffectRollUp;
	else if (strcasecmp (value->s, "sidekick") == 0)
		effect = AnimEffectSidekick;
	else if (strcasecmp (value->s, "wave") == 0)
		effect = AnimEffectWave;
	else if (strcasecmp (value->s, "zoom") == 0)
		effect = AnimEffectZoom;
	else
		effect = AnimEffectNone;

	/* and check if this effect is allowed for
	   the current animation */
	for (i = 0; i < allowedEffectsNum; i++)
		if (effect == allowedEffects[i])
			break;

	if (i == allowedEffectsNum)
		return AnimEffectNone;
	else
		return effect;
}

// iterate over given list
// check if given effect name matches any implemented effect
// Check if it was already in the stored list
// if not, store the effect
// if no valid effect is given, use the default effect
static void
animStoreRandomEffectList (CompOptionValue *value,
						   AnimEffect *allowedEffects,
						   unsigned int numAllowedEffects,
						   AnimEffect *targetList,
						   unsigned int *targetCount)
{
	CompOptionValue *effect = value->list.value;
	AnimEffect listEffect;
	int nItems = value->list.nValue;
	int i, j, count;

	count = 0;

	for (i = 0; i < nItems; i++, effect++)
	{
		listEffect = animEffectFromString (effect,
										   allowedEffects,
										   numAllowedEffects);

		if (listEffect == AnimEffectNone)
			continue;

		for (j = 0; j < count; j++)
		{
			if (targetList[j] == listEffect)
				break;
		}

		if (j < count)
			continue;

		targetList[count] = listEffect;
		count++;
	}

	*targetCount = count;
}

static inline AnimEffect
animGetAnimEffect (AnimEffect effect,
				   AnimEffect *randomEffects,
				   unsigned int nRandomEffects,
				   Bool allRandom)
{
	if ((effect == AnimEffectRandom) || allRandom)
	{
		if (nRandomEffects == 0)
			return AnimEffectNone;
		else
		{
			unsigned int index;
			index = (unsigned int)(nRandomEffects * (double)rand() / RAND_MAX);
			return randomEffects[index];
		}
	}
	else
		return effect;
}

#define sigmoid(fx) (1.0f/(1.0f+exp(-5.0f*2*((fx)-0.5))))
#define sigmoid2(fx, s) (1.0f/(1.0f+exp(-(s)*2*((fx)-0.5))))

// =====================  Particle engine  =========================

static void initParticles(int numParticles, ParticleSystem * ps)
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

static void drawParticles(CompScreen * s, CompWindow * w, ParticleSystem * ps)
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

static void updateParticles(ParticleSystem * ps, float time)
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

static void finiParticles(ParticleSystem * ps)
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

// =====================  END: Particle engine  =========================


static void modelCalcBounds(Model * model)
{
	int i;

	model->topLeft.x = MAXSHORT;
	model->topLeft.y = MAXSHORT;
	model->bottomRight.x = MINSHORT;
	model->bottomRight.y = MINSHORT;

	for (i = 0; i < model->numObjects; i++)
	{
		if (model->objects[i].position.x < model->topLeft.x)
			model->topLeft.x = model->objects[i].position.x;
		else if (model->objects[i].position.x > model->bottomRight.x)
			model->bottomRight.x = model->objects[i].position.x;

		if (model->objects[i].position.y < model->topLeft.y)
			model->topLeft.y = model->objects[i].position.y;
		else if (model->objects[i].position.y > model->bottomRight.y)
			model->bottomRight.y = model->objects[i].position.y;
	}
}

static Bool ensureLargerClipCapacity(PolygonSet * pset)
{
	if (pset->clipCapacity == pset->nClips)	// if list full
	{
		Clip4Polygons *newList = realloc
				(pset->clips, sizeof(Clip4Polygons) *
				 (pset->clipCapacity + ANIM_CLIP_LIST_INCREMENT));
		if (!newList)
			return FALSE;
		// reset newly allocated part of this memory to 0
		memset(newList + pset->clipCapacity,
			   0, sizeof(Clip4Polygons) * ANIM_CLIP_LIST_INCREMENT);

		int *newList2 = realloc
				(pset->lastClipInGroup, sizeof(int) *
				 (pset->clipCapacity + ANIM_CLIP_LIST_INCREMENT));
		if (!newList2)
		{
			free(newList);
			pset->clips = 0;
			pset->lastClipInGroup = 0;
			return FALSE;
		}
		// reset newly allocated part of this memory to 0
		memset(newList2 + pset->clipCapacity,
			   0, sizeof(int) * ANIM_CLIP_LIST_INCREMENT);

		pset->clips = newList;
		pset->clipCapacity += ANIM_CLIP_LIST_INCREMENT;
		pset->lastClipInGroup = newList2;
	}
	return TRUE;
}

static float defaultAnimProgress(AnimWindow * aw)
{
	float forwardProgress =
			1 - (aw->animRemainingTime - aw->timestep) /
			(aw->animTotalTime - aw->timestep);
	forwardProgress = MIN(forwardProgress, 1);
	forwardProgress = MAX(forwardProgress, 0);

	if (aw->curWindowEvent == WindowEventCreate ||
		aw->curWindowEvent == WindowEventUnminimize ||
		aw->curWindowEvent == WindowEventUnshade ||
		aw->curWindowEvent == WindowEventFocus)
		forwardProgress = 1 - forwardProgress;

	return forwardProgress;
}

// Converts animation direction string to an integer direction
// (up, down, left, or right)
static AnimDirection getAnimationDirection(CompWindow * w,
										   CompOptionValue *value, 
										   Bool openDir)
{
	ANIM_WINDOW(w);

	AnimDirection dir;

	dir = value->i;

	if (dir == AnimDirectionRandom)
	{
		dir = rand() % 4;
	}
	else if (dir == AnimDirectionAuto)
	{
		// away from icon
		int centerX = BORDER_X(w) + BORDER_W(w) / 2;
		int centerY = BORDER_Y(w) + BORDER_H(w) / 2;
		float relDiffX = ((float)centerX - aw->icon.x) / BORDER_W(w);
		float relDiffY = ((float)centerY - aw->icon.y) / BORDER_H(w);

		if (openDir)
		{
			if (aw->curWindowEvent == WindowEventMinimize ||
				aw->curWindowEvent == WindowEventUnminimize)
				// min/unmin. should always result in +/- y direction
				dir = aw->icon.y < w->screen->height - aw->icon.y ?
						AnimDirectionDown : AnimDirectionUp;
			else if (fabs(relDiffY) > fabs(relDiffX))
				dir = relDiffY > 0 ? AnimDirectionDown : AnimDirectionUp;
			else
				dir = relDiffX > 0 ? AnimDirectionRight : AnimDirectionLeft;
		}
		else
		{
			if (aw->curWindowEvent == WindowEventMinimize ||
				aw->curWindowEvent == WindowEventUnminimize)
				// min/unmin. should always result in +/- y direction
				dir = aw->icon.y < w->screen->height - aw->icon.y ?
						AnimDirectionUp : AnimDirectionDown;
			else if (fabs(relDiffY) > fabs(relDiffX))
				dir = relDiffY > 0 ? AnimDirectionUp : AnimDirectionDown;
			else
				dir = relDiffX > 0 ? AnimDirectionLeft : AnimDirectionRight;
		}
	}
	return dir;
}

// Gives some acceleration (when closing a window)
// or deceleration (when opening a window)
// Applies a sigmoid with slope s,
// where minx and maxx are the
// starting and ending points on the sigmoid
static float decelerateProgressCustom(float progress, float minx, float maxx)
{
	float x = 1 - progress;
	float s = 8;

	return (1 -
			((sigmoid2(minx + (x * (maxx - minx)), s) - sigmoid2(minx, s)) /
			 (sigmoid2(maxx, s) - sigmoid2(minx, s))));
}

static float decelerateProgress2(float progress)
{
	return decelerateProgressCustom(progress, 0.5, 0.75);
}


static void
applyTransformToObject(Object *obj, GLfloat *mat)
{
	Point3d o = obj->posRel3d;

	float x =
		o.x * mat[0] + o.y * mat[4] + o.z * mat[8] + 1 * mat[12];
	float y =
		o.x * mat[1] + o.y * mat[5] + o.z * mat[9] + 1 * mat[13];
	// ignore z
	float w =
		o.x * mat[3] + o.y * mat[7] + o.z * mat[11] + 1 * mat[15];

	obj->position.x = x / w;
	obj->position.y = y / w;
}

static void
obtainTransformMatrix (CompScreen *s, GLfloat *mat,
					   float rotAngle, Vector3d rotAxis,
					   Point3d translation)
{
	glPushMatrix();
	glLoadIdentity();

	// column-major order
	GLfloat perspMat[16] =
			{1, 0, 0, 0,
			 0, 1, 0, 0,
			 0, 0, 1, -1.0/s->width,
			 0, 0, 1, 1};

	glTranslatef(0, 0, DEFAULT_Z_CAMERA * s->width);

	glMultMatrixf(perspMat);

	glTranslatef(translation.x, translation.y, translation.z);

	glRotatef(rotAngle, rotAxis.x, rotAxis.y, rotAxis.z);

	glGetFloatv(GL_MODELVIEW_MATRIX, mat);
	glPopMatrix();
}


static void polygonsAnimStep(CompScreen * s, CompWindow * w, float time)
{
	int i, j, steps;

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->timestep = timestep;

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;
	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	for (j = 0; j < steps; j++)
	{
		float forwardProgress = defaultAnimProgress(aw);

		if (aw->polygonSet)
		{
			if (animEffectPropertiesTmp[aw->curAnimEffect].
				animStepPolygonFunc)
			{
				for (i = 0; i < aw->polygonSet->nPolygons; i++)
					animEffectPropertiesTmp[aw->curAnimEffect].
							animStepPolygonFunc
							(w, &aw->polygonSet->polygons[i],
							 forwardProgress);
			}
		}
		else
			fprintf(stderr, "%s: pset null at line %d\n", __FILE__, __LINE__);
		aw->animRemainingTime -= timestep;
		if (aw->animRemainingTime <= 0)
		{
			aw->animRemainingTime = 0;	// avoid sub-zero values
			break;
		}
	}
	modelCalcBounds(model);
}


// =====================  Effect: Magic Lamp  =========================

static void
fxMagicLampInitGrid(AnimScreen * as,
					 WindowEvent forWindowEvent,
					 int *gridWidth, int *gridHeight)
{
	*gridWidth = 2;
	*gridHeight = as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_GRID_RES].value.i;
}
static void
fxMagicLampVacuumInitGrid(AnimScreen * as, WindowEvent forWindowEvent,
					 int *gridWidth, int *gridHeight)
{
	*gridWidth = 2;
	*gridHeight = as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_GRID_RES].value.i;
}

static void fxMagicLampInit(CompScreen * s, CompWindow * w)
{
	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	int screenHeight = s->height;
	Bool minimizeToTop = (WIN_Y(w) + WIN_H(w) / 2) >
			(aw->icon.y + aw->icon.height / 2);
	int maxWaves;
	float waveAmpMin, waveAmpMax;

	if (aw->curAnimEffect == AnimEffectMagicLamp)
	{
		maxWaves = as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_MAX_WAVES].value.i;
		waveAmpMin =
				as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MIN].value.f;
		waveAmpMax =
				as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MAX].value.f;
	}
	else
	{
		maxWaves = as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_MAX_WAVES].value.i;
		waveAmpMin =
				as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_WAVE_AMP_MIN].value.f;
		waveAmpMax =
				as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_WAVE_AMP_MAX].value.f;
	}
	if (waveAmpMax < waveAmpMin)
		waveAmpMax = waveAmpMin;

	if (maxWaves > 0)
	{
		float distance;

		if (minimizeToTop)
			distance = WIN_Y(w) + WIN_H(w) - aw->icon.y;
		else
			distance = aw->icon.y - WIN_Y(w);

		// Initialize waves
		model->magicLampWaveCount =
				1 + (float)maxWaves *distance / screenHeight;

		if (!(model->magicLampWaves))
			model->magicLampWaves =
					calloc(1, model->magicLampWaveCount * sizeof(WaveParam));

		int ampDirection = (RAND_FLOAT() < 0.5 ? 1 : -1);
		int i;
		float minHalfWidth = 0.22f;
		float maxHalfWidth = 0.38f;

		for (i = 0; i < model->magicLampWaveCount; i++)
		{
			model->magicLampWaves[i].amp =
					ampDirection * (waveAmpMax - waveAmpMin) *
					rand() / RAND_MAX + ampDirection * waveAmpMin;
			model->magicLampWaves[i].halfWidth =
					RAND_FLOAT() * (maxHalfWidth -
									minHalfWidth) + minHalfWidth;

			// avoid offset at top and bottom part by added waves
			float availPos = 1 - 2 * model->magicLampWaves[i].halfWidth;
			float posInAvailSegment = 0;

			if (i > 0)
				posInAvailSegment =
						(availPos /
						 model->magicLampWaveCount) * rand() / RAND_MAX;

			model->magicLampWaves[i].pos =
					(posInAvailSegment +
					 i * availPos / model->magicLampWaveCount +
					 model->magicLampWaves[i].halfWidth);

			// switch wave direction
			ampDirection *= -1;
		}
	}
	else
		model->magicLampWaveCount = 0;
}

static void
fxMagicLampModelStepObject(CompWindow * w,
						   Model * model,
						   Object * object,
						   float forwardProgress, Bool minimizeToTop)
{
	ANIM_WINDOW(w);

	float iconCloseEndY;
	float iconFarEndY;
	float winFarEndY;
	float winVisibleCloseEndY;

	if (minimizeToTop)
	{
		iconFarEndY = aw->icon.y;
		iconCloseEndY = aw->icon.y + aw->icon.height;
		winFarEndY = WIN_Y(w) + WIN_H(w);
		winVisibleCloseEndY = WIN_Y(w);
		if (winVisibleCloseEndY < iconCloseEndY)
			winVisibleCloseEndY = iconCloseEndY;
	}
	else
	{
		iconFarEndY = aw->icon.y + aw->icon.height;
		iconCloseEndY = aw->icon.y;
		winFarEndY = WIN_Y(w);
		winVisibleCloseEndY = WIN_Y(w) + WIN_H(w);
		if (winVisibleCloseEndY > iconCloseEndY)
			winVisibleCloseEndY = iconCloseEndY;
	}

	float preShapePhaseEnd = 0.17f;
	float stretchPhaseEnd =
			preShapePhaseEnd + (1 - preShapePhaseEnd) *
			(iconCloseEndY -
			 winVisibleCloseEndY) / ((iconCloseEndY - winFarEndY) +
									 (iconCloseEndY - winVisibleCloseEndY));
	if (stretchPhaseEnd < preShapePhaseEnd + 0.1)
		stretchPhaseEnd = preShapePhaseEnd + 0.1;

	float origx = w->attrib.x + (WIN_W(w) * object->gridPosition.x -
								 w->output.left) * model->scale.x;
	float origy = w->attrib.y + (WIN_H(w) * object->gridPosition.y -
								 w->output.top) * model->scale.y;

	float iconShadowLeft =
			((float)(w->output.left - w->input.left)) * 
			aw->icon.width / w->width;
	float iconShadowRight =
			((float)(w->output.right - w->input.right)) * 
			aw->icon.width / w->width;
	float iconx =
			(aw->icon.x - iconShadowLeft) + 
			(aw->icon.width + iconShadowLeft + iconShadowRight) *
			object->gridPosition.x;
	float icony = aw->icon.y + aw->icon.height * object->gridPosition.y;

	if (forwardProgress < preShapePhaseEnd)
	{
		float preShapeProgress = forwardProgress / preShapePhaseEnd;
		float fx = (iconCloseEndY - object->position.y) / 
			       (iconCloseEndY - winFarEndY);
		float fy = (sigmoid(fx) - sigmoid(0)) / (sigmoid(1) - sigmoid(0));
		int i;
		float targetx = fy * (origx - iconx) + iconx;

		for (i = 0; i < model->magicLampWaveCount; i++)
		{
			float cosfx =
					(fx -
					 model->magicLampWaves[i].pos) /
					model->magicLampWaves[i].halfWidth;
			if (cosfx < -1 || cosfx > 1)
				continue;
			targetx +=
					model->magicLampWaves[i].amp * model->scale.x *
					(cos(cosfx * M_PI) + 1) / 2;
		}
		object->position.x =
				(1 - preShapeProgress) * origx + preShapeProgress * targetx;
		object->position.y = origy;
	}
	else
	{
		float stretchedPos;

		if (minimizeToTop)
			stretchedPos =
					object->gridPosition.y * origy +
					(1 - object->gridPosition.y) * icony;
		else
			stretchedPos =
					(1 - object->gridPosition.y) * origy +
					object->gridPosition.y * icony;

		if (forwardProgress < stretchPhaseEnd)
		{
			float stretchProgress =
					(forwardProgress - preShapePhaseEnd) /
					(stretchPhaseEnd - preShapePhaseEnd);

			object->position.y =
					(1 - stretchProgress) * origy +
					stretchProgress * stretchedPos;
		}
		else
		{
			float postStretchProgress =
					(forwardProgress - stretchPhaseEnd) / (1 -
														   stretchPhaseEnd);

			object->position.y =
					(1 - postStretchProgress) *
					stretchedPos +
					postStretchProgress *
					(stretchedPos + (iconCloseEndY - winFarEndY));
		}
		float fx =
				(iconCloseEndY - object->position.y) / (iconCloseEndY -
														winFarEndY);
		float fy = (sigmoid(fx) - sigmoid(0)) / (sigmoid(1) - sigmoid(0));

		int i;

		object->position.x = fy * (origx - iconx) + iconx;
		for (i = 0; i < model->magicLampWaveCount; i++)
		{
			float cosfx =
					(fx -
					 model->magicLampWaves[i].pos) /
					model->magicLampWaves[i].halfWidth;
			if (cosfx < -1 || cosfx > 1)
				continue;
			object->position.x +=
					model->magicLampWaves[i].amp * model->scale.x *
					(cos(cosfx * M_PI) + 1) / 2;
		}
	}
	if (minimizeToTop)
	{
		if (object->position.y < iconFarEndY)
			object->position.y = iconFarEndY;
	}
	else
	{
		if (object->position.y > iconFarEndY)
			object->position.y = iconFarEndY;
	}
}

static void fxMagicLampModelStep(CompScreen * s, CompWindow * w, float time)
{
	int i, j, steps;

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;

	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	Bool minimizeToTop = (WIN_Y(w) + WIN_H(w) / 2) >
			(aw->icon.y + aw->icon.height / 2);

	for (j = 0; j < steps; j++)
	{
		float forwardProgress =
				1 - (aw->animRemainingTime - timestep) /
				(aw->animTotalTime - timestep);
		if (aw->curWindowEvent == WindowEventUnminimize ||
			aw->curWindowEvent == WindowEventCreate)
			forwardProgress = 1 - forwardProgress;

		for (i = 0; i < model->numObjects; i++)
		{
			fxMagicLampModelStepObject(w, model,
									   &model->objects[i],
									   forwardProgress, minimizeToTop);
		}
		aw->animRemainingTime -= timestep;
		if (aw->animRemainingTime <= 0)
		{
			aw->animRemainingTime = 0;	// avoid sub-zero values
			break;
		}
	}
	modelCalcBounds(model);
}


// =====================  Effect: Dream  =========================

static void fxDreamInit(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	// store window opacity
	aw->storedOpacity = w->paint.opacity;

	aw->timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);
}

static void
fxDreamModelStepObject(CompWindow * w,
					   Model * model, Object * object, float forwardProgress)
{
	float waveAmpMax = MIN(WIN_H(w), WIN_W(w)) * 0.125f;
	float waveWidth = 10.0f;
	float waveSpeed = 7.0f;

	float origx = w->attrib.x + (WIN_W(w) * object->gridPosition.x -
								 w->output.left) * model->scale.x;
	float origy = w->attrib.y + (WIN_H(w) * object->gridPosition.y -
								 w->output.top) * model->scale.y;

	object->position.y = origy;
	object->position.x =
			origx +
			forwardProgress * waveAmpMax * model->scale.x *
			sin(object->gridPosition.y * M_PI * waveWidth +
				waveSpeed * forwardProgress);

}

static void fxDreamModelStep(CompScreen * s, CompWindow * w, float time)
{
	int i, j, steps;

	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->timestep = timestep;

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;

	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	for (j = 0; j < steps; j++)
	{
		float forwardProgress = defaultAnimProgress(aw);

		for (i = 0; i < model->numObjects; i++)
		{
			fxDreamModelStepObject(w,
								   model,
								   &model->objects[i], forwardProgress);
		}
		aw->animRemainingTime -= timestep;
		if (aw->animRemainingTime <= 0)
		{
			aw->animRemainingTime = 0;	// avoid sub-zero values
			break;
		}
	}
	modelCalcBounds(model);
}

static void
fxDreamUpdateWindowAttrib(AnimScreen * as,
						  AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	float forwardProgress = 0;
	if (aw->animTotalTime - aw->timestep != 0)
		forwardProgress =
			1 - (aw->animRemainingTime - aw->timestep) /
			(aw->animTotalTime - aw->timestep);
	forwardProgress = MIN(forwardProgress, 1);
	forwardProgress = MAX(forwardProgress, 0);

	if (aw->curWindowEvent == WindowEventCreate ||
		aw->curWindowEvent == WindowEventUnminimize)
		forwardProgress = 1 - forwardProgress;

	wAttrib->opacity = (GLushort) (aw->storedOpacity * (1 - forwardProgress));
}


// =====================  Effect: Wave  =========================

static void
fxWaveModelStepObject(CompWindow * w,
					  Model * model,
					  Object * object,
					  float forwardProgress,
					  float waveAmp, float waveHalfWidth)
{
	float origx = w->attrib.x + (WIN_W(w) * object->gridPosition.x -
								 w->output.left) * model->scale.x;
	float origy = w->attrib.y + (WIN_H(w) * object->gridPosition.y -
								 w->output.top) * model->scale.y;

	float wavePosition =
			WIN_Y(w) - waveHalfWidth +
			forwardProgress * (WIN_H(w) * model->scale.y + 2 * waveHalfWidth);

	object->position.y = origy;
	object->position.x = origx;

	if (fabs(object->position.y - wavePosition) < waveHalfWidth)
		object->position.x +=
				(object->gridPosition.x - 0.5) * waveAmp *
				(cos
				 ((object->position.y -
				   wavePosition) * M_PI / waveHalfWidth) + 1) / 2;
}

static void fxWaveModelStep(CompScreen * s, CompWindow * w, float time)
{
	int i, j, steps;

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;

	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	for (j = 0; j < steps; j++)
	{
		float forwardProgress =
				1 - (aw->animRemainingTime - timestep) /
				(aw->animTotalTime - timestep);

		for (i = 0; i < model->numObjects; i++)
		{
			fxWaveModelStepObject(w,
								  model,
								  &model->objects[i],
								  forwardProgress,
								  WIN_H(w) * model->scale.y *
								  as->opt[ANIM_SCREEN_OPTION_WAVE_AMP].value.f,
								  WIN_H(w) * model->scale.y *
								  as->opt[ANIM_SCREEN_OPTION_WAVE_WIDTH].value.f / 2);
		}
		aw->animRemainingTime -= timestep;
		if (aw->animRemainingTime <= 0)
		{
			aw->animRemainingTime = 0;	// avoid sub-zero values
			break;
		}
	}
	modelCalcBounds(model);
}


// =====================  Effect: Zoom and Sidekick  =========================

static void fxSidekickInit(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	// determine number of rotations randomly in [0.75, 1.25] range
	aw->numZoomRotations =
			as->opt[ANIM_SCREEN_OPTION_SIDEKICK_NUM_ROTATIONS].value.f *
			(1.0 + 0.2 * rand() / RAND_MAX - 0.1);

	// store window opacity
	aw->storedOpacity = w->paint.opacity;
	aw->timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);
}

static void fxZoomInit(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	// store window opacity
	aw->storedOpacity = w->paint.opacity;
	aw->timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);
}

// spring crossing x (second time it spring movement reaches target)
#define SPRING_CROSSING_X 0.6184f
#define SPRING_PERCEIVED_T 0.5f
//0.52884f

static void fxZoomAnimProgressDir(AnimScreen * as,
								  AnimWindow * aw,
								  float *moveProgress,
								  float *scaleProgress)
{
	float forwardProgress =
			1 - (aw->animRemainingTime - aw->timestep) /
			(aw->animTotalTime - aw->timestep);
	forwardProgress = MIN(forwardProgress, 1);
	forwardProgress = MAX(forwardProgress, 0);

	float x = forwardProgress;
	Bool backwards = FALSE;
	int animProgressDir = 1;

	if (aw->curWindowEvent == WindowEventUnminimize ||
		aw->curWindowEvent == WindowEventCreate)
		animProgressDir = 2;
	if (aw->animOverrideProgressDir != 0)
		animProgressDir = aw->animOverrideProgressDir;
	if ((animProgressDir == 1 &&
		 (aw->curWindowEvent == WindowEventUnminimize ||
		  aw->curWindowEvent == WindowEventCreate)) ||
		(animProgressDir == 2 &&
		 (aw->curWindowEvent == WindowEventMinimize ||
		  aw->curWindowEvent == WindowEventClose)))
		backwards = TRUE;
	if (backwards)
		x = 1 - x;
	
	float cx = SPRING_CROSSING_X;
	float nonSpringyProgress = 1 - pow(1-(x/cx*0.5),10);

	x = pow(x, 0.7);
	float damping = (pow(1-(x*0.5),10)-pow(1-0.5,10))/(1-pow(1-0.5,10));

	if (moveProgress)
	{
		float springiness = 0;
		if (aw->curAnimEffect == AnimEffectZoom)
			springiness = 2 *
				as->opt[ANIM_SCREEN_OPTION_ZOOM_SPRINGINESS].value.f;
		else
			springiness = 1.6 *
				as->opt[ANIM_SCREEN_OPTION_SIDEKICK_SPRINGINESS].value.f;

		float springyMoveProgress =
			1 - sin(3.5*M_PI*(pow(x,1.5)-1)) * damping;

		*moveProgress =
			springiness * springyMoveProgress +
			(1 - springiness) * nonSpringyProgress;

		if (aw->curWindowEvent == WindowEventUnminimize ||
			aw->curWindowEvent == WindowEventCreate)
			*moveProgress = 1 - *moveProgress;
		if (backwards)
			*moveProgress = 1 - *moveProgress;
	}
	if (scaleProgress)
	{
		*scaleProgress = nonSpringyProgress;
		if (aw->curWindowEvent == WindowEventUnminimize ||
			aw->curWindowEvent == WindowEventCreate)
			*scaleProgress = 1 - *scaleProgress;
		if (backwards)
			*scaleProgress = 1 - *scaleProgress;
	}
}

static void
fxSidekickModelStepObject(CompWindow * w,
						  Model * model,
						  Object * object,
						  Point currentCenter, Point currentSize,
						  float sinRot, float cosRot)
{
	float x =
		currentCenter.x - currentSize.x / 2 +
		currentSize.x * object->gridPosition.x;
	float y =
		currentCenter.y - currentSize.y / 2 +
		currentSize.y * object->gridPosition.y;

	x -= currentCenter.x;
	y -= currentCenter.y;

	object->position.x = cosRot * x - sinRot * y;
	object->position.y = sinRot * x + cosRot * y;

	object->position.x += currentCenter.x;
	object->position.y += currentCenter.y;
}

static void
fxZoomModelStepObject(CompScreen *s, CompWindow * w,
					  Model * model, Object * object,
					  Point currentCenter, Point currentSize)
{
	object->position.x =
		currentCenter.x - currentSize.x / 2 +
		currentSize.x * object->gridPosition.x;
	object->position.y =
		currentCenter.y - currentSize.y / 2 +
		currentSize.y * object->gridPosition.y;
}

static void fxZoomModelStep(CompScreen * s, CompWindow * w, float time)
{
	int i, j, steps;

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->timestep = timestep;

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;

	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	Point winCenter =
		{(WIN_X(w) + WIN_W(w) * model->scale.x / 2),
		 (WIN_Y(w) + WIN_H(w) * model->scale.y / 2)};
	Point iconCenter =
		{aw->icon.x + aw->icon.width / 2,
		 aw->icon.y + aw->icon.height / 2};
	Point winSize =
		{WIN_W(w) * model->scale.x,
		 WIN_H(w) * model->scale.y};

	for (j = 0; j < steps; j++)
	{
		float sinRot = 0;
		float cosRot = 0;

		float scaleProgress;
		float moveProgress;
		float rotateProgress = 0;

		if (aw->curAnimEffect == AnimEffectSidekick)
		{
			fxZoomAnimProgressDir(as, aw, &moveProgress, &scaleProgress);
			rotateProgress = moveProgress;
		}
		else
		{
			fxZoomAnimProgressDir(as, aw, &moveProgress, &scaleProgress);
		}

		Point currentCenter =
			{(1 - moveProgress) * winCenter.x + moveProgress * iconCenter.x,
			 (1 - moveProgress) * winCenter.y + moveProgress * iconCenter.y};
		Point currentSize =
			{(1 - scaleProgress) * winSize.x + scaleProgress * aw->icon.width,
			 (1 - scaleProgress) * winSize.y + scaleProgress * aw->icon.height};

		if (aw->curAnimEffect == AnimEffectSidekick)
		{
			sinRot = sin(2 * M_PI * rotateProgress * aw->numZoomRotations);
			cosRot = cos(2 * M_PI * rotateProgress * aw->numZoomRotations);

			for (i = 0; i < model->numObjects; i++)
				fxSidekickModelStepObject(w, model,
										  &model->objects[i],
										  currentCenter, currentSize,
										  sinRot, cosRot);
		}
		else
			for (i = 0; i < model->numObjects; i++)
				fxZoomModelStepObject(s, w, model,
									  &model->objects[i],
									  currentCenter, currentSize);

		aw->animRemainingTime -= timestep;
		if (aw->animRemainingTime <= 0)
		{
			aw->animRemainingTime = 0;	// avoid sub-zero values
			break;
		}
	}
	modelCalcBounds(model);
}

static void
fxZoomUpdateWindowAttrib(AnimScreen * as,
						 AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	if (aw->model->scale.x < 1.0 &&	// if Scale plugin in progress
		aw->curWindowEvent == WindowEventUnminimize)	// and if unminimizing
		return;					// then allow Fade to take over opacity
	float forwardProgress;
	fxZoomAnimProgressDir(as, aw, NULL, &forwardProgress);

	wAttrib->opacity =
			(GLushort) (aw->storedOpacity * pow(1 - forwardProgress, 0.75));
}


// =====================  Effect: Glide  =========================

static void fxGlideGetParams
	(AnimScreen *as, AnimWindow *aw,
	 float *finalDistFac, float *finalRotAng, float *thickness)
{
	// Sub effects:
	// 1: Glide 1
	// 2: Glide 2
	if (aw->subEffectNo == 1)
	{
		*finalDistFac = as->opt[ANIM_SCREEN_OPTION_GLIDE1_AWAY_POS].value.f;
		*finalRotAng = as->opt[ANIM_SCREEN_OPTION_GLIDE1_AWAY_ANGLE].value.f;
		*thickness = as->opt[ANIM_SCREEN_OPTION_GLIDE1_THICKNESS].value.f;
	}
	else
	{
		*finalDistFac = as->opt[ANIM_SCREEN_OPTION_GLIDE2_AWAY_POS].value.f;
		*finalRotAng = as->opt[ANIM_SCREEN_OPTION_GLIDE2_AWAY_ANGLE].value.f;
		*thickness = as->opt[ANIM_SCREEN_OPTION_GLIDE2_THICKNESS].value.f;
	}

}

static float fxGlideAnimProgress(AnimWindow * aw)
{
	float forwardProgress =
			1 - (aw->animRemainingTime - aw->timestep) /
			(aw->animTotalTime - aw->timestep);
	forwardProgress = MIN(forwardProgress, 1);
	forwardProgress = MAX(forwardProgress, 0);

	if (aw->curWindowEvent == WindowEventCreate ||
		aw->curWindowEvent == WindowEventUnminimize ||
		aw->curWindowEvent == WindowEventUnshade)
		forwardProgress = 1 - forwardProgress;

	return decelerateProgress2(forwardProgress);
}

static void
fxGlideModelStepObject(CompWindow * w,
					   Model * model,
					   Object * obj,
					   GLfloat *mat,
					   Point3d rotAxisOffset)
{
	float origx = w->attrib.x + (WIN_W(w) * obj->gridPosition.x -
								 w->output.left) * model->scale.x;
	float origy = w->attrib.y + (WIN_H(w) * obj->gridPosition.y -
								 w->output.top) * model->scale.y;

	obj->posRel3d.x = origx - rotAxisOffset.x;
	obj->posRel3d.y = origy - rotAxisOffset.y;

	applyTransformToObject(obj, mat);
	obj->position.x += rotAxisOffset.x;
	obj->position.y += rotAxisOffset.y;
}

static void fxGlideAnimStep(CompScreen * s, CompWindow * w, float time)
{
	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	float finalDistFac;
	float finalRotAng;
	float thickness;

	fxGlideGetParams(as, aw, &finalDistFac, &finalRotAng, &thickness);

	if (thickness > 1e-5) // the effect is 3D
	{
		polygonsAnimStep(s, w, time); // do 3D anim step instead
		return;
	}

	// for 2D glide effect
	// ------------------------

	int i, j, steps;
	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->timestep = timestep;

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;

	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	for (j = 0; j < steps; j++)
	{
		float forwardProgress = fxGlideAnimProgress(aw);

		float finalz = finalDistFac * 0.8 * DEFAULT_Z_CAMERA * s->width;

		Vector3d rotAxis = {1, 0, 0};
		Point3d rotAxisOffset =
			{WIN_X(w) + WIN_W(w) * model->scale.x / 2,
			 WIN_Y(w) + WIN_H(w) * model->scale.y / 2,
			 0};
		Point3d modelPos = {0, 0, finalz * forwardProgress};

		GLfloat mat[16];
		obtainTransformMatrix(s, mat, finalRotAng * forwardProgress,
							  rotAxis, modelPos);
		for (i = 0; i < model->numObjects; i++)
			fxGlideModelStepObject(w, model,
								   &model->objects[i],
								   mat,
								   rotAxisOffset);

		aw->animRemainingTime -= timestep;
		if (aw->animRemainingTime <= 0)
		{
			aw->animRemainingTime = 0;	// avoid sub-zero values
			break;
		}
	}
	modelCalcBounds(model);
}

static void
fxGlideUpdateWindowAttrib(AnimScreen * as,
						 AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	float finalDistFac;
	float finalRotAng;
	float thickness;

	fxGlideGetParams(as, aw, &finalDistFac, &finalRotAng, &thickness);

	if (thickness > 1e-5) // the effect is 3D
		return;

	// the effect is 2D

	if (aw->model->scale.x < 1.0 &&	// if Scale plugin in progress
		aw->curWindowEvent == WindowEventUnminimize)	// and if unminimizing
		return;					// then allow Fade to take over opacity
	float forwardProgress = fxGlideAnimProgress(aw);

	wAttrib->opacity = aw->storedOpacity * (1 - forwardProgress);
}


// =====================  Effect: Curved Fold  =========================

static void
fxCurvedFoldModelStepObject(CompWindow * w,
							Model * model,
							Object * object,
							float forwardProgress, float curveMaxAmp)
{
	ANIM_WINDOW(w);

	float origx = w->attrib.x + (WIN_W(w) * object->gridPosition.x -
								 w->output.left) * model->scale.x;
	float origy = w->attrib.y + (WIN_H(w) * object->gridPosition.y -
								 w->output.top) * model->scale.y;

	if (aw->curWindowEvent == WindowEventShade ||
		aw->curWindowEvent == WindowEventUnshade)
	{
		// Execute shade mode

		// find position in window contents
		// (window contents correspond to 0.0-1.0 range)
		float relPosInWinContents =
				(object->gridPosition.y * WIN_H(w) -
				 model->topHeight) / w->height;
		float relDistToCenter = fabs(relPosInWinContents - 0.5);

		if (object->gridPosition.y == 0)
		{
			object->position.x = origx;
			object->position.y = WIN_Y(w);
		}
		else if (object->gridPosition.y == 1)
		{
			object->position.x = origx;
			object->position.y = 
				(1 - forwardProgress) * origy +
				forwardProgress *
				(WIN_Y(w) + model->topHeight + model->bottomHeight);
		}
		else
		{
			object->position.x =
					origx + sin(forwardProgress * M_PI / 2) *
					(0.5 - object->gridPosition.x) * 2 * model->scale.x *
					(curveMaxAmp -
					 curveMaxAmp * 4 * relDistToCenter * relDistToCenter);
			object->position.y =
					(1 - forwardProgress) * origy +
					forwardProgress * (WIN_Y(w) + model->topHeight);
		}
	}
	else
	{							// Execute normal mode

		// find position within window borders
		// (border contents correspond to 0.0-1.0 range)
		float relPosInWinBorders =
				(object->gridPosition.y * WIN_H(w) -
				 (w->output.top - w->input.top)) / BORDER_H(w);
		float relDistToCenter = fabs(relPosInWinBorders - 0.5);

		// prevent top & bottom shadows from extending too much
		if (relDistToCenter > 0.5)
			relDistToCenter = 0.5;

		object->position.x =
				origx + sin(forwardProgress * M_PI / 2) *
				(0.5 - object->gridPosition.x) * 2 * model->scale.x *
				(curveMaxAmp -
				 curveMaxAmp * 4 * relDistToCenter * relDistToCenter);
		object->position.y =
				(1 - forwardProgress) * origy + forwardProgress * BORDER_Y(w);
	}
}

static void fxCurvedFoldModelStep(CompScreen * s, CompWindow * w, float time)
{
	int i, j, steps;

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;

	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	for (j = 0; j < steps; j++)
	{
		float forwardProgress =
				1 - (aw->animRemainingTime - timestep) /
				(aw->animTotalTime - timestep);
		if (aw->curWindowEvent == WindowEventCreate ||
			aw->curWindowEvent == WindowEventUnminimize ||
			aw->curWindowEvent == WindowEventUnshade)
			forwardProgress = 1 - forwardProgress;

		for (i = 0; i < model->numObjects; i++)
			fxCurvedFoldModelStepObject(w, 
				model,
				&model->objects[i],
				forwardProgress,
				as->opt[ANIM_SCREEN_OPTION_CURVED_FOLD_AMP].value.f * WIN_W(w));

		aw->animRemainingTime -= timestep;
		if (aw->animRemainingTime <= 0)
		{
			aw->animRemainingTime = 0;	// avoid sub-zero values
			break;
		}
	}
	modelCalcBounds(model);
}


// =====================  Effect: Horizontal Folds  =========================


static void
fxHorizontalFoldsInitGrid(AnimScreen * as,
						  WindowEvent forWindowEvent,
						  int *gridWidth, int *gridHeight)
{
	*gridWidth = 2;
	if (forWindowEvent == WindowEventShade ||
		forWindowEvent == WindowEventUnshade)
		*gridHeight = 3 + 2 *	
			as->opt[ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_NUM_FOLDS].value.i;
	else
		*gridHeight = 1 + 2 *
			as->opt[ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_NUM_FOLDS].value.i;
}

static void
fxHorizontalFoldsModelStepObject(CompWindow * w,
								 Model * model,
								 Object * object,
								 float forwardProgress,
								 float curveMaxAmp, int rowNo)
{
	ANIM_WINDOW(w);

	float origx = w->attrib.x + (WIN_W(w) * object->gridPosition.x -
								 w->output.left) * model->scale.x;
	float origy = w->attrib.y + (WIN_H(w) * object->gridPosition.y -
								 w->output.top) * model->scale.y;

	if (aw->curWindowEvent == WindowEventShade ||
		aw->curWindowEvent == WindowEventUnshade)
	{
		// Execute shade mode

		float relDistToFoldCenter = (rowNo % 2 == 1 ? 0.5 : 0);

		if (object->gridPosition.y == 0)
		{
			object->position.x = origx;
			object->position.y = WIN_Y(w);
		}
		else if (object->gridPosition.y == 1)
		{
			object->position.x = origx;
			object->position.y =
					(1 - forwardProgress) * origy +
					forwardProgress *
					(WIN_Y(w) + model->topHeight + model->bottomHeight);
		}
		else
		{
			object->position.x =
					origx + sin(forwardProgress * M_PI / 2) *
					(0.5 -
					 object->gridPosition.x) * 2 * model->scale.x *
					(curveMaxAmp -
					 curveMaxAmp * 4 * relDistToFoldCenter *
					 relDistToFoldCenter);
			object->position.y =
					(1 - forwardProgress) * origy +
					forwardProgress * (WIN_Y(w) + model->topHeight);
		}
	}
	else
	{							// Execute normal mode

		float relDistToFoldCenter;

		relDistToFoldCenter = (rowNo % 2 == 0 ? 0.5 : 0);

		object->position.x =
				origx + sin(forwardProgress * M_PI / 2) *
				(0.5 - object->gridPosition.x) * 2 * model->scale.x *
				(curveMaxAmp - curveMaxAmp * 4 *
				 relDistToFoldCenter * relDistToFoldCenter);
		object->position.y =
				(1 - forwardProgress) * origy + forwardProgress * BORDER_Y(w);
	}
}

static void
fxHorizontalFoldsModelStep(CompScreen * s, CompWindow * w, float time)
{
	int i, j, steps;

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;
	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	for (j = 0; j < steps; j++)
	{
		float forwardProgress =
				1 - (aw->animRemainingTime - timestep) /
				(aw->animTotalTime - timestep);
		if (aw->curWindowEvent == WindowEventCreate ||
			aw->curWindowEvent == WindowEventUnminimize ||
			aw->curWindowEvent == WindowEventUnshade)
			forwardProgress = 1 - forwardProgress;

		for (i = 0; i < model->numObjects; i++)
			fxHorizontalFoldsModelStepObject(w, 
				model,
				&model->objects[i],
				forwardProgress,
				as->opt[ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_AMP].value.f * WIN_W(w), 
				i / model->gridWidth);

		aw->animRemainingTime -= timestep;
		if (aw->animRemainingTime <= 0)
		{
			aw->animRemainingTime = 0;	// avoid sub-zero values
			break;
		}
	}
	modelCalcBounds(model);
}


// =====================  Effect: Roll Up  =========================

static void
fxRollUpInitGrid(AnimScreen * as,
				 WindowEvent forWindowEvent, int *gridWidth, int *gridHeight)
{
	*gridWidth = 2;
	if (forWindowEvent == WindowEventShade ||
		forWindowEvent == WindowEventUnshade)
		*gridHeight = 4;
	else
		*gridHeight = 2;
}

static void
fxRollUpModelStepObject(CompWindow * w,
						Model * model,
						Object * object,
						float forwardProgress, Bool fixedInterior)
{
	ANIM_WINDOW(w);

	float origx = WIN_X(w) + WIN_W(w) * object->gridPosition.x;

	if (aw->curWindowEvent == WindowEventShade ||
		aw->curWindowEvent == WindowEventUnshade)
	{
		// Execute shade mode

		// find position in window contents
		// (window contents correspond to 0.0-1.0 range)
		float relPosInWinContents =
				(object->gridPosition.y * WIN_H(w) -
				 model->topHeight) / w->height;

		if (object->gridPosition.y == 0)
		{
			object->position.x = origx;
			object->position.y = WIN_Y(w);
		}
		else if (object->gridPosition.y == 1)
		{
			object->position.x = origx;
			object->position.y =
					(1 - forwardProgress) *
					(WIN_Y(w) +
					 WIN_H(w) * object->gridPosition.y) +
					forwardProgress * (WIN_Y(w) +
									   model->topHeight +
									   model->bottomHeight);
		}
		else
		{
			object->position.x = origx;

			if (relPosInWinContents > forwardProgress)
			{
				object->position.y =
						(1 - forwardProgress) *
						(WIN_Y(w) +
						 WIN_H(w) * object->gridPosition.y) +
						forwardProgress * (WIN_Y(w) + model->topHeight);

				if (fixedInterior)
					object->offsetTexCoordForQuadBefore.y =
							-forwardProgress * w->height;
			}
			else
			{
				object->position.y = WIN_Y(w) + model->topHeight;
				if (!fixedInterior)
					object->offsetTexCoordForQuadAfter.
							y =
							(forwardProgress -
							 relPosInWinContents) * w->height;
			}
		}
	}
}

static void fxRollUpModelStep(CompScreen * s, CompWindow * w, float time)
{
	int i, j, steps;

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;
	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	for (j = 0; j < steps; j++)
	{
		float forwardProgress =
				1 - (aw->animRemainingTime - timestep) /
				(aw->animTotalTime - timestep);

		forwardProgress =
			(sigmoid(forwardProgress) - sigmoid(0)) /
			(sigmoid(1) - sigmoid(0));

		if (aw->curWindowEvent == WindowEventCreate ||
			aw->curWindowEvent == WindowEventUnminimize ||
			aw->curWindowEvent == WindowEventUnshade)
			forwardProgress = 1 - forwardProgress;

		for (i = 0; i < model->numObjects; i++)
			fxRollUpModelStepObject(w, 
				model,
				&model->objects[i],
				forwardProgress,
				as->opt[ANIM_SCREEN_OPTION_ROLLUP_FIXED_INTERIOR].value.b);

		aw->animRemainingTime -= timestep;
		if (aw->animRemainingTime <= 0)
		{
			aw->animRemainingTime = 0;	// avoid sub-zero values
			break;
		}
	}
	modelCalcBounds(model);
}


// =====================  Effect: Fade  =========================

static void fxFadeInit(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	// store window opacity
	aw->storedOpacity = w->paint.opacity;

	aw->timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);
}

static void defaultAnimStep(CompScreen * s, CompWindow * w, float time)
{
	int j, steps;

	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

	aw->timestep = timestep;

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;

	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	for (j = 0; j < steps; j++)
	{
		aw->animRemainingTime -= timestep;
		if (aw->animRemainingTime <= 0)
		{
			aw->animRemainingTime = 0;	// avoid sub-zero values
			break;
		}
	}
}

static void
fxFadeUpdateWindowAttrib(AnimScreen * as,
						 AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	float forwardProgress = defaultAnimProgress(aw);

	wAttrib->opacity = (GLushort) (aw->storedOpacity * (1 - forwardProgress));
}


// =====================  Effect: Focus Fade  =========================


static void
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

static void
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


// =====================  Effect: Burn  =========================

static void fxBurnInit(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	modelInitObjects(aw->model, WIN_X(w), WIN_Y(w), WIN_W(w), WIN_H(w));
	if (!aw->numPs)
	{
		aw->ps = calloc(1, 2 * sizeof(ParticleSystem));
		if (!aw->ps)
		{
			postAnimationCleanup(w, TRUE);
			return;
		}
		aw->numPs = 2;
	}
	initParticles(as->opt[ANIM_SCREEN_OPTION_FIRE_PARTICLES].value.i /
				  10, &aw->ps[0]);
	initParticles(as->opt[ANIM_SCREEN_OPTION_FIRE_PARTICLES].value.i,
				  &aw->ps[1]);
	aw->ps[1].slowdown = as->opt[ANIM_SCREEN_OPTION_FIRE_SLOWDOWN].value.f;
	aw->ps[1].darken = 0.5;
	aw->ps[1].blendMode = GL_ONE;

	aw->ps[0].slowdown =
			as->opt[ANIM_SCREEN_OPTION_FIRE_SLOWDOWN].value.f / 2.0;
	aw->ps[0].darken = 0.0;
	aw->ps[0].blendMode = GL_ONE_MINUS_SRC_ALPHA;

	if (!aw->ps[0].tex)
		glGenTextures(1, &aw->ps[0].tex);
	glBindTexture(GL_TEXTURE_2D, aw->ps[0].tex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0,
				 GL_RGBA, GL_UNSIGNED_BYTE, fireTex);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (!aw->ps[1].tex)
		glGenTextures(1, &aw->ps[1].tex);
	glBindTexture(GL_TEXTURE_2D, aw->ps[1].tex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0,
				 GL_RGBA, GL_UNSIGNED_BYTE, fireTex);
	glBindTexture(GL_TEXTURE_2D, 0);

	aw->animFireDirection = getAnimationDirection
			(w, &as->opt[ANIM_SCREEN_OPTION_FIRE_DIRECTION].value, FALSE);

	if (as->opt[ANIM_SCREEN_OPTION_FIRE_CONSTANT_SPEED].value.b)
	{
		aw->animTotalTime *= WIN_H(w) / 500.0;
		aw->animRemainingTime *= WIN_H(w) / 500.0;
	}
}

static void
fxBurnGenNewFire(CompScreen * s, ParticleSystem * ps, int x, int y,
				 int width, int height, float size, float time)
{
	ANIM_SCREEN(s);

	float max_new =
			ps->numParticles * (time / 50) * (1.05 -
											  as->
											  opt
											  [ANIM_SCREEN_OPTION_FIRE_LIFE].
											  value.f);
	int i;
	Particle *part;
	float rVal;

	for (i = 0; i < ps->numParticles && max_new > 0; i++)
	{
		part = &ps->particles[i];
		if (part->life <= 0.0f)
		{
			// give gt new life
			rVal = (float)(random() & 0xff) / 255.0;
			part->life = 1.0f;
			part->fade = (rVal * (1 - as->opt[ANIM_SCREEN_OPTION_FIRE_LIFE].value.f)) + 
				         (0.2f * (1.01 - as->opt[ANIM_SCREEN_OPTION_FIRE_LIFE].value.f));	// Random Fade Value

			// set size
			part->width = as->opt[ANIM_SCREEN_OPTION_FIRE_SIZE].value.f;
			part->height = as->opt[ANIM_SCREEN_OPTION_FIRE_SIZE].value.f * 1.5;
			rVal = (float)(random() & 0xff) / 255.0;
			part->w_mod = size * rVal;
			part->h_mod = size * rVal;

			// choose random position
			rVal = (float)(random() & 0xff) / 255.0;
			part->x = x + ((width > 1) ? (rVal * width) : 0);
			rVal = (float)(random() & 0xff) / 255.0;
			part->y = y + ((height > 1) ? (rVal * height) : 0);
			part->z = 0.0;
			part->xo = part->x;
			part->yo = part->y;
			part->zo = part->z;

			// set speed and direction
			rVal = (float)(random() & 0xff) / 255.0;
			part->xi = ((rVal * 20.0) - 10.0f);
			rVal = (float)(random() & 0xff) / 255.0;
			part->yi = ((rVal * 20.0) - 15.0f);
			part->zi = 0.0f;
			rVal = (float)(random() & 0xff) / 255.0;

			if (as->opt[ANIM_SCREEN_OPTION_FIRE_MYSTICAL].value.b)
			{
				// Random colors! (aka Mystical Fire)
				rVal = (float)(random() & 0xff) / 255.0;
				part->r = rVal;
				rVal = (float)(random() & 0xff) / 255.0;
				part->g = rVal;
				rVal = (float)(random() & 0xff) / 255.0;
				part->b = rVal;
			}
			else
			{
				// set color ABAB as->opt[ANIM_SCREEN_OPTION_FIRE_COLOR].value.f
				part->r = (float)as->opt[ANIM_SCREEN_OPTION_FIRE_COLOR].value.c[0] / 0xffff -
						  (rVal / 1.7 * (float)as->opt[ANIM_SCREEN_OPTION_FIRE_COLOR].value.c[0] / 0xffff);
				part->g = (float)as->opt[ANIM_SCREEN_OPTION_FIRE_COLOR].value.c[1] / 0xffff -
						  (rVal / 1.7 * (float)as->opt[ANIM_SCREEN_OPTION_FIRE_COLOR].value.c[1] / 0xffff);
				part->b = (float)as->opt[ANIM_SCREEN_OPTION_FIRE_COLOR].value.c[2] / 0xffff -
						  (rVal / 1.7 * (float)as->opt[ANIM_SCREEN_OPTION_FIRE_COLOR].value.c[2] / 0xffff);
			}
			// set transparancy
			part->a = (float)as->opt[ANIM_SCREEN_OPTION_FIRE_COLOR].value.c[3] / 0xffff;

			// set gravity
			part->xg = (part->x < part->xo) ? 1.0 : -1.0;
			part->yg = -3.0f;
			part->zg = 0.0f;

			ps->active = TRUE;
			max_new -= 1;
		}
		else
		{
			part->xg = (part->x < part->xo) ? 1.0 : -1.0;
		}
	}

}

static void
fxBurnGenNewSmoke(CompScreen * s, ParticleSystem * ps, int x, int y,
				  int width, int height, float size, float time)
{
	ANIM_SCREEN(s);

	float max_new =
			ps->numParticles * (time / 50) * (1.05 -
											  as->
											  opt
											  [ANIM_SCREEN_OPTION_FIRE_LIFE].
											  value.f);
	int i;
	Particle *part;
	float rVal;

	for (i = 0; i < ps->numParticles && max_new > 0; i++)
	{
		part = &ps->particles[i];
		if (part->life <= 0.0f)
		{
			// give gt new life
			rVal = (float)(random() & 0xff) / 255.0;
			part->life = 1.0f;
			part->fade = (rVal * (1 - as->opt[ANIM_SCREEN_OPTION_FIRE_LIFE].value.f)) + 
				         (0.2f * (1.01 - as->opt[ANIM_SCREEN_OPTION_FIRE_LIFE].value.f));	// Random Fade Value

			// set size
			part->width = as->opt[ANIM_SCREEN_OPTION_FIRE_SIZE].value.f * size * 5;
			part->height = as->opt[ANIM_SCREEN_OPTION_FIRE_SIZE].value.f * size * 5;
			rVal = (float)(random() & 0xff) / 255.0;
			part->w_mod = -0.8;
			part->h_mod = -0.8;

			// choose random position
			rVal = (float)(random() & 0xff) / 255.0;
			part->x = x + ((width > 1) ? (rVal * width) : 0);
			rVal = (float)(random() & 0xff) / 255.0;
			part->y = y + ((height > 1) ? (rVal * height) : 0);
			part->z = 0.0;
			part->xo = part->x;
			part->yo = part->y;
			part->zo = part->z;

			// set speed and direction
			rVal = (float)(random() & 0xff) / 255.0;
			part->xi = ((rVal * 20.0) - 10.0f);
			rVal = (float)(random() & 0xff) / 255.0;
			part->yi = (rVal + 0.2) * -size;
			part->zi = 0.0f;

			// set color
			rVal = (float)(random() & 0xff) / 255.0;
			part->r = rVal / 4.0;
			part->g = rVal / 4.0;
			part->b = rVal / 4.0;
			rVal = (float)(random() & 0xff) / 255.0;
			part->a = 0.5 + (rVal / 2.0);

			// set gravity
			part->xg = (part->x < part->xo) ? size : -size;
			part->yg = -size;
			part->zg = 0.0f;

			ps->active = TRUE;
			max_new -= 1;
		}
		else
		{
			part->xg = (part->x < part->xo) ? size : -size;
		}
	}

}

static void fxBurnModelStep(CompScreen * s, CompWindow * w, float time)
{
	int steps;

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	Bool smoke = as->opt[ANIM_SCREEN_OPTION_FIRE_SMOKE].value.b;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP_INTENSE].value.i);
	float old = 1 - (aw->animRemainingTime) / (aw->animTotalTime);
	float stepSize;

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;
	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	aw->animRemainingTime -= timestep;
	if (aw->animRemainingTime <= 0)
		aw->animRemainingTime = 0;	// avoid sub-zero values
	float new = 1 - (aw->animRemainingTime) / (aw->animTotalTime);

	stepSize = new - old;

	if (aw->curWindowEvent == WindowEventCreate ||
		aw->curWindowEvent == WindowEventUnminimize ||
		aw->curWindowEvent == WindowEventUnshade)
	{
		old = 1 - old;
		new = 1 - new;
	}

	if (!aw->drawRegion)
		aw->drawRegion = XCreateRegion();
	if (aw->animRemainingTime > 0)
	{
		XRectangle rect;

		switch (aw->animFireDirection)
		{
		case AnimDirectionUp:
			rect.x = 0;
			rect.y = 0;
			rect.width = WIN_W(w);
			rect.height = WIN_H(w) - (old * WIN_H(w));
			break;
		case AnimDirectionRight:
			rect.x = (old * WIN_W(w));
			rect.y = 0;
			rect.width = WIN_W(w) - (old * WIN_W(w));
			rect.height = WIN_H(w);
			break;
		case AnimDirectionLeft:
			rect.x = 0;
			rect.y = 0;
			rect.width = WIN_W(w) - (old * WIN_W(w));
			rect.height = WIN_H(w);
			break;
		case AnimDirectionDown:
		default:
			rect.x = 0;
			rect.y = (old * WIN_H(w));
			rect.width = WIN_W(w);
			rect.height = WIN_H(w) - (old * WIN_H(w));
			break;
		}
		XUnionRectWithRegion(&rect, &emptyRegion, aw->drawRegion);
	}
	else
	{
		XUnionRegion(&emptyRegion, &emptyRegion, aw->drawRegion);
	}
	if (new != 0)
		aw->useDrawRegion = TRUE;
	else
		aw->useDrawRegion = FALSE;

	if (aw->animRemainingTime > 0 && aw->ps)
	{
		switch (aw->animFireDirection)
		{
		case AnimDirectionUp:
			if (smoke)
				fxBurnGenNewSmoke(s, &aw->ps[0], WIN_X(w),
								  WIN_Y(w) + ((1 - old) * WIN_H(w)),
								  WIN_W(w), 1, WIN_W(w) / 40.0, time);
			fxBurnGenNewFire(s, &aw->ps[1], WIN_X(w),
							 WIN_Y(w) + ((1 - old) * WIN_H(w)),
							 WIN_W(w), (stepSize) * WIN_H(w),
							 WIN_W(w) / 40.0, time);
			break;
		case AnimDirectionLeft:
			if (smoke)
				fxBurnGenNewSmoke(s, &aw->ps[0],
								  WIN_X(w) + ((1 - old) * WIN_W(w)),
								  WIN_Y(w),
								  (stepSize) * WIN_W(w),
								  WIN_H(w), WIN_H(w) / 40.0, time);
			fxBurnGenNewFire(s, &aw->ps[1],
							 WIN_X(w) + ((1 - old) * WIN_W(w)),
							 WIN_Y(w), (stepSize) * WIN_W(w),
							 WIN_H(w), WIN_H(w) / 40.0, time);
			break;
		case AnimDirectionRight:
			if (smoke)
				fxBurnGenNewSmoke(s, &aw->ps[0],
								  WIN_X(w) + (old * WIN_W(w)),
								  WIN_Y(w),
								  (stepSize) * WIN_W(w),
								  WIN_H(w), WIN_H(w) / 40.0, time);
			fxBurnGenNewFire(s, &aw->ps[1],
							 WIN_X(w) + (old * WIN_W(w)),
							 WIN_Y(w), (stepSize) * WIN_W(w),
							 WIN_H(w), WIN_H(w) / 40.0, time);
			break;
		case AnimDirectionDown:
		default:
			if (smoke)
				fxBurnGenNewSmoke(s, &aw->ps[0], WIN_X(w),
								  WIN_Y(w) + (old * WIN_H(w)),
								  WIN_W(w), 1, WIN_W(w) / 40.0, time);
			fxBurnGenNewFire(s, &aw->ps[1], WIN_X(w),
							 WIN_Y(w) + (old * WIN_H(w)),
							 WIN_W(w), (stepSize) * WIN_H(w),
							 WIN_W(w) / 40.0, time);
			break;
		}

	}
	if (aw->animRemainingTime <= 0 && aw->numPs
		&& (aw->ps[0].active || aw->ps[1].active))
		aw->animRemainingTime = timestep;

	if (!aw->numPs)
	{
		if (aw->ps)
		{
			finiParticles(aw->ps);
			free(aw->ps);
			aw->ps = NULL;
		}
		return;					// FIXME - is this correct behaviour?
	}

	int i;
	Particle *part;

	for (i = 0;
		 i < aw->ps[0].numParticles && aw->animRemainingTime > 0
		 && smoke; i++)
	{
		part = &aw->ps[0].particles[i];
		part->xg = (part->x < part->xo) ? WIN_W(w) / 40.0 : -WIN_W(w) / 40.0;
	}
	aw->ps[0].x = WIN_X(w);
	aw->ps[0].y = WIN_Y(w);

	for (i = 0; i < aw->ps[1].numParticles && aw->animRemainingTime > 0; i++)
	{
		part = &aw->ps[1].particles[i];
		part->xg = (part->x < part->xo) ? 1.0 : -1.0;
	}
	aw->ps[1].x = WIN_X(w);
	aw->ps[1].y = WIN_Y(w);

	modelCalcBounds(model);
}

static void drawParticleSystems(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);

	if (aw->numPs)
	{
		int i = 0;

		for (i = 0; i < aw->numPs; i++)
		{
			if (aw->ps[i].active && !WINDOW_INVISIBLE(w))
			{
				drawParticles(s, w, &aw->ps[i]);
			}
		}
	}
}


// =====================  Effect: Beam Up  =========================

static void fxBeamUpInit(CompScreen * s, CompWindow * w)
{
	int particles = WIN_W(w);

	fxFadeInit(s, w);
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);
	if (!aw->numPs)
	{
		aw->ps = calloc(1, 2 * sizeof(ParticleSystem));
		if (!aw->ps)
		{
			postAnimationCleanup(w, TRUE);
			return;
		}
		aw->numPs = 2;
	}
	initParticles(particles / 10, &aw->ps[0]);
	initParticles(particles, &aw->ps[1]);
	aw->ps[1].slowdown = as->opt[ANIM_SCREEN_OPTION_BEAMUP_SLOWDOWN].value.f;
	aw->ps[1].darken = 0.5;
	aw->ps[1].blendMode = GL_ONE;

	aw->ps[0].slowdown =
			as->opt[ANIM_SCREEN_OPTION_BEAMUP_SLOWDOWN].value.f / 2.0;
	aw->ps[0].darken = 0.0;
	aw->ps[0].blendMode = GL_ONE_MINUS_SRC_ALPHA;

	if (!aw->ps[0].tex)
		glGenTextures(1, &aw->ps[0].tex);
	glBindTexture(GL_TEXTURE_2D, aw->ps[0].tex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0,
				 GL_RGBA, GL_UNSIGNED_BYTE, fireTex);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (!aw->ps[1].tex)
		glGenTextures(1, &aw->ps[1].tex);
	glBindTexture(GL_TEXTURE_2D, aw->ps[1].tex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0,
				 GL_RGBA, GL_UNSIGNED_BYTE, fireTex);
	glBindTexture(GL_TEXTURE_2D, 0);

}

static void
fxBeamUpGenNewFire(CompScreen * s, ParticleSystem * ps, int x, int y,
				   int width, int height, float size, float time)
{
	ANIM_SCREEN(s);

	ps->numParticles =
			width / as->opt[ANIM_SCREEN_OPTION_BEAMUP_SPACING].value.i;
	float max_new =
			ps->numParticles * (time / 50) * (1.05 -
											  as->
											  opt
											  [ANIM_SCREEN_OPTION_BEAMUP_LIFE].
											  value.f);
	int i;
	Particle *part;
	float rVal;

	for (i = 0; i < ps->numParticles && max_new > 0; i++)
	{
		part = &ps->particles[i];
		if (part->life <= 0.0f)
		{
			// give gt new life
			rVal = (float)(random() & 0xff) / 255.0;
			part->life = 1.0f;
			part->fade = (rVal * (1 - as->opt[ANIM_SCREEN_OPTION_BEAMUP_LIFE].value.f)) + 
				         (0.2f * (1.01 - as->opt[ANIM_SCREEN_OPTION_BEAMUP_LIFE].value.f));	// Random Fade Value

			// set size
			part->width = 2.5 * as->opt[ANIM_SCREEN_OPTION_BEAMUP_SIZE].value.f;
			part->height = height;
			part->w_mod = size * 0.2;
			part->h_mod = size * 0.02;

			// choose random x position
			rVal = (float)(random() & 0xff) / 255.0;
			part->x = x + ((width > 1) ? (rVal * width) : 0);
			part->y = y;
			part->z = 0.0;
			part->xo = part->x;
			part->yo = part->y;
			part->zo = part->z;

			// set speed and direction
			part->xi = 0.0f;
			part->yi = 0.0f;
			part->zi = 0.0f;

			// set color ABAB as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.f
			part->r = (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[0] / 0xffff -
					  (rVal / 1.7 * (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[0] / 0xffff);
			part->g = (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[1] / 0xffff -
					  (rVal / 1.7 * (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[1] / 0xffff);
			part->b = (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[2] / 0xffff -
					  (rVal / 1.7 * (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[2] / 0xffff);
			part->a = (float)as->opt[ANIM_SCREEN_OPTION_BEAMUP_COLOR].value.c[3] / 0xffff;

			// set gravity
			part->xg = 0.0f;
			part->yg = 0.0f;
			part->zg = 0.0f;

			ps->active = TRUE;
			max_new -= 1;
		}
		else
		{
			part->xg = (part->x < part->xo) ? 1.0 : -1.0;
		}
	}

}

static void fxBeamUpModelStep(CompScreen * s, CompWindow * w, float time)
{
	int steps;
	int creating = 0;

	defaultAnimStep(s, w, time);

	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	Model *model = aw->model;

	float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					  as->opt[ANIM_SCREEN_OPTION_TIME_STEP_INTENSE].value.i);
	float old = 1 - (aw->animRemainingTime) / (aw->animTotalTime);
	float stepSize;

	aw->remainderSteps += time / timestep;
	steps = floor(aw->remainderSteps);
	aw->remainderSteps -= steps;
	if (!steps && aw->animRemainingTime < aw->animTotalTime)
		return;
	steps = MAX(1, steps);

	aw->animRemainingTime -= timestep;
	if (aw->animRemainingTime <= 0)
		aw->animRemainingTime = 0;	// avoid sub-zero values
	float new = 1 - (aw->animRemainingTime) / (aw->animTotalTime);

	stepSize = new - old;

	if (aw->curWindowEvent == WindowEventCreate ||
		aw->curWindowEvent == WindowEventUnminimize ||
		aw->curWindowEvent == WindowEventUnshade)
	{
		old = 1 - old;
		new = 1 - new;
		creating = 1;
	}

	if (!aw->drawRegion)
		aw->drawRegion = XCreateRegion();
	if (aw->animRemainingTime > 0)
	{
		XRectangle rect;

		rect.x = ((old / 2) * WIN_W(w));
		rect.width = WIN_W(w) - (old * WIN_W(w));
		rect.y = ((old / 2) * WIN_H(w));
		rect.height = WIN_H(w) - (old * WIN_H(w));
		XUnionRectWithRegion(&rect, &emptyRegion, aw->drawRegion);
	}
	else
	{
		XUnionRegion(&emptyRegion, &emptyRegion, aw->drawRegion);
	}
	if (new != 0)
		aw->useDrawRegion = TRUE;
	else
		aw->useDrawRegion = FALSE;

	if (aw->animRemainingTime > 0 && aw->ps)
	{
		fxBeamUpGenNewFire(s, &aw->ps[1], 
			WIN_X(w), WIN_Y(w) + (WIN_H(w) / 2), WIN_W(w),
			creating ? WIN_H(w) - (old / 2 * WIN_H(w)) : 
			           (WIN_H(w) -   (old * WIN_H(w))),
		    WIN_W(w) / 40.0, time);

	}
	if (aw->animRemainingTime <= 0 && aw->numPs
		&& (aw->ps[0].active || aw->ps[1].active))
		aw->animRemainingTime = timestep;

	if (!aw->numPs)
	{
		if (aw->ps)
		{
			finiParticles(aw->ps);
			free(aw->ps);
			aw->ps = NULL;
		}
		return;					// FIXME - is this correct behaviour?
	}

	int i;
	Particle *part;

	aw->ps[0].x = WIN_X(w);
	aw->ps[0].y = WIN_Y(w);

	for (i = 0; i < aw->ps[1].numParticles && aw->animRemainingTime > 0; i++)
	{
		part = &aw->ps[1].particles[i];
		part->xg = (part->x < part->xo) ? 1.0 : -1.0;
	}
	aw->ps[1].x = WIN_X(w);
	aw->ps[1].y = WIN_Y(w);

	modelCalcBounds(model);
}

static void
fxBeamupUpdateWindowAttrib(AnimScreen * as,
						   AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	float forwardProgress = 0;
	if (aw->animTotalTime - aw->timestep != 0)
		forwardProgress =
			1 - (aw->animRemainingTime - aw->timestep) /
			(aw->animTotalTime - aw->timestep);
	forwardProgress = MIN(forwardProgress, 1);
	forwardProgress = MAX(forwardProgress, 0);

	if (aw->curWindowEvent == WindowEventCreate ||
		aw->curWindowEvent == WindowEventUnminimize)
	{
		forwardProgress = forwardProgress * forwardProgress;
		forwardProgress = forwardProgress * forwardProgress;
		forwardProgress = 1 - forwardProgress;
	}

	wAttrib->opacity = (GLushort) (aw->storedOpacity * (1 - forwardProgress));
}


// =====================  Effect: 3D Spread  =========================

// Frees up polygon objects in pset
static void freePolygonObjects(PolygonSet * pset)
{
	PolygonObject *p = pset->polygons;

	if (!p)
	{
		pset->nPolygons = 0;
		return;
	}
	int i;

	for (i = 0; i < pset->nPolygons; i++, p++)
	{
		if (p->nVertices > 0)
		{
			if (p->vertices)
				free(p->vertices);
			if (p->sideIndices)
				free(p->sideIndices);
			if (p->normals)
				free(p->normals);
			p->vertices = 0;
			p->sideIndices = 0;
			p->normals = 0;
			p->nVertices = 0;
		}
		/*if (p->nShadowQuads > 0)
		   {
		   if (p->shadowVertices)
		   free(p->shadowVertices);
		   if (p->shadowTexCoords)
		   free(p->shadowTexCoords);
		   p->shadowVertices = 0;
		   p->shadowTexCoords = 0;
		   p->nShadowQuads = 0;
		   } */
		p->nSides = 0;
	}
	free(pset->polygons);
	pset->polygons = 0;
	pset->nPolygons = 0;
}

// Frees up intersecting polygon info of PolygonSet clips
static void freeClipsPolygons(PolygonSet * pset)
{
	int k;

	for (k = 0; k < pset->clipCapacity; k++)
	{
		if (pset->clips[k].intersectingPolygons)
		{
			free(pset->clips[k].intersectingPolygons);
			pset->clips[k].intersectingPolygons = 0;
		}
		if (pset->clips[k].polygonVertexTexCoords)
		{
			free(pset->clips[k].polygonVertexTexCoords);
			pset->clips[k].polygonVertexTexCoords = 0;
		}
		pset->clips[k].nIntersectingPolygons = 0;
	}
}

// Frees up the whole polygon set
static void freePolygonSet(AnimWindow * aw)
{
	PolygonSet *pset = aw->polygonSet;

	freePolygonObjects(pset);
	if (pset->clipCapacity > 0)
	{
		freeClipsPolygons(pset);
		free(pset->clips);
		pset->clips = 0;
		pset->nClips = 0;
		pset->firstNondrawnClip = 0;
		pset->clipCapacity = 0;
	}
	free(pset);
	aw->polygonSet = 0;
}

/*
// Tessellates window into extruded rectangular objects
// in a brick wall formation
static Bool
tessellateIntoBricks(CompWindow * w,
                     int gridSizeX, int gridSizeY, float thickness)
{
}

// Tessellates window into Voronoi segments
static Bool
tessellateVoronoi(CompWindow * w,
                  float thickness)
{
}
*/

// Tessellates window into extruded rectangular objects
static Bool
tessellateIntoRectangles(CompWindow * w,
						 int gridSizeX, int gridSizeY, float thickness)
{
	ANIM_WINDOW(w);

	PolygonSet *pset = aw->polygonSet;

	if (!pset)
		return FALSE;

	int winLimitsX;				// boundaries of polygon tessellation
	int winLimitsY;
	int winLimitsW;
	int winLimitsH;

	if (pset->includeShadows)
	{
		winLimitsX = WIN_X(w);
		winLimitsY = WIN_Y(w);
		winLimitsW = WIN_W(w) - 1; // avoid artifact on right edge
		winLimitsH = WIN_H(w);
	}
	else
	{
		winLimitsX = BORDER_X(w);
		winLimitsY = BORDER_Y(w);
		winLimitsW = BORDER_W(w);
		winLimitsH = BORDER_H(w);
	}
	float minRectSize = MIN_WINDOW_GRID_SIZE;
	float rectW = winLimitsW / (float)gridSizeX;
	float rectH = winLimitsH / (float)gridSizeY;

	if (rectW < minRectSize)
		gridSizeX = winLimitsW / minRectSize;	// int div.
	if (rectH < minRectSize)
		gridSizeY = winLimitsH / minRectSize;	// int div.

	if (pset->nPolygons != gridSizeX * gridSizeY)
	{
		if (pset->nPolygons > 0)
			freePolygonObjects(pset);

		pset->nPolygons = gridSizeX * gridSizeY;

		pset->polygons = calloc(1, sizeof(PolygonObject) * pset->nPolygons);
		if (!pset->polygons)
		{
			fprintf(stderr, "%s: Not enough memory at line %d!\n",
					__FILE__, __LINE__);
			pset->nPolygons = 0;
			return FALSE;
		}
	}

	thickness /= w->screen->width;
	pset->thickness = thickness;
	pset->nTotalFrontVertices = 0;

	float cellW = (float)winLimitsW / gridSizeX;
	float cellH = (float)winLimitsH / gridSizeY;
	float halfW = cellW / 2;
	float halfH = cellH / 2;

	float halfThick = pset->thickness / 2;
	PolygonObject *p = pset->polygons;
	int x, y;

	//float vec = 1 / sqrt(3); // vector component for normals

	for (y = 0; y < gridSizeY; y++)
	{
		float posY = winLimitsY + cellH * (y + 0.5);

		for (x = 0; x < gridSizeX; x++, p++)
		{
			p->centerPos.x = p->centerPosStart.x =
					winLimitsX + cellW * (x + 0.5);
			p->centerPos.y = p->centerPosStart.y = posY;
			p->centerPos.z = p->centerPosStart.z = -halfThick;
			p->rotAngle = p->rotAngleStart = 0;

			p->centerRelPos.x = (x + 0.5) / gridSizeX;
			p->centerRelPos.y = (y + 0.5) / gridSizeY;

			p->nSides = 4;
			p->nVertices = 2 * 4;
			pset->nTotalFrontVertices += 4;

			// 4 front, 4 back vertices
			if (!p->vertices)
			{
				p->vertices = calloc(1, sizeof(GLfloat) * 8 * 3);
			}
			//if (!p->vertexOnEdge)
			//  p->vertexOnEdge = calloc (1, sizeof (int) * p->nSides);
			//p->vertexTexCoords4Clips = calloc (1, sizeof (GLfloat) * 4 * 2 * 2);
			if (!p->vertices)	// || !p->vertexOnEdge)// || !p->vertexTexCoords4Clips)
			{
				fprintf(stderr, "%s: Not enough memory at line %d!\n",
						__FILE__, __LINE__);
				freePolygonObjects(pset);
				return FALSE;
			}

			GLfloat *pv = p->vertices;

			// Determine 4 front vertices in ccw direction
			pv[0] = -halfW;
			pv[1] = -halfH;
			pv[2] = halfThick;

			pv[3] = -halfW;
			pv[4] = halfH;
			pv[5] = halfThick;

			pv[6] = halfW;
			pv[7] = halfH;
			pv[8] = halfThick;

			pv[9] = halfW;
			pv[10] = -halfH;
			pv[11] = halfThick;

			// Determine 4 back vertices in cw direction
			pv[12] = halfW;
			pv[13] = -halfH;
			pv[14] = -halfThick;

			pv[15] = halfW;
			pv[16] = halfH;
			pv[17] = -halfThick;

			pv[18] = -halfW;
			pv[19] = halfH;
			pv[20] = -halfThick;

			pv[21] = -halfW;
			pv[22] = -halfH;
			pv[23] = -halfThick;

			// 16 indices for 4 sides (for quad strip)
			if (!p->sideIndices)
			{
				//p->sideIndices = calloc(1, sizeof(GLushort) * 2 * (4 + 1));
				p->sideIndices = calloc(1, sizeof(GLushort) * 4 * 4);
			}
			if (!p->sideIndices)
			{
				fprintf(stderr, "%s: Not enough memory at line %d!\n",
						__FILE__, __LINE__);
				freePolygonObjects(pset);
				return FALSE;
			}

			GLushort *ind = p->sideIndices;

			/*
			   ind[0] = 0;
			   ind[1] = 7;
			   ind[2] = 1;
			   ind[3] = 6;
			   ind[4] = 2;
			   ind[5] = 5;
			   ind[6] = 3;
			   ind[7] = 4;
			   ind[8] = 0;
			   ind[9] = 7;
			 */
			int id = 0;

			ind[id++] = 0;
			ind[id++] = 7;
			ind[id++] = 6;
			ind[id++] = 1;

			ind[id++] = 1;
			ind[id++] = 6;
			ind[id++] = 5;
			ind[id++] = 2;

			ind[id++] = 2;
			ind[id++] = 5;
			ind[id++] = 4;
			ind[id++] = 3;

			ind[id++] = 3;
			ind[id++] = 4;
			ind[id++] = 7;
			ind[id++] = 0;

			// Surface normals
			if (!p->normals)
			{
				p->normals = calloc(1, sizeof(GLfloat) * (2 + 4) * 3);
			}
			if (!p->normals)
			{
				fprintf(stderr, "%s: Not enough memory at line %d!\n",
						__FILE__, __LINE__);
				freePolygonObjects(pset);
				return FALSE;
			}

			GLfloat *nor = p->normals;

			// Front
			nor[0] = 0;
			nor[1] = 0;
			nor[2] = -1;

			// Back
			nor[3] = 0;
			nor[4] = 0;
			nor[5] = 1;

			// Sides
			nor[6] = -1;
			nor[7] = 0;
			nor[8] = 0;

			nor[9] = 0;
			nor[10] = 1;
			nor[11] = 0;

			nor[12] = 1;
			nor[13] = 0;
			nor[14] = 0;

			nor[15] = 0;
			nor[16] = -1;
			nor[17] = 0;

			// Determine bounding box (to test intersection with clips)
			p->boundingBox.x1 = -halfW + p->centerPos.x;
			p->boundingBox.y1 = -halfH + p->centerPos.y;
			p->boundingBox.x2 = ceil(halfW + p->centerPos.x);
			p->boundingBox.y2 = ceil(halfH + p->centerPos.y);
			/*
			   // Determine edge/corner status
			   if (x == 0)
			   {
			   p->nShadowQuads++;
			   if (y == 0)
			   p->vertexOnEdge[0] = 5;
			   if (y == gridSizeY - 1)
			   p->nShadowQuads++;
			   }
			   if (x == gridSizeX - 1)
			   {
			   p->nShadowQuads++;
			   if (y == 0)
			   p->nShadowQuads++;
			   if (y == gridSizeY - 1)
			   p->nShadowQuads++;
			   }
			   if (y == 0)
			   p->nShadowQuads++;
			   if (y == gridSizeY - 1)
			   p->nShadowQuads++; */
		}
	}
	return TRUE;
}

// Tessellates window into extruded hexagon objects
static Bool
tessellateIntoHexagons(CompWindow * w,
                     int gridSizeX, int gridSizeY, float thickness)
{
	ANIM_WINDOW(w);

	PolygonSet *pset = aw->polygonSet;

	if (!pset)
		return FALSE;

	int winLimitsX;				// boundaries of polygon tessellation
	int winLimitsY;
	int winLimitsW;
	int winLimitsH;

	if (pset->includeShadows)
	{
		winLimitsX = WIN_X(w);
		winLimitsY = WIN_Y(w);
		winLimitsW = WIN_W(w) - 1; // avoid artifact on right edge
		winLimitsH = WIN_H(w);
	}
	else
	{
		winLimitsX = BORDER_X(w);
		winLimitsY = BORDER_Y(w);
		winLimitsW = BORDER_W(w);
		winLimitsH = BORDER_H(w);
	}
	float minSize = 20;
	float hexW = winLimitsW / (float)gridSizeX;
	float hexH = winLimitsH / (float)gridSizeY;

	if (hexW < minSize)
		gridSizeX = winLimitsW / minSize;	// int div.
	if (hexH < minSize)
		gridSizeY = winLimitsH / minSize;	// int div.

	int nPolygons = (gridSizeY + 1) * gridSizeX + (gridSizeY + 1) / 2;

	if (pset->nPolygons != nPolygons)
	{
		if (pset->nPolygons > 0)
			freePolygonObjects(pset);

		pset->nPolygons = nPolygons;

		pset->polygons = calloc(1, sizeof(PolygonObject) * pset->nPolygons);
		if (!pset->polygons)
		{
			fprintf(stderr, "%s: Not enough memory at line %d!\n",
					__FILE__, __LINE__);
			pset->nPolygons = 0;
			return FALSE;
		}
	}

	thickness /= w->screen->width;
	pset->thickness = thickness;
	pset->nTotalFrontVertices = 0;

	float cellW = (float)winLimitsW / gridSizeX;
	float cellH = (float)winLimitsH / gridSizeY;
	float halfW = cellW / 2;
	float twoThirdsH = 2*cellH / 3;
	float thirdH = cellH / 3;

	float halfThick = pset->thickness / 2;
	PolygonObject *p = pset->polygons;
	int x, y;

	for (y = 0; y < gridSizeY+1; y++)
	{
		float posY = winLimitsY + cellH * (y);
		int numPolysinRow = (y%2==0) ? gridSizeX : (gridSizeX + 1);
		// Clip polygons to the window dimensions
		float topY, topRightY, topLeftY, bottomY, bottomLeftY, bottomRightY;
		if(y == 0){
			topY = topRightY = topLeftY = 0;
			bottomY = twoThirdsH;
			bottomLeftY = bottomRightY = thirdH;
		} else if(y == gridSizeY){
			bottomY = bottomLeftY = bottomRightY = 0;
			topY = -twoThirdsH;
			topLeftY = topRightY = -thirdH;
		} else {
			topY = -twoThirdsH;
			topLeftY = topRightY = -thirdH;
			bottomLeftY = bottomRightY = thirdH;
			bottomY = twoThirdsH;
		}

		for (x = 0; x < numPolysinRow; x++, p++)
		{
			// Clip odd rows when necessary
			float topLeftX, topRightX, bottomLeftX, bottomRightX;
			if(y%2 == 1){
				if(x == 0){
					topLeftX = bottomLeftX = 0;
					topRightX = halfW;
					bottomRightX = halfW;
				} else if(x == numPolysinRow-1){
					topRightX = bottomRightX = 0;
					topLeftX = -halfW;
					bottomLeftX = -halfW;
				} else {
					topLeftX = bottomLeftX = -halfW;
					topRightX = bottomRightX = halfW;
				}
			} else {
				topLeftX = bottomLeftX = -halfW;
				topRightX = bottomRightX = halfW;
			}
			
			p->centerPos.x = p->centerPosStart.x =
					winLimitsX + cellW * (x + (y%2 ? 0.0 : 0.5));
			p->centerPos.y = p->centerPosStart.y = posY;
			p->centerPos.z = p->centerPosStart.z = -halfThick;
			p->rotAngle = p->rotAngleStart = 0;

			p->centerRelPos.x = (x + 0.5) / gridSizeX;
			p->centerRelPos.y = (y + 0.5) / gridSizeY;

			p->nSides = 6;
			p->nVertices = 2 * 6;
			pset->nTotalFrontVertices += 6;

			// 6 front, 6 back vertices
			if (!p->vertices)
			{
				p->vertices = calloc(1, sizeof(GLfloat) * 6 * 2 * 3);
				if (!p->vertices)
				{
					fprintf(stderr, "%s: Not enough memory at line %d!\n",
							__FILE__, __LINE__);
					freePolygonObjects(pset);
					return FALSE;
				}
			}

			GLfloat *pv = p->vertices;

			// Determine 6 front vertices in ccw direction
			// Starting at top
			pv[0] = 0;
			pv[1] = topY;
			pv[2] = halfThick;

			pv[3] = topLeftX;
			pv[4] = topLeftY;
			pv[5] = halfThick;

			pv[6] = bottomLeftX;
			pv[7] = bottomLeftY;
			pv[8] = halfThick;

			pv[9] = 0;
			pv[10] = bottomY;
			pv[11] = halfThick;

			pv[12] = bottomRightX;
			pv[13] = bottomRightY;
			pv[14] = halfThick;

			pv[15] = topRightX;
			pv[16] = topRightY;
			pv[17] = halfThick;
			
			// Determine 6 back vertices in cw direction
			pv[18] = topRightX;
			pv[19] = topRightY;
			pv[20] = -halfThick;

			pv[21] = bottomRightX;
			pv[22] = bottomRightY;
			pv[23] = -halfThick;
			
			pv[24] = 0;
			pv[25] = bottomY;
			pv[26] = -halfThick;

			pv[27] = bottomLeftX;
			pv[28] = bottomLeftY;
			pv[29] = -halfThick;
			
			pv[30] = topLeftX;
			pv[31] = topLeftY;
			pv[32] = -halfThick;

			pv[33] = 0;
			pv[34] = topY;
			pv[35] = -halfThick;

			// 24 indices per 6 sides (for quad strip)
			if (!p->sideIndices)
			{
				p->sideIndices = calloc(1, sizeof(GLushort) * 4*6);
			}
			if (!p->sideIndices)
			{
				fprintf(stderr, "%s: Not enough memory at line %d!\n",
						__FILE__, __LINE__);
				freePolygonObjects(pset);
				return FALSE;
			}

			GLushort *ind = p->sideIndices;

			int id = 0;
			// upper left side face
			ind[id++] = 0;
			ind[id++] = 11;
			ind[id++] = 10;
			ind[id++] = 1;
			// left side face
			ind[id++] = 1;
			ind[id++] = 10;
			ind[id++] = 9;
			ind[id++] = 2;
			// lower left side face
			ind[id++] = 2;
			ind[id++] = 9;
			ind[id++] = 8;
			ind[id++] = 3;
			// lower right side face
			ind[id++] = 3;
			ind[id++] = 8;
			ind[id++] = 7;
			ind[id++] = 4;
			// right side face
			ind[id++] = 4;
			ind[id++] = 7;
			ind[id++] = 6;
			ind[id++] = 5;
			// upper right side face
			ind[id++] = 5;
			ind[id++] = 6;
			ind[id++] = 11;
			ind[id++] = 0;			

			// Surface normals
			if (!p->normals)
			{
				p->normals = calloc(1, sizeof(GLfloat) * (2 + 6) * 3);
			}
			if (!p->normals)
			{
				fprintf(stderr, "%s: Not enough memory at line %d!\n",
						__FILE__, __LINE__);
				freePolygonObjects(pset);
				return FALSE;
			}

			GLfloat *nor = p->normals;

			// Front
			nor[0] = 0;
			nor[1] = 0;
			nor[2] = -1;
			
			// Back
			nor[3] = 0;
			nor[4] = 0;
			nor[5] = 1;
			
			// Sides
			nor[6] = -1;
			nor[7] = 1;
			nor[8] = 0;
			
			nor[9] = -1;
			nor[10] = 0;
			nor[11] = 0;
			
			nor[12] = -1;
			nor[13] = -1;
			nor[14] = 0;

			nor[15] = 1;
			nor[16] = -1;
			nor[17] = 0;

			nor[18] = 1;
			nor[19] = 0;
			nor[20] = 0;

			nor[21] = 1;
			nor[22] = 1;
			nor[23] = 0;			

			// Determine bounding box (to test intersection with clips)
			p->boundingBox.x1 = topLeftX + p->centerPos.x;
			p->boundingBox.y1 = topY + p->centerPos.y;
			p->boundingBox.x2 = ceil(bottomRightX + p->centerPos.x);
			p->boundingBox.y2 = ceil(bottomY + p->centerPos.y);
		}
	}
	if (pset->nPolygons != p - pset->polygons)
		fprintf(stderr, "%s: Error in tessellateIntoHexagons at line %d!\n",
				__FILE__, __LINE__);
	return TRUE;

}

static void
polygonsStoreClips(CompScreen * s, CompWindow * w,
				   int nClip, BoxPtr pClip, int nMatrix, CompMatrix * matrix)
{
	ANIM_WINDOW(w);

	PolygonSet *pset = aw->polygonSet;

	// if polygon set is not valid or effect is not 3D (glide w/thickness=0)
	if (!pset)
		return;

	// only draw windows on current viewport
	if (w->attrib.x > s->width || w->attrib.x + w->width < 0 ||
		w->attrib.y > s->height || w->attrib.y + w->height < 0 ||
		(aw->lastKnownCoords.x != NOT_INITIALIZED &&
		 (aw->lastKnownCoords.x != w->attrib.x ||
		  aw->lastKnownCoords.y != w->attrib.y)))
	{
		return;
		// since this is not the viewport the window was drawn
		// just before animation started
	}

	Bool dontStoreClips = TRUE;

	// If this clip doesn't match the corresponding stored clip,
	// clear the stored clips from this point (aw->nClipsPassed)
	// to the end and store the new ones instead.

	if (aw->nClipsPassed < pset->nClips) // if we have clips stored earlier
	{
		Clip4Polygons *c = pset->clips + aw->nClipsPassed;
		// the stored clip at position aw->nClipsPassed

		// if either clip coordinates or texture matrix is different
		if (memcmp(pClip, &c->box, sizeof(Box)) ||
			memcmp(matrix, &c->texMatrix, sizeof(CompMatrix)))
		{
			// get rid of the clips from here (aw->nClipsPassed) to the end
			pset->nClips = aw->nClipsPassed;
			dontStoreClips = FALSE;
		}
	}
	else
		dontStoreClips = FALSE;

	if (dontStoreClips)
	{
		aw->nClipsPassed += nClip;
		return;
	}
	// For each clip passed to this function
	for (; nClip--; pClip++, aw->nClipsPassed++)
	{
		// New clip

		if (!ensureLargerClipCapacity(pset))
		{
			fprintf(stderr, "%s: Not enough memory at line %d!\n",
					__FILE__, __LINE__);
			return;
		}

		Clip4Polygons *newClip = &pset->clips[pset->nClips];

		newClip->id = aw->nClipsPassed;
		memcpy(&newClip->box, pClip, sizeof(Box));
		memcpy(&newClip->texMatrix, matrix, sizeof(CompMatrix));
		// nMatrix is not used for now
		// (i.e. only first texture matrix is considered)

		// avoid clipping along window edge
		// for the "window contents" clip
		if (pClip->x1 == BORDER_X(w) &&
		    pClip->y1 == BORDER_Y(w) &&
			pClip->x2 == BORDER_X(w) + BORDER_W(w) &&
			pClip->y2 == BORDER_Y(w) + BORDER_H(w))
		{
			newClip->boxf.x1 = pClip->x1 - 0.1f;
			newClip->boxf.y1 = pClip->y1 - 0.1f;
			newClip->boxf.x2 = pClip->x2 + 0.1f;
			newClip->boxf.y2 = pClip->y2 + 0.1f;
		}
		else
		{
			newClip->boxf.x1 = pClip->x1;
			newClip->boxf.y1 = pClip->y1;
			newClip->boxf.x2 = pClip->x2;
			newClip->boxf.y2 = pClip->y2;
		}

		pset->nClips++;
		aw->clipsUpdated = TRUE;

		/*
		   // Look for a container / contained clip to minimize # of clips.
		   // Go backward, since such a clip is likely to be one of the last ones.
		   // Go until you hit the clips corresponding to the previous texture.
		   int i;
		   for (i=pset->nClips-1; i>=pset->firstClipWithNoTex; i--)
		   {
		   Clip4Polygons *clip = &pset->clips[i];

		   // if tex matrices are different
		   if (clip->texMatrix.xx != matrix->xx ||
		   clip->texMatrix.yy != matrix->yy ||
		   clip->texMatrix.x0 != matrix->x0 ||
		   clip->texMatrix.y0 != matrix->y0)
		   continue;

		   // if an old clip contains the new clip
		   if (x1 >= clip->box.x1 && y1 >= clip->box.y1 &&
		   x2 <= clip->box.x2 && y2 <= clip->box.y2)
		   {
		   newClipNo = i;
		   updateCoords = FALSE;
		   break;
		   }
		   // if the new clip contains an old clip
		   if (x1 <= clip->box.x1 && y1 <= clip->box.y1 &&
		   x2 >= clip->box.x2 && y2 >= clip->box.y2)
		   {
		   // update the old clip in this case
		   newClipNo = i;
		   break;
		   }
		   }
		   if (pset->firstClipWithNoTex > 0)
		   {
		   // Go through clip's of earlier textures to look for
		   // an identical clip. If such clips are found, their
		   // texture should be updated with the latest one
		   for (i=pset->firstClipWithNoTex-1; i>=0; i--)
		   {
		   Clip4Polygons *clip = &pset->clips[i];

		   // if tex matrices are different
		   if (clip->texMatrix.xx != matrix->xx ||
		   clip->texMatrix.yy != matrix->yy ||
		   clip->texMatrix.x0 != matrix->x0 ||
		   clip->texMatrix.y0 != matrix->y0)
		   continue;

		   // if an old clip == the new clip
		   if (x1 == clip->box.x1 && y1 == clip->box.y1 &&
		   x2 == clip->box.x2 && y2 == clip->box.y2)
		   {
		   newClipNo = i;
		   updateCoords = FALSE;
		   clip->textureNo = aw->nTextures;
		   break;
		   }
		   }
		   }

		   if (newClipNo == pset->nClips && // if no such old clip is found
		   !ensureLargerClipCapacity (pset))
		   {
		   fprintf(stderr, "%s: Not enough memory at line %d!\n",
		   __FILE__, __LINE__);
		   return;
		   }
		   Clip4Polygons *newClip = &pset->clips[newClipNo];
		   if (updateCoords)
		   {
		   memcpy (&newClip->box, pClip, sizeof (Box));
		   memcpy (&newClip->texMatrix, matrix, sizeof (CompMatrix));
		   // nMatrix is not used for now
		   // (i.e. only first texture matrix is considered)
		   }
		   if (newClipNo == pset->nClips) // if new clip is being added
		   {
		   //printf("adding x1: %4d, y1: %4d, x2: %4d, y2: %4d\n",
		   //       pClip->x1, pClip->y1, pClip->x2, pClip->y2);

		   newClip->textureNo = aw->nTextures;
		   pset->nClips++;
		   } */
	}
}

// For each rectangular clip, this function finds polygons which
// have a bounding box that intersects the clip. For intersecting
// polygons, it computes the texture coordinates for the vertices
// of that polygon (to draw the clip texture).
static Bool processIntersectingPolygons(PolygonSet * pset)
{
	int j;

	for (j = pset->firstNondrawnClip; j < pset->nClips; j++)
	{
		Clip4Polygons *c = pset->clips + j;
		Box *cb = &c->box;
		int nFrontVerticesTilThisPoly = 0;

		c->nIntersectingPolygons = 0;

		// TODO: If it doesn't affect speed much, for each clip,
		// consider doing 2 passes, counting the intersecting polygons
		// in the 1st pass and allocating just enough space for those
		// polygons instead of all polygons in the 2nd pass.
		int i;

		for (i = 0; i < pset->nPolygons; i++)
		{
			PolygonObject *p = pset->polygons + i;

			Box *bb = &p->boundingBox;

			if (bb->x2 <= cb->x1)
				continue;		// no intersection
			if (bb->y2 <= cb->y1)
				continue;		// no intersection
			if (bb->x1 >= cb->x2)
				continue;		// no intersection
			if (bb->y1 >= cb->y2)
				continue;		// no intersection

			// There is intersection, add clip info

			if (!c->intersectingPolygons)
			{
				c->intersectingPolygons = calloc(1, sizeof(int) *
												 pset->nPolygons);
			}
			// allocate tex coords
			// 2 {x, y} * 2 {front, back} * <total # of polygon front vertices>
			if (!c->polygonVertexTexCoords)
			{
				c->polygonVertexTexCoords =
						calloc(1, sizeof(GLfloat) * 2 * 2 *
							   pset->nTotalFrontVertices);
			}
			if (!c->intersectingPolygons || !c->polygonVertexTexCoords)
			{
				fprintf(stderr, "%s: Not enough memory at line %d!\n",
						__FILE__, __LINE__);
				freeClipsPolygons(pset);
				return FALSE;
			}
			c->intersectingPolygons[c->nIntersectingPolygons] = i;

			int k;

			for (k = 0; k < p->nSides; k++)
			{
				float x = p->vertices[3 * k] +
						  p->centerPosStart.x;
				float y = p->vertices[3 * k + 1] +
						  p->centerPosStart.y;
				GLfloat tx;
				GLfloat ty;
				if (c->texMatrix.xy != 0.0f || c->texMatrix.yx != 0.0f)
				{	// "non-rect" coordinates
					tx = COMP_TEX_COORD_XY(&c->texMatrix, x, y);
					ty = COMP_TEX_COORD_YX(&c->texMatrix, x, y);
				}
				else
				{
					tx = COMP_TEX_COORD_X(&c->texMatrix, x);
					ty = COMP_TEX_COORD_Y(&c->texMatrix, y);
				}
				// for front vertices
				int ti = 2 * (2 * nFrontVerticesTilThisPoly + k);

				c->polygonVertexTexCoords[ti] = tx;
				c->polygonVertexTexCoords[ti + 1] = ty;

				// for back vertices
				ti = 2 * (2 * nFrontVerticesTilThisPoly +
						  (2 * p->nSides - 1 - k));
				c->polygonVertexTexCoords[ti] = tx;
				c->polygonVertexTexCoords[ti + 1] = ty;
			}
			c->nIntersectingPolygons++;
			nFrontVerticesTilThisPoly += p->nSides;
		}
	}

	return TRUE;
}

/*
static Bool computePolygonShadows(PolygonSet * pset)
{
	   PolygonObject *p = pset->polygons;
	   int i;
	   for (i=0; i<pset->nPolygons; i++, p++)
	   {
	   p->nShadowQuads = 0;

	   // If an edge polygon, determine shadow quads
	   // First count them
	   if (x == 0)
	   {
	   p->nShadowQuads++;
	   if (y == 0)
	   p->nShadowQuads++;
	   if (y == gridSizeY - 1)
	   p->nShadowQuads++;
	   }
	   if (x == gridSizeX - 1)
	   {
	   p->nShadowQuads++;
	   if (y == 0)
	   p->nShadowQuads++;
	   if (y == gridSizeY - 1)
	   p->nShadowQuads++;
	   }
	   if (y == 0)
	   p->nShadowQuads++;
	   if (y == gridSizeY - 1)
	   p->nShadowQuads++;

	   // Allocate them
	   p->shadowVertices = calloc (1, sizeof (GLfloat) * 3 *
	   4 * p->nShadowQuads);
	   if (!p->shadowVertices)
	   {
	   fprintf(stderr, "%s: Not enough memory at line %d!\n",
	   __FILE__, __LINE__);
	   freePolygonObjects (pset);
	   return FALSE;
	   }
	   // Store them
	   int n = 0;
	   GLfloat *v = p->shadowVertices;
	   if (x == 0)
	   {
	   // Left quad
	   v[12 * n]     = w->attrib.x - w->output.left;
	   v[12 * n + 1] = w->attrib.x - w->output.left;
	   v[12 * n + 2] = w->attrib.x - w->output.left;
	   n++;
	   if (y == 0)
	   n++;
	   if (y == gridSizeY - 1)
	   n++;
	   }
	   if (x == gridSizeX - 1)
	   {
	   n++;
	   if (y == 0)
	   n++;
	   if (y == gridSizeY - 1)
	   n++;
	   }
	   if (y == 0)
	   n++;
	   if (y == gridSizeY - 1)
	   n++;

	   p->nShadowQuads = n;
	   }
	return TRUE;
}
*/

static void polygonsDrawCustomGeometry(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);

	if (						// only draw windows on current viewport
		   w->attrib.x > s->width || w->attrib.x + w->width < 0 ||
		   w->attrib.y > s->height || w->attrib.y + w->height < 0 ||
		   (aw->lastKnownCoords.x != NOT_INITIALIZED &&
			(aw->lastKnownCoords.x != w->attrib.x ||
			 aw->lastKnownCoords.y != w->attrib.y)))
	{
		return;
		// since this is not the viewport the window was drawn
		// just before animation started
	}
	PolygonSet *pset = aw->polygonSet;

	// if polygon set is not valid or effect is not 3D (glide w/thickness=0)
	if (!pset)
		return;

	// TODO: Fix the source of the crash problem
	// (uninitialized lastClipInGroup)
	// instead of doing this uninitialized value check
	if (pset->firstNondrawnClip < 0 ||
		pset->firstNondrawnClip > pset->nClips ||
		(!aw->clipsUpdated &&
		 (pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] < 0 ||
		  pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] >= pset->nClips)))
	{
		return;
	}

	if (aw->clipsUpdated && aw->nDrawGeometryCalls > 0)
	{
		if (!processIntersectingPolygons(pset))
		{
			return;
		}
	}

	int lastClip;				// last clip to draw

	if (aw->clipsUpdated)
	{
		lastClip = pset->nClips - 1;
	}
	else
	{
		lastClip = pset->lastClipInGroup[aw->nDrawGeometryCalls - 1];
	}

	float forwardProgress = defaultAnimProgress(aw);

	// OpenGL stuff starts here

	if (pset->doLighting)
	{
		glPushAttrib(GL_LIGHT0);
		glPushAttrib(GL_COLOR_MATERIAL);
		glPushAttrib(GL_LIGHTING);
		glEnable(GL_COLOR_MATERIAL);
		glEnable(GL_LIGHTING);

		GLfloat ambientLight[] = { 0.3f, 0.3f, 0.3f, 0.3f };
		GLfloat diffuseLight[] = { 0.9f, 0.9f, 0.9f, 0.9f };
		GLfloat light0Position[] = { -0.5f, 0.5f, -9.0f, 0.0f };

		glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight);
		glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseLight);
		glLightfv(GL_LIGHT0, GL_POSITION, light0Position);
	}
	glPushMatrix();

	// Store old blend values
	GLint blendSrcWas, blendDstWas;

	glGetIntegerv(GL_BLEND_SRC, &blendSrcWas);
	glGetIntegerv(GL_BLEND_DST, &blendDstWas);
	GLboolean blendWas = glIsEnabled(GL_BLEND);

	glPushAttrib(GL_STENCIL_BUFFER_BIT);
	glDisable(GL_STENCIL_TEST);

	//GLboolean normalArrayWas = glIsEnabled(GL_NORMAL_ARRAY);
	//glShadeModel(GL_FLAT);

	if (pset->doDepthTest)
	{
		// Depth test
		glPushAttrib(GL_DEPTH_FUNC);
		glPushAttrib(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glEnable(GL_DEPTH_TEST);
	}

	// Clip planes
	GLdouble clipPlane0[] = { 1, 0, 0, 0 };
	GLdouble clipPlane1[] = { 0, 1, 0, 0 };
	GLdouble clipPlane2[] = { -1, 0, 0, 0 };
	GLdouble clipPlane3[] = { 0, -1, 0, 0 };

	// Save old color values
	GLfloat oldColor[4];

	glGetFloatv(GL_CURRENT_COLOR, oldColor);


	// Determine where we are called from in paint.c's drawWindowTexture
	// to find out how we should change the opacity
	GLint prevActiveTexture = GL_TEXTURE0_ARB;
	Bool saturationFull = TRUE;

	if (w->screen->canDoSaturated && aw->curPaintAttrib.saturation != COLOR)
	{
		saturationFull = FALSE;
		if (w->screen->canDoSlightlySaturated &&
			aw->curPaintAttrib.saturation > 0)
		{
			if (aw->curPaintAttrib.opacity < OPAQUE ||
				aw->curPaintAttrib.brightness != BRIGHT)
				prevActiveTexture = GL_TEXTURE3_ARB;
			else
				prevActiveTexture = GL_TEXTURE2_ARB;
		}
		else
			prevActiveTexture = GL_TEXTURE1_ARB;
	}

	float brightness = aw->curPaintAttrib.brightness / 65535.0;
	float opacity = aw->curPaintAttrib.opacity / 65535.0;

	float newOpacity = opacity;
	float fadePassedBy;

	if (!blendWas)				// if translucency is not already turned on in paint.c
	{
		glEnable(GL_BLEND);
	}
	if (saturationFull)
	{
		screenTexEnvMode(w->screen, GL_MODULATE);
	}
	else if (prevActiveTexture == GL_TEXTURE2_ARB)
	{
		w->screen->activeTexture(prevActiveTexture + 1);
		enableTexture(w->screen, aw->curTexture, aw->curTextureFilter);
	}
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// if fade-out duration is not specified per polygon
	if (pset->allFadeDuration > -1.0f)
	{
		fadePassedBy = forwardProgress - (1 - pset->allFadeDuration);

		// if "fade out starting point" is passed
		if (fadePassedBy > 1e-5)	// if true, allFadeDuration should be > 0
		{
			float opacityFac;

			if (aw->deceleratingMotion)
				opacityFac = 1 - decelerateProgress2
						(fadePassedBy / pset->allFadeDuration);
			else
				opacityFac = 1 - fadePassedBy / pset->allFadeDuration;
			if (opacityFac < 0)
				opacityFac = 0;
			if (opacityFac > 1)
				opacityFac = 1;
			newOpacity = opacity * opacityFac;
		}
	}

	int pass;
	// 0: draw opaque ones
	// 2: draw transparent ones
	for (pass = 0; pass < 2; pass++)
	{
		int j;

		for (j = pset->firstNondrawnClip; j <= lastClip; j++)
		{
			Clip4Polygons *c = pset->clips + j;
			int nFrontVerticesTilThisPoly = 0;
			int nNewSides = 0;
			int i;

			for (i = 0; i < c->nIntersectingPolygons;
				 i++, nFrontVerticesTilThisPoly += nNewSides)
			{
				PolygonObject *p =
						pset->polygons + c->intersectingPolygons[i];
				nNewSides = p->nSides;

				float newOpacityPolygon = newOpacity;

				// if fade-out duration is specified per polygon
				if (pset->allFadeDuration == -1.0f)
				{
					fadePassedBy = forwardProgress - p->fadeStartTime;
					// if "fade out starting point" is passed
					if (fadePassedBy > 1e-5)	// if true, then allFadeDuration > 0
					{
						float opacityFac;

						if (aw->deceleratingMotion)
							opacityFac = 1 - decelerateProgress2
									(fadePassedBy / p->fadeDuration);
						else
							opacityFac = 1 - fadePassedBy / p->fadeDuration;
						if (opacityFac < 0)
							opacityFac = 0;
						if (opacityFac > 1)
							opacityFac = 1;
						newOpacityPolygon = newOpacity * opacityFac;
					}
				}

				if (newOpacityPolygon < 1e-5)	// if polygon object is invisible
					continue;

				if (pass == 0)
				{
					if (newOpacityPolygon < 0.9999)	// if not fully opaque
						continue;	// draw only opaque ones in pass 0
				}
				else if (newOpacityPolygon > 0.9999)	// if fully opaque
					continue;	// draw only non-opaque ones in pass 1

				glPushMatrix();

				if (pset->correctPerspective)
				{
					// Correct perspective appearance by skewing

					GLfloat skewx = -((p->centerPos.x - s->width / 2) * 1.15);
					GLfloat skewy = -((p->centerPos.y - s->height / 2) * 1.15);

					// column-major order
					GLfloat skewMat[16] =
							{1,0,0,0,
							 0,1,0,0,
							 skewx,skewy,1,0,
							 0,0,0,1};
					glMultMatrixf( skewMat);
				}

				// Center
				glTranslatef(p->centerPos.x, p->centerPos.y, p->centerPos.z);

				// Scale z first
				glScalef(1.0f, 1.0f, 1.0f / s->width);

				// Move by "rotation axis offset"
				glTranslatef(p->rotAxisOffset.x, p->rotAxisOffset.y,
							 p->rotAxisOffset.z);

				// Rotate by desired angle
				glRotatef(p->rotAngle, p->rotAxis.x, p->rotAxis.y,
						  p->rotAxis.z);

				// Move back to center
				glTranslatef(-p->rotAxisOffset.x, -p->rotAxisOffset.y,
							 -p->rotAxisOffset.z);

				// Scale back
				glScalef(1.0f, 1.0f, s->width);


				clipPlane0[3] = -(c->boxf.x1 - p->centerPosStart.x);
				clipPlane1[3] = -(c->boxf.y1 - p->centerPosStart.y);
				clipPlane2[3] = (c->boxf.x2 - p->centerPosStart.x);
				clipPlane3[3] = (c->boxf.y2 - p->centerPosStart.y);
				glClipPlane(GL_CLIP_PLANE0, clipPlane0);
				glClipPlane(GL_CLIP_PLANE1, clipPlane1);
				glClipPlane(GL_CLIP_PLANE2, clipPlane2);
				glClipPlane(GL_CLIP_PLANE3, clipPlane3);

				int k;

				for (k = 0; k < 4; k++)
					glEnable(GL_CLIP_PLANE0 + k);
				Bool fadeBackAndSides =
						pset->backAndSidesFadeDur > 0 &&
						forwardProgress <= pset->backAndSidesFadeDur;

				float newOpacityPolygon2 = newOpacityPolygon;

				if (fadeBackAndSides)
				{
					// Fade-in opacity for back face and sides
					newOpacityPolygon2 *=
							(forwardProgress / pset->backAndSidesFadeDur);
				}

				if (saturationFull)
					glColor4f(brightness, brightness, brightness,
							  newOpacityPolygon2);
				else if (prevActiveTexture == GL_TEXTURE1_ARB)
				{
					// From paint.c

					GLfloat constant2[4] =
							{ 0.5f +
						0.5f * RED_SATURATION_WEIGHT * brightness,
						0.5f + 0.5f * GREEN_SATURATION_WEIGHT * brightness,
						0.5f + 0.5f * BLUE_SATURATION_WEIGHT * brightness,
						newOpacityPolygon2
					};

					glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR,
							   constant2);
				}
				else			//if (prevActiveTexture >= GL_TEXTURE2_ARB)
				{
					GLfloat constant2[4] = { brightness,
						brightness,
						brightness,
						newOpacityPolygon2
					};

					// From paint.c

					glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR,
							   constant2);

					glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
							  GL_COMBINE);

					glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

					glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
					glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_CONSTANT);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA,
							  GL_SRC_ALPHA);
					glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA,
							  GL_SRC_ALPHA);
				}

				//glEnableClientState(GL_NORMAL_ARRAY);
				//glEnable(GL_NORMALIZE);

				// Draw back face
				glVertexPointer(3, GL_FLOAT, 0, p->vertices + 3 * p->nSides);
				//glNormalPointer(GL_FLOAT, 0,
				//              p->normals + 3 * p->nSides);
				glTexCoordPointer(2, GL_FLOAT, 0,
								  c->polygonVertexTexCoords +
								  2 * (2 * nFrontVerticesTilThisPoly +
									   p->nSides));

				glNormal3f(p->normals[3], p->normals[4], p->normals[5]);
				glDrawArrays(GL_POLYGON, 0, p->nSides);

				// Front vertex coords
				glVertexPointer(3, GL_FLOAT, 0, p->vertices);
				//glNormalPointer(GL_FLOAT, 0, p->normals);
				glTexCoordPointer(2, GL_FLOAT, 0,
								  c->polygonVertexTexCoords +
								  2 * 2 * nFrontVerticesTilThisPoly);

				//if (pset->thickness > 1e-5)
				{
					// TODO: Surface normals for sides
					// Draw quad strip for sides
					if (TRUE)
					{
						// Do each quad separately to be able to specify
						// different normals
						for (k = 0; k < p->nSides; k++)
						{
							/*
							   int k2 = (k + 2) * 3;
							   glNormal3f(p->normals[k2 + 0],
							   p->normals[k2 + 1],
							   p->normals[k2 + 2]); */
							glNormal3f(p->normals[0],	// front face normal for now
									   p->normals[1], p->normals[2]);
							glDrawElements(GL_QUADS, 4,
										   GL_UNSIGNED_SHORT,
										   p->sideIndices + k * 4);
						}
					}
					else			// no need for separate quad rendering
						glDrawElements(GL_QUAD_STRIP, 2 * (p->nSides + 1),
									   GL_UNSIGNED_SHORT, p->sideIndices);
				}
				if (fadeBackAndSides)
					// if opacity was changed just above
				{
					// Go back to normal opacity for front face

					if (saturationFull)
						glColor4f(brightness, brightness, brightness,
								  newOpacityPolygon);
					else if (prevActiveTexture == GL_TEXTURE1_ARB)
					{
						GLfloat constant[4] =
								{ 0.5f +
							0.5f * RED_SATURATION_WEIGHT * brightness,
							0.5f + 0.5f * GREEN_SATURATION_WEIGHT *
									brightness,
							0.5f + 0.5f * BLUE_SATURATION_WEIGHT * brightness,
							newOpacityPolygon
						};

						glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR,
								   constant);
					}
					else
					{
						GLfloat constant[4] = { brightness,
							brightness,
							brightness,
							newOpacityPolygon
						};
						glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR,
								   constant);
					}
				}
				// Draw front face

				glNormal3f(p->normals[0], p->normals[1], p->normals[2]);
				glDrawArrays(GL_POLYGON, 0, p->nSides);

				for (k = 0; k < 4; k++)
					glDisable(GL_CLIP_PLANE0 + k);

				glPopMatrix();
			}
		}
	}
	// Restore
	// -----------------------------------------

	// Restore old color values
	glColor4f(oldColor[0], oldColor[1], oldColor[2], oldColor[3]);
	//screenTexEnvMode(w->screen, GL_REPLACE);

	glPopAttrib();
	if (pset->doDepthTest)
	{
		glPopAttrib();
		glPopAttrib();
	}

	//if (!normalArrayWas)
	//  glDisableClientState(GL_NORMAL_ARRAY);

	// Restore texture stuff
	if (saturationFull)
		screenTexEnvMode(w->screen, GL_REPLACE);
	else if (prevActiveTexture == GL_TEXTURE2_ARB)
	{
		disableTexture(w->screen, aw->curTexture);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		w->screen->activeTexture(prevActiveTexture);
	}
	// Restore blend values
	if (!blendWas)
		glDisable(GL_BLEND);
	glBlendFunc(blendSrcWas, blendDstWas);

	glPopMatrix();

	if (pset->doLighting)		// && !s->lighting)
	{
		glPopAttrib();
		glPopAttrib();
		glPopAttrib();
	}
	if (aw->clipsUpdated)		// set end mark for this group of clips
		pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] = lastClip;

	// Next time, start drawing from next group of clips
	pset->firstNondrawnClip =
			pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] + 1;
}

static void polygonsPrePaintWindow(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	if (aw->polygonSet)
		aw->polygonSet->firstNondrawnClip = 0;
}

static void polygonsPostPaintWindow(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	if (aw->clipsUpdated &&	// clips should be dropped only in the 1st step
		aw->polygonSet && aw->nDrawGeometryCalls == 0)	// if clips not drawn
	{
		// drop these unneeded clips (e.g. ones passed by blurfx)
		aw->polygonSet->nClips = aw->polygonSet->firstNondrawnClip;
	}
}

static void fxExplode3DInit(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	switch (as->opt[ANIM_SCREEN_OPTION_EXPLODE3D_TESS].value.i)
	{
	case PolygonTessRect:
		if (!tessellateIntoRectangles(w, 
			as->opt[ANIM_SCREEN_OPTION_EXPLODE3D_GRIDSIZE_X].value.i,
			as->opt[ANIM_SCREEN_OPTION_EXPLODE3D_GRIDSIZE_Y].value.i,
			as->opt[ANIM_SCREEN_OPTION_EXPLODE3D_THICKNESS].value.f))
			return;
	case PolygonTessHex:
		if (!tessellateIntoHexagons(w, 
			as->opt[ANIM_SCREEN_OPTION_EXPLODE3D_GRIDSIZE_X].value.i,
			as->opt[ANIM_SCREEN_OPTION_EXPLODE3D_GRIDSIZE_Y].value.i,
			as->opt[ANIM_SCREEN_OPTION_EXPLODE3D_THICKNESS].value.f))
			return;
	default:
		return;
	}

	PolygonSet *pset = aw->polygonSet;
	PolygonObject *p = pset->polygons;
	double sqrt2 = sqrt(2);

	int i;

	for (i = 0; i < pset->nPolygons; i++, p++)
	{
		p->rotAxis.x = RAND_FLOAT();
		p->rotAxis.y = RAND_FLOAT();
		p->rotAxis.z = RAND_FLOAT();

		float screenSizeFactor = (0.8 * DEFAULT_Z_CAMERA * s->width);
		float speed = screenSizeFactor / 10 * (0.2 + RAND_FLOAT());

		float xx = 2 * (p->centerRelPos.x - 0.5);
		float yy = 2 * (p->centerRelPos.y - 0.5);

		float x = speed * 2 * (xx + 0.5 * (RAND_FLOAT() - 0.5));
		float y = speed * 2 * (yy + 0.5 * (RAND_FLOAT() - 0.5));

		float distToCenter = sqrt(xx * xx + yy * yy) / sqrt2;
		float moveMult = 1 - distToCenter;
		moveMult = moveMult < 0 ? 0 : moveMult;
		float zbias = 0.1;
		float z = speed * 10 *
			(zbias + RAND_FLOAT() *
			 pow(moveMult, 0.5));

		p->finalRelPos.x = x;
		p->finalRelPos.y = y;
		p->finalRelPos.z = z;
		p->finalRotAng = RAND_FLOAT() * 540 - 270;
	}
	pset->allFadeDuration = 0.3f;
	pset->doDepthTest = TRUE;
	pset->doLighting = TRUE;
	pset->correctPerspective = TRUE;
	pset->backAndSidesFadeDur = 0.2f;
}

static void fxLeafSpread3DInit(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);

	if (!tessellateIntoRectangles(w, 20, 14, 15.0f))
		return;

	PolygonSet *pset = aw->polygonSet;
	PolygonObject *p = pset->polygons;
	float fadeDuration = 0.26;
	float life = 0.4;
	float spreadFac = 3.5;
	float randYMax = 0.07;
	float winFacX = WIN_W(w) / 800.0;
	float winFacY = WIN_H(w) / 800.0;
	float winFacZ = (WIN_H(w) + WIN_W(w)) / 2.0 / 800.0;

	int i;

	for (i = 0; i < pset->nPolygons; i++, p++)
	{
		p->rotAxis.x = RAND_FLOAT();
		p->rotAxis.y = RAND_FLOAT();
		p->rotAxis.z = RAND_FLOAT();

		float screenSizeFactor = (0.8 * DEFAULT_Z_CAMERA * s->width);
		float speed = screenSizeFactor / 10 * (0.2 + RAND_FLOAT());

		float xx = 2 * (p->centerRelPos.x - 0.5);
		float yy = 2 * (p->centerRelPos.y - 0.5);

		float x =
				speed * winFacX * spreadFac * (xx +
											   0.5 * (RAND_FLOAT() - 0.5));
		float y =
				speed * winFacY * spreadFac * (yy +
											   0.5 * (RAND_FLOAT() - 0.5));
		float z = speed * winFacZ * 7 * ((RAND_FLOAT() - 0.5) / 0.5);

		p->finalRelPos.x = x;
		p->finalRelPos.y = y;
		p->finalRelPos.z = z;

		p->moveStartTime =
				p->centerRelPos.y * (1 - fadeDuration - randYMax) +
				randYMax * RAND_FLOAT();
		p->moveDuration = 1;

		p->fadeStartTime = p->moveStartTime + life;
		if (p->fadeStartTime > 1 - fadeDuration)
			p->fadeStartTime = 1 - fadeDuration;
		p->fadeDuration = fadeDuration;

		p->finalRotAng = 150;
	}
	pset->doDepthTest = TRUE;
	pset->doLighting = TRUE;
	pset->correctPerspective = TRUE;
}

static void fxDomino3DInit(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	// Sub effects:
	// 1: Domino
	// 2: Razr
	int subEffectNo = aw->subEffectNo;

	int fallDir;

	if (subEffectNo == 2)
		fallDir = getAnimationDirection
				(w, &as->opt[ANIM_SCREEN_OPTION_RAZR_DIRECTION].value, TRUE);
	else
		fallDir = getAnimationDirection
				(w, &as->opt[ANIM_SCREEN_OPTION_DOMINO_DIRECTION].value,
				 TRUE);

	int defaultGridSize = 20;
	float minCellSize = 30;
	int gridSizeX;
	int gridSizeY;
	int fallDirGridSize;
	float minDistStartEdge;		// half piece size in [0,1] range
	float gridCellW;
	float gridCellH;
	float cellAspectRatio = 1.25;

	if (subEffectNo == 2)
		cellAspectRatio = 1;

	// Determine sensible domino piece sizes
	if (fallDir == AnimDirectionDown || fallDir == AnimDirectionUp)
	{
		if (minCellSize > BORDER_W(w))
			minCellSize = BORDER_W(w);
		if (BORDER_W(w) / defaultGridSize < minCellSize)
			gridSizeX = (int)(BORDER_W(w) / minCellSize);
		else
			gridSizeX = defaultGridSize;
		gridCellW = BORDER_W(w) / gridSizeX;
		gridSizeY = (int)(BORDER_H(w) / (gridCellW * cellAspectRatio));
		if (gridSizeY == 0)
			gridSizeY = 1;
		gridCellH = BORDER_H(w) / gridSizeY;
		fallDirGridSize = gridSizeY;
	}
	else
	{
		if (minCellSize > BORDER_H(w))
			minCellSize = BORDER_H(w);
		if (BORDER_H(w) / defaultGridSize < minCellSize)
			gridSizeY = (int)(BORDER_H(w) / minCellSize);
		else
			gridSizeY = defaultGridSize;
		gridCellH = BORDER_H(w) / gridSizeY;
		gridSizeX = (int)(BORDER_W(w) / (gridCellH * cellAspectRatio));
		if (gridSizeX == 0)
			gridSizeX = 1;
		gridCellW = BORDER_W(w) / gridSizeX;
		fallDirGridSize = gridSizeX;
	}
	minDistStartEdge = (1.0 / fallDirGridSize) / 2;

	float thickness = MIN(gridCellW, gridCellH) / 3.5;

	if (!tessellateIntoRectangles(w, gridSizeX, gridSizeY, thickness))
		return;

	float rotAxisX = 0;
	float rotAxisY = 0;
	Point3d rotAxisOff = { 0, 0, thickness / 2 };
	float posX = 0;				// position of standing piece
	float posY = 0;
	float posZ = 0;
	int nFallingColumns = gridSizeX;
	float gridCellHalfW = gridCellW / 2;
	float gridCellHalfH = gridCellH / 2;

	switch (fallDir)
	{
	case AnimDirectionDown:
		rotAxisX = -1;
		if (subEffectNo == 2)
			rotAxisOff.y = -gridCellHalfH;
		else
		{
			posY = -(gridCellHalfH + thickness);
			posZ = gridCellHalfH - thickness / 2;
		}
		break;
	case AnimDirectionLeft:
		rotAxisY = -1;
		if (subEffectNo == 2)
			rotAxisOff.x = gridCellHalfW;
		else
		{
			posX = gridCellHalfW + thickness;
			posZ = gridCellHalfW - thickness / 2;
		}
		nFallingColumns = gridSizeY;
		break;
	case AnimDirectionUp:
		rotAxisX = 1;
		if (subEffectNo == 2)
			rotAxisOff.y = gridCellHalfH;
		else
		{
			posY = gridCellHalfH + thickness;
			posZ = gridCellHalfH - thickness / 2;
		}
		break;
	case AnimDirectionRight:
		rotAxisY = 1;
		if (subEffectNo == 2)
			rotAxisOff.x = -gridCellHalfW;
		else
		{
			posX = -(gridCellHalfW + thickness);
			posZ = gridCellHalfW - thickness / 2;
		}
		nFallingColumns = gridSizeY;
		break;
	}

	float fadeDuration;
	float riseDuration;
	float riseTimeRandMax = 0.2;

	if (subEffectNo == 2)
	{
		riseDuration = (1 - riseTimeRandMax) / fallDirGridSize;
		fadeDuration = riseDuration / 2;
	}
	else
	{
		fadeDuration = 0.18;
		riseDuration = 0.2;
	}
	float *riseTimeRandSeed = calloc(1, nFallingColumns * sizeof(float));

	if (!riseTimeRandSeed)
		// TODO: log error here
		return;
	int i;

	for (i = 0; i < nFallingColumns; i++)
		riseTimeRandSeed[i] = RAND_FLOAT();

	PolygonSet *pset = aw->polygonSet;
	PolygonObject *p = pset->polygons;

	for (i = 0; i < pset->nPolygons; i++, p++)
	{
		p->rotAxis.x = rotAxisX;
		p->rotAxis.y = rotAxisY;
		p->rotAxis.z = 0;

		p->finalRelPos.x = posX;
		p->finalRelPos.y = posY;
		p->finalRelPos.z = posZ;

		// dist. from rise-start / fall-end edge in window ([0,1] range)
		float distStartEdge = 0;

		// dist. from edge perpendicular to movement (for move start time)
		// so that same the blocks in same row/col. appear to knock down
		// the next one
		float distPerpEdge = 0;

		switch (fallDir)
		{
		case AnimDirectionUp:
			distStartEdge = p->centerRelPos.y;
			distPerpEdge = p->centerRelPos.x;
			break;
		case AnimDirectionRight:
			distStartEdge = 1 - p->centerRelPos.x;
			distPerpEdge = p->centerRelPos.y;
			break;
		case AnimDirectionDown:
			distStartEdge = 1 - p->centerRelPos.y;
			distPerpEdge = p->centerRelPos.x;
			break;
		case AnimDirectionLeft:
			distStartEdge = p->centerRelPos.x;
			distPerpEdge = p->centerRelPos.y;
			break;
		}

		float riseTimeRand =
				riseTimeRandSeed[(int)(distPerpEdge * nFallingColumns)] *
				riseTimeRandMax;

		p->moveDuration = riseDuration;

		float mult = 1;
		if (fallDirGridSize > 1)
			mult = ((distStartEdge - minDistStartEdge) /
					(1 - 2 * minDistStartEdge));
		if (subEffectNo == 2)
		{
			p->moveStartTime =
					mult *
					(1 - riseDuration - riseTimeRandMax) + riseTimeRand;
			p->fadeStartTime = p->moveStartTime + riseDuration / 2;
			p->finalRotAng = -180;

			p->rotAxisOffset.x = rotAxisOff.x;
			p->rotAxisOffset.y = rotAxisOff.y;
			p->rotAxisOffset.z = rotAxisOff.z;
		}
		else
		{
			p->moveStartTime =
					mult *
					(1 - riseDuration - riseTimeRandMax) +
					riseTimeRand;
			p->fadeStartTime =
					p->moveStartTime + riseDuration - riseTimeRand + 0.03;
			p->finalRotAng = -90;
		}
		if (p->fadeStartTime > 1 - fadeDuration)
			p->fadeStartTime = 1 - fadeDuration;
		p->fadeDuration = fadeDuration;
	}
	free(riseTimeRandSeed);
	pset->doDepthTest = TRUE;
	pset->doLighting = TRUE;
	pset->correctPerspective = TRUE;
}

/*
static void fxTornado3DInit(CompScreen * s, CompWindow * w)
{
	//ANIM_WINDOW(w);

	if (!tessellateIntoRectangles(w, 20, 14, 15.0f))
		return;
}
*/

static void fxGlideInit(CompScreen * s, CompWindow * w)
{
	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	float finalDistFac;
	float finalRotAng;
	float thickness;

	fxGlideGetParams(as, aw, &finalDistFac, &finalRotAng, &thickness);

	if (thickness < 1e-5) // if thicknes is 0, we'll make the effect 2D
	{
		// store window opacity
		aw->storedOpacity = w->paint.opacity;
		aw->timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo
						as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

		return; // we're done with 2D initialization
	}

	// for 3D glide effect
	// ------------------------

	PolygonSet *pset = aw->polygonSet;

	pset->includeShadows = (thickness < 1e-5);

	if (!tessellateIntoRectangles(w, 1, 1, thickness))
		return;

	PolygonObject *p = pset->polygons;

	int i;

	for (i = 0; i < pset->nPolygons; i++, p++)
	{
		p->rotAxis.x = 1;
		p->rotAxis.y = 0;
		p->rotAxis.z = 0;

		p->finalRelPos.x = 0;
		p->finalRelPos.y = 0;
		p->finalRelPos.z = finalDistFac * 0.8 * DEFAULT_Z_CAMERA * s->width;

		p->finalRotAng = finalRotAng;
	}
	pset->allFadeDuration = 1.0f;
	pset->backAndSidesFadeDur = 0.2f;
	pset->doLighting = TRUE;
	pset->correctPerspective = TRUE;
}

// Computes polygon's new position and orientation
// with linear movement
static void
polygonsLinearAnimStepPolygon(CompWindow * w,
							  PolygonObject * p, float forwardProgress)
{
	float moveProgress = forwardProgress - p->moveStartTime;

	if (p->moveDuration > 0)
		moveProgress /= p->moveDuration;
	if (moveProgress < 0)
		moveProgress = 0;
	else if (moveProgress > 1)
		moveProgress = 1;

	p->centerPos.x = moveProgress * p->finalRelPos.x + p->centerPosStart.x;
	p->centerPos.y = moveProgress * p->finalRelPos.y + p->centerPosStart.y;
	p->centerPos.z = 1.0f / w->screen->width *
			moveProgress * p->finalRelPos.z + p->centerPosStart.z;

	p->rotAngle = moveProgress * p->finalRotAng + p->rotAngleStart;
}

// Similar to polygonsLinearAnimStepPolygon,
// but slightly ac/decelerates movement
static void
polygonsDeceleratingAnimStepPolygon(CompWindow * w,
									PolygonObject * p, float forwardProgress)
{
	float moveProgress = forwardProgress - p->moveStartTime;

	if (p->moveDuration > 0)
		moveProgress /= p->moveDuration;
	if (moveProgress < 0)
		moveProgress = 0;
	else if (moveProgress > 1)
		moveProgress = 1;

	moveProgress = decelerateProgress2(moveProgress);

	p->centerPos.x = moveProgress * p->finalRelPos.x + p->centerPosStart.x;
	p->centerPos.y = moveProgress * p->finalRelPos.y + p->centerPosStart.y;
	p->centerPos.z = 1.0f / w->screen->width *
			moveProgress * p->finalRelPos.z + p->centerPosStart.z;

	p->rotAngle = moveProgress * p->finalRotAng + p->rotAngleStart;
}

// =====================  End of Effects  =========================


AnimEffectProperties animEffectProperties[AnimEffectNum] = {
	// AnimEffectNone
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	// AnimEffectRandom
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	// AnimEffectBeamUp
	{fxBeamupUpdateWindowAttrib, 0, drawParticleSystems, fxBeamUpModelStep,
	 fxBeamUpInit, 0, 0, 0, 1, 0, 0, 0},
	// AnimEffectBurn
	{0, 0, drawParticleSystems, fxBurnModelStep, fxBurnInit, 0, 0, 0, 1, 0, 0, 0},
	// AnimEffectCurvedFold
	{0, 0, 0, fxCurvedFoldModelStep, 0, fxMagicLampInitGrid, 0, 0, 0, 0, 0, 0},
	// AnimEffectDomino3D
	{0, polygonsPrePaintWindow, polygonsPostPaintWindow, polygonsAnimStep,
	 fxDomino3DInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsLinearAnimStepPolygon, 1, 0},
	// AnimEffectDream
	{fxDreamUpdateWindowAttrib, 0, 0, fxDreamModelStep, fxDreamInit,
	 fxMagicLampInitGrid, 0, 0, 0, 0, 0, 0},
	// AnimEffectExplode3D
	{0, polygonsPrePaintWindow, polygonsPostPaintWindow, polygonsAnimStep,
	 fxExplode3DInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsLinearAnimStepPolygon, 0, 0},
	// AnimEffectFade
	{fxFadeUpdateWindowAttrib, 0, 0, defaultAnimStep, fxFadeInit, 0, 0, 0, 0,
	 0, 0, TRUE},
	// AnimEffectFocusFade
	{fxFocusFadeUpdateWindowAttrib, 0, 0, defaultAnimStep, fxFadeInit, 0, 0, 0, 0,
	 0, 0, TRUE},
	// AnimEffectGlide3D1
	{fxGlideUpdateWindowAttrib, polygonsPrePaintWindow,
	 polygonsPostPaintWindow, fxGlideAnimStep,
	 fxGlideInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsDeceleratingAnimStepPolygon, 1, 0},
	// AnimEffectGlide3D2
	{fxGlideUpdateWindowAttrib, polygonsPrePaintWindow,
	 polygonsPostPaintWindow, fxGlideAnimStep,
	 fxGlideInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsDeceleratingAnimStepPolygon, 2, 0},
	// AnimEffectHorizontalFolds
	{0, 0, 0, fxHorizontalFoldsModelStep, 0, fxHorizontalFoldsInitGrid,
	 0, 0, 0, 0, 0, 0},
	// AnimEffectLeafSpread3D
	{0, polygonsPrePaintWindow, polygonsPostPaintWindow, polygonsAnimStep,
	 fxLeafSpread3DInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsLinearAnimStepPolygon, 0, 0},
	// AnimEffectMagicLamp
	{0, 0, 0, fxMagicLampModelStep, fxMagicLampInit, fxMagicLampInitGrid,
	 0, 0, 0, 0, 0, 0},
	// AnimEffectMagicLampVacuum
	{0, 0, 0, fxMagicLampModelStep, fxMagicLampInit,
	 fxMagicLampVacuumInitGrid, 0, 0, 0, 0, 0, 0},
	// AnimEffectRazr3D
	{0, polygonsPrePaintWindow, polygonsPostPaintWindow, polygonsAnimStep,
	 fxDomino3DInit, 0, polygonsStoreClips, polygonsDrawCustomGeometry, 0,
	 polygonsLinearAnimStepPolygon, 2, 0},
	// AnimEffectRollUp
	{0, 0, 0, fxRollUpModelStep, 0, fxRollUpInitGrid, 0, 0, 1, 0, 0, 0},
	// AnimEffectSidekick
	{fxZoomUpdateWindowAttrib, 0, 0, fxZoomModelStep, fxSidekickInit,
	 0, 0, 0, 1, 0, 0, 0},
	// AnimEffectWave
	{0, 0, 0, fxWaveModelStep, 0, fxMagicLampInitGrid, 0, 0, 0, 0, 0, 0},
	// AnimEffectZoom
	{fxZoomUpdateWindowAttrib, 0, 0, fxZoomModelStep, fxZoomInit, 0, 0, 0, 1,
	 0, 0, 0}
};


static Bool getMousePointerXY(CompScreen * s, short *x, short *y)
{
	Window w1, w2;
	int xp, yp, xj, yj;
	unsigned int m;

	if (XQueryPointer
		(s->display->display, s->root, &w1, &w2, &xj, &yj, &xp, &yp, &m))
	{
		*x = xp;
		*y = yp;
		return TRUE;
	}
	return FALSE;
}

static int animGetWindowState(CompWindow * w)
{
	Atom actual;
	int result, format;
	unsigned long n, left;
	unsigned char *data;

	result = XGetWindowProperty(w->screen->display->display, w->id,
								w->screen->display->wmStateAtom, 0L,
								1L, FALSE,
								w->screen->display->wmStateAtom,
								&actual, &format, &n, &left, &data);

	if (result == Success && n && data)
	{
		int state;

		memcpy(&state, data, sizeof(int));
		XFree((void *)data);

		return state;
	}

	return WithdrawnState;
}

static Bool
animSetScreenOptions(CompPlugin *plugin,
					CompScreen * screen,
					char *name,
					CompOptionValue * value)
{
	CompOption *o;
	int index;

	ANIM_SCREEN(screen);

	o = compFindOption(as->opt, NUM_OPTIONS(as), name, &index);
	if (!o)
		return FALSE;

	switch (index)
	{
	case ANIM_SCREEN_OPTION_MINIMIZE_EFFECT:
		if (compSetStringOption(o, value))
		{
			as->minimizeEffect = animEffectFromString (&o->value,
													   minimizeEffectType,
													   NUM_MINIMIZE_EFFECT);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CLOSE1_EFFECT:
		if (compSetStringOption(o, value))
		{
			as->close1Effect = animEffectFromString (&o->value,
													 closeEffectType,
													 NUM_CLOSE_EFFECT);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CLOSE2_EFFECT:
		if (compSetStringOption(o, value))
		{
			as->close2Effect = animEffectFromString (&o->value,
													 closeEffectType,
													 NUM_CLOSE_EFFECT);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CREATE1_EFFECT:
		if (compSetStringOption(o, value))
		{
			as->create1Effect = animEffectFromString (&o->value,
					 								  closeEffectType,
					 								  NUM_CLOSE_EFFECT);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CREATE2_EFFECT:
		if (compSetStringOption(o, value))
		{
			as->create2Effect = animEffectFromString (&o->value,
					 								  closeEffectType,
					 								  NUM_CLOSE_EFFECT);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_FOCUS_EFFECT:
		if (compSetStringOption(o, value))
		{
			as->focusEffect = animEffectFromString (&o->value,
													focusEffectType,
													NUM_FOCUS_EFFECT);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_SHADE_EFFECT:
		if (compSetStringOption(o, value))
		{
			as->shadeEffect = animEffectFromString (&o->value,
													shadeEffectType,
													NUM_SHADE_EFFECT);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_MINIMIZE_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
									   minimizeEffectType,
									   NUM_MINIMIZE_EFFECT,
									   as->minimizeRandomEffects,
									   &as->nMinimizeRandomEffects);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CLOSE1_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
									   closeEffectType,
									   NUM_CLOSE_EFFECT,
									   as->close1RandomEffects,
									   &as->nClose1RandomEffects);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CLOSE2_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
									   closeEffectType,
									   NUM_CLOSE_EFFECT,
									   as->close2RandomEffects,
									   &as->nClose2RandomEffects);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CREATE1_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
									   closeEffectType,
									   NUM_CLOSE_EFFECT,
									   as->create1RandomEffects,
									   &as->nCreate1RandomEffects);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_CREATE2_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
									   closeEffectType,
									   NUM_CLOSE_EFFECT,
									   as->create2RandomEffects,
									   &as->nCreate2RandomEffects);
			return TRUE;
		}
		break;
	case ANIM_SCREEN_OPTION_SHADE_RANDOM_EFFECTS:
		if (compSetOptionList(o, value))
		{
			animStoreRandomEffectList (&o->value,
									   shadeEffectType,
									   NUM_SHADE_EFFECT,
									   as->shadeRandomEffects,
									   &as->nShadeRandomEffects);
			return TRUE;
		}
		break;
	default:
		return compSetScreenOption (screen, o, value);
		break;
	}

	return FALSE;
}

static const CompMetadataOptionInfo animScreenOptionInfo[] = {
	{ "minimize_match", "match", 0, 0, 0 },
	{ "close1_match", "match", 0, 0, 0 },
	{ "close2_match", "match", 0, 0, 0 },
	{ "create1_match", "match", 0, 0, 0 },
	{ "create2_match", "match", 0, 0, 0 },
	{ "focus_match", "match", 0, 0, 0 },
	{ "shade_match", "match", 0, 0, 0 },
	{ "minimize_effect", "string", 0, 0, 0 },
	{ "minimize_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "minimize_random_effects", "list", "<type>string</type>", 0, 0 },
	{ "close1_effect", "string", 0, 0, 0 },
	{ "close1_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "close1_random_effects", "list", "<type>string</type>", 0, 0 },
	{ "create1_effect", "string", 0, 0, 0 },
	{ "create1_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "create1_random_effects", "list", "<type>string</type>", 0, 0 },
	{ "close2_effect", "string", 0, 0, 0 },
	{ "close2_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "close2_random_effects", "list", "<type>string</type>", 0, 0 },
	{ "create2_effect", "string", 0, 0, 0 },
	{ "create2_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "create2_random_effects", "list", "<type>string</type>", 0, 0 },
	{ "focus_effect", "string", 0, 0, 0 },
	{ "focus_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "shade_effect", "string", 0, 0, 0 },
	{ "shade_duration", "float", "<min>0.1</min>", 0, 0 },
	{ "shade_random_effects", "list", "<type>string</type>", 0, 0 },
	{ "rollup_fixed_interior", "bool", 0, 0, 0 },
	{ "all_random", "bool", 0, 0, 0 },
	{ "time_step", "int", "<min>1</min>", 0, 0 },
	{ "time_step_intense", "int", "<min>1</min>", 0, 0 },
	{ "beam_size", "float", "<min>0.1</min>", 0, 0 },
	{ "beam_spacing", "int", "<min>1</min>", 0, 0 },
	{ "beam_color", "color", 0, 0, 0 },
	{ "beam_slowdown", "float", "<min>0.1</min>", 0, 0 },
	{ "beam_life", "float", "<min>0.1</min>", 0, 0 },
	{ "curved_fold_amp", "float", "<min>-0.5</min><max>0.5</max>", 0, 0 },
	{ "domino_direction", "int", RESTOSTRING (0, AnimDirectionNum-1), 0, 0 },
	{ "razr_direction", "int", RESTOSTRING (0, AnimDirectionNum-1), 0, 0 },
	{ "explode_thickness", "float", "<min>0</min>", 0, 0 },
	{ "explode_gridx", "int", "<min>1</min>", 0, 0 },
	{ "explode_gridy", "int", "<min>1</min>", 0, 0 },
	{ "explode_tesselation", "int", RESTOSTRING (0, PolygonTessNum-1), 0, 0 },
	{ "fire_particles", "int", "<min>0</min>", 0, 0 },
	{ "fire_size", "float", "<min>0.1</min>", 0, 0 },
	{ "fire_slowdown", "float", "<min>0.1</min>", 0, 0 },
	{ "fire_life", "float", "<min>0.1</min>", 0, 0 },
	{ "fire_color", "color", 0, 0, 0 },
	{ "fire_direction", "int", RESTOSTRING (0, PolygonTessNum-1), 0, 0 },
	{ "fire_constant_speed", "bool", 0, 0, 0 },
	{ "fire_smoke", "bool", 0, 0, 0 },
	{ "fire_mystical", "bool", 0, 0, 0 },
	{ "glide1_away_position", "float", 0, 0, 0 },
	{ "glide1_away_angle", "float", 0, 0, 0 },
	{ "glide1_thickness", "float", "<min>0</min>", 0, 0 },
	{ "glide2_away_position", "float", 0, 0, 0 },
	{ "glide2_away_angle", "float", 0, 0, 0 },
	{ "glide2_thickness", "float", "<min>0</min>", 0, 0 },
	{ "horizontal_folds_amp", "float", "<min>-0.5</min><max>0.5</max>", 0, 0 },
	{ "horizontal_folds_num_folds", "int", "<min>1</min>", 0, 0 },
	{ "magic_lamp_grid_res", "int", "<min>4</min>", 0, 0 },
	{ "magic_lamp_max_waves", "int", "<min>3</min>", 0, 0 },
	{ "magic_lamp_amp_min", "float", "<min>200</min>", 0, 0 },
	{ "magic_lamp_amp_max", "float", "<min>200</min>", 0, 0 },
	{ "magic_lamp_create_start_width", "int", "<min>0</min>", 0, 0 },
	{ "magic_lamp_vacuum_grid_res", "int", "<min>4</min>", 0, 0 },
	{ "magic_lamp_vacuum_max_waves", "int", "<min>0</min>", 0, 0 },
	{ "magic_lamp_vacuum_amp_min", "float", "<min>200</min>", 0, 0 },
	{ "magic_lamp_vacuum_amp_max", "float", "<min>200</min>", 0, 0 },
	{ "magic_lamp_vacuum_create_start_width", "int", "<min>0</min>", 0, 0 },
	{ "sidekick_num_rotations", "float", "<min>0</min>", 0, 0 },
	{ "sidekick_springiness", "float", "<min>0</min><max>1</max>", 0, 0 },
	{ "wave_width", "float", "<min>0</min>", 0, 0 },
	{ "wave_amp", "float", "<min>0</min>", 0, 0 },
	{ "zoom_from_center", "int", RESTOSTRING (0, ZoomFromCenterNum-1), 0, 0 },
	{ "zoom_springiness", "float", "<min>0</min><max>1</max>", 0, 0 }
};

static CompOption *
animGetScreenOptions(CompPlugin *plugin, CompScreen * screen, int *count)
{
	ANIM_SCREEN(screen);

	*count = NUM_OPTIONS(as);
	return as->opt;
}

static void
objectInit(Object * object,
		   float positionX, float positionY,
		   float gridPositionX, float gridPositionY)
{
	object->gridPosition.x = gridPositionX;
	object->gridPosition.y = gridPositionY;

	object->position.x = positionX;
	object->position.y = positionY;

	object->offsetTexCoordForQuadBefore.x = 0;
	object->offsetTexCoordForQuadBefore.y = 0;
	object->offsetTexCoordForQuadAfter.x = 0;
	object->offsetTexCoordForQuadAfter.y = 0;
}

static void
modelInitObjects(Model * model, int x, int y, int width, int height)
{
	int gridX, gridY;
	int nGridCellsX, nGridCellsY;
	float x0, y0;

	x0 = model->scaleOrigin.x;
	y0 = model->scaleOrigin.y;

	// number of grid cells in x direction
	nGridCellsX = model->gridWidth - 1;

	if (model->forWindowEvent == WindowEventShade ||
		model->forWindowEvent == WindowEventUnshade)
	{
		// number of grid cells in y direction
		nGridCellsY = model->gridHeight - 3;	// One allocated for top, one for bottom

		float winContentsHeight =
				height - model->topHeight - model->bottomHeight;

		//Top
		float objectY = y + (0 - y0) * model->scale.y + y0;

		for (gridX = 0; gridX < model->gridWidth; gridX++)
		{
			objectInit(&model->objects[gridX],
					   x + ((gridX * width / nGridCellsX) - x0) * 
					   model->scale.x + x0, objectY,
					   (float)gridX / nGridCellsX, 0);
		}

		// Window contents
		for (gridY = 1; gridY < model->gridHeight - 1; gridY++)
		{
			float inWinY =
					(gridY - 1) * winContentsHeight / nGridCellsY +
					model->topHeight;
			float gridPosY = inWinY / height;

			objectY = y + (inWinY - y0) * model->scale.y + y0;

			for (gridX = 0; gridX < model->gridWidth; gridX++)
			{
				objectInit(&model->objects[gridY * model->gridWidth + gridX],
						   x + ((gridX * width / nGridCellsX) - x0) * 
						   model->scale.x + x0,
						   objectY, (float)gridX / nGridCellsX, gridPosY);
			}
		}

		// Bottom (gridY is model->gridHeight-1 now)
		objectY = y + (height - y0) * model->scale.y + y0;

		for (gridX = 0; gridX < model->gridWidth; gridX++)
		{
			objectInit(&model->objects[gridY * model->gridWidth + gridX],
					   x + ((gridX * width / nGridCellsX) - x0) * 
					   model->scale.x + x0, objectY,
					   (float)gridX / nGridCellsX, 1);
		}
	}
	else
	{
		// number of grid cells in y direction
		nGridCellsY = model->gridHeight - 1;

		int i = 0;

		for (gridY = 0; gridY < model->gridHeight; gridY++)
		{
			float objectY =
					y + ((gridY * height / nGridCellsY) -
						 y0) * model->scale.y + y0;
			for (gridX = 0; gridX < model->gridWidth; gridX++)
			{
				objectInit(&model->objects[i],
						   x + ((gridX * width / nGridCellsX) - x0) * 
						   model->scale.x + x0,
						   objectY,
						   (float)gridX / nGridCellsX,
						   (float)gridY / nGridCellsY);
				i++;
			}
		}
	}
}

static Model *createModel(CompWindow * w,
						  WindowEvent forWindowEvent,
						  AnimEffect forAnimEffect, int gridWidth,
						  int gridHeight)
{
	int x = WIN_X(w);
	int y = WIN_Y(w);
	int width = WIN_W(w);
	int height = WIN_H(w);

	Model *model;

	model = calloc(1, sizeof(Model));
	if (!model)
	{
		fprintf(stderr, "%s: Not enough memory at line %d!\n",
				__FILE__, __LINE__);
		return 0;
	}
	model->magicLampWaveCount = 0;
	model->magicLampWaves = NULL;

	model->gridWidth = gridWidth;
	model->gridHeight = gridHeight;
	model->numObjects = gridWidth * gridHeight;
	model->objects = calloc(1, sizeof(Object) * model->numObjects);
	if (!model->objects)
	{
		fprintf(stderr, "%s: Not enough memory at line %d!\n",
				__FILE__, __LINE__);
		free(model);
		return 0;
	}

	// Store win. size to check later
	model->winWidth = width;
	model->winHeight = height;

	// For shading
	model->forWindowEvent = forWindowEvent;
	model->topHeight = w->output.top;
	model->bottomHeight = w->output.bottom;

	model->scale.x = 1.0f;
	model->scale.y = 1.0f;

	model->scaleOrigin.x = 0.0f;
	model->scaleOrigin.y = 0.0f;

	modelInitObjects(model, x, y, width, height);

	modelCalcBounds(model);

	return model;
}

static Bool
animEnsureModel(CompWindow * w,
				WindowEvent forWindowEvent, AnimEffect forAnimEffect)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(w->screen);

	int gridWidth = 2;
	int gridHeight = 2;

	if (animEffectProperties[forAnimEffect].initGridFunc)
		animEffectProperties[forAnimEffect].initGridFunc(as,
														 forWindowEvent,
														 &gridWidth,
														 &gridHeight);

	Bool isShadeUnshadeEvent =
			(forWindowEvent == WindowEventShade ||
			 forWindowEvent == WindowEventUnshade);

	Bool wasShadeUnshadeEvent = aw->model &&
			(aw->model->forWindowEvent == WindowEventShade ||
			 aw->model->forWindowEvent == WindowEventUnshade);

	if (!aw->model ||
		gridWidth != aw->model->gridWidth ||
		gridHeight != aw->model->gridHeight ||
		(isShadeUnshadeEvent != wasShadeUnshadeEvent) ||
		aw->model->winWidth != WIN_W(w) || aw->model->winHeight != WIN_H(w))
	{
		if (aw->model)
		{
			if (aw->model->magicLampWaves)
				free(aw->model->magicLampWaves);
			free(aw->model->objects);
			free(aw->model);
		}
		aw->model = createModel(w,
								forWindowEvent, forAnimEffect,
								gridWidth, gridHeight);
		if (!aw->model)
			return FALSE;
	}

	return TRUE;
}

static Bool playingPolygonEffect(AnimScreen *as, AnimWindow *aw)
{
	if (!animEffectProperties[aw->curAnimEffect].
		 addCustomGeometryFunc)
		return FALSE;

	if (!(aw->curAnimEffect == AnimEffectGlide3D1 ||
		  aw->curAnimEffect == AnimEffectGlide3D2))
		return TRUE;

	float finalDistFac;
	float finalRotAng;
	float thickness;

	fxGlideGetParams(as, aw, &finalDistFac, &finalRotAng, &thickness);

	return (thickness > 1e-5); // glide is 3D if thickness > 0
}

static void postAnimationCleanup(CompWindow * w, Bool resetAnimation)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(w->screen);

	if (resetAnimation)
	{
		aw->curWindowEvent = WindowEventNone;
		aw->curAnimEffect = AnimEffectNone;
		aw->animOverrideProgressDir = 0;

		if (aw->model)
		{
			if (aw->model->magicLampWaves)
				free(aw->model->magicLampWaves);
			aw->model->magicLampWaves = 0;
		}
	}
	if (aw->winThisIsPaintedBefore && !aw->winThisIsPaintedBefore->destroyed)
	{
		AnimWindow *aw2 = GET_ANIM_WINDOW(aw->winThisIsPaintedBefore, as);
		if (aw2)
		{
			aw2->winToBePaintedBeforeThis = NULL;
		}
		aw->winThisIsPaintedBefore = NULL;
		aw->moreToBePaintedPrev = NULL;
		aw->moreToBePaintedNext = NULL;
	}

	aw->state = aw->newState;

	if (aw->drawRegion)
		XDestroyRegion(aw->drawRegion);
	aw->drawRegion = NULL;
	aw->useDrawRegion = FALSE;

	if (aw->numPs)
	{
		int i = 0;

		for (i = 0; i < aw->numPs; i++)
			finiParticles(aw->ps + i);
		free(aw->ps);
		aw->ps = NULL;
		aw->numPs = 0;
	}

	if (aw->polygonSet)
	{
		freePolygonSet(aw);
		//aw->polygonSet->nClips = 0;
	}
	aw->animInitialized = FALSE;
	aw->remainderSteps = 0;

	//if (aw->unmapCnt || aw->destroyCnt)
	//    releaseWindow (w);
	while (aw->unmapCnt)
	{
		unmapWindow(w);
		aw->unmapCnt--;
	}
	while (aw->destroyCnt)
	{
		destroyWindow(w);
		aw->destroyCnt--;
	}
}

static inline Bool
isWinVisible(CompWindow *w)
{
	return (!w->destroyed &&
			!(!w->shaded &&
			  (w->attrib.map_state != IsViewable)));
}

static void
initiateFocusAnimation
	(CompScreen *s, CompWindow *w, Bool raised,
	 RestackInfo *restackInfo)
{
	CompWindow *wStart;
	CompWindow *wEnd;
	CompWindow *wOldAbove;

	if (restackInfo)
	{
		wStart = restackInfo->wStart;
		wEnd = restackInfo->wEnd;
		wOldAbove = restackInfo->wOldAbove;
		raised = restackInfo->raised;
	}

	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	if (aw->curWindowEvent != WindowEventNone ||
		as->switcherActive || as->groupTabChangeActive)
		return;

	if (matchEval (&as->opt[ANIM_SCREEN_OPTION_FOCUS_MATCH].value.match, w) &&
		as->focusEffect &&
		// On unminimization, focus event is fired first.
		// When this happens and minimize is in progress,
		// don't prevent rewinding of minimize when unminimize is fired
		// right after this focus event.
		aw->curWindowEvent != WindowEventMinimize &&
		animEnsureModel(w, WindowEventFocus, as->focusEffect))
	{
		if (as->focusEffect == AnimEffectFocusFade)
		{
			// Find union region of all windows that will be
			// faded through by w. If the region is empty, don't
			// run focus fade effect.

			Region fadeRegion = XCreateRegion();
			Region tmpRegion = XCreateRegion();
			Region finalFadeRegion = XCreateRegion();

			XRectangle rect;
			CompWindow *nw;
			for (nw = wStart; nw && nw != wEnd; nw = nw->next)
			{
				if (!isWinVisible(nw))
					continue;

				rect.x = WIN_X(nw);
				rect.y = WIN_Y(nw);
				rect.width = WIN_W(nw);
				rect.height = WIN_H(nw);
				XUnionRectWithRegion(&rect, fadeRegion, tmpRegion);
				XUnionRegion(&emptyRegion, tmpRegion, fadeRegion);
			}
			rect.x = WIN_X(wEnd);
			rect.y = WIN_Y(wEnd);
			rect.width = WIN_W(wEnd);
			rect.height = WIN_H(wEnd);
			XUnionRectWithRegion(&rect, &emptyRegion, tmpRegion);
			XIntersectRegion(fadeRegion, tmpRegion, finalFadeRegion);

			if (finalFadeRegion->numRects == 0)
				return; // empty -> won't be drawn
		}

		// FOCUS event!

		//printf("FOCUS event! %X\n", (unsigned)aw);

		if (aw->curWindowEvent != WindowEventNone)
		{
			postAnimationCleanup(w, TRUE);
		}

		as->animInProgress = TRUE;
		aw->curWindowEvent = WindowEventFocus;
		aw->curAnimEffect = as->focusEffect;
		aw->animTotalTime =
			as->opt[ANIM_SCREEN_OPTION_FOCUS_DURATION].value.f * 1000;
		aw->animRemainingTime = aw->animTotalTime;

		// Store coords in this viewport to omit 3d effect
		// painting in other viewports
		aw->lastKnownCoords.x = w->attrib.x;
		aw->lastKnownCoords.y = w->attrib.y;

		if (as->focusEffect == AnimEffectFocusFade && wOldAbove)
		{
			// Store this window in the next window
			// so that this is drawn before that
			// (for focus fade effect)
			AnimWindow *awNext = GET_ANIM_WINDOW(wOldAbove, as);
			awNext->winToBePaintedBeforeThis = w;
			aw->winThisIsPaintedBefore = wOldAbove;
		}

		damageScreen(w->screen);
	}
}

static void
initiateFocusAnimationNonFade(CompWindow *w)
{
	initiateFocusAnimation (w->screen, w, TRUE, NULL);
}

static void
initiateFocusAnimationFade(CompWindow *w, RestackInfo *restackInfo)
{
	initiateFocusAnimation (w->screen, w, TRUE, restackInfo);
}

// returns whether this window is relevant for fade focus
static Bool
relevantForFadeFocus(CompWindow *nw)
{
	if (!(nw->type &
		  (CompWindowTypeDockMask |
		   CompWindowTypeNormalMask |
		   CompWindowTypeSplashMask |
		   CompWindowTypeDialogMask |
		   CompWindowTypeModalDialogMask)))
		return FALSE;
	return isWinVisible(nw);
}

static void animPreparePaintScreen(CompScreen * s, int msSinceLastPaint)
{
	CompWindow *w;

	ANIM_SCREEN(s);

	if (as->focusEffect == AnimEffectFocusFade)
	{
		if (as->aWinWasRestackedJustNow)
		{
			// do in reverse order so that focus-fading chains are handled
			// properly
			for (w = s->reverseWindows; w; w = w->prev)
			{
				ANIM_WINDOW(w);
				if (aw->restackInfo)
				{
					// Check if above window is focus-fading
					// (like a dialog of an app. window)
					// if so, focus-fade this together with the one above
					// (link to it)
					CompWindow *nw;
					for (nw = w->next; nw; nw = nw->next)
					{
						if (relevantForFadeFocus(nw))
							break;
					}
					if (!nw)
						continue;
					AnimWindow *awNext = GET_ANIM_WINDOW(nw, as);
					if (awNext && awNext->winThisIsPaintedBefore)
					{
						awNext->moreToBePaintedPrev = w;
						aw->moreToBePaintedNext = nw;
						aw->restackInfo->wOldAbove =
							awNext->winThisIsPaintedBefore;
					}
					initiateFocusAnimationFade(w, aw->restackInfo);
				}
			}
		}
	}

	if (as->animInProgress)
	{
		AnimWindow *aw;
		BoxRec box;
		Point topLeft, bottomRight;

		as->animInProgress = FALSE;
		for (w = s->windows; w; w = w->next)
		{
			aw = GET_ANIM_WINDOW(w, as);

			if (aw->numPs)
			{
				int i = 0;

				for (i = 0; i < aw->numPs; i++)
				{
					if (aw->ps[i].active)
					{
						updateParticles(&aw->ps[i], msSinceLastPaint);
						as->animInProgress = TRUE;
					}
				}
			}

			if (aw->animRemainingTime > 0)
			{
				if (!aw->animInitialized)	// if animation is just starting
				{
					aw->subEffectNo =
							animEffectProperties[aw->curAnimEffect].
							subEffectNo;

					aw->deceleratingMotion =
							animEffectProperties[aw->curAnimEffect].
							animStepPolygonFunc ==
							polygonsDeceleratingAnimStepPolygon;

					if (playingPolygonEffect(as, aw))
					{
						// Allocate polygon set if null
						if (!aw->polygonSet)
						{
							//printf("calloc aw->polygonSet\n");
							aw->polygonSet = calloc(1, sizeof(PolygonSet));
						}
						if (!aw->polygonSet)
						{
							fprintf(stderr,
									"%s: Not enough memory at line %d!\n",
									__FILE__, __LINE__);
							return;
						}
						aw->polygonSet->allFadeDuration = -1.0f;
					}
				}

				// if 3d polygon fx
				if (playingPolygonEffect(as, aw))
				{
					aw->nClipsPassed = 0;
					aw->clipsUpdated = FALSE;
				}
				if (aw->model)
				{
					topLeft = aw->model->topLeft;
					bottomRight = aw->model->bottomRight;

					// If just starting, call fx init func.
					if (!aw->animInitialized &&
						animEffectProperties[aw->curAnimEffect].initFunc)
						animEffectProperties[aw->
											 curAnimEffect].initFunc(s, w);
					aw->animInitialized = TRUE;

					if (aw->model->winWidth != WIN_W(w) ||
						aw->model->winHeight != WIN_H(w))
					{
						// model needs update
						// re-create model
						animEnsureModel
								(w, aw->curWindowEvent, aw->curAnimEffect);
						if (aw->model == 0)
							continue;	// skip this window
					}
					// Call fx step func.
					if (animEffectProperties[aw->curAnimEffect].animStepFunc)
					{
						animEffectProperties[aw->
											 curAnimEffect].
								animStepFunc(s, w, msSinceLastPaint);
					}
					if (aw->animRemainingTime <= 0)
					{
						// Animation done
						postAnimationCleanup(w, TRUE);
					}

					if (!(s->damageMask & COMP_SCREEN_DAMAGE_ALL_MASK))
					{
						if (aw->animRemainingTime > 0)
						{
							if (aw->model->topLeft.x < topLeft.x)
								topLeft.x = aw->model->topLeft.x;
							if (aw->model->topLeft.y < topLeft.y)
								topLeft.y = aw->model->topLeft.y;
							if (aw->model->bottomRight.x > bottomRight.x)
								bottomRight.x = aw->model->bottomRight.x;
							if (aw->model->bottomRight.y > bottomRight.y)
								bottomRight.y = aw->model->bottomRight.y;
						}
						else
							addWindowDamage(w);

						box.x1 = topLeft.x;
						box.y1 = topLeft.y;
						box.x2 = bottomRight.x + 0.5f;
						box.y2 = bottomRight.y + 0.5f;

						box.x1 -= w->attrib.x + w->attrib.border_width;
						box.y1 -= w->attrib.y + w->attrib.border_width;
						box.x2 -= w->attrib.x + w->attrib.border_width;
						box.y2 -= w->attrib.y + w->attrib.border_width;

						addWindowDamageRect(w, &box);
					}
				}
				as->animInProgress |= (aw->animRemainingTime > 0);
			}

			if (aw->animRemainingTime <= 0)
			{
				if (aw->curAnimEffect != AnimEffectNone ||
					aw->unmapCnt > 0 || aw->destroyCnt > 0)
					postAnimationCleanup(w, TRUE);
				aw->curWindowEvent = WindowEventNone;
				aw->curAnimEffect = AnimEffectNone;
			}
		}
	}

	UNWRAP(as, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, msSinceLastPaint);
	WRAP(as, s, preparePaintScreen, animPreparePaintScreen);
}

static void animDonePaintScreen(CompScreen * s)
{
	ANIM_SCREEN(s);

	if (as->animInProgress)
	{
		damageScreen(s);
	}
	UNWRAP(as, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP(as, s, donePaintScreen, animDonePaintScreen);
}

static void
animAddWindowGeometry(CompWindow * w,
					  CompMatrix * matrix,
					  int nMatrix, Region region, Region clip)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(w->screen);

	// if model is lost during animation (e.g. when plugin just reloaded)
	if (aw->animRemainingTime > 0 && !aw->model)
	{
		aw->animRemainingTime = 0;
		postAnimationCleanup(w, TRUE);
	}
	// if window is being animated
	if (aw->animRemainingTime > 0 && aw->model &&
		!animEffectProperties[aw->curAnimEffect].letOthersDrawGeoms)
	{
		BoxPtr pClip;
		int nClip;
		int nVertices, nIndices;
		GLushort *i;
		GLfloat *v;
		int x1, y1, x2, y2;
		float width, height;
		float winContentsY, winContentsHeight;
		float deformedX, deformedY;
		int nVertX, nVertY, wx, wy;
		int vSize, it;
		float gridW, gridH, x, y;
		Bool rect = TRUE;
		Bool useTextureQ = TRUE;
		Model *model = aw->model;
		Region awRegion = NULL;

		// Use Q texture coordinate to avoid jagged-looking quads
		// http://www.r3.nu/~cass/qcoord/
		if (animEffectProperties[aw->curAnimEffect].dontUseQTexCoord)
			useTextureQ = FALSE;

		if (aw->useDrawRegion)
		{
			awRegion = XCreateRegion();
			XOffsetRegion(aw->drawRegion, WIN_X(w), WIN_Y(w));
			XIntersectRegion(region, aw->drawRegion, awRegion);
			XOffsetRegion(aw->drawRegion, -WIN_X(w), -WIN_Y(w));
			nClip = awRegion->numRects;
			pClip = awRegion->rects;
		}
		else
		{
			nClip = region->numRects;
			pClip = region->rects;
		}

		if (nClip == 0)			// nothing to do
		{
			if (awRegion)
				XDestroyRegion(awRegion);
			return;
		}

		for (it = 0; it < nMatrix; it++)
		{
			if (matrix[it].xy != 0.0f || matrix[it].yx != 0.0f)
			{
				rect = FALSE;
				break;
			}
		}

		w->drawWindowGeometry = animDrawWindowGeometry;

		if (aw->polygonSet &&
			animEffectProperties[aw->curAnimEffect].addCustomGeometryFunc)
		{
			/*int nClip2 = nClip;
			   BoxPtr pClip2 = pClip;

			   // For each clip passed to this function
			   for (; nClip2--; pClip2++)
			   {
			   x1 = pClip2->x1;
			   y1 = pClip2->y1;
			   x2 = pClip2->x2;
			   y2 = pClip2->y2;

			   printf("x1: %4d, y1: %4d, x2: %4d, y2: %4d", x1, y1, x2, y2);
			   printf("\tm: %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f\n",
			   matrix[0].xx, matrix[0].xy, matrix[0].yx, matrix[0].yy,
			   matrix[0].x0, matrix[0].y0);
			   } */
			if (nMatrix == 0)
			    return;
			animEffectProperties[aw->curAnimEffect].
					addCustomGeometryFunc(w->screen, w, nClip, pClip,
										  nMatrix, matrix);

			// If addGeometryFunc exists, it is expected to do everthing
			// to add geometries (instead of the rest of this function).

			if (w->vCount == 0)	// if there is no vertex
			{
				// put a dummy quad in vertices and indices
				if (4 > w->indexSize)
				{
					if (!moreWindowIndices(w, 4))
						return;
				}
				if (4 > w->vertexSize)
				{
					if (!moreWindowVertices(w, 4))
						return;
				}
				w->vCount = 4;
				w->indexCount = 4;
				w->texUnits = 1;
				memset(w->vertices, 0, sizeof(GLfloat) * 4);
				memset(w->indices, 0, sizeof(GLushort) * 4);
			}
			return;				// We're done here.
		}

		// window coordinates and size
		wx = WIN_X(w);
		wy = WIN_Y(w);
		width = WIN_W(w);
		height = WIN_H(w);

		// to be used if event is shade/unshade
		winContentsY = w->attrib.y;
		winContentsHeight = w->height;

		w->texUnits = nMatrix;

		if (w->vCount == 0)
		{
			// reset
			w->indexCount = 0;
			w->texCoordSize = 4;
		}
		vSize = 2 + w->texUnits * w->texCoordSize;

		nVertices = w->vCount;
		nIndices = w->indexCount;

		v = w->vertices + (nVertices * vSize);
		i = w->indices + nIndices;

		// For each clip passed to this function
		for (; nClip--; pClip++)
		{
			x1 = pClip->x1;
			y1 = pClip->y1;
			x2 = pClip->x2;
			y2 = pClip->y2;
			/*
			   printf("x1: %4d, y1: %4d, x2: %4d, y2: %4d", x1, y1, x2, y2);
			   printf("\tm: %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f\n",
			   matrix[0].xx, matrix[0].xy, matrix[0].yx, matrix[0].yy,
			   matrix[0].x0, matrix[0].y0);
			 */
			gridW = (float)width / (model->gridWidth - 1);

			if (aw->curWindowEvent == WindowEventShade ||
				aw->curWindowEvent == WindowEventUnshade)
			{
				if (y1 < w->attrib.y)	// if at top part
				{
					gridH = model->topHeight;
				}
				else if (y2 > w->attrib.y + w->height)	// if at bottom
				{
					gridH = model->bottomHeight;
				}
				else			// in window contents (only in Y coords)
				{
					float winContentsHeight =
							height - model->topHeight - model->bottomHeight;
					gridH = winContentsHeight / (model->gridHeight - 3);
				}
			}
			else
				gridH = (float)height / (model->gridHeight - 1);

			// nVertX, nVertY: number of vertices for this clip in x and y dimensions
			// + 2 to avoid running short of vertices in some cases
			nVertX = ceil((x2 - x1) / gridW) + 2;
			nVertY = (gridH ? ceil((y2 - y1) / gridH) : 0) + 2;

			// Allocate 4 indices for each quad
			int newIndexSize = nIndices + ((nVertX - 1) * (nVertY - 1) * 4);

			if (newIndexSize > w->indexSize)
			{
				if (!moreWindowIndices(w, newIndexSize))
					return;

				i = w->indices + nIndices;
			}
			// Assign quad vertices to indices
			int jx, jy;

			for (jy = 0; jy < nVertY - 1; jy++)
			{
				for (jx = 0; jx < nVertX - 1; jx++)
				{
					*i++ = nVertices + nVertX * (2 * jy + 1) + jx;
					*i++ = nVertices + nVertX * (2 * jy + 1) + jx + 1;
					*i++ = nVertices + nVertX * 2 * jy + jx + 1;
					*i++ = nVertices + nVertX * 2 * jy + jx;

					nIndices += 4;
				}
			}

			// Allocate vertices
			int newVertexSize =
					(nVertices + nVertX * (2 * nVertY - 2)) * vSize;
			if (newVertexSize > w->vertexSize)
			{
				if (!moreWindowVertices(w, newVertexSize))
					return;

				v = w->vertices + (nVertices * vSize);
			}

			float rowTexCoordQ = 1;
			float prevRowCellWidth = 0;	// this initial value won't be used
			float rowCellWidth = 0;

			// For each vertex
			for (jy = 0, y = y1; jy < nVertY; jy++)
			{
				if (y > y2)
					y = y2;

				// Do calculations for y here to avoid repeating
				// them unnecessarily in the x loop

				float topiyFloat;
				Bool applyOffsets = TRUE;

				if (aw->curWindowEvent == WindowEventShade
					|| aw->curWindowEvent == WindowEventUnshade)
				{
					if (y1 < w->attrib.y)	// if at top part
					{
						topiyFloat = (y - WIN_Y(w)) / model->topHeight;
						topiyFloat = MIN(topiyFloat, 0.999);	// avoid 1.0
						applyOffsets = FALSE;
					}
					else if (y2 > w->attrib.y + w->height)	// if at bottom
					{
						topiyFloat = (model->gridHeight - 2) +
							         (model->bottomHeight ? (y - winContentsY -
															 winContentsHeight) /
									                         model->bottomHeight : 0);
						applyOffsets = FALSE;
					}
					else		// in window contents (only in Y coords)
					{
						topiyFloat = (model->gridHeight - 3) * 
							         (y - winContentsY) / winContentsHeight + 1;
					}
				}
				else
				{
					topiyFloat = (model->gridHeight - 1) * (y - wy) / height;
				}
				// topiy should be at most (model->gridHeight - 2)
				int topiy = (int)(topiyFloat + 1e-4);

				if (topiy == model->gridHeight - 1)
					topiy--;
				int bottomiy = topiy + 1;
				float iny = topiyFloat - topiy;

				// End of calculations for y

				for (jx = 0, x = x1; jx < nVertX; jx++)
				{
					if (x > x2)
						x = x2;

					// find containing grid cell (leftix rightix) x (topiy bottomiy)
					float leftixFloat =
							(model->gridWidth - 1) * (x - wx) / width;
					int leftix = (int)(leftixFloat + 1e-4);

					if (leftix == model->gridWidth - 1)
						leftix--;
					int rightix = leftix + 1;

					// Objects that are at top, bottom, left, right corners of quad
					Object *objToTopLeft =
							&(model->objects[topiy * model->gridWidth + leftix]);
					Object *objToTopRight =
							&(model->objects[topiy * model->gridWidth + rightix]);
					Object *objToBottomLeft =
							&(model->objects[bottomiy * model->gridWidth + leftix]);
					Object *objToBottomRight =
							&(model->objects[bottomiy * model->gridWidth + rightix]);

					// find position in cell by taking remainder of flooring
					float inx = leftixFloat - leftix;

					// Interpolate to find deformed coordinates

					float hor1x = (1 - inx) *
						    objToTopLeft->position.x +
							inx * objToTopRight->position.x;
					float hor1y = (1 - inx) *
							objToTopLeft->position.y +
							inx * objToTopRight->position.y;
					float hor2x = (1 - inx) *
							objToBottomLeft->position.x +
							inx * objToBottomRight->position.x;
					float hor2y = (1 - inx) *
							objToBottomLeft->position.y +
							inx * objToBottomRight->position.y;

					deformedX = (1 - iny) * hor1x + iny * hor2x;
					deformedY = (1 - iny) * hor1y + iny * hor2y;

					if (useTextureQ)
					{
						if (jx == 1)
							rowCellWidth = deformedX - v[-2];

						if (jy > 0 && jx == 1)	// do only once per row for all rows except row 0
						{
							rowTexCoordQ = (rowCellWidth / prevRowCellWidth);

							v[-3] = rowTexCoordQ;	// update first column
							v[-6] *= rowTexCoordQ;	// (since we didn't know rowTexCoordQ before)
							v[-5] *= rowTexCoordQ;
						}
					}
					if (rect)
					{
						for (it = 0; it < nMatrix; it++)
						{
							float offsetY = 0;

							if (applyOffsets && y < y2)
								offsetY = objToTopLeft->offsetTexCoordForQuadAfter.y;

							*v++ = COMP_TEX_COORD_X(&matrix[it], x);
							*v++ = COMP_TEX_COORD_Y(&matrix[it], y + offsetY);
							*v++ = 0;
							if (useTextureQ)
							{
								*v++ = rowTexCoordQ;	// Q texture coordinate

								if (0 < jy && jy < nVertY - 1)
								{
									// copy first 3 texture coords to duplicate row
									memcpy(v - 4 + nVertX * vSize, 
										   v - 4, 3 * sizeof(GLfloat));
									*(v - 1 + nVertX * vSize) = 1;	// Q texture coordinate
								}
								if (applyOffsets &&	/*0 < jy && */
									objToTopLeft->
									offsetTexCoordForQuadBefore.y != 0)
								{
									// After copying to next row, update texture y coord
									// by following object's offset
									offsetY = objToTopLeft-> offsetTexCoordForQuadBefore.y;
									v[-3] = COMP_TEX_COORD_Y(&matrix[it], y + offsetY);
								}
								if (jx > 0)	// since column 0 is updated when jx == 1
								{
									v[-4] *= rowTexCoordQ;
									v[-3] *= rowTexCoordQ;
								}
							}
							else
							{
								*v++ = 1;

								if (0 < jy && jy < nVertY - 1)
								{
									// copy first 3 texture coords to duplicate row
									memcpy(v - 4 + nVertX * vSize, 
										   v - 4, 3 * sizeof(GLfloat));

									*(v - 1 + nVertX * vSize) = 1;	// Q texture coordinate
								}
								if (applyOffsets && 
									objToTopLeft->offsetTexCoordForQuadBefore.y != 0)
								{
									// After copying to next row, update texture y coord
									// by following object's offset
									offsetY = objToTopLeft->offsetTexCoordForQuadBefore.y;
									v[-3] = COMP_TEX_COORD_Y(&matrix[it], y + offsetY);
								}
							}
						}
					}
					else
					{
						for (it = 0; it < nMatrix; it++)
						{
							float offsetY = 0;

							if (applyOffsets && y < y2)
							{
								// FIXME:
								// the correct value below doesn't work for some reason
								offsetY = 0;
								//		objToTopLeft->
								//		offsetTexCoordForQuadAfter.y;
							}

							*v++ = COMP_TEX_COORD_XY(&matrix[it], x, y + offsetY);
							*v++ = COMP_TEX_COORD_YX(&matrix[it], x, y + offsetY);
							*v++ = 0;
							if (useTextureQ)
							{
								*v++ = rowTexCoordQ;	// Q texture coordinate

								if (0 < jy && jy < nVertY - 1)
								{
									// copy first 3 texture coords to duplicate row
									memcpy(v - 4 + nVertX * vSize, 
										   v - 4, 3 * sizeof(GLfloat));
									*(v - 1 + nVertX * vSize) = 1;	// Q texture coordinate
								}
								if (applyOffsets && 
									objToTopLeft->offsetTexCoordForQuadBefore.y != 0)
								{
									// After copying to next row, update texture y coord
									// by following object's offset
									offsetY = objToTopLeft->offsetTexCoordForQuadBefore.y;
									v[-4] = COMP_TEX_COORD_XY(&matrix[it], x,
															  y + offsetY);
									v[-3] = COMP_TEX_COORD_YX(&matrix[it], x,
															  y + offsetY);
								}
								if (jx > 0)	// column t should be updated when jx is t+1
								{
									v[-4] *= rowTexCoordQ;
									v[-3] *= rowTexCoordQ;
								}
							}
							else
							{
								*v++ = 1;

								if (0 < jy && jy < nVertY - 1)
								{
									// copy first 3 texture coords to duplicate row
									memcpy(v - 4 + nVertX * vSize, 
										   v - 4, 3 * sizeof(GLfloat));
									*(v - 1 + nVertX * vSize) = 1;	// Q texture coordinate
								}
								if (applyOffsets && 
									objToTopLeft->offsetTexCoordForQuadBefore.y != 0)
								{
									// After copying to next row, update texture y coord
									// by following object's offset
									offsetY =
											objToTopLeft->
											offsetTexCoordForQuadBefore.y;
									v[-4] = COMP_TEX_COORD_XY(&matrix[it], x,
															  y + offsetY);
									v[-3] = COMP_TEX_COORD_YX(&matrix[it], x,
															  y + offsetY);
								}
							}
						}
					}
					*v++ = deformedX;
					*v++ = deformedY;

					if (0 < jy && jy < nVertY - 1)
						memcpy(v - 2 + nVertX * vSize, v - 2, 2 * sizeof(GLfloat));

					nVertices++;

					// increment x properly (so that coordinates fall on grid intersections)
					x = rightix * gridW + wx;
				}
				if (useTextureQ)
					prevRowCellWidth = rowCellWidth;

				if (0 < jy && jy < nVertY - 1)
				{
					v += nVertX * vSize;	// skip the duplicate row
					nVertices += nVertX;
				}
				// increment y properly (so that coordinates fall on grid intersections)
				if (aw->curWindowEvent == WindowEventShade
					|| aw->curWindowEvent == WindowEventUnshade)
				{
					y += gridH;
				}
				else
				{
					y = bottomiy * gridH + wy;
				}
			}
		}
		w->vCount = nVertices;
		w->indexCount = nIndices;
		if (awRegion)
		{
			XDestroyRegion(awRegion);
			awRegion = NULL;
		}
	}
	else
	{
		UNWRAP(as, w->screen, addWindowGeometry);
		(*w->screen->addWindowGeometry) (w, matrix, nMatrix, region, clip);
		WRAP(as, w->screen, addWindowGeometry, animAddWindowGeometry);
	}
}

static void
animDrawWindowTexture(CompWindow * w, CompTexture * texture,
					  const FragmentAttrib *attrib,
					  unsigned int mask)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(w->screen);

	if (aw->animRemainingTime > 0)	// if animation in progress, store texture
	{
		//printf("%X animDrawWindowTexture, texture: %X\n",
		//     (unsigned)aw, (unsigned)texture);
		aw->curTexture = texture;
		aw->curPaintAttrib = *attrib;
	}

	UNWRAP(as, w->screen, drawWindowTexture);
	(*w->screen->drawWindowTexture) (w, texture, attrib, mask);
	WRAP(as, w->screen, drawWindowTexture, animDrawWindowTexture);
}

static void
animDrawWindowGeometry(CompWindow * w)
{
	ANIM_WINDOW(w);

	//if (aw->animRemainingTime > 0 &&
	//	!animEffectProperties[aw->curAnimEffect].letOthersDrawGeoms)
	//{
	//printf("animDrawWindowGeometry: %X: coords: %d, %d, %f\n",
	//       (unsigned)aw, WIN_X (w), WIN_Y (w), aw->animRemainingTime);
	aw->nDrawGeometryCalls++;

	ANIM_SCREEN(w->screen);

	if (playingPolygonEffect(as, aw) &&
		animEffectProperties[aw->curAnimEffect].drawCustomGeometryFunc)
	{
		animEffectProperties[aw->curAnimEffect].drawCustomGeometryFunc (w->screen, w);
		return;
	}
	int texUnit = w->texUnits;
	int currentTexUnit = 0;
	int stride = 2 + texUnit * w->texCoordSize;
	GLfloat *vertices = w->vertices + (stride - 2);

	stride *= sizeof(GLfloat);

	glVertexPointer(2, GL_FLOAT, stride, vertices);

	while (texUnit--)
	{
		if (texUnit != currentTexUnit)
		{
			w->screen->clientActiveTexture(GL_TEXTURE0_ARB + texUnit);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			currentTexUnit = texUnit;
		}
		vertices -= w->texCoordSize;
		glTexCoordPointer(w->texCoordSize, GL_FLOAT, stride, vertices);
	}

	glDrawElements(GL_QUADS, w->indexCount, GL_UNSIGNED_SHORT,
				   w->indices);

	// disable all texture coordinate arrays except 0
	texUnit = w->texUnits;
	if (texUnit > 1)
	{
		while (--texUnit)
		{
			(*w->screen->clientActiveTexture) (GL_TEXTURE0_ARB + texUnit);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}

		(*w->screen->clientActiveTexture) (GL_TEXTURE0_ARB);
	}
}

static Bool
animPaintWindow(CompWindow * w,
				const WindowPaintAttrib * attrib,
				const CompTransform    *transform,
				Region region, unsigned int mask)
{
	Bool status;

	ANIM_SCREEN(w->screen);
	ANIM_WINDOW(w);

	// For Focus Fade
	if (aw->winToBePaintedBeforeThis)
	{
		CompWindow *w2 = aw->winToBePaintedBeforeThis;

		// Go to the bottommost window in this "focus fading chain"
		// This chain is used to handle some cases: e.g when Find dialog
		// of an app is open, both windows should be faded when the Find
		// dialog is raised.

		CompWindow *bottommost = w2;
		CompWindow *wPrev = GET_ANIM_WINDOW(bottommost, as)
			->moreToBePaintedPrev;
		while (wPrev)
		{
			bottommost = wPrev;
			wPrev = GET_ANIM_WINDOW(wPrev, as)->moreToBePaintedPrev;
		}
		// Paint each window in the chain going to the topmost
		for (w2 = bottommost; w2;
			 w2 = GET_ANIM_WINDOW(w2, as)->moreToBePaintedNext)
		{
			AnimWindow *aw2 = GET_ANIM_WINDOW(w2, as);
			if (!aw2)
				continue;

			unsigned int mask2 = mask;
			w2->indexCount = 0;
			WindowPaintAttrib wAttrib = w2->paint;

			mask2 |= PAINT_WINDOW_TRANSFORMED_MASK;

			wAttrib.xScale = 1.0f;
			wAttrib.yScale = 1.0f;

			fxFocusFadeUpdateWindowAttrib2(as, aw2, &wAttrib);

			aw2->nDrawGeometryCalls = 0;
			UNWRAP(as, w->screen, paintWindow);
			status = (*w->screen->paintWindow)
				(w2, &wAttrib, transform, region, mask2);
			WRAP(as, w->screen, paintWindow, animPaintWindow);
		}
	}

	if (aw->animRemainingTime > 0)
	{
		//printf("animPaintWindow: %X: coords: %d, %d\n",
		//       (unsigned)aw, WIN_X (w), WIN_X (w));

		if (playingPolygonEffect(as, aw))
		{
			if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
			{
				aw->curTextureFilter = w->screen->filter[WINDOW_TRANS_FILTER];
			}
			else if (mask & PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK)
			{
				aw->curTextureFilter = w->screen->filter[SCREEN_TRANS_FILTER];
			}
			else
			{
				aw->curTextureFilter = w->screen->filter[NOTHING_TRANS_FILTER];
			}
		}
		w->indexCount = 0;

		WindowPaintAttrib wAttrib = *attrib;

		//if (mask & PAINT_WINDOW_SOLID_MASK)
		//	return FALSE;

		// TODO: should only happen for distorting effects
		mask |= PAINT_WINDOW_TRANSFORMED_MASK;

		wAttrib.xScale = 1.0f;
		wAttrib.yScale = 1.0f;

		aw->nDrawGeometryCalls = 0;

		if (animEffectProperties[aw->curAnimEffect].updateWindowAttribFunc)
			animEffectProperties[aw->curAnimEffect].
					updateWindowAttribFunc(as, aw, &wAttrib);

		if (animEffectProperties[aw->curAnimEffect].prePaintWindowFunc)
			animEffectProperties[aw->curAnimEffect].
					prePaintWindowFunc(w->screen, w);

		UNWRAP(as, w->screen, paintWindow);
		status = (*w->screen->paintWindow) (w, &wAttrib, transform, region, mask);
		WRAP(as, w->screen, paintWindow, animPaintWindow);

		if (animEffectProperties[aw->curAnimEffect].postPaintWindowFunc)
			animEffectProperties[aw->curAnimEffect].
					postPaintWindowFunc(w->screen, w);
	}
	else
	{
		UNWRAP(as, w->screen, paintWindow);
		status = (*w->screen->paintWindow) (w, attrib, transform, region, mask);
		WRAP(as, w->screen, paintWindow, animPaintWindow);
	}

	return status;
}

static Bool animGetWindowIconGeometry(CompWindow * w, XRectangle * rect)
{
	Atom actual;
	int result, format;
	unsigned long n, left;
	unsigned char *data;

	ANIM_DISPLAY(w->screen->display);

	result = XGetWindowProperty(w->screen->display->display, w->id,
								ad->winIconGeometryAtom,
								0L, 4L, FALSE, XA_CARDINAL, &actual,
								&format, &n, &left, &data);

	if (result == Success && n && data)
	{
		if (n == 4)
		{
			unsigned long *geometry = (unsigned long *)data;

			rect->x = geometry[0];
			rect->y = geometry[1];
			rect->width = geometry[2];
			rect->height = geometry[3];

			XFree(data);

			return TRUE;
		}

		XFree(data);
	}

	return FALSE;
}

static void animHandleCompizEvent(CompDisplay * d, char *pluginName,
				 char *eventName, CompOption * option, int nOption)
{
	ANIM_DISPLAY(d);

	UNWRAP (ad, d, handleCompizEvent);
	(*d->handleCompizEvent) (d, pluginName, eventName, option, nOption);
	WRAP (ad, d, handleCompizEvent, animHandleCompizEvent);

	if (strcmp(pluginName, "switcher") == 0)
	{
		if (strcmp(eventName, "activate") == 0)
		{
			Window xid = getIntOptionNamed(option, nOption, "root", 0);
			CompScreen *s = findScreenAtDisplay(d, xid);

			if (s)
			{
				ANIM_SCREEN(s);
				as->switcherActive = getBoolOptionNamed(option, nOption, "active", FALSE);
			}
		}
	}
	else if (strcmp(pluginName, "group") == 0)
	{
		if (strcmp(eventName, "tabChangeActivate") == 0)
		{
			Window xid = getIntOptionNamed(option, nOption, "root", 0);
			CompScreen *s = findScreenAtDisplay(d, xid);

			if (s)
			{
				ANIM_SCREEN(s);
				as->groupTabChangeActive = getBoolOptionNamed(option, nOption, "active", FALSE);
			}
		}
	}
	/*
	else if (strcmp(pluginName, "scale") == 0)
	{
		if (strcmp(eventName, "scaleInitiateEvent") == 0)
		{
			Window xid = getIntOptionNamed(option, nOption, "root", 0);
			CompScreen *s = findScreenAtDisplay(d, xid);

			if (s)
			{
				ANIM_SCREEN(s);
				as->scaleActive = getBoolOptionNamed(option, nOption, "active", FALSE);
			}
		}
	}
	*/
}

static void
updateLastClientListStacking(CompScreen *s)
{
	ANIM_SCREEN(s);
	int n = s->nClientList;
	Window *clientListStacking = (Window *) (s->clientList + n) + n;

	if (as->nLastClientListStacking != n) // the number of windows has changed
	{
		Window *list;

		list = realloc (as->lastClientListStacking, sizeof (Window) * n);
		if (!list)
			return;

		as->lastClientListStacking  = list;
		as->nLastClientListStacking = n;
	}

	// Store new client stack listing
	memcpy(as->lastClientListStacking, clientListStacking,
		   sizeof (Window) * n);
}

static void animHandleEvent(CompDisplay * d, XEvent * event)
{
	CompWindow *w;

	ANIM_DISPLAY(d);

	switch (event->type)
	{
	case PropertyNotify:
		if (event->xproperty.atom == d->clientListStackingAtom)
		{
			CompScreen *s = findScreenAtDisplay (d, event->xproperty.window);
			updateLastClientListStacking(s);
		}
		break;
	case MapNotify:
		w = findWindowAtDisplay(d, event->xmap.window);
		if (w)
		{
			ANIM_WINDOW(w);

			if (aw->animRemainingTime > 0)
			{
				aw->state = aw->newState;
			}
			while (aw->unmapCnt)
			{
				unmapWindow(w);
				aw->unmapCnt--;
			}
		}
		break;
	case DestroyNotify:
		w = findWindowAtDisplay(d, event->xunmap.window);
		if (w)
		{
			ANIM_WINDOW(w);
			aw->destroyCnt++;
			w->destroyRefCnt++;
			addWindowDamage(w);
		}
		break;
	case UnmapNotify:
		w = findWindowAtDisplay(d, event->xunmap.window);
		if (w)
		{
			ANIM_SCREEN(w->screen);

			if (w->pendingUnmaps && onCurrentDesktop(w))	// Normal -> Iconic
			{
				ANIM_WINDOW(w);
				if (w->shaded)
				{
					// SHADE event!

					//printf("SHADE event! %X\n", (unsigned)aw);

					aw->nowShaded = TRUE;

					if (as->shadeEffect && 
					    matchEval (&as->opt[ANIM_SCREEN_OPTION_SHADE_MATCH].value.match, w))
					{
						//IPCS_SetBool(IPCS_OBJECT(w), aw->animatedAtom, TRUE);
						Bool startingNew = TRUE;

						if (aw->curWindowEvent != WindowEventNone)
						{
							if (aw->curWindowEvent != WindowEventUnshade)
								postAnimationCleanup(w, TRUE);
							else
							{
								// Play the unshade effect backwards from where it left
								aw->animRemainingTime =
										aw->animTotalTime -
										aw->animRemainingTime;

								// avoid window remains
								if (aw->animRemainingTime <= 0)
									aw->animRemainingTime = 1;

								startingNew = FALSE;
								if (aw->animOverrideProgressDir == 0)
									aw->animOverrideProgressDir = 2;
								else if (aw->animOverrideProgressDir == 1)
									aw->animOverrideProgressDir = 0;
							}
						}

						if (startingNew)
						{
							AnimEffect effectToBePlayed;
							effectToBePlayed = animGetAnimEffect(
								as->shadeEffect,
								as->shadeRandomEffects,
								as->nShadeRandomEffects,
								as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

							// handle empty random effect list
							if (effectToBePlayed == AnimEffectNone)
								break;
						
							aw->curAnimEffect = effectToBePlayed;

							aw->animTotalTime =
									as->opt[ANIM_SCREEN_OPTION_SHADE_DURATION].value.f * 1000;
							aw->animRemainingTime = aw->animTotalTime;
						}

						as->animInProgress = TRUE;
						aw->curWindowEvent = WindowEventShade;

						// Store coords in this viewport to omit 3d effect
						// painting in other viewports
						aw->lastKnownCoords.x = w->attrib.x;
						aw->lastKnownCoords.y = w->attrib.y;

						if (!animEnsureModel
							(w, WindowEventShade, aw->curAnimEffect))
						{
							postAnimationCleanup(w, TRUE);
						}

						aw->unmapCnt++;
						w->unmapRefCnt++;

						addWindowDamage(w);
					}
				}
				else if (!w->invisible
						 && as->minimizeEffect
						 && animGetWindowIconGeometry(w, &aw->icon)
						 && matchEval (&as->opt[ANIM_SCREEN_OPTION_MINIMIZE_MATCH].value.match, w))
				{
					// MINIMIZE event!

					//printf("MINIMIZE event! %X\n", (unsigned)aw);

					Bool startingNew = TRUE;

					if (aw->curWindowEvent != WindowEventNone)
					{
						if (aw->curWindowEvent != WindowEventUnminimize)
							postAnimationCleanup(w, TRUE);
						else
						{
							// Play the unminimize effect backwards from where it left
							aw->animRemainingTime =
									aw->animTotalTime - aw->animRemainingTime;

							// avoid window remains
							if (aw->animRemainingTime == 0)
								aw->animRemainingTime = 1;

							startingNew = FALSE;
							if (aw->animOverrideProgressDir == 0)
								aw->animOverrideProgressDir = 2;
							else if (aw->animOverrideProgressDir == 1)
								aw->animOverrideProgressDir = 0;
						}
					}

					if (startingNew)
					{
						AnimEffect effectToBePlayed;
						
						effectToBePlayed = animGetAnimEffect(
							as->minimizeEffect,
							as->minimizeRandomEffects,
							as->nMinimizeRandomEffects,
							as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

						// handle empty random effect list
						if (effectToBePlayed == AnimEffectNone)
							break;

						aw->curAnimEffect = effectToBePlayed;

						aw->animTotalTime =
								as->opt[ANIM_SCREEN_OPTION_MINIMIZE_DURATION].value.f * 1000;

						// allow extra time for spring damping
						if (effectToBePlayed == AnimEffectZoom ||
							effectToBePlayed == AnimEffectSidekick)
							aw->animTotalTime /= SPRING_PERCEIVED_T;

						aw->animRemainingTime = aw->animTotalTime;
					}

					aw->newState = IconicState;
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventMinimize;

					// Store coords in this viewport to omit 3d effect
					// painting in other viewports
					aw->lastKnownCoords.x = w->attrib.x;
					aw->lastKnownCoords.y = w->attrib.y;

					if (!animEnsureModel
						(w, WindowEventMinimize, aw->curAnimEffect))
					{
						postAnimationCleanup(w, TRUE);
					}
					else
					{
						/*
						if (!animGetWindowIconGeometry(w, &aw->icon))
						{
							// minimize to bottom-center if there is no window list
							aw->icon.x = w->screen->width / 2;
							aw->icon.y = w->screen->height;
							aw->icon.width = 100;
							aw->icon.height = 20;
						}
						*/
						if ((aw->curAnimEffect == AnimEffectZoom || 
							 aw->curAnimEffect == AnimEffectSidekick) &&
							(as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterOn ||
							 as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterMin))
						{
							aw->icon.x =
									WIN_X(w) +
									WIN_W(w) / 2 - aw->icon.width / 2;
							aw->icon.y =
									WIN_Y(w) +
									WIN_H(w) / 2 - aw->icon.height / 2;
						}

						aw->unmapCnt++;
						w->unmapRefCnt++;

						addWindowDamage(w);
					}
				}
			}
			else				// X -> Withdrawn
			{
				ANIM_WINDOW(w);

				AnimEffect windowsCloseEffect = AnimEffectNone;
				int whichClose = 1;	// either 1 or 2

				if (as->close1Effect && 
				    matchEval (&as->opt[ANIM_SCREEN_OPTION_CLOSE1_MATCH].value.match, w))
					windowsCloseEffect = as->close1Effect;
				else if (as->close2Effect && 
					 matchEval (&as->opt[ANIM_SCREEN_OPTION_CLOSE2_MATCH].value.match, w))
				{
					windowsCloseEffect = as->close2Effect;
					whichClose = 2;
				}
				// CLOSE event!

				if (windowsCloseEffect)
				{
					int tmpSteps = 0;

					//printf("CLOSE event! %X\n", (unsigned)aw);

					Bool startingNew = TRUE;

					if (aw->animRemainingTime > 0 &&
						aw->curWindowEvent != WindowEventCreate)
					{
						tmpSteps = aw->animRemainingTime;
						aw->animRemainingTime = 0;
					}
					if (aw->curWindowEvent != WindowEventNone)
					{
						if (aw->curWindowEvent == WindowEventCreate)
						{
							// Play the create effect backward from where it left
							aw->animRemainingTime =
									aw->animTotalTime - aw->animRemainingTime;

							// avoid window remains
							if (aw->animRemainingTime <= 0)
								aw->animRemainingTime = 1;

							startingNew = FALSE;
							if (aw->animOverrideProgressDir == 0)
								aw->animOverrideProgressDir = 2;
							else if (aw->animOverrideProgressDir == 1)
								aw->animOverrideProgressDir = 0;
						}
						else if (aw->curWindowEvent == WindowEventClose)
						{
							if (aw->animOverrideProgressDir == 2)
							{
								aw->animRemainingTime = tmpSteps;
								startingNew = FALSE;
							}
						}
						else
						{
							postAnimationCleanup(w, TRUE);
						}
					}

					if (startingNew)
					{
						AnimEffect effectToBePlayed;
						effectToBePlayed = animGetAnimEffect(
							windowsCloseEffect,
							(whichClose == 1) ? as->close1RandomEffects : 
							                    as->close2RandomEffects,
							(whichClose == 1) ? as->nClose1RandomEffects :
							                    as->nClose2RandomEffects,
							as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

						// handle empty random effect list
						if (effectToBePlayed == AnimEffectNone)
							break;
					
						aw->curAnimEffect = effectToBePlayed;

						aw->animTotalTime =	
							as->opt[whichClose == 1 ?
									ANIM_SCREEN_OPTION_CLOSE1_DURATION :
									ANIM_SCREEN_OPTION_CLOSE2_DURATION].value.f * 1000;

						// allow extra time for spring damping
						if (effectToBePlayed == AnimEffectZoom ||
							effectToBePlayed == AnimEffectSidekick)
							aw->animTotalTime /= SPRING_PERCEIVED_T;

						aw->animRemainingTime = aw->animTotalTime;
					}

					aw->state = NormalState;
					aw->newState = WithdrawnState;
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventClose;

					// Store coords in this viewport to omit 3d effect
					// painting in other viewports
					aw->lastKnownCoords.x = w->attrib.x;
					aw->lastKnownCoords.y = w->attrib.y;

					if (!animEnsureModel
						(w, WindowEventClose, aw->curAnimEffect))
					{
						postAnimationCleanup(w, TRUE);
					}
					else if (getMousePointerXY
							 (w->screen, &aw->icon.x, &aw->icon.y))
					{
						aw->icon.width = FAKE_ICON_SIZE;
						aw->icon.height = FAKE_ICON_SIZE;

						if (aw->curAnimEffect == AnimEffectMagicLamp)
							aw->icon.width = 
								MAX(aw->icon.width,
									as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_CREATE_START_WIDTH].value.i);
						else if (aw->curAnimEffect == AnimEffectMagicLampVacuum)
							aw->icon.width =
								MAX(aw->icon.width,
									as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_CREATE_START_WIDTH].value.i);

						if ((aw->curAnimEffect == AnimEffectZoom || 
							 aw->curAnimEffect == AnimEffectSidekick) &&
							(as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterOn ||
							 as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterCreate))
						{
							aw->icon.x =
									WIN_X(w) +
									WIN_W(w) / 2 - aw->icon.width / 2;
							aw->icon.y =
									WIN_Y(w) +
									WIN_H(w) / 2 - aw->icon.height / 2;
						}

						aw->unmapCnt++;
						w->unmapRefCnt++;

						addWindowDamage(w);
					}
				}
				else if ((as->create1Effect &&
						  matchEval (&as->opt[ANIM_SCREEN_OPTION_CREATE1_MATCH].value.match, w)) ||
						 (as->create2Effect &&
						  matchEval (&as->opt[ANIM_SCREEN_OPTION_CREATE2_MATCH].value.match, w)))
				{
					// stop the current animation and prevent it from rewinding

					if (aw->animRemainingTime > 0 &&
						aw->curWindowEvent != WindowEventCreate)
					{
						aw->animRemainingTime = 0;
					}
					if ((aw->curWindowEvent != WindowEventNone) &&
						(aw->curWindowEvent != WindowEventClose))
					{
						postAnimationCleanup(w, TRUE);
					}
					// set some properties to make sure this window will use the
					// correct create effect the next time it's "created"

					aw->state = NormalState;
					aw->newState = WithdrawnState;
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventClose;

					aw->unmapCnt++;
					w->unmapRefCnt++;

					addWindowDamage(w);
				}
			}
		}
		break;
	case ConfigureNotify:
		{
			XConfigureEvent *ce = &event->xconfigure;
			w = findWindowAtDisplay (d, ce->window);
			if (!w)
				break;
			if (w->prev)
			{
				if (ce->above && ce->above == w->prev->id)
				    break;
			}
			else if (ce->above == None)
				break;
			CompScreen *s = findScreenAtDisplay (d, event->xproperty.window);
			ANIM_SCREEN(s);
			int i;
			int n = s->nClientList;
			Bool winOpenedClosed = FALSE;

			Window *clientList = (Window *) (s->clientList + n);
			Window *clientListStacking = clientList + n;

			if (n != as->nLastClientListStacking)
				winOpenedClosed = TRUE;

			// if restacking occurred and not window open/close
			if (!winOpenedClosed)
			{
				// Find which window is restacked 
				// e.g. here 8507730 was raised:
				// 54526074 8507730 48234499 14680072 6291497
				// 54526074 48234499 14680072 8507730 6291497
				// compare first changed win. of row 1 with last
				// changed win. of row 2, and vica versa
				// the matching one is the restacked one
				CompWindow *wRestacked = 0;
				CompWindow *wStart = 0;
				CompWindow *wEnd = 0;
				CompWindow *wOldAbove = 0;
				CompWindow *wChangeStart = 0;
				CompWindow *wChangeEnd = 0;

				Bool raised = FALSE;
				int changeStart = -1;
				int changeEnd = -1;
				for (i = 0; i < n; i++)
				{
					CompWindow *wi =
						findWindowAtScreen (s, clientListStacking[i]);

					// skip if minimized (prevents flashing problem)
					if (!wi || !isWinVisible(wi))
						continue;

					// skip if (tabbed and) hidden by Group plugin
					if (wi->state & (CompWindowStateSkipPagerMask |
									 CompWindowStateSkipTaskbarMask))
						continue;

					// skip if shaded
					AnimWindow *awi = GET_ANIM_WINDOW(wi, as);
					if (awi && awi->nowShaded)
						continue;

					if (clientListStacking[i] !=
					    as->lastClientListStacking[i])
					{
						if (changeStart < 0)
						{
							changeStart = i;
							wChangeStart = wi; // make use of already found w
						}
						else
						{
							changeEnd = i;
							wChangeEnd = wi;
						}
					}
					else if (changeStart >= 0) // found some change earlier
						break;
				}

				// match
				if (changeStart >= 0 && changeEnd >= 0)
				{
					// if we have only 2 windows changed, 
					// choose the one clicked on (w)
					Bool preferRaise = FALSE;
					if (clientListStacking[changeEnd] ==
					    as->lastClientListStacking[changeStart] &&
						clientListStacking[changeStart] ==
					    as->lastClientListStacking[changeEnd])
						preferRaise = TRUE;
					if (preferRaise ||
						clientListStacking[changeEnd] ==
					    as->lastClientListStacking[changeStart])
					{
						// raised
						raised = TRUE;
						wRestacked = wChangeEnd;
						wStart = wChangeStart;
						wEnd = wRestacked;
						wOldAbove = wStart;
					}
					else if (clientListStacking[changeStart] ==
					    as->lastClientListStacking[changeEnd])
					{
						// lowered
						wRestacked = wChangeStart;
						wStart = wRestacked;
						wEnd = wChangeEnd;
						wOldAbove = findWindowAtScreen
							(s, as->lastClientListStacking[changeEnd+1]);
					}
					for (; wOldAbove && !isWinVisible(wOldAbove);
						 wOldAbove = wOldAbove->next)
						;
				}
				if (wRestacked && wStart && wEnd && wOldAbove)
				{
					AnimWindow *awRestacked = GET_ANIM_WINDOW(wRestacked, as);
					if (awRestacked->created)
					{
						RestackInfo *restackInfo = calloc(sizeof(RestackInfo), 1);
						if (restackInfo)
						{
							restackInfo->wRestacked = wRestacked;
							restackInfo->wStart = wStart;
							restackInfo->wEnd = wEnd;
							restackInfo->wOldAbove = wOldAbove;
							restackInfo->raised = raised;

							awRestacked->restackInfo = restackInfo;
							as->aWinWasRestackedJustNow = TRUE;
						}
					}
				}
			}
			updateLastClientListStacking(s);
		}
		break;
	default:
		break;
	}

	UNWRAP(ad, d, handleEvent);
	(*d->handleEvent) (d, event);
	WRAP(ad, d, handleEvent, animHandleEvent);

	switch (event->type)
	{
	case PropertyNotify:
		if (event->xproperty.atom == d->winActiveAtom &&
			d->activeWindow != ad->activeWindow)
		{
			ad->activeWindow = d->activeWindow;
			w = findWindowAtDisplay(d, d->activeWindow);
			
			if (w)
			{
				ANIM_SCREEN(w->screen);
				if (as->focusEffect != AnimEffectFocusFade)
					initiateFocusAnimationNonFade(w);
			}
		}
		break;
	default:
		break;
	}
}

static Bool animDamageWindowRect(CompWindow * w, Bool initial, BoxPtr rect)
{
	Bool status;

	ANIM_SCREEN(w->screen);

	if (initial)				// Unminimize or Create
	{
		ANIM_WINDOW(w);

		if (aw->state == IconicState)
		{
			if (!w->invisible && as->minimizeEffect &&
			    matchEval (&as->opt[ANIM_SCREEN_OPTION_MINIMIZE_MATCH].value.match, w))
			{
				// UNMINIMIZE event!

				//printf("UNMINIMIZE event! %X\n", (unsigned)aw);

				//IPCS_SetBool(IPCS_OBJECT(w), aw->animatedAtom, TRUE);
				Bool startingNew = TRUE;

				if (aw->curWindowEvent != WindowEventNone)
				{
					if (aw->curWindowEvent != WindowEventMinimize)
						postAnimationCleanup(w, TRUE);
					else
					{
						// Play the minimize effect backwards from where it left
						aw->animRemainingTime =
								aw->animTotalTime - aw->animRemainingTime;

						// avoid window remains
						if (aw->animRemainingTime <= 0)
							aw->animRemainingTime = 1;

						startingNew = FALSE;
						if (aw->animOverrideProgressDir == 0)
							aw->animOverrideProgressDir = 1;
						else if (aw->animOverrideProgressDir == 2)
							aw->animOverrideProgressDir = 0;
					}
				}

				Bool playEffect = TRUE;

				if (startingNew)
				{
					AnimEffect effectToBePlayed;
					effectToBePlayed = animGetAnimEffect(
						as->minimizeEffect,
						as->minimizeRandomEffects,
						as->nMinimizeRandomEffects,
						as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

					// handle empty random effect list
					if (effectToBePlayed == AnimEffectNone)
						playEffect = FALSE;

					if (playEffect)
					{
						aw->curAnimEffect = effectToBePlayed;

						aw->animTotalTime =
							as->opt[ANIM_SCREEN_OPTION_MINIMIZE_DURATION].value.f * 1000;

						// allow extra time for spring damping
						if (effectToBePlayed == AnimEffectZoom ||
							effectToBePlayed == AnimEffectSidekick)
							aw->animTotalTime /= SPRING_PERCEIVED_T;

						aw->animRemainingTime = aw->animTotalTime;
					}
				}

				if (playEffect)
				{
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventUnminimize;

					// Store coords in this viewport to omit 3d effect
					// painting in other viewports
					aw->lastKnownCoords.x = w->attrib.x;
					aw->lastKnownCoords.y = w->attrib.y;

					if (animEnsureModel
						(w, WindowEventUnminimize, aw->curAnimEffect))
					{
						if (!animGetWindowIconGeometry(w, &aw->icon))
						{
							// minimize to bottom-center if there is no window list
							aw->icon.x = w->screen->width / 2;
							aw->icon.y = w->screen->height;
							aw->icon.width = 100;
							aw->icon.height = 20;
						}
						if ((aw->curAnimEffect == AnimEffectZoom || 
							 aw->curAnimEffect == AnimEffectSidekick) &&
							(as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterOn ||
							 as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterMin))
						{
							aw->icon.x =
									WIN_X(w) + WIN_W(w) / 2 - aw->icon.width / 2;
							aw->icon.y =
									WIN_Y(w) + WIN_H(w) / 2 - aw->icon.height / 2;
						}
						addWindowDamage(w);
					}
					else
						postAnimationCleanup(w, TRUE);
				}
			}
		}
		else if (aw->nowShaded)
		{
			// UNSHADE event!
			//printf("UNSHADE event! %X\n", (unsigned)aw);

			//IPCS_SetBool(IPCS_OBJECT(w), aw->animatedAtom, TRUE);
			aw->nowShaded = FALSE;

			if (as->shadeEffect && 
			    matchEval (&as->opt[ANIM_SCREEN_OPTION_SHADE_MATCH].value.match, w))
			{
				Bool startingNew = TRUE;

				if (aw->curWindowEvent != WindowEventNone)
				{
					if (aw->curWindowEvent != WindowEventShade)
						postAnimationCleanup(w, TRUE);
					else
					{
						// Play the shade effect backwards from where it left
						aw->animRemainingTime =
								aw->animTotalTime - aw->animRemainingTime;

						// avoid window remains
						if (aw->animRemainingTime <= 0)
							aw->animRemainingTime = 1;

						startingNew = FALSE;
						if (aw->animOverrideProgressDir == 0)
							aw->animOverrideProgressDir = 1;
						else if (aw->animOverrideProgressDir == 2)
							aw->animOverrideProgressDir = 0;
					}
				}

				Bool playEffect = TRUE;

				if (startingNew)
				{
					AnimEffect effectToBePlayed;
					effectToBePlayed = animGetAnimEffect(
						as->shadeEffect,
						as->shadeRandomEffects,
						as->nShadeRandomEffects,
						as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

					// handle empty random effect list
					if (effectToBePlayed == AnimEffectNone)
						playEffect = FALSE;

					if (playEffect)
					{
						aw->curAnimEffect = effectToBePlayed;

						aw->animTotalTime =
							as->opt[ANIM_SCREEN_OPTION_SHADE_DURATION].value.f * 1000;
						aw->animRemainingTime = aw->animTotalTime;
					}
				}

				if (playEffect)
				{
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventUnshade;

					// Store coords in this viewport to omit 3d effect
					// painting in other viewports
					aw->lastKnownCoords.x = w->attrib.x;
					aw->lastKnownCoords.y = w->attrib.y;

					if (animEnsureModel(w, WindowEventUnshade, aw->curAnimEffect))
						addWindowDamage(w);
					else
						postAnimationCleanup(w, TRUE);
				}
			}
		}
		else if (aw->state != NormalState && !w->invisible)
		{
			aw->created = TRUE;
			AnimEffect windowsCreateEffect = AnimEffectNone;

			int whichCreate = 1;	// either 1 or 2

			if (as->create1Effect &&
			    matchEval (&as->opt[ANIM_SCREEN_OPTION_CREATE1_MATCH].value.match, w))
				windowsCreateEffect = as->create1Effect;
			else if (as->create2Effect &&
					 matchEval (&as->opt[ANIM_SCREEN_OPTION_CREATE2_MATCH].value.match, w))
			{
				windowsCreateEffect = as->create2Effect;
				whichCreate = 2;
			}

			if (windowsCreateEffect &&
				getMousePointerXY(w->screen, &aw->icon.x, &aw->icon.y))
			{
				// CREATE event!

				//printf("CREATE event! %X\n", (unsigned)aw);

				Bool startingNew = TRUE;

				if (aw->curWindowEvent != WindowEventNone)
				{
					if (aw->curWindowEvent != WindowEventClose)
						postAnimationCleanup(w, TRUE);
					else
					{
						// Play the close effect backwards from where it left
						aw->animRemainingTime =
								aw->animTotalTime - aw->animRemainingTime;

						// avoid window remains
						if (aw->animRemainingTime == 0)
							aw->animRemainingTime = 1;

						startingNew = FALSE;
						if (aw->animOverrideProgressDir == 0)
							aw->animOverrideProgressDir = 1;
						else if (aw->animOverrideProgressDir == 2)
							aw->animOverrideProgressDir = 0;
					}
				}

				Bool playEffect = TRUE;

				if (startingNew)
				{
					AnimEffect effectToBePlayed;
					effectToBePlayed = animGetAnimEffect(
						windowsCreateEffect,
						(whichCreate == 1) ? as->create1RandomEffects :
						                     as->create2RandomEffects,
						(whichCreate == 1) ? as->nCreate1RandomEffects :
						                     as->nCreate2RandomEffects,
						as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b);

					// handle empty random effect list
					if (effectToBePlayed == AnimEffectNone)
						playEffect = FALSE;

					if (playEffect)
					{
						aw->curAnimEffect = effectToBePlayed;

						aw->animTotalTime =
							as->opt[whichCreate == 1 ?
									ANIM_SCREEN_OPTION_CREATE1_DURATION	:
									ANIM_SCREEN_OPTION_CREATE2_DURATION].value.f * 1000;

						// allow extra time for spring damping
						if (effectToBePlayed == AnimEffectZoom ||
							effectToBePlayed == AnimEffectSidekick)
							aw->animTotalTime /= SPRING_PERCEIVED_T;

						aw->animRemainingTime = aw->animTotalTime;
					}
				}

				if (playEffect)
				{
					as->animInProgress = TRUE;
					aw->curWindowEvent = WindowEventCreate;

					aw->icon.width = FAKE_ICON_SIZE;
					aw->icon.height = FAKE_ICON_SIZE;

					if (aw->curAnimEffect == AnimEffectMagicLamp)
						aw->icon.width = 
							MAX(aw->icon.width,
								as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_CREATE_START_WIDTH].value.i);
					else if (aw->curAnimEffect == AnimEffectMagicLampVacuum)
						aw->icon.width =
							MAX(aw->icon.width,
								as->opt[ANIM_SCREEN_OPTION_MAGIC_LAMP_VACUUM_CREATE_START_WIDTH].value.i);

					aw->icon.x -= aw->icon.width / 2;
					aw->icon.y -= aw->icon.height / 2;

					if ((aw->curAnimEffect == AnimEffectZoom ||
						 aw->curAnimEffect == AnimEffectSidekick) &&
						(as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterOn ||
						 as->opt[ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER].value.i == ZoomFromCenterCreate))
					{
						aw->icon.x = WIN_X(w) + WIN_W(w) / 2 - aw->icon.width / 2;
						aw->icon.y = WIN_Y(w) + WIN_H(w) / 2 - aw->icon.height / 2;
					}
					aw->state = IconicState;	// we're doing this as a hack, it may not be necessary

					// Store coords in this viewport to omit 3d effect
					// painting in other viewports
					if (aw->lastKnownCoords.x != NOT_INITIALIZED)
					{
						aw->lastKnownCoords.x = w->attrib.x;
						aw->lastKnownCoords.y = w->attrib.y;
					}
					if (animEnsureModel(w, WindowEventCreate, aw->curAnimEffect))
						addWindowDamage(w);
					else
						postAnimationCleanup(w, TRUE);
				}
			}
		}

		aw->newState = NormalState;
	}

	UNWRAP(as, w->screen, damageWindowRect);
	status = (*w->screen->damageWindowRect) (w, initial, rect);
	WRAP(as, w->screen, damageWindowRect, animDamageWindowRect);

	return status;
}

static void animWindowResizeNotify(CompWindow * w, int dx, int dy, int dwidth, int dheight)//, Bool preview)
{
	ANIM_SCREEN(w->screen);
	ANIM_WINDOW(w);

	if (aw->polygonSet && !aw->animInitialized)
	{
		// to refresh polygon coords
		freePolygonSet(aw);
	}

	//if (!preview && (dx || dy || dwidth || dheight))
	//{
	if (aw->animRemainingTime > 0)
	{
		aw->animRemainingTime = 0;
		postAnimationCleanup(w, TRUE);
	}

	if (aw->model)
	{
		modelInitObjects(aw->model, 
						 WIN_X(w), WIN_Y(w), 
						 WIN_W(w), WIN_H(w));
	}
	//}

	aw->state = w->state;

	UNWRAP(as, w->screen, windowResizeNotify);
	(*w->screen->windowResizeNotify) (w, dx, dy, dwidth, dheight);//, preview);
	WRAP(as, w->screen, windowResizeNotify, animWindowResizeNotify);
}

static void
animWindowMoveNotify(CompWindow * w, int dx, int dy, Bool immediate)
{
	ANIM_SCREEN(w->screen);
	ANIM_WINDOW(w);

	if (!(aw->animRemainingTime > 0 &&
		  aw->curAnimEffect == AnimEffectFocusFade))
	{
		CompWindow *w2;

		if (aw->polygonSet && !aw->animInitialized)
		{
			// to refresh polygon coords
			freePolygonSet(aw);
		}
		if (aw->animRemainingTime > 0 && aw->grabbed)
		{
			aw->animRemainingTime = 0;
			if (as->animInProgress)
			{
				as->animInProgress = FALSE;
				for (w2 = w->screen->windows; w2; w2 = w2->next)
				{
					AnimWindow *aw2;

					aw2 = GET_ANIM_WINDOW(w2, as);
					if (aw2->animRemainingTime > 0)
					{
						as->animInProgress = TRUE;
						break;
					}
				}
			}
			postAnimationCleanup(w, TRUE);
		}

		if (aw->model)
		{
			modelInitObjects(aw->model, WIN_X(w), WIN_Y(w), WIN_W(w),
							 WIN_H(w));
		}
	}
	UNWRAP(as, w->screen, windowMoveNotify);
	(*w->screen->windowMoveNotify) (w, dx, dy, immediate);
	WRAP(as, w->screen, windowMoveNotify, animWindowMoveNotify);
}

static void
animWindowGrabNotify(CompWindow * w,
					 int x, int y, unsigned int state, unsigned int mask)
{
	ANIM_SCREEN(w->screen);
	ANIM_WINDOW(w);

	aw->grabbed = TRUE;

	UNWRAP(as, w->screen, windowGrabNotify);
	(*w->screen->windowGrabNotify) (w, x, y, state, mask);
	WRAP(as, w->screen, windowGrabNotify, animWindowGrabNotify);
}

static void animWindowUngrabNotify(CompWindow * w)
{
	ANIM_SCREEN(w->screen);
	ANIM_WINDOW(w);

	aw->grabbed = FALSE;

	UNWRAP(as, w->screen, windowUngrabNotify);
	(*w->screen->windowUngrabNotify) (w);
	WRAP(as, w->screen, windowUngrabNotify, animWindowUngrabNotify);
}

static Bool
animPaintOutput(CompScreen * s,
				const ScreenPaintAttrib * sAttrib,
				const CompTransform    *transform,
				Region region, CompOutput *output, 
				unsigned int mask)
{
	Bool status;

	ANIM_SCREEN(s);

	if (as->animInProgress)
	{
		// Find out if an animation running now uses depth test
		Bool depthUsed = FALSE;
		CompWindow *w;
		for (w = s->windows; w; w = w->next)
		{
			ANIM_WINDOW(w);
			if (aw->animRemainingTime > 0 &&
				aw->polygonSet &&
				aw->polygonSet->doDepthTest)
			{
				depthUsed = TRUE;
				break;
			}
		}
		if (depthUsed)
		{
			glClearDepth(1000.0f);
			glClear(GL_DEPTH_BUFFER_BIT);
		}
		mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;
	}
	UNWRAP(as, s, paintOutput);
 	status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
	WRAP(as, s, paintOutput, animPaintOutput);

	CompWindow *w;
	if (as->aWinWasRestackedJustNow)
	{
		// Reset wasRestackedJustNow for each window
		for (w = s->windows; w; w = w->next)
		{
			ANIM_WINDOW(w);
			if (aw->restackInfo)
			{
				free(aw->restackInfo);
				aw->restackInfo = NULL;
			}
		}
		as->aWinWasRestackedJustNow = FALSE;
	}
	if (as->markAllWinCreatedCountdown > 0)
	{
		if (as->markAllWinCreatedCountdown == 1)
		{
			// Mark all windows as "created"
			for (w = s->windows; w; w = w->next)
			{
				ANIM_WINDOW(w);
				aw->created = TRUE;
			}
		}
		as->markAllWinCreatedCountdown--;
	}
	return status;
}

static Bool animInitDisplay(CompPlugin * p, CompDisplay * d)
{
	AnimDisplay *ad;

	ad = calloc(1, sizeof(AnimDisplay));
	if (!ad)
		return FALSE;

	ad->screenPrivateIndex = allocateScreenPrivateIndex(d);
	if (ad->screenPrivateIndex < 0)
	{
		free(ad);
		return FALSE;
	}

	ad->wmHintsAtom = XInternAtom(d->display, "WM_HINTS", FALSE);
	ad->winIconGeometryAtom =
			XInternAtom(d->display, "_NET_WM_ICON_GEOMETRY", 0);

	WRAP(ad, d, handleEvent, animHandleEvent);
	WRAP(ad, d, handleCompizEvent, animHandleCompizEvent);

	d->privates[displayPrivateIndex].ptr = ad;

	return TRUE;
}

static void animFiniDisplay(CompPlugin * p, CompDisplay * d)
{
	ANIM_DISPLAY(d);

	freeScreenPrivateIndex(d, ad->screenPrivateIndex);

	UNWRAP(ad, d, handleCompizEvent);
	UNWRAP(ad, d, handleEvent);

	free(ad);
}

static Bool animInitScreen(CompPlugin * p, CompScreen * s)
{
	AnimScreen *as;
	
	ANIM_DISPLAY(s->display);

	as = calloc(1, sizeof(AnimScreen));
	if (!as)
		return FALSE;

	if (!compInitScreenOptionsFromMetadata (s,
											&animMetadata,
											animScreenOptionInfo,
											as->opt,
											ANIM_SCREEN_OPTION_NUM))
	{
		free (as);
		return FALSE;
	}

	as->windowPrivateIndex = allocateWindowPrivateIndex(s);
	if (as->windowPrivateIndex < 0)
	{
		compFiniScreenOptions (s, as->opt, ANIM_SCREEN_OPTION_NUM);
		free(as);
		return FALSE;
	}

	as->animInProgress = FALSE;

	as->minimizeEffect = animEffectFromString (
			&as->opt[ANIM_SCREEN_OPTION_MINIMIZE_EFFECT].value,
			minimizeEffectType, NUM_MINIMIZE_EFFECT);
	as->create1Effect = animEffectFromString (
			&as->opt[ANIM_SCREEN_OPTION_CREATE1_EFFECT].value,
			closeEffectType, NUM_CLOSE_EFFECT);
	as->create2Effect = animEffectFromString (
			&as->opt[ANIM_SCREEN_OPTION_CREATE2_EFFECT].value,
			closeEffectType, NUM_CLOSE_EFFECT);
	as->close1Effect = animEffectFromString (
			&as->opt[ANIM_SCREEN_OPTION_CLOSE1_EFFECT].value,
			closeEffectType, NUM_CLOSE_EFFECT);
	as->close2Effect = animEffectFromString (
			&as->opt[ANIM_SCREEN_OPTION_CLOSE2_EFFECT].value,
			closeEffectType, NUM_CLOSE_EFFECT);
	as->focusEffect = animEffectFromString (
			&as->opt[ANIM_SCREEN_OPTION_FOCUS_EFFECT].value,
			focusEffectType, NUM_FOCUS_EFFECT);
	as->shadeEffect = animEffectFromString (
			&as->opt[ANIM_SCREEN_OPTION_SHADE_EFFECT].value,
			shadeEffectType, NUM_SHADE_EFFECT);
	
	animStoreRandomEffectList (&as->opt[ANIM_SCREEN_OPTION_MINIMIZE_RANDOM_EFFECTS].value,
							   minimizeEffectType,
							   NUM_MINIMIZE_EFFECT,
							   as->minimizeRandomEffects,
							   &as->nMinimizeRandomEffects);
	animStoreRandomEffectList (&as->opt[ANIM_SCREEN_OPTION_CLOSE1_RANDOM_EFFECTS].value,
							   closeEffectType,
							   NUM_CLOSE_EFFECT,
							   as->close1RandomEffects,
							   &as->nClose1RandomEffects);
	animStoreRandomEffectList (&as->opt[ANIM_SCREEN_OPTION_CLOSE2_RANDOM_EFFECTS].value,
							   closeEffectType,
							   NUM_CLOSE_EFFECT,
							   as->close2RandomEffects,
							   &as->nClose2RandomEffects);
	animStoreRandomEffectList (&as->opt[ANIM_SCREEN_OPTION_CREATE1_RANDOM_EFFECTS].value,
							   closeEffectType,
							   NUM_CLOSE_EFFECT,
							   as->create1RandomEffects,
							   &as->nCreate1RandomEffects);
	animStoreRandomEffectList (&as->opt[ANIM_SCREEN_OPTION_CREATE2_RANDOM_EFFECTS].value,
							   closeEffectType,
							   NUM_CLOSE_EFFECT,
							   as->create2RandomEffects,
							   &as->nCreate2RandomEffects);
	animStoreRandomEffectList (&as->opt[ANIM_SCREEN_OPTION_SHADE_RANDOM_EFFECTS].value,
							   shadeEffectType,
							   NUM_SHADE_EFFECT,
							   as->shadeRandomEffects,
							   &as->nShadeRandomEffects);

	as->switcherActive = FALSE;
	as->groupTabChangeActive = FALSE;
	//as->scaleActive = FALSE;

	WRAP(as, s, preparePaintScreen, animPreparePaintScreen);
	WRAP(as, s, donePaintScreen, animDonePaintScreen);
	WRAP(as, s, paintOutput, animPaintOutput);
	WRAP(as, s, paintWindow, animPaintWindow);
	WRAP(as, s, damageWindowRect, animDamageWindowRect);
	WRAP(as, s, addWindowGeometry, animAddWindowGeometry);
	WRAP(as, s, drawWindowTexture, animDrawWindowTexture);
	//WRAP(as, s, drawWindowGeometry, animDrawWindowGeometry);
	WRAP(as, s, windowResizeNotify, animWindowResizeNotify);
	WRAP(as, s, windowMoveNotify, animWindowMoveNotify);
	WRAP(as, s, windowGrabNotify, animWindowGrabNotify);
	WRAP(as, s, windowUngrabNotify, animWindowUngrabNotify);

	as->markAllWinCreatedCountdown = 5; // start countdown

	s->privates[ad->screenPrivateIndex].ptr = as;

	return TRUE;
}

static void animFiniScreen(CompPlugin * p, CompScreen * s)
{
	ANIM_SCREEN(s);

	freeWindowPrivateIndex(s, as->windowPrivateIndex);

	if (as->lastClientListStacking)
		free(as->lastClientListStacking);

	UNWRAP(as, s, preparePaintScreen);
	UNWRAP(as, s, donePaintScreen);
	UNWRAP(as, s, paintOutput);
	UNWRAP(as, s, paintWindow);
	UNWRAP(as, s, damageWindowRect);
	UNWRAP(as, s, addWindowGeometry);
	UNWRAP(as, s, drawWindowTexture);
	//UNWRAP(as, s, drawWindowGeometry);
	UNWRAP(as, s, windowResizeNotify);
	UNWRAP(as, s, windowMoveNotify);
	UNWRAP(as, s, windowGrabNotify);
	UNWRAP(as, s, windowUngrabNotify);

	compFiniScreenOptions (s, as->opt, ANIM_SCREEN_OPTION_NUM);

	free(as);
}

static Bool animInitWindow(CompPlugin * p, CompWindow * w)
{
	AnimWindow *aw;

	ANIM_SCREEN(w->screen);

	aw = calloc(1, sizeof(AnimWindow));
	if (!aw)
		return FALSE;

	//aw->animatedAtom = IPCS_GetAtom(IPCS_OBJECT(w), IPCS_BOOL,
	//								"IS_ANIMATED", TRUE);
	aw->model = 0;
	aw->state = w->state;
	aw->animRemainingTime = 0;
	aw->animInitialized = FALSE;
	aw->curAnimEffect = AnimEffectNone;
	aw->curWindowEvent = WindowEventNone;
	aw->animOverrideProgressDir = 0;
	w->indexCount = 0;

	aw->polygonSet = NULL;
	aw->lastKnownCoords.x = NOT_INITIALIZED;
	aw->lastKnownCoords.y = NOT_INITIALIZED;

	aw->unmapCnt = 0;
	aw->destroyCnt = 0;

	aw->grabbed = FALSE;

	aw->useDrawRegion = FALSE;
	aw->drawRegion = NULL;

	if (w->shaded)
	{
		aw->state = aw->newState = NormalState;
		aw->nowShaded = TRUE;
	}
	else
	{
		aw->state = aw->newState = animGetWindowState(w);
		aw->nowShaded = FALSE;
	}

	w->privates[as->windowPrivateIndex].ptr = aw;

	return TRUE;
}

static void animFiniWindow(CompPlugin * p, CompWindow * w)
{
	ANIM_WINDOW(w);

	postAnimationCleanup(w, FALSE);

	if (aw->model)
	{
		if (aw->model->magicLampWaves)
			free(aw->model->magicLampWaves);
		aw->model->magicLampWaves = 0;
		free(aw->model->objects);
		aw->model->objects = 0;
		free(aw->model);
		aw->model = 0;
	}
	if (aw->restackInfo)
	{
		free(aw->restackInfo);
		aw->restackInfo = NULL;
	}

	while (aw->unmapCnt--)
		unmapWindow(w);

	free(aw);
}

static Bool animInit(CompPlugin * p)
{
	if (!compInitPluginMetadataFromInfo (&animMetadata,
										  p->vTable->name,
										   0, 0,
										    animScreenOptionInfo,
											 ANIM_SCREEN_OPTION_NUM))
		return FALSE;
		
	displayPrivateIndex = allocateDisplayPrivateIndex();
	if (displayPrivateIndex < 0)
	{
		compFiniMetadata (&animMetadata);
		return FALSE;
	}

	compAddMetadataFromFile (&animMetadata, p->vTable->name);

	animEffectPropertiesTmp = animEffectProperties;
	return TRUE;
}

static void animFini(CompPlugin * p)
{
	freeDisplayPrivateIndex(displayPrivateIndex);
	compFiniMetadata (&animMetadata);
}

static int
animGetVersion (CompPlugin *plugin,
		int	   version)
{
    return ABIVERSION;
}

static CompMetadata *
animGetMetadata (CompPlugin *plugin)
{
	return &animMetadata;
}

CompPluginVTable animVTable = {
	"animation",
	animGetVersion,
	animGetMetadata,
	animInit,
	animFini,
	animInitDisplay,
	animFiniDisplay,
	animInitScreen,
	animFiniScreen,
	animInitWindow,
	animFiniWindow,
	0,
	0,
	animGetScreenOptions,
	animSetScreenOptions,
	0,
	0,
	0,
	0
};

CompPluginVTable *getCompPluginInfo(void)
{
	return &animVTable;
}

