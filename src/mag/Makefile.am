## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libmag_la_LDFLAGS = $(PFLAGS)
libmag_la_LIBADD = @COMPIZ_LIBS@
nodist_libmag_la_SOURCES = mag_options.c mag_options.h
dist_libmag_la_SOURCES = mag.c

BUILT_SOURCES = $(nodist_libmag_la_SOURCES)

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

mag_options.h: ../../metadata/mag.xml.in
		$(BCOP_BIN) --header $@ $<

mag_options.c: ../../metadata/mag.xml.in mag_options.h
		$(BCOP_BIN) --source $@ $<
