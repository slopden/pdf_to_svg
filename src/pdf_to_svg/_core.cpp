// Converts PDF to SVG in-memory using poppler and cairo

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <glib.h>
#include <poppler.h>
#include <poppler-document.h>
#include <poppler-page.h>
#include <cairo.h>
#include <cairo-svg.h>

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <stdexcept>

namespace nb = nanobind;

// RAII wrapper for GBytes
struct GBytesDeleter {
    void operator()(GBytes* bytes) const {
        if (bytes) g_bytes_unref(bytes);
    }
};
using GBytesPtr = std::unique_ptr<GBytes, GBytesDeleter>;

// RAII wrapper for PopplerDocument
struct PopplerDocDeleter {
    void operator()(PopplerDocument* doc) const {
        if (doc) g_object_unref(doc);
    }
};
using PopplerDocPtr = std::unique_ptr<PopplerDocument, PopplerDocDeleter>;

// RAII wrapper for PopplerPage
struct PopplerPageDeleter {
    void operator()(PopplerPage* page) const {
        if (page) g_object_unref(page);
    }
};
using PopplerPagePtr = std::unique_ptr<PopplerPage, PopplerPageDeleter>;

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

// Open a PDF document from bytes
static PopplerDocPtr open_document(const char* data, size_t len) {
    // Use g_bytes_new_static because nanobind keeps Python bytes alive during call
    GBytesPtr bytes(g_bytes_new_static(data, len));
    if (!bytes) {
        throw std::runtime_error("Failed to create GBytes");
    }

    GError* error = nullptr;
    PopplerDocument* doc = poppler_document_new_from_bytes(bytes.get(), nullptr, &error);

    if (!doc) {
        std::string msg = "Failed to open PDF document";
        if (error) {
            msg += ": ";
            msg += error->message;
            g_error_free(error);
        }
        throw std::runtime_error(msg);
    }

    return PopplerDocPtr(doc);
}

// Convert a single page to SVG (caller must ensure page_num is valid)
static std::string convert_page(PopplerDocument* doc, int page_num, const UnitInfo& unit_info) {
    PopplerPagePtr page(poppler_document_get_page(doc, page_num));
    if (!page) {
        throw std::runtime_error("Failed to get page " + std::to_string(page_num));
    }

    // poppler returns dimensions in points
    double width_pt, height_pt;
    poppler_page_get_size(page.get(), &width_pt, &height_pt);

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

    poppler_page_render_for_printing(page.get(), cr.get());
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

    UnitInfo unit_info = get_unit_info(unit);
    PopplerDocPtr doc = open_document(ptr, len);
    int n_pages = poppler_document_get_n_pages(doc.get());

    std::vector<std::string> results;

    if (!page.has_value()) {
        // Convert all pages
        results.reserve(n_pages);
        for (int i = 0; i < n_pages; ++i) {
            results.push_back(convert_page(doc.get(), i, unit_info));
        }
    } else {
        // Convert single page, return empty if out of range
        int p = page.value();
        if (p >= 0 && p < n_pages) {
            results.push_back(convert_page(doc.get(), p, unit_info));
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
