#include "animation-internal.h"
// =====================  Effect: Horizontal Folds  =========================


void
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

void
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

void
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
