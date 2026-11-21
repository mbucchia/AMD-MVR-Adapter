// MIT License
//
// Copyright (c) 2026 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.h"

#include "adapter.h"
#include "log.h"

namespace {

    // Known IDs.
    enum : NvU32 {
        kId_NvAPI_Initialize = 0x0150E828,
        kId_NvAPI_Unload = 0xD22BDD7E,
        kId_NvAPI_D3D_RegisterDevice = 0x8C02C4D0,
        kId_NvAPI_D3D11_IsNvShaderExtnOpCodeSupported = 0x5F68DA40,
        kId_NvAPI_D3D_QueryMultiViewSupport = 0xB6E0A41C,
        kId_NvAPI_D3D_QueryModifiedWSupport = 0xCBF9F4F5,
        kId_NvAPI_D3D_QuerySinglePassStereoSupport = 0x6F5F0A6D,
        kId_NvAPI_D3D_SetMultiViewMode = 0x8285C8DA,
        kId_NvAPI_D3D_SetModifiedWMode = 0x06EA4BF4,
        kId_NvAPI_D3D_SetSinglePassStereoMode = 0xA39E6E6E,
        kId_NvAPI_D3D11_CreateVertexShaderEx = 0x0BEAA0B2,
        kId_NvAPI_D3D11_CreateHullShaderEx = 0xB53CAB00,
        kId_NvAPI_D3D11_CreateDomainShaderEx = 0xA0D7180D,
        kId_NvAPI_D3D11_CreateGeometryShaderEx_2 = 0x99ED5C1C,
        kId_NvAPI_D3D11_CreateFastGeometryShaderExplicit = 0x71AB7C9C,
        kId_NvAPI_D3D11_CreatePixelShaderEx_2 = 0x4162822B,
    };

    // IDs queried by the static library that we know about and deliberately do not implement (returning nullptr is
    // handled gracefully by the library).
    bool IsKnownUnimplemented(NvU32 id) {
        switch (id) {
        case 0xAD298D3F: // initInterfaceEx
        case 0x33C7358C: // callStart
        case 0x593E8644: // callReturn
        case 0xD7C61344: // used during unload
            return true;
        default:
            return false;
        }
    }

#define LOG_CALL() logging::Log("--> %s\n", __func__)

} // namespace

NvAPI_Status __cdecl NvAPI_Initialize() {
    LOG_CALL();
    return NVAPI_OK;
}

NvAPI_Status __cdecl NvAPI_Unload() {
    LOG_CALL();
    return NVAPI_OK;
}

NvAPI_Status __cdecl NvAPI_D3D_RegisterDevice(IUnknown* pDev) {
    LOG_CALL();
    if (!pDev) {
        return NVAPI_INVALID_ARGUMENT;
    }
    return NVAPI_OK;
}

NvAPI_Status __cdecl NvAPI_D3D11_IsNvShaderExtnOpCodeSupported(IUnknown* pDev, NvU32 opCode, bool* pSupported) {
    logging::Log("--> %s(opCode=%u)\n", __func__, opCode);
    if (!pDev || !pSupported) {
        return NVAPI_INVALID_ARGUMENT;
    }
    *pSupported = false;
    return NVAPI_OK;
}

NvAPI_Status __cdecl NvAPI_D3D_QueryMultiViewSupport(IUnknown* pDevice,
                                                     NV_QUERY_MULTIVIEW_SUPPORT_PARAMS* pQueryMultiViewSupportedParams) {
    LOG_CALL();
    if (!pDevice || !pQueryMultiViewSupportedParams) {
        return NVAPI_INVALID_ARGUMENT;
    }
    if (pQueryMultiViewSupportedParams->version != NV_QUERY_MULTIVIEW_SUPPORT_PARAMS_VER1) {
        return NVAPI_INCOMPATIBLE_STRUCT_VERSION;
    }
    pQueryMultiViewSupportedParams->bMultiViewSupported = false;
    pQueryMultiViewSupportedParams->bSinglePassStereoSupported = false;
    return NVAPI_OK;
}

NvAPI_Status __cdecl NvAPI_D3D_QueryModifiedWSupport(IUnknown* pDev,
                                                     NV_QUERY_MODIFIED_W_SUPPORT_PARAMS* pQueryModifiedWSupportedParams) {
    LOG_CALL();
    if (!pDev || !pQueryModifiedWSupportedParams) {
        return NVAPI_INVALID_ARGUMENT;
    }
    if (pQueryModifiedWSupportedParams->version != NV_QUERY_MODIFIED_W_SUPPORT_PARAMS_VER1) {
        return NVAPI_INCOMPATIBLE_STRUCT_VERSION;
    }
    pQueryModifiedWSupportedParams->bModifiedWSupported = false;
    return NVAPI_OK;
}

NvAPI_Status __cdecl NvAPI_D3D_QuerySinglePassStereoSupport(
    IUnknown* pDevice, NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS* pQuerySinglePassStereoSupportedParams) {
    LOG_CALL();
    if (!pDevice || !pQuerySinglePassStereoSupportedParams) {
        return NVAPI_INVALID_ARGUMENT;
    }
    if (pQuerySinglePassStereoSupportedParams->version != NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS_VER1 &&
        pQuerySinglePassStereoSupportedParams->version != NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS_VER2) {
        return NVAPI_INCOMPATIBLE_STRUCT_VERSION;
    }
    pQuerySinglePassStereoSupportedParams->bSinglePassStereoSupported = false;
    return NVAPI_OK;
}

NvAPI_Status __cdecl NvAPI_D3D_SetMultiViewMode(IUnknown* pDevOrContext, NV_MULTIVIEW_PARAMS* pMultiViewParams) {
    LOG_CALL();
    return NVAPI_NOT_SUPPORTED;
}

NvAPI_Status __cdecl NvAPI_D3D_SetModifiedWMode(IUnknown* pDevOrContext, NV_MODIFIED_W_PARAMS* psModifiedWParams) {
    LOG_CALL();
    return NVAPI_NOT_SUPPORTED;
}

NvAPI_Status __cdecl NvAPI_D3D_SetSinglePassStereoMode(IUnknown* pDevOrContext,
                                                       NvU32 numViews,
                                                       NvU32 renderTargetIndexOffset,
                                                       NvU8 independentViewportMaskEnable) {
    logging::Log("--> %s(numViews=%u, renderTargetIndexOffset=%u, independentViewportMaskEnable=%u)\n",
                 __func__,
                 numViews,
                 renderTargetIndexOffset,
                 independentViewportMaskEnable);
    return NVAPI_NOT_SUPPORTED;
}

NvAPI_Status __cdecl NvAPI_D3D11_CreateVertexShaderEx(ID3D11Device* pDevice,
                                                      const void* pShaderBytecode,
                                                      SIZE_T BytecodeLength,
                                                      ID3D11ClassLinkage* pClassLinkage,
                                                      const NvAPI_D3D11_CREATE_VERTEX_SHADER_EX* pCreateVertexShaderExArgs,
                                                      ID3D11VertexShader** ppVertexShader) {
    LOG_CALL();
    if (!pDevice || !pCreateVertexShaderExArgs || !ppVertexShader) {
        return NVAPI_INVALID_ARGUMENT;
    }
    return SUCCEEDED(pDevice->CreateVertexShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader))
               ? NVAPI_OK
               : NVAPI_ERROR;
}

NvAPI_Status __cdecl NvAPI_D3D11_CreateHullShaderEx(ID3D11Device* pDevice,
                                                    const void* pShaderBytecode,
                                                    SIZE_T BytecodeLength,
                                                    ID3D11ClassLinkage* pClassLinkage,
                                                    const NvAPI_D3D11_CREATE_HULL_SHADER_EX* pCreateHullShaderExArgs,
                                                    ID3D11HullShader** ppHullShader) {
    LOG_CALL();
    if (!pDevice || !pCreateHullShaderExArgs || !ppHullShader) {
        return NVAPI_INVALID_ARGUMENT;
    }
    return SUCCEEDED(pDevice->CreateHullShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppHullShader))
               ? NVAPI_OK
               : NVAPI_ERROR;
}

NvAPI_Status __cdecl NvAPI_D3D11_CreateDomainShaderEx(ID3D11Device* pDevice,
                                                      const void* pShaderBytecode,
                                                      SIZE_T BytecodeLength,
                                                      ID3D11ClassLinkage* pClassLinkage,
                                                      const NvAPI_D3D11_CREATE_DOMAIN_SHADER_EX* pCreateDomainShaderExArgs,
                                                      ID3D11DomainShader** ppDomainShader) {
    LOG_CALL();
    if (!pDevice || !pCreateDomainShaderExArgs || !ppDomainShader) {
        return NVAPI_INVALID_ARGUMENT;
    }
    return SUCCEEDED(pDevice->CreateDomainShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppDomainShader))
               ? NVAPI_OK
               : NVAPI_ERROR;
}

NvAPI_Status __cdecl NvAPI_D3D11_CreateGeometryShaderEx_2(
    ID3D11Device* pDevice,
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    const NvAPI_D3D11_CREATE_GEOMETRY_SHADER_EX* pCreateGeometryShaderExArgs,
    ID3D11GeometryShader** ppGeometryShader) {
    LOG_CALL();
    if (!pDevice || !pCreateGeometryShaderExArgs || !ppGeometryShader) {
        return NVAPI_INVALID_ARGUMENT;
    }
    return SUCCEEDED(pDevice->CreateGeometryShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppGeometryShader))
               ? NVAPI_OK
               : NVAPI_ERROR;
}

NvAPI_Status __cdecl NvAPI_D3D11_CreateFastGeometryShaderExplicit(
    ID3D11Device* pDevice,
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    const NvAPI_D3D11_CREATE_FASTGS_EXPLICIT_DESC* pCreateFastGSArgs,
    ID3D11GeometryShader** ppGeometryShader) {
    LOG_CALL();
    return NVAPI_NOT_SUPPORTED;
}

NvAPI_Status __cdecl NvAPI_D3D11_CreatePixelShaderEx_2(ID3D11Device* pDevice,
                                                       const void* pShaderBytecode,
                                                       SIZE_T BytecodeLength,
                                                       ID3D11ClassLinkage* pClassLinkage,
                                                       const NvAPI_D3D11_CREATE_PIXEL_SHADER_EX* pCreatePixelShaderExArgs,
                                                       ID3D11PixelShader** ppPixelShader) {
    LOG_CALL();
    if (!pDevice || !pCreatePixelShaderExArgs || !ppPixelShader) {
        return NVAPI_INVALID_ARGUMENT;
    }
    return SUCCEEDED(pDevice->CreatePixelShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader))
               ? NVAPI_OK
               : NVAPI_ERROR;
}

// The only export of nvapi64.dll.
extern "C" __declspec(dllexport) void* __cdecl nvapi_QueryInterface(NvU32 id) {
    switch (id) {
#define RESOLVE(name)                                                                                                  \
    case kId_##name:                                                                                                   \
        return reinterpret_cast<void*>(&name)
        RESOLVE(NvAPI_Initialize);
        RESOLVE(NvAPI_Unload);
        RESOLVE(NvAPI_D3D_RegisterDevice);
        RESOLVE(NvAPI_D3D11_IsNvShaderExtnOpCodeSupported);
        RESOLVE(NvAPI_D3D_QueryMultiViewSupport);
        RESOLVE(NvAPI_D3D_QueryModifiedWSupport);
        RESOLVE(NvAPI_D3D_QuerySinglePassStereoSupport);
        RESOLVE(NvAPI_D3D_SetMultiViewMode);
        RESOLVE(NvAPI_D3D_SetModifiedWMode);
        RESOLVE(NvAPI_D3D_SetSinglePassStereoMode);
        RESOLVE(NvAPI_D3D11_CreateVertexShaderEx);
        RESOLVE(NvAPI_D3D11_CreateHullShaderEx);
        RESOLVE(NvAPI_D3D11_CreateDomainShaderEx);
        RESOLVE(NvAPI_D3D11_CreateGeometryShaderEx_2);
        RESOLVE(NvAPI_D3D11_CreateFastGeometryShaderExplicit);
        RESOLVE(NvAPI_D3D11_CreatePixelShaderEx_2);
#undef RESOLVE
    }

    if (!IsKnownUnimplemented(id)) {
        logging::Log("nvapi_QueryInterface(0x%08x): unimplemented\n", id);
    }
    return nullptr;
}
