PFLAGS=-module -avoid-version -no-undefined -lGLU 

libanimation_la_LDFLAGS = $(PFLAGS)
libanimation_la_LIBADD = @COMPIZ_LIBS@
libanimation_la_SOURCES = animation.c \
			  animation-internal.h \
			  curvedfold.c \
			  dodge.c \
			  dream.c \
			  fade.c \
			  focusfade.c \
			  glide.c \
			  horizontalfold.c \
			  magiclamp.c \
			  options.c \
			  rollup.c \
			  wave.c \
			  zoomside.c

INCLUDES =                              \
	-I$(top_srcdir)/include             \
	@COMPIZ_CFLAGS@                     \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)

module_LTLIBRARIES=libanimation.la
