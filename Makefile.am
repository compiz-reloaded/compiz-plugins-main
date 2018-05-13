ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

SUBDIRS = metadata src po include data images icons

EXTRA_DIST =		      \
	AUTHORS		       \
	COPYING		       \
	NEWS		       \
	README.md	       \
	autogen.sh	       \
	compiz-animation.pc.in \
	compiz-mousepoll.pc.in \
	compiz-text.pc.in      \
	intltool-extract.in    \
	intltool-merge.in      \
	intltool-update.in

DISTCLEANFILES =		\
	po/stamp-it		 \
	intltool-extract	 \
	intltool-merge		 \
	intltool-update		 \
	po/.intltool-merge-cache

if TEXT_PLUGIN
textdata = compiz-text.pc
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(textdata) compiz-animation.pc compiz-mousepoll.pc

# Build ChangeLog from GIT  history
ChangeLog:
	$(AM_V_GEN) if test -d $(top_srcdir)/.git; then \
		GIT_DIR="$(top_srcdir)/.git" git log --stat > $@; \
	fi

dist: ChangeLog

.PHONY: ChangeLog
