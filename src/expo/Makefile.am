## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libexpo_la_LDFLAGS = $(PFLAGS)
libexpo_la_LIBADD = @COMPIZ_LIBS@ -lGLU
libexpo_la_SOURCES = expo_options.c expo_options.h expo.c

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

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
