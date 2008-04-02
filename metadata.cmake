find_program (
    INTLTOOL_MERGE_EXECUTABLE
    intltool-merge
)
if (INTLTOOL_MERGE_EXECUTABLE)
    message (STATUS "intltool-merge found")
else (INTLTOOL_MERGE_EXECUTABLE)
    message (FATAL_ERROR "Couldn't find intltool-merge")
endif (INTLTOOL_MERGE_EXECUTABLE)

set (ENV{LC_ALL} "C")
file (GLOB po_files "${CMAKE_CURRENT_SOURCE_DIR}/po/*.po")

macro (gen_xml_metadata plugin)
    add_custom_command (
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${plugin}.xml
        COMMAND ${INTLTOOL_MERGE_EXECUTABLE} -x -u -c
                        ${CMAKE_CURRENT_BINARY_DIR}/po/.intltool-merge-cache
                        ${CMAKE_CURRENT_SOURCE_DIR}/po
                        ${CMAKE_CURRENT_SOURCE_DIR}/${plugin}.xml.in
                        ${CMAKE_CURRENT_BINARY_DIR}/${plugin}.xml
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${plugin}.xml.in
    )
endmacro (gen_xml_metadata)
