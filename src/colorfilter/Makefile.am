## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libezoom_la_LDFLAGS = $(PFLAGS)
libezoom_la_LIBADD = @COMPIZ_LIBS@
libezoom_la_SOURCES = colorfilter_options.h \
		      colorfilter_options.c \
		      colorfilter.c         \
		      parser.c              \
		      parser.h

INCLUDES =                              \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'       \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_srcdir)/include

moduledir = $(plugindir)


module_LTLIBRARIES = libcolorfilter.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
