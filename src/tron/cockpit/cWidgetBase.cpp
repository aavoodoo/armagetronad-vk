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

#include "cockpit/cWidgetBase.h"
#include "cockpit/cCockpit.h"
#include "tValueParser.h"
#include "tResourceManager.h"
#include "rScreen.h"   // sr_screenWidth, sr_screenHeight (aspect-locked size math)

#ifndef DEDICATED

#include <typeinfo>

namespace cWidget {

void Base::SetCam(int Cam) {
    m_Cam = Cam;
}

//! Prints an error message to the console.
//!
//! This should be called by derived classes if parsing a setting fails.
//! @param cur the node that's being attempted to parse
void Base::DisplayError(tXmlParser::node cur) {
    if(!m_ParsingTemplate) {
        tERR_WARN("Element of type '" + cur.GetName() + "' not processable in this context: '" + typeid(*this).name() + "'");
    }
}

//! This needs to be overwritten if the derived class has anyting to parse or can be derived from.
//!
//! It should try to parse the given node, return true on success or return the result of the Process() function of the widget it's derived from if it fails.
//! @param cur the node that's being attempted to parse
//! @returns true on success, false on failure
bool Base::Process(tXmlParser::node cur) {
    return false;
}

WithCoordinates::WithCoordinates() : m_originalPosition(0,0), m_originalSize(1,1), m_position(0,0), m_size(1,1)
{}

bool WithCoordinates::Process(tXmlParser::node cur) {
    if(cur.IsOfType("Position")) {
        if (ParseAnchorPosition(cur)) {
            m_useAnchorPos = true;
        } else {
            tCoord shift;
            cur.GetProp("x", shift.x);
            cur.GetProp("y", shift.y);
            m_position += shift;
            m_originalPosition = m_position;
        }
        return true;
    }
    if(cur.IsOfType("Size")) {
        if (ParseAnchorSize(cur)) {
            m_useAnchorSize = true;
        } else {
            tCoord factor;
            cur.GetProp("width", factor.x);
            cur.GetProp("height", factor.y);
            m_size *= factor;
            m_originalSize = m_size;
        }
        return true;
    }
    return Base::Process(cur);
}

// Parse anchor-style Position attributes. Returns true iff at least one
// new-style attribute was present (which opts this widget into the new
// layout pipeline).
bool WithCoordinates::ParseAnchorPosition(tXmlParser::node const & cur) {
    const bool anyNew =
        cur.HasProp("anchorH")     || cur.HasProp("anchorV")     ||
        cur.HasProp("offsetX")     || cur.HasProp("offsetY")     ||
        cur.HasProp("stretchMinX") || cur.HasProp("stretchMaxX") ||
        cur.HasProp("stretchMinY") || cur.HasProp("stretchMaxY");
    if (!anyNew) return false;

    auto parseAnchor = [](tString const & s) -> AnchorSpec::Anchor {
        if (s == "left"   || s == "top")    return AnchorSpec::Anchor::Min;
        if (s == "center")                  return AnchorSpec::Anchor::Center;
        if (s == "right"  || s == "bottom") return AnchorSpec::Anchor::Max;
        if (s == "stretch")                 return AnchorSpec::Anchor::Stretch;
        return AnchorSpec::Anchor::Center;
    };

    if (cur.HasProp("anchorH"))     m_anchor.anchorH = parseAnchor(cur.GetProp("anchorH"));
    if (cur.HasProp("anchorV"))     m_anchor.anchorV = parseAnchor(cur.GetProp("anchorV"));
    if (cur.HasProp("offsetX"))     cur.GetProp("offsetX",     m_anchor.offsetX);
    if (cur.HasProp("offsetY"))     cur.GetProp("offsetY",     m_anchor.offsetY);
    if (cur.HasProp("stretchMinX")) cur.GetProp("stretchMinX", m_anchor.stretchMinX);
    if (cur.HasProp("stretchMaxX")) cur.GetProp("stretchMaxX", m_anchor.stretchMaxX);
    if (cur.HasProp("stretchMinY")) cur.GetProp("stretchMinY", m_anchor.stretchMinY);
    if (cur.HasProp("stretchMaxY")) cur.GetProp("stretchMaxY", m_anchor.stretchMaxY);
    return true;
}

// Parse anchor-style Size attributes. Returns true iff at least one
// new-style attribute was present.
bool WithCoordinates::ParseAnchorSize(tXmlParser::node const & cur) {
    const bool anyNew =
        cur.HasProp("mode") || cur.HasProp("longest") || cur.HasProp("aspect");
    if (!anyNew) return false;

    if (cur.HasProp("mode")) {
        tString m = cur.GetProp("mode");
        if      (m == "proportional")  m_anchor.sizeMode = AnchorSpec::SizeMode::Proportional;
        else if (m == "aspect-locked") m_anchor.sizeMode = AnchorSpec::SizeMode::AspectLocked;
        else                           m_anchor.sizeMode = AnchorSpec::SizeMode::Fixed;
    }
    if (cur.HasProp("width"))   cur.GetProp("width",   m_anchor.width);
    if (cur.HasProp("height"))  cur.GetProp("height",  m_anchor.height);
    if (cur.HasProp("longest")) cur.GetProp("longest", m_anchor.longest);
    if (cur.HasProp("aspect"))  cur.GetProp("aspect",  m_anchor.aspectRatio);
    return true;
}

// Compute m_position and m_size from m_anchor for a viewport with the given
// aspect factor. `factor` is (4/3) / (viewportWidth/viewportHeight) — the
// same convention as the legacy SetFactor.
//
// New model coordinates are viewport-normalized [0,1] with y growing DOWN
// (screen-space convention: anchorV="top" is y=0). Legacy m_position uses
// [-1,+1] with y growing UP (anchorV="top" is y=+1). One axis is flipped on
// conversion. m_size is half-extent in legacy [-1,+1] space (numerically
// equal to the viewport-fraction of the widget's full extent).
void WithCoordinates::ApplyAnchorLayout(float factor) {
    // Visible cockpit NDC bounds, used both for the stretched-Y size
    // override (just below) and for anchor placement (further down).
    // Hoisted out of the position block so size math can reach it.
    const float vpAspect  = (factor > 0.0f) ? ((4.0f / 3.0f) / factor) : (4.0f / 3.0f);
    // Visible-top in cockpit NDC. With EqualAspectBottom's new max(.,1)
    // clamp, the viewport pixel height = max(sr_W, sr_H) (when the
    // cockpit rViewport's width fraction is 1.0). The visible NDC y
    // range in clip space is always [-1, +1].
    //   - Landscape FBO (W ≥ H): viewport is W × W pixels, anchored at
    //     FBO bottom. FBO top sits at NDC y = 2H/W − 1 (less than +1;
    //     the cockpit square extends above the screen and is clipped).
    //   - Portrait FBO (W < H): viewport is W × H pixels (covers full
    //     FBO after the max clamp). FBO top = NDC y +1.
    // The std::min(1, …) collapses both cases to the right value.
    const float visTopY   = std::min(1.0f, 2.0f / vpAspect - 1.0f);
    const float visBotY   = -1.0f;
    const float visRangeY = visTopY - visBotY;  // = visTopY + 1

    // ---- Size first (position depends on widget half-extent for pivot) ----
    float wFrac = m_useAnchorSize ? m_anchor.width  : m_size.x;
    float hFrac = m_useAnchorSize ? m_anchor.height : m_size.y;

    if (m_useAnchorSize && m_anchor.sizeMode == AnchorSpec::SizeMode::AspectLocked) {
        // Pixel-square aspect-locked widgets. The pixel size is keyed off
        // min(W, H), NOT max(W, H). Effects:
        //   - Landscape 1920×1080 → button = 110 px (unchanged; this
        //     calibrates with kSizeBase = 16/9 so existing XMLs that
        //     used longest=0.057 still produce 110-px buttons here).
        //   - Ultrawide 21:9 3440×1440 → 146 px (vs 196 px before;
        //     scaled to the shorter axis, not the wider).
        //   - Ultrawide 32:9 5120×1440 → 146 px (same as 21:9 — no
        //     further growth with W).
        //   - Square 1080×1080 → 110 px (calibration baseline).
        //   - Portrait 1080×1920 → 110 px (same as rotated 16:9, since
        //     min stays at 1080).
        //   - Extreme portrait 540×1920 → 55 px (the "slight shrinking
        //     when W < H" the user asked for — naturally proportional
        //     to W/H since min picks W in portrait).
        // The 16/9 multiplier is a calibration constant so longest values
        // tuned on 16:9 landscape don't need updating.
        const float fboW       = static_cast<float>(sr_screenWidth);
        const float fboH       = static_cast<float>(sr_screenHeight);
        const float minDim     = std::min(fboW, fboH);
        constexpr float kSizeBase = 16.0f / 9.0f;
        const float pixelFull  = m_anchor.longest * minDim * kSizeBase;

        // Apply widget aspect ratio (default 1.0 = square in pixels).
        const float widgetAspect = (m_anchor.aspectRatio > 0.0f) ? m_anchor.aspectRatio : 1.0f;
        float pixHalfX, pixHalfY;
        if (widgetAspect >= 1.0f) { pixHalfX = pixelFull * 0.5f; pixHalfY = pixelFull * 0.5f / widgetAspect; }
        else                      { pixHalfY = pixelFull * 0.5f; pixHalfX = pixelFull * 0.5f * widgetAspect; }

        // Convert pixel half-extents to NDC. EqualAspectBottom's OpenGL
        // viewport has: width = fboW, height = max(fboW, fboH). Landscape
        // gives a square viewport (= 2 × fboW/2 NDC pixel range); portrait
        // gives the full FBO (Y scaled to fboH pixels). hFrac uses the
        // larger denominator on portrait so the widget remains pixel-
        // square regardless of FBO aspect.
        const float vpWidthPx  = fboW;
        const float vpHeightPx = std::max(fboW, fboH);
        if (vpWidthPx  > 0.0f) wFrac = pixHalfX / (vpWidthPx  * 0.5f);
        if (vpHeightPx > 0.0f) hFrac = pixHalfY / (vpHeightPx * 0.5f);
    }

    // Stretch overrides size on the stretched axis.
    //   X: SDL fraction × 1.0 → NDC half-extent (SDL X range 1.0 maps to
    //      NDC range 2.0, so half-extent = SDL fraction). Note: hFrac uses
    //      visRangeY because the Y axis is compressed in landscape — see
    //      the visTopY clamp in the position block below.
    if (m_useAnchorPos) {
        if (m_anchor.anchorH == AnchorSpec::Anchor::Stretch)
            wFrac = m_anchor.stretchMaxX - m_anchor.stretchMinX;
        if (m_anchor.anchorV == AnchorSpec::Anchor::Stretch)
            hFrac = 0.5f * (m_anchor.stretchMaxY - m_anchor.stretchMinY) * visRangeY;
    }

    if (m_useAnchorSize) {
        m_size.x = wFrac;
        m_size.y = hFrac;
        m_originalSize = m_size;
    }

    // ---- Position with auto-pivot ----
    if (m_useAnchorPos) {
        // visTopY / visBotY / visRangeY are hoisted to the top of the
        // function so the size block can use them too. anchorV="top" is
        // clamped to the visible cockpit area (which falls below NDC y=+1
        // in landscape because EqualAspectBottom extends the cockpit
        // viewport above the actual screen — see rViewport.cpp:449).

        // Safe area insets (notch/Dynamic Island on iOS, zero on other platforms).
        // Insets are screen fractions → NDC: fraction * 2.0 (NDC range is 2.0).
        const sr_SafeAreaInsets sa = sr_GetSafeAreaInsets();
        const float saLeft  = sa.left  * 2.0f;
        const float saRight = sa.right * 2.0f;
        const float saTop   = sa.top   * visRangeY;
        const float saBot   = sa.bottom * visRangeY;

        // Anchor reference in NDC, inset by safe area.
        auto anchorXNdc = [&](AnchorSpec::Anchor a) -> float {
            switch (a) {
                case AnchorSpec::Anchor::Min:     return -1.0f + saLeft;
                case AnchorSpec::Anchor::Center:  return  0.0f;
                case AnchorSpec::Anchor::Max:     return  1.0f - saRight;
                case AnchorSpec::Anchor::Stretch: return  0.0f;
            }
            return 0.0f;
        };
        auto anchorYNdc = [&](AnchorSpec::Anchor a) -> float {
            switch (a) {
                case AnchorSpec::Anchor::Min:     return visTopY - saTop;
                case AnchorSpec::Anchor::Center:  return 0.5f * (visTopY + visBotY);
                case AnchorSpec::Anchor::Max:     return visBotY + saBot;
                case AnchorSpec::Anchor::Stretch: return 0.5f * (visTopY + visBotY);
            }
            return 0.0f;
        };

        // Pivot offsets directly in NDC. m_size half-extents already are
        // in NDC ([-1,+1] half-extent). Auto-pivot for anchorV="top"
        // means the widget TOP edge sits at the anchor — its center is
        // BELOW (smaller NDC y) by halfH.
        auto pivotShiftX = [](AnchorSpec::Anchor a, float halfExt) -> float {
            switch (a) {
                case AnchorSpec::Anchor::Min:     return +halfExt;
                case AnchorSpec::Anchor::Center:  return 0.0f;
                case AnchorSpec::Anchor::Max:     return -halfExt;
                case AnchorSpec::Anchor::Stretch: return 0.0f;
            }
            return 0.0f;
        };
        auto pivotShiftY = [](AnchorSpec::Anchor a, float halfExt) -> float {
            switch (a) {
                case AnchorSpec::Anchor::Min:     return -halfExt;  // top edge at anchor
                case AnchorSpec::Anchor::Center:  return 0.0f;
                case AnchorSpec::Anchor::Max:     return +halfExt;  // bottom edge at anchor
                case AnchorSpec::Anchor::Stretch: return 0.0f;
            }
            return 0.0f;
        };

        float ax, ay;
        if (m_anchor.anchorH == AnchorSpec::Anchor::Stretch) {
            // Stretch X: anchor at midpoint of stretchMinX..stretchMaxX,
            // those values are SDL 0..1 → NDC -1..+1 (range 2).
            const float midSdl = 0.5f * (m_anchor.stretchMinX + m_anchor.stretchMaxX);
            ax = midSdl * 2.0f - 1.0f;
        } else {
            ax = anchorXNdc(m_anchor.anchorH);
        }
        if (m_anchor.anchorV == AnchorSpec::Anchor::Stretch) {
            const float midSdl = 0.5f * (m_anchor.stretchMinY + m_anchor.stretchMaxY);
            ay = visTopY - midSdl * visRangeY;  // SDL Y is compressed into visible range
        } else {
            ay = anchorYNdc(m_anchor.anchorV);
        }

        // Offsets in SDL fractions of the visible range.
        const float offsetXNdc =  m_anchor.offsetX * 2.0f;     // SDL X range = NDC 2.0
        const float offsetYNdc = -m_anchor.offsetY * visRangeY;// SDL Y down → NDC y down

        m_position.x = ax + pivotShiftX(m_anchor.anchorH, m_size.x) + offsetXNdc;
        m_position.y = ay + pivotShiftY(m_anchor.anchorV, m_size.y) + offsetYNdc;
        m_originalPosition = m_position;
    }
}

// Legacy y-correction factor, adjusted for the post-2026-05-22
// EqualAspectBottom widening. The cockpit's GL viewport pixel height is
// max(vpW, vpH); for landscape FBOs that's vpW (= 4/3 / factor in this
// scope, ≥ 1 when vpAspect ≥ 1) — unchanged from before, so legacy
// gauges land in the same pixel position. For portrait FBOs the
// viewport widened to vpH pixels, which would shift legacy gauges UP if
// we kept passing the same factor; scaling by vpAspect = vpW/vpH brings
// them back to their old pixel position (~67% from FBO bottom for a
// centered widget on a 960×1080 sub-viewport, matching the pre-widening
// behaviour of all legacy cockpits including the default).
//
// The new anchor model is NOT affected — ApplyAnchorLayout reads vpAspect
// from `factor` directly so it sees the unmodified value.
static float sg_LegacyYFactor(float factor) {
    if (factor <= 0.0f) return factor;
    const float vpAspect = (4.0f / 3.0f) / factor;
    return (vpAspect >= 1.0f) ? factor : factor * vpAspect;
}

//!@arg factor the factor to multiply with
void WithCoordinates::SetFactor(float factor) {
    // Mixed widgets are supported: a dimension that opted into the anchor
    // model is computed via ApplyAnchorLayout; the other dimension (if any)
    // falls back to the legacy y-aspect tweak.
    if (m_useAnchorPos || m_useAnchorSize) {
        ApplyAnchorLayout(factor);
        const float legacy = sg_LegacyYFactor(factor);
        if (!m_useAnchorPos) m_position.y = (m_originalPosition.y + 1.) * legacy - 1.;
        if (!m_useAnchorSize) m_size.y = m_originalSize.y * legacy;
        return;
    }
    const float legacy = sg_LegacyYFactor(factor);
    m_position.y = (m_originalPosition.y + 1.) * legacy - 1.;
    m_size.y = m_originalSize.y * legacy;
}

bool WithDataFunctions::Process(tXmlParser::node cur) {
    return Base::Process(cur);
}

//! @param cur the node to be parsed as DataSet
//! @return the resulting data set
tValue::Set WithDataFunctions::ProcessDataSet(tXmlParser::node cur) {
    tValue::BasePtr
    value(new tValue::Base()),
    minimum(new tValue::Base()),
    maximum(new tValue::Base());
    for (cur = cur.GetFirstChild(); cur; ++cur) {
        tValue::Base *newvalue = 0;
        tString name = cur.GetName();
        if(name == "AtomicData") {
            newvalue = ProcessAtomicData(cur);
        } else if(name == "Conditional") {
            newvalue = ProcessConditional(cur);
        } else if(name == "Math") {
            newvalue = ProcessMath(cur);
        }
        else
            if (name == "Value")
                newvalue = ProcessValue(cur);
        if(newvalue != 0) {
            tString field = cur.GetProp("field");
            if(field == "source") {
                value = tValue::BasePtr(newvalue);
            } else if(field=="minimum") {
                minimum = tValue::BasePtr(newvalue);
            } else if(field=="maximum") {
                maximum = tValue::BasePtr(newvalue);
            }
        }
    }
    // TODO:
    /*
    return tValue::Set(
               value.release(),
               minimum.release(),
               maximum.release());
    */
    return tValue::Set(
               value,
               minimum,
               maximum);
}

tValue::Base *WithDataFunctions::ProcessMath(tXmlParser::node cur) {
    tValue::BasePtr lvalue(ProcessDataSource(cur.GetProp("lvalue")));
    tValue::BasePtr rvalue(ProcessDataSource(cur.GetProp("rvalue")));

    for (tXmlParser::node child = cur.GetFirstChild(); child; ++child) {
        tString name = child.GetName();
        if(name == "RValue") {
            rvalue = tValue::BasePtr(ProcessConditionalCore(child));
        } else if(name == "LValue") {
            lvalue = tValue::BasePtr(ProcessConditionalCore(child));
        }
    }

    tValue::Base *val;
    if (cur.GetProp("type") == "sum")
        val = new tValue::Add(lvalue, rvalue);
    else
        if (cur.GetProp("type") == "difference")
            val = new tValue::Subtract(lvalue, rvalue);
        else
            if (cur.GetProp("type") == "product")
                val = new tValue::Multiply(lvalue, rvalue);
            else
                if (cur.GetProp("type") == "quotient")
                    val = new tValue::Divide(lvalue, rvalue);
                else
                    if (cur.GetProp("type") == "power")
                        val = new tValue::Power(lvalue, rvalue);
                    else
                        if (cur.GetProp("type") == "root")
                            val = new tValue::Root(lvalue, rvalue);
                        else
                        {
                            tERR_WARN("Type '" + cur.GetProp("type") + "' unknown!");
                            val = new tValue::Add(lvalue, rvalue);
                        }
    ProcessDataTags(cur, *val);
    return val;
}

tValue::Base *WithDataFunctions::ProcessConditional(tXmlParser::node cur) {
    tValue::BasePtr lvalue(ProcessDataSource(cur.GetProp("lvalue")));
    tValue::BasePtr rvalue(ProcessDataSource(cur.GetProp("rvalue")));
    tValue::BasePtr truevalue(new tValue::Base), falsevalue(new tValue::Base);

    tString oper = cur.GetProp("operator");
    tValue::Base *condvalue = NULL;
    if (oper.size() == 2)
    {
        switch (oper[1]) {
    case 't': case 'T':
            switch (oper[0]) {
        case 'g': case 'G':
                condvalue = new tValue::GreaterThan    (lvalue, rvalue);
                break;
        case 'l': case 'L':
                condvalue = new tValue::   LessThan    (lvalue, rvalue);
                break;
            }
            break;
    case 'e': case 'E':
            switch (oper[0]) {
        case 'g': case 'G':
                condvalue = new tValue::GreaterOrEquals(lvalue, rvalue);
                break;
        case 'l': case 'L':
                condvalue = new tValue::   LessOrEquals(lvalue, rvalue);
                break;
        case 'n': case 'N':
                tValue::BasePtr precond(new tValue::Equals(lvalue, rvalue));
                condvalue = new tValue::Not(precond);
                break;
            }
            break;
    case 'q': case 'Q':
            if (oper[0] == 'e' || oper[0] == 'E')
                condvalue = new tValue::         Equals(lvalue, rvalue);
            break;
        }
    }
    if (!condvalue)
    {
        tERR_WARN("Operator '" + oper + "' unknown!");
        condvalue = new tValue::Equals(lvalue, rvalue);
    }
    tValue::BasePtr condvalueP(condvalue);

    for (cur = cur.GetFirstChild(); cur; ++cur) {
        tString name = cur.GetName();
        if(name == "IfTrue") {
            truevalue = tValue::BasePtr(ProcessConditionalCore(cur));
        } else if(name == "IfFalse") {
            falsevalue = tValue::BasePtr(ProcessConditionalCore(cur));
        }
    }

    return new tValue::Condition(condvalueP, truevalue, falsevalue);
}

tValue::Base *WithDataFunctions::ProcessConditionalCore(tXmlParser::node cur) {
    for (cur = cur.GetFirstChild(); cur; ++cur) {
        tString name = cur.GetName();
        if(name == "AtomicData") {
            return ProcessAtomicData(cur);
        }
        if(name == "Conditional") {
            return ProcessConditional(cur);
        }
        if(name == "Math") {
            return ProcessMath(cur);
        }
        else
            if (name == "Value")
                return ProcessValue(cur);
    }
    tERR_WARN("IfTrue or IfFalse node doesn't contain an AtomicData or Conditional node")
    return new tValue::Base();
}

tValue::Base *WithDataFunctions::ProcessAtomicData(tXmlParser::node cur) {
    tValue::Base *ret = ProcessDataSource(cur.GetProp("source"));
    ProcessDataTags(cur, *ret);
    return ret;
}

tValue::Base *WithDataFunctions::ProcessValue(tXmlParser::node cur) {
#ifndef WIN32
    return tValueParser::parse(cur.GetProp("expr"));
#else
    return 0;
#endif
}

void WithDataFunctions::ProcessDataTags(tXmlParser::node cur, tValue::Base &data) {
    int precision, minwidth;
    cur.GetProp("precision", precision);
    cur.GetProp("minwidth", minwidth);
    tString fill = cur.GetProp("fill");
    if(fill.size() != 1) {
        tERR_WARN("Attribute 'fill' has to have a length of 1!");
        fill = "!";
    }
    data.SetPrecision(precision);
    data.SetMinsize(minwidth);
    data.SetFill(fill(0));
}

tValue::Base *WithDataFunctions::ProcessDataSource(tString const &data) {
    // A color code by itself is convertible to a float, so check for that
    // first.
    if ( data.StartsWith( "0x" ) )
        return new tValue::String( data );

    //is it an integer?
    int val_int;
    if(data.Convert(val_int)) return new tValue::Int(val_int);
    //is it a float
    float val_float;
    if(data.Convert(val_float)) return new tValue::Float(val_float);

    //is it one of the dynamic callbacks?
    std::map<tString, tValue::Callback<cCockpit>::cb_ptr>::const_iterator iter;
    if((iter = stc_callbacks.find(data.ToLower())) != stc_callbacks.end()) {
        if(stc_forbiddenCallbacks.count(data.ToLower())) return new tValue::Base();
        return new tValue::Callback<cCockpit>(iter->second, m_Cockpit);
    }

    //growing desperate... is this a configuration value maybe?
    tValue::ConfItem *item = new tValue::ConfItem(data);
    if(item->Good())
        return item;
    delete item;

    //Ok, giving up... this has to be a string then.
    return new tValue::String(data);
}

bool WithSingleData::Process(tXmlParser::node cur) {
    if(cur.IsOfType("DataSet")) {
        m_data = ProcessDataSet(cur);
        return true;
    }
    return WithDataFunctions::Process(cur);
}

bool WithIdData::Process(tXmlParser::node cur) {
    if(cur.IsOfType("DataSet")) {
        tString id = cur.GetProp("id");
        if(id.empty()) {
            tERR_WARN("Empty or no id tag where needed!");
            return true;
        }
        m_data[id] = ProcessDataSet(cur);
        return true;
    }
    return WithDataFunctions::Process(cur);
}

bool WithTable::Process(tXmlParser::node cur) {
    if(cur.IsOfType("Face")) {
        ProcessCore(cur);
        return true;
    }
    return WithIdData::Process(cur);
}

void WithTable::ProcessCore(tXmlParser::node cur) {
    for (cur = cur.GetFirstChild(); cur; ++cur) {
        if(cur.IsOfType("Table")) {
            ProcessTable(cur);
            return;
        }
    }
}

void WithTable::ProcessTable(tXmlParser::node cur) {
    for (cur = cur.GetFirstChild(); cur; ++cur) {
        if(cur.IsOfType("Row")) {
            m_table.push_back(std::deque<std::deque<tValue::Set> >());
            ProcessRow(cur);
        }
    }
}

void WithTable::ProcessRow(tXmlParser::node cur) {
    for (cur = cur.GetFirstChild(); cur; ++cur) {
        tString name = cur.GetName();
        if(name == "Cell") {
            m_table.back().push_back(std::deque<tValue::Set>());
            ProcessCell(cur);
        }
    }
}

void WithTable::ProcessCell(tXmlParser::node cur) {
    for (cur = cur.GetFirstChild(); cur; ++cur) {
        if(cur.IsOfType("Text")) {
            tValue::BasePtr a(new tValue::String(cur.GetProp("value")));
            m_table.back().back().push_back(tValue::Set(a));
        } else if(cur.IsOfType("GameData")) {
            std::map<tString, tValue::Set>::iterator iter;
            if((iter = m_data.find(cur.GetProp("data"))) != m_data.end()) {
                m_table.back().back().push_back((tValue::Set(iter->second)));
            } else {
                tERR_WARN("Id '" + cur.GetProp("data") + "' undefined!");
            }
        }
    }
}

bool WithColorFunctions::Process(tXmlParser::node cur) {
    return Base::Process(cur);
}

rGradient WithColorFunctions::ProcessGradient(tXmlParser::node cur) {
    rGradient ret;
    for (cur = cur.GetFirstChild(); cur; ++cur) {
        tString name=cur.GetName();
        if(name == "Solid") {
            ProcessGradientCore(cur, ret);
        } else if(name == "Gradient") {
            std::map<tString, rGradient::direction> directions;
            directions[tString("horizontal")] = rGradient::horizontal;
            directions[tString("vertical")] = rGradient::vertical;
            directions[tString("value")] = rGradient::value;
            std::map<tString, rGradient::direction>::iterator iter;
            if((iter = directions.find(cur.GetProp("orientation"))) != directions.end()) {
                ret.SetDir(iter->second);
            } else {
                tERR_WARN("Gradient orientation '" + cur.GetProp("orientation") + "' unknown!");
            }
            ProcessGradientCore(cur, ret);
        } else if(name == "Image") {
            int rep = 0;
            std::map<tString, int> repeat;
            repeat[tString("none")] = 0;
            repeat[tString("x")] = 1;
            repeat[tString("y")] = 2;
            repeat[tString("both")] = 3;
            std::map<tString, int>::iterator iter;
            if((iter = repeat.find(cur.GetProp("repeat"))) != repeat.end()) {
                rep = iter->second;
            } else {
                tERR_WARN("Repeat setting '" + cur.GetProp("repeat") + "' unknown!");
            }
            tCoord scale;
            cur.GetProp("scale_x", scale.x);
            cur.GetProp("scale_y", scale.y);
            ret.SetTextureScale(scale);
            ProcessImage(cur, ret, rep);
        }
    }
    return ret;
}

void WithColorFunctions::ProcessGradientCore(tXmlParser::node cur, rGradient &gradient) {
    for (cur = cur.GetFirstChild(); cur; ++cur) {
        if(cur.IsOfType("Color")) {
            float r,g,b,a,at;
            cur.GetProp("r", r);
            cur.GetProp("g", g);
            cur.GetProp("b", b);
            cur.GetProp("alpha", a);
            cur.GetProp("at", at);
            gradient[at] = rColor(r ,g ,b ,a);
        }
    }
}

void WithColorFunctions::ProcessImage(tXmlParser::node cur, rGradient &gradient, int repeat) {
    for(cur = cur.GetFirstChild(); cur; ++cur) {
        if(cur.IsOfType("Graphic")) {
            tResourcePath path(
                cur.HasProp("author") ? cur.GetProp("author") : m_Cockpit->Path().Author(),
                cur.HasProp("category") ? cur.GetProp("category") : m_Cockpit->Path().Category(),
                cur.GetProp("name"),
                cur.GetProp("version"),
                tString("aatex"),
                cur.GetProp("extension"),
                cur.GetProp("uri")
            );
            gradient.SetTexture(rResourceTexture(path, repeat & 1, repeat & 2));

            // SDF rendering mode: sdf="sdf|msdf|mtsdf"
            if (cur.HasProp("sdf")) {
                tString mode = cur.GetProp("sdf");
                if (mode == "sdf")       gradient.SetSDFMode(1);
                else if (mode == "msdf") gradient.SetSDFMode(2);
                else if (mode == "mtsdf") gradient.SetSDFMode(3);

                // Optional outline parameters. Each GetProp(name, T&) call
                // logs a "Call for non-existent Attribute" warning when the
                // attribute is missing, so gate the read on HasProp first
                // — the no-outline case is the common one and the warnings
                // were flooding the console every time a Graphic loaded.
                float outW = 0.0f, outR = 0.0f, outG = 0.0f, outB = 0.0f;
                if (cur.HasProp("outline"))  cur.GetProp("outline",  outW);
                if (cur.HasProp("outlineR")) cur.GetProp("outlineR", outR);
                if (cur.HasProp("outlineG")) cur.GetProp("outlineG", outG);
                if (cur.HasProp("outlineB")) cur.GetProp("outlineB", outB);
                gradient.SetSDFOutline(outW, outR, outG, outB);
            }
        }
    }
}

bool WithForeground::Process(tXmlParser::node cur) {
    if(cur.IsOfType("Foreground")) {
        m_foreground = ProcessGradient(cur);
        return true;
    }
    return WithColorFunctions::Process(cur);
}

bool WithBackground::Process(tXmlParser::node cur) {
    if(cur.IsOfType("Background")) {
        m_background = ProcessGradient(cur);
        return true;
    }
    return WithColorFunctions::Process(cur);
}

bool WithLineColor::Process(tXmlParser::node cur) {
    if(cur.IsOfType("LineColor")) {
        m_line_color = ProcessGradient(cur);
        return true;
    }
    return WithColorFunctions::Process(cur);
}

bool WithCaption::Process(tXmlParser::node cur) {
    if(cur.IsOfType("Caption")) {
        ProcessCaption(cur);
        return true;
    }
    return Base::Process(cur);
}

void WithCaption::ProcessCaption(tXmlParser::node cur) {
    ProcessCaptionLocation(cur);
    for (cur = cur.GetFirstChild(); cur; ++cur) {
        if(cur.IsOfType("Text")) {
            m_caption = cur.GetProp("value");
        }
    }
}

void WithCaption::ProcessCaptionLocation(tXmlParser::node cur) {
    std::map<tString, int> locations;
    locations[tString("top")] = top;
    locations[tString("bottom")] = bottom;
    locations[tString("off")] = off;

    std::map<tString, int>::iterator iter;
    if((iter = locations.find(cur.GetProp("location"))) != locations.end()) {
        m_captionloc = iter->second;
    } else {
        tERR_WARN("Location '" + cur.GetProp("location") + "' unknown!");
        m_captionloc = bottom;
    }
}

bool WithReverse::Process(tXmlParser::node cur) {
    if(cur.IsOfType("Reverse")) {
        m_reverse = cur.GetPropBool("value");
        return true;
    }
    return Base::Process(cur);
}

bool WithAngles::Process(tXmlParser::node cur) {
    if(cur.IsOfType("Angles")) {
        cur.GetProp("min", m_angle_min);
        cur.GetProp("max", m_angle_max);
        m_angle_min *= M_PI / 180.;
        m_angle_max *= M_PI / 180.;
        return true;
    }
    return Base::Process(cur);
}

bool WithShowSettings::Process(tXmlParser::node cur) {
    if(cur.IsOfType("ShowMinimum")) {
        m_showmin = cur.GetPropBool("value");
        return true;
    }
    if(cur.IsOfType("ShowMaximum")) {
        m_showmax = cur.GetPropBool("value");
        return true;
    }
    if(cur.IsOfType("ShowCurrent")) {
        m_showvalue = cur.GetPropBool("value");
        return true;
    }
    return Base::Process(cur);
}

}

#endif
