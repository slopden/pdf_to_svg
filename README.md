# pdf_to_svg

In-memory PDF/SVG conversion using poppler, librsvg, and cairo.

```python
from pdf_to_svg import svg_to_pdf, pdf_to_svg

pdf = svg_to_pdf(b'<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"><rect width="100" height="100" fill="red"/></svg>')
svg = pdf_to_svg(pdf, page=0, unit="mm")[0]
```
