#ifndef __SLIPPY_FROG_SHAPE_H__
#define __SLIPPY_FROG_SHAPE_H__

// Slippy's silhouette - the head and both eye bumps as one outline, without
// the seams where they overlap - for the ripples that radiate from Slippy.
// Units of D from the box's bottom-left, y up (the panels' mascot space).
// Shared by the Mac and Windows panels; no system headers.

#include <cmath>
#include <vector>

namespace slippy {

struct FrogPoint { double x, y; };

// Must match the drawing: the head ellipse and the bumps at 0.5 +/- eyeX, eyeY.
inline std::vector<FrogPoint> FrogOutline(double eyeX = 0.22, double eyeY = 0.70, double bumpR = 0.2)
{
	const double kTwoPi = 6.28318530717958647692;
	const double hx = 0.5, hy = 0.37, ha = 0.5, hb = 0.31;   // head: box (0, 0.06, 1, 0.62)
	auto inHead = [&](double x, double y) { double u = (x - hx) / ha, v = (y - hy) / hb; return u * u + v * v < 1; };
	auto inBump = [&](double x, double y, double side) { double dx = x - (0.5 + side * eyeX), dy = y - eyeY; return dx * dx + dy * dy < bumpR * bumpR; };

	// Each shape's edge, counterclockwise, split into the runs that are outside the others.
	struct Run { std::vector<FrogPoint> pts; };
	std::vector<Run> runs;
	auto collect = [&](int n, auto at, auto hidden) {
		std::vector<FrogPoint> ring(n);
		std::vector<bool> keep(n);
		for (int i = 0; i < n; i++) { ring[i] = at(kTwoPi * i / n); keep[i] = !hidden(ring[i]); }
		int start = -1;
		for (int i = 0; i < n; i++) if (!keep[i] && keep[(i + 1) % n]) { start = (i + 1) % n; break; }
		if (start < 0) { if (keep[0]) runs.push_back({ring}); return; }   // all or nothing
		for (int k = 0; k < n; k++) {
			int i = (start + k) % n;
			if (!keep[i]) continue;
			if (k == 0 || !keep[(i + n - 1) % n]) runs.push_back({});
			runs.back().pts.push_back(ring[i]);
		}
	};
	collect(720, [&](double t) { return FrogPoint{hx + ha * std::cos(t), hy + hb * std::sin(t)}; },
		[&](FrogPoint p) { return inBump(p.x, p.y, -1) || inBump(p.x, p.y, 1); });
	for (double side : {-1.0, 1.0})
		collect(360, [&](double t) { return FrogPoint{0.5 + side * eyeX + bumpR * std::cos(t), eyeY + bumpR * std::sin(t)}; },
			[&](FrogPoint p) { return inHead(p.x, p.y) || inBump(p.x, p.y, -side); });

	// Chain the runs end to start into one loop.
	std::vector<FrogPoint> out;
	if (runs.empty()) return out;
	std::vector<bool> used(runs.size());
	size_t cur = 0;
	for (size_t n = 0; n < runs.size(); n++) {
		used[cur] = true;
		out.insert(out.end(), runs[cur].pts.begin(), runs[cur].pts.end());
		FrogPoint end = out.back();
		double best = 1e9;
		size_t next = cur;
		for (size_t j = 0; j < runs.size(); j++) {
			if (used[j]) continue;
			double d = std::hypot(runs[j].pts.front().x - end.x, runs[j].pts.front().y - end.y);
			if (d < best) { best = d; next = j; }
		}
		if (next == cur) break;
		cur = next;
	}
	return out;
}

// Where the ripples grow from: the middle of the silhouette.
const double kFrogMidX = 0.5, kFrogMidY = 0.48;

} // namespace slippy

#endif // __SLIPPY_FROG_SHAPE_H__
