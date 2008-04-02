project (compiz-fusion-plugins-main)

if ("" STREQUAL "$ENV{PKG_CONFIG_PATH}")
    set (ENV{PKG_CONFIG_PATH} "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig/")
else ("" STREQUAL "$ENV{PKG_CONFIG_PATH}")
    set (ENV{PKG_CONFIG_PATH}
         "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig/:$ENV{PKG_CONFIG_PATH}")
endif ("" STREQUAL "$ENV{PKG_CONFIG_PATH}")

include (FindPkgConfig)
pkg_check_modules (COMPIZ REQUIRED compiz)
pkg_check_modules (BCOP REQUIRED bcop>=0.7.3)
pkg_check_modules (SCALE compiz-scale)
pkg_check_modules (CAIRO cairo-xlib-xrender cairo>=1.0)
pkg_check_modules (PANGO cairo-xlib-xrender pangocairo cairo>=1.0)

include (metadata.cmake)
include (bcop.cmake)
include (plugin.cmake)

set (MAIN_CFLAGS "${CMAKE_C_FLAGS} -W -DPREFIX='\"${CMAKE_INSTALL_PREFIX}\"'")

set (CMAKE_C_FLAGS "${MAIN_CFLAGS}")
add_subdirectory (metadata)
add_subdirectory (src)
add_subdirectory (include)
add_subdirectory (data)
add_subdirectory (images)
