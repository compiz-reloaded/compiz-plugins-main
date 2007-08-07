## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libsnap_la_LDFLAGS = $(PFLAGS)
libsnap_la_LIBADD = @COMPIZ_LIBS@
libsnap_la_SOURCES = snap_options.c snap_options.h snap.c

INCLUDES =                              \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_srcdir)/include

moduledir = $(plugindir)


module_LTLIBRARIES = libsnap.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
