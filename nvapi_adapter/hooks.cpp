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

    decltype(&LoadLibraryExW) g_original_LoadLibraryExW = nullptr;
    decltype(&D3D11CreateDeviceAndSwapChain) g_original_D3D11CreateDeviceAndSwapChain = nullptr;
    decltype(&D3D11CreateDevice) g_original_D3D11CreateDevice = nullptr;

    // Set while we create a device, so that nested calls (AGS calls D3D11CreateDevice itself) go straight to the
    // original functions.
    thread_local bool t_inDeviceCreation = false;

    HMODULE WINAPI hooked_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
        if (lpLibFileName && _wcsicmp(lpLibFileName, L"nvapi64.dll") == 0) {
            logging::Log("Redirecting LoadLibraryExW(%ls)\n", lpLibFileName);
            // Take a reference on ourselves, to balance the FreeLibrary() the caller will eventually do.
            HMODULE self = nullptr;
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&hooked_LoadLibraryExW), &self);
            return self;
        }
        return g_original_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
    }

    HRESULT WINAPI hooked_D3D11CreateDeviceAndSwapChain(IDXGIAdapter* pAdapter,
                                                        D3D_DRIVER_TYPE DriverType,
                                                        HMODULE Software,
                                                        UINT Flags,
                                                        const D3D_FEATURE_LEVEL* pFeatureLevels,
                                                        UINT FeatureLevels,
                                                        UINT SDKVersion,
                                                        const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
                                                        IDXGISwapChain** ppSwapChain,
                                                        ID3D11Device** ppDevice,
                                                        D3D_FEATURE_LEVEL* pFeatureLevel,
                                                        ID3D11DeviceContext** ppImmediateContext) {
        auto& state = adapter::GetState();

        // Nested calls, and calls we cannot handle (feature level probing with no device returned).
        if (t_inDeviceCreation || !ppDevice) {
            return g_original_D3D11CreateDeviceAndSwapChain(pAdapter,
                                                            DriverType,
                                                            Software,
                                                            Flags,
                                                            pFeatureLevels,
                                                            FeatureLevels,
                                                            SDKVersion,
                                                            pSwapChainDesc,
                                                            ppSwapChain,
                                                            ppDevice,
                                                            pFeatureLevel,
                                                            ppImmediateContext);
        }
        t_inDeviceCreation = true;

        if (!state.agsInitAttempted) {
            state.agsInitAttempted = true;
            AGSGPUInfo gpuInfo{};
            const AGSReturnCode result = agsInitialize(AGS_CURRENT_VERSION, nullptr, &state.agsContext, &gpuInfo);
            logging::Log("agsInitialize() returned %d (Radeon Software: %s)\n",
                         result,
                         result == AGS_SUCCESS && gpuInfo.radeonSoftwareVersion ? gpuInfo.radeonSoftwareVersion : "?");
            if (result != AGS_SUCCESS) {
                state.agsContext = nullptr;
            }
        }

        AGSDX11DeviceCreationParams creationParams{};
        creationParams.pAdapter = pAdapter;
        creationParams.DriverType = DriverType;
        creationParams.Software = Software;
        creationParams.Flags = Flags;
        creationParams.pFeatureLevels = pFeatureLevels;
        creationParams.FeatureLevels = FeatureLevels;
        creationParams.SDKVersion = SDKVersion;
        creationParams.pSwapChainDesc = ppSwapChain ? pSwapChainDesc : nullptr;

        AGSDX11ExtensionParams extensionParams{};
        extensionParams.pAppName = L"AMD-MVR-Adapter";
        extensionParams.pEngineName = L"AMD-MVR-Adapter";
        // TODO: Must pick a UAV slot that does not collide with the application's shaders.
        extensionParams.uavSlot = 7;
        extensionParams.crossfireMode = AGS_CROSSFIRE_MODE_DISABLE;

        AGSDX11ReturnedParams returnedParams{};
        HRESULT hr;
        if (state.agsContext &&
            agsDriverExtensionsDX11_CreateDevice(
                state.agsContext, &creationParams, &extensionParams, &returnedParams) == AGS_SUCCESS) {
            logging::Log("Created D3D11 device %p through AGS (multiView=%u)\n",
                         returnedParams.pDevice,
                         returnedParams.extensionsSupported.multiView);

            // TODO: AGS wants agsDriverExtensionsDX11_DestroyDevice() to be called to release the device. For now the
            // application simply releases it, which may leak AGS's internal resources.
            state.agsDevice = returnedParams.pDevice;
            state.agsExtensions = returnedParams.extensionsSupported;

            *ppDevice = returnedParams.pDevice;
            if (ppImmediateContext) {
                *ppImmediateContext = returnedParams.pImmediateContext;
            } else {
                returnedParams.pImmediateContext->Release();
            }
            if (ppSwapChain) {
                *ppSwapChain = returnedParams.pSwapChain;
            }
            if (pFeatureLevel) {
                *pFeatureLevel = returnedParams.featureLevel;
            }
            hr = S_OK;
        } else {
            logging::Log("Could not create the device through AGS, falling back without AGS\n");
            hr = g_original_D3D11CreateDeviceAndSwapChain(pAdapter,
                                                          DriverType,
                                                          Software,
                                                          Flags,
                                                          pFeatureLevels,
                                                          FeatureLevels,
                                                          SDKVersion,
                                                          pSwapChainDesc,
                                                          ppSwapChain,
                                                          ppDevice,
                                                          pFeatureLevel,
                                                          ppImmediateContext);
        }

        t_inDeviceCreation = false;
        return hr;
    }

    HRESULT WINAPI hooked_D3D11CreateDevice(IDXGIAdapter* pAdapter,
                                            D3D_DRIVER_TYPE DriverType,
                                            HMODULE Software,
                                            UINT Flags,
                                            const D3D_FEATURE_LEVEL* pFeatureLevels,
                                            UINT FeatureLevels,
                                            UINT SDKVersion,
                                            ID3D11Device** ppDevice,
                                            D3D_FEATURE_LEVEL* pFeatureLevel,
                                            ID3D11DeviceContext** ppImmediateContext) {
        if (t_inDeviceCreation) {
            return g_original_D3D11CreateDevice(pAdapter,
                                                DriverType,
                                                Software,
                                                Flags,
                                                pFeatureLevels,
                                                FeatureLevels,
                                                SDKVersion,
                                                ppDevice,
                                                pFeatureLevel,
                                                ppImmediateContext);
        }
        return hooked_D3D11CreateDeviceAndSwapChain(pAdapter,
                                                    DriverType,
                                                    Software,
                                                    Flags,
                                                    pFeatureLevels,
                                                    FeatureLevels,
                                                    SDKVersion,
                                                    nullptr,
                                                    nullptr,
                                                    ppDevice,
                                                    pFeatureLevel,
                                                    ppImmediateContext);
    }

    template <typename T>
    void Attach(T& original, T hooked, const wchar_t* moduleName, const char* functionName) {
        original = reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(moduleName), functionName));
        DetourAttach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(hooked));
    }

    template <typename T>
    void Detach(T& original, T hooked) {
        DetourDetach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(hooked));
    }

} // namespace

namespace adapter {

    void InstallHooks() {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        Attach(g_original_LoadLibraryExW, hooked_LoadLibraryExW, L"kernelbase.dll", "LoadLibraryExW");
        Attach(g_original_D3D11CreateDevice, hooked_D3D11CreateDevice, L"d3d11.dll", "D3D11CreateDevice");
        Attach(g_original_D3D11CreateDeviceAndSwapChain,
               hooked_D3D11CreateDeviceAndSwapChain,
               L"d3d11.dll",
               "D3D11CreateDeviceAndSwapChain");
        logging::Log("Hooks installed: %ld\n", DetourTransactionCommit());
    }

    void UninstallHooks() {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        Detach(g_original_LoadLibraryExW, hooked_LoadLibraryExW);
        Detach(g_original_D3D11CreateDevice, hooked_D3D11CreateDevice);
        Detach(g_original_D3D11CreateDeviceAndSwapChain, hooked_D3D11CreateDeviceAndSwapChain);
        DetourTransactionCommit();
    }

} // namespace adapter
