#pragma once

#include <filesystem>
#include <string>

namespace Engine::PathUtils
{
    std::filesystem::path GetExecutablePath();
    std::filesystem::path GetExecutableDirectory();

    std::filesystem::path FindFileUpward(
        std::filesystem::path startDirectory,
        const std::filesystem::path& relativePath,
        int maxDepth = 10);

    std::filesystem::path GetParentPath(const std::filesystem::path& absolutePath, int parentLevel);
    std::filesystem::path GetParentPath(const char* absolutePath, int parentLevel);
    std::filesystem::path GetParentPath(const wchar_t* absolutePath, int parentLevel);
    std::filesystem::path GetParentPath(const std::string& absolutePath, int parentLevel);
    std::filesystem::path GetParentPath(const std::wstring& absolutePath, int parentLevel);

    std::filesystem::path GetExecutableParentPath(int parentLevel);

    std::filesystem::path ResolveResourcePath(const std::filesystem::path& relativePath);
    std::filesystem::path ResolveResourcePath(const char* relativePath);
    std::filesystem::path ResolveResourcePath(const wchar_t* relativePath);
    std::filesystem::path ResolveResourcePath(const std::string& relativePath);
    std::filesystem::path ResolveResourcePath(const std::wstring& relativePath);

    std::string ResolveResourcePathString(const std::filesystem::path& relativePath);
    std::string ResolveResourcePathString(const char* relativePath);
    std::string ResolveResourcePathString(const std::string& relativePath);
    std::string GetParentPathString(const std::filesystem::path& absolutePath, int parentLevel);
    std::string GetParentPathString(const char* absolutePath, int parentLevel);
    std::string GetParentPathString(const std::string& absolutePath, int parentLevel);
    std::string GetExecutableParentPathString(int parentLevel);

    std::wstring ResolveResourcePathWString(const std::filesystem::path& relativePath);
    std::wstring ResolveResourcePathWString(const wchar_t* relativePath);
    std::wstring ResolveResourcePathWString(const std::wstring& relativePath);
    std::wstring GetParentPathWString(const std::filesystem::path& absolutePath, int parentLevel);
    std::wstring GetParentPathWString(const wchar_t* absolutePath, int parentLevel);
    std::wstring GetParentPathWString(const std::wstring& absolutePath, int parentLevel);
    std::wstring GetExecutableParentPathWString(int parentLevel);
}
