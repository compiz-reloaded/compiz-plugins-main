## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libscaleaddon_la_LDFLAGS = $(PFLAGS)
libscaleaddon_la_LIBADD = @COMPIZ_LIBS@ @RESIZEINFO_LIBS@
libscaleaddon_la_SOURCES = scaleaddon_options.c scaleaddon_options.h scaleaddon.c

INCLUDES =                              \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(datadir)"'          \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_srcdir)/include

moduledir = $(plugindir)

module_LTLIBRARIES = libscaleaddon.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
