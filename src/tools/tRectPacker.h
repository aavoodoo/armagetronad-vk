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

Simple skyline-based rectangle packer for texture atlases

*/

#ifndef TRECTPACKER_H
#define TRECTPACKER_H

#include <vector>
#include <algorithm>

//! Simple skyline-based rectangle packer for texture atlases
class tRectPacker
{
public:
    //! Create a packer with specified atlas dimensions
    //! @param width Atlas width in pixels
    //! @param height Atlas height in pixels
    tRectPacker(int width, int height)
        : width_(width), height_(height)
    {
        Reset();
    }

    //! Reset the packer, clearing all allocations
    void Reset()
    {
        skyline_.clear();
        skyline_.push_back({0, 0, width_});
    }

    //! Try to pack a rectangle into the atlas
    //! @param rectWidth Width of rectangle to pack
    //! @param rectHeight Height of rectangle to pack
    //! @param outX Output X position
    //! @param outY Output Y position
    //! @return true if rectangle was successfully packed
    bool Pack(int rectWidth, int rectHeight, int& outX, int& outY)
    {
        // Add padding between glyphs.
        // The extra pixel beyond the bare minimum prevents bilinear filtering
        // from sampling into the adjacent glyph's data (especially noticeable
        // with SDF/MSDF where the distance field extends to the cell edge).
        int paddedWidth = rectWidth + 2;
        int paddedHeight = rectHeight + 2;

        // Find the best position using bottom-left heuristic
        int bestHeight = height_ + 1;
        int bestWidth = width_ + 1;
        int bestIndex = -1;

        for (size_t i = 0; i < skyline_.size(); ++i)
        {
            int y;
            if (RectangleFits(i, paddedWidth, paddedHeight, y))
            {
                if (y + paddedHeight < bestHeight ||
                    (y + paddedHeight == bestHeight && skyline_[i].width < bestWidth))
                {
                    bestHeight = y + paddedHeight;
                    bestWidth = skyline_[i].width;
                    bestIndex = static_cast<int>(i);
                    outX = skyline_[i].x;
                    outY = y;
                }
            }
        }

        if (bestIndex == -1)
        {
            return false; // Doesn't fit
        }

        // outX/outY are the top-left of the padded slot.
        // Inset by 1 so the caller writes glyph data inside the empty border.
        int slotX = outX;
        int slotY = outY;
        outX = slotX + 1;
        outY = slotY + 1;

        // Add new skyline node using the padded slot dimensions
        SkylineNode newNode;
        newNode.x = slotX;
        newNode.y = slotY + paddedHeight;
        newNode.width = paddedWidth;

        skyline_.insert(skyline_.begin() + bestIndex, newNode);

        // Shrink or remove nodes covered by the new node
        for (size_t i = bestIndex + 1; i < skyline_.size(); ++i)
        {
            if (skyline_[i].x < skyline_[i - 1].x + skyline_[i - 1].width)
            {
                int shrink = skyline_[i - 1].x + skyline_[i - 1].width - skyline_[i].x;
                skyline_[i].x += shrink;
                skyline_[i].width -= shrink;

                if (skyline_[i].width <= 0)
                {
                    skyline_.erase(skyline_.begin() + i);
                    --i;
                }
                else
                {
                    break;
                }
            }
            else
            {
                break;
            }
        }

        // Merge adjacent nodes with same height
        MergeSkyline();

        return true;
    }

    //! Get atlas width
    int GetWidth() const { return width_; }

    //! Get atlas height
    int GetHeight() const { return height_; }

private:
    struct SkylineNode
    {
        int x;      //!< X position of node
        int y;      //!< Y position (height) of node
        int width;  //!< Width of the node
    };

    //! Check if a rectangle fits at a given skyline index
    bool RectangleFits(size_t index, int width, int height, int& y) const
    {
        int x = skyline_[index].x;
        if (x + width > width_)
        {
            return false;
        }

        int widthLeft = width;
        size_t i = index;
        y = skyline_[index].y;

        while (widthLeft > 0)
        {
            y = std::max(y, skyline_[i].y);
            if (y + height > height_)
            {
                return false;
            }
            widthLeft -= skyline_[i].width;
            ++i;
            if (i >= skyline_.size())
            {
                return widthLeft <= 0;
            }
        }

        return true;
    }

    //! Merge adjacent skyline nodes with the same height
    void MergeSkyline()
    {
        for (size_t i = 0; i + 1 < skyline_.size(); ++i)
        {
            if (skyline_[i].y == skyline_[i + 1].y)
            {
                skyline_[i].width += skyline_[i + 1].width;
                skyline_.erase(skyline_.begin() + i + 1);
                --i;
            }
        }
    }

    int width_;
    int height_;
    std::vector<SkylineNode> skyline_;
};

#endif // TRECTPACKER_H
