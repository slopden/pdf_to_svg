cmake_minimum_required(VERSION 3.17)
project(pdf_to_svg LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(ExternalProject)

# Installation prefix for dependencies
set(DEPS_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/deps_install")
set(DEPS_LIB_DIR "${DEPS_INSTALL_PREFIX}/lib")
set(DEPS_LIB64_DIR "${DEPS_INSTALL_PREFIX}/lib/x86_64-linux-gnu")
set(DEPS_INCLUDE_DIR "${DEPS_INSTALL_PREFIX}/include")

# Build cairo using meson
ExternalProject_Add(cairo_ext
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/upstream/cairo"
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        "LDFLAGS=-Wl,-rpath,$ORIGIN"
        meson setup
        --prefix=${DEPS_INSTALL_PREFIX}
        --buildtype=release
        -Ddefault_library=shared
        -Dtests=disabled
        -Dgtk_doc=false
        -Dquartz=disabled
        -Dxcb=disabled
        -Dxlib=disabled
        -Dxlib-xcb=disabled
        -Dglib=enabled
        -Dfreetype=enabled
        -Dfontconfig=enabled
        -Dpng=enabled
        -Dzlib=enabled
        -Dtee=disabled
        -Dspectre=disabled
        -Dsymbol-lookup=disabled
        <BINARY_DIR>
        <SOURCE_DIR>
    BUILD_COMMAND meson compile -C <BINARY_DIR>
    INSTALL_COMMAND meson install -C <BINARY_DIR>
    BUILD_BYPRODUCTS
        "${DEPS_LIB64_DIR}/libcairo.so"
        "${DEPS_LIB64_DIR}/libcairo.so.2"
)

# Build poppler using cmake (depends on cairo)
ExternalProject_Add(poppler_ext
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/upstream/poppler"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DEPS_INSTALL_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_PREFIX_PATH=${DEPS_INSTALL_PREFIX}
        -DCMAKE_INSTALL_RPATH=$ORIGIN:$ORIGIN/../lib/x86_64-linux-gnu
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
        -DPKG_CONFIG_PATH=${DEPS_LIB_DIR}/pkgconfig:${DEPS_LIB64_DIR}/pkgconfig
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
    BUILD_BYPRODUCTS
        "${DEPS_LIB_DIR}/libpoppler.so"
        "${DEPS_LIB_DIR}/libpoppler-glib.so"
)

# Find Python and nanobind
find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)

execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE NB_DIR)
list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
find_package(nanobind CONFIG REQUIRED)

# Create the nanobind module
nanobind_add_module(_core src/pdf_to_svg/_core.cpp)

# Make _core depend on poppler being built
add_dependencies(_core poppler_ext)

# Include directories - use the deps_install paths
target_include_directories(_core PRIVATE
    ${DEPS_INCLUDE_DIR}
    ${DEPS_INCLUDE_DIR}/poppler
    ${DEPS_INCLUDE_DIR}/poppler/glib
    ${DEPS_INCLUDE_DIR}/cairo
    ${DEPS_INCLUDE_DIR}/glib-2.0
    ${DEPS_LIB64_DIR}/glib-2.0/include
    ${DEPS_INCLUDE_DIR}/freetype2
    ${DEPS_INCLUDE_DIR}/libpng16
)

# Link directories
target_link_directories(_core PRIVATE
    ${DEPS_LIB_DIR}
    ${DEPS_LIB64_DIR}
)

# Link libraries
target_link_libraries(_core PRIVATE
    poppler-glib
    poppler
    cairo
    gobject-2.0
    glib-2.0
)

# Set rpath so the module can find the libraries at runtime
set_target_properties(_core PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib:$ORIGIN/../lib/x86_64-linux-gnu"
    BUILD_WITH_INSTALL_RPATH TRUE
)

# Install the module
install(TARGETS _core LIBRARY DESTINATION pdf_to_svg)

# Install the shared libraries alongside the module
install(DIRECTORY ${DEPS_LIB_DIR}/ DESTINATION lib
    FILES_MATCHING
    PATTERN "*.so*"
    PATTERN "pkgconfig" EXCLUDE
    PATTERN "cmake" EXCLUDE
)
install(DIRECTORY ${DEPS_LIB64_DIR}/ DESTINATION lib/x86_64-linux-gnu
    FILES_MATCHING
    PATTERN "*.so*"
    PATTERN "pkgconfig" EXCLUDE
    PATTERN "cmake" EXCLUDE
)
