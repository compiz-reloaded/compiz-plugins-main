## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libcolorfilter_la_LDFLAGS = $(PFLAGS)
libcolorfilter_la_LIBADD = @COMPIZ_LIBS@
libcolorfilter_la_SOURCES = colorfilter_options.c \
			    colorfilter_options.h \
			    colorfilter.c         \
			    parser.c              \
			    parser.h

INCLUDES =                              \
	-I$(top_srcdir)/include		\
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'       \
	-DPREFIX='"$(prefix)"'             \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libcolorfilter.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
