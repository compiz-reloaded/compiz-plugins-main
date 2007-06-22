#include "animation-internal.h"
void fxExplode3DInit(CompScreen * s, CompWindow * w)
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
		break;
	case PolygonTessHex:
		if (!tessellateIntoHexagons(w, 
			as->opt[ANIM_SCREEN_OPTION_EXPLODE3D_GRIDSIZE_X].value.i,
			as->opt[ANIM_SCREEN_OPTION_EXPLODE3D_GRIDSIZE_Y].value.i,
			as->opt[ANIM_SCREEN_OPTION_EXPLODE3D_THICKNESS].value.f))
			return;
		break;
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

