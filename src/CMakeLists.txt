project(binary)

include_directories(${TOP_SRC_DIR})
include_directories (${TOP_SRC_DIR}/src/ttsubset)

link_directories(${binary_BINARY_DIR}/ttsubset)

add_executable(xournal main.c xournal.h xo-misc.c xo-misc.h xo-file.c xo-file.h xo-paint.c xo-paint.h xo-selection.c xo-selection.h xo-clipboard.c xo-clipboard.h xo-image.c xo-image.h xo-print.c xo-print.h xo-support.c xo-support.h xo-interface.c xo-interface.h xo-callbacks.c xo-callbacks.h xo-shapes.c xo-shapes.h)

#target_link_libraries(xournal ${_pano_target} ${_common_libs})

target_link_libraries(xournal m z)
target_link_libraries(xournal ${GTK2_LIBRARIES} ${GNOMECANVAS2_LIBRARIES} )
target_link_libraries(xournal ${ZLIB_LIBRARIES} ${X11_LIBRARIES} ${POPPLER_LIBRARIES} ${CAIRO_LIBRARIES} ${PANGOFT2_LIBRARIES})
target_link_libraries(xournal ttsubset)

add_custom_target(binary DEPENDS xournal)

install(TARGETS xournal DESTINATION bin)
