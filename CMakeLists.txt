cmake_minimum_required(VERSION 3.17)
project(pdf_to_svg LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(ExternalProject)

# ============== Platform Detection ==============
if(APPLE)
    set(PLATFORM_MACOS TRUE)
    set(PLATFORM_LIB_SUFFIX ".dylib")
    set(PLATFORM_RPATH_PREFIX "@loader_path")
    set(LIB_PATTERN "*.dylib*")
elseif(WIN32)
    set(PLATFORM_WINDOWS TRUE)
    set(PLATFORM_LIB_SUFFIX ".dll")
    set(PLATFORM_RPATH_PREFIX "")
    set(LIB_PATTERN "*.dll")
else()
    set(PLATFORM_LINUX TRUE)
    set(PLATFORM_LIB_SUFFIX ".so")
    set(PLATFORM_RPATH_PREFIX "$ORIGIN")
    set(LIB_PATTERN "*.so*")
endif()

# ============== Dependency Paths ==============
set(DEPS_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/deps_install")
set(DEPS_LIB_DIR "${DEPS_INSTALL_PREFIX}/lib")
set(DEPS_INCLUDE_DIR "${DEPS_INSTALL_PREFIX}/include")

# ============== Cairo Build ==============
# Platform-specific LDFLAGS
if(PLATFORM_LINUX)
    set(CAIRO_ENV "LDFLAGS=-Wl,-rpath,${PLATFORM_RPATH_PREFIX}")
else()
    set(CAIRO_ENV "")
endif()

ExternalProject_Add(cairo_ext
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/upstream/cairo"
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        ${CAIRO_ENV}
        meson setup
        --prefix=${DEPS_INSTALL_PREFIX}
        --libdir=lib
        --buildtype=release
        -Ddefault_library=shared
        -Dtests=disabled
        -Dgtk_doc=false
        -Dlzo=disabled              # FIX: Disable LZO to avoid macOS include path issues
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
    BUILD_BYPRODUCTS "${DEPS_LIB_DIR}/libcairo${PLATFORM_LIB_SUFFIX}"
)

# ============== Poppler Build ==============
# Platform-specific RPATH args
if(PLATFORM_WINDOWS)
    set(POPPLER_RPATH_ARGS "")
else()
    set(POPPLER_RPATH_ARGS
        -DCMAKE_INSTALL_RPATH=${PLATFORM_RPATH_PREFIX}
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
    )
endif()

ExternalProject_Add(poppler_ext
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/upstream/poppler"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DEPS_INSTALL_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_PREFIX_PATH=${DEPS_INSTALL_PREFIX}
        -DCMAKE_INSTALL_LIBDIR=lib
        ${POPPLER_RPATH_ARGS}
        -DPKG_CONFIG_PATH=${DEPS_LIB_DIR}/pkgconfig
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
        "${DEPS_LIB_DIR}/libpoppler${PLATFORM_LIB_SUFFIX}"
        "${DEPS_LIB_DIR}/libpoppler-glib${PLATFORM_LIB_SUFFIX}"
)

# ============== Python Module ==============
find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)

execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE NB_DIR)
list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_core src/pdf_to_svg/_core.cpp)
add_dependencies(_core poppler_ext)

target_include_directories(_core PRIVATE
    ${DEPS_INCLUDE_DIR}
    ${DEPS_INCLUDE_DIR}/poppler
    ${DEPS_INCLUDE_DIR}/poppler/glib
    ${DEPS_INCLUDE_DIR}/cairo
    ${DEPS_INCLUDE_DIR}/glib-2.0
    ${DEPS_LIB_DIR}/glib-2.0/include
    ${DEPS_INCLUDE_DIR}/freetype2
    ${DEPS_INCLUDE_DIR}/libpng16
)

target_link_directories(_core PRIVATE ${DEPS_LIB_DIR})

target_link_libraries(_core PRIVATE
    poppler-glib
    poppler
    cairo
    gobject-2.0
    glib-2.0
)

# Platform-specific RPATH for the module
if(PLATFORM_MACOS)
    set_target_properties(_core PROPERTIES
        INSTALL_RPATH "@loader_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(PLATFORM_LINUX)
    set_target_properties(_core PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# ============== Installation ==============
install(TARGETS _core LIBRARY DESTINATION pdf_to_svg)

install(DIRECTORY ${DEPS_LIB_DIR}/ DESTINATION lib
    FILES_MATCHING
    PATTERN "${LIB_PATTERN}"
    PATTERN "pkgconfig" EXCLUDE
    PATTERN "cmake" EXCLUDE
)
