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


typedef enum
{
	AnimDirectionDown = 0,
	AnimDirectionUp,
	AnimDirectionLeft,
	AnimDirectionRight,
	AnimDirectionRandom,
	AnimDirectionAuto
} AnimDirection;
#define LAST_ANIM_DIRECTION 5

typedef enum
{
	ZoomFromCenterOff = 0,
	ZoomFromCenterMin,
	ZoomFromCenterCreate,
	ZoomFromCenterOn
} ZoomFromCenter;
#define LAST_ZOOM_FROM_CENTER 3

// Polygon tesselation type: Rectangular, Hexagonal
typedef enum
{
	PolygonTessRect = 0,
	PolygonTessHex
} PolygonTess;
#define LAST_POLYGON_TESS 1

typedef enum
{
	AnimEffectNone = 0,
	AnimEffectRandom,
	AnimEffectBeamUp,
	AnimEffectBurn,
	AnimEffectCurvedFold,
	AnimEffectDodge,
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

#define RANDOM_EFFECT_OFFSET 2 /* skip none and random */

/* These macros definitions _always_ must match the number
   of array items in the corresponding EffectType structure
   in animation.c.
   LAST_*_EFFECT always must be NUM_*_EFFECT - 1
   LAST_RANDOM_*_EFFECT always must be LAST_*_EFFECT - RANDOM_EFFECT_OFFSET
   */

#define NUM_MINIMIZE_EFFECT 17
#define LAST_MINIMIZE_EFFECT 16
#define LAST_RANDOM_MINIMIZE_EFFECT 14

#define NUM_CLOSE_EFFECT 19
#define LAST_CLOSE_EFFECT 18
#define LAST_RANDOM_CLOSE_EFFECT 16

#define NUM_FOCUS_EFFECT 4
#define LAST_FOCUS_EFFECT 3

#define NUM_SHADE_EFFECT 5
#define LAST_SHADE_EFFECT 4
#define LAST_RANDOM_SHADE_EFFECT 2


typedef struct RestackInfo
{
	CompWindow *wRestacked, *wStart, *wEnd, *wOldAbove;
	Bool raised;
} RestackInfo;

extern int animDisplayPrivateIndex;
extern CompMetadata animMetadata;

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
	ANIM_SCREEN_OPTION_DODGE_GAP_RATIO,
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
	Bool scaleActive;

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

	float transformStartProgress;
	float transformProgress;

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
	Bool configureNotified;     // was configureNotified before restack check

	// for dodge
	Bool isDodgeSubject;			// TRUE if this window is the cause of dodging
	CompWindow *dodgeSubjectWin;	// The window being dodged
	float dodgeMaxAmount;		// max # pixels it should dodge
								// (neg. values dodge left)
	int dodgeOrder;				// dodge order (used temporarily)
	Bool dodgeDirection;		// 0: up, down, left, right

	CompWindow *dodgeChainStart;// for the subject window
	CompWindow *dodgeChainPrev;	// for dodging windows
	CompWindow *dodgeChainNext;	// for dodging windows
	Bool skipPostPrepareScreen;
	Bool drawnOnHostSkip;
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
	void (*updateWindowTransformFunc)
		(CompScreen *, CompWindow *, CompTransform *);
	void (*postPreparePaintScreenFunc) (CompScreen *, CompWindow *);
} AnimEffectProperties;

AnimEffectProperties *animEffectPropertiesTmp;

#define GET_ANIM_DISPLAY(d)                                       \
    ((AnimDisplay *) (d)->privates[animDisplayPrivateIndex].ptr)

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

#define sigmoid(fx) (1.0f/(1.0f+exp(-5.0f*2*((fx)-0.5))))
#define sigmoid2(fx, s) (1.0f/(1.0f+exp(-(s)*2*((fx)-0.5))))

// up, down, left, right
#define DODGE_AMOUNT(w, dw, dir) \
	((dir) == 0 ? WIN_Y(w) - (WIN_Y(dw) + WIN_H(dw)) : \
	 (dir) == 1 ? (WIN_Y(w) + WIN_H(w)) - WIN_Y(dw) : \
	 (dir) == 2 ? WIN_X(w) - (WIN_X(dw) + WIN_W(dw)) : \
	              (WIN_X(w) + WIN_W(w)) - WIN_X(dw))

// up, down, left, right
#define DODGE_AMOUNT_BOX(box, dw, dir) \
	((dir) == 0 ? (box).y - (WIN_Y(dw) + WIN_H(dw)) : \
	 (dir) == 1 ? ((box).y + (box).height) - WIN_Y(dw) : \
	 (dir) == 2 ? (box).x - (WIN_X(dw) + WIN_W(dw)) : \
	              ((box).x + (box).width) - WIN_X(dw))

// spring crossing x (second time it spring movement reaches target)
#define SPRING_CROSSING_X 0.6184f
#define SPRING_PERCEIVED_T 0.5f
//0.52884f

/*
 * Function prototypes
 *
 */

/* animation.c*/
 
 void
modelInitObjects (Model * model,
				  int x, int y,
				  int width, int height);
 
void
postAnimationCleanup (CompWindow * w,
					  Bool resetAnimation);
 
void
modelCalcBounds (Model * model);
 
float
defaultAnimProgress (AnimWindow * aw);
 
float
decelerateProgressCustom (float progress,
						  float minx, float maxx);
 
float
decelerateProgress2 (float progress);
 
void
applyTransformToObject (Object *obj, GLfloat *mat);
 
void
obtainTransformMatrix (CompScreen *s, GLfloat *mat,
					   float rotAngle, Vector3d rotAxis,
					   Point3d translation);
 
void polygonsAnimStep (CompScreen * s,
					   CompWindow * w,
					   float time);

AnimDirection 
getAnimationDirection (CompWindow * w,
					   CompOptionValue *value,
					   Bool openDir);

void 
defaultAnimStep (CompScreen * s,
				 CompWindow * w,
				 float time);

void
defaultAnimInit (CompScreen * s,
				 CompWindow * w);

void
animDrawWindowGeometry(CompWindow * w);


/* beamup.c */

void
fxBeamupUpdateWindowAttrib (AnimScreen *as,
							AnimWindow *aw,
							WindowPaintAttrib *wAttrib);

void
fxBeamUpModelStep (CompScreen *s,
				   CompWindow *w,
				   float time);

void fxBeamUpInit (CompScreen *s,
				   CompWindow *w);


/* burn.c */

void
fxBurnModelStep (CompScreen *s,
				 CompWindow *w,
				 float time);

void fxBurnInit (CompScreen *s,
				 CompWindow *w);


/* curvedfold.c */

void
fxCurvedFoldModelStep (CompScreen *s,
					   CompWindow *w,
					   float time);


/* dodge.c */

void
fxDodgePostPreparePaintScreen (CompScreen *s,
							   CompWindow *w);

void
fxDodgeUpdateWindowTransform (CompScreen *s,
							  CompWindow *w,
							  CompTransform *wTransform);

void
fxDodgeAnimStep (CompScreen *s,
				 CompWindow *w,
				 float time);

/* domino.c */

void
fxDomino3DInit (CompScreen *s,
				CompWindow *w);


/* dream.c */

void
fxDreamModelStep (CompScreen * s,
				  CompWindow * w,
				  float time);

void
fxDreamUpdateWindowAttrib(AnimScreen * as,
						  AnimWindow * aw, 
						  WindowPaintAttrib * wAttrib);


/* explode3d.c */

void
fxExplode3DInit (CompScreen *s,
				 CompWindow *w);


/* fade.c */

void
fxFadeUpdateWindowAttrib (AnimScreen *as,
						  AnimWindow *aw,
						  WindowPaintAttrib *wAttrib);


/* focusfade.c */

void
fxFocusFadeUpdateWindowAttrib2 (AnimScreen *as,
								AnimWindow *aw,
								WindowPaintAttrib *wAttrib);

void
fxFocusFadeUpdateWindowAttrib (AnimScreen *as,
							   AnimWindow *aw,
							   WindowPaintAttrib *wAttrib);


/* glide.c */

void
fxGlideInit (CompScreen *s,
			 CompWindow *w);

void
fxGlideUpdateWindowAttrib (AnimScreen *as,
						   AnimWindow *aw,
						   WindowPaintAttrib *wAttrib);

void
fxGlideAnimStep (CompScreen *s,
				 CompWindow *w,
				 float time);

float
fxGlideAnimProgress (AnimWindow *aw);

void
fxGlideGetParams (AnimScreen *as,
				  AnimWindow *aw,
				  float *finalDistFac,
				  float *finalRotAng,
				  float *thickness);


/* horizontalfold.c */

void
fxHorizontalFoldsModelStep (CompScreen *s,
							CompWindow *w,
							float time);

void
fxHorizontalFoldsInitGrid (AnimScreen *as,
						   WindowEvent forWindowEvent,
						   int *gridWidth,
						   int *gridHeight);


/* leafspread.c */

void
fxLeafSpread3DInit (CompScreen *s,
					CompWindow *w);


/* magiclamp.c */

void
fxMagicLampInitGrid(AnimScreen * as,
					 WindowEvent forWindowEvent,
					 int *gridWidth, 
					 int *gridHeight);

void
fxMagicLampVacuumInitGrid (AnimScreen * as,
						   WindowEvent forWindowEvent,
						   int *gridWidth, 
						   int *gridHeight);

void
fxMagicLampInit (CompScreen * s,
				 CompWindow * w);

void
fxMagicLampModelStep (CompScreen * s,
					  CompWindow * w,
					  float time);


/* particle.c */

void
initParticles (int numParticles,
			   ParticleSystem * ps);

void
drawParticles (CompScreen * s,
			   CompWindow * w,
			   ParticleSystem * ps);

void
updateParticles (ParticleSystem * ps,
				 float time);

void
finiParticles (ParticleSystem * ps);

void
drawParticleSystems (CompScreen *s,
					 CompWindow *w);


/* polygon.c */

Bool
tessellateIntoRectangles (CompWindow * w,
						  int gridSizeX,
						  int gridSizeY,
						  float thickness);
 
 Bool
tessellateIntoHexagons (CompWindow * w,
						int gridSizeX,
						int gridSizeY,
						float thickness);

 void
polygonsStoreClips (CompScreen * s,
					CompWindow * w,
					int nClip, BoxPtr pClip,
					int nMatrix, CompMatrix * matrix);
 
void
polygonsDrawCustomGeometry (CompScreen * s,
							CompWindow * w);

void
polygonsPrePaintWindow (CompScreen * s,
						CompWindow * w);
 
void
polygonsPostPaintWindow (CompScreen * s,
						 CompWindow * w);
 
void
freePolygonSet (AnimWindow * aw);
 
 
/* rollup.c */
 
void
fxRollUpModelStep (CompScreen *s,
				   CompWindow *w,
				   float time);
 
void fxRollUpInitGrid (AnimScreen *as,
					   WindowEvent forWindowEvent,
					   int *gridWidth,
					   int *gridHeight);
 
 
/* wave.c */
 
void
fxWaveModelStep (CompScreen * s,
				 CompWindow * w,
				 float time);


/* zoomside.c */

void
fxZoomUpdateWindowAttrib (AnimScreen *as,
						  AnimWindow *aw,
						  WindowPaintAttrib *wAttrib);

void
fxZoomUpdateWindowTransform(CompScreen *s,
							CompWindow *w,
							CompTransform *wTransform);

void
fxSidekickInit (CompScreen *s,
				CompWindow *w);

