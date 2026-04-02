cmake_minimum_required(VERSION 3.17)
project(pdf_to_svg LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(ExternalProject)

# all vendored deps install here so auditwheel/delocate can find them
set(DEPS_PREFIX "${CMAKE_SOURCE_DIR}/_deps_install")

# ============== Cairo (via Meson) ==============
set(CAIRO_MESON_OPTIONS
    --prefix=${DEPS_PREFIX} --libdir=lib --buildtype=release
    -Ddefault_library=shared -Dtests=disabled -Dgtk_doc=false
    -Dlzo=disabled -Dquartz=disabled -Dxcb=disabled -Dxlib=disabled
    -Dxlib-xcb=disabled -Dtee=disabled -Dspectre=disabled
    -Dsymbol-lookup=disabled -Dglib=disabled
    -Dfreetype=enabled -Dpng=enabled -Dzlib=enabled
    -Dfreetype:harfbuzz=disabled)
if(WIN32)
    list(APPEND CAIRO_MESON_OPTIONS --vsenv -Dfontconfig=disabled -Ddwrite=enabled)
else()
    list(APPEND CAIRO_MESON_OPTIONS -Dfontconfig=enabled)
endif()

ExternalProject_Add(cairo_ext
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/upstream/cairo"
    CONFIGURE_COMMAND meson setup ${CAIRO_MESON_OPTIONS} <BINARY_DIR> <SOURCE_DIR>
    BUILD_COMMAND meson compile -C <BINARY_DIR>
    INSTALL_COMMAND meson install -C <BINARY_DIR>)

# ============== Poppler (via CMake) ==============
set(POPPLER_PKG_CONFIG_PATH
    "${DEPS_PREFIX}/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig:/opt/homebrew/lib/pkgconfig:/usr/local/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

ExternalProject_Add(poppler_ext
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/upstream/poppler"
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        "PKG_CONFIG_PATH=${POPPLER_PKG_CONFIG_PATH}"
        ${CMAKE_COMMAND}
        -DCMAKE_INSTALL_PREFIX=${DEPS_PREFIX}
        -DCMAKE_PREFIX_PATH=${DEPS_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_CXX_STANDARD=20
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DENABLE_GLIB=OFF -DENABLE_CPP=OFF
        -DENABLE_UNSTABLE_API_ABI_HEADERS=ON
        -DENABLE_QT5=OFF -DENABLE_QT6=OFF -DENABLE_UTILS=OFF
        -DENABLE_BOOST=OFF -DENABLE_LIBCURL=OFF -DENABLE_LIBTIFF=OFF
        -DENABLE_NSS3=OFF -DENABLE_GPGME=OFF -DENABLE_LCMS=OFF
        -DENABLE_DCTDECODER=none -DENABLE_LIBOPENJPEG=none
        -DENABLE_GOBJECT_INTROSPECTION=OFF -DENABLE_GTK_DOC=OFF
        -DBUILD_GTK_TESTS=OFF -DBUILD_QT5_TESTS=OFF -DBUILD_QT6_TESTS=OFF
        -DBUILD_CPP_TESTS=OFF -DBUILD_MANUAL_TESTS=OFF
        -S <SOURCE_DIR> -B <BINARY_DIR>
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config Release
    DEPENDS cairo_ext)

# ============== System dependencies ==============
# pkg-config available on all platforms (system on Linux/macOS, MSYS2 on Windows)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(RSVG REQUIRED librsvg-2.0)
set(SYS_INCLUDE_DIRS ${FREETYPE_INCLUDE_DIRS} ${RSVG_INCLUDE_DIRS})
set(SYS_LIBRARIES ${FREETYPE_LIBRARIES} ${RSVG_LIBRARIES})
set(SYS_LINK_DIRS ${FREETYPE_LIBRARY_DIRS} ${RSVG_LIBRARY_DIRS})
if(NOT WIN32)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
    list(APPEND SYS_INCLUDE_DIRS ${FONTCONFIG_INCLUDE_DIRS})
    list(APPEND SYS_LIBRARIES ${FONTCONFIG_LIBRARIES})
    list(APPEND SYS_LINK_DIRS ${FONTCONFIG_LIBRARY_DIRS})
endif()

# ============== Python module ==============
find_package(Python 3.12 COMPONENTS Interpreter Development.Module REQUIRED)
execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE NB_DIR)
list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
find_package(nanobind CONFIG REQUIRED)

# poppler doesn't export CairoOutputDev symbols — compile them into our module
set(CAIRO_OUTPUT_SOURCES
    ${CMAKE_SOURCE_DIR}/upstream/poppler/poppler/CairoFontEngine.cc
    ${CMAKE_SOURCE_DIR}/upstream/poppler/poppler/CairoOutputDev.cc
    ${CMAKE_SOURCE_DIR}/upstream/poppler/poppler/CairoRescaleBox.cc)
if(NOT WIN32)
    list(APPEND CAIRO_OUTPUT_SOURCES
        ${CMAKE_SOURCE_DIR}/upstream/poppler/poppler/Error.cc)
endif()

nanobind_add_module(_core STABLE_ABI src/pdf_to_svg/_core.cpp ${CAIRO_OUTPUT_SOURCES})
add_dependencies(_core poppler_ext)

target_compile_definitions(_core PRIVATE HAVE_CAIRO HAVE_FREETYPE_H)
if(NOT WIN32)
    target_compile_definitions(_core PRIVATE HAVE_FONTCONFIG)
endif()

if(MSVC)
    target_compile_options(_core PRIVATE "/FI${DEPS_PREFIX}/include/poppler/Error.h")
else()
    target_compile_options(_core PRIVATE "-include" "${DEPS_PREFIX}/include/poppler/Error.h")
endif()

target_include_directories(_core PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${DEPS_PREFIX}/include
    ${DEPS_PREFIX}/include/poppler
    ${DEPS_PREFIX}/include/cairo
    ${CMAKE_SOURCE_DIR}/upstream/poppler         # for goo/ft_utils.h
    ${CMAKE_SOURCE_DIR}/upstream/poppler/poppler # for CairoOutputDev.h
    ${SYS_INCLUDE_DIRS})

target_link_directories(_core PRIVATE ${DEPS_PREFIX}/lib ${SYS_LINK_DIRS})
target_link_libraries(_core PRIVATE poppler cairo ${SYS_LIBRARIES})

# RPATH so the installed .so finds bundled libs relative to itself
set_target_properties(_core PROPERTIES BUILD_RPATH "${DEPS_PREFIX}/lib")
if(APPLE)
    set_target_properties(_core PROPERTIES INSTALL_RPATH "@loader_path/.libs")
elseif(UNIX)
    set_target_properties(_core PROPERTIES INSTALL_RPATH "$ORIGIN/.libs")
endif()

# ============== Install ==============
install(TARGETS _core LIBRARY DESTINATION pdf_to_svg)
install(DIRECTORY ${DEPS_PREFIX}/lib/ DESTINATION pdf_to_svg/.libs
    FILES_MATCHING PATTERN "*.so*" PATTERN "*.dylib*" PATTERN "*.dll"
    PATTERN "pkgconfig" EXCLUDE PATTERN "cmake" EXCLUDE)
