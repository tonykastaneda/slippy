#ifndef __SLIPPY_RASTER_H__
#define __SLIPPY_RASTER_H__

// document.export's PNG / JPEG: stacks the layers Illustrator rendered and
// writes them at the asked size, resolution and quality. ImageIO / Core
// Graphics on macOS; WIC on Windows. No SDK.

#include <cstddef>
#include <string>
#include <vector>

namespace slippy {

struct RasterJob {
	std::vector<std::string> images;   // UTF-8 paths of Illustrator's renders, stacked bottom first (EncodeImage)
	size_t pixelsWide = 0, pixelsHigh = 0;
	double dpi = 72;
	bool png = true;         // false: JPEG
	bool transparent = true; // PNG only
	int quality = 90;        // JPEG, 1-100
	std::string out;         // UTF-8 path to write
};

// "" when written; otherwise what went wrong.
std::string EncodeImage(const RasterJob& job);

} // namespace slippy

#endif // __SLIPPY_RASTER_H__
