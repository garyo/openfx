// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// The colour management vocabulary of OFX 1.5 (ofxColour.h): the styles, and
// the colourspaces and roles of the native config with the style each belongs
// to. Only the identifiers are written out below; every attribute comes from
// the config header's own macros, so this table cannot disagree with it.

#include <ofxColour.h>

#include <optional>
#include <string_view>

namespace openfx {

// The values of kOfxImageEffectPropColourManagementStyle, in the increasing
// order the specification gives them: OCIO > Full > Core > Basic.
enum class ColourManagementStyle { None, Basic, Core, Full, OCIO };

inline const char* colourManagementStyleName(ColourManagementStyle style) {
  switch (style) {
    case ColourManagementStyle::Basic:
      return kOfxImageEffectColourManagementBasic;
    case ColourManagementStyle::Core:
      return kOfxImageEffectColourManagementCore;
    case ColourManagementStyle::Full:
      return kOfxImageEffectColourManagementFull;
    case ColourManagementStyle::OCIO:
      return kOfxImageEffectColourManagementOCIO;
    case ColourManagementStyle::None:
      break;
  }
  return kOfxImageEffectColourManagementNone;
}

inline std::optional<ColourManagementStyle> colourManagementStyleFromName(
    std::string_view name) {
  for (ColourManagementStyle style :
       {ColourManagementStyle::None, ColourManagementStyle::Basic,
        ColourManagementStyle::Core, ColourManagementStyle::Full,
        ColourManagementStyle::OCIO})
    if (name == colourManagementStyleName(style))
      return style;
  return std::nullopt;
}

// One colourspace or role of the native config. A role has no encoding of its
// own, since which colourspace it stands for is the config's business.
struct ColourspaceInfo {
  const char* name;
  bool isBasic;
  bool isCore;
  const char* encoding;

  // The least capable style this colourspace may be used in.
  constexpr ColourManagementStyle style() const {
    return isBasic  ? ColourManagementStyle::Basic
           : isCore ? ColourManagementStyle::Core
                    : ColourManagementStyle::Full;
  }
};

#define OFX_COLOURSPACE(ident) \
  ColourspaceInfo { ident, ident##IsBasic, ident##IsCore, ident##Encoding }
#define OFX_COLOURSPACE_ROLE(ident) \
  ColourspaceInfo{ident, ident##IsBasic, ident##IsCore, ""}

// Every colourspace and role of kOfxConfigIdentifier, in config header order.
inline constexpr ColourspaceInfo kColourspaces[] = {
    OFX_COLOURSPACE(kOfxColourspaceOfxDisplayHdr),
    OFX_COLOURSPACE(kOfxColourspaceOfxDisplaySdr),
    OFX_COLOURSPACE(kOfxColourspaceOfxRaw),
    OFX_COLOURSPACE(kOfxColourspaceOfxSceneLinear),
    OFX_COLOURSPACE(kOfxColourspaceOfxSceneLog),
    OFX_COLOURSPACE(kOfxColourspaceSrgbDisplay),
    OFX_COLOURSPACE(kOfxColourspaceDisplayp3Display),
    OFX_COLOURSPACE(kOfxColourspaceRec1886Rec709Display),
    OFX_COLOURSPACE(kOfxColourspaceRec1886Rec2020Display),
    OFX_COLOURSPACE(kOfxColourspaceRec2100HlgDisplay),
    OFX_COLOURSPACE(kOfxColourspaceRec2100PqDisplay),
    OFX_COLOURSPACE(kOfxColourspaceSt2084P3d65Display),
    OFX_COLOURSPACE(kOfxColourspaceP3d65Display),
    OFX_COLOURSPACE(kOfxColourspaceACES20651),
    OFX_COLOURSPACE(kOfxColourspaceACEScc),
    OFX_COLOURSPACE(kOfxColourspaceACEScct),
    OFX_COLOURSPACE(kOfxColourspaceACEScg),
    OFX_COLOURSPACE(kOfxColourspaceLinP3d65),
    OFX_COLOURSPACE(kOfxColourspaceLinRec2020),
    OFX_COLOURSPACE(kOfxColourspaceLinRec709Srgb),
    OFX_COLOURSPACE(kOfxColourspaceG18Rec709Tx),
    OFX_COLOURSPACE(kOfxColourspaceG22Ap1Tx),
    OFX_COLOURSPACE(kOfxColourspaceG22Rec709Tx),
    OFX_COLOURSPACE(kOfxColourspaceG24Rec709Tx),
    OFX_COLOURSPACE(kOfxColourspaceSrgbEncodedAp1Tx),
    OFX_COLOURSPACE(kOfxColourspaceSrgbEncodedP3d65Tx),
    OFX_COLOURSPACE(kOfxColourspaceSrgbTx),
    OFX_COLOURSPACE(kOfxColourspaceRaw),
    OFX_COLOURSPACE(kOfxColourspaceCIEXYZD65),
    OFX_COLOURSPACE(kOfxColourspaceP3d60Display),
    OFX_COLOURSPACE(kOfxColourspaceP3DciDisplay),
    OFX_COLOURSPACE(kOfxColourspaceADX10),
    OFX_COLOURSPACE(kOfxColourspaceADX16),
    OFX_COLOURSPACE(kOfxColourspaceLinArriWideGamut3),
    OFX_COLOURSPACE(kOfxColourspaceArriLogc3Ei800),
    OFX_COLOURSPACE(kOfxColourspaceLinArriWideGamut4),
    OFX_COLOURSPACE(kOfxColourspaceArriLogc4),
    OFX_COLOURSPACE(kOfxColourspaceBmdfilmWidegamutGen5),
    OFX_COLOURSPACE(kOfxColourspaceDavinciIntermediateWidegamut),
    OFX_COLOURSPACE(kOfxColourspaceLinBmdWidegamutGen5),
    OFX_COLOURSPACE(kOfxColourspaceLinDavinciWidegamut),
    OFX_COLOURSPACE(kOfxColourspaceCanonlog2CinemagamutD55),
    OFX_COLOURSPACE(kOfxColourspaceCanonlog3CinemagamutD55),
    OFX_COLOURSPACE(kOfxColourspaceLinCinemagamutD55),
    OFX_COLOURSPACE(kOfxColourspaceLinVgamut),
    OFX_COLOURSPACE(kOfxColourspaceVlogVgamut),
    OFX_COLOURSPACE(kOfxColourspaceLinRedwidegamutrgb),
    OFX_COLOURSPACE(kOfxColourspaceLog3g10Redwidegamutrgb),
    OFX_COLOURSPACE(kOfxColourspaceLinSgamut3),
    OFX_COLOURSPACE(kOfxColourspaceLinSgamut3cine),
    OFX_COLOURSPACE(kOfxColourspaceLinVeniceSgamut3),
    OFX_COLOURSPACE(kOfxColourspaceLinVeniceSgamut3cine),
    OFX_COLOURSPACE(kOfxColourspaceSlog3Sgamut3),
    OFX_COLOURSPACE(kOfxColourspaceSlog3Sgamut3cine),
    OFX_COLOURSPACE(kOfxColourspaceSlog3VeniceSgamut3),
    OFX_COLOURSPACE(kOfxColourspaceSlog3VeniceSgamut3cine),
    OFX_COLOURSPACE(kOfxColourspaceCameraRec709),
    OFX_COLOURSPACE_ROLE(kOfxColourspaceRoleAcesInterchange),
    OFX_COLOURSPACE_ROLE(kOfxColourspaceRoleCieXyzD65Interchange),
    OFX_COLOURSPACE_ROLE(kOfxColourspaceRoleColorPicking),
    OFX_COLOURSPACE_ROLE(kOfxColourspaceRoleColorTiming),
    OFX_COLOURSPACE_ROLE(kOfxColourspaceRoleCompositingLog),
    OFX_COLOURSPACE_ROLE(kOfxColourspaceRoleData),
    OFX_COLOURSPACE_ROLE(kOfxColourspaceRoleMattePaint),
    OFX_COLOURSPACE_ROLE(kOfxColourspaceRoleSceneLinear),
    OFX_COLOURSPACE_ROLE(kOfxColourspaceRoleTexturePaint),
};

#undef OFX_COLOURSPACE
#undef OFX_COLOURSPACE_ROLE

inline const ColourspaceInfo* findColourspace(std::string_view name) {
  for (const ColourspaceInfo& info : kColourspaces)
    if (name == info.name)
      return &info;
  return nullptr;
}

// Whether a colourspace identifier may be used under a colour management style.
// An identifier the config does not name belongs to the OCIO style alone, where
// a colourspace is whatever the host's own config calls one.
inline bool colourspaceAllowedIn(std::string_view name, ColourManagementStyle style) {
  const ColourspaceInfo* info = findColourspace(name);
  return info ? info->style() <= style : style == ColourManagementStyle::OCIO;
}

// The basic colourspace that stands for this one: a basic colourspace is any
// colourspace with the same encoding and reference space, so this is what to
// name when only the basic style is in use. Null for a colourspace with no
// encoding, and for anything the config does not name.
inline const char* basicColourspaceFor(std::string_view name) {
  const ColourspaceInfo* info = findColourspace(name);
  if (!info || *info->encoding == '\0')
    return nullptr;
  for (const ColourspaceInfo& basic : kColourspaces)
    if (basic.isBasic && std::string_view(basic.encoding) == info->encoding)
      return basic.name;
  return nullptr;
}

}  // namespace openfx
