#ifndef _ofxColourspaceList_h_
#define _ofxColourspaceList_h_

// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef __cplusplus
extern "C" {
#endif

/** @file ofxColourspaceList.h
Contains the list of supported colourspaces.
This file was auto-generated by scripts/genColour from studio-config-v1.0.0_aces-v1.3_ocio-v2.1.
*/

// Roles
#define kOfxColourspaceRoleAcesInterchange "aces_interchange"
#define kOfxColourspaceRoleAcesInterchangeColourspace "ACES2065-1"
#define kOfxColourspaceRoleAcesInterchangeMapping { "aces_interchange", "ACES2065-1" }
#define kOfxColourspaceRoleCieXyzD65Interchange "cie_xyz_d65_interchange"
#define kOfxColourspaceRoleCieXyzD65InterchangeColourspace "CIE-XYZ-D65"
#define kOfxColourspaceRoleCieXyzD65InterchangeMapping { "cie_xyz_d65_interchange", "CIE-XYZ-D65" }
#define kOfxColourspaceRoleColorPicking "color_picking"
#define kOfxColourspaceRoleColorPickingColourspace "sRGB - Texture"
#define kOfxColourspaceRoleColorPickingMapping { "color_picking", "sRGB - Texture" }
#define kOfxColourspaceRoleColorTiming "color_timing"
#define kOfxColourspaceRoleColorTimingColourspace "ACEScct"
#define kOfxColourspaceRoleColorTimingMapping { "color_timing", "ACEScct" }
#define kOfxColourspaceRoleCompositingLog "compositing_log"
#define kOfxColourspaceRoleCompositingLogColourspace "ACEScct"
#define kOfxColourspaceRoleCompositingLogMapping { "compositing_log", "ACEScct" }
#define kOfxColourspaceRoleData "data"
#define kOfxColourspaceRoleDataColourspace "Raw"
#define kOfxColourspaceRoleDataMapping { "data", "Raw" }
#define kOfxColourspaceRoleMattePaint "matte_paint"
#define kOfxColourspaceRoleMattePaintColourspace "sRGB - Texture"
#define kOfxColourspaceRoleMattePaintMapping { "matte_paint", "sRGB - Texture" }
#define kOfxColourspaceRoleSceneLinear "scene_linear"
#define kOfxColourspaceRoleSceneLinearColourspace "ACEScg"
#define kOfxColourspaceRoleSceneLinearMapping { "scene_linear", "ACEScg" }
#define kOfxColourspaceRoleTexturePaint "texture_paint"
#define kOfxColourspaceRoleTexturePaintColourspace "ACEScct"
#define kOfxColourspaceRoleTexturePaintMapping { "texture_paint", "ACEScct" }

// Display Colourspaces

// sRGB - Display
// Convert CIE XYZ (D65 white) to sRGB (piecewise EOTF)
#define kOfxColourspaceSRGBDisplayAliasSrgbDisplay "srgb_display"
#define kOfxColourspaceSRGBDisplayList { "sRGB - Display", "srgb_display" }
#define kOfxColourspaceSRGBDisplayEncoding "sdr-video"
#define kOfxColourspaceSRGBDisplayIsSceneLinear false
#define kOfxColourspaceSRGBDisplayIsData false

// Rec.1886 Rec.709 - Display
// Convert CIE XYZ (D65 white) to Rec.1886/Rec.709 (HD video)
#define kOfxColourspaceRec1886Rec709DisplayAliasRec1886Rec709Display "rec1886_rec709_display"
#define kOfxColourspaceRec1886Rec709DisplayList { "Rec.1886 Rec.709 - Display", "rec1886_rec709_display" }
#define kOfxColourspaceRec1886Rec709DisplayEncoding "sdr-video"
#define kOfxColourspaceRec1886Rec709DisplayIsSceneLinear false
#define kOfxColourspaceRec1886Rec709DisplayIsData false

// Rec.1886 Rec.2020 - Display
// Convert CIE XYZ (D65 white) to Rec.1886/Rec.2020 (UHD video)
#define kOfxColourspaceRec1886Rec2020DisplayAliasRec1886Rec2020Display "rec1886_rec2020_display"
#define kOfxColourspaceRec1886Rec2020DisplayList { "Rec.1886 Rec.2020 - Display", "rec1886_rec2020_display" }
#define kOfxColourspaceRec1886Rec2020DisplayEncoding "sdr-video"
#define kOfxColourspaceRec1886Rec2020DisplayIsSceneLinear false
#define kOfxColourspaceRec1886Rec2020DisplayIsData false

// Rec.2100-HLG - Display
// Convert CIE XYZ (D65 white) to Rec.2100-HLG, 1000 nit
#define kOfxColourspaceRec2100HLGDisplayAliasRec2100HlgDisplay "rec2100_hlg_display"
#define kOfxColourspaceRec2100HLGDisplayList { "Rec.2100-HLG - Display", "rec2100_hlg_display" }
#define kOfxColourspaceRec2100HLGDisplayEncoding "hdr-video"
#define kOfxColourspaceRec2100HLGDisplayIsSceneLinear false
#define kOfxColourspaceRec2100HLGDisplayIsData false

// Rec.2100-PQ - Display
// Convert CIE XYZ (D65 white) to Rec.2100-PQ
#define kOfxColourspaceRec2100PQDisplayAliasRec2100PqDisplay "rec2100_pq_display"
#define kOfxColourspaceRec2100PQDisplayList { "Rec.2100-PQ - Display", "rec2100_pq_display" }
#define kOfxColourspaceRec2100PQDisplayEncoding "hdr-video"
#define kOfxColourspaceRec2100PQDisplayIsSceneLinear false
#define kOfxColourspaceRec2100PQDisplayIsData false

// ST2084-P3-D65 - Display
// Convert CIE XYZ (D65 white) to ST-2084 (PQ), P3-D65 primaries
#define kOfxColourspaceST2084P3D65DisplayAliasSt2084P3d65Display "st2084_p3d65_display"
#define kOfxColourspaceST2084P3D65DisplayList { "ST2084-P3-D65 - Display", "st2084_p3d65_display" }
#define kOfxColourspaceST2084P3D65DisplayEncoding "hdr-video"
#define kOfxColourspaceST2084P3D65DisplayIsSceneLinear false
#define kOfxColourspaceST2084P3D65DisplayIsData false

// P3-D60 - Display
// Convert CIE XYZ (D65 white) to Gamma 2.6, P3-D60 (Bradford adaptation)
#define kOfxColourspaceP3D60DisplayAliasP3d60Display "p3d60_display"
#define kOfxColourspaceP3D60DisplayList { "P3-D60 - Display", "p3d60_display" }
#define kOfxColourspaceP3D60DisplayEncoding "sdr-video"
#define kOfxColourspaceP3D60DisplayIsSceneLinear false
#define kOfxColourspaceP3D60DisplayIsData false

// P3-D65 - Display
// Convert CIE XYZ (D65 white) to Gamma 2.6, P3-D65
#define kOfxColourspaceP3D65DisplayAliasP3d65Display "p3d65_display"
#define kOfxColourspaceP3D65DisplayList { "P3-D65 - Display", "p3d65_display" }
#define kOfxColourspaceP3D65DisplayEncoding "sdr-video"
#define kOfxColourspaceP3D65DisplayIsSceneLinear false
#define kOfxColourspaceP3D65DisplayIsData false

// P3-DCI - Display
// Convert CIE XYZ (D65 white) to Gamma 2.6, P3-DCI (DCI white with Bradford adaptation)
#define kOfxColourspaceP3DCIDisplayAliasP3DciDisplay "p3_dci_display"
#define kOfxColourspaceP3DCIDisplayList { "P3-DCI - Display", "p3_dci_display" }
#define kOfxColourspaceP3DCIDisplayEncoding "sdr-video"
#define kOfxColourspaceP3DCIDisplayIsSceneLinear false
#define kOfxColourspaceP3DCIDisplayIsData false

// Scene Colourspaces

// ACES2065-1
// The "Academy Color Encoding System" reference colorspace.
#define kOfxColourspaceACES20651AliasAces20651 "aces2065_1"
#define kOfxColourspaceACES20651AliasACESACES20651 "ACES - ACES2065-1"
#define kOfxColourspaceACES20651AliasLinAp0 "lin_ap0"
#define kOfxColourspaceACES20651List { "ACES2065-1", "aces2065_1", "ACES - ACES2065-1", "lin_ap0" }
#define kOfxColourspaceACES20651Encoding "scene-linear"
#define kOfxColourspaceACES20651IsSceneLinear true
#define kOfxColourspaceACES20651IsData false

// ACEScc
// Convert ACEScc to ACES2065-1
// ACEStransformID: urn:ampas:aces:transformId:v1.5:ACEScsc.Academy.ACEScc_to_ACES.a1.0.3
#define kOfxColourspaceACESccAliasACESACEScc "ACES - ACEScc"
#define kOfxColourspaceACESccAliasAcesccAp1 "acescc_ap1"
#define kOfxColourspaceACESccList { "ACEScc", "ACES - ACEScc", "acescc_ap1" }
#define kOfxColourspaceACESccEncoding "log"
#define kOfxColourspaceACESccIsSceneLinear false
#define kOfxColourspaceACESccIsData false

// ACEScct
// Convert ACEScct to ACES2065-1
// ACEStransformID: urn:ampas:aces:transformId:v1.5:ACEScsc.Academy.ACEScct_to_ACES.a1.0.3
#define kOfxColourspaceACEScctAliasACESACEScct "ACES - ACEScct"
#define kOfxColourspaceACEScctAliasAcescctAp1 "acescct_ap1"
#define kOfxColourspaceACEScctList { "ACEScct", "ACES - ACEScct", "acescct_ap1" }
#define kOfxColourspaceACEScctEncoding "log"
#define kOfxColourspaceACEScctIsSceneLinear false
#define kOfxColourspaceACEScctIsData false

// ACEScg
// Convert ACEScg to ACES2065-1
// ACEStransformID: urn:ampas:aces:transformId:v1.5:ACEScsc.Academy.ACEScg_to_ACES.a1.0.3
#define kOfxColourspaceACEScgAliasACESACEScg "ACES - ACEScg"
#define kOfxColourspaceACEScgAliasLinAp1 "lin_ap1"
#define kOfxColourspaceACEScgList { "ACEScg", "ACES - ACEScg", "lin_ap1" }
#define kOfxColourspaceACEScgEncoding "scene-linear"
#define kOfxColourspaceACEScgIsSceneLinear true
#define kOfxColourspaceACEScgIsData false

// ADX10
// Convert ADX10 to ACES2065-1
// ACEStransformID: urn:ampas:aces:transformId:v1.5:ACEScsc.Academy.ADX10_to_ACES.a1.0.3
#define kOfxColourspaceADX10AliasInputADXADX10 "Input - ADX - ADX10"
#define kOfxColourspaceADX10List { "ADX10", "Input - ADX - ADX10" }
#define kOfxColourspaceADX10Encoding "log"
#define kOfxColourspaceADX10IsSceneLinear false
#define kOfxColourspaceADX10IsData false

// ADX16
// Convert ADX16 to ACES2065-1
// ACEStransformID: urn:ampas:aces:transformId:v1.5:ACEScsc.Academy.ADX16_to_ACES.a1.0.3
#define kOfxColourspaceADX16AliasInputADXADX16 "Input - ADX - ADX16"
#define kOfxColourspaceADX16List { "ADX16", "Input - ADX - ADX16" }
#define kOfxColourspaceADX16Encoding "log"
#define kOfxColourspaceADX16IsSceneLinear false
#define kOfxColourspaceADX16IsData false

// Linear ARRI Wide Gamut 3
// Convert Linear ARRI Wide Gamut 3 to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:ARRI:Input:Linear_ARRI_Wide_Gamut_3_to_ACES2065-1:1.0
#define kOfxColourspaceLinearARRIWideGamut3AliasLinArriWideGamut3 "lin_arri_wide_gamut_3"
#define kOfxColourspaceLinearARRIWideGamut3AliasInputARRILinearALEXAWideGamut "Input - ARRI - Linear - ALEXA Wide Gamut"
#define kOfxColourspaceLinearARRIWideGamut3AliasLinAlexawide "lin_alexawide"
#define kOfxColourspaceLinearARRIWideGamut3List { "Linear ARRI Wide Gamut 3", "lin_arri_wide_gamut_3", "Input - ARRI - Linear - ALEXA Wide Gamut", "lin_alexawide" }
#define kOfxColourspaceLinearARRIWideGamut3Encoding "scene-linear"
#define kOfxColourspaceLinearARRIWideGamut3IsSceneLinear true
#define kOfxColourspaceLinearARRIWideGamut3IsData false

// ARRI LogC3 (EI800)
// Convert ARRI LogC3 (EI800) to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:ARRI:Input:ARRI_LogC3_EI800_to_ACES2065-1:1.0
#define kOfxColourspaceARRILogC3EI800AliasArriLogc3Ei800 "arri_logc3_ei800"
#define kOfxColourspaceARRILogC3EI800AliasInputARRIV3LogCEI800WideGamut "Input - ARRI - V3 LogC (EI800) - Wide Gamut"
#define kOfxColourspaceARRILogC3EI800AliasLogc3ei800Alexawide "logc3ei800_alexawide"
#define kOfxColourspaceARRILogC3EI800List { "ARRI LogC3 (EI800)", "arri_logc3_ei800", "Input - ARRI - V3 LogC (EI800) - Wide Gamut", "logc3ei800_alexawide" }
#define kOfxColourspaceARRILogC3EI800Encoding "log"
#define kOfxColourspaceARRILogC3EI800IsSceneLinear false
#define kOfxColourspaceARRILogC3EI800IsData false

// Linear ARRI Wide Gamut 4
// Convert Linear ARRI Wide Gamut 4 to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:ARRI:Input:Linear_ARRI_Wide_Gamut_4_to_ACES2065-1:1.0
#define kOfxColourspaceLinearARRIWideGamut4AliasLinArriWideGamut4 "lin_arri_wide_gamut_4"
#define kOfxColourspaceLinearARRIWideGamut4AliasLinAwg4 "lin_awg4"
#define kOfxColourspaceLinearARRIWideGamut4List { "Linear ARRI Wide Gamut 4", "lin_arri_wide_gamut_4", "lin_awg4" }
#define kOfxColourspaceLinearARRIWideGamut4Encoding "scene-linear"
#define kOfxColourspaceLinearARRIWideGamut4IsSceneLinear true
#define kOfxColourspaceLinearARRIWideGamut4IsData false

// ARRI LogC4
// Convert ARRI LogC4 to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:ARRI:Input:ARRI_LogC4_to_ACES2065-1:1.0
#define kOfxColourspaceARRILogC4AliasArriLogc4 "arri_logc4"
#define kOfxColourspaceARRILogC4List { "ARRI LogC4", "arri_logc4" }
#define kOfxColourspaceARRILogC4Encoding "log"
#define kOfxColourspaceARRILogC4IsSceneLinear false
#define kOfxColourspaceARRILogC4IsData false

// BMDFilm WideGamut Gen5
// Convert Blackmagic Film Wide Gamut (Gen 5) to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:BlackmagicDesign:Input:BMDFilm_WideGamut_Gen5_to_ACES2065-1:1.0
#define kOfxColourspaceBMDFilmWideGamutGen5AliasBmdfilmWidegamutGen5 "bmdfilm_widegamut_gen5"
#define kOfxColourspaceBMDFilmWideGamutGen5List { "BMDFilm WideGamut Gen5", "bmdfilm_widegamut_gen5" }
#define kOfxColourspaceBMDFilmWideGamutGen5Encoding "log"
#define kOfxColourspaceBMDFilmWideGamutGen5IsSceneLinear false
#define kOfxColourspaceBMDFilmWideGamutGen5IsData false

// DaVinci Intermediate WideGamut
// Convert DaVinci Intermediate Wide Gamut to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:BlackmagicDesign:Input:DaVinci_Intermediate_WideGamut_to_ACES2065-1:1.0
#define kOfxColourspaceDaVinciIntermediateWideGamutAliasDavinciIntermediateWidegamut "davinci_intermediate_widegamut"
#define kOfxColourspaceDaVinciIntermediateWideGamutList { "DaVinci Intermediate WideGamut", "davinci_intermediate_widegamut" }
#define kOfxColourspaceDaVinciIntermediateWideGamutEncoding "log"
#define kOfxColourspaceDaVinciIntermediateWideGamutIsSceneLinear false
#define kOfxColourspaceDaVinciIntermediateWideGamutIsData false

// Linear BMD WideGamut Gen5
// Convert Linear Blackmagic Wide Gamut (Gen 5) to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:BlackmagicDesign:Input:Linear_BMD_WideGamut_Gen5_to_ACES2065-1:1.0
#define kOfxColourspaceLinearBMDWideGamutGen5AliasLinBmdWidegamutGen5 "lin_bmd_widegamut_gen5"
#define kOfxColourspaceLinearBMDWideGamutGen5List { "Linear BMD WideGamut Gen5", "lin_bmd_widegamut_gen5" }
#define kOfxColourspaceLinearBMDWideGamutGen5Encoding "scene-linear"
#define kOfxColourspaceLinearBMDWideGamutGen5IsSceneLinear true
#define kOfxColourspaceLinearBMDWideGamutGen5IsData false

// Linear DaVinci WideGamut
// Convert Linear DaVinci Wide Gamut to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:BlackmagicDesign:Input:Linear_DaVinci_WideGamut_to_ACES2065-1:1.0
#define kOfxColourspaceLinearDaVinciWideGamutAliasLinDavinciWidegamut "lin_davinci_widegamut"
#define kOfxColourspaceLinearDaVinciWideGamutList { "Linear DaVinci WideGamut", "lin_davinci_widegamut" }
#define kOfxColourspaceLinearDaVinciWideGamutEncoding "scene-linear"
#define kOfxColourspaceLinearDaVinciWideGamutIsSceneLinear true
#define kOfxColourspaceLinearDaVinciWideGamutIsData false

// CanonLog3 CinemaGamut D55
// Convert Canon Log 3 Cinema Gamut to ACES2065-1
#define kOfxColourspaceCanonLog3CinemaGamutD55AliasCanonlog3CinemagamutD55 "canonlog3_cinemagamut_d55"
#define kOfxColourspaceCanonLog3CinemaGamutD55AliasInputCanonCanonLog3CinemaGamutDaylight "Input - Canon - Canon-Log3 - Cinema Gamut Daylight"
#define kOfxColourspaceCanonLog3CinemaGamutD55AliasCanonlog3Cgamutday "canonlog3_cgamutday"
#define kOfxColourspaceCanonLog3CinemaGamutD55List { "CanonLog3 CinemaGamut D55", "canonlog3_cinemagamut_d55", "Input - Canon - Canon-Log3 - Cinema Gamut Daylight", "canonlog3_cgamutday" }
#define kOfxColourspaceCanonLog3CinemaGamutD55Encoding "log"
#define kOfxColourspaceCanonLog3CinemaGamutD55IsSceneLinear false
#define kOfxColourspaceCanonLog3CinemaGamutD55IsData false

// Linear CinemaGamut D55
// Convert Linear Canon Cinema Gamut (Daylight) to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:Canon:Input:Linear-CinemaGamut-D55_to_ACES2065-1:1.0
#define kOfxColourspaceLinearCinemaGamutD55AliasLinCinemagamutD55 "lin_cinemagamut_d55"
#define kOfxColourspaceLinearCinemaGamutD55AliasInputCanonLinearCanonCinemaGamutDaylight "Input - Canon - Linear - Canon Cinema Gamut Daylight"
#define kOfxColourspaceLinearCinemaGamutD55AliasLinCanoncgamutday "lin_canoncgamutday"
#define kOfxColourspaceLinearCinemaGamutD55List { "Linear CinemaGamut D55", "lin_cinemagamut_d55", "Input - Canon - Linear - Canon Cinema Gamut Daylight", "lin_canoncgamutday" }
#define kOfxColourspaceLinearCinemaGamutD55Encoding "scene-linear"
#define kOfxColourspaceLinearCinemaGamutD55IsSceneLinear true
#define kOfxColourspaceLinearCinemaGamutD55IsData false

// Linear V-Gamut
// Convert Linear Panasonic V-Gamut to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:Panasonic:Input:Linear_VGamut_to_ACES2065-1:1.0
#define kOfxColourspaceLinearVGamutAliasLinVgamut "lin_vgamut"
#define kOfxColourspaceLinearVGamutAliasInputPanasonicLinearVGamut "Input - Panasonic - Linear - V-Gamut"
#define kOfxColourspaceLinearVGamutList { "Linear V-Gamut", "lin_vgamut", "Input - Panasonic - Linear - V-Gamut" }
#define kOfxColourspaceLinearVGamutEncoding "scene-linear"
#define kOfxColourspaceLinearVGamutIsSceneLinear true
#define kOfxColourspaceLinearVGamutIsData false

// V-Log V-Gamut
// Convert Panasonic V-Log - V-Gamut to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:Panasonic:Input:VLog_VGamut_to_ACES2065-1:1.0
#define kOfxColourspaceVLogVGamutAliasVlogVgamut "vlog_vgamut"
#define kOfxColourspaceVLogVGamutAliasInputPanasonicVLogVGamut "Input - Panasonic - V-Log - V-Gamut"
#define kOfxColourspaceVLogVGamutList { "V-Log V-Gamut", "vlog_vgamut", "Input - Panasonic - V-Log - V-Gamut" }
#define kOfxColourspaceVLogVGamutEncoding "log"
#define kOfxColourspaceVLogVGamutIsSceneLinear false
#define kOfxColourspaceVLogVGamutIsData false

// Linear REDWideGamutRGB
// Convert Linear REDWideGamutRGB to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:RED:Input:Linear_REDWideGamutRGB_to_ACES2065-1:1.0
#define kOfxColourspaceLinearREDWideGamutRGBAliasLinRedwidegamutrgb "lin_redwidegamutrgb"
#define kOfxColourspaceLinearREDWideGamutRGBAliasInputREDLinearREDWideGamutRGB "Input - RED - Linear - REDWideGamutRGB"
#define kOfxColourspaceLinearREDWideGamutRGBAliasLinRwg "lin_rwg"
#define kOfxColourspaceLinearREDWideGamutRGBList { "Linear REDWideGamutRGB", "lin_redwidegamutrgb", "Input - RED - Linear - REDWideGamutRGB", "lin_rwg" }
#define kOfxColourspaceLinearREDWideGamutRGBEncoding "scene-linear"
#define kOfxColourspaceLinearREDWideGamutRGBIsSceneLinear true
#define kOfxColourspaceLinearREDWideGamutRGBIsData false

// Log3G10 REDWideGamutRGB
// Convert RED Log3G10 REDWideGamutRGB to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:RED:Input:Log3G10_REDWideGamutRGB_to_ACES2065-1:1.0
#define kOfxColourspaceLog3G10REDWideGamutRGBAliasLog3g10Redwidegamutrgb "log3g10_redwidegamutrgb"
#define kOfxColourspaceLog3G10REDWideGamutRGBAliasInputREDREDLog3G10REDWideGamutRGB "Input - RED - REDLog3G10 - REDWideGamutRGB"
#define kOfxColourspaceLog3G10REDWideGamutRGBAliasRl3g10Rwg "rl3g10_rwg"
#define kOfxColourspaceLog3G10REDWideGamutRGBList { "Log3G10 REDWideGamutRGB", "log3g10_redwidegamutrgb", "Input - RED - REDLog3G10 - REDWideGamutRGB", "rl3g10_rwg" }
#define kOfxColourspaceLog3G10REDWideGamutRGBEncoding "log"
#define kOfxColourspaceLog3G10REDWideGamutRGBIsSceneLinear false
#define kOfxColourspaceLog3G10REDWideGamutRGBIsData false

// Linear S-Gamut3
// Convert Linear S-Gamut3 to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:Sony:Input:Linear_SGamut3_to_ACES2065-1:1.0
#define kOfxColourspaceLinearSGamut3AliasLinSgamut3 "lin_sgamut3"
#define kOfxColourspaceLinearSGamut3AliasInputSonyLinearSGamut3 "Input - Sony - Linear - S-Gamut3"
#define kOfxColourspaceLinearSGamut3List { "Linear S-Gamut3", "lin_sgamut3", "Input - Sony - Linear - S-Gamut3" }
#define kOfxColourspaceLinearSGamut3Encoding "scene-linear"
#define kOfxColourspaceLinearSGamut3IsSceneLinear true
#define kOfxColourspaceLinearSGamut3IsData false

// Linear S-Gamut3.Cine
// Convert Linear S-Gamut3.Cine to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:Sony:Input:Linear_SGamut3Cine_to_ACES2065-1:1.0
#define kOfxColourspaceLinearSGamut3CineAliasLinSgamut3cine "lin_sgamut3cine"
#define kOfxColourspaceLinearSGamut3CineAliasInputSonyLinearSGamut3Cine "Input - Sony - Linear - S-Gamut3.Cine"
#define kOfxColourspaceLinearSGamut3CineList { "Linear S-Gamut3.Cine", "lin_sgamut3cine", "Input - Sony - Linear - S-Gamut3.Cine" }
#define kOfxColourspaceLinearSGamut3CineEncoding "scene-linear"
#define kOfxColourspaceLinearSGamut3CineIsSceneLinear true
#define kOfxColourspaceLinearSGamut3CineIsData false

// Linear Venice S-Gamut3
// Convert Linear Venice S-Gamut3 to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:Sony:Input:Linear_Venice_SGamut3_to_ACES2065-1:1.0
#define kOfxColourspaceLinearVeniceSGamut3AliasLinVeniceSgamut3 "lin_venice_sgamut3"
#define kOfxColourspaceLinearVeniceSGamut3AliasInputSonyLinearVeniceSGamut3 "Input - Sony - Linear - Venice S-Gamut3"
#define kOfxColourspaceLinearVeniceSGamut3List { "Linear Venice S-Gamut3", "lin_venice_sgamut3", "Input - Sony - Linear - Venice S-Gamut3" }
#define kOfxColourspaceLinearVeniceSGamut3Encoding "scene-linear"
#define kOfxColourspaceLinearVeniceSGamut3IsSceneLinear true
#define kOfxColourspaceLinearVeniceSGamut3IsData false

// Linear Venice S-Gamut3.Cine
// Convert Linear Venice S-Gamut3.Cine to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:Sony:Input:Linear_Venice_SGamut3Cine_to_ACES2065-1:1.0
#define kOfxColourspaceLinearVeniceSGamut3CineAliasLinVeniceSgamut3cine "lin_venice_sgamut3cine"
#define kOfxColourspaceLinearVeniceSGamut3CineAliasInputSonyLinearVeniceSGamut3Cine "Input - Sony - Linear - Venice S-Gamut3.Cine"
#define kOfxColourspaceLinearVeniceSGamut3CineList { "Linear Venice S-Gamut3.Cine", "lin_venice_sgamut3cine", "Input - Sony - Linear - Venice S-Gamut3.Cine" }
#define kOfxColourspaceLinearVeniceSGamut3CineEncoding "scene-linear"
#define kOfxColourspaceLinearVeniceSGamut3CineIsSceneLinear true
#define kOfxColourspaceLinearVeniceSGamut3CineIsData false

// S-Log3 S-Gamut3
// Convert Sony S-Log3 S-Gamut3 to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:Sony:Input:SLog3_SGamut3_to_ACES2065-1:1.0
#define kOfxColourspaceSLog3SGamut3AliasSlog3Sgamut3 "slog3_sgamut3"
#define kOfxColourspaceSLog3SGamut3AliasInputSonySLog3SGamut3 "Input - Sony - S-Log3 - S-Gamut3"
#define kOfxColourspaceSLog3SGamut3List { "S-Log3 S-Gamut3", "slog3_sgamut3", "Input - Sony - S-Log3 - S-Gamut3" }
#define kOfxColourspaceSLog3SGamut3Encoding "log"
#define kOfxColourspaceSLog3SGamut3IsSceneLinear false
#define kOfxColourspaceSLog3SGamut3IsData false

// S-Log3 S-Gamut3.Cine
// Convert Sony S-Log3 S-Gamut3.Cine to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:Sony:Input:SLog3_SGamut3Cine_to_ACES2065-1:1.0
#define kOfxColourspaceSLog3SGamut3CineAliasSlog3Sgamut3cine "slog3_sgamut3cine"
#define kOfxColourspaceSLog3SGamut3CineAliasInputSonySLog3SGamut3Cine "Input - Sony - S-Log3 - S-Gamut3.Cine"
#define kOfxColourspaceSLog3SGamut3CineAliasSlog3Sgamutcine "slog3_sgamutcine"
#define kOfxColourspaceSLog3SGamut3CineList { "S-Log3 S-Gamut3.Cine", "slog3_sgamut3cine", "Input - Sony - S-Log3 - S-Gamut3.Cine", "slog3_sgamutcine" }
#define kOfxColourspaceSLog3SGamut3CineEncoding "log"
#define kOfxColourspaceSLog3SGamut3CineIsSceneLinear false
#define kOfxColourspaceSLog3SGamut3CineIsData false

// S-Log3 Venice S-Gamut3
// Convert Sony S-Log3 Venice S-Gamut3 to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:Sony:Input:SLog3_Venice_SGamut3_to_ACES2065-1:1.0
#define kOfxColourspaceSLog3VeniceSGamut3AliasSlog3VeniceSgamut3 "slog3_venice_sgamut3"
#define kOfxColourspaceSLog3VeniceSGamut3AliasInputSonySLog3VeniceSGamut3 "Input - Sony - S-Log3 - Venice S-Gamut3"
#define kOfxColourspaceSLog3VeniceSGamut3List { "S-Log3 Venice S-Gamut3", "slog3_venice_sgamut3", "Input - Sony - S-Log3 - Venice S-Gamut3" }
#define kOfxColourspaceSLog3VeniceSGamut3Encoding "log"
#define kOfxColourspaceSLog3VeniceSGamut3IsSceneLinear false
#define kOfxColourspaceSLog3VeniceSGamut3IsData false

// S-Log3 Venice S-Gamut3.Cine
// Convert Sony S-Log3 Venice S-Gamut3.Cine to ACES2065-1
// CLFtransformID: urn:aswf:ocio:transformId:1.0:Sony:Input:SLog3_Venice_SGamut3Cine_to_ACES2065-1:1.0
#define kOfxColourspaceSLog3VeniceSGamut3CineAliasSlog3VeniceSgamut3cine "slog3_venice_sgamut3cine"
#define kOfxColourspaceSLog3VeniceSGamut3CineAliasInputSonySLog3VeniceSGamut3Cine "Input - Sony - S-Log3 - Venice S-Gamut3.Cine"
#define kOfxColourspaceSLog3VeniceSGamut3CineAliasSlog3VeniceSgamutcine "slog3_venice_sgamutcine"
#define kOfxColourspaceSLog3VeniceSGamut3CineList { "S-Log3 Venice S-Gamut3.Cine", "slog3_venice_sgamut3cine", "Input - Sony - S-Log3 - Venice S-Gamut3.Cine", "slog3_venice_sgamutcine" }
#define kOfxColourspaceSLog3VeniceSGamut3CineEncoding "log"
#define kOfxColourspaceSLog3VeniceSGamut3CineIsSceneLinear false
#define kOfxColourspaceSLog3VeniceSGamut3CineIsData false

// Camera Rec.709
// Convert ACES2065-1 to Rec.709 camera OETF Rec.709 primaries, D65 white point
// CLFtransformID: urn:aswf:ocio:transformId:1.0:ITU:Utility:AP0_to_Camera_Rec709:1.0
#define kOfxColourspaceCameraRec709AliasCameraRec709 "camera_rec709"
#define kOfxColourspaceCameraRec709AliasUtilityRec709Camera "Utility - Rec.709 - Camera"
#define kOfxColourspaceCameraRec709AliasRec709Camera "rec709_camera"
#define kOfxColourspaceCameraRec709List { "Camera Rec.709", "camera_rec709", "Utility - Rec.709 - Camera", "rec709_camera" }
#define kOfxColourspaceCameraRec709Encoding "sdr-video"
#define kOfxColourspaceCameraRec709IsSceneLinear false
#define kOfxColourspaceCameraRec709IsData false

// Linear P3-D65
// Convert ACES2065-1 to linear P3 primaries, D65 white point
// CLFtransformID: urn:aswf:ocio:transformId:1.0:OCIO:Utility:AP0_to_Linear_P3-D65:1.0
#define kOfxColourspaceLinearP3D65AliasLinP3d65 "lin_p3d65"
#define kOfxColourspaceLinearP3D65AliasUtilityLinearP3D65 "Utility - Linear - P3-D65"
#define kOfxColourspaceLinearP3D65List { "Linear P3-D65", "lin_p3d65", "Utility - Linear - P3-D65" }
#define kOfxColourspaceLinearP3D65Encoding "scene-linear"
#define kOfxColourspaceLinearP3D65IsSceneLinear true
#define kOfxColourspaceLinearP3D65IsData false

// Linear Rec.2020
// Convert ACES2065-1 to linear Rec.2020 primaries, D65 white point
// CLFtransformID: urn:aswf:ocio:transformId:1.0:OCIO:Utility:AP0_to_Linear_Rec2020:1.0
#define kOfxColourspaceLinearRec2020AliasLinRec2020 "lin_rec2020"
#define kOfxColourspaceLinearRec2020AliasUtilityLinearRec2020 "Utility - Linear - Rec.2020"
#define kOfxColourspaceLinearRec2020List { "Linear Rec.2020", "lin_rec2020", "Utility - Linear - Rec.2020" }
#define kOfxColourspaceLinearRec2020Encoding "scene-linear"
#define kOfxColourspaceLinearRec2020IsSceneLinear true
#define kOfxColourspaceLinearRec2020IsData false

// Linear Rec.709 (sRGB)
// Convert ACES2065-1 to linear Rec.709 primaries, D65 white point
// CLFtransformID: urn:aswf:ocio:transformId:1.0:OCIO:Utility:AP0_to_Linear_Rec709:1.0
#define kOfxColourspaceLinearRec709sRGBAliasLinRec709Srgb "lin_rec709_srgb"
#define kOfxColourspaceLinearRec709sRGBAliasUtilityLinearRec709 "Utility - Linear - Rec.709"
#define kOfxColourspaceLinearRec709sRGBAliasLinRec709 "lin_rec709"
#define kOfxColourspaceLinearRec709sRGBAliasLinSrgb "lin_srgb"
#define kOfxColourspaceLinearRec709sRGBAliasUtilityLinearSRGB "Utility - Linear - sRGB"
#define kOfxColourspaceLinearRec709sRGBList { "Linear Rec.709 (sRGB)", "lin_rec709_srgb", "Utility - Linear - Rec.709", "lin_rec709", "lin_srgb", "Utility - Linear - sRGB" }
#define kOfxColourspaceLinearRec709sRGBEncoding "scene-linear"
#define kOfxColourspaceLinearRec709sRGBIsSceneLinear true
#define kOfxColourspaceLinearRec709sRGBIsData false

// Gamma 1.8 Rec.709 - Texture
// Convert ACES2065-1 to 1.8 gamma-corrected Rec.709 primaries, D65 white point
// CLFtransformID: urn:aswf:ocio:transformId:1.0:OCIO:Utility:AP0_to_Gamma1.8_Rec709-Texture:1.0
#define kOfxColourspaceGamma18Rec709TextureAliasG18Rec709Tx "g18_rec709_tx"
#define kOfxColourspaceGamma18Rec709TextureAliasUtilityGamma18Rec709Texture "Utility - Gamma 1.8 - Rec.709 - Texture"
#define kOfxColourspaceGamma18Rec709TextureAliasG18Rec709 "g18_rec709"
#define kOfxColourspaceGamma18Rec709TextureList { "Gamma 1.8 Rec.709 - Texture", "g18_rec709_tx", "Utility - Gamma 1.8 - Rec.709 - Texture", "g18_rec709" }
#define kOfxColourspaceGamma18Rec709TextureEncoding "sdr-video"
#define kOfxColourspaceGamma18Rec709TextureIsSceneLinear false
#define kOfxColourspaceGamma18Rec709TextureIsData false

// Gamma 2.2 AP1 - Texture
// Convert ACES2065-1 to 2.2 gamma-corrected AP1 primaries, D60 white point
// CLFtransformID: urn:aswf:ocio:transformId:1.0:OCIO:Utility:AP0_to_Gamma2.2_AP1-Texture:1.0
#define kOfxColourspaceGamma22AP1TextureAliasG22Ap1Tx "g22_ap1_tx"
#define kOfxColourspaceGamma22AP1TextureAliasG22Ap1 "g22_ap1"
#define kOfxColourspaceGamma22AP1TextureList { "Gamma 2.2 AP1 - Texture", "g22_ap1_tx", "g22_ap1" }
#define kOfxColourspaceGamma22AP1TextureEncoding "sdr-video"
#define kOfxColourspaceGamma22AP1TextureIsSceneLinear false
#define kOfxColourspaceGamma22AP1TextureIsData false

// Gamma 2.2 Rec.709 - Texture
// Convert ACES2065-1 to 2.2 gamma-corrected Rec.709 primaries, D65 white point
// CLFtransformID: urn:aswf:ocio:transformId:1.0:OCIO:Utility:AP0_to_Gamma2.2_Rec709-Texture:1.0
#define kOfxColourspaceGamma22Rec709TextureAliasG22Rec709Tx "g22_rec709_tx"
#define kOfxColourspaceGamma22Rec709TextureAliasUtilityGamma22Rec709Texture "Utility - Gamma 2.2 - Rec.709 - Texture"
#define kOfxColourspaceGamma22Rec709TextureAliasG22Rec709 "g22_rec709"
#define kOfxColourspaceGamma22Rec709TextureList { "Gamma 2.2 Rec.709 - Texture", "g22_rec709_tx", "Utility - Gamma 2.2 - Rec.709 - Texture", "g22_rec709" }
#define kOfxColourspaceGamma22Rec709TextureEncoding "sdr-video"
#define kOfxColourspaceGamma22Rec709TextureIsSceneLinear false
#define kOfxColourspaceGamma22Rec709TextureIsData false

// Gamma 2.4 Rec.709 - Texture
// Convert ACES2065-1 to 2.4 gamma-corrected Rec.709 primaries, D65 white point
// CLFtransformID: urn:aswf:ocio:transformId:1.0:OCIO:Utility:AP0_to_Gamma2.4_Rec709-Texture:1.0
#define kOfxColourspaceGamma24Rec709TextureAliasG24Rec709Tx "g24_rec709_tx"
#define kOfxColourspaceGamma24Rec709TextureAliasG24Rec709 "g24_rec709"
#define kOfxColourspaceGamma24Rec709TextureAliasRec709Display "rec709_display"
#define kOfxColourspaceGamma24Rec709TextureAliasUtilityRec709Display "Utility - Rec.709 - Display"
#define kOfxColourspaceGamma24Rec709TextureList { "Gamma 2.4 Rec.709 - Texture", "g24_rec709_tx", "g24_rec709", "rec709_display", "Utility - Rec.709 - Display" }
#define kOfxColourspaceGamma24Rec709TextureEncoding "sdr-video"
#define kOfxColourspaceGamma24Rec709TextureIsSceneLinear false
#define kOfxColourspaceGamma24Rec709TextureIsData false

// sRGB Encoded AP1 - Texture
// Convert ACES2065-1 to sRGB Encoded AP1 primaries, D60 white point
// CLFtransformID: urn:aswf:ocio:transformId:1.0:OCIO:Utility:AP0_to_sRGB_Encoded_AP1-Texture:1.0
#define kOfxColourspaceSRGBEncodedAP1TextureAliasSrgbEncodedAp1Tx "srgb_encoded_ap1_tx"
#define kOfxColourspaceSRGBEncodedAP1TextureAliasSrgbAp1 "srgb_ap1"
#define kOfxColourspaceSRGBEncodedAP1TextureList { "sRGB Encoded AP1 - Texture", "srgb_encoded_ap1_tx", "srgb_ap1" }
#define kOfxColourspaceSRGBEncodedAP1TextureEncoding "sdr-video"
#define kOfxColourspaceSRGBEncodedAP1TextureIsSceneLinear false
#define kOfxColourspaceSRGBEncodedAP1TextureIsData false

// sRGB - Texture
// Convert ACES2065-1 to sRGB
// CLFtransformID: urn:aswf:ocio:transformId:1.0:OCIO:Utility:AP0_to_sRGB-Texture:1.0
#define kOfxColourspaceSRGBTextureAliasSrgbTx "srgb_tx"
#define kOfxColourspaceSRGBTextureAliasUtilitySRGBTexture "Utility - sRGB - Texture"
#define kOfxColourspaceSRGBTextureAliasSrgbTexture "srgb_texture"
#define kOfxColourspaceSRGBTextureAliasInputGenericSRGBTexture "Input - Generic - sRGB - Texture"
#define kOfxColourspaceSRGBTextureList { "sRGB - Texture", "srgb_tx", "Utility - sRGB - Texture", "srgb_texture", "Input - Generic - sRGB - Texture" }
#define kOfxColourspaceSRGBTextureEncoding ""
#define kOfxColourspaceSRGBTextureIsSceneLinear false
#define kOfxColourspaceSRGBTextureIsData false

// Raw
// The utility "Raw" colorspace.
#define kOfxColourspaceRawAliasUtilityRaw "Utility - Raw"
#define kOfxColourspaceRawList { "Raw", "Utility - Raw" }
#define kOfxColourspaceRawEncoding ""
#define kOfxColourspaceRawIsSceneLinear false
#define kOfxColourspaceRawIsData true

#ifdef __cplusplus
}
#endif

#endif

