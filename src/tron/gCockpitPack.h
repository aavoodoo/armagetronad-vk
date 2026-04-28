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

#ifndef ArmageTron_COCKPITPACK_H
#define ArmageTron_COCKPITPACK_H

#include "tString.h"
#include "tArray.h"
#include "uMenu.h"
#include <vector>

#ifndef DEDICATED
#include "rTexture.h"
#endif

//! Information about a single cockpit pack
struct gCockpitPack
{
    tString name;      //!< Display name (derived from filename)
    tString path;      //!< Full path to .aacockpit.zip file
    tString cockpitFile; //!< Cockpit XML filename inside the zip (e.g., "cockpit.aacockpit.xml")

    gCockpitPack() {}
    gCockpitPack(const tString& n, const tString& p)
        : name(n), path(p) {}
};

//! Manager for cockpit pack selection and activation
class gCockpitPackManager
{
public:
    static gCockpitPackManager& Get();

    //! Scan for available cockpit packs in data directories
    void ScanPacks();

    int GetCount() const { return packs_.Len(); }
    const gCockpitPack* GetPack(int index) const;
    int GetActiveIndex() const { return activeIndex_; }

    //! Set the active cockpit pack by index
    void SetActiveIndex(int index);

    //! Get the active pack name (for config persistence)
    const tString& GetActivePackName() const;

    //! Restore selection from saved name
    void RestoreFromName(const tString& name);

#ifndef DEDICATED
    rITexture* GetPreviewTexture(int index);
#endif

    //! Public wrapper for ZIP extraction (used by sr_EnsureCockpitPackExtracted)
    bool ExtractFileFromZipPublic(const tString& zipPath, const char* fileName,
                                   void** outData, size_t* outSize)
    { return ExtractFileFromZip(zipPath, fileName, outData, outSize); }

private:
    gCockpitPackManager();
    ~gCockpitPackManager();
    gCockpitPackManager(const gCockpitPackManager&);
    gCockpitPackManager& operator=(const gCockpitPackManager&);

    bool ExtractFileFromZip(const tString& zipPath, const char* fileName,
                            void** outData, size_t* outSize);

#ifndef DEDICATED
    rITexture* LoadTextureFromZip(const tString& zipPath, const char* filename,
                                   tArray<rITexture*>& cache, int index);
#endif

    bool ExtractZipToDirectory(const tString& zipPath, const tString& destDir);
    tString GetTempExtractPath() const;

    tArray<gCockpitPack*> packs_;  //!< Available packs (index 0 = "Default")
    int activeIndex_;

#ifndef DEDICATED
    tArray<rITexture*> previewTextures_;
#endif
};

#ifndef DEDICATED
//! Menu item for cockpit pack selection with preview
class gCockpitPackMenuItem : public uMenuItemSelection<int>
{
public:
    gCockpitPackMenuItem(uMenu* menu);
    virtual ~gCockpitPackMenuItem();
    virtual void RenderBackground();
    virtual REAL SpaceRight() { return 0.25; }
    virtual void LeftRight(int lr);
    virtual void LeftRightRelease();
    virtual void Enter();

private:
    int selectionIndex_;
    void UpdateFromManager();
};
#endif

#endif // ArmageTron_COCKPITPACK_H
