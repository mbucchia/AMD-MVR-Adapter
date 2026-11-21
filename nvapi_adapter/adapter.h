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

#pragma once

namespace adapter {

    struct AgsDevice {
        AGSContext* context = nullptr;
        AGSDX11ExtensionsSupported extensions{};
    };

    // Global state shared between the D3D11 hooks and the NVAPI implementation.
    struct State {
        HMODULE module = nullptr;

        // Set when agsInitialize() failed (no AMD driver): we no longer try to create devices through AGS.
        bool agsUnavailable = false;

        // Keep track of AGS contexts, keyed by their ID3D11Device interface.
        // TODO: Possible leak because AGS holds references  which are only released by
        // agsDriverExtensionsDX11_DestroyDevice().
        std::mutex agsDevicesMutex;
        std::unordered_map<ID3D11Device*, AgsDevice> agsDevices;

        // The last view broadcast masks enabled (for logging).
        unsigned long long vpMask = 0;
        unsigned long long rtSliceMask = 0;
    };

    State& GetState();

    bool FindAgsDevice(IUnknown* deviceOrContext, AgsDevice& agsDevice);

    void InstallHooks();
    void UninstallHooks();

} // namespace adapter
