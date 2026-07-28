#pragma once
#include "global.h"
using Microsoft::WRL::ComPtr;
using std::string;
using std::wstring;
class Shader
{
public:
    struct StageCompileDesc
    {
        const char* entryPoint = nullptr;
        const char* target = nullptr;
        const char* stageName = nullptr;

        StageCompileDesc() = default;

        StageCompileDesc(const char* entryPoint, const char* target, const char* stageName)
            : entryPoint(entryPoint), target(target), stageName(stageName)
        {
        }
    };

private:
    ComPtr<ID3DBlob> vsBlob = nullptr;
    ComPtr<ID3DBlob> gsBlob = nullptr;
    ComPtr<ID3DBlob> psBlob = nullptr;
    ComPtr<ID3DBlob> csBlob = nullptr;

    ComPtr<ID3DBlob> errorBlob;
    string errorInfo;
    D3D12_SHADER_BYTECODE vs{};
    D3D12_SHADER_BYTECODE gs{};
    D3D12_SHADER_BYTECODE ps{};
    D3D12_SHADER_BYTECODE cs{};
private:
    static const char* GetStageName(const StageCompileDesc& stage)
    {
        return stage.stageName ? stage.stageName : "Unknown";
    }

    bool ValidateStage(const StageCompileDesc& stage)
    {
        if (stage.entryPoint != nullptr && stage.target != nullptr)
        {
            return true;
        }

        errorInfo = "Shader stage entry point and target must not be null.";
        std::print("Create {} Shader failed information: {}\n", GetStageName(stage), errorInfo);
        return false;
    }

    bool CompileStageFromFile(
        const wstring& filePath,
        const StageCompileDesc& stage,
        ComPtr<ID3DBlob>& blob,
        D3D12_SHADER_BYTECODE& byteCode,
        UINT flags,
        const D3D_SHADER_MACRO* defines,
        ID3DInclude* include)
    {
        blob.Reset();
        byteCode = {};
        errorBlob.Reset();
        errorInfo.clear();

        if (!ValidateStage(stage))
        {
            return false;
        }

        if (FAILED(D3DCompileFromFile(
            filePath.c_str(),
            defines,
            include,
            stage.entryPoint,
            stage.target,
            flags,
            0,
            blob.GetAddressOf(),
            errorBlob.GetAddressOf())))
        {
            errorInfo = errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "";
            std::print("Create {} Shader failed information: {}\n", GetStageName(stage), errorInfo);
            return false;
        }

        byteCode = { blob->GetBufferPointer(), blob->GetBufferSize() };
        return true;
    }

    bool CompileStageFromString(
        const string& source,
        const char* sourceName,
        const StageCompileDesc& stage,
        ComPtr<ID3DBlob>& blob,
        D3D12_SHADER_BYTECODE& byteCode,
        UINT flags,
        const D3D_SHADER_MACRO* defines,
        ID3DInclude* include)
    {
        blob.Reset();
        byteCode = {};
        errorBlob.Reset();
        errorInfo.clear();

        if (!ValidateStage(stage))
        {
            return false;
        }

        if (FAILED(D3DCompile(
            source.c_str(),
            source.size(),
            sourceName,
            defines,
            include,
            stage.entryPoint,
            stage.target,
            flags,
            0,
            blob.GetAddressOf(),
            errorBlob.GetAddressOf())))
        {
            errorInfo = errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "";
            std::print("Create {} Shader failed information: {}\n", GetStageName(stage), errorInfo);
            return false;
        }

        byteCode = { blob->GetBufferPointer(), blob->GetBufferSize() };
        return true;
    }
private:
    bool CompileGraphicsStagesFromFile(
        const wstring& filePath,
        const StageCompileDesc& vsStage,
        const StageCompileDesc* gsStage,
        const StageCompileDesc* psStage,
        UINT flags,
        const D3D_SHADER_MACRO* defines,
        ID3DInclude* include)
    {
        Reset();
        if (!CompileStageFromFile(filePath, vsStage, vsBlob, vs, flags, defines, include))
        {
            return false;
        }

        if (gsStage != nullptr &&
            !CompileStageFromFile(filePath, *gsStage, gsBlob, gs, flags, defines, include))
        {
            return false;
        }

        return psStage == nullptr ||
            CompileStageFromFile(filePath, *psStage, psBlob, ps, flags, defines, include);
    }

    bool CompileGraphicsStagesFromString(
        const string& source,
        const StageCompileDesc& vsStage,
        const StageCompileDesc* gsStage,
        const StageCompileDesc* psStage,
        UINT flags,
        const D3D_SHADER_MACRO* defines,
        ID3DInclude* include,
        const char* sourceName)
    {
        Reset();
        if (!CompileStageFromString(source, sourceName, vsStage, vsBlob, vs, flags, defines, include))
        {
            return false;
        }

        if (gsStage != nullptr &&
            !CompileStageFromString(source, sourceName, *gsStage, gsBlob, gs, flags, defines, include))
        {
            return false;
        }

        return psStage == nullptr ||
            CompileStageFromString(source, sourceName, *psStage, psBlob, ps, flags, defines, include);
    }

public:
    Shader() = default;

    bool CompileGraphicsFromFile(
        const wstring& filePath,
        const StageCompileDesc& vsStage,
        const StageCompileDesc& psStage,
        UINT flags = DefaultCompileFlags(),
        const D3D_SHADER_MACRO* defines = nullptr,
        ID3DInclude* include = D3D_COMPILE_STANDARD_FILE_INCLUDE)
    {
        return CompileGraphicsStagesFromFile(filePath, vsStage, nullptr, &psStage, flags, defines, include);
    }

    bool CompileGraphicsFromFile(
        const wstring& filePath,
        const StageCompileDesc& vsStage,
        const StageCompileDesc& gsStage,
        const StageCompileDesc& psStage,
        UINT flags = DefaultCompileFlags(),
        const D3D_SHADER_MACRO* defines = nullptr,
        ID3DInclude* include = D3D_COMPILE_STANDARD_FILE_INCLUDE)
    {
        return CompileGraphicsStagesFromFile(filePath, vsStage, &gsStage, &psStage, flags, defines, include);
    }

    bool CompileVertexOnlyFromFile(
        const wstring& filePath,
        const StageCompileDesc& vsStage,
        UINT flags = DefaultCompileFlags(),
        const D3D_SHADER_MACRO* defines = nullptr,
        ID3DInclude* include = D3D_COMPILE_STANDARD_FILE_INCLUDE)
    {
        return CompileGraphicsStagesFromFile(filePath, vsStage, nullptr, nullptr, flags, defines, include);
    }

    bool CompileComputeFromFile(
        const wstring& filePath,
        const StageCompileDesc& computeStage,
        UINT flags = DefaultCompileFlags(),
        const D3D_SHADER_MACRO* defines = nullptr,
        ID3DInclude* include = D3D_COMPILE_STANDARD_FILE_INCLUDE)
    {
        Reset();
        return CompileStageFromFile(filePath, computeStage, csBlob, cs, flags, defines, include);
    }

    bool CompileGraphicsFromString(
        const string& source,
        const StageCompileDesc& vsStage,
        const StageCompileDesc& psStage,
        UINT flags = DefaultCompileFlags(),
        const D3D_SHADER_MACRO* defines = nullptr,
        ID3DInclude* include = nullptr,
        const char* sourceName = nullptr)
    {
        return CompileGraphicsStagesFromString(source, vsStage, nullptr, &psStage, flags, defines, include, sourceName);
    }

    bool CompileGraphicsFromString(
        const string& source,
        const StageCompileDesc& vsStage,
        const StageCompileDesc& gsStage,
        const StageCompileDesc& psStage,
        UINT flags = DefaultCompileFlags(),
        const D3D_SHADER_MACRO* defines = nullptr,
        ID3DInclude* include = nullptr,
        const char* sourceName = nullptr)
    {
        return CompileGraphicsStagesFromString(source, vsStage, &gsStage, &psStage, flags, defines, include, sourceName);
    }

    D3D12_SHADER_BYTECODE GetVsShaderByteCode()
    {
        return vs;
    }

    D3D12_SHADER_BYTECODE GetGsShaderByteCode()
    {
        return gs;
    }

    D3D12_SHADER_BYTECODE GetPsShaderByteCode()
    {
        return ps;
    }

    D3D12_SHADER_BYTECODE GetCsShaderByteCode()
    {
        return cs;
    }
    const ComPtr<ID3DBlob>& GetVsBlobComPtr() const
    {
        return vsBlob;
    }
    const ComPtr<ID3DBlob>& GetGsBlobComPtr() const
    {
        return gsBlob;
    }
    const ComPtr<ID3DBlob>& GetPsBlobComPtr() const
    {
        return psBlob;
    }

    const ComPtr<ID3DBlob>& GetCsBlobComPtr() const
    {
        return csBlob;
    }

    const std::string& GetError() const
    {
        return errorInfo;
    }

    bool IsVSValid() const
    {
        return vsBlob != nullptr;
    }

    bool IsGSValid() const
    {
        return gsBlob != nullptr;
    }

    bool IsPSValid() const
    {
        return psBlob != nullptr;
    }

    bool IsCSValid() const
    {
        return csBlob != nullptr;
    }

    void Reset()
    {
        vsBlob.Reset();
        gsBlob.Reset();
        psBlob.Reset();
        csBlob.Reset();
        errorBlob.Reset();
        errorInfo.clear();
        vs = {};
        gs = {};
        ps = {};
        cs = {};
    }

    static UINT DefaultCompileFlags()
    {
    #ifdef _DEBUG
        return D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    #else
       return D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    #endif
    }


};
