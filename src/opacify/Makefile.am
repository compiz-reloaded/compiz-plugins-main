## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libopacify_la_LDFLAGS = $(PFLAGS)
libopacify_la_LIBADD = @COMPIZ_LIBS@
libopacify_la_SOURCES = opacify_options.c opacify_options.h opacify.c

INCLUDES =                              \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(datadir)"'          \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_srcdir)/include

moduledir = $(plugindir)


module_LTLIBRARIES = libopacify.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
