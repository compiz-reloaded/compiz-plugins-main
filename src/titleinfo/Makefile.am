## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libtitleinfo_la_LDFLAGS = $(PFLAGS)
libtitleinfo_la_LIBADD = @COMPIZ_LIBS@
nodist_libtitleinfo_la_SOURCES = titleinfo_options.c titleinfo_options.h
dist_libtitleinfo_la_SOURCES = titleinfo.c

BUILT_SOURCES = $(nodist_libtitleinfo_la_SOURCES)

AM_CPPFLAGS =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'

moduledir = $(plugindir)

module_LTLIBRARIES = libtitleinfo.la

CLEANFILES = *_options.c *_options.h

titleinfo_options.h: ../../metadata/titleinfo.xml.in
	$(BCOP_BIN) --header $@ $<

titleinfo_options.c: ../../metadata/titleinfo.xml.in titleinfo_options.h
	$(BCOP_BIN) --source $@ $<

