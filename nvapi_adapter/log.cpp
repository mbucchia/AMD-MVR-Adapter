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

#include "log.h"

namespace {

    std::mutex g_logMutex;
    FILE* g_logFile = nullptr;
    bool g_logFileOpenAttempted = false;

    void OpenLogFile() {
        g_logFileOpenAttempted = true;

        const std::wstring directory = logging::GetDataDirectory();
        if (directory.empty()) {
            return;
        }

        // Allow other processes (and editors) to read the log while we hold it open.
        g_logFile = _wfsopen((directory + L"\\nvapi_adapter.log").c_str(), L"w", _SH_DENYWR);
    }

} // namespace

namespace logging {

    std::wstring GetDataDirectory() {
        wchar_t localAppData[MAX_PATH];
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return {};
        }
        const std::wstring path = std::wstring(localAppData) + L"\\AMD-MVR-Adapter";
        CreateDirectoryW(path.c_str(), nullptr);
        return path;
    }

    void Log(const char* fmt, ...) {
        char buffer[1024];
        va_list va;
        va_start(va, fmt);
        vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, va);
        va_end(va);

        OutputDebugStringA(buffer);

        std::unique_lock lock(g_logMutex);
        if (!g_logFileOpenAttempted) {
            OpenLogFile();
        }
        if (g_logFile) {
            fputs(buffer, g_logFile);
            fflush(g_logFile);
        }
    }

} // namespace logging
