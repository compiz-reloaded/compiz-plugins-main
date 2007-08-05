PFLAGS=-module -avoid-version -no-undefined  

libanimation_la_LDFLAGS = $(PFLAGS)
libanimation_la_LIBADD = @COMPIZ_LIBS@
libanimation_la_SOURCES = animation.c \
			  animation-internal.h \
			  animation_tex.h \
			  airplane3d.c \
			  beamup.c \
			  burn.c \
			  curvedfold.c \
			  dodge.c \
			  domino.c \
			  dream.c \
			  explode3d.c \
			  fade.c \
			  focusfade.c \
			  fold3d.c \
			  glide.c \
			  horizontalfold.c \
			  leafspread.c \
			  magiclamp.c \
			  options.c \
			  particle.c \
			  polygon.c \
			  rollup.c \
			  skewer.c \
			  wave.c \
			  zoomside.c

INCLUDES =                              \
	@COMPIZ_CFLAGS@                     \
	-DDATADIR='"$(datadir)"'            \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_srcdir)/include

moduledir = $(plugindir)

module_LTLIBRARIES=libanimation.la
