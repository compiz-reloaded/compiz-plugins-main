#include "animation-internal.h"
// =====================  Effect: Roll Up  =========================

void
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

void
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

void fxRollUpModelStep(CompScreen * s, CompWindow * w, float time)
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

