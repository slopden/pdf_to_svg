from typing import Literal

from ._core import (
    pdf_to_svg as _pdf_to_svg,
    pdf_to_svg_all as _pdf_to_svg_all,
    get_page_count as _get_page_count,
)

Unit = Literal["pt", "in", "mm", "cm", "px", "pc"]


def pdf_to_svg(
    data: bytes,
    page: int = 0,
    unit: Unit = "pt",
) -> str:
    """
    Convert a single page of a PDF to SVG.

    Parameters
    ----------
    data
        PDF file contents as bytes.
    page
        Page number (0-indexed).
    unit
        SVG unit for dimensions: pt, in, mm, cm, px, pc.

    Returns
    -------
    str
        SVG content as a string.
    """
    return _pdf_to_svg(data, page, unit)


def pdf_to_svg_all(
    data: bytes,
    unit: Unit = "pt",
) -> list[str]:
    """
    Convert all pages of a PDF to SVG.

    Parameters
    ----------
    data
        PDF file contents as bytes.
    unit
        SVG unit for dimensions: pt, in, mm, cm, px, pc.

    Returns
    -------
    list[str]
        List of SVG strings, one per page.
    """
    return _pdf_to_svg_all(data, unit)


def get_page_count(data: bytes) -> int:
    """
    Get the number of pages in a PDF.

    Parameters
    ----------
    data
        PDF file contents as bytes.

    Returns
    -------
    int
        Number of pages.
    """
    return _get_page_count(data)


__all__ = ["pdf_to_svg", "pdf_to_svg_all", "get_page_count", "Unit"]
