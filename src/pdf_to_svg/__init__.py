from typing import Literal

from ._core import pdf_to_svg

Unit = Literal["pt", "in", "mm", "cm", "px", "pc"]

__all__ = ["pdf_to_svg", "Unit"]
