AC_PREREQ(2.57)

AC_INIT([compiz-plugins-main], [0.8.18], [chat-to-me@raveit.de],
        [compiz-plugins-main], [https://gitlab.com/compiz/compiz-plugins-main])
AM_INIT_AUTOMAKE([1.9 foreign dist-xz no-dist-gzip check-news])
AC_CONFIG_HEADER([config.h])
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
LT_INIT
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h sys/time.h unistd.h])
AC_CONFIG_MACRO_DIR([m4])
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19.7])
AM_GNU_GETTEXT_REQUIRE_VERSION([0.19.7])
GETTEXT_PACKAGE=AC_PACKAGE_NAME
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])
AC_SUBST(GETTEXT_PACKAGE)

dnl The following case statement works around a bug present in
dnl gettext-0.20 and gettext-0.20.1 which occurs at 'make dist'
dnl and 'make distcheck' invocations.
dnl https://git.savannah.gnu.org/gitweb/?p=gettext.git;a=commitdiff;h=2336451ed68d91ff4b5ae1acbc1eca30e47a86a9

dnl Unless --for-msgfmt is functional, assign the value that
dnl was default before --for-msgfmt was introduced:
case `LC_ALL=C $MSGMERGE --version | sed 1q | sed -e 's,^[[^0-9]]*,,'` in
  0.20|0.20.1)
    MSGMERGE_FOR_MSGFMT_OPTION='--no-fuzzy-matching --no-location --quiet' ;;
esac

if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *[[\ \	]]-Wall[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wpointer-arith[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wpointer-arith" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wstrict-prototypes[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wstrict-prototypes" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wmissing-prototypes[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wmissing-prototypes" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wmissing-declarations[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wmissing-declarations" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wnested-externs[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wnested-externs" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-fno-strict-aliasing[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -fno-strict-aliasing" ;;
  esac

  if test "x$enable_ansi" = "xyes"; then
    case " $CFLAGS " in
    *[[\ \	]]-ansi[[\ \	]]*) ;;
    *) CFLAGS="$CFLAGS -ansi" ;;
    esac

    case " $CFLAGS " in
    *[[\ \	]]-pedantic[[\ \	]]*) ;;
    *) CFLAGS="$CFLAGS -pedantic" ;;
    esac
  fi
fi

AC_C_BIGENDIAN

plugindir=$libdir/compiz
AC_SUBST(plugindir)

imagedir=$datadir/compiz
AC_SUBST(imagedir)

metadatadir=$datadir/compiz
AC_SUBST(metadatadir)

compdatadir=$datadir/compiz
AC_SUBST(compdatadir)

pluginiconsdir=$datadir/compiz/icons/hicolor
AC_SUBST(pluginiconsdir)

dnl ============================================================
dnl Check for the pkg-config path.
if test x"$PKG_CONFIG_PATH" = x; then
    PKG_CONFIG_PATH=${prefix}/lib/pkgconfig:${prefix}/share/pkgconfig
        else
    PKG_CONFIG_PATH=${prefix}/lib/pkgconfig:${prefix}/share/pkgconfig:${PKG_CONFIG_PATH}
fi
export PKG_CONFIG_PATH
AC_SUBST(PKG_CONFIG_PATH)
AC_MSG_NOTICE([Using PKG_CONFIG_PATH=$PKG_CONFIG_PATH])

PKG_CHECK_MODULES(COMPIZ, compiz)
PKG_CHECK_MODULES(BCOP, bcop >= 0.7.3, [bcop_found=yes])
if test "$bcop_found" = yes; then
  if test -z "$PKG_CONFIG"; then
      AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
  fi  
  if test "$PKG_CONFIG" != "no" ; then
      BCOP_BIN=`$PKG_CONFIG --variable=bin bcop`
      AC_SUBST(BCOP_BIN)
  fi
fi

AC_MSG_CHECKING(for GL_CFLAGS)
AC_ARG_WITH(gl-cflags, [  --with-gl-cflags=CFLAGS ],
		       [GL_CFLAGS="$withval"],
		       [GL_CFLAGS=""])

AC_MSG_RESULT($GL_CFLAGS)
AC_MSG_CHECKING(for GL_LIBS)
AC_ARG_WITH(gl-libs, [  --with-gl-libs=LIBS ],
		     [GL_LIBS="$withval"],
		     [GL_LIBS="-lGL"])
AC_MSG_RESULT($GL_LIBS)

AC_SUBST(GL_CFLAGS)
AC_SUBST(GL_LIBS)

PKG_CHECK_MODULES(ATSPI2, atspi-2, [have_atspi2=yes], [have_atspi2=no])
PKG_CHECK_MODULES(SCALE, compiz-scale, [have_compiz_scale=yes], [have_compiz_scale=no])
PKG_CHECK_MODULES(PANGO, cairo-xlib-xrender pangocairo cairo >= 1.0, [have_pango=yes], [have_pango=no])
PKG_CHECK_MODULES(LIBNOTIFY, libnotify >= 0.6.1,
                  [have_libnotify=yes
                   AC_DEFINE(HAVE_LIBNOTIFY, 1, [libnotify is available])],
                  [have_libnotify=no])

AM_CONDITIONAL(FOCUSPOLL_PLUGIN, test "x$have_atspi2" = "xyes")
if test "x$have_atspi2" = "xyes"; then
  AC_DEFINE(USE_FOCUSPOLL, 1, [Build focuspoll plugin])
  _save_cflags="$CFLAGS"
  CFLAGS="$CFLAGS $ATSPI2_CFLAGS"
  AC_CHECK_MEMBERS([AtspiEvent.sender], , , [#include <atspi/atspi-types.h>])
  CFLAGS="$_save_cflags"
fi

AM_CONDITIONAL(TEXT_PLUGIN, test "x$have_pango" = "xyes")
if test "x$have_pango" = "xyes"; then
  AC_DEFINE(USE_TEXT, 1, [Build text plugin])
fi

AM_CONDITIONAL(RESIZEINFO_PLUGIN, test "x$have_pango" = "xyes")
if test "x$have_pango" = "xyes"; then
  AC_DEFINE(USE_RESIZEINFO, 1, [Build resizeinfo plugin])
fi

AM_CONDITIONAL(SCALEADDON_PLUGIN, test "x$have_compiz_scale" = "xyes")
if test "x$have_compiz_scale" = "xyes"; then
  AC_DEFINE(USE_SCALEADDON, 1, [Build scaleaddon plugin])
fi

AC_ARG_ENABLE(jpeg,
  [  --disable-jpeg      Disable jpeg plugin],
  [use_jpeg=$enableval], [use_jpeg=yes])

AM_CONDITIONAL(JPEG_PLUGIN, test "x$use_jpeg" = "xyes")
if test "$use_jpeg" = yes; then
  AC_DEFINE(USE_JPEG, 1, [Build jpeg plugin])
fi

AC_PATH_PROG(UPDATE_ICON_CACHE, gtk-update-icon-cache)

AC_OUTPUT([
compiz-text.pc
compiz-mousepoll.pc
compiz-focuspoll.pc
compiz-animation.pc
data/Makefile
data/filters/Makefile
images/Makefile
images/Default/Makefile
Makefile
icons/Makefile
metadata/Makefile
include/Makefile
src/Makefile
src/animation/Makefile
src/colorfilter/Makefile
src/expo/Makefile
src/ezoom/Makefile
src/jpeg/Makefile
src/mag/Makefile
src/mousepoll/Makefile
src/focuspoll/Makefile
src/neg/Makefile
src/opacify/Makefile
src/put/Makefile
src/resizeinfo/Makefile
src/ring/Makefile
src/scaleaddon/Makefile
src/session/Makefile
src/shift/Makefile
src/snap/Makefile
src/staticswitcher/Makefile
src/text/Makefile
src/thumbnail/Makefile
src/titleinfo/Makefile
src/vpswitch/Makefile
src/winrules/Makefile
src/workarounds/Makefile
po/Makefile.in
])

echo ""
echo "the following optional plugins will be compiled:"
echo "  jpeg:      $use_jpeg"
echo ""
