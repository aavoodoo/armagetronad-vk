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

#ifndef RVULKANSHADER_H
#define RVULKANSHADER_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstdint>

//! Loads SPIR-V shader modules (and compiles GLSL to SPIR-V via libshaderc)
class rVulkanShader
{
public:
    enum class Stage { Vertex, Fragment, Compute };

    rVulkanShader() = default;
    ~rVulkanShader() = default;

    //! Load SPIR-V from a file
    [[nodiscard]] static VkShaderModule LoadFromFile(VkDevice device, const char* path);

    //! Load SPIR-V from memory
    [[nodiscard]] static VkShaderModule LoadFromMemory(VkDevice device, const uint32_t* code, size_t sizeBytes);

    //! Destroy a shader module
    static void Destroy(VkDevice device, VkShaderModule module);

    //! Read a binary file into a byte vector
    static std::vector<char> ReadFile(const char* path);

#ifdef HAVE_SHADERC_SHADERC_HPP
    //! Compile GLSL source to SPIR-V via libshaderc.
    //! `sourcePath` is used as the reported filename for errors and as the origin
    //! for #include resolution. `includePaths` lists directories searched for
    //! quoted includes (in order, first match wins). `defines` lists preprocessor
    //! macro definitions as (name, value) pairs — pass empty value for a bare
    //! #define. Returns empty vector on failure, with the error message written
    //! to `outError`.
    [[nodiscard]] static std::vector<uint32_t> CompileGLSL(
        const std::string& source,
        const std::string& sourcePath,
        Stage stage,
        const std::vector<std::string>& includePaths,
        const std::vector<std::pair<std::string, std::string>>& defines,
        std::string* outError);

    //! Convenience overload without defines.
    static std::vector<uint32_t> CompileGLSL(
        const std::string& source,
        const std::string& sourcePath,
        Stage stage,
        const std::vector<std::string>& includePaths,
        std::string* outError)
    {
        return CompileGLSL(source, sourcePath, stage, includePaths, {}, outError);
    }

    //! Compile GLSL from a file on disk (reads the file, then delegates to CompileGLSL).
    [[nodiscard]] static std::vector<uint32_t> CompileGLSLFromFile(
        const std::string& path,
        Stage stage,
        const std::vector<std::string>& includePaths,
        const std::vector<std::pair<std::string, std::string>>& defines,
        std::string* outError);

    //! Convenience overload without defines.
    static std::vector<uint32_t> CompileGLSLFromFile(
        const std::string& path,
        Stage stage,
        const std::vector<std::string>& includePaths,
        std::string* outError)
    {
        return CompileGLSLFromFile(path, stage, includePaths, {}, outError);
    }

    //! Create a VkShaderModule directly from GLSL source (compile + create).
    //! Convenience wrapper; returns VK_NULL_HANDLE on compilation failure.
    [[nodiscard]] static VkShaderModule CompileFromFile(
        VkDevice device,
        const std::string& path,
        Stage stage,
        const std::vector<std::string>& includePaths,
        std::string* outError);
#endif // HAVE_SHADERC_SHADERC_HPP
};

#endif // DEDICATED
#endif // RVULKANSHADER_H
