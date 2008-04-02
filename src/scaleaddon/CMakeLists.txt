set (plugin "scaleaddon")

bcop (${plugin})

include_directories (
    ${compiz-fusion-plugins-main_SOURCE_DIR}/include
    ${COMPIZ_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${SCALE_INCLUDE_DIRS}
)

link_directories (
    ${COMPIZ_LINK_DIRS}
    ${SCALE_LINK_DIRS}
)

add_library (
    ${plugin} SHARED ${plugin}.c
                     ${plugin}_options.c
                     ${plugin}_options.h
)

target_link_libraries (
    ${plugin} ${COMPIZ_LIBRARIES} ${SCALE_LIBRARIES}
)

install (
    TARGETS ${plugin}
    LIBRARY DESTINATION lib/compiz
)
