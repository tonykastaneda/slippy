#ifndef __SLIPPY_AGENT_LOGO_H__
#define __SLIPPY_AGENT_LOGO_H__

// The agents' logos for the panel's "who's calling" mark. Each is an SVG
// path on a 24 x 24 grid (Resources/agents/<Agent>.svg, from the Lobe Icons
// and Simple Icons sets), built in by tools/agent_logos.py. AgentLogo parses
// one into moves, lines, cubic curves and closes - arcs and the shorthand
// commands included - that both panels draw. No system headers.

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace slippy {

struct LogoOp {
	char op;          // 'M', 'L', 'C', 'Z' - absolute coordinates, y down
	double v[6];      // M / L: x y; C: x1 y1 x2 y2 x y
};

struct AgentLogo {
	const char* agent;
	int color;        // 0xRRGGBB, or -1: the panel's text color (single-color marks)
	bool evenOdd;
	const char* path; // SVG path data
};

namespace logo_detail {

struct Reader {
	const char* p;
	void Skip() { while (*p && (isspace((unsigned char) *p) || *p == ',')) p++; }
	bool Number(double& out)
	{
		Skip();
		const char* s = p;
		if (*s == '+' || *s == '-') s++;
		bool digits = false, dot = false;
		while (isdigit((unsigned char) *s) || (*s == '.' && !dot)) { if (*s == '.') dot = true; else digits = true; s++; }
		if (!digits) return false;
		if (*s == 'e' || *s == 'E') { const char* e = s + 1; if (*e == '+' || *e == '-') e++; if (isdigit((unsigned char) *e)) { s = e; while (isdigit((unsigned char) *s)) s++; } }
		out = strtod(std::string(p, s).c_str(), nullptr);
		p = s;
		return true;
	}
	bool Flag(double& out)   // arc flags may be packed: "a1 1 0 110 3"
	{
		Skip();
		if (*p != '0' && *p != '1') return false;
		out = *p++ - '0';
		return true;
	}
};

// One SVG arc as cubic curves (endpoint to center parameterization, SVG spec F.6).
inline void Arc(std::vector<LogoOp>& out, double x0, double y0, double rx, double ry, double phiDeg, bool large, bool sweep, double x, double y)
{
	if (rx == 0 || ry == 0) { out.push_back({'L', {x, y}}); return; }
	const double kPi = 3.14159265358979323846;
	double phi = phiDeg * kPi / 180, c = std::cos(phi), s = std::sin(phi);
	double dx = (x0 - x) / 2, dy = (y0 - y) / 2;
	double x1 = c * dx + s * dy, y1 = -s * dx + c * dy;
	rx = std::fabs(rx); ry = std::fabs(ry);
	double l = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
	if (l > 1) { rx *= std::sqrt(l); ry *= std::sqrt(l); }
	double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1, den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
	double k = (large == sweep ? -1 : 1) * std::sqrt(std::max(0.0, num / den));
	double cx1 = k * rx * y1 / ry, cy1 = -k * ry * x1 / rx;
	double cx = c * cx1 - s * cy1 + (x0 + x) / 2, cy = s * cx1 + c * cy1 + (y0 + y) / 2;
	auto angle = [](double ux, double uy, double vx, double vy) {
		double a = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
		return a;
	};
	double t1 = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
	double dt = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
	if (!sweep && dt > 0) dt -= 2 * kPi;
	if (sweep && dt < 0) dt += 2 * kPi;
	int n = (int) std::ceil(std::fabs(dt) / (kPi / 2) - 1e-9);
	if (n < 1) n = 1;
	double step = dt / n, e = 4.0 / 3.0 * std::tan(step / 4);
	auto point = [&](double t, double& px, double& py, double& tx, double& ty) {
		double ct = std::cos(t), st = std::sin(t);
		px = cx + rx * c * ct - ry * s * st;
		py = cy + rx * s * ct + ry * c * st;
		tx = -rx * c * st - ry * s * ct;
		ty = -rx * s * st + ry * c * ct;
	};
	for (int i = 0; i < n; i++) {
		double a = t1 + i * step, b = a + step, ax, ay, atx, aty, bx, by, btx, bty;
		point(a, ax, ay, atx, aty);
		point(b, bx, by, btx, bty);
		if (i == n - 1) { bx = x; by = y; }
		out.push_back({'C', {ax + e * atx, ay + e * aty, bx - e * btx, by - e * bty, bx, by}});
	}
}

} // namespace logo_detail

// SVG path data -> absolute moves, lines, cubics and closes.
inline std::vector<LogoOp> ParseLogo(const char* d)
{
	using namespace logo_detail;
	std::vector<LogoOp> out;
	Reader r{d};
	char cmd = 0;
	double x = 0, y = 0, sx = 0, sy = 0;     // current point, subpath start
	double cx = 0, cy = 0;                   // last control point (for S / T)
	char last = 0;
	for (;;) {
		r.Skip();
		if (!*r.p) break;
		if (isalpha((unsigned char) *r.p)) cmd = *r.p++;
		else if (!cmd) break;
		bool rel = islower((unsigned char) cmd) != 0;
		char C = (char) toupper((unsigned char) cmd);
		double ox = rel ? x : 0, oy = rel ? y : 0;
		double a[7];
		auto nums = [&](int n) { for (int i = 0; i < n; i++) if (!r.Number(a[i])) return false; return true; };
		if (C == 'Z') { out.push_back({'Z', {0}}); x = sx; y = sy; last = 'Z'; cmd = 0; continue; }
		if (C == 'M') {
			if (!nums(2)) break;
			x = ox + a[0]; y = oy + a[1]; sx = x; sy = y;
			out.push_back({'M', {x, y}});
			cmd = rel ? 'l' : 'L';   // more pairs after a move are lines
		}
		else if (C == 'L') { if (!nums(2)) break; x = ox + a[0]; y = oy + a[1]; out.push_back({'L', {x, y}}); }
		else if (C == 'H') { if (!nums(1)) break; x = ox + a[0]; out.push_back({'L', {x, y}}); }
		else if (C == 'V') { if (!nums(1)) break; y = oy + a[0]; out.push_back({'L', {x, y}}); }
		else if (C == 'C') {
			if (!nums(6)) break;
			out.push_back({'C', {ox + a[0], oy + a[1], ox + a[2], oy + a[3], ox + a[4], oy + a[5]}});
			cx = ox + a[2]; cy = oy + a[3]; x = ox + a[4]; y = oy + a[5];
		}
		else if (C == 'S') {
			if (!nums(4)) break;
			double c1x = (last == 'C' || last == 'S') ? 2 * x - cx : x, c1y = (last == 'C' || last == 'S') ? 2 * y - cy : y;
			out.push_back({'C', {c1x, c1y, ox + a[0], oy + a[1], ox + a[2], oy + a[3]}});
			cx = ox + a[0]; cy = oy + a[1]; x = ox + a[2]; y = oy + a[3];
		}
		else if (C == 'Q' || C == 'T') {
			double qx, qy, ex, ey;
			if (C == 'Q') { if (!nums(4)) break; qx = ox + a[0]; qy = oy + a[1]; ex = ox + a[2]; ey = oy + a[3]; }
			else { if (!nums(2)) break; qx = (last == 'Q' || last == 'T') ? 2 * x - cx : x; qy = (last == 'Q' || last == 'T') ? 2 * y - cy : y; ex = ox + a[0]; ey = oy + a[1]; }
			out.push_back({'C', {x + 2.0 / 3 * (qx - x), y + 2.0 / 3 * (qy - y), ex + 2.0 / 3 * (qx - ex), ey + 2.0 / 3 * (qy - ey), ex, ey}});
			cx = qx; cy = qy; x = ex; y = ey;
		}
		else if (C == 'A') {
			double large, sweep;
			if (!r.Number(a[0]) || !r.Number(a[1]) || !r.Number(a[2]) || !r.Flag(large) || !r.Flag(sweep) || !r.Number(a[3]) || !r.Number(a[4])) break;
			double ex = ox + a[3], ey = oy + a[4];
			Arc(out, x, y, a[0], a[1], a[2], large != 0, sweep != 0, ex, ey);
			x = ex; y = ey;
		}
		else break;
		last = C;
	}
	return out;
}

} // namespace slippy

#endif // __SLIPPY_AGENT_LOGO_H__
