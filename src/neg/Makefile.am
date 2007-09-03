## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libneg_la_LDFLAGS = $(PFLAGS)
libneg_la_LIBADD = @COMPIZ_LIBS@
libneg_la_SOURCES = neg_options.c neg_options.h neg.c

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

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
