#ifndef __SLIPPY_RASTER_H__
#define __SLIPPY_RASTER_H__

// document.export's PNG / JPEG: draws one page of the PDF copy Illustrator
// wrote, at any resolution. Core Graphics on macOS; Windows.Data.Pdf + WIC on
// Windows. No SDK.

#include <cstddef>
#include <string>

namespace slippy {

struct RasterJob {
	std::string pdf;         // UTF-8 path of the PDF copy
	int page = 0;            // from 0
	// The part of the page to draw, in points from its bottom-left corner.
	double left = 0, bottom = 0, width = 0, height = 0;
	size_t pixelsWide = 0, pixelsHigh = 0;
	double dpi = 72;
	bool png = true;         // false: JPEG
	bool transparent = true; // PNG only
	int quality = 90;        // JPEG, 1-100
	std::string out;         // UTF-8 path to write
};

// "" when written; otherwise what went wrong.
std::string RenderPdfPage(const RasterJob& job);

} // namespace slippy

#endif // __SLIPPY_RASTER_H__
