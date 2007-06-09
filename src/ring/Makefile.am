## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libring_la_LDFLAGS = $(PFLAGS)
libring_la_LIBADD = @COMPIZ_LIBS@
libring_la_SOURCES = ring_options.c ring_options.h ring.c

INCLUDES =                              \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(datadir)"'          \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_builddir)/include

moduledir = $(plugindir)


module_LTLIBRARIES = libring.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
