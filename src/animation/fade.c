<<<<<<< HEAD:fade.c
#include "animation.h"
=======
#include "animation-internal.h"
>>>>>>> 1d600624bf9a0956375a82cfdd1f99da511af55f:fade.c

// =====================  Effect: Fade  =========================



void
fxFadeUpdateWindowAttrib(AnimScreen * as,
						 AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	float forwardProgress = defaultAnimProgress(aw);

	wAttrib->opacity = (GLushort) (aw->storedOpacity * (1 - forwardProgress));
}


