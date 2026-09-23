// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Plugin-side wrapper over OfxDrawSuiteV1 (OFX 1.5): the drawing context an
// overlay is handed in the Draw action's in-args, with one member per suite
// entry point and a convenience per primitive.
//
//   OfxStatus draw(Interact& interact, ActionArgs& in) override {
//     Draw d(in, suites());  // the overlay's suites
//     d.setColour(d.getColour(kOfxStandardColourOverlayActive));
//     std::array<OfxPointD, 2> corners{{{x - r, y - r}, {x + r, y + r}}};
//     d.drawRectangle(corners[0], corners[1]);
//   }
//
// Every call is only valid inside kOfxInteractActionDraw; outside one the
// host answers kOfxStatFailed, which arrives here as an OfxException.

#include <ofxCore.h>
#include <ofxDrawSuite.h>
#include <ofxInteract.h>
#include <ofxPixels.h>

#include <string>
#include <string_view>

#include "openfx/ofxExceptions.h"
#include "openfx/ofxSpan.h"
#include "openfx/ofxSuites.h"
#include "openfx/plugin/ofxEffect.h"
#include "openfx/plugin/ofxPropSetAccessors.h"

namespace openfx::plugin {

namespace detail {

inline const OfxDrawSuiteV1* requireDrawSuite(const OfxDrawSuiteV1* suite) {
  if (!suite)
    throw SuiteNotFoundException(kOfxStatErrMissingHostFeature, kOfxDrawSuite);
  return suite;
}

}  // namespace detail

// The host's drawing context for one Draw action.
class Draw {
 public:
  Draw(OfxDrawContextHandle context, const SuiteContainer& suites)
      : Draw(context, suites.get<OfxDrawSuiteV1>()) {}
  Draw(OfxDrawContextHandle context, const OfxDrawSuiteV1* suite)
      : suite_(detail::requireDrawSuite(suite)), context_(context) {}

  // The context out of the Draw action's in-args, which is where an overlay
  // gets it.
  Draw(ActionArgs& args, const SuiteContainer& suites)
      : Draw(static_cast<OfxDrawContextHandle>(
                 args.as<propsets::InteractActionDraw_InArgs>().drawContext()),
             suites) {}

  OfxDrawContextHandle handle() const { return context_; }

  // One of the host's own overlay colours, so a plugin can match its theme.
  OfxRGBAColourF getColour(OfxStandardColour which) const {
    OfxRGBAColourF colour{1, 1, 1, 1};
    check(suite_->getColour(context_, which, &colour), "getColour");
    return colour;
  }

  // The colour future lines, filled shapes and text are drawn in.
  void setColour(const OfxRGBAColourF& colour) {
    check(suite_->setColour(context_, &colour), "setColour");
  }

  // Width 0 is a single-pixel line; anything else is a smooth line of that
  // width, which the host scales for screen density.
  void setLineWidth(float width) {
    check(suite_->setLineWidth(context_, width), "setLineWidth");
  }

  void setLineStipple(OfxDrawLineStipplePattern pattern) {
    check(suite_->setLineStipple(context_, pattern), "setLineStipple");
  }

  // The general form; the conveniences below name each primitive and say how
  // many points it takes.
  void draw(OfxDrawPrimitive primitive, const OfxPointD* points, int count) {
    check(suite_->draw(context_, primitive, points, count), "draw");
  }
  void draw(OfxDrawPrimitive primitive, span<const OfxPointD> points) {
    draw(primitive, points.data(), static_cast<int>(points.size()));
  }

  // n points draw n/2 separated line segments.
  void drawLines(span<const OfxPointD> points) { draw(kOfxDrawPrimitiveLines, points); }
  void drawLine(OfxPointD from, OfxPointD to) {
    const OfxPointD points[2] = {from, to};
    draw(kOfxDrawPrimitiveLines, points, 2);
  }
  // n points draw n-1 connected segments.
  void drawLineStrip(span<const OfxPointD> points) {
    draw(kOfxDrawPrimitiveLineStrip, points);
  }
  // n points draw n connected segments, the last closing the loop.
  void drawLineLoop(span<const OfxPointD> points) {
    draw(kOfxDrawPrimitiveLineLoop, points);
  }
  // A filled axis-aligned rectangle between two opposite corners.
  void drawRectangle(OfxPointD corner1, OfxPointD corner2) {
    const OfxPointD points[2] = {corner1, corner2};
    draw(kOfxDrawPrimitiveRectangle, points, 2);
  }
  // A filled n-sided polygon.
  void drawPolygon(span<const OfxPointD> points) {
    draw(kOfxDrawPrimitivePolygon, points);
  }
  // An unfilled axis-aligned ellipse inscribed in the rectangle two opposite
  // corners give.
  void drawEllipse(OfxPointD corner1, OfxPointD corner2) {
    const OfxPointD points[2] = {corner1, corner2};
    draw(kOfxDrawPrimitiveEllipse, points, 2);
  }

  // The font face and size are the host's; alignment is a combination of the
  // kOfxDrawTextAlignment* flags.
  void drawText(std::string_view text, OfxPointD position,
                int alignment = kOfxDrawTextAlignmentLeft |
                                kOfxDrawTextAlignmentBaseline) {
    const std::string utf8(text);
    check(suite_->drawText(context_, utf8.c_str(), &position, alignment), "drawText");
  }

 private:
  static void check(OfxStatus status, const char* what) {
    if (status != kOfxStatOK)
      throw OfxException(status, what);
  }

  const OfxDrawSuiteV1* suite_;
  OfxDrawContextHandle context_;
};

}  // namespace openfx::plugin
