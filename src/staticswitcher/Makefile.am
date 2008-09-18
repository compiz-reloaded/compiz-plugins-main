## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libstaticswitcher_la_LDFLAGS = $(PFLAGS)
libstaticswitcher_la_LIBADD = @COMPIZ_LIBS@
nodist_libstaticswitcher_la_SOURCES = staticswitcher_options.c \
			       staticswitcher_options.h \
dist_libstaticswitcher_la_SOURCES = staticswitcher.c

BUILT_SOURCES = $(nodist_libstaticswitcher_la_SOURCES)

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'

moduledir = $(plugindir)

module_LTLIBRARIES = libstaticswitcher.la

CLEANFILES = *_options.c *_options.h

staticswitcher_options.h: ../../metadata/staticswitcher.xml.in
		$(BCOP_BIN) --header $@ $<

staticswitcher_options.c: ../../metadata/staticswitcher.xml.in staticswitcher_options.h
		$(BCOP_BIN) --source $@ $<
