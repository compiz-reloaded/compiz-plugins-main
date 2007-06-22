#include "animation-internal.h"

// =====================  Effect: Dodge  =========================

void
fxDodgeAnimStep (CompScreen * s, CompWindow * w, float time)
{
	defaultAnimStep(s, w, time);

	ANIM_WINDOW(w);
	aw->transformProgress = 0;

	float forwardProgress = defaultAnimProgress(aw);
	if (forwardProgress > aw->transformStartProgress)
	{
		aw->transformProgress = 
			(forwardProgress - aw->transformStartProgress) /
			(1 - aw->transformStartProgress);
	}

	if (!aw->isDodgeSubject)
	{
		// Update dodge amount if subject window is moved during dodge
		int newDodgeAmount =
			DODGE_AMOUNT(aw->dodgeDirection, aw->dodgeSubjectWin, w);

		// Only update if amount got larger
		if (abs(newDodgeAmount) > abs(aw->dodgeMaxAmount))
			aw->dodgeMaxAmount = newDodgeAmount;
	}
}

void
fxDodgeUpdateWindowTransform
	(CompScreen *s, CompWindow *w, CompTransform *wTransform)
{
	ANIM_WINDOW(w);

	float amount = sin(M_PI * aw->transformProgress) * aw->dodgeMaxAmount;

	if (aw->dodgeDirection > 1) // if x axis
		matrixTranslate (wTransform, amount, 0.0f, 0.0f);
	else
		matrixTranslate (wTransform, 0.0f, amount, 0.0f);
}

void
fxDodgePostPreparePaintScreen(CompScreen *s, CompWindow *w)
{
	ANIM_SCREEN(s);
	ANIM_WINDOW(w);

	// Only dodge subjects should be processed here
	if (!aw->isDodgeSubject)
		return;

	if (!aw->restackInfo)
		return;

	// Dodgy window
	CompWindow *dw;
	AnimWindow *adw = NULL;
	for (dw = aw->dodgeChainStart; dw; dw = adw->dodgeChainNext)
	{
		adw = GET_ANIM_WINDOW(dw, as);
		if (!adw)
			break;

		// find the first dodging window that hasn't yet
		// reached 50% progress yet. The subject window should be
		// painted right behind that one (or right in front of it if
		// the subject window is being lowered).
		if (!(adw->transformProgress > 0.5f))
			break;
	}
	AnimWindow *awOldHost = NULL;
	
	if (aw->restackInfo->raised &&
		dw != aw->winThisIsPaintedBefore) // w's host is changing
	{
		if (aw->winThisIsPaintedBefore)
		{
			awOldHost = GET_ANIM_WINDOW(aw->winThisIsPaintedBefore, as);

			// Clear old host
			awOldHost->winToBePaintedBeforeThis = NULL;
		}
		if (dw) // if a dodgy win. is still at <0.5 progress
		{
			// Put subject right behind adw (new host)
			adw->winToBePaintedBeforeThis = w;
		}
		// otherwise all dodgy win.s have passed 0.5 progress

		aw->winThisIsPaintedBefore = dw; // dw can be null, which is ok
	}
	else if (!aw->restackInfo->raised)
	{
		// Put subject right in front of dw
		// But we need to find the dodgy window above dw
		// (since we need to put subject *behind* another one)

		CompWindow *wDodgeChainAbove = NULL;

		if (dw && adw) // if a dodgy win. is still at <0.5 progress
		{
			if (adw->dodgeChainPrev)
				wDodgeChainAbove = adw->dodgeChainPrev;
			else
				wDodgeChainAbove = aw->restackInfo->wOldAbove;

			if (!wDodgeChainAbove)
				compLogMessage (s->display, "animation", CompLogLevelError,
								"%s: error at line %d", __FILE__, __LINE__);
			else if (aw->winThisIsPaintedBefore !=
					 wDodgeChainAbove) // w's host is changing
			{
				AnimWindow *adw2 = GET_ANIM_WINDOW(wDodgeChainAbove, as);

				// Put subject right behind adw2 (new host)
				adw2->winToBePaintedBeforeThis = w;
			}
		}
		if (aw->winThisIsPaintedBefore &&
			aw->winThisIsPaintedBefore != wDodgeChainAbove)
		{
			awOldHost = GET_ANIM_WINDOW(aw->winThisIsPaintedBefore, as);

			// Clear old host
			awOldHost->winToBePaintedBeforeThis = NULL;
		}
		// otherwise all dodgy win.s have passed 0.5 progress

		// wDodgeChainAbove can be null, which is ok
		aw->winThisIsPaintedBefore = wDodgeChainAbove;
	}
}


