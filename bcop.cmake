find_program (
    BCOP_EXECUTABLE
    bcop
)
if (BCOP_EXECUTABLE)
    message (STATUS "bcop found")
else (BCOP_EXECUTABLE)
    message (FATAL_ERROR "Couldn't find bcop")
endif (BCOP_EXECUTABLE)

macro (bcop plugin)
    add_custom_command (
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${plugin}_options.h
        COMMAND ${BCOP_EXECUTABLE}
                    --header ${CMAKE_CURRENT_BINARY_DIR}/${plugin}_options.h
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../metadata/${plugin}.xml.in
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../metadata/${plugin}.xml.in
    )
    add_custom_command (
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${plugin}_options.c
        COMMAND ${BCOP_EXECUTABLE}
                    --source ${CMAKE_CURRENT_BINARY_DIR}/${plugin}_options.c
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../metadata/${plugin}.xml
                    ${CMAKE_CURRENT_BINARY_DIR}/${plugin}_options.h
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../metadata/${plugin}.xml.in
                ${CMAKE_CURRENT_BINARY_DIR}/${plugin}_options.h
    )
endmacro (bcop)
