#include "animation.h"

// =====================  Effect: Fade  =========================

void defaultAnimInit(CompScreen * s, CompWindow * w)
{
	ANIM_WINDOW(w);
	ANIM_SCREEN(s);

	// store window opacity
	aw->storedOpacity = w->paint.opacity;

	aw->timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
					as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);
}

void
fxFadeUpdateWindowAttrib(AnimScreen * as,
						 AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	float forwardProgress = defaultAnimProgress(aw);

	wAttrib->opacity = (GLushort) (aw->storedOpacity * (1 - forwardProgress));
}


