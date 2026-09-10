#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string>

namespace WinUtil {

    // Checks whether the current process is running with Administrator privileges
    bool IsUserAdmin();

    // Relaunches the current executable with 'runas' verb to trigger UAC elevation
    bool RelaunchAsAdmin(HWND hWnd = NULL);

    // Accurately detects Windows version (Windows 7, 8, 8.1, 10, 11) using RtlGetVersion
    std::wstring GetWindowsVersionName();

    // Returns the canonical path to the hosts file
    std::wstring GetHostsFilePath();

    // Flushes the Windows DNS resolver cache using DnsFlushResolverCache and ipconfig
    bool FlushDnsCache();

    // Opens a file in Notepad
    bool OpenInNotepad(const std::wstring& filePath);

} // namespace WinUtil
