import os
import re

import pytest

from pdf_to_svg import pdf_to_svg


def get(file_name: str) -> bytes:
    cwd = os.path.abspath(os.path.dirname(__file__))
    with open(os.path.join(cwd, "data", file_name), "rb") as f:
        return f.read()


def test_invalid_pdf():
    """Test that invalid PDF data raises an error."""
    with pytest.raises(RuntimeError, match="Failed to open PDF"):
        pdf_to_svg(b"not a pdf")


def test_empty_pdf():
    """Test that empty data raises an error."""
    with pytest.raises(RuntimeError, match="Failed to open PDF"):
        pdf_to_svg(b"")


def path_aabb(d: str) -> tuple[float, float, float, float]:
    """Return (min_x, min_y, max_x, max_y) from SVG path coordinates."""
    nums = [float(n) for n in re.findall(r"[-+]?\d*\.?\d+", d)]
    xs, ys = nums[0::2], nums[1::2]
    return min(xs), min(ys), max(xs), max(ys)


def test_units():
    """Test that units are reflected in the SVG output."""
    data = get("test_500_1000_mm.pdf")

    # Test mm units - page is 2000x2000mm with a 500x1000mm rectangle
    svg_mm = pdf_to_svg(data, page=0, unit="mm")[0]
    assert 'width="2000mm"' in svg_mm
    assert 'height="2000mm"' in svg_mm

    # Check rectangle dimensions from path
    path_d = re.search(r'd="([^"]+)"', svg_mm).group(1)
    min_x, min_y, max_x, max_y = path_aabb(path_d)
    assert abs((max_x - min_x) - 500) < 1
    assert abs((max_y - min_y) - 1000) < 1

    # Test inch units
    svg_in = pdf_to_svg(data, page=0, unit="in")[0]
    assert re.search(r'width="[0-9.]+in"', svg_in)

    # Test pt units (default)
    svg_pt = pdf_to_svg(data, page=0, unit="pt")[0]
    assert re.search(r'width="[0-9.]+pt"', svg_pt)


def test_page_out_of_range():
    """Test that out-of-range page numbers raise IndexError."""
    import pytest

    data = get("test_500_1000_mm.pdf")
    with pytest.raises(IndexError):
        pdf_to_svg(data, page=999)


def test_single_page():
    """Test basic single page conversion."""
    data = get("test_500_1000_mm.pdf")
    result = pdf_to_svg(data, page=0)
    assert isinstance(result, list)
    assert len(result) == 1
    svg = result[0]
    assert isinstance(svg, str)
    assert "<svg" in svg
    assert "</svg>" in svg


def test_all_pages():
    """Test converting all pages (page=None)."""
    data = get("test_500_1000_mm.pdf")
    result = pdf_to_svg(data)
    assert isinstance(result, list)
    assert len(result) >= 1
    for svg in result:
        assert isinstance(svg, str)
        assert "</svg>" in svg
