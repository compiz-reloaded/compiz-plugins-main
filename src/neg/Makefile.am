## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libneg_la_LDFLAGS = $(PFLAGS)
libneg_la_LIBADD = @COMPIZ_LIBS@
nodist_libneg_la_SOURCES = neg_options.c neg_options.h
dist_libneg_la_SOURCES = neg.c

BUILT_SOURCES = $(nodist_libneg_la_SOURCES)

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libneg.la

CLEANFILES = *_options.c *_options.h

neg_options.h: ../../metadata/neg.xml.in
		$(BCOP_BIN) --header $@ $<

neg_options.c: ../../metadata/neg.xml.in neg_options.h
		$(BCOP_BIN) --source $@ $<
