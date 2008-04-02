macro (add_bcop_plugin plugin extra_sources)
    bcop (${plugin})

    add_plugin (
        ${plugin} ${plugin}_options.c
                  ${plugin}_options.h
                  ${extra_sources}
    )
endmacro (add_bcop_plugin)

macro (add_plugin plugin sources)
    include_directories (
        ${compiz-fusion-plugins-main_SOURCE_DIR}/include
        ${COMPIZ_INCLUDE_DIRS}
    )

    link_directories (
        ${COMPIZ_LINK_DIRS}
    )

    add_library (
        ${plugin} SHARED ${plugin}.c
                         ${sources}
    )

    target_link_libraries (
        ${plugin} ${COMPIZ_LIBRARIES}
    )

    install (
        TARGETS ${plugin}
        LIBRARY DESTINATION lib/compiz
    )
endmacro (add_plugin)
