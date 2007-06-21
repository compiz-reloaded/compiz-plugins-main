void
fxMagicLampInitGrid(AnimScreen * as,
					 WindowEvent forWindowEvent,
					 int *gridWidth, int *gridHeight);
void
fxMagicLampVacuumInitGrid(AnimScreen * as, WindowEvent forWindowEvent,
					 int *gridWidth, int *gridHeight);
void fxMagicLampInit(CompScreen * s, CompWindow * w);
void
fxMagicLampModelStepObject(CompWindow * w,
						   Model * model,
						   Object * object,
						   float forwardProgress, Bool minimizeToTop);
void fxMagicLampModelStep(CompScreen * s, CompWindow * w, float time);
