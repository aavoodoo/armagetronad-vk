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

#include "aa_config.h"
#include "gCockpitPack.h"
#include "tDirectories.h"
#include "tConfiguration.h"
#include "tConsole.h"

#ifndef DEDICATED
#include "rRenderBucket.h"
#include "rRenderQueue.h"
#include "rRawPixelTexture.h"
#include "rFont.h"
#include "rScreen.h"
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

// miniz for ZIP extraction — header only (symbols compiled via gMoviepack.cpp)
#include "../thirdparty/miniz/miniz.h"

#ifndef DEDICATED
// stb_image is already included elsewhere, just declare the functions we need
extern "C" {
    unsigned char* stbi_load_from_memory(unsigned char const* buffer, int len,
                                          int* x, int* y, int* comp, int req_comp);
    void stbi_image_free(void* retval_from_stbi_load);
}
using rPreviewTexture = rRawPixelTexture;
#endif

// Config: saved cockpit pack name for persistence across sessions
static tString sg_cockpitPackName("");
static tConfItem<tString> sg_cockpitPackNameConf("COCKPIT_PACK", sg_cockpitPackName);

// Forward: the COCKPIT_FILE setting from cCockpit.cpp
extern tString cockpit_file;

// =============================================================================
// gCockpitPackManager
// =============================================================================

gCockpitPackManager::gCockpitPackManager()
    : activeIndex_(0)
{
    // Index 0 = "Default" (uses COCKPIT_FILE as-is)
    gCockpitPack* defaultPack = new gCockpitPack();
    defaultPack->name = "Default";
    packs_.push_back(defaultPack);
}

gCockpitPackManager::~gCockpitPackManager()
{
    for (int i = 0; i < packs_.Len(); ++i)
        delete packs_(i);
#ifndef DEDICATED
    for (int i = 0; i < previewTextures_.Len(); ++i)
        delete previewTextures_(i);
#endif
}

gCockpitPackManager& gCockpitPackManager::Get()
{
    static gCockpitPackManager instance;
    return instance;
}

const gCockpitPack* gCockpitPackManager::GetPack(int index) const
{
    if (index >= 0 && index < packs_.Len())
        return packs_(index);
    return nullptr;
}

const tString& gCockpitPackManager::GetActivePackName() const
{
    static tString empty;
    if (activeIndex_ > 0 && activeIndex_ < packs_.Len())
        return packs_(activeIndex_)->name;
    return empty;
}

void gCockpitPackManager::RestoreFromName(const tString& name)
{
    for (int i = 0; i < packs_.Len(); ++i)
    {
        if (packs_(i)->name == name)
        {
            // Don't set activeIndex_ here — SetActiveIndex checks
            // index != activeIndex_ as an early-return guard.
            if (i > 0)
                SetActiveIndex(i);
            return;
        }
    }
}

tString gCockpitPackManager::GetTempExtractPath() const
{
    return tDirectories::GetUserData() + "/cockpit";
}

// =============================================================================
// ZIP helpers (same pattern as gMoviepack.cpp)
// =============================================================================

bool gCockpitPackManager::ExtractFileFromZip(const tString& zipPath, const char* fileName,
                                              void** outData, size_t* outSize)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, static_cast<const char*>(zipPath), 0))
        return false;

    int fileIndex = mz_zip_reader_locate_file(&zip, fileName, nullptr, 0);
    if (fileIndex < 0)
    {
        mz_zip_reader_end(&zip);
        return false;
    }

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&zip, fileIndex, &stat))
    {
        mz_zip_reader_end(&zip);
        return false;
    }

    *outSize = static_cast<size_t>(stat.m_uncomp_size);
    *outData = malloc(*outSize);
    if (!*outData)
    {
        mz_zip_reader_end(&zip);
        return false;
    }

    if (!mz_zip_reader_extract_to_mem(&zip, fileIndex, *outData, *outSize, 0))
    {
        free(*outData);
        *outData = nullptr;
        mz_zip_reader_end(&zip);
        return false;
    }

    mz_zip_reader_end(&zip);
    return true;
}

bool gCockpitPackManager::ExtractZipToDirectory(const tString& zipPath, const tString& destDir)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, static_cast<const char*>(zipPath), 0))
        return false;

    int numFiles = mz_zip_reader_get_num_files(&zip);
    for (int i = 0; i < numFiles; i++)
    {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;

        tString fullPath = destDir;
        fullPath += "/";
        fullPath += stat.m_filename;

        if (stat.m_is_directory)
        {
            mkdir(static_cast<const char*>(fullPath), 0755);
            continue;
        }

        // Create parent directories
        tString dir = fullPath;
        for (int c = dir.Len() - 2; c >= 0; --c)
        {
            if (dir[c] == '/')
            {
                tString parent = dir.SubStr(0, c);
                mkdir(static_cast<const char*>(parent), 0755);
            }
        }

        mz_zip_reader_extract_to_file(&zip, i, static_cast<const char*>(fullPath), 0);
    }

    mz_zip_reader_end(&zip);
    return true;
}

// =============================================================================
// Scanning
// =============================================================================

void gCockpitPackManager::ScanPacks()
{
    // Clear existing packs (keep index 0 = "Default")
    for (int i = 1; i < packs_.Len(); ++i)
        delete packs_(i);
    if (packs_.Len() > 1)
        packs_.SetLen(1);

#ifndef DEDICATED
    for (int i = 0; i < previewTextures_.Len(); ++i)
        delete previewTextures_(i);
    previewTextures_.SetLen(0);
#endif

    // Scan for .aacockpit.zip files in cockpits/ subdirectory of all data paths
    tArray<tString> paths;
    tDirectories::Data().GetPaths(paths);

    SDL_Log("[CockpitPack] Scanning %d data paths", paths.Len());
    for (int p = 0; p < paths.Len(); ++p)
    {
        tString cockpitsDir = paths(p);
        cockpitsDir += "/cockpits";

        tArray<tString> files;
        tDirectories::GetFiles(cockpitsDir, tString("*.aacockpit.zip"), files,
                               tDirectories::eGetFilesFilesOnly);
        SDL_Log("[CockpitPack]   %s -> %d files", static_cast<const char*>(cockpitsDir), files.Len());

        for (int f = 0; f < files.Len(); ++f)
        {
            tString filename = files(f);
            tString fullPath = cockpitsDir;
            fullPath += "/";
            fullPath += filename;

            // Extract display name (remove .aacockpit.zip)
            tString displayName = filename;
            int extPos = displayName.StrPos(".aacockpit.zip");
            if (extPos > 0)
                displayName = displayName.SubStr(0, extPos);

            // Replace underscores with spaces
            for (size_t i = 0; i < displayName.Size(); ++i)
                if (displayName[i] == '_') displayName[i] = ' ';

            // Skip duplicates
            bool exists = false;
            for (int m = 0; m < packs_.Len(); ++m)
                if (packs_(m)->name == displayName) { exists = true; break; }

            if (!exists)
            {
                gCockpitPack* pack = new gCockpitPack(displayName, fullPath);

                // Find the cockpit XML filename inside the zip
                mz_zip_archive zip;
                memset(&zip, 0, sizeof(zip));
                if (mz_zip_reader_init_file(&zip, static_cast<const char*>(fullPath), 0))
                {
                    int numFiles = mz_zip_reader_get_num_files(&zip);
                    for (int z = 0; z < numFiles; z++)
                    {
                        mz_zip_archive_file_stat stat;
                        if (mz_zip_reader_file_stat(&zip, z, &stat))
                        {
                            tString fname(stat.m_filename);
                            if (fname.StrPos(".aacockpit.xml") >= 0)
                            {
                                pack->cockpitFile = fname;
                                break;
                            }
                        }
                    }
                    mz_zip_reader_end(&zip);
                }

                packs_.push_back(pack);
            }
        }
    }

    // Sort alphabetically (skip index 0)
    for (int i = 1; i < packs_.Len() - 1; ++i)
        for (int j = i + 1; j < packs_.Len(); ++j)
            if (strcmp(static_cast<const char*>(packs_(j)->name),
                       static_cast<const char*>(packs_(i)->name)) < 0)
            {
                gCockpitPack* tmp = packs_(i);
                packs_(i) = packs_(j);
                packs_(j) = tmp;
            }

    SDL_Log("[CockpitPack] Scan complete: %d packs total", packs_.Len());
    for (int i = 0; i < packs_.Len(); ++i)
        SDL_Log("[CockpitPack]   [%d] %s", i, static_cast<const char*>(packs_(i)->name));

    // Restore from saved name
    if (sg_cockpitPackName.Len() > 1)
        RestoreFromName(sg_cockpitPackName);

#ifndef DEDICATED
    previewTextures_.SetLen(packs_.Len());
    for (int i = 0; i < previewTextures_.Len(); ++i)
        previewTextures_(i) = nullptr;
#endif
}

// =============================================================================
// Activation
// =============================================================================

void gCockpitPackManager::SetActiveIndex(int index)
{
    SDL_Log("[CockpitPack] SetActiveIndex(%d) current=%d total=%d", index, activeIndex_, packs_.Len());
    if (index < 0 || index >= packs_.Len() || index == activeIndex_)
        return;

    activeIndex_ = index;

    if (index == 0)
    {
        sg_cockpitPackName = "";
        // Set via config system so the change callback fires
        tCurrentAccessLevel elevate(tAccessLevel_Owner, true);
        std::ostringstream s;
        s << "COCKPIT_FILE Anonymous/standard-0.0.1.aacockpit.xml";
        std::istringstream is(s.str());
        tConfItemBase::LoadAll(is, false);
    }
    else
    {
        const gCockpitPack* pack = packs_(index);

        // Parse the cockpit XML from the ZIP to get resource metadata
        // (author, category, name, version) needed for the resource path.
        tString resourcePath;
        {
            void* xmlData = nullptr;
            size_t xmlSize = 0;
            if (pack->cockpitFile.Len() > 1 &&
                ExtractFileFromZip(pack->path, static_cast<const char*>(pack->cockpitFile), &xmlData, &xmlSize))
            {
                // Quick parse: find author, category, name, version from the Resource element
                std::string xml(static_cast<const char*>(xmlData), xmlSize);
                free(xmlData);

                // Search only within the <Resource ...> tag to avoid matching
                // attributes from the <?xml version="1.0"?> declaration.
                auto resPos = xml.find("<Resource");
                std::string resTag = (resPos != std::string::npos)
                    ? xml.substr(resPos, xml.find('>', resPos) - resPos)
                    : xml;

                auto getAttr = [&](const std::string& attr) -> std::string {
                    std::string key = attr + "=\"";
                    auto pos = resTag.find(key);
                    if (pos == std::string::npos) return "";
                    pos += key.size();
                    auto end = resTag.find('"', pos);
                    if (end == std::string::npos) return "";
                    return resTag.substr(pos, end - pos);
                };

                std::string author   = getAttr("author");
                std::string category = getAttr("category");
                std::string name     = getAttr("name");
                std::string version  = getAttr("version");

                if (!author.empty() && !name.empty() && !version.empty())
                {
                    // Build resource path: author/[category/]name-version.aacockpit.xml
                    std::string rp = author;
                    if (!category.empty()) { rp += "/"; rp += category; }
                    rp += "/";
                    rp += name;
                    rp += "-";
                    rp += version;
                    rp += ".aacockpit.xml";
                    resourcePath = rp.c_str();

                    // Extract to the writable resource directory. On iOS the app bundle
                    // is read-only, so we must use GetWritePath (resource/automatic/).
                    // The DTD resolves via myxmlParserInputBufferCreateFilenameFunc which
                    // uses tResourceManager::openResource — searches all resource paths.
                    SDL_Log("[CockpitPack] resourcePath=%s", static_cast<const char*>(resourcePath));
                    tString resDir;
                    {
                        tString writePath = tDirectories::Resource().GetWritePath(resourcePath);
                        SDL_Log("[CockpitPack] writePath=%s (len=%d)", static_cast<const char*>(writePath), writePath.Len());
                        if (writePath.Len() > 1)
                        {
                            // Strip filename from write path to get directory
                            int lastSlash = -1;
                            for (int c = writePath.Len() - 2; c >= 0; --c)
                                if (writePath[c] == '/') { lastSlash = c; break; }
                            if (lastSlash > 0) resDir = writePath.SubStr(0, lastSlash);
                        }
                        if (resDir.Len() <= 1)
                        {
                            // Fallback: first writable resource path
                            tArray<tString> resPaths;
                            tDirectories::Resource().GetPaths(resPaths);
                            if (resPaths.Len() > 0)
                                resDir = resPaths(0);
                            resDir += "/";
                            resDir += author.c_str();
                            if (!category.empty()) { resDir += "/"; resDir += category.c_str(); }
                        }
                        SDL_Log("[CockpitPack] resDir=%s", static_cast<const char*>(resDir));
                    }

                    if (resDir.Len() > 1)
                    {
                        // Create directory and extract all files from ZIP
                        mkdir(static_cast<const char*>(resDir), 0755);
                        ExtractZipToDirectory(pack->path, resDir);

                        // Rename the cockpit XML to match the resource convention
                        // (e.g., cockpit.aacockpit.xml → touch-0.1.aacockpit.xml)
                        std::string destName = name + "-" + version + ".aacockpit.xml";
                        tString srcFile = resDir;
                        srcFile += "/";
                        srcFile += pack->cockpitFile;
                        tString destFile = resDir;
                        destFile += "/";
                        destFile += destName.c_str();

                        // If source != dest (they differ when ZIP uses generic name)
                        SDL_Log("[CockpitPack] srcFile=%s", static_cast<const char*>(srcFile));
                        SDL_Log("[CockpitPack] destFile=%s", static_cast<const char*>(destFile));
                        {
                            struct stat st;
                            SDL_Log("[CockpitPack] srcFile exists=%d", stat(static_cast<const char*>(srcFile), &st) == 0);
                        }
                        if (srcFile != destFile)
                        {
                            // Remove destination if it exists (from previous activation)
                            unlink(static_cast<const char*>(destFile));
                            int rv = rename(static_cast<const char*>(srcFile),
                                            static_cast<const char*>(destFile));
                            SDL_Log("[CockpitPack] rename rv=%d errno=%d", rv, rv != 0 ? errno : 0);
                            if (rv != 0)
                            {
                                // rename failed — try copy instead (cross-device)
                                std::ifstream src(static_cast<const char*>(srcFile), std::ios::binary);
                                std::ofstream dst(static_cast<const char*>(destFile), std::ios::binary);
                                SDL_Log("[CockpitPack] copy fallback: src=%d dst=%d", (bool)src, (bool)dst);
                                if (src && dst)
                                {
                                    dst << src.rdbuf();
                                    unlink(static_cast<const char*>(srcFile));
                                }
                            }
                        }

                        // Verify extraction worked
                        struct stat st;
                        bool exists = (stat(static_cast<const char*>(destFile), &st) == 0);
                        con << "[Cockpit] resDir=" << resDir << "\n";
                        con << "[Cockpit] destFile=" << destFile << " exists=" << exists << "\n";
                        con << "[Cockpit] resourcePath=" << resourcePath << "\n";
                        // Also log what tDirectories::Resource sees
                        tString readPath = tDirectories::Resource().GetReadPath(resourcePath);
                        con << "[Cockpit] GetReadPath=" << readPath << "\n";
                    }
                }
            }
        }

        // Don't set cockpit_file directly — let tConfItemBase::LoadAll
        // handle it so the change-detection callback (parsecockpit) fires.
        tString newFile;
        if (resourcePath.Len() > 1)
            newFile = resourcePath;
        else if (pack->cockpitFile.Len() > 1)
            newFile = pack->cockpitFile;
        else
            newFile = cockpit_file;

        sg_cockpitPackName = pack->name;

        // Trigger cockpit reload via config system
        {
            tCurrentAccessLevel elevate(tAccessLevel_Owner, true);
            std::ostringstream s;
            s << "COCKPIT_FILE " << static_cast<const char*>(newFile);
            std::istringstream is(s.str());
            tConfItemBase::LoadAll(is, false);
        }
    }

    con << "[Cockpit] Activated: " << (index > 0 ? packs_(index)->name : tString("Default"))
        << " (" << cockpit_file << ")\n";
}

// =============================================================================
// Ensure extraction on config load
// =============================================================================

void sr_EnsureCockpitPackExtracted()
{
    // If cockpit_file is the default, nothing to extract
    std::string cf(static_cast<const char*>(cockpit_file));
    if (cf.empty() || cf == "Anonymous/standard-0.0.1.aacockpit.xml")
        return;

    // Check if the file already exists in the resource system
    tString readPath = tDirectories::Resource().GetReadPath(cockpit_file);
    if (readPath.Len() > 1)
        return; // file exists, no extraction needed

    con << "[Cockpit] File not found: " << cockpit_file << ", scanning for matching pack...\n";

    // File not found — try to extract from the matching cockpit pack ZIP.
    // Scan if not already done.
    gCockpitPackManager& mgr = gCockpitPackManager::Get();
    if (mgr.GetCount() <= 1)
        mgr.ScanPacks();

    con << "[Cockpit] Found " << mgr.GetCount() << " packs\n";

    // Try each pack: extract its XML metadata, build the resource path,
    // and check if it matches the current cockpit_file.
    for (int i = 1; i < mgr.GetCount(); i++)
    {
        const gCockpitPack* pack = mgr.GetPack(i);
        if (!pack || pack->path.Len() <= 1) continue;

        // Match by saved name if available
        if (sg_cockpitPackName.Len() > 1 && pack->name == sg_cockpitPackName)
        {
            con << "[Cockpit] Extracting pack '" << pack->name << "' (name match)\n";
            mgr.SetActiveIndex(i);
            return;
        }

        // Match by cockpit_file content: extract XML header from ZIP and check resource path
        if (pack->cockpitFile.Len() > 1)
        {
            void* xmlData = nullptr;
            size_t xmlSize = 0;
            if (mgr.ExtractFileFromZipPublic(pack->path, static_cast<const char*>(pack->cockpitFile), &xmlData, &xmlSize))
            {
                std::string xml(static_cast<const char*>(xmlData), xmlSize);
                free(xmlData);

                auto resPos = xml.find("<Resource");
                if (resPos != std::string::npos)
                {
                    std::string resTag = xml.substr(resPos, xml.find('>', resPos) - resPos);
                    auto getA = [&](const std::string& a) -> std::string {
                        std::string k = a + "=\"";
                        auto p = resTag.find(k);
                        if (p == std::string::npos) return "";
                        p += k.size();
                        auto e = resTag.find('"', p);
                        return (e != std::string::npos) ? resTag.substr(p, e - p) : "";
                    };
                    std::string rp = getA("author");
                    std::string cat = getA("category");
                    if (!cat.empty()) { rp += "/"; rp += cat; }
                    rp += "/"; rp += getA("name"); rp += "-"; rp += getA("version");
                    rp += ".aacockpit.xml";

                    if (rp == cf)
                    {
                        con << "[Cockpit] Extracting pack '" << pack->name << "' (path match)\n";
                        mgr.SetActiveIndex(i);
                        return;
                    }
                }
            }
        }
    }

    con << "[Cockpit] No matching pack found for: " << cockpit_file << "\n";
}

// =============================================================================
// Preview textures
// =============================================================================

#ifndef DEDICATED

rITexture* gCockpitPackManager::LoadTextureFromZip(const tString& zipPath, const char* filename,
                                                     tArray<rITexture*>& cache, int index)
{
    void* data = nullptr;
    size_t dataSize = 0;
    if (!ExtractFileFromZip(zipPath, filename, &data, &dataSize))
        return nullptr;

    int w, h, channels;
    unsigned char* pixels = stbi_load_from_memory(
        static_cast<const unsigned char*>(data), static_cast<int>(dataSize),
        &w, &h, &channels, 4);
    free(data);

    if (!pixels)
        return nullptr;

    rPreviewTexture* tex = new rPreviewTexture();
    if (tex->LoadFromPixels(pixels, w, h))
    {
        while (cache.Len() <= index)
            cache.push_back(nullptr);
        cache(index) = tex;
        stbi_image_free(pixels);
        return tex;
    }
    delete tex;
    stbi_image_free(pixels);
    return nullptr;
}

rITexture* gCockpitPackManager::GetPreviewTexture(int index)
{
    if (index <= 0 || index >= packs_.Len())
        return nullptr;

    if (index < previewTextures_.Len() && previewTextures_(index))
        return previewTextures_(index);

    const gCockpitPack* pack = packs_(index);
    // Try preview.png first, then preview.jpg
    rITexture* tex = LoadTextureFromZip(pack->path, "preview.png", previewTextures_, index);
    if (!tex)
        tex = LoadTextureFromZip(pack->path, "preview.jpg", previewTextures_, index);
    return tex;
}

// =============================================================================
// Menu item
// =============================================================================

static void sg_RenderQuad(rITexture* tex, float left, float top, float right, float bottom)
{
    if (!tex) return;
    tex->Select();
    unsigned int textureId = RenderGetBoundTexture2D();
    if (textureId == 0) return;

    rVertex20 v0(left,  top,    0, 255,255,255,255, 0.0f, 0.0f);
    rVertex20 v1(right, top,    0, 255,255,255,255, 1.0f, 0.0f);
    rVertex20 v2(right, bottom, 0, 255,255,255,255, 1.0f, 1.0f);
    rVertex20 v3(left,  bottom, 0, 255,255,255,255, 0.0f, 1.0f);
    rRenderStateKey state = rRenderStateKey::HUD(textureId, rBlendMode::Alpha);
    rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, state, v0, v1, v2, v3);
}

gCockpitPackMenuItem::gCockpitPackMenuItem(uMenu* menu)
    : uMenuItemSelection<int>(menu, "$cockpit_pack_text", "$cockpit_pack_help", selectionIndex_)
    , selectionIndex_(0)
{
    gCockpitPackManager& mgr = gCockpitPackManager::Get();
    if (mgr.GetCount() <= 1)
        mgr.ScanPacks();

    for (int i = 0; i < mgr.GetCount(); ++i)
    {
        const gCockpitPack* pack = mgr.GetPack(i);
        if (pack)
            NewChoice(tOutput(static_cast<const char*>(pack->name)), tOutput(""), i);
    }

    UpdateFromManager();
}

gCockpitPackMenuItem::~gCockpitPackMenuItem() {}

void gCockpitPackMenuItem::UpdateFromManager()
{
    selectionIndex_ = gCockpitPackManager::Get().GetActiveIndex();
}

void gCockpitPackMenuItem::RenderBackground()
{
    uMenuItem::RenderBackground();

    gCockpitPackManager& mgr = gCockpitPackManager::Get();
    rITexture* preview = mgr.GetPreviewTexture(selectionIndex_);
    if (!preview) return;

    // Display preview on right side (same layout as moviepack)
    float aspect = static_cast<float>(sr_screenWidth) / sr_screenHeight;
    float R = 1.0f;
    float L = R - 0.5f;
    float imgH = (R - L) / aspect;
    float y = -0.95f;
    sg_RenderQuad(preview, L, y + imgH, R, y);
}

void gCockpitPackMenuItem::LeftRight(int lr)
{
    uMenuItemSelection<int>::LeftRight(lr);
    // Browse only — preview displayed, activation on Enter
}

void gCockpitPackMenuItem::LeftRightRelease()
{
    // Browse only — no activation on scroll (same pattern as moviepack)
}

void gCockpitPackMenuItem::Enter()
{
    gCockpitPackManager::Get().SetActiveIndex(selectionIndex_);
}

#endif // DEDICATED
