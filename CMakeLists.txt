cmake_minimum_required(VERSION 3.17)
project(pdf_to_svg LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(ExternalProject)

# Use a fixed path for deps so auditwheel can find them
set(DEPS_PREFIX "${CMAKE_SOURCE_DIR}/_deps_install")

# ============== Cairo (via Meson) ==============
# Platform-specific cairo configuration
if(WIN32)
    # Use --vsenv to force MSVC environment (ensures correct link.exe is found)
    # On Windows, use DWrite for fonts instead of fontconfig (fc-cache fails on CI)
    set(CAIRO_MESON_OPTIONS
        --vsenv
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
        -Dglib=disabled
        -Dfreetype=enabled
        -Dfontconfig=disabled
        -Ddwrite=enabled
        -Dpng=enabled
        -Dzlib=enabled
    )
else()
    set(CAIRO_MESON_OPTIONS
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
        -Dglib=disabled
        -Dfreetype=enabled
        -Dfontconfig=enabled
        -Dpng=enabled
        -Dzlib=enabled
    )
endif()

ExternalProject_Add(cairo_ext
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/upstream/cairo"
    CONFIGURE_COMMAND meson setup ${CAIRO_MESON_OPTIONS} <BINARY_DIR> <SOURCE_DIR>
    BUILD_COMMAND meson compile -C <BINARY_DIR>
    INSTALL_COMMAND meson install -C <BINARY_DIR>
)
set(POPPLER_DEPENDS cairo_ext)
set(POPPLER_PKG_CONFIG_PATH "${DEPS_PREFIX}/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig:/opt/homebrew/lib/pkgconfig:/usr/local/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

# ============== Poppler (via CMake) ==============
# Use custom CONFIGURE_COMMAND to set PKG_CONFIG_PATH so poppler can find cairo
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
        -DENABLE_GLIB=OFF
        -DENABLE_CPP=OFF
        -DENABLE_UNSTABLE_API_ABI_HEADERS=ON
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
        -S <SOURCE_DIR>
        -B <BINARY_DIR>
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config Release
    DEPENDS ${POPPLER_DEPENDS}
)

# ============== Python Module ==============
# On Windows use find_package (vcpkg), on Unix use pkg-config
# Cairo is built from source on all platforms
if(WIN32)
    # Windows uses DWrite for fonts, not fontconfig
    find_package(Freetype REQUIRED)
    set(FREETYPE_INCLUDE_DIRS ${FREETYPE_INCLUDE_DIRS})
    set(FREETYPE_LIBRARIES Freetype::Freetype)
    set(FONTCONFIG_INCLUDE_DIRS "")
    set(FONTCONFIG_LIBRARIES "")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FREETYPE REQUIRED freetype2)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
endif()
# Cairo paths from our build
set(CAIRO_INCLUDE_DIRS "${DEPS_PREFIX}/include/cairo")
set(CAIRO_LIBRARIES cairo)

find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)
execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE NB_DIR)
list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
find_package(nanobind CONFIG REQUIRED)

# CairoOutputDev is only built as part of poppler-glib, so we compile it ourselves
# Error.cc provides the error() function needed by Object.h macros
# On Windows, Error.cc is already in poppler.dll so we don't compile it again
set(CAIRO_OUTPUT_SOURCES
    ${CMAKE_SOURCE_DIR}/upstream/poppler/poppler/CairoFontEngine.cc
    ${CMAKE_SOURCE_DIR}/upstream/poppler/poppler/CairoOutputDev.cc
    ${CMAKE_SOURCE_DIR}/upstream/poppler/poppler/CairoRescaleBox.cc
)
if(NOT WIN32)
    list(APPEND CAIRO_OUTPUT_SOURCES ${CMAKE_SOURCE_DIR}/upstream/poppler/poppler/Error.cc)
endif()

nanobind_add_module(_core src/pdf_to_svg/_core.cpp ${CAIRO_OUTPUT_SOURCES})
add_dependencies(_core poppler_ext)
target_compile_definitions(_core PRIVATE HAVE_CAIRO)

target_include_directories(_core PRIVATE
    ${CMAKE_SOURCE_DIR}/src  # For our config.h
    ${DEPS_PREFIX}/include
    ${DEPS_PREFIX}/include/poppler
    ${CAIRO_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/upstream/poppler
    ${CMAKE_SOURCE_DIR}/upstream/poppler/poppler
    ${FREETYPE_INCLUDE_DIRS}
    ${FONTCONFIG_INCLUDE_DIRS}
)
# Poppler config defines needed for Cairo sources
target_compile_definitions(_core PRIVATE HAVE_FREETYPE_H)
if(NOT WIN32)
    target_compile_definitions(_core PRIVATE HAVE_FONTCONFIG)
endif()
# Force-include Error.h so error() is declared before Object.h is parsed
if(MSVC)
    target_compile_options(_core PRIVATE "/FI${DEPS_PREFIX}/include/poppler/Error.h")
else()
    target_compile_options(_core PRIVATE "-include" "${DEPS_PREFIX}/include/poppler/Error.h")
endif()

target_link_directories(_core PRIVATE ${DEPS_PREFIX}/lib ${FREETYPE_LIBRARY_DIRS} ${FONTCONFIG_LIBRARY_DIRS})
target_link_libraries(_core PRIVATE poppler ${CAIRO_LIBRARIES} ${FREETYPE_LIBRARIES} ${FONTCONFIG_LIBRARIES})

# ============== Install ==============
install(TARGETS _core LIBRARY DESTINATION pdf_to_svg)
install(DIRECTORY ${DEPS_PREFIX}/lib/ DESTINATION pdf_to_svg/.libs
    FILES_MATCHING PATTERN "*.so*" PATTERN "*.dylib*" PATTERN "*.dll"
    PATTERN "pkgconfig" EXCLUDE PATTERN "cmake" EXCLUDE
)
