// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// The host side of the OFX 1.5 draw suite: DrawContext, the object behind
// OfxDrawContextHandle, and OfxDrawSuiteV1 over it.
//
// DrawContext is the contract a host implements to support the suite, not a
// drawing engine of its own: it validates every call against the
// specification (open only for the duration of one Draw action, null and
// count checks on points, the stipple range) and tracks the colour, line
// width and stipple state a plugin can query back, then hands the
// host-specific work -- putting a colour or a primitive on screen, and
// answering getColour from the host's palette -- to virtuals a real host
// overrides with its own renderer and theme. TestHost's RecordingDrawContext
// (TestHost/src/DrawRecorder.h) is the example: it has no display, so it
// appends each call to a list a test can inspect, print or rasterise instead.
//
// The object behind OfxDrawContextHandle is a DrawContext. It is open only for
// the duration of one Draw action -- the specification has every entry point
// fail outside kOfxInteractActionDraw -- so the host opens it around the
// action and closes it afterwards (see host/ofxInteract.h, which does exactly
// that).

#include <ofxCore.h>
#include <ofxDrawSuite.h>
#include <ofxPixels.h>

namespace openfx::host {

inline bool isStandardColour(int which) {
  return which >= kOfxStandardColourOverlayBackground &&
         which <= kOfxStandardColourOverlayText;
}

// The object behind OfxDrawContextHandle. The base implements the
// specification's own rules -- the refuse-when-closed rule, argument
// validation, and the current colour/line-width/stipple state -- and calls a
// pure virtual for every piece of work that is a host's own choice: how a
// colour or a primitive actually reaches the screen, and what the standard
// palette is.
class DrawContext {
 public:
  DrawContext() = default;
  virtual ~DrawContext() = default;

  DrawContext(const DrawContext&) = delete;
  DrawContext& operator=(const DrawContext&) = delete;

  OfxDrawContextHandle handle() { return reinterpret_cast<OfxDrawContextHandle>(this); }
  static DrawContext* from(OfxDrawContextHandle h) {
    return reinterpret_cast<DrawContext*>(h);
  }

  // Open for the duration of one Draw action: the suite refuses calls made
  // outside one, as the specification requires. Opening resets the state.
  void open() {
    colour_ = {1, 1, 1, 1};
    lineWidth_ = 0;
    stipple_ = kOfxDrawLineStipplePatternSolid;
    open_ = true;
    onOpen();
  }
  void close() { open_ = false; }
  bool isOpen() const { return open_; }

  const OfxRGBAColourF& colour() const { return colour_; }
  float lineWidth() const { return lineWidth_; }
  OfxDrawLineStipplePattern stipple() const { return stipple_; }

  // The host's palette for OfxDrawSuiteV1::getColour.
  virtual OfxRGBAColourF standardColour(OfxStandardColour which) const = 0;

  // --- The suite's entry points, as member functions --------------------

  OfxStatus setColour(const OfxRGBAColourF& c) {
    if (!open_)
      return kOfxStatFailed;
    colour_ = c;
    onSetColour(c);
    return kOfxStatOK;
  }

  OfxStatus setLineWidth(float width) {
    if (!open_)
      return kOfxStatFailed;
    lineWidth_ = width;
    onSetLineWidth(width);
    return kOfxStatOK;
  }

  OfxStatus setLineStipple(OfxDrawLineStipplePattern pattern) {
    if (!open_)
      return kOfxStatFailed;
    if (pattern < kOfxDrawLineStipplePatternSolid ||
        pattern > kOfxDrawLineStipplePatternDotDash)
      return kOfxStatErrValue;
    stipple_ = pattern;
    onSetLineStipple(pattern);
    return kOfxStatOK;
  }

  OfxStatus draw(OfxDrawPrimitive primitive, const OfxPointD* points, int count) {
    if (!open_)
      return kOfxStatFailed;
    if (!points || count < minimumPoints(primitive))
      return kOfxStatErrValue;
    if (primitive == kOfxDrawPrimitiveLines && count % 2 != 0)
      return kOfxStatErrValue;
    onDraw(primitive, points, count);
    return kOfxStatOK;
  }

  OfxStatus drawText(const char* text, const OfxPointD* pos, int alignment) {
    if (!open_)
      return kOfxStatFailed;
    if (!text || !pos)
      return kOfxStatErrValue;
    onDrawText(text, *pos, alignment);
    return kOfxStatOK;
  }

 protected:
  // Called after open() has reset the state; a host that keeps its own
  // per-action record overrides this to clear it.
  virtual void onOpen() {}

  virtual void onSetColour(const OfxRGBAColourF& c) = 0;
  virtual void onSetLineWidth(float width) = 0;
  virtual void onSetLineStipple(OfxDrawLineStipplePattern pattern) = 0;
  virtual void onDraw(OfxDrawPrimitive primitive, const OfxPointD* points, int count) = 0;
  virtual void onDrawText(const char* text, const OfxPointD& pos, int alignment) = 0;

 private:
  static int minimumPoints(OfxDrawPrimitive primitive) {
    switch (primitive) {
      case kOfxDrawPrimitiveRectangle:
      case kOfxDrawPrimitiveEllipse:
        return 2;
      case kOfxDrawPrimitivePolygon:
        return 3;
      default:
        return 2;
    }
  }

  OfxRGBAColourF colour_{1, 1, 1, 1};
  float lineWidth_ = 0;
  OfxDrawLineStipplePattern stipple_ = kOfxDrawLineStipplePatternSolid;
  bool open_ = false;
};

// ---------------------------------------------------------------------------
// OfxDrawSuiteV1 over DrawContext
// ---------------------------------------------------------------------------

namespace detail {

inline OfxStatus drawGetColour(OfxDrawContextHandle context, OfxStandardColour which,
                               OfxRGBAColourF* colour) {
  DrawContext* ctx = DrawContext::from(context);
  if (!ctx || !colour)
    return kOfxStatErrBadHandle;
  if (!isStandardColour(which))
    return kOfxStatErrValue;
  if (!ctx->isOpen())
    return kOfxStatFailed;
  *colour = ctx->standardColour(which);
  return kOfxStatOK;
}

inline OfxStatus drawSetColour(OfxDrawContextHandle context,
                               const OfxRGBAColourF* colour) {
  DrawContext* ctx = DrawContext::from(context);
  if (!ctx || !colour)
    return kOfxStatErrBadHandle;
  return ctx->setColour(*colour);
}

inline OfxStatus drawSetLineWidth(OfxDrawContextHandle context, float width) {
  DrawContext* ctx = DrawContext::from(context);
  return ctx ? ctx->setLineWidth(width) : kOfxStatErrBadHandle;
}

inline OfxStatus drawSetLineStipple(OfxDrawContextHandle context,
                                    OfxDrawLineStipplePattern pattern) {
  DrawContext* ctx = DrawContext::from(context);
  return ctx ? ctx->setLineStipple(pattern) : kOfxStatErrBadHandle;
}

inline OfxStatus drawDraw(OfxDrawContextHandle context, OfxDrawPrimitive primitive,
                          const OfxPointD* points, int count) {
  DrawContext* ctx = DrawContext::from(context);
  return ctx ? ctx->draw(primitive, points, count) : kOfxStatErrBadHandle;
}

inline OfxStatus drawDrawText(OfxDrawContextHandle context, const char* text,
                              const OfxPointD* pos, int alignment) {
  DrawContext* ctx = DrawContext::from(context);
  return ctx ? ctx->drawText(text, pos, alignment) : kOfxStatErrBadHandle;
}

}  // namespace detail

// The draw suite over DrawContext: every call is validated against the
// specification here and handed to the host's own implementation.
inline const OfxDrawSuiteV1* drawSuite() {
  static const OfxDrawSuiteV1 kSuite = {
      detail::drawGetColour,      detail::drawSetColour, detail::drawSetLineWidth,
      detail::drawSetLineStipple, detail::drawDraw,      detail::drawDrawText,
  };
  return &kSuite;
}

}  // namespace openfx::host
