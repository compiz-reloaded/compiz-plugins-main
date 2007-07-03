## Process this file with automake to produce Makefile.in
SUBDIRS = metadata src po include

EXTRA_DIST =            \
    VERSION             \
    compiz-text.pc.in   \
    intltool-extract.in \
    intltool-merge.in   \
    intltool-update.in  

DISTCLEANFILES =     \
  	intltool-extract \
	intltool-merge   \
	intltool-update  

if TEXT_PLUGIN
textdata = compiz-text.pc
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(textdata)
