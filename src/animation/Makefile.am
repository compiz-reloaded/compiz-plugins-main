PFLAGS=-module -avoid-version -no-undefined  

libanimation_la_LDFLAGS = $(PFLAGS)
libanimation_la_LIBADD = @COMPIZ_LIBS@
libanimation_la_SOURCES = animation.c animation_tex.h 

INCLUDES =                              \
	@COMPIZ_CFLAGS@                     \
	-DDATADIR='"$(datadir)"'            \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_srcdir)/include

moduledir = $(plugindir)

module_LTLIBRARIES=libanimation.la
