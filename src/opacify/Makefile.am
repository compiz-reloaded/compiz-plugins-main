## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libopacify_la_LDFLAGS = $(PFLAGS)
libopacify_la_LIBADD = @COMPIZ_LIBS@
nodist_libopacify_la_SOURCES = opacify_options.c opacify_options.h
dist_libopacify_la_SOURCES = opacify.c

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'         

moduledir = $(plugindir)


module_LTLIBRARIES = libopacify.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
