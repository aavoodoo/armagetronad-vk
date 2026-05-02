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

#include "rFont.h"
#include "rScreen.h"
#include "tConfiguration.h"
#include "tDirectories.h"
#include "tCoord.h"
#include "tColor.h"
#include "tError.h"
#include <ctype.h>

#ifndef DEDICATED
#include "rRender.h"
#include "rVertex.h"
#include "rRenderQueue.h"
#include "rTexture.h"
#include "rFontSTB.h"

static size_t my_strnlen(char const * c, size_t i) {
    char const *begin = c;
    char const *end = c + i;
    for(; *c && c != end; ++c) ;
    return c - begin;
}
#define my_strncmp strncmp

int sr_fontType = sr_fontTexture;
static tConfItem< int > sr_fontTypeConf( "FONT_TYPE", sr_fontType, &sr_ReloadFont);

// MSDF font configuration
// Mode: 0=Legacy, 1=SDF, 2=MSDF, 3=MTSDF
int sr_fontMSDFMode = 0;
static tConfItem< int > sr_fontMSDFModeConf( "FONT_MSDF_MODE", sr_fontMSDFMode, &sr_ReloadFont);

// SDF distance range in pixels
float sr_fontMSDFRange = 4.0f;
static tConfItem< float > sr_fontMSDFRangeConf( "FONT_MSDF_RANGE", sr_fontMSDFRange, &sr_ReloadFont);

// MSDF glyph size in atlas (quality vs memory tradeoff)
int sr_fontMSDFGlyphSize = 48;
static tConfItem< int > sr_fontMSDFGlyphSizeConf( "FONT_MSDF_SIZE", sr_fontMSDFGlyphSize, &sr_ReloadFont);

// Debug: show font atlas texture
int sr_showFontAtlas = 0;
static tConfItem< int > sr_showFontAtlasConf( "SHOW_FONT_ATLAS", sr_showFontAtlas);

// Screen pixel range multiplier for debugging SDF sharpness
float sr_fontSDFPxRangeMult = 1.0f;
static tConfItem< float > sr_fontSDFPxRangeMultConf( "FONT_SDF_PXRANGE_MULT", sr_fontSDFPxRangeMult);

bool restrictLineHeight( float const &newValue )
{
    return newValue > 0;
}

float sr_lineHeight = 1.;
static tConfItem< float > sr_lineHeightconf( "LINE_HEIGHT", sr_lineHeight, &restrictLineHeight );

// Font config items
tString fontFile("Armagetronad.ttf");
static tConfItemLine ff("FONT_FILE", fontFile, &sr_ReloadFont);

tString customFont("");
static tConfItemLine ffc("FONT_FILE_CUSTOM", customFont, &sr_ReloadFont);

int useCustomFont = 0;
static tConfItem<int> ufc("USE_CUSTOM_FONT", useCustomFont, &sr_ReloadFont);

static rCallbackBeforeScreenModeChange reloadft(&sr_ReloadFont);

// STB truetype font container
class rFontContainer : public std::map<int, std::unique_ptr<rIFont>> {
    typedef std::map<int, std::unique_ptr<rIFont>> BaseMap;
    rIFont* GetOrCreateFont(int size);
    [[nodiscard]] tString GetFontPath() const;
public:
    void clear() {
        BaseMap::clear();
    }

    float GetWidth(std::string const &str, float height) {
        rIFont* font = GetOrCreateFont(static_cast<int>(height * sr_screenHeight / 2.));
        if (!font || !font->IsValid())
        {
            static bool warned = false;
            if (!warned)
            {
                tERR_WARN("Font not available for GetWidth(), returning 0");
                warned = true;
            }
            return 0;
        }
        return font->GetTextWidth(str.c_str()) / sr_screenWidth * 2.;
    }

    void Render(std::string const &str, float height, tCoord const &where) {
        rIFont* font = GetOrCreateFont(static_cast<int>(height * sr_screenHeight / 2.));
        if (!font || !font->IsValid())
        {
            static bool warned = false;
            if (!warned)
            {
                tERR_WARN("Font not available for Render(), skipping");
                warned = true;
            }
            return;
        }

        // Get current color for text rendering
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        RenderGetColor(color);

        // Render as batched HUD geometry — pre-transform glyph pixels to NDC
        float scaleX = 2.f / sr_screenWidth;
        float scaleY = 2.f / sr_screenHeight;
        font->RenderBatched(str.c_str(), where.x, where.y, scaleX, scaleY, color);
    }

    void BBox(std::string const &str, float height, tCoord where, float &l, float &b, float &r, float &t) {
        rIFont* font = GetOrCreateFont(static_cast<int>(height * sr_screenHeight / 2.));
        if (!font || !font->IsValid()) {
            l = where.x;
            r = where.x + GetWidth(str, height);
            b = where.y;
            t = where.y + height;
            return;
        }

        rTextBounds bounds;
        font->GetTextBounds(str.c_str(), bounds);

        l = bounds.minX / sr_screenWidth * 2.;
        r = bounds.maxX / sr_screenWidth * 2.;
        t = bounds.maxY / sr_screenHeight * 2.;
        b = bounds.minY / sr_screenHeight * 2.;

        l += where.x - 0.005;
        r += where.x + 0.005;
        t += where.y + 0.005;
        b += where.y - 0.005;
    }

    ~rFontContainer() {
        clear();
    }
};

tString rFontContainer::GetFontPath() const {
    if (useCustomFont == 1) {
        return customFont;
    }
    tString theFontFile("textures/");
    theFontFile << fontFile;
    return tDirectories::Data().GetReadPath(theFontFile);
}

rIFont* rFontContainer::GetOrCreateFont(int size) {
    BaseMap::iterator it = BaseMap::find(size);
    if (it != BaseMap::end()) {
        return it->second.get();
    }

    // Create new font
    tString fontPath = GetFontPath();
    std::unique_ptr<rIFont> font = rFontFactory::Create(fontPath, size);
    if (!font) {
        // Try default font
        tString defaultPath = tDirectories::Data().GetReadPath("textures/Armagetronad.ttf");
        font = rFontFactory::Create(defaultPath, size);
    }

    if (!font) {
        return nullptr;
    }

    rIFont* result = font.get();
    BaseMap::operator[](size) = std::move(font);
    return result;
}

rFontContainer sr_Font;

void sr_ReloadFont(void) {
    sr_Font.clear();
}

// Debug font atlas overlay removed with the GL3 renderer; the Vulkan
// renderer does not expose direct texture binding for debug HUDs.
void sr_RenderFontAtlas(void)
{
}

// Called from rBeginFrame() before any rendering begins.
// Propagates to all loaded font instances so they can perform deferred atlas
// grows (mid-frame grows corrupt in-flight vertex UV coordinates).
void sr_FontBeginFrame(void)
{
    for (auto& [size, font] : sr_Font)
    {
        if (font)
            font->BeginFrame();
    }
}
#endif

rTextField::rTextField(REAL Left,REAL Top,
                       REAL Cheight, sr_fontClass Type)
        :parIndent(0),
left(Left),top(Top),cheight(Cheight),x(0),y(0),realx(0),nextx(Left),currentWidth(0),multiline(false),cursor(0),cursorPos(0){
    if (cheight*sr_screenHeight<18)
        cheight=18/REAL(sr_screenHeight);

    color_ = defaultColor_;

    width = 1.-Left;

    cursor_x = -100;
    cursor_y = -100;
}

REAL rTextField::AspectWidthMultiplier()
{
    return std::min(((4.0f/3.0f)*sr_screenHeight)/sr_screenWidth,1.0f);
}

REAL rTextField::AspectHeightMultiplier()
{
    return std::min(((3.0f/4.0f)*sr_screenWidth)/sr_screenHeight,1.0f);
}

REAL rTextField::Pixelize(REAL xy, int WidthHeight)
{
    auto pixelIn = static_cast<int>(.5f * xy * WidthHeight);
    return (2.0f*(pixelIn+.5f))/WidthHeight;
}


rTextField::~rTextField(){
    FlushLine();

#ifndef DEDICATED
    if (cursor && sr_glOut){
        uint8_t cr, cg, cb, ca;
        if (cursor==2) { cr = 255; cg = 255; cb = 255; ca = 127; }
        else           { cr = 255; cg = 255; cb = 0;   ca = 255; }

        rVertex20 line[2] = {
            rVertex20(cursor_x, cursor_y,          0, cr, cg, cb, ca, 0, 0),
            rVertex20(cursor_x, cursor_y - cheight, 0, cr, cg, cb, ca, 0, 0)
        };
        rRenderStateKey state = rRenderStateKey::HUD(0, rBlendMode::Alpha);
        rRenderQueue::Instance().SubmitLines(rRenderPhase::HUD, state, line, 2);
    }
#endif
}

void rTextField::FlushLine(int len,bool newline){
#ifndef DEDICATED
    float realTop = top-y*cheight;
    FTGL_STRING str(buffer.substr(realx, len));
    realx += len;
    if (len >= cursorPos && cursorPos >= 0) {
        cursor_y=realTop;
        cursor_x=nextx+sr_Font.GetWidth(str.substr(0, cursorPos), cheight);
    }
    cursorPos -= len+1;
    float thisx = nextx+sr_Font.GetWidth(str, cheight);

    REAL r = color_.r_;
    REAL g = color_.g_;
    REAL b = color_.b_;
    REAL a = color_.a_;

    if (sr_glOut)
    {
        // render bright background
        if ( color_.IsDark() )
        {
            if ( sr_alphaBlend && !str.empty() )
            {
                float bl, bt, br, bb;
                sr_Font.BBox(str, cheight, tCoord(nextx, realTop-cheight), bl, bb, br, bt);
                if(bt > realTop) { bt = realTop; }

                uint8_t hr = rFloatToU8(blendColor_.r_);
                uint8_t hg = rFloatToU8(blendColor_.g_);
                uint8_t hb = rFloatToU8(blendColor_.b_);
                uint8_t ha = rFloatToU8(a * blendColor_.a_);

                rVertex20 h0(bl, bb, 0, hr, hg, hb, ha, 0, 0);
                rVertex20 h1(br, bb, 0, hr, hg, hb, ha, 0, 0);
                rVertex20 h2(br, bt, 0, hr, hg, hb, ha, 0, 0);
                rVertex20 h3(bl, bt, 0, hr, hg, hb, ha, 0, 0);
                // Remap to fullscreen NDC for split-screen (matches glyph remap in rFontSTB)
                rVertex20 bgVerts[4] = {h0, h1, h2, h3};
                rRenderStateKey hState = rRenderStateKey::HUD(0, rBlendMode::Alpha);
                rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, hState, bgVerts[0], bgVerts[1], bgVerts[2], bgVerts[3]);
            }
            else
            {
                if ( r < .5 ) r = .5;
                if ( g < .5 ) g = .5;
                if ( b < .5 ) b = .5;
            }
        }

        Color(r * blendColor_.r_,g * blendColor_.g_,b * blendColor_.b_,a * blendColor_.a_);
    }

    // glRasterPos2f is deprecated in GL3, we rely on font system for positioning
    sr_Font.Render(str, cheight, tCoord(nextx, realTop-cheight));
    nextx = thisx;

#endif

    if (newline){
        y++;
        realx=x=0;
        nextx=left;
    }
}

void rTextField::FlushLine(bool newline){
    FlushLine(buffer.size()-realx,newline);
}

inline void rTextField::WriteChar(FTGL_CHAR c)
{
    switch(c){
    case('\n'):
                    FlushLine();
        buffer.clear();
        break;
    default:
        buffer += c;
        x++;
        break;
    }
}

rTextField & rTextField::StringOutput(const FTGL_CHAR * c, ColorMode colorMode)
{
#ifndef DEDICATED
    float const &maxWidth = width;
    bool lastIsNewline = true;
    bool trouble = false; // Do we have a word that won't fit on a line?
    static FTGL_STRING spaces;
    // run through string
    while (*c!='\0')
    {
        if (trouble && !(*c=='0' && my_strnlen(c, 8)>=8 && c[1]=='x' && colorMode != COLOR_IGNORE)) {
            FTGL_STRING str;
            str += *c;
            // be sure to add full utf8 character sequences
            if ( (*c & 0x80) == 0x80 )
            {
                c++;
                while ((*c & 0xc0) == 0x80 )
                {
                    str += *(c);
                    c++;
                }

                // gone one step too far
                c--;
            }
            currentWidth += sr_Font.GetWidth(str, cheight);
            if(isspace(*c)) {
                trouble = false;
            } else if ( currentWidth >= maxWidth) {
                WriteChar('\n');
                spaces.clear();
                for ( int i = parIndent-1; i >= 0; --i )
                {
                    WriteChar(' ');
                    spaces += ' ';
                    cursorPos++;
                }
                currentWidth = sr_Font.GetWidth(spaces, cheight);
            }
        }
        // break line if next space character is too far away
        if ( !trouble && multiline && (isblank(*c) || lastIsNewline) )
        {
            lastIsNewline = false;
            // count number of nonblank characters following
            FTGL_CHAR const * nextSpace = c+1;
            while ( *nextSpace != '\0' && *nextSpace != '\n' && !isblank(*nextSpace) )
            {
                if (*nextSpace=='0' && my_strnlen(nextSpace, 8)>=8 && nextSpace[1]=='x' && colorMode != COLOR_IGNORE )
                {
                    // skip color code
                    nextSpace += 8;
                }
                else
                {
                    // count letter
                    nextSpace++;
                }
            }
            FTGL_STRING str(c, nextSpace);
            str = tColoredString::RemoveColors(str.c_str());
            float wordWidth = sr_Font.GetWidth(str, cheight);

            currentWidth += wordWidth;
            if ( currentWidth >= maxWidth)
            {
                WriteChar('\n');
                c++;

                spaces.clear();
                for ( int i = parIndent-1; i >= 0; --i )
                {
                    WriteChar(' ');
                    spaces += ' ';
                    cursorPos++;
                }
                float spaceWidth = sr_Font.GetWidth(spaces, cheight);

                if (wordWidth >= maxWidth) {
                    trouble = true;
                    currentWidth = spaceWidth;
                } else {
                    currentWidth = wordWidth + spaceWidth;
                }
                continue;
            }
        }
        if ( *c == '\n' ) {
            lastIsNewline = true;
            currentWidth = 0.;
            cursorPos += 1;
        }

        // detect presence of color code

        FTGL_CHAR const resett[] = {
                                     static_cast<FTGL_CHAR>('0'),
                                     static_cast<FTGL_CHAR>('x'),
                                     static_cast<FTGL_CHAR>('R'),
                                     static_cast<FTGL_CHAR>('E'),
                                     static_cast<FTGL_CHAR>('S'),
                                     static_cast<FTGL_CHAR>('E'),
                                     static_cast<FTGL_CHAR>('T'),
                                     static_cast<FTGL_CHAR>('T'),
                                     0};
        bool isResettColor = false;
        bool isUsableColor = colorMode != COLOR_IGNORE && *c == '0' && my_strnlen(c, 8) >= 8 && c[1] == 'x';

        // Check for reset color first, because VerifyColorCode() will by default return true for 0xRESETT.
        if ( isUsableColor &&  ( ( isResettColor = 0 == my_strncmp( c, resett, 8 ) ) || tColor::VerifyColorCode( c ) ) )
        {
            tColor color = isResettColor ? defaultColor_ : tColor( c );

            if ( colorMode == COLOR_USE )
            {
                // Advance over the color code.
                c += 8;

                // The code will be hidden, so move the cursor to correct position.
                cursorPos -= 8;
            }
            else
            {
                // colorMode is COLOR_SHOW. Write the color code out.
                for(int i=7; i>=0;--i)
                    WriteChar(*(c++));
            }

            FlushLine(false);
            cursorPos++;
            color_ = color;
        }
        else
        {
            // normal operation: add char
            WriteChar(*(c++));
        }
    }
#endif
    return *this;
}

rTextField & operator<<(rTextField &c,const FTGL_STRING &x){
    return c.StringOutput(x.c_str());
}

void DisplayText(REAL x,REAL y,REAL h,const char *t,sr_fontClass type,int center,int cursor,int cursorPos, rTextField::ColorMode colorMode){
#ifndef DEDICATED
    tString text( t );
    // transform string
    STRING_TO_FTGL( text, str );
    // do so again, with colors removed
    STRING_TO_FTGL( tColoredString::RemoveColors(t), str_colorless );
    float height;
    float width = rTextField::GetTextLengthRaw(str_colorless, h, true, &height);

    // shrink fields that don't fit the screen
    REAL maxw = 1.95 - x;
    if ( width > maxw )
    {
        h *= maxw/(width);
        width=maxw;
    }

    rTextField c(x-(center+1)*width*.5,y+h*.5,h,type);
    if (center==-1)
        c.SetWidth(1.-x);
    else
        c.SetWidth(10000);

    c.SetIndent(5);
    if (cursor)
    {
        c.SetCursor(cursor,cursorPos);
    }
    c.StringOutput(str.c_str(), colorMode );
#endif
}

// only here for compatibility with stuff merged from 0.2.9
void DisplayTextAutoWidth(REAL x, REAL y, const char *text, REAL h, int center, int cursor, int cursorPos, rTextField::ColorMode colorMode)
{
    DisplayText(x, y, h, text, sr_fontClass::sr_fontConsole, center, cursor, cursorPos, colorMode);
}

// only here for compatibility with stuff merged from 0.2.9
void DisplayTextAutoHeight(REAL x, REAL y, const char *text, REAL w, int center, int cursor, int cursorPos, rTextField::ColorMode colorMode)
{
    DisplayText(x, y, w*(rCHEIGHT_NORMAL/rCWIDTH_NORMAL)*rTextField::AspectHeightMultiplier(), text, sr_fontClass::sr_fontConsole, center, cursor, cursorPos, colorMode);
}

// *******************************************************************************************
// *
// *	GetDefaultColor
// *
// *******************************************************************************************
//!
//!		@return		default color
//!
// *******************************************************************************************

tColor const & rTextField::GetDefaultColor( void )
{
    return defaultColor_;
}

// *******************************************************************************************
// *
// *	GetDefaultColor
// *
// *******************************************************************************************
//!
//!		@param	defaultColor	default color to fill
//!
// *******************************************************************************************

void rTextField::GetDefaultColor( tColor & defaultColor )
{
    defaultColor = defaultColor_;
}

// *******************************************************************************************
// *
// *	SetDefaultColor
// *
// *******************************************************************************************
//!
//!		@param	defaultColor	default color to set
//!
// *******************************************************************************************

void rTextField::SetDefaultColor( tColor const & defaultColor )
{
    defaultColor_ = defaultColor;
    if ( !sr_alphaBlend )
    {
        defaultColor_.r_ *= defaultColor_.a_;
        defaultColor_.g_ *= defaultColor_.a_;
        defaultColor_.b_ *= defaultColor_.a_;
        defaultColor_.a_ = 1;
    }
    blendColor_ = tColor();
}

// *******************************************************************************************
// *
// *	GetBlendColor
// *
// *******************************************************************************************
//!
//!		@return		color all other colors are multiplied with
//!
// *******************************************************************************************

tColor const & rTextField::GetBlendColor( void )
{
    return blendColor_;
}

// *******************************************************************************************
// *
// *	GetBlendColor
// *
// *******************************************************************************************
//!
//!		@param	blendColor	color all other colors are multiplied with to fill
//!
// *******************************************************************************************

void rTextField::GetBlendColor( tColor & blendColor )
{
    blendColor = blendColor_;
}

// *******************************************************************************************
// *
// *	SetBlendColor
// *
// *******************************************************************************************
//!
//!		@param	blendColor	color all other colors are multiplied with to set
//!
// *******************************************************************************************

void rTextField::SetBlendColor( tColor const & blendColor )
{
    blendColor_ = blendColor;
    if ( !sr_alphaBlend )
    {
        blendColor_.r_ *= blendColor_.a_;
        blendColor_.g_ *= blendColor_.a_;
        blendColor_.b_ *= blendColor_.a_;
        blendColor_.a_ = 1;
    }
}

tColor rTextField::defaultColor_;
tColor rTextField::blendColor_;

#ifndef DEDICATED
//! @param str the string to be used
//! @param height the height of one character
//! @param stripColors should colors be recognized?
//! @param useNewline should newlines be recognized (and the longest line be found)?
//! @param resultingHeight address to store the number of lines (height times the number of newlines+1)
//! @returns the width of the string if it was printed
float rTextField::GetTextLength (std::string const &utf8str, float height, bool stripColors, bool useNewline, float *resultingHeight) {
    if(stripColors) {
        tString colorlessstr = tColoredString::RemoveColors(utf8str.c_str());
        STRING_TO_FTGL(colorlessstr, str);
        return GetTextLengthRaw( str, height, useNewline, resultingHeight );
    } else {
        STRING_TO_FTGL( utf8str, str );
        return GetTextLengthRaw( str, height, useNewline, resultingHeight );
    }
}

//! @param str the string to be used
//! @param height the height of one character
//! @param useNewline should newlines be recognized (and the longest line be found)?
//! @param resultingHeight address to store the number of lines (height times the number of newlines+1)
//! @returns the width of the string if it was printed
float rTextField::GetTextLengthRaw (FTGL_STRING const &str, float height, bool useNewline, float *resultingHeight) {
    return sr_Font.GetWidth(str, height); //TODO: Implement all the rest!
}


#endif
