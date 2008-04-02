include_directories (
    ${compiz-fusion-plugins-main_SOURCE_DIR}/include
    ${COMPIZ_INCLUDE_DIRS}
)

link_directories (
    ${COMPIZ_LINK_DIRS}
)

add_library (
    animation SHARED animation.c
                     animation-internal.h
                     animation_tex.h
                     airplane3d.c
                     beamup.c
                     burn.c
                     curvedfold.c
                     dodge.c
                     domino.c
                     dream.c
                     explode3d.c
                     fade.c
                     focusfade.c
                     fold3d.c
                     glide.c
                     horizontalfold.c
                     leafspread.c
                     magiclamp.c
                     options.c
                     particle.c
                     polygon.c
                     rollup.c
                     skewer.c
                     wave.c
                     zoomside.c
)

target_link_libraries (
    animation ${COMPIZ_LIBRARIES} -lGLU
)

install (
    TARGETS animation
    LIBRARY DESTINATION lib/compiz
)
