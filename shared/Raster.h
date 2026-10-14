#ifndef __SLIPPY_RASTER_H__
#define __SLIPPY_RASTER_H__

// document.export's PNG / JPEG: stacks the layers Illustrator rendered and
// writes them at the asked size, resolution and quality. ImageIO / Core
// Graphics on macOS; WIC on Windows. No SDK.

#include "Json.h"

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

// A picture for an agent that can't open files on this computer: the PNG at
// 'png' as a JPEG on white, at most maxSide pixels on its long side. Adds
// "image": {mimeType, data (base64)} and the sent width / height to 'result'
// (Mcp.cpp makes "image" an MCP image block) and removes the PNG.
// "" when done; otherwise what went wrong.
std::string InlineImage(const std::string& png, json::Value& result, size_t maxSide = 1024);

} // namespace slippy

#endif // __SLIPPY_RASTER_H__
