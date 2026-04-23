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

// iOS-only Objective-C helper to retrieve the app's Documents directory.
// Called once at startup from tDirectories::InitiOS() (tDirectories.cpp).
// Using Documents instead of Library/Application Support means:
//   - Files are visible in the Files app ("On My iPhone → Armagetron")
//   - Users can drop moviepacks, autoexec.cfg, and custom maps via iTunes File Sharing.
// Requires Info.plist: UIFileSharingEnabled = true, LSSupportsOpeningDocumentsInPlace = true.

#import <Foundation/Foundation.h>
#include <limits.h>   // PATH_MAX
#include <string.h>   // strlcpy

extern "C"
const char* tGetIOSDocumentsPath(void)
{
    NSArray<NSString*>* paths =
        NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString* docDir = [paths firstObject];
    if (!docDir)
        return "";

    // Use a static buffer — this function is called exactly once at startup.
    static char buf[PATH_MAX];
    strlcpy(buf, [docDir UTF8String], sizeof(buf));
    return buf;
}
