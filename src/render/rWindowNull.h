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

#ifndef ArmageTron_rWindowNull_H
#define ArmageTron_rWindowNull_H

#include "rWindow.h"

//! Null window implementation for dedicated server builds
//! All operations are no-ops since there's no display.
class rWindowNull : public rWindow
{
public:
    rWindowNull() = default;
    ~rWindowNull() override = default;

    bool Create(const tString&, int, int, int, int, rWindowFlags) override { return true; }
    void Destroy() override {}
    bool IsValid() const override { return true; }

    void SetTitle(const tString&) override {}
    void SetSize(int, int) override {}
    void GetSize(int& width, int& height) const override { width = 640; height = 480; }
    void GetDrawableSize(int& width, int& height) const override { width = 640; height = 480; }
    void SetPosition(int, int) override {}
    void GetPosition(int& x, int& y) const override { x = 0; y = 0; }

    bool SetFullscreen(bool, bool) override { return true; }
    bool IsFullscreen() const override { return false; }
    bool SetDisplayMode(const rDisplayMode&) override { return true; }
    rDisplayMode GetDisplayMode() const override { return {640, 480, 60, 0}; }
    rDisplayMode GetDesktopDisplayMode(int) const override { return {640, 480, 60, 0}; }

    int GetNumDisplays() const override { return 1; }
    int GetDisplayIndex() const override { return 0; }

    void SwapBuffers() override {}
    void Minimize() override {}
    void Restore() override {}
    void Raise() override {}

    void Lock() override {}
    void Unlock() override {}
};

#endif // ArmageTron_rWindowNull_H
