#include "animation-internal.h"
void fxLeafSpread3DInit(CompScreen * s, CompWindow * w)
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

