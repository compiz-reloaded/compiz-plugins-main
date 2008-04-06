include (FindJPEG)
include (CompizFusion)

if (JPEG_FOUND)
    compiz_fusion_plugin (imgjpeg LIBRARIES ${JPEG_LIBRARIES} INCDIRS ${JPEG_INCLUDE_DIR})
endif (JPEG_FOUND)