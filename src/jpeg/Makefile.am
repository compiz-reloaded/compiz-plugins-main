## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

if JPEG_PLUGIN
libimgjpeg_la_LDFLAGS = $(PFLAGS)
libimgjpeg_la_LIBADD = @COMPIZ_LIBS@ -ljpeg
nodist_libimgjpeg_la_SOURCES = imgjpeg_options.c imgjpeg_options.h
dist_libimgjpeg_la_SOURCES = imgjpeg.c
endif

BUILT_SOURCES = $(nodist_libimgjpeg_la_SOURCES)

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)

if JPEG_PLUGIN
module_LTLIBRARIES = libimgjpeg.la
endif

CLEANFILES = *_options.c *_options.h

imgjpeg_options.h: ../../metadata/imgjpeg.xml.in
		$(BCOP_BIN) --header $@ $<

imgjpeg_options.c: ../../metadata/imgjpeg.xml.in imgjpeg_options.h
		$(BCOP_BIN) --source $@ $<
