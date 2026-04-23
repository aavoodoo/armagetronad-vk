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

Font rendering interface - abstracts font backend (stb_truetype)

*/

#ifndef RFONTINTERFACE_H
#define RFONTINTERFACE_H

#include "defs.h"
#include "tString.h"
#include <memory>

//! Glyph metrics for a single character
struct rGlyphMetrics
{
    int width;          //!< Width of glyph in pixels
    int height;         //!< Height of glyph in pixels
    int bearingX;       //!< Left side bearing
    int bearingY;       //!< Top side bearing (baseline to top)
    int advance;        //!< Horizontal advance to next glyph

    // Texture coordinates (normalized 0-1)
    float texU0, texV0; //!< Top-left UV
    float texU1, texV1; //!< Bottom-right UV

    // Texture ID (for atlas-based rendering)
    unsigned int textureId;

    rGlyphMetrics() : width(0), height(0), bearingX(0), bearingY(0), advance(0),
                      texU0(0), texV0(0), texU1(0), texV1(0), textureId(0) {}
};

//! Bounding box for text
struct rTextBounds
{
    float minX, minY;   //!< Lower-left corner
    float maxX, maxY;   //!< Upper-right corner

    float Width() const { return maxX - minX; }
    float Height() const { return maxY - minY; }

    rTextBounds() : minX(0), minY(0), maxX(0), maxY(0) {}
};

//! Abstract font interface
class rIFont
{
public:
    virtual ~rIFont() = default;

    //! Load font from file at specified size
    //! @param path Path to font file (TTF)
    //! @param size Font size in pixels
    //! @return true on success
    virtual bool Load(const char* path, int size) = 0;

    //! Check if font is loaded and valid
    virtual bool IsValid() const = 0;

    //! Get the line height (baseline to baseline)
    virtual float GetLineHeight() const = 0;

    //! Get the ascender (baseline to top of tallest glyph)
    virtual float GetAscender() const = 0;

    //! Get the descender (baseline to bottom of lowest glyph, typically negative)
    virtual float GetDescender() const = 0;

    //! Get metrics for a single glyph
    //! @param codepoint Unicode codepoint
    //! @param metrics Output metrics
    //! @return true if glyph exists
    virtual bool GetGlyphMetrics(unsigned int codepoint, rGlyphMetrics& metrics) = 0;

    //! Get the advance width for a string (horizontal text width)
    //! @param text UTF-8 encoded text
    //! @return Width in pixels
    virtual float GetTextWidth(const char* text) = 0;

    //! Get bounding box for text
    //! @param text UTF-8 encoded text
    //! @param bounds Output bounding box
    virtual void GetTextBounds(const char* text, rTextBounds& bounds) = 0;

    //! Render text as batched HUD geometry (pre-transformed to NDC)
    //! Submits to HUD phase queue — no matrix stack needed.
    //! @param text UTF-8 encoded text
    //! @param ndcX, ndcY Position in NDC (-1..1)
    //! @param scaleX, scaleY Scale from glyph pixels to NDC (typically 2/screenWidth, 2/screenHeight)
    //! @param color RGBA color (0..1 float)
    virtual void RenderBatched(const char* text, float ndcX, float ndcY,
                               float scaleX, float scaleY, const float color[4]) = 0;

    //! Get kerning adjustment between two glyphs
    //! @param left Left codepoint
    //! @param right Right codepoint
    //! @return Kerning adjustment in pixels
    virtual float GetKerning(unsigned int left, unsigned int right) = 0;
};

//! Font factory - creates appropriate font backend
class rFontFactory
{
public:
    //! Create a font instance using the current backend
    //! @param path Path to font file
    //! @param size Font size in pixels
    //! @return Font instance, or nullptr on failure
    static std::unique_ptr<rIFont> Create(tString const& path, int size);

    //! Set the font backend to use
    //! @param useSTB true for stb_truetype, false for FTGL (if available)
    static void SetBackend(bool useSTB);

    //! Check if stb_truetype backend is available
    static bool IsSTBAvailable();

    //! Check if FTGL backend is available
    static bool IsFTGLAvailable();
};

#endif // RFONTINTERFACE_H
