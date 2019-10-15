## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libcolorfilter_la_LDFLAGS = $(PFLAGS)
libcolorfilter_la_LIBADD = @COMPIZ_LIBS@ @LIBNOTIFY_LIBS@
nodist_libcolorfilter_la_SOURCES = colorfilter_options.c \
			    colorfilter_options.h
dist_libcolorfilter_la_SOURCES = colorfilter.c \
			    parser.c              \
			    parser.h

BUILT_SOURCES = $(nodist_libcolorfilter_la_SOURCES)

AM_CPPFLAGS =                              \
	-I$(top_srcdir)/include		\
	@COMPIZ_CFLAGS@                  \
	@LIBNOTIFY_CFLAGS@               \
	-DDATADIR='"$(compdatadir)"'       \
	-DPREFIX='"$(prefix)"'             \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DICONSDIR='"$(pluginiconsdir)"'    \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libcolorfilter.la

CLEANFILES = *_options.c *_options.h

colorfilter_options.h: ../../metadata/colorfilter.xml.in
		$(BCOP_BIN) --header $@ $<

colorfilter_options.c: ../../metadata/colorfilter.xml.in colorfilter_options.h
		$(BCOP_BIN) --source $@ $<
