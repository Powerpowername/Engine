#include "Auxi/PathUtils.hpp"

#include <windows.h>

namespace Engine::PathUtils
{
    std::filesystem::path GetExecutablePath()
    {
        wchar_t modulePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        if (length == 0u || length >= MAX_PATH)
        {
            return {};
        }

        return std::filesystem::path(modulePath);
    }

    std::filesystem::path GetExecutableDirectory()
    {
        const std::filesystem::path executablePath = GetExecutablePath();
        if (executablePath.empty())
        {
            return {};
        }

        return executablePath.parent_path();
    }

    std::filesystem::path FindFileUpward(
        std::filesystem::path startDirectory,
        const std::filesystem::path& relativePath,
        int maxDepth)
    {
        std::error_code error;
        for (int i = 0; i < maxDepth; ++i)
        {
            const std::filesystem::path candidate = startDirectory / relativePath;
            if (std::filesystem::exists(candidate, error))
            {
                return candidate;
            }

            const std::filesystem::path parent = startDirectory.parent_path();
            if (parent.empty() || parent == startDirectory)
            {
                break;
            }
            startDirectory = parent;
        }

        return {};
    }

    std::filesystem::path GetParentPath(const std::filesystem::path& absolutePath, int parentLevel)
    {
        std::filesystem::path path = absolutePath;
        for (int i = 0; i < parentLevel; ++i)
        {
            const std::filesystem::path parent = path.parent_path();
            if (parent.empty() || parent == path)
            {
                break;
            }
            path = parent;
        }

        return path;
    }

    std::filesystem::path GetParentPath(const char* absolutePath, int parentLevel)
    {
        return GetParentPath(std::filesystem::path(absolutePath), parentLevel);
    }

    std::filesystem::path GetParentPath(const wchar_t* absolutePath, int parentLevel)
    {
        return GetParentPath(std::filesystem::path(absolutePath), parentLevel);
    }

    std::filesystem::path GetParentPath(const std::string& absolutePath, int parentLevel)
    {
        return GetParentPath(std::filesystem::path(absolutePath), parentLevel);
    }

    std::filesystem::path GetParentPath(const std::wstring& absolutePath, int parentLevel)
    {
        return GetParentPath(std::filesystem::path(absolutePath), parentLevel);
    }

    std::filesystem::path GetExecutableParentPath(int parentLevel)
    {
        return GetParentPath(GetExecutablePath(), parentLevel);
    }

    std::filesystem::path ResolveResourcePath(const std::filesystem::path& relativePath)
    {
        const std::filesystem::path executableDirectory = GetExecutableDirectory();
        if (!executableDirectory.empty())
        {
            const std::filesystem::path filePath = FindFileUpward(executableDirectory, relativePath);
            if (!filePath.empty())
            {
                return filePath;
            }
        }

        std::error_code error;
        const std::filesystem::path currentPath = std::filesystem::current_path(error);
        if (!error)
        {
            const std::filesystem::path filePath = FindFileUpward(currentPath, relativePath);
            if (!filePath.empty())
            {
                return filePath;
            }
        }

        return {};
    }

    std::filesystem::path ResolveResourcePath(const std::string& relativePath)
    {
        return ResolveResourcePath(std::filesystem::path(relativePath));
    }

    std::filesystem::path ResolveResourcePath(const char* relativePath)
    {
        return ResolveResourcePath(std::filesystem::path(relativePath));
    }

    std::filesystem::path ResolveResourcePath(const std::wstring& relativePath)
    {
        return ResolveResourcePath(std::filesystem::path(relativePath));
    }

    std::filesystem::path ResolveResourcePath(const wchar_t* relativePath)
    {
        return ResolveResourcePath(std::filesystem::path(relativePath));
    }

    std::string ResolveResourcePathString(const std::filesystem::path& relativePath)
    {
        return ResolveResourcePath(relativePath).string();
    }

    std::string ResolveResourcePathString(const char* relativePath)
    {
        return ResolveResourcePath(relativePath).string();
    }

    std::string ResolveResourcePathString(const std::string& relativePath)
    {
        return ResolveResourcePath(relativePath).string();
    }

    std::string GetParentPathString(const std::filesystem::path& absolutePath, int parentLevel)
    {
        return GetParentPath(absolutePath, parentLevel).string();
    }

    std::string GetParentPathString(const char* absolutePath, int parentLevel)
    {
        return GetParentPath(absolutePath, parentLevel).string();
    }

    std::string GetParentPathString(const std::string& absolutePath, int parentLevel)
    {
        return GetParentPath(absolutePath, parentLevel).string();
    }

    std::string GetExecutableParentPathString(int parentLevel)
    {
        return GetExecutableParentPath(parentLevel).string();
    }

    std::wstring ResolveResourcePathWString(const std::filesystem::path& relativePath)
    {
        return ResolveResourcePath(relativePath).wstring();
    }

    std::wstring ResolveResourcePathWString(const wchar_t* relativePath)
    {
        return ResolveResourcePath(relativePath).wstring();
    }

    std::wstring ResolveResourcePathWString(const std::wstring& relativePath)
    {
        return ResolveResourcePath(relativePath).wstring();
    }

    std::wstring GetParentPathWString(const std::filesystem::path& absolutePath, int parentLevel)
    {
        return GetParentPath(absolutePath, parentLevel).wstring();
    }

    std::wstring GetParentPathWString(const wchar_t* absolutePath, int parentLevel)
    {
        return GetParentPath(absolutePath, parentLevel).wstring();
    }

    std::wstring GetParentPathWString(const std::wstring& absolutePath, int parentLevel)
    {
        return GetParentPath(absolutePath, parentLevel).wstring();
    }

    std::wstring GetExecutableParentPathWString(int parentLevel)
    {
        return GetExecutableParentPath(parentLevel).wstring();
    }
}
