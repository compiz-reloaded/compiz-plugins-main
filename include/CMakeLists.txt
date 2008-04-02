file (GLOB headers "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
install (
    FILES ${headers}
    DESTINATION include/compiz
)
