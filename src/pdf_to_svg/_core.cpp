// PDF <-> SVG conversion using poppler, librsvg, and cairo

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
#include <cairo-pdf.h>
#include <librsvg/rsvg.h>

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <stdexcept>
#include <cstring>
#include <mutex>

namespace nb = nanobind;

// ============================================================================
// RAII wrappers
// ============================================================================

struct CairoSurfaceDeleter {
    void operator()(cairo_surface_t* s) const { if (s) cairo_surface_destroy(s); }
};
using SurfacePtr = std::unique_ptr<cairo_surface_t, CairoSurfaceDeleter>;

struct CairoDeleter {
    void operator()(cairo_t* cr) const { if (cr) cairo_destroy(cr); }
};
using CairoPtr = std::unique_ptr<cairo_t, CairoDeleter>;

struct RsvgHandleDeleter {
    void operator()(RsvgHandle* h) const { if (h) g_object_unref(h); }
};
using RsvgPtr = std::unique_ptr<RsvgHandle, RsvgHandleDeleter>;

// ============================================================================
// Shared helpers
// ============================================================================

// Write callback: appends data to a std::string
static cairo_status_t write_to_string(void* closure, const unsigned char* data, unsigned int length) {
    static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}

// Create a cairo context on a surface, checking both for errors
static CairoPtr make_cairo(cairo_surface_t* surface, const char* what) {
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        throw std::runtime_error(std::string("Failed to create ") + what + " surface");
    CairoPtr cr(cairo_create(surface));
    if (cairo_status(cr.get()) != CAIRO_STATUS_SUCCESS)
        throw std::runtime_error(std::string("Failed to create cairo context for ") + what);
    return cr;
}

// Flush output by destroying cairo context then surface (order matters)
static void flush(CairoPtr& cr, SurfacePtr& surface) {
    cr.reset();
    surface.reset();
}

// ============================================================================
// Poppler global state
// ============================================================================

// Release GlobalParams before C++ static destructors run.
// We compile CairoOutputDev.cc into this .so alongside libpoppler — both
// have file-scope statics whose destruction order is undefined. Releasing
// GlobalParams in atexit (which runs before static destructors) avoids
// use-after-free on shutdown.
static std::once_flag g_init_flag;

static void release_global_params() {
    globalParams.reset();
}

static void ensure_global_params() {
    std::call_once(g_init_flag, [] {
        globalParams = std::make_unique<GlobalParams>();
        std::atexit(release_global_params);
    });
}

// ============================================================================
// PDF → SVG
// ============================================================================

struct UnitInfo {
    cairo_svg_unit_t cairo_unit;
    double from_points;
};

static UnitInfo get_unit_info(const std::string& unit) {
    if (unit == "pt") return {CAIRO_SVG_UNIT_PT, 1.0};
    if (unit == "in") return {CAIRO_SVG_UNIT_IN, 1.0 / 72.0};
    if (unit == "mm") return {CAIRO_SVG_UNIT_MM, 25.4 / 72.0};
    if (unit == "cm") return {CAIRO_SVG_UNIT_CM, 2.54 / 72.0};
    if (unit == "px") return {CAIRO_SVG_UNIT_PX, 1.0};
    if (unit == "pc") return {CAIRO_SVG_UNIT_PC, 1.0 / 12.0};
    throw std::invalid_argument("Unknown unit: " + unit);
}

static std::string convert_page(PDFDoc* doc, int page_num, const UnitInfo& ui) {
    double w = doc->getPageMediaWidth(page_num) * ui.from_points;
    double h = doc->getPageMediaHeight(page_num) * ui.from_points;

    std::string out;
    out.reserve(65536);

    SurfacePtr surface(cairo_svg_surface_create_for_stream(write_to_string, &out, w, h));
    auto cr = make_cairo(surface.get(), "SVG");
    cairo_svg_surface_set_document_unit(surface.get(), ui.cairo_unit);
    cairo_scale(cr.get(), ui.from_points, ui.from_points);

    CairoOutputDev dev;
    dev.setCairo(cr.get());
    dev.startDoc(doc);
    doc->displayPage(&dev, page_num, 72.0, 72.0, 0, true, false, true);
    cairo_show_page(cr.get());

    flush(cr, surface);
    return out;
}

std::vector<std::string> pdf_to_svg(const nb::bytes& data, std::optional<int> page, const std::string& unit = "pt") {
    ensure_global_params();
    UnitInfo ui = get_unit_info(unit);

    size_t len = data.size();
    char* copy = static_cast<char*>(gmalloc(len));
    std::memcpy(copy, data.c_str(), len);

    Object obj;
    PDFDoc doc(std::make_unique<MemStream>(copy, 0, len, std::move(obj)));
    if (!doc.isOk())
        throw std::runtime_error("Failed to open PDF (error " + std::to_string(doc.getErrorCode()) + ")");

    int n = doc.getNumPages();
    std::vector<std::string> results;

    if (!page.has_value()) {
        results.reserve(n);
        for (int i = 1; i <= n; ++i)
            results.push_back(convert_page(&doc, i, ui));
    } else {
        int p = page.value() + 1;
        if (p < 1 || p > n)
            throw std::out_of_range("Page " + std::to_string(page.value()) + " out of range [0, " + std::to_string(n - 1) + "]");
        results.push_back(convert_page(&doc, p, ui));
    }

    return results;
}

// ============================================================================
// SVG → PDF
// ============================================================================

// Convert an RsvgLength to points
static double rsvg_length_to_pt(RsvgLength len) {
    switch (len.unit) {
        case RSVG_UNIT_PT:   return len.length;
        case RSVG_UNIT_PX:   return len.length * 0.75;        // 96 DPI → 72 DPI
        case RSVG_UNIT_IN:   return len.length * 72.0;
        case RSVG_UNIT_CM:   return len.length * 72.0 / 2.54;
        case RSVG_UNIT_MM:   return len.length * 72.0 / 25.4;
        default:             return len.length;                // EM/EX/% — treat as pt
    }
}

nb::bytes svg_to_pdf(const nb::bytes& data) {
    GError* err = nullptr;
    RsvgPtr handle(rsvg_handle_new_from_data(
        reinterpret_cast<const guint8*>(data.c_str()), data.size(), &err));
    if (!handle) {
        std::string msg = "Failed to parse SVG";
        if (err) { msg += ": "; msg += err->message; g_error_free(err); }
        throw std::runtime_error(msg);
    }

    // get intrinsic dimensions
    gboolean has_w = FALSE, has_h = FALSE;
    RsvgLength rw, rh;
    rsvg_handle_get_intrinsic_dimensions(handle.get(), &has_w, &rw, &has_h, &rh, nullptr, nullptr);

    double w = (has_w && rw.length > 0) ? rsvg_length_to_pt(rw) : 612.0;  // letter
    double h = (has_h && rh.length > 0) ? rsvg_length_to_pt(rh) : 792.0;

    std::string out;
    out.reserve(65536);

    SurfacePtr surface(cairo_pdf_surface_create_for_stream(write_to_string, &out, w, h));
    auto cr = make_cairo(surface.get(), "PDF");

    RsvgRectangle viewport = {0, 0, w, h};
    err = nullptr;
    if (!rsvg_handle_render_document(handle.get(), cr.get(), &viewport, &err)) {
        std::string msg = "Failed to render SVG";
        if (err) { msg += ": "; msg += err->message; g_error_free(err); }
        throw std::runtime_error(msg);
    }

    cairo_show_page(cr.get());
    flush(cr, surface);

    return nb::bytes(out.data(), out.size());
}

// ============================================================================
// Python module
// ============================================================================

NB_MODULE(_core, m) {
    m.doc() = "PDF/SVG conversion using poppler, librsvg, and cairo";

    m.def("pdf_to_svg", &pdf_to_svg,
          nb::arg("data"), nb::arg("page") = nb::none(), nb::arg("unit") = "pt",
          "Convert PDF pages to SVG strings.\n\n"
          "Args:\n"
          "    data: PDF file as bytes\n"
          "    page: 0-based page index, or None for all pages\n"
          "    unit: SVG dimension unit (pt, in, mm, cm, px, pc)\n\n"
          "Returns:\n"
          "    List of SVG strings, one per page.\n\n"
          "Raises:\n"
          "    RuntimeError: if the PDF is invalid\n"
          "    out_of_range: if page index is out of bounds");

    m.def("svg_to_pdf", &svg_to_pdf,
          nb::arg("data"),
          "Convert SVG to PDF.\n\n"
          "Args:\n"
          "    data: SVG file as bytes\n\n"
          "Returns:\n"
          "    PDF file as bytes.");
}
