#include "win_util.h"
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <vector>

namespace WinUtil {

    bool IsUserAdmin() {
        BOOL isAdmin = FALSE;
        PSID administratorsGroup = NULL;
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

        if (AllocateAndInitializeSid(
            &ntAuthority, 2,
            SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0,
            &administratorsGroup))
        {
            CheckTokenMembership(NULL, administratorsGroup, &isAdmin);
            FreeSid(administratorsGroup);
        }

        return (isAdmin == TRUE);
    }

    bool RelaunchAsAdmin(HWND hWnd) {
        wchar_t szPath[MAX_PATH];
        if (GetModuleFileNameW(NULL, szPath, MAX_PATH) == 0) {
            return false;
        }

        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = szPath;
        sei.hwnd = hWnd;
        sei.nShow = SW_SHOWNORMAL;

        if (ShellExecuteExW(&sei)) {
            ExitProcess(0);
            return true;
        }
        return false;
    }

typedef LONG NTSTATUS;
typedef NTSTATUS(WINAPI* RtlGetVersionFunc)(PRTL_OSVERSIONINFOW);

    std::wstring GetWindowsVersionName() {
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            RtlGetVersionFunc pRtlGetVersion = (RtlGetVersionFunc)GetProcAddress(hNtdll, "RtlGetVersion");
            if (pRtlGetVersion) {
                OSVERSIONINFOEXW osvi = { 0 };
                osvi.dwOSVersionInfoSize = sizeof(osvi);
                if (pRtlGetVersion((PRTL_OSVERSIONINFOW)&osvi) == 0) {
                    std::wstringstream ss;
                    if (osvi.dwMajorVersion == 10 && osvi.dwMinorVersion == 0) {
                        if (osvi.dwBuildNumber >= 22000) {
                            ss << L"Windows 11 (Build " << osvi.dwBuildNumber << L")";
                        } else {
                            ss << L"Windows 10 (Build " << osvi.dwBuildNumber << L")";
                        }
                    } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 3) {
                        ss << L"Windows 8.1 (Build " << osvi.dwBuildNumber << L")";
                    } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 2) {
                        ss << L"Windows 8 (Build " << osvi.dwBuildNumber << L")";
                    } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 1) {
                        ss << L"Windows 7 (Build " << osvi.dwBuildNumber << L")";
                    } else {
                        ss << L"Windows " << osvi.dwMajorVersion << L"." << osvi.dwMinorVersion 
                           << L" (Build " << osvi.dwBuildNumber << L")";
                    }
                    return ss.str();
                }
            }
        }
        return L"Windows (Detected)";
    }

    std::wstring GetHostsFilePath() {
        wchar_t sysDir[MAX_PATH];
        if (GetSystemDirectoryW(sysDir, MAX_PATH) > 0) {
            std::wstring p(sysDir);
            if (!p.empty() && p.back() != L'\\') p += L'\\';
            p += L"drivers\\etc\\hosts";
            return p;
        }
        return L"C:\\Windows\\System32\\drivers\\etc\\hosts";
    }

    typedef BOOL(WINAPI* DnsFlushResolverCacheFunc)();

    bool FlushDnsCache() {
        bool flushed = false;
        HMODULE hDnsApi = LoadLibraryW(L"dnsapi.dll");
        if (hDnsApi) {
            DnsFlushResolverCacheFunc pDnsFlush = 
                (DnsFlushResolverCacheFunc)GetProcAddress(hDnsApi, "DnsFlushResolverCache");
            if (pDnsFlush) {
                flushed = (pDnsFlush() != FALSE);
            }
            FreeLibrary(hDnsApi);
        }

        // Also run ipconfig /flushdns silently to guarantee all subsystems refresh
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        wchar_t cmd[] = L"cmd.exe /c ipconfig /flushdns";
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 3000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            flushed = true;
        }

        return flushed;
    }

    bool OpenInNotepad(const std::wstring& filePath) {
        HINSTANCE hInst = ShellExecuteW(NULL, L"open", L"notepad.exe", filePath.c_str(), NULL, SW_SHOWNORMAL);
        return ((INT_PTR)hInst > 32);
    }

} // namespace WinUtil
