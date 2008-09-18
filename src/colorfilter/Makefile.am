## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libcolorfilter_la_LDFLAGS = $(PFLAGS)
libcolorfilter_la_LIBADD = @COMPIZ_LIBS@
nodist_libcolorfilter_la_SOURCES = colorfilter_options.c \
			    colorfilter_options.h
dist_libcolorfilter_la_SOURCES = colorfilter.c \
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

colorfilter_options.h: ../../metadata/colorfilter.xml
		$(BCOP_BIN) --header $@ $<

colorfilter_options.c: ../../metadata/colorfilter.xml colorfilter_options.h
		$(BCOP_BIN) --source $@ $<

colorfilter.c: colorfilter_options.c colorfilter_options.h
