import os
import sys
from typing import Literal

# on macOS/Windows, help dlopen find bundled libs
# on Linux, RPATH ($ORIGIN/.libs) handles this at build time
_libs = os.path.join(os.path.dirname(__file__), '.libs')
if os.path.exists(_libs):
    if sys.platform == 'darwin':
        os.environ['DYLD_LIBRARY_PATH'] = _libs + ':' + os.environ.get('DYLD_LIBRARY_PATH', '')
    elif sys.platform == 'win32':
        os.add_dll_directory(_libs)

from ._core import pdf_to_svg, svg_to_pdf

Unit = Literal["pt", "in", "mm", "cm", "px", "pc"]

__all__ = ["pdf_to_svg", "svg_to_pdf", "Unit"]
