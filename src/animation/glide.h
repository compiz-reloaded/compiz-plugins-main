
void fxGlideInit(CompScreen *s,CompWindow *w);
void fxGlideUpdateWindowAttrib(AnimScreen *as,AnimWindow *aw,WindowPaintAttrib *wAttrib);
void fxGlideAnimStep(CompScreen *s,CompWindow *w,float time);
void fxGlideModelStepObject(CompWindow *w,Model *model,Object *obj,GLfloat *mat,Point3d rotAxisOffset);
float fxGlideAnimProgress(AnimWindow *aw);
void fxGlideGetParams(AnimScreen *as,AnimWindow *aw,float *finalDistFac,float *finalRotAng,float *thickness);
