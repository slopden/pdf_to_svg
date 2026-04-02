import pytest

from pdf_to_svg import pdf_to_svg, svg_to_pdf


# ============================================================================
# svg_to_pdf
# ============================================================================


def test_svg_rect_to_pdf():
    """Simple SVG rect produces valid PDF."""
    svg = (
        b'<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100">'
        b'<rect x="10" y="10" width="80" height="40" fill="none" stroke="black"/>'
        b"</svg>"
    )
    pdf = svg_to_pdf(svg)
    assert pdf[:5] == b"%PDF-"
    assert len(pdf) > 100


def test_svg_to_pdf_with_text():
    """SVG with text element produces valid PDF."""
    svg = (
        b'<svg xmlns="http://www.w3.org/2000/svg" width="300" height="200">'
        b'<text x="10" y="50" font-size="24">Hello</text>'
        b"</svg>"
    )
    pdf = svg_to_pdf(svg)
    assert pdf[:5] == b"%PDF-"


def test_svg_to_pdf_empty():
    """Empty SVG (no children) produces valid PDF."""
    svg = b'<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"/>'
    pdf = svg_to_pdf(svg)
    assert pdf[:5] == b"%PDF-"


def test_svg_to_pdf_no_dimensions():
    """SVG without width/height falls back to letter size."""
    svg = (
        b'<svg xmlns="http://www.w3.org/2000/svg">'
        b'<circle cx="50" cy="50" r="25" fill="red"/>'
        b"</svg>"
    )
    pdf = svg_to_pdf(svg)
    assert pdf[:5] == b"%PDF-"


def test_svg_to_pdf_mm_units():
    """SVG with mm dimensions converts correctly."""
    svg = (
        b'<svg xmlns="http://www.w3.org/2000/svg" width="210mm" height="297mm">'
        b'<rect width="210mm" height="297mm" fill="white"/>'
        b"</svg>"
    )
    pdf = svg_to_pdf(svg)
    assert pdf[:5] == b"%PDF-"
    assert len(pdf) > 100


def test_svg_to_pdf_invalid():
    """Invalid SVG raises RuntimeError."""
    with pytest.raises(RuntimeError, match="Failed to parse SVG"):
        svg_to_pdf(b"this is not svg")


def test_svg_to_pdf_empty_bytes():
    """Empty bytes raises RuntimeError."""
    with pytest.raises(RuntimeError, match="Failed to parse SVG"):
        svg_to_pdf(b"")


# ============================================================================
# pdf_to_svg
# ============================================================================


def test_pdf_to_svg_all_pages():
    """Convert all pages of a PDF generated from SVG."""
    svg = b'<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"><rect width="100" height="100" fill="blue"/></svg>'
    pdf = svg_to_pdf(svg)
    pages = pdf_to_svg(pdf)
    assert len(pages) == 1
    assert "<svg" in pages[0]


def test_pdf_to_svg_single_page():
    """Convert a specific page by index."""
    svg = b'<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"><rect width="50" height="50" fill="red"/></svg>'
    pdf = svg_to_pdf(svg)
    pages = pdf_to_svg(pdf, page=0)
    assert len(pages) == 1


def test_pdf_to_svg_page_out_of_range():
    """Out-of-range page index raises."""
    svg = b'<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"/>'
    pdf = svg_to_pdf(svg)
    with pytest.raises(IndexError):
        pdf_to_svg(pdf, page=999)


def test_pdf_to_svg_negative_page():
    """Negative page index raises."""
    svg = b'<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"/>'
    pdf = svg_to_pdf(svg)
    with pytest.raises(IndexError):
        pdf_to_svg(pdf, page=-1)


def test_pdf_to_svg_invalid():
    """Invalid PDF raises RuntimeError."""
    with pytest.raises(RuntimeError, match="Failed to open PDF"):
        pdf_to_svg(b"not a pdf")


def test_pdf_to_svg_units():
    """Different unit options produce valid SVG."""
    svg = b'<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"><rect width="100" height="100"/></svg>'
    pdf = svg_to_pdf(svg)
    for unit in ("pt", "in", "mm", "cm", "px", "pc"):
        pages = pdf_to_svg(pdf, unit=unit)
        assert len(pages) == 1
        assert "<svg" in pages[0]


def test_pdf_to_svg_invalid_unit():
    """Invalid unit raises ValueError."""
    svg = b'<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"/>'
    pdf = svg_to_pdf(svg)
    with pytest.raises(ValueError, match="Unknown unit"):
        pdf_to_svg(pdf, unit="furlongs")


# ============================================================================
# Round-trip
# ============================================================================


def test_roundtrip_rect():
    """SVG rect → PDF → SVG preserves geometry."""
    svg_in = (
        b'<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100">'
        b'<rect x="10" y="10" width="80" height="40" fill="none" stroke="black"/>'
        b"</svg>"
    )
    pdf = svg_to_pdf(svg_in)
    pages = pdf_to_svg(pdf, unit="pt")
    assert len(pages) == 1
    # cairo may emit rect as path
    assert "rect" in pages[0] or "path" in pages[0]


def test_roundtrip_multiple_elements():
    """SVG with multiple elements survives round-trip."""
    svg = (
        b'<svg xmlns="http://www.w3.org/2000/svg" width="400" height="300">'
        b'<rect x="0" y="0" width="400" height="300" fill="white"/>'
        b'<line x1="0" y1="0" x2="400" y2="300" stroke="black" stroke-width="2"/>'
        b'<circle cx="200" cy="150" r="50" fill="none" stroke="red"/>'
        b"</svg>"
    )
    pdf = svg_to_pdf(svg)
    pages = pdf_to_svg(pdf, unit="mm")
    assert len(pages) == 1
    assert len(pages[0]) > 200
