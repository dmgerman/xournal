cmake_minimum_required(VERSION 2.6)

if(POLICY CMP0011)
   cmake_policy(SET CMP0011 OLD) # or even better, NEW
endif(POLICY CMP0011)

project(xournal C)


SET(VERSION "0.4.7")

IF(NOT DEFINED SHARE_INSTALL_PREFIX)
  SET(PKGDATADIR "${CMAKE_INSTALL_PREFIX}/share/xournal")
ELSE()
  SET(PKGDATADIR "${SHARE_INSTALL_PREFIX}/xournal")
ENDIF()

find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)
find_package(X11 REQUIRED)

PKG_CHECK_MODULES(CAIRO cairo)
PKG_CHECK_MODULES(POPPLER poppler-glib)
PKG_CHECK_MODULES(GNOMECANVAS2 libgnomecanvas-2.0)
PKG_CHECK_MODULES(PANGOFT2 pangoft2)

find_package(GTK2 2.10 REQUIRED gtk glade)

add_definitions(-DPACKAGE="xournal" -DPACKAGE_DATA_DIR="${PKGDATADIR}" -DVERSION="${VERSION}")

include_directories(${GNOMECANVAS2_INCLUDE_DIRS})
include_directories(${GTK2_INCLUDE_DIRS})
include_directories("${PROJECT_BINARY_DIR}")

add_subdirectory(src/ttsubset)
add_subdirectory(src)
