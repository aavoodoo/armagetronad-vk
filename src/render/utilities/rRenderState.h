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

#ifndef ArmageTron_RENDER_STATE_H
#define ArmageTron_RENDER_STATE_H

#include "defs.h"
#include <unordered_map>

//! Render state cache to avoid redundant GL state changes.
//! This is a performance optimization - the renderer works correctly
//! without the cache, but may make redundant GL calls.
//!
//! Thread Safety: This class is NOT thread-safe. It is designed to be
//! accessed only from the main rendering thread. All GL state changes
//! must occur on the thread that owns the GL context.
class rRenderState
{
public:
    //! Get singleton instance
    static rRenderState& Get();

    //! Check if capability is currently enabled (returns cached value)
    //! Returns -1 if unknown (not cached)
    int IsEnabled(int capability) const;

    //! Set enabled state in cache and return true if state changed
    bool SetEnabled(int capability, bool enabled);

    //! Get cached blend function parameters
    //! Returns false if not cached
    bool GetBlendFunc(int& sfactor, int& dfactor) const;

    //! Set blend function in cache and return true if state changed
    bool SetBlendFunc(int sfactor, int dfactor);

    //! Get cached depth function
    //! Returns -1 if not cached
    int GetDepthFunc() const;

    //! Set depth function in cache and return true if state changed
    bool SetDepthFunc(int func);

    //! Get cached depth mask
    //! Returns -1 if not cached
    int GetDepthMask() const;

    //! Set depth mask in cache and return true if state changed
    bool SetDepthMask(bool write);

    //! Get cached cull face mode
    //! Returns -1 if not cached
    int GetCullFace() const;

    //! Set cull face in cache and return true if state changed
    bool SetCullFace(int face);

    //! Get cached front face mode
    //! Returns -1 if not cached
    int GetFrontFace() const;

    //! Set front face in cache and return true if state changed
    bool SetFrontFace(int mode);

    //! Get cached line width
    //! Returns -1 if not cached
    REAL GetLineWidth() const;

    //! Set line width in cache and return true if state changed
    bool SetLineWidth(REAL width);

    //! Get cached clear color
    bool GetClearColor(REAL& r, REAL& g, REAL& b, REAL& a) const;

    //! Set clear color in cache and return true if state changed
    bool SetClearColor(REAL r, REAL g, REAL b, REAL a);

    //! Invalidate all cached state (call on context loss/recreation)
    void Invalidate();

    //! Reset statistics
    void ResetStats();

    //! Get number of state changes avoided by cache
    int GetCacheHits() const { return cacheHits_; }

    //! Get number of actual state changes made
    int GetCacheMisses() const { return cacheMisses_; }

private:
    rRenderState();
    ~rRenderState() = default;

    // Cached capability states: -1 = unknown, 0 = disabled, 1 = enabled
    std::unordered_map<int, int> enabledCaps_;

    // Blend function
    int blendSrc_ = -1;
    int blendDst_ = -1;

    // Depth state
    int depthFunc_ = -1;
    int depthMask_ = -1;

    // Culling
    int cullFace_ = -1;
    int frontFace_ = -1;

    // Line width
    REAL lineWidth_ = -1;

    // Clear color
    bool clearColorValid_ = false;
    REAL clearR_ = 0, clearG_ = 0, clearB_ = 0, clearA_ = 0;

    // Statistics
    mutable int cacheHits_ = 0;
    mutable int cacheMisses_ = 0;
};

#endif // ArmageTron_RENDER_STATE_H
