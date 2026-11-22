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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>

// The functions the stubs of exports.asm jump to, in the order of kExportNames. Defined in exports.asm, where each
// entry initially points to a resolver that calls ResolveExport().
constexpr size_t kExportCount = 20;
extern "C" void* g_systemFunctions[kExportCount];

namespace {

    // The applications to load the NVAPI adapter into (executable names, case insensitive). Other applications in
    // the same folder only get the dxgi.dll forwarding.
    constexpr const wchar_t* kSupportedApplications[] = {
        L"iRacingSim64DX11.exe",
        L"demo_dx11.exe", // The VRWorks multiprojection sample, for testing.
    };

    // Must match the order of the stubs in exports.asm.
    constexpr const char* kExportNames[] = {
        "ApplyCompatResolutionQuirking",
        "CompatString",
        "CompatValue",
        "DXGIDumpJournal",
        "PIXBeginCapture",
        "PIXEndCapture",
        "PIXGetCaptureState",
        "SetAppCompatStringPointer",
        "UpdateHMDEmulationStatus",
        "CreateDXGIFactory",
        "CreateDXGIFactory1",
        "CreateDXGIFactory2",
        "DXGID3D10CreateDevice",
        "DXGID3D10CreateLayeredDevice",
        "DXGID3D10GetLayeredDeviceSize",
        "DXGID3D10RegisterLayers",
        "DXGIDeclareAdapterRemovalSupport",
        "DXGIDisableVBlankVirtualization",
        "DXGIGetDebugInterface1",
        "DXGIReportAdapterConfiguration",
    };
    static_assert(ARRAYSIZE(kExportNames) == kExportCount);

    void Log(const char* fmt, ...) {
        char buffer[1024];
        va_list va;
        va_start(va, fmt);
        vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, va);
        va_end(va);
        OutputDebugStringA(buffer);

        static HANDLE file = [] {
            wchar_t directory[MAX_PATH];
            const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", directory, MAX_PATH);
            wchar_t executable[MAX_PATH];
            if (length == 0 || length >= MAX_PATH || !GetModuleFileNameW(nullptr, executable, MAX_PATH)) {
                return INVALID_HANDLE_VALUE;
            }
            const wchar_t* name = wcsrchr(executable, L'\\');
            name = name ? name + 1 : executable;
            wchar_t path[MAX_PATH * 2];
            swprintf_s(path, L"%s\\AMD-MVR-Adapter", directory);
            CreateDirectoryW(path, nullptr);
            swprintf_s(path, L"%s\\AMD-MVR-Adapter\\dxgi_proxy_%s.log", directory, name);
            return CreateFileW(
                path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }();
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(file, buffer, static_cast<DWORD>(strlen(buffer)), &written, nullptr);
        }
    }

    std::wstring GetModulePath(HMODULE module) {
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
        return length > 0 && length < MAX_PATH ? std::wstring(path, length) : std::wstring{};
    }

    std::wstring GetDirectory(const std::wstring& path) {
        const size_t separator = path.find_last_of(L"\\/");
        return separator == std::wstring::npos ? std::wstring{} : path.substr(0, separator);
    }

    bool IsSupportedApplication() {
        const std::wstring path = GetModulePath(nullptr);
        const size_t separator = path.find_last_of(L"\\/");
        const std::wstring executable = separator == std::wstring::npos ? path : path.substr(separator + 1);
        for (const wchar_t* application : kSupportedApplications) {
            if (_wcsicmp(executable.c_str(), application) == 0) {
                return true;
            }
        }
        return false;
    }

    // What a stub calls when the system dxgi.dll does not have the function (or could not be loaded yet).
    HRESULT __stdcall UnavailableExport() {
        return E_NOTIMPL;
    }

    // May run before our DllMain(), so before the C runtime is initialized: only Win32 calls, and no stack cookie.
    __declspec(safebuffers) HMODULE LoadSystemDxgi() {
        static HMODULE volatile systemDxgi = nullptr;
        if (const HMODULE dxgi = systemDxgi) {
            return dxgi;
        }
        wchar_t path[MAX_PATH + 16];
        const UINT length = GetSystemDirectoryW(path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return nullptr;
        }
        const wchar_t kFileName[] = L"\\dxgi.dll";
        for (size_t i = 0; i < ARRAYSIZE(kFileName); i++) {
            path[length + i] = kFileName[i];
        }
        // Never unloaded: the stubs jump into it until the process exits. Concurrent first calls may load it twice,
        // which only takes an extra reference.
        const HMODULE dxgi = LoadLibraryW(path);
        systemDxgi = dxgi;
        return dxgi;
    }

} // namespace

// Called by the resolvers of exports.asm on the first call to an export: returns the function to jump to.
// May run before our DllMain(), so before the C runtime is initialized: only Win32 calls, and no stack cookie.
// For example, the application compatibility engine calls SetAppCompatStringPointer() very early.
extern "C" __declspec(safebuffers) void* ResolveExport(unsigned int index) {
    if (index >= kExportCount) {
        return reinterpret_cast<void*>(&UnavailableExport);
    }

    // Very early in the process, the system dxgi.dll cannot be loaded yet: fail this call, but keep the resolver for
    // the next one.
    const HMODULE dxgi = LoadSystemDxgi();
    if (!dxgi) {
        return reinterpret_cast<void*>(&UnavailableExport);
    }
    const FARPROC systemFunction = GetProcAddress(dxgi, kExportNames[index]);
    void* const function =
        systemFunction ? reinterpret_cast<void*>(systemFunction) : reinterpret_cast<void*>(&UnavailableExport);
    InterlockedExchangePointer(&g_systemFunctions[index], function);
    return function;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Log("dxgi_proxy: loaded into %ls\n", GetModulePath(nullptr).c_str());

        // Loaded from DllMain(), which is normally discouraged, but this is the only way to run before the
        // application's code. Its DllMain() does not wait on other threads.
        if (IsSupportedApplication()) {
            const std::wstring path = GetDirectory(GetModulePath(module)) + L"\\nvapi64.dll";
            // Never unloaded: the adapter installs hooks for the lifetime of the process.
            if (LoadLibraryW(path.c_str())) {
                Log("dxgi_proxy: loaded %ls\n", path.c_str());
            } else {
                Log("dxgi_proxy: failed to load %ls: error %lu\n", path.c_str(), GetLastError());
            }
        }
    }
    return TRUE;
}
