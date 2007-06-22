void fxZoomUpdateWindowAttrib(AnimScreen *as,AnimWindow *aw,WindowPaintAttrib *wAttrib);
void fxZoomModelStep(CompScreen *s,CompWindow *w,float time);
void fxZoomModelStepObject(CompScreen *s,CompWindow *w,Model *model,Object *object,Point currentCenter,Point currentSize);
void fxSidekickModelStepObject(CompWindow *w,Model *model,Object *object,Point currentCenter,Point currentSize,float sinRot,float cosRot);
void fxZoomAnimProgressDir(AnimScreen *as,AnimWindow *aw,float *moveProgress,float *scaleProgress);
void fxSidekickInit(CompScreen *s,CompWindow *w);
