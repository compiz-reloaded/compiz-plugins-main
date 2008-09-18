## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libexpo_la_LDFLAGS = $(PFLAGS)
libexpo_la_LIBADD = @COMPIZ_LIBS@ -lGLU
nodist_libexpo_la_SOURCES = expo_options.c expo_options.h
dist_libexpo_la_SOURCES = expo.c

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libexpo.la

CLEANFILES = *_options.c *_options.h

expo_options.h: ../../metadata/expo.xml.in
		$(BCOP_BIN) --header $@ $<

expo_options.c: ../../metadata/expo.xml.in expo_options.h
		$(BCOP_BIN) --source $@ $<

expo.c: expo_options.c expo_options.h

expo.c: expo_options.c expo_options.h
