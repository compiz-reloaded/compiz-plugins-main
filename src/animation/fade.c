#include "animation-internal.h"

// =====================  Effect: Fade  =========================



void
fxFadeUpdateWindowAttrib(AnimScreen * as,
						 AnimWindow * aw, WindowPaintAttrib * wAttrib)
{
	float forwardProgress = defaultAnimProgress(aw);

	wAttrib->opacity = (GLushort) (aw->storedOpacity * (1 - forwardProgress));
}


