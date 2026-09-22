// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// This host has no display, so its DrawContext (openfx/host/ofxDrawSuiteHost.h)
// records instead of drawing: every call a plugin makes while drawing an
// overlay is appended to a list of DrawCommands that the host can inspect,
// print or rasterise; nothing is drawn by the suite itself, because a real
// host's display is its own business.

#include <ofxCore.h>
#include <ofxDrawSuite.h>
#include <ofxPixels.h>
#include <openfx/host/ofxDrawSuiteHost.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace testhost {

// One recorded call on a draw context. A state call (colour, line width,
// stipple) is recorded in its own right, and every geometry call also carries
// the state in force when it was made, so a command stands on its own.
struct DrawCommand {
  enum class Kind { Colour, LineWidth, LineStipple, Primitive, Text };

  Kind kind = Kind::Primitive;
  OfxRGBAColourF colour{1, 1, 1, 1};
  float lineWidth = 0;
  OfxDrawLineStipplePattern stipple = kOfxDrawLineStipplePatternSolid;
  OfxDrawPrimitive primitive = kOfxDrawPrimitiveLines;
  std::vector<OfxPointD> points;
  std::string text;
  int alignment = 0;

  // One line, for a host's log: the call, its points and its colour.
  std::string summary() const;
};

inline const char* drawPrimitiveName(OfxDrawPrimitive p) {
  switch (p) {
    case kOfxDrawPrimitiveLines:
      return "lines";
    case kOfxDrawPrimitiveLineStrip:
      return "line-strip";
    case kOfxDrawPrimitiveLineLoop:
      return "line-loop";
    case kOfxDrawPrimitiveRectangle:
      return "rectangle";
    case kOfxDrawPrimitivePolygon:
      return "polygon";
    case kOfxDrawPrimitiveEllipse:
      return "ellipse";
  }
  return "?";
}

inline const char* drawStippleName(OfxDrawLineStipplePattern p) {
  switch (p) {
    case kOfxDrawLineStipplePatternSolid:
      return "solid";
    case kOfxDrawLineStipplePatternDot:
      return "dot";
    case kOfxDrawLineStipplePatternDash:
      return "dash";
    case kOfxDrawLineStipplePatternAltDash:
      return "alt-dash";
    case kOfxDrawLineStipplePatternDotDash:
      return "dot-dash";
  }
  return "?";
}

inline std::string DrawCommand::summary() const {
  std::ostringstream os;
  auto colourText = [&] {
    os << " [" << colour.r << "," << colour.g << "," << colour.b << "," << colour.a
       << "]";
  };
  auto pointList = [&] {
    for (size_t i = 0; i < points.size(); ++i)
      os << " (" << points[i].x << "," << points[i].y << ")";
  };
  switch (kind) {
    case Kind::Colour:
      os << "colour";
      colourText();
      break;
    case Kind::LineWidth:
      os << "line-width " << lineWidth;
      break;
    case Kind::LineStipple:
      os << "line-stipple " << drawStippleName(stipple);
      break;
    case Kind::Primitive:
      os << drawPrimitiveName(primitive) << " x" << points.size();
      pointList();
      colourText();
      break;
    case Kind::Text:
      os << "text \"" << text << "\"";
      pointList();
      colourText();
      break;
  }
  return os.str();
}

// The test host's palette for OfxDrawSuiteV1::getColour: the neutral values
// this host has no theme to replace them with.
inline const OfxRGBAColourF& neutralStandardColour(OfxStandardColour which) {
  static const OfxRGBAColourF kColours[] = {
      {0.0f, 0.0f, 0.0f, 0.0f},  // OverlayBackground
      {1.0f, 1.0f, 0.0f, 1.0f},  // OverlayActive
      {1.0f, 0.6f, 0.0f, 1.0f},  // OverlaySelected
      {0.6f, 0.6f, 0.6f, 1.0f},  // OverlayDeselected
      {1.0f, 1.0f, 1.0f, 1.0f},  // OverlayMarqueeFG
      {0.0f, 0.0f, 0.0f, 0.5f},  // OverlayMarqueeBG
      {1.0f, 1.0f, 1.0f, 1.0f},  // OverlayText
  };
  return kColours[which];
}

// What a plugin drew, in the order it drew it: the concrete DrawContext this
// host implements OfxDrawSuiteV1's virtuals with. The handle a plugin holds
// is a pointer to one of these, as every other OFX handle in this framework
// is.
class RecordingDrawContext : public openfx::host::DrawContext {
 public:
  const std::vector<DrawCommand>& commands() const { return commands_; }

  OfxRGBAColourF standardColour(OfxStandardColour which) const override {
    return neutralStandardColour(which);
  }

 protected:
  void onOpen() override { commands_.clear(); }

  void onSetColour(const OfxRGBAColourF&) override { record(DrawCommand::Kind::Colour); }

  void onSetLineWidth(float) override { record(DrawCommand::Kind::LineWidth); }

  void onSetLineStipple(OfxDrawLineStipplePattern) override {
    record(DrawCommand::Kind::LineStipple);
  }

  void onDraw(OfxDrawPrimitive primitive, const OfxPointD* points, int count) override {
    DrawCommand& c = record(DrawCommand::Kind::Primitive);
    c.primitive = primitive;
    c.points.assign(points, points + count);
  }

  void onDrawText(const char* text, const OfxPointD& pos, int alignment) override {
    DrawCommand& c = record(DrawCommand::Kind::Text);
    c.text = text;
    c.points.assign(1, pos);
    c.alignment = alignment;
  }

 private:
  DrawCommand& record(DrawCommand::Kind kind) {
    DrawCommand c;
    c.kind = kind;
    c.colour = colour();
    c.lineWidth = lineWidth();
    c.stipple = stipple();
    commands_.push_back(std::move(c));
    return commands_.back();
  }

  std::vector<DrawCommand> commands_;
};

// ---------------------------------------------------------------------------
// Rasterising what was recorded
// ---------------------------------------------------------------------------

// Canonical coordinates to viewport pixels: the projection the host set up for
// the interact. pixelScale is kOfxInteractPropPixelScale -- canonical units
// per screen pixel -- and origin is the canonical point at viewport (0, 0).
struct ViewportMapping {
  OfxPointD origin{0, 0};
  OfxPointD pixelScale{1, 1};
  OfxPointI size{0, 0};
};

// Called once per viewport pixel a primitive covers.
using PlotFn = std::function<void(int x, int y, const OfxRGBAColourF& colour)>;

namespace detail {

inline OfxPointI toViewport(const ViewportMapping& m, const OfxPointD& p) {
  double sx = m.pixelScale.x != 0 ? m.pixelScale.x : 1;
  double sy = m.pixelScale.y != 0 ? m.pixelScale.y : 1;
  return {static_cast<int>(std::lround((p.x - m.origin.x) / sx)),
          static_cast<int>(std::lround((p.y - m.origin.y) / sy))};
}

inline void plotClipped(const ViewportMapping& m, const PlotFn& plot, int x, int y,
                        const OfxRGBAColourF& c) {
  if (x >= 0 && y >= 0 && x < m.size.x && y < m.size.y)
    plot(x, y, c);
}

// Whether the nth pixel along a line is drawn, for each stipple pattern.
inline bool stippleOn(OfxDrawLineStipplePattern stipple, int step) {
  switch (stipple) {
    case kOfxDrawLineStipplePatternSolid:
      return true;
    case kOfxDrawLineStipplePatternDot:
      return step % 3 == 0;
    case kOfxDrawLineStipplePatternDash:
      return step % 6 < 3;
    case kOfxDrawLineStipplePatternAltDash:
      return (step + 3) % 6 < 3;
    case kOfxDrawLineStipplePatternDotDash:
      return step % 8 == 0 || (step % 8 >= 2 && step % 8 <= 5);
  }
  return true;
}

// Bresenham, with the stipple pattern thinning the run of pixels.
inline void line(const ViewportMapping& m, const PlotFn& plot, OfxPointI a, OfxPointI b,
                 const OfxRGBAColourF& c, OfxDrawLineStipplePattern stipple) {
  const int dx = std::abs(b.x - a.x), sx = a.x < b.x ? 1 : -1;
  const int dy = -std::abs(b.y - a.y), sy = a.y < b.y ? 1 : -1;
  int err = dx + dy;
  int step = 0;
  for (;;) {
    if (stippleOn(stipple, step))
      plotClipped(m, plot, a.x, a.y, c);
    if (a.x == b.x && a.y == b.y)
      break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      a.x += sx;
    }
    if (e2 <= dx) {
      err += dx;
      a.y += sy;
    }
    ++step;
  }
}

// Scanline fill of a closed polygon given in viewport pixels.
inline void fillPolygon(const ViewportMapping& m, const PlotFn& plot,
                        const std::vector<OfxPointI>& p, const OfxRGBAColourF& c) {
  if (p.size() < 3)
    return;
  int yMin = p[0].y, yMax = p[0].y;
  for (const auto& v : p) {
    yMin = std::min(yMin, v.y);
    yMax = std::max(yMax, v.y);
  }
  yMin = std::max(yMin, 0);
  yMax = std::min(yMax, m.size.y - 1);
  std::vector<double> crossings;
  for (int y = yMin; y <= yMax; ++y) {
    crossings.clear();
    const double scan = y + 0.5;
    for (size_t i = 0, n = p.size(); i < n; ++i) {
      const OfxPointI& a = p[i];
      const OfxPointI& b = p[(i + 1) % n];
      if ((a.y <= scan) == (b.y <= scan) || a.y == b.y)
        continue;
      crossings.push_back(a.x + (scan - a.y) * double(b.x - a.x) / double(b.y - a.y));
    }
    std::sort(crossings.begin(), crossings.end());
    for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
      const int x1 = static_cast<int>(std::lround(crossings[i]));
      const int x2 = static_cast<int>(std::lround(crossings[i + 1]));
      for (int x = x1; x <= x2; ++x) plotClipped(m, plot, x, y, c);
    }
  }
}

}  // namespace detail

// Turns the recorded geometry into pixels: plot() is called once per viewport
// pixel a primitive covers, with the colour in force for that primitive. Line
// width is ignored (every line is one pixel wide) and text draws nothing,
// since the host owns the font; both are still in the command list.
inline void rasterise(const RecordingDrawContext& context, const ViewportMapping& mapping,
                      const PlotFn& plot) {
  using detail::fillPolygon;
  using detail::line;
  using detail::toViewport;
  for (const DrawCommand& c : context.commands()) {
    if (c.kind != DrawCommand::Kind::Primitive)
      continue;
    std::vector<OfxPointI> p;
    p.reserve(c.points.size());
    for (const OfxPointD& v : c.points) p.push_back(toViewport(mapping, v));
    switch (c.primitive) {
      case kOfxDrawPrimitiveLines:
        for (size_t i = 0; i + 1 < p.size(); i += 2)
          line(mapping, plot, p[i], p[i + 1], c.colour, c.stipple);
        break;
      case kOfxDrawPrimitiveLineStrip:
        for (size_t i = 0; i + 1 < p.size(); ++i)
          line(mapping, plot, p[i], p[i + 1], c.colour, c.stipple);
        break;
      case kOfxDrawPrimitiveLineLoop:
        for (size_t i = 0; i < p.size(); ++i)
          line(mapping, plot, p[i], p[(i + 1) % p.size()], c.colour, c.stipple);
        break;
      case kOfxDrawPrimitiveRectangle: {
        const int x1 = std::min(p[0].x, p[1].x), x2 = std::max(p[0].x, p[1].x);
        const int y1 = std::min(p[0].y, p[1].y), y2 = std::max(p[0].y, p[1].y);
        for (int y = std::max(y1, 0); y <= std::min(y2, mapping.size.y - 1); ++y)
          for (int x = std::max(x1, 0); x <= std::min(x2, mapping.size.x - 1); ++x)
            plot(x, y, c.colour);
        break;
      }
      case kOfxDrawPrimitivePolygon:
        fillPolygon(mapping, plot, p, c.colour);
        break;
      case kOfxDrawPrimitiveEllipse: {
        // An axis-aligned outline inscribed in the rectangle the two points
        // give, sampled finely enough that the steps join up.
        const double cx = (p[0].x + p[1].x) / 2.0, cy = (p[0].y + p[1].y) / 2.0;
        const double rx = std::abs(p[1].x - p[0].x) / 2.0;
        const double ry = std::abs(p[1].y - p[0].y) / 2.0;
        const int steps = std::max(16, int(4 * (rx + ry)));
        OfxPointI prev{};
        for (int i = 0; i <= steps; ++i) {
          const double a = 2 * 3.14159265358979323846 * i / steps;
          OfxPointI now{static_cast<int>(std::lround(cx + rx * std::cos(a))),
                        static_cast<int>(std::lround(cy + ry * std::sin(a)))};
          if (i)
            line(mapping, plot, prev, now, c.colour, c.stipple);
          prev = now;
        }
        break;
      }
    }
  }
}

}  // namespace testhost
