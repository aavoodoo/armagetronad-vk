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

#ifndef DEDICATED

#include "rVulkanShader.h"
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#endif
#ifdef HAVE_SHADERC_SHADERC_HPP
#include <shaderc/shaderc.hpp>
#endif

VkShaderModule rVulkanShader::LoadFromFile(VkDevice device, const char* path)
{
    auto code = ReadFile(path);
    if (code.empty())
    {
        std::cerr << "[Vulkan] Failed to read shader: " << path << std::endl;
        return VK_NULL_HANDLE;
    }

    return LoadFromMemory(device, reinterpret_cast<const uint32_t*>(code.data()), code.size());
}

VkShaderModule rVulkanShader::LoadFromMemory(VkDevice device, const uint32_t* code, size_t sizeBytes)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = sizeBytes;
    createInfo.pCode = code;

    VkShaderModule module;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create shader module" << std::endl;
        return VK_NULL_HANDLE;
    }

    return module;
}

void rVulkanShader::Destroy(VkDevice device, VkShaderModule module)
{
    if (module != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, module, nullptr);
}

std::vector<char> rVulkanShader::ReadFile(const char* path)
{
#ifdef __ANDROID__
    // On Android, SDL_IOFromFile reads from APK assets when given a relative
    // path (SDL3 routes relative reads through AAssetManager transparently).
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) return {};
    Sint64 sz = SDL_GetIOSize(io);
    if (sz <= 0) { SDL_CloseIO(io); return {}; }
    std::vector<char> buffer(static_cast<size_t>(sz));
    SDL_ReadIO(io, buffer.data(), buffer.size());
    SDL_CloseIO(io);
    return buffer;
#else
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        return {};

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    return buffer;
#endif
}

#ifdef HAVE_SHADERC_SHADERC_HPP
// ============================================================================
// Runtime GLSL→SPIR-V compilation via libshaderc
// ============================================================================

namespace
{
    // Includer that resolves #include "file" against a list of search paths.
    // First match wins; system includes (<>) are not supported.
    class PathIncluder : public shaderc::CompileOptions::IncluderInterface
    {
    public:
        explicit PathIncluder(const std::vector<std::string>& paths) : paths_(paths) {}

        shaderc_include_result* GetInclude(
            const char* requested_source,
            shaderc_include_type /*type*/,
            const char* /*requesting_source*/,
            size_t /*include_depth*/) override
        {
            auto* result = new shaderc_include_result{};
            for (const auto& dir : paths_)
            {
                std::string full = dir;
                if (!full.empty() && full.back() != '/') full += '/';
                full += requested_source;

                std::ifstream f(full);
                if (f.good())
                {
                    std::stringstream ss;
                    ss << f.rdbuf();
                    auto* data = new std::string(ss.str());
                    auto* name = new std::string(full);
                    result->source_name = name->c_str();
                    result->source_name_length = name->size();
                    result->content = data->c_str();
                    result->content_length = data->size();
                    result->user_data = new std::pair<std::string*, std::string*>(name, data);
                    return result;
                }
            }
            // Not found — return empty result with error message
            auto* err = new std::string("include not found: ");
            *err += requested_source;
            result->source_name = "";
            result->source_name_length = 0;
            result->content = err->c_str();
            result->content_length = err->size();
            result->user_data = new std::pair<std::string*, std::string*>(nullptr, err);
            return result;
        }

        void ReleaseInclude(shaderc_include_result* result) override
        {
            auto* pair = static_cast<std::pair<std::string*, std::string*>*>(result->user_data);
            delete pair->first;
            delete pair->second;
            delete pair;
            delete result;
        }

    private:
        std::vector<std::string> paths_;
    };
}

std::vector<uint32_t> rVulkanShader::CompileGLSL(
    const std::string& source,
    const std::string& sourcePath,
    Stage stage,
    const std::vector<std::string>& includePaths,
    const std::vector<std::pair<std::string, std::string>>& defines,
    std::string* outError)
{
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetIncluder(std::make_unique<PathIncluder>(includePaths));

    for (const auto& d : defines)
    {
        if (d.second.empty())
            options.AddMacroDefinition(d.first);
        else
            options.AddMacroDefinition(d.first, d.second);
    }

    shaderc_shader_kind kind;
    switch (stage) {
        case Stage::Vertex:  kind = shaderc_glsl_vertex_shader;  break;
        default:             kind = shaderc_glsl_fragment_shader; break;
    }

    auto result = compiler.CompileGlslToSpv(source, kind, sourcePath.c_str(), options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        if (outError) *outError = result.GetErrorMessage();
        return {};
    }
    return {result.cbegin(), result.cend()};
}

std::vector<uint32_t> rVulkanShader::CompileGLSLFromFile(
    const std::string& path,
    Stage stage,
    const std::vector<std::string>& includePaths,
    const std::vector<std::pair<std::string, std::string>>& defines,
    std::string* outError)
{
    std::ifstream f(path);
    if (!f.good())
    {
        if (outError) *outError = "cannot open source file: " + path;
        return {};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return CompileGLSL(ss.str(), path, stage, includePaths, defines, outError);
}

VkShaderModule rVulkanShader::CompileFromFile(
    VkDevice device,
    const std::string& path,
    Stage stage,
    const std::vector<std::string>& includePaths,
    std::string* outError)
{
    auto spirv = CompileGLSLFromFile(path, stage, includePaths, outError);
    if (spirv.empty())
        return VK_NULL_HANDLE;
    return LoadFromMemory(device, spirv.data(), spirv.size() * sizeof(uint32_t));
}

#endif // HAVE_SHADERC_SHADERC_HPP

#endif // DEDICATED
