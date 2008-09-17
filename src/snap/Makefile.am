## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libsnap_la_LDFLAGS = $(PFLAGS)
libsnap_la_LIBADD = @COMPIZ_LIBS@
nodist_libsnap_la_SOURCES = snap_options.c snap_options.h
dist_libsnap_la_SOURCES = snap.c

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libsnap.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
