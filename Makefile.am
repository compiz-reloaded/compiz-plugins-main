## Process this file with automake to produce Makefile.in
SUBDIRS = metadata src po include

EXTRA_DIST =            \
    VERSION             \
    compiz-text.pc.in   \
    intltool-extract.in \
    intltool-merge.in   \
    intltool-update.in \
    intltool-extract \
    intltool-merge \
    intltool-update 

DISTCLEANFILES =     \
    intltool-extract \
    intltool-merge   \
    intltool-update  \
    po/stamp-it \
    po/.intltool-merge-cache

if TEXT_PLUGIN
textdata = compiz-text.pc
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(textdata)
