PFLAGS=-module -avoid-version -no-undefined  

libanimation_la_LDFLAGS = $(PFLAGS)
libanimation_la_LIBADD = @COMPIZ_LIBS@
libanimation_la_SOURCES = animation.c animation-internal.h \
			  animation_tex.h \
			  beamup.c beamup.h \
			  burn.c burn.h \
			  curvedfold.c curvedfold.h \
			  dodge.c dodge.h \
			  domino.c domino.h \
			  dream.c dream.h \
			  explode3d.c explode3d.h \
			  fade.c fade.h \
			  focusfade.c focusfade.h \
			  glide.c glide.h \
			  horizontalfold.c horizontalfold.h \
			  leafspread.c leafspread.h \
			  magiclamp.c magiclamp.h \
			  particle.c polygon.c \
			  rollup.c rollup.h \
			  wave.c wave.h \
			  zoomside.c zoomside.h

INCLUDES =                              \
	@COMPIZ_CFLAGS@                     \
	-DDATADIR='"$(datadir)"'            \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_srcdir)/include

moduledir = $(plugindir)

module_LTLIBRARIES=libanimation.la
