## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libmag_la_LDFLAGS = $(PFLAGS)
libmag_la_LIBADD = @COMPIZ_LIBS@
nodist_libmag_la_SOURCES = mag_options.c mag_options.h
dist_libmag_la_SOURCES = mag.c

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)

module_LTLIBRARIES = libmag.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
