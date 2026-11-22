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

        // The view broadcast masks last set on the immediate context, to skip the driver call when the application
        // sets the same masks again (it does so many times per frame). Only used from the thread that uses the
        // immediate context, since D3D11 requires the application to serialize its use.
        bool viewBroadcastMasksValid = false;
        unsigned long long vpMask = 0;
        unsigned long long rtSliceMask = 0;
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

    // The AGS device of a device or context, or nullptr. Entries are never removed from State::agsDevices, so the
    // pointer remains valid for the lifetime of the process.
    AgsDevice* FindAgsDevice(IUnknown* deviceOrContext);

    void InstallHooks();
    void UninstallHooks();

} // namespace adapter
