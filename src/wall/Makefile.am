## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

if WALL_PLUGIN
libwall_la_LDFLAGS = $(PFLAGS)
libwall_la_LIBADD = @COMPIZ_LIBS@ @CAIRO_LIBS@
nodist_libwall_la_SOURCES = wall_options.c wall_options.h
dist_libwall_la_SOURCES = wall.c
endif

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	@CAIRO_CFLAGS@			\
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)

if WALL_PLUGIN
module_LTLIBRARIES = libwall.la
endif

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
