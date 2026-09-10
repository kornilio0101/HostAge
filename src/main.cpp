#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

#include "resource.h"
#include "hosts_manager.h"
#include "win_util.h"
#include "ui_theme.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comdlg32.lib")

// Control IDs
#define IDC_EDIT_IP          1001
#define IDC_EDIT_DOMAIN      1002
#define IDC_EDIT_COMMENT     1003
#define IDC_BTN_ADD          1004
#define IDC_EDIT_SEARCH      1005
#define IDC_EDIT_GROUP       1008

#define IDM_RESTORE_BASE      2000
#define IDM_GROUP_FILTER_BASE 3000
#define IDM_ASSIGN_BASE       5000

// Navigation Sections
enum class NavSection {
    AllEntries = 0,
    Active,
    Disabled,
    Groups,
    Settings,
    Logs
};

// Filter modes
enum class FilterMode {
    All,
    ActiveOnly,
    DisabledOnly
};

enum class ListRowType {
    GroupHeader,
    HostCard
};

struct ListRow {
    ListRowType type;
    std::wstring groupName;
    int hostItemId;
};

struct LogEntry {
    std::wstring timeStr;
    std::wstring message;
};

// Global App State
struct AppState {
    HINSTANCE hInstance;
    HWND hWndMain;
    
    // Child controls
    HWND hEditIp;
    HWND hEditDomain;
    HWND hEditGroup;
    HWND hEditComment;
    HWND hBtnAdd;
    HWND hEditSearch;

    HFONT hFontRegular;
    HFONT hFontBold;
    HFONT hFontSmall;

    HostsManager hosts;
    std::wstring osVersion;
    bool isAdmin;

    NavSection currentNav;
    FilterMode filter;
    std::wstring selectedGroupFilter; // Empty = All Groups
    std::wstring searchQuery;
    std::wstring statusToast;
    DWORD toastTimer;

    // Scrolling & Layout
    int scrollOffset;
    int maxScroll;
    int hoveredRowIndex;
    int hoveredRowBtn; // 0=None, 1=Toggle, 2=Edit, 3=Delete
    int hoveredNav;    // 0=None, 1=All, 2=Active, 3=Disabled, 4=Groups, 5=Settings, 6=Logs
    int hoveredAction; // 0=None, 1=Import, 2=Export, 3=Backup, 4=Restore, 5=FlushDns
    int hoveredTitleBtn;// 0=None, 1=Min, 2=Max, 3=Close
    int activeEditId;

    // Filtered list rows
    std::vector<ListRow> visibleRows;

    // Activity Logs
    std::vector<LogEntry> activityLogs;

    // Dynamic hit rects
    Gdiplus::RectF rcImport;
    Gdiplus::RectF rcExport;
    Gdiplus::RectF rcBackup;
    Gdiplus::RectF rcRestore;
    Gdiplus::RectF rcFlushDns;

    Gdiplus::RectF rcMinBtn;
    Gdiplus::RectF rcMaxBtn;
    Gdiplus::RectF rcCloseBtn;

    ULONG_PTR gdiplusToken;
};

static AppState g_app;

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void OpenEditWindow(HWND hWndParent, int itemId);
void OpenManageGroupsWindow(HWND hWndParent);
void OpenSettingsWindow(HWND hWndParent);
void OpenLogsWindow(HWND hWndParent);
void ShowGroupFilterMenu(HWND hWnd);
void ShowAssignGroupMenu(HWND hWnd, int itemId);
void ShowBackupsMenu(HWND hWnd);
void UpdateFilteredList();
void ShowToast(const std::wstring& msg);
void AddLog(const std::wstring& msg);
void RepositionControls(int width, int height);

void AddLog(const std::wstring& msg) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timeBuf[32];
    swprintf_s(timeBuf, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);

    LogEntry entry;
    entry.timeStr = timeBuf;
    entry.message = msg;
    g_app.activityLogs.push_back(entry);
}

static std::wstring ToLower(const std::wstring& s) {
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::towlower);
    return r;
}

static inline int GetRowHeight(const ListRow& row) {
    return (row.type == ListRowType::GroupHeader) ? 36 : 50;
}

void UpdateFilteredList() {
    g_app.visibleRows.clear();
    std::wstring query = ToLower(g_app.searchQuery);

    std::vector<std::wstring> allGroups = g_app.hosts.GetGroups();

    for (const auto& grp : allGroups) {
        if (!g_app.selectedGroupFilter.empty() && grp != g_app.selectedGroupFilter) {
            continue;
        }

        std::vector<int> groupItemIds;
        for (const auto& item : g_app.hosts.GetItems()) {
            std::wstring itemGrp = item.group.empty() ? L"General" : item.group;
            if (itemGrp != grp) continue;

            if (g_app.filter == FilterMode::ActiveOnly && !item.enabled) continue;
            if (g_app.filter == FilterMode::DisabledOnly && item.enabled) continue;

            if (!query.empty()) {
                std::wstring ipL = ToLower(item.ip);
                std::wstring domL = ToLower(item.domain);
                std::wstring comL = ToLower(item.comment);
                std::wstring grpL = ToLower(itemGrp);
                if (ipL.find(query) == std::wstring::npos &&
                    domL.find(query) == std::wstring::npos &&
                    comL.find(query) == std::wstring::npos &&
                    grpL.find(query) == std::wstring::npos)
                {
                    continue;
                }
            }
            groupItemIds.push_back(item.id);
        }

        if (!groupItemIds.empty()) {
            // Only add group header if in Groups view
            if (g_app.currentNav == NavSection::Groups) {
                ListRow hr;
                hr.type = ListRowType::GroupHeader;
                hr.groupName = grp;
                hr.hostItemId = 0;
                g_app.visibleRows.push_back(hr);
            }

            for (int id : groupItemIds) {
                ListRow ir;
                ir.type = ListRowType::HostCard;
                ir.groupName = grp;
                ir.hostItemId = id;
                g_app.visibleRows.push_back(ir);
            }
        }
    }
}

void ShowToast(const std::wstring& msg) {
    g_app.statusToast = msg;
    g_app.toastTimer = GetTickCount();
    InvalidateRect(g_app.hWndMain, NULL, FALSE);
}

// Subclass edit controls for modern dark appearance & Enter key
static WNDPROC g_OldEditProc = NULL;
static LRESULT CALLBACK DarkEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        if (hWnd == g_app.hEditIp || hWnd == g_app.hEditDomain) {
            SendMessage(g_app.hWndMain, WM_COMMAND, IDC_BTN_ADD, 0);
            return 0;
        }
    }
    return CallWindowProc(g_OldEditProc, hWnd, msg, wParam, lParam);
}

// ============================================================================
// Modal Edit Window (Edit IP, Domain, Group, Comment, Status)
// ============================================================================
static HWND g_hEditModal = NULL;
static HWND g_hModalIp = NULL;
static HWND g_hModalDomain = NULL;
static HWND g_hModalGroup = NULL;
static HWND g_hModalComment = NULL;
static HWND g_hModalCheck = NULL;

static LRESULT CALLBACK ModalEditWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = g_app.hFontRegular;

        CreateWindowW(L"STATIC", L"IP Address:", WS_CHILD | WS_VISIBLE, 25, 18, 100, 18, hWnd, NULL, g_app.hInstance, NULL);
        g_hModalIp = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 25, 38, 330, 24, hWnd, NULL, g_app.hInstance, NULL);

        CreateWindowW(L"STATIC", L"Domain Name(s):", WS_CHILD | WS_VISIBLE, 25, 70, 120, 18, hWnd, NULL, g_app.hInstance, NULL);
        g_hModalDomain = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 25, 90, 330, 24, hWnd, NULL, g_app.hInstance, NULL);

        CreateWindowW(L"STATIC", L"Group Category:", WS_CHILD | WS_VISIBLE, 25, 122, 120, 18, hWnd, NULL, g_app.hInstance, NULL);
        g_hModalGroup = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL, 25, 142, 330, 180, hWnd, NULL, g_app.hInstance, NULL);

        CreateWindowW(L"STATIC", L"Comment (Optional):", WS_CHILD | WS_VISIBLE, 25, 174, 140, 18, hWnd, NULL, g_app.hInstance, NULL);
        g_hModalComment = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 25, 194, 330, 24, hWnd, NULL, g_app.hInstance, NULL);

        g_hModalCheck = CreateWindowW(L"BUTTON", L"Enabled (Active routing)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 25, 230, 250, 22, hWnd, NULL, g_app.hInstance, NULL);

        HWND hBtnSave = CreateWindowW(L"BUTTON", L"Save Changes", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 120, 275, 110, 28, hWnd, (HMENU)IDOK, g_app.hInstance, NULL);
        HWND hBtnCancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 245, 275, 110, 28, hWnd, (HMENU)IDCANCEL, g_app.hInstance, NULL);

        SendMessage(g_hModalIp, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hModalDomain, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hModalGroup, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hModalComment, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hModalCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnSave, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);
        SendMessage(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Populate groups into combobox
        auto groups = g_app.hosts.GetGroups();
        for (const auto& g : groups) {
            SendMessageW(g_hModalGroup, CB_ADDSTRING, 0, (LPARAM)g.c_str());
        }

        // Populate current values
        for (const auto& item : g_app.hosts.GetItems()) {
            if (item.id == g_app.activeEditId) {
                SetWindowTextW(g_hModalIp, item.ip.c_str());
                SetWindowTextW(g_hModalDomain, item.domain.c_str());
                std::wstring grp = item.group.empty() ? L"General" : item.group;
                SetWindowTextW(g_hModalGroup, grp.c_str());
                SetWindowTextW(g_hModalComment, item.comment.c_str());
                Button_SetCheck(g_hModalCheck, item.enabled ? BST_CHECKED : BST_UNCHECKED);
                break;
            }
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(220, 225, 235));
        SetBkColor(hdc, RGB(24, 26, 36));
        static HBRUSH hbr = CreateSolidBrush(RGB(24, 26, 36));
        return (INT_PTR)hbr;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK) {
            wchar_t szIp[256] = {0};
            wchar_t szDomain[1024] = {0};
            wchar_t szGroup[256] = {0};
            wchar_t szComment[1024] = {0};
            GetWindowTextW(g_hModalIp, szIp, 256);
            GetWindowTextW(g_hModalDomain, szDomain, 1024);
            GetWindowTextW(g_hModalGroup, szGroup, 256);
            GetWindowTextW(g_hModalComment, szComment, 1024);
            bool enabled = (Button_GetCheck(g_hModalCheck) == BST_CHECKED);

            if (!HostsManager::IsValidIp(szIp)) {
                MessageBoxW(hWnd, L"Please enter a valid IP address (e.g. 127.0.0.1 or 0.0.0.0).", L"Validation Error", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (!HostsManager::IsValidDomain(szDomain)) {
                MessageBoxW(hWnd, L"Please enter a valid domain name.", L"Validation Error", MB_OK | MB_ICONWARNING);
                return 0;
            }

            g_app.hosts.UpdateItem(g_app.activeEditId, szIp, szDomain, szComment, szGroup);
            g_app.hosts.SetItemEnabled(g_app.activeEditId, enabled);
            
            std::wstring saveErr;
            g_app.hosts.Save(true, saveErr);
            WinUtil::FlushDnsCache();
            AddLog(L"Updated entry '" + std::wstring(szDomain) + L"' -> " + szIp);

            UpdateFilteredList();
            ShowToast(L"Entry updated & DNS flushed!");
            EnableWindow(g_app.hWndMain, TRUE);
            DestroyWindow(hWnd);
            g_hEditModal = NULL;
            InvalidateRect(g_app.hWndMain, NULL, FALSE);
            return 0;
        } else if (id == IDCANCEL) {
            EnableWindow(g_app.hWndMain, TRUE);
            DestroyWindow(hWnd);
            g_hEditModal = NULL;
            return 0;
        }
        break;
    }
    case WM_CLOSE: {
        EnableWindow(g_app.hWndMain, TRUE);
        DestroyWindow(hWnd);
        g_hEditModal = NULL;
        InvalidateRect(g_app.hWndMain, NULL, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void OpenEditWindow(HWND hWndParent, int itemId) {
    if (g_hEditModal) return;
    g_app.activeEditId = itemId;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = ModalEditWndProc;
        wc.hInstance = g_app.hInstance;
        wc.hbrBackground = CreateSolidBrush(RGB(24, 26, 36));
        wc.lpszClassName = L"HostageEditModal";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rcParent;
    GetWindowRect(hWndParent, &rcParent);
    int w = 395;
    int h = 360;
    int x = rcParent.left + (rcParent.right - rcParent.left - w) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - h) / 2;

    EnableWindow(hWndParent, FALSE);
    g_hEditModal = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"HostageEditModal",
        L"Edit Hosts Entry",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        hWndParent, NULL, g_app.hInstance, NULL
    );
}

// ============================================================================
// Manage Groups Modal Window (Add / Rename / Remove Groups)
// ============================================================================
static HWND g_hManageGroupsModal = NULL;
static HWND g_hListGroups = NULL;
static HWND g_hEditNewGroup = NULL;
static HWND g_hEditRenameGroup = NULL;

static void RefreshGroupListbox(HWND hListBox) {
    SendMessage(hListBox, LB_RESETCONTENT, 0, 0);
    auto groups = g_app.hosts.GetGroups();
    for (const auto& g : groups) {
        size_t count = g_app.hosts.GetGroupCount(g);
        std::wstring itemText = g + L"  (" + std::to_wstring(count) + L" domains)";
        SendMessageW(hListBox, LB_ADDSTRING, 0, (LPARAM)itemText.c_str());
    }
}

static LRESULT CALLBACK ManageGroupsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = g_app.hFontRegular;
        HFONT hFontB = g_app.hFontBold;

        CreateWindowW(L"STATIC", L"Active Domain Groups:", WS_CHILD | WS_VISIBLE, 20, 14, 200, 18, hWnd, NULL, g_app.hInstance, NULL);
        g_hListGroups = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, 20, 36, 240, 300, hWnd, (HMENU)9001, g_app.hInstance, NULL);

        // Section: Add New Group
        CreateWindowW(L"STATIC", L"Add New Group:", WS_CHILD | WS_VISIBLE, 280, 20, 180, 18, hWnd, NULL, g_app.hInstance, NULL);
        g_hEditNewGroup = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 280, 42, 175, 24, hWnd, NULL, g_app.hInstance, NULL);
        HWND hBtnAdd = CreateWindowW(L"BUTTON", L"+ Add Group", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 280, 72, 175, 26, hWnd, (HMENU)9002, g_app.hInstance, NULL);

        // Section: Rename Group
        CreateWindowW(L"STATIC", L"Rename Selected Group:", WS_CHILD | WS_VISIBLE, 280, 120, 180, 18, hWnd, NULL, g_app.hInstance, NULL);
        g_hEditRenameGroup = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 280, 142, 175, 24, hWnd, NULL, g_app.hInstance, NULL);
        HWND hBtnRename = CreateWindowW(L"BUTTON", L"Rename Group", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 280, 172, 175, 26, hWnd, (HMENU)9003, g_app.hInstance, NULL);

        // Section: Remove Group
        CreateWindowW(L"STATIC", L"Remove Selected Group:", WS_CHILD | WS_VISIBLE, 280, 220, 180, 18, hWnd, NULL, g_app.hInstance, NULL);
        CreateWindowW(L"STATIC", L"(Domains revert to Ungrouped)", WS_CHILD | WS_VISIBLE, 280, 238, 180, 16, hWnd, NULL, g_app.hInstance, NULL);
        HWND hBtnRemove = CreateWindowW(L"BUTTON", L"Remove Group", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 280, 260, 175, 26, hWnd, (HMENU)9004, g_app.hInstance, NULL);

        HWND hBtnClose = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 340, 340, 115, 28, hWnd, (HMENU)IDOK, g_app.hInstance, NULL);

        SendMessage(g_hListGroups, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hEditNewGroup, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hEditRenameGroup, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnAdd, WM_SETFONT, (WPARAM)hFontB, TRUE);
        SendMessage(hBtnRename, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnRemove, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnClose, WM_SETFONT, (WPARAM)hFontB, TRUE);

        SendMessageW(g_hEditNewGroup, 0x1501, TRUE, (LPARAM)L"e.g. Staging");
        SendMessageW(g_hEditRenameGroup, 0x1501, TRUE, (LPARAM)L"New name");

        RefreshGroupListbox(g_hListGroups);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(220, 225, 235));
        SetBkColor(hdc, RGB(24, 26, 36));
        static HBRUSH hbr = CreateSolidBrush(RGB(24, 26, 36));
        return (INT_PTR)hbr;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == 9001 && code == LBN_SELCHANGE) {
            int sel = (int)SendMessage(g_hListGroups, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                auto groups = g_app.hosts.GetGroups();
                if (sel >= 0 && sel < (int)groups.size()) {
                    SetWindowTextW(g_hEditRenameGroup, groups[sel].c_str());
                }
            }
            return 0;
        }

        if (id == 9002) {
            wchar_t szNew[256] = {0};
            GetWindowTextW(g_hEditNewGroup, szNew, 256);
            std::wstring newGrp = szNew;
            while (!newGrp.empty() && iswspace(newGrp.front())) newGrp.erase(newGrp.begin());
            while (!newGrp.empty() && iswspace(newGrp.back())) newGrp.pop_back();

            if (newGrp.empty()) {
                MessageBoxW(hWnd, L"Please enter a valid group name.", L"Input Required", MB_OK | MB_ICONWARNING);
                return 0;
            }

            if (g_app.hosts.AddGroup(newGrp)) {
                RefreshGroupListbox(g_hListGroups);
                SetWindowTextW(g_hEditNewGroup, L"");
                UpdateFilteredList();
                ShowToast(L"Group '" + newGrp + L"' created.");
                AddLog(L"Created new group: " + newGrp);
            } else {
                MessageBoxW(hWnd, L"A group with that name already exists.", L"Duplicate Group", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        if (id == 9003) {
            int sel = (int)SendMessage(g_hListGroups, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBoxW(hWnd, L"Please select a group to rename from the list.", L"Selection Required", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            auto groups = g_app.hosts.GetGroups();
            if (sel < 0 || sel >= (int)groups.size()) return 0;
            std::wstring oldName = groups[sel];

            if (oldName == L"Ungrouped") {
                MessageBoxW(hWnd, L"The default 'Ungrouped' category cannot be renamed.", L"Notice", MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            wchar_t szRename[256] = {0};
            GetWindowTextW(g_hEditRenameGroup, szRename, 256);
            std::wstring newName = szRename;
            while (!newName.empty() && iswspace(newName.front())) newName.erase(newName.begin());
            while (!newName.empty() && iswspace(newName.back())) newName.pop_back();

            if (newName.empty() || newName == oldName) return 0;

            if (g_app.hosts.RenameGroup(oldName, newName)) {
                RefreshGroupListbox(g_hListGroups);
                UpdateFilteredList();
                ShowToast(L"Group renamed to '" + newName + L"'.");
                AddLog(L"Renamed group '" + oldName + L"' to '" + newName + L"'");
            } else {
                MessageBoxW(hWnd, L"Cannot rename: a group with that name already exists.", L"Rename Error", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }

        if (id == 9004) {
            int sel = (int)SendMessage(g_hListGroups, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBoxW(hWnd, L"Please select a group to remove from the list.", L"Selection Required", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            auto groups = g_app.hosts.GetGroups();
            if (sel < 0 || sel >= (int)groups.size()) return 0;
            std::wstring groupToRemove = groups[sel];

            if (groupToRemove == L"Ungrouped") {
                MessageBoxW(hWnd, L"The default 'Ungrouped' category cannot be removed.", L"Notice", MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            size_t count = g_app.hosts.GetGroupCount(groupToRemove);
            std::wstringstream confirmMsg;
            confirmMsg << L"Are you sure you want to remove the group '" << groupToRemove << L"'?\n\n"
                       << L"All " << count << L" domain(s) and IP(s) in this group will revert to 'Ungrouped' and will NOT be deleted.";

            if (MessageBoxW(hWnd, confirmMsg.str().c_str(), L"Confirm Remove Group", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                if (g_app.hosts.RemoveGroup(groupToRemove)) {
                    RefreshGroupListbox(g_hListGroups);
                    SetWindowTextW(g_hEditRenameGroup, L"");
                    UpdateFilteredList();
                    ShowToast(L"Group '" + groupToRemove + L"' removed. Domains reverted to Ungrouped.");
                    AddLog(L"Removed group '" + groupToRemove + L"'; domains reverted to Ungrouped");
                }
            }
            return 0;
        }

        if (id == IDOK || id == IDCANCEL) {
            EnableWindow(g_app.hWndMain, TRUE);
            DestroyWindow(hWnd);
            g_hManageGroupsModal = NULL;
            InvalidateRect(g_app.hWndMain, NULL, FALSE);
            return 0;
        }
        break;
    }
    case WM_CLOSE: {
        EnableWindow(g_app.hWndMain, TRUE);
        DestroyWindow(hWnd);
        g_hManageGroupsModal = NULL;
        InvalidateRect(g_app.hWndMain, NULL, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void OpenManageGroupsWindow(HWND hWndParent) {
    if (g_hManageGroupsModal) return;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = ManageGroupsWndProc;
        wc.hInstance = g_app.hInstance;
        wc.hbrBackground = CreateSolidBrush(RGB(24, 26, 36));
        wc.lpszClassName = L"HostageManageGroupsModal";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rcParent;
    GetWindowRect(hWndParent, &rcParent);
    int w = 490;
    int h = 420;
    int x = rcParent.left + (rcParent.right - rcParent.left - w) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - h) / 2;

    EnableWindow(hWndParent, FALSE);
    g_hManageGroupsModal = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"HostageManageGroupsModal",
        L"Manage Groups",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        hWndParent, NULL, g_app.hInstance, NULL
    );
}

// ============================================================================
// Settings Modal Window
// ============================================================================
static HWND g_hSettingsModal = NULL;
static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = g_app.hFontRegular;
        HFONT hFontB = g_app.hFontBold;

        CreateWindowW(L"STATIC", L"HostAge Configuration & Environment", WS_CHILD | WS_VISIBLE, 25, 18, 350, 20, hWnd, NULL, g_app.hInstance, NULL);

        CreateWindowW(L"STATIC", L"Hosts File Location:", WS_CHILD | WS_VISIBLE, 25, 48, 200, 16, hWnd, NULL, g_app.hInstance, NULL);
        HWND hEditPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_app.hosts.GetFilePath().c_str(), WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL, 25, 68, 380, 24, hWnd, NULL, g_app.hInstance, NULL);

        HWND hBtnNotepad = CreateWindowW(L"BUTTON", L"Open in Notepad", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 25, 104, 130, 26, hWnd, (HMENU)8001, g_app.hInstance, NULL);
        HWND hBtnFolder = CreateWindowW(L"BUTTON", L"Open Hosts Folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 165, 104, 140, 26, hWnd, (HMENU)8002, g_app.hInstance, NULL);
        HWND hBtnFlush = CreateWindowW(L"BUTTON", L"Flush DNS", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 315, 104, 90, 26, hWnd, (HMENU)8003, g_app.hInstance, NULL);

        CreateWindowW(L"STATIC", L"Operating System:", WS_CHILD | WS_VISIBLE, 25, 150, 130, 16, hWnd, NULL, g_app.hInstance, NULL);
        std::wstring osInfo = g_app.osVersion + L" (" + (g_app.isAdmin ? L"Administrator Privileges" : L"Standard User") + L")";
        CreateWindowW(L"STATIC", osInfo.c_str(), WS_CHILD | WS_VISIBLE, 25, 168, 380, 16, hWnd, NULL, g_app.hInstance, NULL);

        CreateWindowW(L"STATIC", L"HostAge Version: v1.3.0 (Portable Native Win32 C++)", WS_CHILD | WS_VISIBLE, 25, 200, 380, 16, hWnd, NULL, g_app.hInstance, NULL);

        HWND hBtnClose = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 295, 240, 110, 28, hWnd, (HMENU)IDOK, g_app.hInstance, NULL);

        SendMessage(hEditPath, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnNotepad, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnFolder, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnFlush, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnClose, WM_SETFONT, (WPARAM)hFontB, TRUE);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(220, 225, 235));
        SetBkColor(hdc, RGB(24, 26, 36));
        static HBRUSH hbr = CreateSolidBrush(RGB(24, 26, 36));
        return (INT_PTR)hbr;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == 8001) {
            WinUtil::OpenInNotepad(g_app.hosts.GetFilePath());
            AddLog(L"Opened hosts file in Notepad");
            return 0;
        }
        if (id == 8002) {
            ShellExecuteW(NULL, L"open", L"explorer.exe", L"/select,C:\\Windows\\System32\\drivers\\etc\\hosts", NULL, SW_SHOWNORMAL);
            AddLog(L"Opened hosts directory in Explorer");
            return 0;
        }
        if (id == 8003) {
            WinUtil::FlushDnsCache();
            ShowToast(L"DNS Cache Flushed Successfully!");
            AddLog(L"Manually flushed DNS resolver cache");
            return 0;
        }
        if (id == IDOK || id == IDCANCEL) {
            EnableWindow(g_app.hWndMain, TRUE);
            DestroyWindow(hWnd);
            g_hSettingsModal = NULL;
            return 0;
        }
        break;
    }
    case WM_CLOSE: {
        EnableWindow(g_app.hWndMain, TRUE);
        DestroyWindow(hWnd);
        g_hSettingsModal = NULL;
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void OpenSettingsWindow(HWND hWndParent) {
    if (g_hSettingsModal) return;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = g_app.hInstance;
        wc.hbrBackground = CreateSolidBrush(RGB(24, 26, 36));
        wc.lpszClassName = L"HostageSettingsModal";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rcParent;
    GetWindowRect(hWndParent, &rcParent);
    int w = 445;
    int h = 320;
    int x = rcParent.left + (rcParent.right - rcParent.left - w) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - h) / 2;

    EnableWindow(hWndParent, FALSE);
    g_hSettingsModal = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"HostageSettingsModal",
        L"Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        hWndParent, NULL, g_app.hInstance, NULL
    );
}

// ============================================================================
// Activity Logs Modal Window
// ============================================================================
static HWND g_hLogsModal = NULL;
static HWND g_hEditLogs = NULL;

static void RefreshLogsText(HWND hEdit) {
    std::wstring allText;
    for (const auto& l : g_app.activityLogs) {
        allText += l.timeStr + l.message + L"\r\n";
    }
    SetWindowTextW(hEdit, allText.c_str());
    SendMessage(hEdit, EM_SETSEL, (WPARAM)allText.size(), (LPARAM)allText.size());
    SendMessage(hEdit, EM_SCROLLCARET, 0, 0);
}

static LRESULT CALLBACK LogsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = g_app.hFontSmall;
        HFONT hFontB = g_app.hFontBold;

        CreateWindowW(L"STATIC", L"HostAge Activity & Event Log:", WS_CHILD | WS_VISIBLE, 20, 14, 300, 18, hWnd, NULL, g_app.hInstance, NULL);

        g_hEditLogs = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            20, 36, 520, 310,
            hWnd, NULL, g_app.hInstance, NULL
        );

        HWND hBtnClear = CreateWindowW(L"BUTTON", L"Clear Logs", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 360, 110, 28, hWnd, (HMENU)7001, g_app.hInstance, NULL);
        HWND hBtnClose = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 430, 360, 110, 28, hWnd, (HMENU)IDOK, g_app.hInstance, NULL);

        SendMessage(g_hEditLogs, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnClear, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnClose, WM_SETFONT, (WPARAM)hFontB, TRUE);

        RefreshLogsText(g_hEditLogs);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(220, 225, 235));
        SetBkColor(hdc, RGB(24, 26, 36));
        static HBRUSH hbr = CreateSolidBrush(RGB(24, 26, 36));
        return (INT_PTR)hbr;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == 7001) {
            g_app.activityLogs.clear();
            AddLog(L"Activity log cleared");
            RefreshLogsText(g_hEditLogs);
            return 0;
        }
        if (id == IDOK || id == IDCANCEL) {
            EnableWindow(g_app.hWndMain, TRUE);
            DestroyWindow(hWnd);
            g_hLogsModal = NULL;
            return 0;
        }
        break;
    }
    case WM_CLOSE: {
        EnableWindow(g_app.hWndMain, TRUE);
        DestroyWindow(hWnd);
        g_hLogsModal = NULL;
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void OpenLogsWindow(HWND hWndParent) {
    if (g_hLogsModal) return;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = LogsWndProc;
        wc.hInstance = g_app.hInstance;
        wc.hbrBackground = CreateSolidBrush(RGB(24, 26, 36));
        wc.lpszClassName = L"HostageLogsModal";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rcParent;
    GetWindowRect(hWndParent, &rcParent);
    int w = 575;
    int h = 440;
    int x = rcParent.left + (rcParent.right - rcParent.left - w) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - h) / 2;

    EnableWindow(hWndParent, FALSE);
    g_hLogsModal = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"HostageLogsModal",
        L"Activity Logs",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        hWndParent, NULL, g_app.hInstance, NULL
    );
}

// Show Backups Popup Menu
void ShowBackupsMenu(HWND hWnd) {
    auto backups = g_app.hosts.GetAvailableBackups();
    HMENU hMenu = CreatePopupMenu();

    if (backups.empty()) {
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"No backups found");
    } else {
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"Select backup to restore:");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        for (size_t i = 0; i < backups.size() && i < 15; ++i) {
            std::wstring label = L"Restore " + backups[i].timestamp + L" (" + backups[i].filename + L")";
            AppendMenuW(hMenu, MF_STRING, IDM_RESTORE_BASE + (UINT)i, label.c_str());
        }
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_RESTORE_BASE + 999, L"Create New Backup Now");

    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == IDM_RESTORE_BASE + 999) {
        std::wstring bPath, err;
        if (g_app.hosts.CreateBackupNow(bPath, err)) {
            ShowToast(L"Backup created successfully.");
            AddLog(L"Created backup: " + bPath);
        } else {
            MessageBoxW(hWnd, err.c_str(), L"Backup Error", MB_OK | MB_ICONERROR);
        }
    } else if (cmd >= IDM_RESTORE_BASE && cmd < IDM_RESTORE_BASE + (int)backups.size()) {
        size_t idx = cmd - IDM_RESTORE_BASE;
        std::wstring confirmMsg = L"Restore hosts file from " + backups[idx].filename + L"? Current changes will be overwritten.";
        if (MessageBoxW(hWnd, confirmMsg.c_str(), L"Confirm Restore", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            std::wstring err;
            if (g_app.hosts.RestoreBackup(backups[idx].fullPath, err)) {
                UpdateFilteredList();
                WinUtil::FlushDnsCache();
                ShowToast(L"Restored from backup & DNS flushed!");
                AddLog(L"Restored hosts file from: " + backups[idx].filename);
            } else {
                MessageBoxW(hWnd, err.c_str(), L"Restore Error", MB_OK | MB_ICONERROR);
            }
        }
    }
}

// -------------------------------------------------------------
// Paint the entire UI matching the Target Mockup
// -------------------------------------------------------------
void PaintUI(HDC hdc, const RECT& clientRect) {
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) return;

    // Create memory DC and bitmap for flicker-free double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBM = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBM = SelectObject(memDC, memBM);

    {
        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // 1. Canvas Background
        Gdiplus::SolidBrush bgBrush(UITheme::Colors::BgCanvas);
        g.FillRectangle(&bgBrush, 0, 0, width, height);

        // Fonts
        Gdiplus::Font fontTitle(L"Segoe UI", 16.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontRegular(L"Segoe UI", 13.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font fontBold(L"Segoe UI", 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontDomain(L"Segoe UI", 13.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontMono(L"Segoe UI", 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font fontSmall(L"Segoe UI", 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font fontBadge(L"Segoe UI", 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        Gdiplus::StringFormat sfCenter;
        sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
        sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        // ----------------------------------------------------
        // 2. Header Bar (y: 0 to 54)
        // ----------------------------------------------------
        Gdiplus::SolidBrush headerBg(UITheme::Colors::BgHeader);
        g.FillRectangle(&headerBg, 0, 0, width, 54);
        Gdiplus::Pen headerBorder(UITheme::Colors::BorderDark, 1.0f);
        g.DrawLine(&headerBorder, 0, 54, width, 54);

        // Logo
        UITheme::DrawServerStackLogo(g, 18.0f, 11.0f, 32.0f);

        // Title "HOSTAGE"
        Gdiplus::SolidBrush textWhite(UITheme::Colors::TextPrimary);
        g.DrawString(L"HOSTAGE", -1, &fontTitle, Gdiplus::PointF(58.0f, 16.0f), &textWhite);

        // Administrator Badge
        UITheme::DrawBadge(g, L"✔ ADMINISTRATOR", 168.0f, 16.0f, UITheme::Colors::ActiveGreen, UITheme::Colors::ActiveGreenBg, &fontBadge, 10, 4, UITheme::Colors::ActiveGreen);

        // Window Control Buttons (Right of Header)
        float winCtrlRight = (float)width - 12.0f;
        g_app.rcCloseBtn = Gdiplus::RectF(winCtrlRight - 28.0f, 12.0f, 28.0f, 28.0f);
        g_app.rcMaxBtn   = Gdiplus::RectF(winCtrlRight - 60.0f, 12.0f, 28.0f, 28.0f);
        g_app.rcMinBtn   = Gdiplus::RectF(winCtrlRight - 92.0f, 12.0f, 28.0f, 28.0f);

        // Close Button
        if (g_app.hoveredTitleBtn == 3) {
            UITheme::DrawRoundedRect(g, g_app.rcCloseBtn, 4.0f, Gdiplus::Color(255, 225, 29, 72), Gdiplus::Color(0,0,0,0), 0);
        }
        Gdiplus::Pen ctrlPen(UITheme::Colors::TextSecondary, 1.4f);
        g.DrawLine(&ctrlPen, g_app.rcCloseBtn.X + 9, g_app.rcCloseBtn.Y + 9, g_app.rcCloseBtn.X + 19, g_app.rcCloseBtn.Y + 19);
        g.DrawLine(&ctrlPen, g_app.rcCloseBtn.X + 19, g_app.rcCloseBtn.Y + 9, g_app.rcCloseBtn.X + 9, g_app.rcCloseBtn.Y + 19);

        // Maximize Button
        if (g_app.hoveredTitleBtn == 2) {
            UITheme::DrawRoundedRect(g, g_app.rcMaxBtn, 4.0f, UITheme::Colors::BgCardHover, Gdiplus::Color(0,0,0,0), 0);
        }
        g.DrawRectangle(&ctrlPen, g_app.rcMaxBtn.X + 8.0f, g_app.rcMaxBtn.Y + 8.0f, 11.0f, 11.0f);

        // Minimize Button
        if (g_app.hoveredTitleBtn == 1) {
            UITheme::DrawRoundedRect(g, g_app.rcMinBtn, 4.0f, UITheme::Colors::BgCardHover, Gdiplus::Color(0,0,0,0), 0);
        }
        g.DrawLine(&ctrlPen, g_app.rcMinBtn.X + 8, g_app.rcMinBtn.Y + 16, g_app.rcMinBtn.X + 20, g_app.rcMinBtn.Y + 16);

        // Windows OS Pill Badge (left of controls)
        float osX = winCtrlRight - 104.0f - 110.0f;
        Gdiplus::RectF osPill(osX, 15.0f, 106.0f, 24.0f);
        UITheme::DrawRoundedRect(g, osPill, 12.0f, Gdiplus::Color(255, 26, 32, 48), Gdiplus::Color(255, 42, 52, 76), 1.0f);
        UITheme::DrawWindowsLogo(g, osX + 10.0f, 20.0f, 13.0f);
        g.DrawString(g_app.osVersion.c_str(), -1, &fontSmall, Gdiplus::PointF(osX + 28.0f, 18.0f), &textWhite);

        // ----------------------------------------------------
        // 3. Left Navigation Sidebar (x: 0 to 88, y: 54 to height)
        // ----------------------------------------------------
        Gdiplus::SolidBrush sideBg(UITheme::Colors::BgSidebar);
        g.FillRectangle(&sideBg, 0, 54, 88, height - 54);
        g.DrawLine(&headerBorder, 88, 54, 88, height);

        auto drawNavItem = [&](NavSection nav, int hoverCode, float y, const std::wstring& label, int iconType) {
            bool isCurrent = (g_app.currentNav == nav);
            bool isHovered = (g_app.hoveredNav == hoverCode);
            Gdiplus::RectF itemRect(8.0f, y, 72.0f, 54.0f);

            if (isCurrent) {
                UITheme::DrawRoundedRect(g, itemRect, 8.0f, UITheme::Colors::BgSidebarActive, UITheme::Colors::BorderDark, 1.0f);
            } else if (isHovered) {
                UITheme::DrawRoundedRect(g, itemRect, 8.0f, UITheme::Colors::BgSidebarHover, Gdiplus::Color(0,0,0,0), 0);
            }

            Gdiplus::Color iconCol = isCurrent ? Gdiplus::Color(255, 255, 255, 255) : (isHovered ? UITheme::Colors::TextPrimary : UITheme::Colors::TextSecondary);
            Gdiplus::SolidBrush textB(iconCol);

            float cx = itemRect.X + itemRect.Width * 0.5f;
            if (iconType == 1) UITheme::DrawGridIcon(g, cx, y + 17.0f, 18.0f, iconCol);
            else if (iconType == 2) UITheme::DrawLightningIcon(g, cx, y + 17.0f, 18.0f, iconCol);
            else if (iconType == 3) UITheme::DrawCircleMinusIcon(g, cx, y + 17.0f, 18.0f, iconCol);
            else if (iconType == 4) UITheme::DrawUsersIcon(g, cx, y + 17.0f, 18.0f, iconCol);
            else if (iconType == 5) UITheme::DrawGearIcon(g, cx, y + 17.0f, 18.0f, iconCol);
            else if (iconType == 6) UITheme::DrawDocumentIcon(g, cx, y + 17.0f, 18.0f, iconCol);

            Gdiplus::RectF labelRect(itemRect.X, y + 31.0f, itemRect.Width, 18.0f);
            g.DrawString(label.c_str(), -1, &fontSmall, labelRect, &sfCenter, &textB);
        };

        drawNavItem(NavSection::AllEntries, 1, 66.0f, L"All Entries", 1);
        drawNavItem(NavSection::Active,     2, 126.0f, L"Active", 2);
        drawNavItem(NavSection::Disabled,   3, 186.0f, L"Disabled", 3);
        drawNavItem(NavSection::Groups,     4, 246.0f, L"Groups", 4);

        drawNavItem(NavSection::Settings,   5, (float)height - 124.0f, L"Settings", 5);
        drawNavItem(NavSection::Logs,       6, (float)height - 66.0f,  L"Logs", 6);

        // ----------------------------------------------------
        // 4. Quick Add Card (x: 108, y: 66, w: width - 128, h: 122)
        // ----------------------------------------------------
        float cardX = 108.0f;
        float cardY = 66.0f;
        float cardW = (float)width - 128.0f;
        float cardH = 122.0f;
        Gdiplus::RectF quickAddRect(cardX, cardY, cardW, cardH);
        UITheme::DrawRoundedRect(g, quickAddRect, 10.0f, UITheme::Colors::BgCard, UITheme::Colors::BorderDark, 1.0f);

        // "Quick Add" Title
        g.DrawString(L"Quick Add", -1, &fontBold, Gdiplus::PointF(cardX + 18.0f, cardY + 12.0f), &textWhite);

        // Labels above inputs
        Gdiplus::SolidBrush labelBrush(UITheme::Colors::TextSecondary);
        g.DrawString(L"IP Address", -1, &fontSmall, Gdiplus::PointF(cardX + 18.0f, cardY + 34.0f), &labelBrush);
        g.DrawString(L"Domain", -1, &fontSmall, Gdiplus::PointF(cardX + 242.0f, cardY + 34.0f), &labelBrush);

        // Quick Actions Toolbar Row (cardY + 88)
        float actY = cardY + 90.0f;

        auto drawActionLink = [&](int actCode, float x, const std::wstring& label, int iconType, Gdiplus::RectF& outRect) {
            bool isHov = (g_app.hoveredAction == actCode);
            Gdiplus::Color col = isHov ? Gdiplus::Color(255, 255, 255, 255) : UITheme::Colors::TextSecondary;
            Gdiplus::SolidBrush b(col);

            float iconX = x + 7.0f;
            float iconY = actY + 7.0f;
            if (iconType == 1) UITheme::DrawDownloadArrowIcon(g, iconX, iconY, 13.0f, col);
            else if (iconType == 2) UITheme::DrawUploadArrowIcon(g, iconX, iconY, 13.0f, col);
            else if (iconType == 3) UITheme::DrawBackupIcon(g, iconX, iconY, 13.0f, col);
            else if (iconType == 4) UITheme::DrawRefreshIcon(g, iconX, iconY, 13.0f, col);

            g.DrawString(label.c_str(), -1, &fontSmall, Gdiplus::PointF(x + 18.0f, actY), &b);
            outRect = Gdiplus::RectF(x, actY - 2.0f, 68.0f, 22.0f);
        };

        drawActionLink(1, cardX + 18.0f,  L"Import",  1, g_app.rcImport);
        drawActionLink(2, cardX + 94.0f,  L"Export",  2, g_app.rcExport);
        drawActionLink(3, cardX + 172.0f, L"Backup",  3, g_app.rcBackup);
        drawActionLink(4, cardX + 252.0f, L"Restore", 4, g_app.rcRestore);

        // "Flush DNS" Button (Right aligned in Quick Add)
        float flushW = 96.0f;
        float flushH = 26.0f;
        float flushX = cardX + cardW - flushW - 14.0f;
        g_app.rcFlushDns = Gdiplus::RectF(flushX, actY - 4.0f, flushW, flushH);

        bool isFlushHov = (g_app.hoveredAction == 5);
        Gdiplus::Color flushBg = isFlushHov ? Gdiplus::Color(255, 34, 40, 58) : Gdiplus::Color(255, 24, 28, 42);
        Gdiplus::Color flushBorder = isFlushHov ? UITheme::Colors::BorderFocus : UITheme::Colors::BorderDark;
        UITheme::DrawRoundedRect(g, g_app.rcFlushDns, 6.0f, flushBg, flushBorder, 1.0f);

        Gdiplus::Color flushCol = isFlushHov ? Gdiplus::Color(255, 255, 255, 255) : UITheme::Colors::TextPrimary;
        UITheme::DrawRefreshIcon(g, flushX + 14.0f, actY + 8.0f, 13.0f, flushCol);
        Gdiplus::SolidBrush flushTextB(flushCol);
        g.DrawString(L"Flush DNS", -1, &fontSmall, Gdiplus::PointF(flushX + 26.0f, actY), &flushTextB);

        // ----------------------------------------------------
        // 5. Host Entries List (y: 202 to height - 16)
        // ----------------------------------------------------
        int listY = 202;
        int listH = height - listY - 14;
        Gdiplus::Rect listClip((int)cardX - 4, listY, (int)cardW + 8, listH);
        g.SetClip(listClip);

        int listTotalH = (int)g_app.visibleRows.size() * 58;
        g_app.maxScroll = max(0, listTotalH - listH);
        if (g_app.scrollOffset > g_app.maxScroll) g_app.scrollOffset = g_app.maxScroll;
        if (g_app.scrollOffset < 0) g_app.scrollOffset = 0;

        if (g_app.visibleRows.empty()) {
            std::wstring emptyMsg = (g_app.hosts.GetTotalCount() == 0)
                ? L"No entries in hosts file. Use Quick Add above to add your first domain!"
                : L"No host entries match your selected navigation filter.";
            Gdiplus::RectF emptyRect(cardX, (float)listY + 40.0f, cardW, 60.0f);
            g.DrawString(emptyMsg.c_str(), -1, &fontRegular, emptyRect, &sfCenter, &labelBrush);
        } else {
            int curY = listY - g_app.scrollOffset;

            for (size_t i = 0; i < g_app.visibleRows.size(); ++i) {
                const auto& row = g_app.visibleRows[i];
                int rHeight = GetRowHeight(row);

                if (curY + rHeight >= listY && curY <= listY + listH) {
                    bool isHovered = ((int)i == g_app.hoveredRowIndex);

                    if (row.type == ListRowType::GroupHeader) {
                        // Group Section Header
                        Gdiplus::RectF grpRect(cardX, (float)curY, cardW, 34.0f);
                        UITheme::DrawRoundedRect(g, grpRect, 6.0f, Gdiplus::Color(255, 20, 24, 36), UITheme::Colors::BorderDark, 1.0f);

                        UITheme::DrawFolderIcon(g, cardX + 12.0f, (float)curY + 11.0f, UITheme::Colors::AccentCyan);
                        g.DrawString(row.groupName.c_str(), -1, &fontBold, Gdiplus::PointF(cardX + 32.0f, (float)curY + 8.0f), &textWhite);

                        size_t totalInGrp = g_app.hosts.GetGroupCount(row.groupName);
                        std::wstring countStr = std::to_wstring(totalInGrp) + L" entries";
                        UITheme::DrawBadge(g, countStr, cardX + 36.0f + (float)row.groupName.size() * 9.0f, (float)curY + 7.0f, UITheme::Colors::TextSecondary, Gdiplus::Color(255, 30, 35, 52), &fontSmall, 6, 2);
                    } else {
                        // Host Entry Card
                        const HostItem* pItem = NULL;
                        for (const auto& itm : g_app.hosts.GetItems()) {
                            if (itm.id == row.hostItemId) { pItem = &itm; break; }
                        }

                        if (pItem) {
                            Gdiplus::RectF rowRect(cardX, (float)curY, cardW, 50.0f);
                            Gdiplus::Color rowBg = isHovered ? UITheme::Colors::BgCardHover : UITheme::Colors::BgCard;
                            Gdiplus::Color rowBorder = isHovered ? Gdiplus::Color(255, 50, 60, 90) : UITheme::Colors::BorderDark;
                            UITheme::DrawRoundedRect(g, rowRect, 8.0f, rowBg, rowBorder, 1.0f);

                            // Left: Domain Name
                            Gdiplus::SolidBrush domBrush(pItem->enabled ? UITheme::Colors::TextPrimary : UITheme::Colors::TextMuted);
                            g.DrawString(pItem->domain.c_str(), -1, &fontDomain, Gdiplus::PointF(cardX + 18.0f, (float)curY + 16.0f), &domBrush);

                            // Middle: IP Address Pill
                            float ipPillX = cardX + cardW * 0.44f;
                            UITheme::DrawBadge(g, pItem->ip, ipPillX, (float)curY + 13.0f, UITheme::Colors::TextPrimary, UITheme::Colors::BadgeIpBg, &fontMono, 14, 4, Gdiplus::Color(255, 42, 50, 75));

                            // Right: Status Badge
                            float statusX = cardX + cardW - 195.0f;
                            if (pItem->enabled) {
                                UITheme::DrawBadge(g, L"Active", statusX, (float)curY + 14.0f, UITheme::Colors::ActiveGreen, UITheme::Colors::ActiveGreenBg, &fontSmall, 9, 3, UITheme::Colors::ActiveGreen);
                            } else {
                                UITheme::DrawBadge(g, L"Disabled", statusX, (float)curY + 14.0f, UITheme::Colors::DisabledGray, UITheme::Colors::DisabledGrayBg, &fontSmall, 9, 3);
                            }

                            // Toggle Switch
                            float togX = cardX + cardW - 130.0f;
                            float togY = (float)curY + 15.0f;
                            bool isTogHov = (isHovered && g_app.hoveredRowBtn == 1);
                            UITheme::DrawToggleSwitch(g, togX, togY, 38.0f, 20.0f, pItem->enabled, isTogHov);

                            // Edit Button (Pencil)
                            float editX = cardX + cardW - 82.0f;
                            float editY = (float)curY + 11.0f;
                            bool isEditHov = (isHovered && g_app.hoveredRowBtn == 2);
                            Gdiplus::RectF editRect(editX, editY, 28.0f, 28.0f);
                            Gdiplus::Color editBg = isEditHov ? Gdiplus::Color(255, 34, 42, 65) : Gdiplus::Color(255, 24, 28, 44);
                            Gdiplus::Color editBorder = isEditHov ? UITheme::Colors::BorderFocus : Gdiplus::Color(255, 40, 48, 70);
                            UITheme::DrawRoundedRect(g, editRect, 6.0f, editBg, editBorder, 1.0f);
                            UITheme::DrawPencilIcon(g, editX + 14.0f, editY + 14.0f, 13.0f, isEditHov ? UITheme::Colors::AccentCyan : UITheme::Colors::TextSecondary);

                            // Delete Button (Trash)
                            float delX = cardX + cardW - 46.0f;
                            float delY = (float)curY + 11.0f;
                            bool isDelHov = (isHovered && g_app.hoveredRowBtn == 3);
                            Gdiplus::RectF delRect(delX, delY, 28.0f, 28.0f);
                            Gdiplus::Color delBg = isDelHov ? UITheme::Colors::DangerBg : Gdiplus::Color(255, 24, 28, 44);
                            Gdiplus::Color delBorder = isDelHov ? UITheme::Colors::Danger : Gdiplus::Color(255, 40, 48, 70);
                            UITheme::DrawRoundedRect(g, delRect, 6.0f, delBg, delBorder, 1.0f);
                            UITheme::DrawTrashIcon(g, delX + 14.0f, delY + 14.0f, 13.0f, isDelHov ? UITheme::Colors::Danger : Gdiplus::Color(255, 248, 113, 113));
                        }
                    }
                }
                curY += rHeight + 8;
            }
        }

        g.ResetClip();

        // ----------------------------------------------------
        // 6. Toast Notification Overlay
        // ----------------------------------------------------
        if (!g_app.statusToast.empty() && (GetTickCount() - g_app.toastTimer < 3500)) {
            float toastW = (float)g_app.statusToast.size() * 8.5f + 32.0f;
            float toastX = (float)width - toastW - 20.0f;
            float toastY = (float)height - 46.0f;
            Gdiplus::RectF tRect(toastX, toastY, toastW, 32.0f);
            UITheme::DrawRoundedRect(g, tRect, 6.0f, Gdiplus::Color(245, 16, 185, 129), Gdiplus::Color(255, 255, 255, 255), 1.0f);
            Gdiplus::SolidBrush tText(Gdiplus::Color(255, 255, 255, 255));
            g.DrawString(g_app.statusToast.c_str(), -1, &fontBold, tRect, &sfCenter, &tText);
        }
    }

    // Blt memory DC to screen
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBM);
    DeleteObject(memBM);
    DeleteDC(memDC);
}

// Reposition child controls (Edit inputs, add button)
void RepositionControls(int width, int height) {
    float cardX = 108.0f;
    float cardY = 66.0f;
    float cardW = (float)width - 128.0f;

    int ipX = (int)cardX + 18;
    int ipY = (int)cardY + 54;
    int ipW = 210;
    int inputH = 28;
    MoveWindow(g_app.hEditIp, ipX, ipY, ipW, inputH, TRUE);

    int domX = ipX + ipW + 14;
    int btnW = 72;
    int domW = max(160, (int)cardW - (domX - (int)cardX) - btnW - 22);
    MoveWindow(g_app.hEditDomain, domX, ipY, domW, inputH, TRUE);

    int btnX = domX + domW + 10;
    MoveWindow(g_app.hBtnAdd, btnX, ipY - 1, btnW, inputH + 2, TRUE);

    // Hide unused inputs from old bar
    ShowWindow(g_app.hEditGroup, SW_HIDE);
    ShowWindow(g_app.hEditComment, SW_HIDE);
    ShowWindow(g_app.hEditSearch, SW_HIDE);
}

// Helper to find row at (x, y)
static int FindRowIndexAt(int y, int listY, int scrollOffset) {
    int curY = listY - scrollOffset;
    for (size_t i = 0; i < g_app.visibleRows.size(); ++i) {
        int rH = GetRowHeight(g_app.visibleRows[i]);
        if (y >= curY && y <= curY + rH) {
            return (int)i;
        }
        curY += rH + 8;
    }
    return -1;
}

// Handle Mouse Click in UI
void HandleMouseClick(int x, int y) {
    RECT rc;
    GetClientRect(g_app.hWndMain, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    // 1. Titlebar buttons
    if (g_app.rcCloseBtn.Contains((float)x, (float)y)) {
        PostMessage(g_app.hWndMain, WM_CLOSE, 0, 0);
        return;
    }
    if (g_app.rcMaxBtn.Contains((float)x, (float)y)) {
        if (IsZoomed(g_app.hWndMain)) {
            ShowWindow(g_app.hWndMain, SW_RESTORE);
        } else {
            ShowWindow(g_app.hWndMain, SW_MAXIMIZE);
        }
        return;
    }
    if (g_app.rcMinBtn.Contains((float)x, (float)y)) {
        ShowWindow(g_app.hWndMain, SW_MINIMIZE);
        return;
    }

    // 2. Navigation Sidebar click (x: 0 to 88)
    if (x >= 0 && x <= 88) {
        if (y >= 66 && y <= 120) {
            g_app.currentNav = NavSection::AllEntries;
            g_app.filter = FilterMode::All;
            g_app.selectedGroupFilter.clear();
            UpdateFilteredList();
            InvalidateRect(g_app.hWndMain, NULL, FALSE);
            return;
        }
        if (y >= 126 && y <= 180) {
            g_app.currentNav = NavSection::Active;
            g_app.filter = FilterMode::ActiveOnly;
            g_app.selectedGroupFilter.clear();
            UpdateFilteredList();
            InvalidateRect(g_app.hWndMain, NULL, FALSE);
            return;
        }
        if (y >= 186 && y <= 240) {
            g_app.currentNav = NavSection::Disabled;
            g_app.filter = FilterMode::DisabledOnly;
            g_app.selectedGroupFilter.clear();
            UpdateFilteredList();
            InvalidateRect(g_app.hWndMain, NULL, FALSE);
            return;
        }
        if (y >= 246 && y <= 300) {
            g_app.currentNav = NavSection::Groups;
            g_app.filter = FilterMode::All;
            OpenManageGroupsWindow(g_app.hWndMain);
            return;
        }
        if (y >= height - 124 && y <= height - 70) {
            OpenSettingsWindow(g_app.hWndMain);
            return;
        }
        if (y >= height - 66 && y <= height - 12) {
            OpenLogsWindow(g_app.hWndMain);
            return;
        }
    }

    // 3. Quick Actions Toolbar Row
    if (g_app.rcImport.Contains((float)x, (float)y)) {
        wchar_t szFile[MAX_PATH] = {0};
        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = g_app.hWndMain;
        ofn.lpstrFilter = L"Hosts Files (*.txt;hosts)\0*.txt;hosts\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

        if (GetOpenFileNameW(&ofn)) {
            std::wstring err;
            if (g_app.hosts.ImportFromFile(szFile, err)) {
                std::wstring saveErr;
                g_app.hosts.Save(true, saveErr);
                WinUtil::FlushDnsCache();
                UpdateFilteredList();
                ShowToast(L"Imported entries & flushed DNS!");
                AddLog(L"Imported entries from: " + std::wstring(szFile));
                InvalidateRect(g_app.hWndMain, NULL, FALSE);
            } else {
                MessageBoxW(g_app.hWndMain, err.c_str(), L"Import Error", MB_OK | MB_ICONERROR);
            }
        }
        return;
    }

    if (g_app.rcExport.Contains((float)x, (float)y)) {
        wchar_t szFile[MAX_PATH] = L"hosts_export.txt";
        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = g_app.hWndMain;
        ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT;

        if (GetSaveFileNameW(&ofn)) {
            std::wstring err;
            if (g_app.hosts.ExportToFile(szFile, err)) {
                ShowToast(L"Hosts file exported successfully.");
                AddLog(L"Exported hosts file to: " + std::wstring(szFile));
            } else {
                MessageBoxW(g_app.hWndMain, err.c_str(), L"Export Error", MB_OK | MB_ICONERROR);
            }
        }
        return;
    }

    if (g_app.rcBackup.Contains((float)x, (float)y)) {
        std::wstring bPath, err;
        if (g_app.hosts.CreateBackupNow(bPath, err)) {
            ShowToast(L"Safety backup created successfully.");
            AddLog(L"Created manual safety backup: " + bPath);
        } else {
            MessageBoxW(g_app.hWndMain, err.c_str(), L"Backup Error", MB_OK | MB_ICONERROR);
        }
        return;
    }

    if (g_app.rcRestore.Contains((float)x, (float)y)) {
        ShowBackupsMenu(g_app.hWndMain);
        return;
    }

    if (g_app.rcFlushDns.Contains((float)x, (float)y)) {
        WinUtil::FlushDnsCache();
        ShowToast(L"Windows DNS Cache Flushed Successfully!");
        AddLog(L"Flushed Windows DNS Resolver cache via dnsapi.dll");
        return;
    }

    // 4. Host Entries List Click
    int listY = 202;
    float cardX = 108.0f;
    float cardW = (float)width - 128.0f;

    if (y >= listY && y <= height - 14) {
        int rowIndex = FindRowIndexAt(y, listY, g_app.scrollOffset);
        if (rowIndex >= 0 && rowIndex < (int)g_app.visibleRows.size()) {
            const auto& row = g_app.visibleRows[rowIndex];

            if (row.type == ListRowType::HostCard) {
                int itemId = row.hostItemId;

                // Toggle Button
                float togX = cardX + cardW - 130.0f;
                if (x >= togX && x <= togX + 38.0f) {
                    g_app.hosts.ToggleItem(itemId);
                    std::wstring saveErr;
                    g_app.hosts.Save(true, saveErr);
                    WinUtil::FlushDnsCache();
                    UpdateFilteredList();

                    for (const auto& itm : g_app.hosts.GetItems()) {
                        if (itm.id == itemId) {
                            ShowToast(L"Toggled " + itm.domain + (itm.enabled ? L" (Active)" : L" (Disabled)"));
                            AddLog(L"Toggled '" + itm.domain + L"' to " + (itm.enabled ? L"Active" : L"Disabled"));
                            break;
                        }
                    }
                    InvalidateRect(g_app.hWndMain, NULL, FALSE);
                    return;
                }

                // Edit Button
                float editX = cardX + cardW - 82.0f;
                if (x >= editX && x <= editX + 28.0f) {
                    OpenEditWindow(g_app.hWndMain, itemId);
                    return;
                }

                // Delete Button
                float delX = cardX + cardW - 46.0f;
                if (x >= delX && x <= delX + 28.0f) {
                    std::wstring delDomain;
                    for (const auto& itm : g_app.hosts.GetItems()) {
                        if (itm.id == itemId) { delDomain = itm.domain; break; }
                    }

                    if (MessageBoxW(g_app.hWndMain, (L"Delete entry for '" + delDomain + L"' from hosts file?").c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        g_app.hosts.DeleteItem(itemId);
                        std::wstring saveErr;
                        g_app.hosts.Save(true, saveErr);
                        WinUtil::FlushDnsCache();
                        UpdateFilteredList();
                        ShowToast(L"Deleted entry '" + delDomain + L"'.");
                        AddLog(L"Deleted entry: " + delDomain);
                        InvalidateRect(g_app.hWndMain, NULL, FALSE);
                    }
                    return;
                }
            }
        }
    }
}

// Handle Mouse Move in UI
void HandleMouseMove(int x, int y) {
    RECT rc;
    GetClientRect(g_app.hWndMain, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    int prevRow = g_app.hoveredRowIndex;
    int prevBtn = g_app.hoveredRowBtn;
    int prevNav = g_app.hoveredNav;
    int prevAct = g_app.hoveredAction;
    int prevTitle = g_app.hoveredTitleBtn;

    g_app.hoveredRowIndex = -1;
    g_app.hoveredRowBtn = 0;
    g_app.hoveredNav = 0;
    g_app.hoveredAction = 0;
    g_app.hoveredTitleBtn = 0;

    // Check Titlebar buttons
    if (g_app.rcCloseBtn.Contains((float)x, (float)y)) g_app.hoveredTitleBtn = 3;
    else if (g_app.rcMaxBtn.Contains((float)x, (float)y)) g_app.hoveredTitleBtn = 2;
    else if (g_app.rcMinBtn.Contains((float)x, (float)y)) g_app.hoveredTitleBtn = 1;

    // Check Sidebar
    if (x >= 0 && x <= 88) {
        if (y >= 66 && y <= 120) g_app.hoveredNav = 1;
        else if (y >= 126 && y <= 180) g_app.hoveredNav = 2;
        else if (y >= 186 && y <= 240) g_app.hoveredNav = 3;
        else if (y >= 246 && y <= 300) g_app.hoveredNav = 4;
        else if (y >= height - 124 && y <= height - 70) g_app.hoveredNav = 5;
        else if (y >= height - 66 && y <= height - 12) g_app.hoveredNav = 6;
    }

    // Check Quick Actions
    if (g_app.rcImport.Contains((float)x, (float)y)) g_app.hoveredAction = 1;
    else if (g_app.rcExport.Contains((float)x, (float)y)) g_app.hoveredAction = 2;
    else if (g_app.rcBackup.Contains((float)x, (float)y)) g_app.hoveredAction = 3;
    else if (g_app.rcRestore.Contains((float)x, (float)y)) g_app.hoveredAction = 4;
    else if (g_app.rcFlushDns.Contains((float)x, (float)y)) g_app.hoveredAction = 5;

    // Check Host List items
    int listY = 202;
    float cardX = 108.0f;
    float cardW = (float)width - 128.0f;

    if (y >= listY && y <= height - 14) {
        int rowIndex = FindRowIndexAt(y, listY, g_app.scrollOffset);
        if (rowIndex >= 0 && rowIndex < (int)g_app.visibleRows.size()) {
            g_app.hoveredRowIndex = rowIndex;
            const auto& row = g_app.visibleRows[rowIndex];

            if (row.type == ListRowType::HostCard) {
                float togX = cardX + cardW - 130.0f;
                float editX = cardX + cardW - 82.0f;
                float delX = cardX + cardW - 46.0f;

                if (x >= togX && x <= togX + 38.0f) g_app.hoveredRowBtn = 1;
                else if (x >= editX && x <= editX + 28.0f) g_app.hoveredRowBtn = 2;
                else if (x >= delX && x <= delX + 28.0f) g_app.hoveredRowBtn = 3;
            }
        }
    }

    if (prevRow != g_app.hoveredRowIndex || prevBtn != g_app.hoveredRowBtn ||
        prevNav != g_app.hoveredNav || prevAct != g_app.hoveredAction || prevTitle != g_app.hoveredTitleBtn) {
        InvalidateRect(g_app.hWndMain, NULL, FALSE);
    }
}

// Window Procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_app.hWndMain = hWnd;

        // Fonts
        g_app.hFontRegular = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_app.hFontBold = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_app.hFontSmall = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // Controls
        g_app.hEditIp = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 28, hWnd, (HMENU)IDC_EDIT_IP, g_app.hInstance, NULL);
        g_app.hEditDomain = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 28, hWnd, (HMENU)IDC_EDIT_DOMAIN, g_app.hInstance, NULL);
        g_app.hBtnAdd = CreateWindowW(L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 0, 0, 72, 28, hWnd, (HMENU)IDC_BTN_ADD, g_app.hInstance, NULL);

        // Dummy/hidden controls for compatibility
        g_app.hEditGroup = CreateWindowExW(0, L"EDIT", L"General", WS_CHILD, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_GROUP, g_app.hInstance, NULL);
        g_app.hEditComment = CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_COMMENT, g_app.hInstance, NULL);
        g_app.hEditSearch = CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_SEARCH, g_app.hInstance, NULL);

        SendMessage(g_app.hEditIp, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);
        SendMessage(g_app.hEditDomain, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);

        SendMessageW(g_app.hEditDomain, 0x1501 /*EM_SETCUEBANNER*/, TRUE, (LPARAM)L"Domain (e.g. dev.local)");

        // Subclass edit controls
        g_OldEditProc = (WNDPROC)SetWindowLongPtrW(g_app.hEditDomain, GWLP_WNDPROC, (LONG_PTR)DarkEditProc);
        SetWindowLongPtrW(g_app.hEditIp, GWLP_WNDPROC, (LONG_PTR)DarkEditProc);

        // Load hosts file
        std::wstring loadErr;
        if (!g_app.hosts.Load(loadErr)) {
            MessageBoxW(hWnd, loadErr.c_str(), L"Hosts Manager Warning", MB_OK | MB_ICONWARNING);
        } else {
            AddLog(L"Loaded " + std::to_wstring(g_app.hosts.GetTotalCount()) + L" entries from hosts file");
        }
        UpdateFilteredList();
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        SetTextColor(hdcEdit, RGB(241, 245, 249));
        SetBkColor(hdcEdit, RGB(15, 17, 27)); // BgInput
        static HBRUSH hbrInput = CreateSolidBrush(RGB(15, 17, 27));
        return (LRESULT)hbrInput;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlID == IDC_BTN_ADD) {
            Gdiplus::Graphics g(pDIS->hDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            bool isPressed = (pDIS->itemState & ODS_SELECTED);
            Gdiplus::RectF btnRect(0, 0, (float)(pDIS->rcItem.right - pDIS->rcItem.left), (float)(pDIS->rcItem.bottom - pDIS->rcItem.top));

            Gdiplus::Color bg = isPressed ? Gdiplus::Color(255, 26, 31, 46) : Gdiplus::Color(255, 37, 43, 62);
            Gdiplus::Color border = Gdiplus::Color(255, 52, 60, 86);
            UITheme::DrawRoundedRect(g, btnRect, 6.0f, bg, border, 1.0f);

            Gdiplus::Font font(L"Segoe UI", 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(L"Add", -1, &font, btnRect, &sf, &textBrush);
            return TRUE;
        }
        break;
    }

    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        RepositionControls(w, h);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
        lpMMI->ptMinTrackSize.x = 880;
        lpMMI->ptMinTrackSize.y = 520;
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Prevent flickering

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        PaintUI(hdc, rc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        SetFocus(hWnd);
        HandleMouseClick(x, y);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        HandleMouseMove(x, y);

        TRACKMOUSEEVENT tme = { sizeof(tme) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hWnd;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE: {
        g_app.hoveredRowIndex = -1;
        g_app.hoveredRowBtn = 0;
        g_app.hoveredNav = 0;
        g_app.hoveredAction = 0;
        g_app.hoveredTitleBtn = 0;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_app.scrollOffset -= (delta / WHEEL_DELTA) * 50;
        if (g_app.scrollOffset < 0) g_app.scrollOffset = 0;
        if (g_app.scrollOffset > g_app.maxScroll) g_app.scrollOffset = g_app.maxScroll;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        if (id == IDC_BTN_ADD) {
            wchar_t szIp[256] = {0};
            wchar_t szDomain[1024] = {0};
            GetWindowTextW(g_app.hEditIp, szIp, 256);
            GetWindowTextW(g_app.hEditDomain, szDomain, 1024);

            if (!HostsManager::IsValidIp(szIp)) {
                MessageBoxW(hWnd, L"Please enter a valid IP address (e.g. 127.0.0.1 or 0.0.0.0).", L"Validation Error", MB_OK | MB_ICONWARNING);
                SetFocus(g_app.hEditIp);
                return 0;
            }

            if (!HostsManager::IsValidDomain(szDomain)) {
                MessageBoxW(hWnd, L"Please enter a valid domain name (e.g. dev.local or site.test).", L"Validation Error", MB_OK | MB_ICONWARNING);
                SetFocus(g_app.hEditDomain);
                return 0;
            }

            g_app.hosts.AddItem(szIp, szDomain, L"", L"General", true);

            std::wstring saveErr;
            g_app.hosts.Save(true, saveErr);
            WinUtil::FlushDnsCache();
            AddLog(L"Added host entry: " + std::wstring(szIp) + L" " + szDomain);

            SetWindowTextW(g_app.hEditDomain, L"");
            SetFocus(g_app.hEditDomain);

            UpdateFilteredList();
            ShowToast(L"Added " + std::wstring(szDomain) + L" & flushed DNS!");
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_DESTROY: {
        DeleteObject(g_app.hFontRegular);
        DeleteObject(g_app.hFontBold);
        DeleteObject(g_app.hFontSmall);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// WinMain Entry Point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;

    // Check Administrator elevation
    if (!WinUtil::IsUserAdmin()) {
        if (WinUtil::RelaunchAsAdmin()) {
            return 0; // Child elevated process spawned
        }
    }

    g_app.hInstance = hInstance;
    g_app.currentNav = NavSection::AllEntries;
    g_app.filter = FilterMode::All;
    g_app.scrollOffset = 0;
    g_app.hoveredRowIndex = -1;
    g_app.hoveredRowBtn = 0;
    g_app.hoveredNav = 0;
    g_app.hoveredAction = 0;
    g_app.hoveredTitleBtn = 0;

    AddLog(L"HostAge started with Administrator privileges");

    // Enable High-DPI Awareness if available
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
        SetProcessDpiAwarenessContextProc pSetDpiContext = 
            (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetDpiContext) {
            pSetDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            typedef BOOL(WINAPI* SetProcessDPIAwareProc)();
            SetProcessDPIAwareProc pSetDPIAware = 
                (SetProcessDPIAwareProc)GetProcAddress(hUser32, "SetProcessDPIAware");
            if (pSetDPIAware) {
                pSetDPIAware();
            }
        }
    }

    // Initialize Common Controls
    INITCOMMONCONTROLSEX icex = { sizeof(icex) };
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_app.gdiplusToken, &gdiplusStartupInput, NULL);

    // Detect environment
    g_app.isAdmin = WinUtil::IsUserAdmin();
    g_app.osVersion = WinUtil::GetWindowsVersionName();

    // Register Window Class
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"HostageMainWindow";
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Failed to register window class.", L"Error", MB_ICONERROR);
        return 1;
    }

    // Default window dimensions matching mockup
    int defaultW = 1040;
    int defaultH = 680;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = max(50, (screenW - defaultW) / 2);
    int posY = max(50, (screenH - defaultH) / 2);

    HWND hWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"HostageMainWindow",
        L"Hostage - Windows Hosts Manager",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        posX, posY, defaultW, defaultH,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        MessageBoxW(NULL, L"Failed to create main window.", L"Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Message Loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // Shutdown GDI+
    Gdiplus::GdiplusShutdown(g_app.gdiplusToken);

    return (int)msg.wParam;
}
