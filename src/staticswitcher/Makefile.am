## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libstaticswitcher_la_LDFLAGS = $(PFLAGS)
libstaticswitcher_la_LIBADD = @COMPIZ_LIBS@
nodist_libstaticswitcher_la_SOURCES = staticswitcher_options.c \
			       staticswitcher_options.h \
dist_libstaticswitcher_la_SOURCES = staticswitcher.c

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

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
