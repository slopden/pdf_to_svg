cmake_minimum_required(VERSION 3.17)
project(pdf_to_svg LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(ExternalProject)

set(DEPS_PREFIX "${CMAKE_BINARY_DIR}/deps_install")

# ============== Cairo (via Meson) ==============
ExternalProject_Add(cairo_ext
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/upstream/cairo"
    CONFIGURE_COMMAND meson setup
        --prefix=${DEPS_PREFIX}
        --libdir=lib
        --buildtype=release
        -Ddefault_library=shared
        -Dtests=disabled
        -Dgtk_doc=false
        -Dlzo=disabled
        -Dquartz=disabled
        -Dxcb=disabled
        -Dxlib=disabled
        -Dxlib-xcb=disabled
        -Dtee=disabled
        -Dspectre=disabled
        -Dsymbol-lookup=disabled
        <BINARY_DIR>
        <SOURCE_DIR>
    BUILD_COMMAND meson compile -C <BINARY_DIR>
    INSTALL_COMMAND meson install -C <BINARY_DIR>
)

# ============== Poppler (via CMake) ==============
ExternalProject_Add(poppler_ext
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/upstream/poppler"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DEPS_PREFIX}
        -DCMAKE_PREFIX_PATH=${DEPS_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_LIBDIR=lib
        -DENABLE_GLIB=ON
        -DENABLE_CPP=OFF
        -DENABLE_QT5=OFF
        -DENABLE_QT6=OFF
        -DENABLE_UTILS=OFF
        -DENABLE_BOOST=OFF
        -DENABLE_LIBCURL=OFF
        -DENABLE_LIBTIFF=OFF
        -DENABLE_NSS3=OFF
        -DENABLE_GPGME=OFF
        -DENABLE_LCMS=OFF
        -DENABLE_DCTDECODER=none
        -DENABLE_LIBOPENJPEG=none
        -DENABLE_GOBJECT_INTROSPECTION=OFF
        -DENABLE_GTK_DOC=OFF
        -DBUILD_GTK_TESTS=OFF
        -DBUILD_QT5_TESTS=OFF
        -DBUILD_QT6_TESTS=OFF
        -DBUILD_CPP_TESTS=OFF
        -DBUILD_MANUAL_TESTS=OFF
    DEPENDS cairo_ext
)

# ============== Python Module ==============
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gobject-2.0)

find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)
execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE NB_DIR)
list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_core src/pdf_to_svg/_core.cpp)
add_dependencies(_core poppler_ext)

target_include_directories(_core PRIVATE
    ${DEPS_PREFIX}/include
    ${DEPS_PREFIX}/include/poppler
    ${DEPS_PREFIX}/include/poppler/glib
    ${DEPS_PREFIX}/include/cairo
    ${GLIB_INCLUDE_DIRS}
)

target_link_directories(_core PRIVATE ${DEPS_PREFIX}/lib ${GLIB_LIBRARY_DIRS})
target_link_libraries(_core PRIVATE poppler-glib poppler cairo ${GLIB_LIBRARIES})

# ============== Install ==============
install(TARGETS _core LIBRARY DESTINATION pdf_to_svg)
install(DIRECTORY ${DEPS_PREFIX}/lib/ DESTINATION pdf_to_svg/.libs
    FILES_MATCHING PATTERN "*.so*" PATTERN "*.dylib*" PATTERN "*.dll"
    PATTERN "pkgconfig" EXCLUDE PATTERN "cmake" EXCLUDE
)
