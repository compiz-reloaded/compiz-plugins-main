## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

if RESIZEINFO_PLUGIN
libresizeinfo_la_LDFLAGS = $(PFLAGS)
libresizeinfo_la_LIBADD = @COMPIZ_LIBS@ @PANGO_LIBS@
nodist_libresizeinfo_la_SOURCES = resizeinfo_options.c
			   resizeinfo_options.h \
dist_libresizeinfo_la_SOURCES = resizeinfo.c
endif

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	@PANGO_CFLAGS@		\
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)

if RESIZEINFO_PLUGIN
module_LTLIBRARIES = libresizeinfo.la
endif

CLEANFILES = *_options.c *_options.h

resizeinfo_options.h: ../../metadata/resizeinfo.xml
		$(BCOP_BIN) --header $@ $<

resizeinfo_options.c: ../../metadata/resizeinfo.xml resizeinfo_options.h
		$(BCOP_BIN) --source $@ $<

resizeinfo.c: resizeinfo_options.c resizeinfo_options.h
