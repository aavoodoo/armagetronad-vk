/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

***************************************************************************

*/

#ifndef ArmageTron_RENDER_ENUMS_H
#define ArmageTron_RENDER_ENUMS_H

// Capability enums for Enable/Disable state
enum class rCapability : int
{
    AlphaTest,
    Blend,
    CullFace,
    DepthTest,
    Lighting,
    Light0,
    Light1,
    Texture2D,
    LineSmooth,
    ClipPlane0,
    ClipPlane1,
    ClipPlane2,
    ClipPlane3,
    ClipPlane4,
    ClipPlane5,
    PolygonOffsetFill,
    PolygonOffsetLine,
    PolygonOffsetPoint
};

// Blend factor enums
enum class rBlendFactor : int
{
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstColor,
    SrcColor,
    OneMinusSrcColor,
    OneMinusDstColor,
    DstAlpha,
    OneMinusDstAlpha
};

// Depth/Alpha comparison functions
enum class rCompareFunc : int
{
    Less,
    LEqual,
    Greater,
    GEqual,
    Equal,
    NotEqual,
    Always,
    Never
};

// Face winding
enum class rFrontFace : int
{
    CW,
    CCW
};

// Material face
enum class rMaterialFace : int
{
    Front,
    Back,
    FrontAndBack
};

// Material property
enum class rMaterialProperty : int
{
    Ambient,
    Diffuse,
    Specular,
    Emission,
    Shininess,
    AmbientAndDiffuse
};

// Texture filter modes
enum class rTexFilter : int
{
    Nearest,
    Linear,
    NearestMipmapNearest,
    LinearMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapLinear
};

// Pixel formats for rSurface
enum class rPixelFormat : int
{
    R8,
    RGB8,
    RGBA8,
    BGR8,
    BGRA8,
    L8,
    LA8
};

// Texture wrap modes
enum class rTexWrap : int
{
    Repeat,
    ClampToEdge,
    MirroredRepeat
};

// Polygon modes
enum class rPolygonMode : int
{
    Point,
    Line,
    Fill
};

// Shade models
enum class rShadeModel : int
{
    Flat,
    Smooth
};

// Light parameters
enum class rLightParam : int
{
    Ambient,
    Diffuse,
    Specular,
    Position,
    SpotDirection,
    SpotExponent,
    SpotCutoff,
    ConstantAttenuation,
    LinearAttenuation,
    QuadraticAttenuation
};

// Hint targets
enum class rHintTarget : int {
    LineSmoothHint,
    PolygonSmoothHint,
    FogHint,
    GenerateMipmapHint
};

// Hint modes
enum class rHintMode : int {
    Fastest,
    Nicest,
    DontCare
};

// ============================================================================
// Backend-neutral GL constant mirrors.
// These match OpenGL values so they can be passed directly to GL calls,
// but don't require including any GL headers.
// ============================================================================

namespace rGLConst {
// Texture targets
constexpr int Texture2D           = 0x0DE1;
constexpr int ProxyTexture2D      = 0x8064;

// Texture parameters
constexpr int TextureMinFilter    = 0x2801;
constexpr int TextureMagFilter    = 0x2800;
constexpr int TextureWrapS        = 0x2802;
constexpr int TextureWrapT        = 0x2803;
constexpr int TextureSwizzleR     = 0x8E42;
constexpr int TextureSwizzleG     = 0x8E43;
constexpr int TextureSwizzleB     = 0x8E44;
constexpr int TextureSwizzleA     = 0x8E45;
constexpr int TextureWidth        = 0x1000;
constexpr int UnpackAlignment     = 0x0CF5;

// Filter modes
constexpr int Nearest             = 0x2600;
constexpr int Linear              = 0x2601;
constexpr int NearestMipmapNearest = 0x2700;
constexpr int LinearMipmapNearest = 0x2701;
constexpr int NearestMipmapLinear = 0x2702;
constexpr int LinearMipmapLinear  = 0x2703;

// Wrap modes
constexpr int Repeat              = 0x2901;
constexpr int ClampToEdge         = 0x812F;
constexpr int MirroredRepeat      = 0x8370;

// Pixel formats
constexpr int Red                 = 0x1903;
constexpr int Green               = 0x1904;
constexpr int Blue                = 0x1905;
constexpr int RGB                 = 0x1907;
constexpr int RGBA                = 0x1908;
constexpr int BGR                 = 0x80E0;
constexpr int BGRA                = 0x80E1;
constexpr int Luminance           = 0x1909;
constexpr int LuminanceAlpha      = 0x190A;
constexpr int Luminance8Alpha8    = 0x8045;
constexpr int R8                  = 0x8229;
constexpr int RGB8                = 0x8051;
constexpr int RGBA8               = 0x8058;
constexpr int RGBA4               = 0x8056;
constexpr int RGB5                = 0x8050;
constexpr int DepthComponent32    = 0x81A7;

// Texture unit
constexpr int Texture0            = 0x84C0;

// Data types
constexpr int UnsignedByte        = 0x1401;
constexpr int Float               = 0x1406;

// Capabilities (Enable/Disable)
constexpr int DepthTest           = 0x0B71;
constexpr int Blend               = 0x0BE2;
constexpr int CullFace            = 0x0B44;
constexpr int Lighting            = 0x0B50;
constexpr int AlphaTest           = 0x0BC0;
constexpr int LineSmooth          = 0x0B20;
constexpr int ScissorTest         = 0x0C11;
constexpr int PolygonOffsetFill   = 0x8037;
constexpr int PolygonOffsetLine   = 0x2A02;
constexpr int PolygonOffsetPoint  = 0x2A01;
constexpr int ClipPlane0          = 0x3000;

// Blend factors
constexpr int Zero                = 0;
constexpr int One                 = 1;
constexpr int SrcAlpha            = 0x0302;
constexpr int OneMinusSrcAlpha    = 0x0303;
constexpr int DstAlpha            = 0x0304;
constexpr int OneMinusDstAlpha    = 0x0305;
constexpr int DstColor            = 0x0306;
constexpr int OneMinusDstColor    = 0x0307;
constexpr int SrcColor            = 0x0300;
constexpr int OneMinusSrcColor    = 0x0301;

// Compare functions
constexpr int Never               = 0x0200;
constexpr int Less                = 0x0201;
constexpr int Equal               = 0x0202;
constexpr int LEqual              = 0x0203;
constexpr int Greater             = 0x0204;
constexpr int NotEqual            = 0x0205;
constexpr int GEqual              = 0x0206;
constexpr int Always              = 0x0207;

// Draw modes
constexpr int Points              = 0x0000;
constexpr int Lines               = 0x0001;
constexpr int Triangles           = 0x0004;

// Client state arrays
constexpr int VertexArray         = 0x8074;
constexpr int ColorArray          = 0x8076;

// Framebuffer
constexpr int ColorAttachment0    = 0x8CE0;
constexpr int DepthAttachment     = 0x8D00;
constexpr int Front               = 0x0404;
constexpr int Back                = 0x0405;

// String queries
constexpr int Vendor              = 0x1F00;
constexpr int Renderer            = 0x1F01;
constexpr int Version             = 0x1F02;
constexpr int Extensions          = 0x1F03;

// Swizzle source
constexpr int SwizzleOne          = 1;

// Light parameters
constexpr int Light0              = 0x4000;
constexpr int Ambient             = 0x1200;
constexpr int Diffuse             = 0x1201;
constexpr int Specular            = 0x1202;
constexpr int Position            = 0x1203;
constexpr int SpotDirection       = 0x1204;
constexpr int SpotExponent        = 0x1205;
constexpr int SpotCutoff          = 0x1206;
constexpr int ConstantAttenuation = 0x1207;
constexpr int LinearAttenuation   = 0x1208;
constexpr int QuadraticAttenuation = 0x1209;

// Material
constexpr int FrontAndBack        = 0x0408;
constexpr int Emission            = 0x1600;
constexpr int Shininess           = 0x1601;
constexpr int AmbientAndDiffuse   = 0x1602;

// Hint
constexpr int DontCare            = 0x1100;
constexpr int Fastest             = 0x1101;
constexpr int Nicest              = 0x1102;
constexpr int LineSmoothHint      = 0x0C52;
constexpr int PolygonSmoothHint   = 0x0C53;
constexpr int FogHint             = 0x0C54;
constexpr int GenerateMipmapHint  = 0x8192;

// Face winding
constexpr int CW                  = 0x0900;
constexpr int CCW                 = 0x0901;
} // namespace rGLConst

// ============================================================================
// Constexpr inline enum-to-int conversions using rGLConst values.
// These allow renderer wrappers to work without GL headers.
// ============================================================================

constexpr inline int rCapabilityToInt(rCapability cap) {
    switch (cap) {
        case rCapability::AlphaTest: return rGLConst::AlphaTest;
        case rCapability::Blend: return rGLConst::Blend;
        case rCapability::CullFace: return rGLConst::CullFace;
        case rCapability::DepthTest: return rGLConst::DepthTest;
        case rCapability::Lighting: return rGLConst::Lighting;
        case rCapability::Light0: return rGLConst::Light0;
        case rCapability::Light1: return rGLConst::Light0 + 1;
        case rCapability::Texture2D: return rGLConst::Texture2D;
        case rCapability::LineSmooth: return rGLConst::LineSmooth;
        case rCapability::ClipPlane0: return rGLConst::ClipPlane0;
        case rCapability::ClipPlane1: return rGLConst::ClipPlane0 + 1;
        case rCapability::ClipPlane2: return rGLConst::ClipPlane0 + 2;
        case rCapability::ClipPlane3: return rGLConst::ClipPlane0 + 3;
        case rCapability::ClipPlane4: return rGLConst::ClipPlane0 + 4;
        case rCapability::ClipPlane5: return rGLConst::ClipPlane0 + 5;
        case rCapability::PolygonOffsetFill: return rGLConst::PolygonOffsetFill;
        case rCapability::PolygonOffsetLine: return rGLConst::PolygonOffsetLine;
        case rCapability::PolygonOffsetPoint: return rGLConst::PolygonOffsetPoint;
        default: return 0;
    }
}

constexpr inline int rBlendFactorToInt(rBlendFactor factor) {
    switch (factor) {
        case rBlendFactor::Zero: return rGLConst::Zero;
        case rBlendFactor::One: return rGLConst::One;
        case rBlendFactor::SrcAlpha: return rGLConst::SrcAlpha;
        case rBlendFactor::OneMinusSrcAlpha: return rGLConst::OneMinusSrcAlpha;
        case rBlendFactor::DstColor: return rGLConst::DstColor;
        case rBlendFactor::SrcColor: return rGLConst::SrcColor;
        case rBlendFactor::OneMinusSrcColor: return rGLConst::OneMinusSrcColor;
        case rBlendFactor::OneMinusDstColor: return rGLConst::OneMinusDstColor;
        case rBlendFactor::DstAlpha: return rGLConst::DstAlpha;
        case rBlendFactor::OneMinusDstAlpha: return rGLConst::OneMinusDstAlpha;
        default: return rGLConst::One;
    }
}

constexpr inline int rCompareFuncToInt(rCompareFunc func) {
    switch (func) {
        case rCompareFunc::Less: return rGLConst::Less;
        case rCompareFunc::LEqual: return rGLConst::LEqual;
        case rCompareFunc::Greater: return rGLConst::Greater;
        case rCompareFunc::GEqual: return rGLConst::GEqual;
        case rCompareFunc::Equal: return rGLConst::Equal;
        case rCompareFunc::NotEqual: return rGLConst::NotEqual;
        case rCompareFunc::Always: return rGLConst::Always;
        case rCompareFunc::Never: return rGLConst::Never;
        default: return rGLConst::Less;
    }
}

constexpr inline int rFrontFaceToInt(rFrontFace face) {
    switch (face) {
        case rFrontFace::CW: return rGLConst::CW;
        case rFrontFace::CCW: return rGLConst::CCW;
        default: return rGLConst::CCW;
    }
}

constexpr inline int rMaterialFaceToInt(rMaterialFace face) {
    switch (face) {
        case rMaterialFace::Front: return rGLConst::Front;
        case rMaterialFace::Back: return rGLConst::Back;
        case rMaterialFace::FrontAndBack: return rGLConst::FrontAndBack;
        default: return rGLConst::FrontAndBack;
    }
}

constexpr inline int rMaterialPropertyToInt(rMaterialProperty prop) {
    switch (prop) {
        case rMaterialProperty::Ambient: return rGLConst::Ambient;
        case rMaterialProperty::Diffuse: return rGLConst::Diffuse;
        case rMaterialProperty::Specular: return rGLConst::Specular;
        case rMaterialProperty::Emission: return rGLConst::Emission;
        case rMaterialProperty::Shininess: return rGLConst::Shininess;
        case rMaterialProperty::AmbientAndDiffuse: return rGLConst::AmbientAndDiffuse;
        default: return rGLConst::Diffuse;
    }
}

constexpr inline int rLightParamToInt(rLightParam param) {
    switch (param) {
        case rLightParam::Ambient: return rGLConst::Ambient;
        case rLightParam::Diffuse: return rGLConst::Diffuse;
        case rLightParam::Specular: return rGLConst::Specular;
        case rLightParam::Position: return rGLConst::Position;
        case rLightParam::SpotDirection: return rGLConst::SpotDirection;
        case rLightParam::SpotExponent: return rGLConst::SpotExponent;
        case rLightParam::SpotCutoff: return rGLConst::SpotCutoff;
        case rLightParam::ConstantAttenuation: return rGLConst::ConstantAttenuation;
        case rLightParam::LinearAttenuation: return rGLConst::LinearAttenuation;
        case rLightParam::QuadraticAttenuation: return rGLConst::QuadraticAttenuation;
        default: return rGLConst::Diffuse;
    }
}

constexpr inline int rHintTargetToInt(rHintTarget target) {
    switch (target) {
        case rHintTarget::LineSmoothHint: return rGLConst::LineSmoothHint;
        case rHintTarget::PolygonSmoothHint: return rGLConst::PolygonSmoothHint;
        case rHintTarget::FogHint: return rGLConst::FogHint;
        case rHintTarget::GenerateMipmapHint: return rGLConst::GenerateMipmapHint;
        default: return rGLConst::LineSmoothHint;
    }
}

constexpr inline int rHintModeToInt(rHintMode mode) {
    switch (mode) {
        case rHintMode::Fastest: return rGLConst::Fastest;
        case rHintMode::Nicest: return rGLConst::Nicest;
        case rHintMode::DontCare: return rGLConst::DontCare;
        default: return rGLConst::Fastest;
    }
}

constexpr inline int rPixelFormatToInt(rPixelFormat format) {
    switch (format) {
        case rPixelFormat::R8: return rGLConst::Red;
        case rPixelFormat::RGB8: return rGLConst::RGB;
        case rPixelFormat::RGBA8: return rGLConst::RGBA;
        case rPixelFormat::BGR8: return rGLConst::BGR;
        case rPixelFormat::BGRA8: return rGLConst::BGRA;
        case rPixelFormat::L8: return rGLConst::Luminance;
        case rPixelFormat::LA8: return rGLConst::LuminanceAlpha;
        default: return rGLConst::RGBA;
    }
}

#endif // ArmageTron_RENDER_ENUMS_H
