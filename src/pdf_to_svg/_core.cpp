// Converts PDF to SVG in-memory using poppler core C++ API and cairo

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <PDFDoc.h>
#include <GlobalParams.h>
#include <CairoOutputDev.h>
#include <Object.h>
#include <goo/gmem.h>

#include <cairo.h>
#include <cairo-svg.h>

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <stdexcept>
#include <cstring>

namespace nb = nanobind;

// RAII wrapper for cairo_surface_t
struct CairoSurfaceDeleter {
    void operator()(cairo_surface_t* surface) const {
        if (surface) cairo_surface_destroy(surface);
    }
};
using CairoSurfacePtr = std::unique_ptr<cairo_surface_t, CairoSurfaceDeleter>;

// RAII wrapper for cairo_t
struct CairoDeleter {
    void operator()(cairo_t* cr) const {
        if (cr) cairo_destroy(cr);
    }
};
using CairoPtr = std::unique_ptr<cairo_t, CairoDeleter>;

// Write callback for cairo_svg_surface_create_for_stream
static cairo_status_t write_callback(void* closure, const unsigned char* data, unsigned int length) {
    std::string* output = static_cast<std::string*>(closure);
    output->append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}

// Unit conversion info
struct UnitInfo {
    cairo_svg_unit_t cairo_unit;
    double from_points;  // multiply points by this to get target unit
};

static UnitInfo get_unit_info(const std::string& unit) {
    if (unit == "pt") return {CAIRO_SVG_UNIT_PT, 1.0};
    if (unit == "in") return {CAIRO_SVG_UNIT_IN, 1.0 / 72.0};
    if (unit == "mm") return {CAIRO_SVG_UNIT_MM, 25.4 / 72.0};
    if (unit == "cm") return {CAIRO_SVG_UNIT_CM, 2.54 / 72.0};
    if (unit == "px") return {CAIRO_SVG_UNIT_PX, 1.0};  // 1:1 with points at 72 DPI
    if (unit == "pc") return {CAIRO_SVG_UNIT_PC, 1.0 / 12.0};  // 12 points per pica
    throw std::invalid_argument("Unknown unit: " + unit + ". Valid units: pt, in, mm, cm, px, pc");
}

// Initialize global params once
static void ensure_global_params() {
    static bool initialized = false;
    if (!initialized) {
        globalParams = std::make_unique<GlobalParams>();
        initialized = true;
    }
}

// Convert a single page to SVG (page_num is 1-indexed for PDFDoc)
static std::string convert_page(PDFDoc* doc, int page_num, const UnitInfo& unit_info) {
    // Get page dimensions in points (1-indexed)
    double width_pt = doc->getPageMediaWidth(page_num);
    double height_pt = doc->getPageMediaHeight(page_num);

    // Convert to target unit for SVG dimensions
    double width = width_pt * unit_info.from_points;
    double height = height_pt * unit_info.from_points;

    std::string svg_output;
    svg_output.reserve(65536);  // Pre-allocate for typical SVG size

    CairoSurfacePtr surface(
        cairo_svg_surface_create_for_stream(write_callback, &svg_output, width, height)
    );
    if (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS) {
        throw std::runtime_error("Failed to create SVG surface");
    }

    cairo_svg_surface_set_document_unit(surface.get(), unit_info.cairo_unit);

    CairoPtr cr(cairo_create(surface.get()));
    if (cairo_status(cr.get()) != CAIRO_STATUS_SUCCESS) {
        throw std::runtime_error("Failed to create cairo context");
    }

    // Scale the rendering to match the unit conversion
    cairo_scale(cr.get(), unit_info.from_points, unit_info.from_points);

    // Create CairoOutputDev and render
    CairoOutputDev output_dev;
    output_dev.setCairo(cr.get());
    output_dev.startDoc(doc);

    // displayPage params: outputDev, page, hDPI, vDPI, rotate, useMediaBox, crop, printing
    doc->displayPage(&output_dev, page_num, 72.0, 72.0, 0, true, false, true);

    cairo_show_page(cr.get());

    // Destroy in order to flush output
    cr.reset();
    surface.reset();

    return svg_output;
}

// Python-exposed function: convert PDF pages to SVG
std::vector<std::string> pdf_to_svg(nb::bytes data, std::optional<int> page, const std::string& unit = "pt") {
    const char* ptr = data.c_str();
    size_t len = data.size();

    ensure_global_params();
    UnitInfo unit_info = get_unit_info(unit);

    // Create a copy of the data that PDFDoc can own
    // MemStream requires a non-const char* and may modify/free it
    char* data_copy = static_cast<char*>(gmalloc(len));
    std::memcpy(data_copy, ptr, len);

    // Create memory stream - Object() creates a null object for dict
    // MemStream takes ownership of data_copy
    Object obj;
    auto stream = std::make_unique<MemStream>(data_copy, 0, len, std::move(obj));

    // Create PDFDoc - takes ownership of stream
    PDFDoc doc(std::move(stream));

    if (!doc.isOk()) {
        int error_code = doc.getErrorCode();
        throw std::runtime_error("Failed to open PDF document (error code: " + std::to_string(error_code) + ")");
    }

    int n_pages = doc.getNumPages();
    std::vector<std::string> results;

    if (!page.has_value()) {
        // Convert all pages
        results.reserve(n_pages);
        for (int i = 1; i <= n_pages; ++i) {  // 1-indexed
            results.push_back(convert_page(&doc, i, unit_info));
        }
    } else {
        // Convert single page (user provides 0-based, PDFDoc uses 1-based)
        int p = page.value() + 1;  // Convert to 1-indexed
        if (p >= 1 && p <= n_pages) {
            results.push_back(convert_page(&doc, p, unit_info));
        }
    }

    return results;
}

NB_MODULE(_core, m) {
    m.doc() = "PDF to SVG conversion using poppler and cairo";

    m.def("pdf_to_svg", &pdf_to_svg,
          nb::arg("data"), nb::arg("page") = nb::none(), nb::arg("unit") = "pt",
          "Convert PDF to SVG.\n\n"
          "Args:\n"
          "    data: PDF file contents as bytes\n"
          "    page: Page index (0-based), or None for all pages\n"
          "    unit: SVG dimension unit (pt, in, mm, cm, px, pc)\n\n"
          "Returns:\n"
          "    List of SVG strings. Empty if page doesn't exist.");
}
