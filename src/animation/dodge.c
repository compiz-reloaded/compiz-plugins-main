#include "animation-internal.h"

// =====================  Effect: Dodge  =========================

void
fxDodgeProcessSubject (CompWindow *wCur, Region wRegion, Region dodgeRegion)
{
	XRectangle rect;
	rect.x = WIN_X(wCur);
	rect.y = WIN_Y(wCur);
	rect.width = WIN_W(wCur);
	rect.height = WIN_H(wCur);
	Region wCurRegion = XCreateRegion();
	Region intersectionRegion = XCreateRegion();
	XUnionRectWithRegion(&rect, &emptyRegion, wCurRegion);
	XIntersectRegion(wRegion, wCurRegion,
					 intersectionRegion);
	if (!XEmptyRegion(intersectionRegion))
		XUnionRegion(dodgeRegion, wCurRegion, dodgeRegion);
}

void
fxDodgeFindDodgeBox (CompWindow *w, XRectangle *dodgeBox)
{
	ANIM_SCREEN(w->screen);
	ANIM_WINDOW(w);

	// Find the box to be dodged, it can contain multiple windows
	// when there are dialog/utility windows of subject windows
	// (stacked in the moreToBePaintedNext chain)
	// Then this would be a bounding box of the subject windows
	// intersecting with dodger.
	Region wRegion = XCreateRegion();
	Region dodgeRegion = XCreateRegion();

	XRectangle rect;
	rect.x = WIN_X(w);
	rect.y = WIN_Y(w);
	rect.width = WIN_W(w);
	rect.height = WIN_H(w);
	XUnionRectWithRegion(&rect, &emptyRegion, wRegion);

	AnimWindow *awCur;
	CompWindow *wCur = aw->dodgeSubjectWin;
	for (; wCur; wCur = awCur->moreToBePaintedNext)
	{
		fxDodgeProcessSubject(wCur, wRegion, dodgeRegion);
		awCur = GET_ANIM_WINDOW(wCur, as);
	}

	AnimWindow *awSubj = GET_ANIM_WINDOW(aw->dodgeSubjectWin, as);
	wCur = awSubj->moreToBePaintedPrev;
	for (; wCur; wCur = awCur->moreToBePaintedPrev)
	{
		fxDodgeProcessSubject(wCur, wRegion, dodgeRegion);
		awCur = GET_ANIM_WINDOW(wCur, as);
	}

	XClipBox(dodgeRegion, dodgeBox);
}

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
		XRectangle dodgeBox;
		fxDodgeFindDodgeBox (w, &dodgeBox);

		// Update dodge amount if subject window is moved during dodge
		int newDodgeAmount =
			DODGE_AMOUNT_BOX(dodgeBox, w, aw->dodgeDirection);

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

	if (aw->isDodgeSubject)
		return;
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

	if (aw->skipPostPrepareScreen)
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
			// Clear old host
			awOldHost = GET_ANIM_WINDOW(aw->winThisIsPaintedBefore, as);			
			awOldHost->winToBePaintedBeforeThis = NULL;
		}
		if (dw) // if a dodgy win. is still at <0.5 progress
		{
			// Put subject right behind adw (new host)
			adw->winToBePaintedBeforeThis = w;
		}
		// otherwise all dodgy win.s have passed 0.5 progress

		CompWindow *wCur = w;
		while (wCur)
		{
			AnimWindow *awCur = GET_ANIM_WINDOW(wCur, as);
			awCur->winThisIsPaintedBefore = dw; // dw can be null, which is ok
			wCur = awCur->moreToBePaintedNext;
		}
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

