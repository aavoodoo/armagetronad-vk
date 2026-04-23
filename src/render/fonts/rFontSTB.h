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

stb_truetype based font implementation

*/

#ifndef RFONTSTB_H
#define RFONTSTB_H

#include "rFontInterface.h"
#include "rFontAtlas.h"
#include "tRectPacker.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <cstdint>
#include <mutex>
#include <atomic>

//! Font rendering mode
enum class FontMode
{
    LEGACY,  //!< Traditional rasterized glyphs
    SDF,     //!< Single-channel signed distance field
    MSDF,    //!< Multi-channel signed distance field
    MTSDF    //!< Multi-channel + true SDF (with outline support)
};

//! stb_truetype based font implementation
class rFontSTB : public rIFont
{
public:
    rFontSTB();
    ~rFontSTB() override;

    // rIFont interface
    bool Load(const char* path, int size) override;
    bool IsValid() const override;
    float GetLineHeight() const override;
    float GetAscender() const override;
    float GetDescender() const override;
    bool GetGlyphMetrics(unsigned int codepoint, rGlyphMetrics& metrics) override;
    float GetTextWidth(const char* text) override;
    void GetTextBounds(const char* text, rTextBounds& bounds) override;
    void RenderBatched(const char* text, float ndcX, float ndcY,
                       float scaleX, float scaleY, const float color[4]) override;
    float GetKerning(unsigned int left, unsigned int right) override;

    //! Get the texture atlas ID for rendering (returns active texture based on mode)
    unsigned int GetTextureId() const;

    //! Get current font mode
    FontMode GetFontMode() const { return mode_; }

private:
    //! Glyph loading state
    enum class GlyphState
    {
        NOT_LOADED,  //!< Not in cache
        LOADING,     //!< Being generated in background
        LOADED       //!< Ready to use
    };

    //! Cached glyph data
    struct CachedGlyph
    {
        rGlyphMetrics metrics;
        GlyphState state = GlyphState::NOT_LOADED;
    };

    //! Pending MSDF result from background thread
    struct PendingGlyph
    {
        unsigned int codepoint;
        rGlyphMetrics metrics;
        std::vector<unsigned char> bitmapData;
        int bitmapWidth;
        int bitmapHeight;
        int channels;  // 1 for SDF, 3 for MSDF, 4 for MTSDF
    };

    //! Load a glyph into the cache (sync for legacy, queues async for MSDF)
    bool LoadGlyph(unsigned int codepoint);

    //! Process completed async glyph uploads (call from main thread)
    void ProcessPendingGlyphs();

    //! Ensure atlas has enough space, grow if needed
    bool EnsureAtlasSpace(int width, int height);

    //! Create or resize the texture atlas
    void CreateAtlas(int width, int height);

    //! Update a region of the atlas texture
    void UpdateAtlasRegion(int x, int y, int width, int height, const unsigned char* data);

    //! Decode UTF-8 codepoint from string
    //! @param text Input string pointer, advanced past the decoded codepoint
    //! @return Unicode codepoint, or 0 on error
    static unsigned int DecodeUTF8(const char*& text);

    // Font data
    std::vector<unsigned char> fontData_;
    void* fontInfo_;  // stbtt_fontinfo*
    float scale_;
    int ascent_;
    int descent_;
    int lineGap_;
    int fontSize_;
    bool valid_;
    bool initializing_ = true;  //!< True during initial ASCII preload (sync mode)

    // Texture atlas
    unsigned int textureId_;
    int atlasWidth_;
    int atlasHeight_;
    std::unique_ptr<tRectPacker> packer_;
    std::vector<unsigned char> atlasData_;
    bool atlasDirty_ = false;  //!< Set when CPU atlas data is updated, cleared after GPU re-upload

    // Glyph cache
    std::unordered_map<unsigned int, CachedGlyph> glyphCache_;
    mutable std::mutex glyphCacheMutex_;  //!< Protects glyphCache_ for async access

    // Pending glyphs from background generation (ready for texture upload)
    std::vector<PendingGlyph> pendingGlyphs_;
    mutable std::mutex pendingGlyphsMutex_;

    // Glyphs currently being generated (to avoid duplicate work)
    std::unordered_set<unsigned int> glyphsInFlight_;
    mutable std::mutex inFlightMutex_;

    // Kerning cache (key = (left << 32) | right for full Unicode support)
    std::unordered_map<uint64_t, float> kerningCache_;

    // MSDF support
    FontMode mode_ = FontMode::LEGACY;

    //! Atlas variants (only one active based on mode_)
    std::unique_ptr<rFontAtlasSDF> atlasSDF_;      //!< glm::u8vec1 for SDF
    std::unique_ptr<rFontAtlasMSDF> atlasMSDF_;    //!< glm::u8vec3 for MSDF
    std::unique_ptr<rFontAtlasMTSDF> atlasMTSDF_;  //!< glm::u8vec4 for MTSDF

    //! MSDF glyph size (pixels in atlas per glyph)
    int msdfGlyphSize_ = 48;

    //! MSDF range (SDF distance range in pixels)
    float msdfRange_ = 4.0f;

    //! Generate MSDF glyph and upload to atlas
    bool LoadMSDFGlyph(unsigned int codepoint);

    //! Get active texture ID based on current mode
    unsigned int GetActiveTextureId() const;

    //! Compute screen pixel range for current font size
    float ComputeScreenPxRange() const;
};

#endif // RFONTSTB_H
