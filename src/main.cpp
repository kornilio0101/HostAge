#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
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

// Control IDs
#define IDC_EDIT_IP          1001
#define IDC_EDIT_DOMAIN      1002
#define IDC_EDIT_COMMENT     1003
#define IDC_BTN_ADD          1004
#define IDC_EDIT_SEARCH      1005
#define IDC_EDIT_GROUP       1008

#define IDC_PRESET_127       1006
#define IDC_PRESET_000       1007

#define IDC_FILTER_ALL       1010
#define IDC_FILTER_ACTIVE    1011
#define IDC_FILTER_DISABLED  1012

#define IDC_BTN_SAVE         1020
#define IDC_BTN_FLUSHDNS     1021
#define IDC_BTN_BACKUP       1022
#define IDC_BTN_NOTEPAD      1023
#define IDC_BTN_RELOAD       1024
#define IDC_BTN_ENABLE_ALL   1025
#define IDC_BTN_DISABLE_ALL  1026

#define IDM_RESTORE_BASE      2000
#define IDM_GROUP_FILTER_BASE 3000
#define IDM_ASSIGN_BASE       5000

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

    FilterMode filter;
    std::wstring selectedGroupFilter; // Empty = All Groups
    std::wstring searchQuery;
    std::wstring statusToast;
    DWORD toastTimer;

    // Scrolling & Layout
    int scrollOffset;
    int maxScroll;
    int hoveredRowIndex;
    int hoveredButton; // 0=None, 1=Toggle, 2=Edit, 3=Delete, 4=GroupToggle, 5=GroupBadge
    int activeEditId;

    // Filtered list rows
    std::vector<ListRow> visibleRows;

    // Hovered bottom buttons
    int hoveredBottomBtn; // 0=None, 1=Reload, 2=Notepad, 3=Backup, 4=FlushDNS, 5=Save

    ULONG_PTR gdiplusToken;
};

static AppState g_app;

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void OpenEditWindow(HWND hWndParent, int itemId);
void OpenManageGroupsWindow(HWND hWndParent);
void ShowGroupFilterMenu(HWND hWnd);
void ShowAssignGroupMenu(HWND hWnd, int itemId);
void UpdateFilteredList();
void ShowToast(const std::wstring& msg);

static std::wstring ToLower(const std::wstring& s) {
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::towlower);
    return r;
}

static inline int GetRowHeight(const ListRow& row) {
    return (row.type == ListRowType::GroupHeader) ? 36 : 58;
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
            ListRow hr;
            hr.type = ListRowType::GroupHeader;
            hr.groupName = grp;
            hr.hostItemId = 0;
            g_app.visibleRows.push_back(hr);

            for (int id : groupItemIds) {
                ListRow cr;
                cr.type = ListRowType::HostCard;
                cr.groupName = grp;
                cr.hostItemId = id;
                g_app.visibleRows.push_back(cr);
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
        if (hWnd == g_app.hEditIp || hWnd == g_app.hEditDomain || hWnd == g_app.hEditGroup || hWnd == g_app.hEditComment) {
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

        CreateWindowW(L"STATIC", L"Group / Category (Assign or create new):", WS_CHILD | WS_VISIBLE, 25, 122, 280, 18, hWnd, NULL, g_app.hInstance, NULL);
        g_hModalGroup = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"General", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 25, 142, 330, 24, hWnd, NULL, g_app.hInstance, NULL);

        CreateWindowW(L"STATIC", L"Comment (Optional):", WS_CHILD | WS_VISIBLE, 25, 174, 140, 18, hWnd, NULL, g_app.hInstance, NULL);
        g_hModalComment = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 25, 194, 330, 24, hWnd, NULL, g_app.hInstance, NULL);

        g_hModalCheck = CreateWindowW(L"BUTTON", L"Entry is Active (Enabled)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 25, 230, 250, 24, hWnd, NULL, g_app.hInstance, NULL);

        CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 185, 268, 80, 32, hWnd, (HMENU)IDOK, g_app.hInstance, NULL);
        CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 275, 268, 80, 32, hWnd, (HMENU)IDCANCEL, g_app.hInstance, NULL);

        SendMessage(g_hModalIp, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hModalDomain, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hModalGroup, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hModalComment, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hModalCheck, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Pre-fill
        for (const auto& item : g_app.hosts.GetItems()) {
            if (item.id == g_app.activeEditId) {
                SetWindowTextW(g_hModalIp, item.ip.c_str());
                SetWindowTextW(g_hModalDomain, item.domain.c_str());
                SetWindowTextW(g_hModalGroup, item.group.empty() ? L"General" : item.group.c_str());
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
            UpdateFilteredList();
            ShowToast(L"Entry updated successfully.");
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

        CreateWindowW(L"STATIC", L"Manage Domain Groups", WS_CHILD | WS_VISIBLE, 20, 16, 260, 22, hWnd, NULL, g_app.hInstance, NULL);
        CreateWindowW(L"STATIC", L"Existing Groups:", WS_CHILD | WS_VISIBLE, 20, 46, 200, 18, hWnd, NULL, g_app.hInstance, NULL);

        g_hListGroups = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY,
            20, 68, 220, 240, hWnd, (HMENU)4001, g_app.hInstance, NULL);

        // Right side: Add Group
        CreateWindowW(L"STATIC", L"Add New Group:", WS_CHILD | WS_VISIBLE, 260, 46, 180, 18, hWnd, NULL, g_app.hInstance, NULL);
        g_hEditNewGroup = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            260, 68, 195, 26, hWnd, (HMENU)4002, g_app.hInstance, NULL);
        CreateWindowW(L"BUTTON", L"+ Add Group", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            260, 100, 195, 28, hWnd, (HMENU)4003, g_app.hInstance, NULL);

        // Rename Selected Group
        CreateWindowW(L"STATIC", L"Rename Selected Group:", WS_CHILD | WS_VISIBLE, 260, 142, 190, 18, hWnd, NULL, g_app.hInstance, NULL);
        g_hEditRenameGroup = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            260, 164, 195, 26, hWnd, (HMENU)4004, g_app.hInstance, NULL);
        CreateWindowW(L"BUTTON", L"Rename Group", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            260, 196, 195, 28, hWnd, (HMENU)4005, g_app.hInstance, NULL);

        // Remove Selected Group
        CreateWindowW(L"BUTTON", L"Remove Group", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            260, 240, 195, 32, hWnd, (HMENU)4006, g_app.hInstance, NULL);

        // Bottom note and Close button
        CreateWindowW(L"STATIC", L"* Removing a group reverts domains & IPs to 'Ungrouped' (never deleted).",
            WS_CHILD | WS_VISIBLE, 20, 322, 440, 18, hWnd, NULL, g_app.hInstance, NULL);
        CreateWindowW(L"BUTTON", L"Done", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            375, 350, 80, 32, hWnd, (HMENU)IDOK, g_app.hInstance, NULL);

        SendMessage(g_hListGroups, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hEditNewGroup, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hEditRenameGroup, WM_SETFONT, (WPARAM)hFont, TRUE);

        SendMessageW(g_hEditNewGroup, 0x1501 /*EM_SETCUEBANNER*/, TRUE, (LPARAM)L"New group name...");
        SendMessageW(g_hEditRenameGroup, 0x1501 /*EM_SETCUEBANNER*/, TRUE, (LPARAM)L"New name for selected...");

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

        if (id == 4001 && code == LBN_SELCHANGE) {
            int sel = (int)SendMessage(g_hListGroups, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                auto groups = g_app.hosts.GetGroups();
                if (sel >= 0 && sel < (int)groups.size()) {
                    SetWindowTextW(g_hEditRenameGroup, groups[sel].c_str());
                }
            }
            return 0;
        }

        // Add Group
        if (id == 4003) {
            wchar_t szNewGroup[256] = {0};
            GetWindowTextW(g_hEditNewGroup, szNewGroup, 256);
            std::wstring gName(szNewGroup);
            if (gName.empty()) {
                MessageBoxW(hWnd, L"Please enter a valid group name.", L"Validation", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (g_app.hosts.AddGroup(gName)) {
                RefreshGroupListbox(g_hListGroups);
                SetWindowTextW(g_hEditNewGroup, L"");
                UpdateFilteredList();
                ShowToast(L"Group '" + gName + L"' created.");
            } else {
                MessageBoxW(hWnd, L"Group already exists or name is invalid.", L"Information", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        // Rename Group
        if (id == 4005) {
            int sel = (int)SendMessage(g_hListGroups, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBoxW(hWnd, L"Please select a group to rename from the list.", L"Selection Required", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            auto groups = g_app.hosts.GetGroups();
            if (sel >= 0 && sel < (int)groups.size()) {
                std::wstring oldName = groups[sel];
                wchar_t szNewName[256] = {0};
                GetWindowTextW(g_hEditRenameGroup, szNewName, 256);
                std::wstring newName(szNewName);
                if (newName.empty()) {
                    MessageBoxW(hWnd, L"Please enter a new group name.", L"Validation", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                if (g_app.hosts.RenameGroup(oldName, newName)) {
                    RefreshGroupListbox(g_hListGroups);
                    UpdateFilteredList();
                    ShowToast(L"Renamed group '" + oldName + L"' to '" + newName + L"'.");
                } else {
                    MessageBoxW(hWnd, L"Could not rename group (name may already exist).", L"Rename Failed", MB_OK | MB_ICONWARNING);
                }
            }
            return 0;
        }

        // Remove Group
        if (id == 4006) {
            int sel = (int)SendMessage(g_hListGroups, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBoxW(hWnd, L"Please select a group to remove from the list.", L"Selection Required", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            auto groups = g_app.hosts.GetGroups();
            if (sel >= 0 && sel < (int)groups.size()) {
                std::wstring groupToRemove = groups[sel];
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
                    }
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
    int h = 430;
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

// Quick Group Assignment Menu (shown when clicking the Group Pill on a card)
void ShowAssignGroupMenu(HWND hWnd, int itemId) {
    auto groups = g_app.hosts.GetGroups();
    HMENU hMenu = CreatePopupMenu();

    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"Assign Domain to Group:");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_ASSIGN_BASE, L"Ungrouped");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i] == L"Ungrouped") continue;
        AppendMenuW(hMenu, MF_STRING, IDM_ASSIGN_BASE + 1 + (UINT)i, groups[i].c_str());
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_ASSIGN_BASE + 999, L"⚙️ Manage Groups...");

    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == IDM_ASSIGN_BASE) {
        g_app.hosts.AssignItemGroup(itemId, L"Ungrouped");
        UpdateFilteredList();
        ShowToast(L"Domain reverted to Ungrouped.");
        InvalidateRect(hWnd, NULL, FALSE);
    } else if (cmd == IDM_ASSIGN_BASE + 999) {
        OpenManageGroupsWindow(hWnd);
    } else if (cmd > IDM_ASSIGN_BASE && cmd <= IDM_ASSIGN_BASE + (int)groups.size()) {
        size_t idx = cmd - IDM_ASSIGN_BASE - 1;
        g_app.hosts.AssignItemGroup(itemId, groups[idx]);
        UpdateFilteredList();
        ShowToast(L"Assigned domain to group [" + groups[idx] + L"].");
        InvalidateRect(hWnd, NULL, FALSE);
    }
}

// Show Group Filter Popup Menu
void ShowGroupFilterMenu(HWND hWnd) {
    auto groups = g_app.hosts.GetGroups();
    HMENU hMenu = CreatePopupMenu();

    AppendMenuW(hMenu, (g_app.selectedGroupFilter.empty() ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, IDM_GROUP_FILTER_BASE, L"All Groups");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    for (size_t i = 0; i < groups.size(); ++i) {
        std::wstring label = groups[i] + L" (" + std::to_wstring(g_app.hosts.GetGroupCount(groups[i])) + L")";
        UINT flags = MF_STRING;
        if (g_app.selectedGroupFilter == groups[i]) flags |= MF_CHECKED;
        AppendMenuW(hMenu, flags, IDM_GROUP_FILTER_BASE + 1 + (UINT)i, label.c_str());
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_GROUP_FILTER_BASE + 999, L"⚙️ Manage Groups (Add / Rename / Remove)...");

    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == IDM_GROUP_FILTER_BASE) {
        g_app.selectedGroupFilter.clear();
        UpdateFilteredList();
        InvalidateRect(hWnd, NULL, FALSE);
    } else if (cmd == IDM_GROUP_FILTER_BASE + 999) {
        OpenManageGroupsWindow(hWnd);
    } else if (cmd > IDM_GROUP_FILTER_BASE && cmd <= IDM_GROUP_FILTER_BASE + (int)groups.size()) {
        g_app.selectedGroupFilter = groups[cmd - IDM_GROUP_FILTER_BASE - 1];
        UpdateFilteredList();
        InvalidateRect(hWnd, NULL, FALSE);
    }
}

// Layout helper coordinates
struct UILayout {
    int winW;
    int winH;

    int headerH;
    int addBoxY;
    int addBoxH;
    int filterY;
    int filterH;
    int listY;
    int listH;
    int bottomY;
    int bottomH;
};

static UILayout GetLayout(int w, int h) {
    UILayout l;
    l.winW = w;
    l.winH = h;
    l.headerH = 62;
    l.addBoxY = l.headerH;
    l.addBoxH = 76;
    l.filterY = l.addBoxY + l.addBoxH;
    l.filterH = 46;
    l.bottomH = 64;
    l.bottomY = h - l.bottomH;
    l.listY = l.filterY + l.filterH;
    l.listH = max(100, l.bottomY - l.listY);
    return l;
}

// Paint the entire UI in double buffer
void PaintUI(HDC hdc, const RECT& clientRect) {
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) return;

    UILayout l = GetLayout(width, height);

    // Create memory DC and bitmap for flicker-free double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBM = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBM = SelectObject(memDC, memBM);

    {
        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // Background
        Gdiplus::SolidBrush bgBrush(UITheme::Colors::BgMain);
        g.FillRectangle(&bgBrush, 0, 0, width, height);

        // Fonts
        Gdiplus::Font fontTitle(L"Segoe UI", 16.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontSub(L"Segoe UI", 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font fontRegular(L"Segoe UI", 13.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font fontBold(L"Segoe UI", 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontDomain(L"Segoe UI", 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontMono(L"Consolas", 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fontSmall(L"Segoe UI", 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font fontBadge(L"Segoe UI", 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        // ----------------------------------------------------
        // 1. Header Bar
        // ----------------------------------------------------
        Gdiplus::SolidBrush headerBg(UITheme::Colors::BgHeader);
        g.FillRectangle(&headerBg, 0, 0, width, l.headerH);
        Gdiplus::Pen headerBorder(UITheme::Colors::BorderDark, 1.0f);
        g.DrawLine(&headerBorder, 0, l.headerH, width, l.headerH);

        // Logo Icon emblem
        UITheme::DrawRoundedRect(g, Gdiplus::RectF(22, 14, 34, 34), 8.0f, UITheme::Colors::Primary, Gdiplus::Color(0, 0, 0, 0), 0);
        Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::SolidBrush greenDot(UITheme::Colors::ActiveGreen);
        g.FillRectangle(&whiteBrush, 29, 21, 14, 4);
        g.FillEllipse(&greenDot, 46, 21, 4, 4);
        g.FillRectangle(&whiteBrush, 29, 28, 14, 4);
        g.FillEllipse(&greenDot, 46, 28, 4, 4);
        g.FillRectangle(&whiteBrush, 29, 35, 14, 4);
        g.FillEllipse(&greenDot, 46, 35, 4, 4);

        // Title
        Gdiplus::SolidBrush textWhite(UITheme::Colors::TextPrimary);
        Gdiplus::SolidBrush textMuted(UITheme::Colors::TextSecondary);
        g.DrawString(L"HOSTAGE", -1, &fontTitle, Gdiplus::PointF(66, 13), &textWhite);
        g.DrawString(L"Hosts File Manager & Domain Groups", -1, &fontSub, Gdiplus::PointF(67, 34), &textMuted);

        // Badges on Header Right
        float rightX = (float)width - 24.0f;

        // OS Badge
        std::wstring osText = g_app.osVersion;
        Gdiplus::RectF osLayout(0, 0, 1000, 100);
        Gdiplus::RectF osBounds;
        g.MeasureString(osText.c_str(), -1, &fontBadge, osLayout, NULL, &osBounds);
        float osBadgeW = osBounds.Width + 20.0f;
        rightX -= osBadgeW;
        UITheme::DrawBadge(g, osText, rightX, 19, UITheme::Colors::TextSecondary, Gdiplus::Color(255, 34, 38, 52), &fontBadge, 10, 4);

        // Admin Badge
        rightX -= 12.0f;
        std::wstring adminText = g_app.isAdmin ? L"ADMINISTRATOR" : L"LIMITED PRIVILEGES";
        Gdiplus::Color adminColor = g_app.isAdmin ? UITheme::Colors::ActiveGreen : UITheme::Colors::AccentAmber;
        Gdiplus::Color adminBg = g_app.isAdmin ? UITheme::Colors::ActiveGreenBg : Gdiplus::Color(255, 69, 39, 10);
        g.MeasureString(adminText.c_str(), -1, &fontBadge, osLayout, NULL, &osBounds);
        float adminBadgeW = osBounds.Width + 20.0f;
        rightX -= adminBadgeW;
        UITheme::DrawBadge(g, adminText, rightX, 19, adminColor, adminBg, &fontBadge, 10, 4);

        // ----------------------------------------------------
        // 2. Add Entry Panel Background
        // ----------------------------------------------------
        Gdiplus::RectF addBoxRect(20, (float)l.addBoxY + 8, (float)width - 40, (float)l.addBoxH - 12);
        UITheme::DrawRoundedRect(g, addBoxRect, 10.0f, UITheme::Colors::BgCard, UITheme::Colors::BorderDark, 1.0f);

        // Preset IP & Group pill labels
        UITheme::DrawBadge(g, L"127.0.0.1", 32, (float)l.addBoxY + 16, UITheme::Colors::AccentCyan, Gdiplus::Color(255, 20, 45, 65), &fontSmall, 6, 2);
        UITheme::DrawBadge(g, L"0.0.0.0", 100, (float)l.addBoxY + 16, UITheme::Colors::AccentAmber, Gdiplus::Color(255, 55, 40, 15), &fontSmall, 6, 2);
        
        UITheme::DrawBadge(g, L"[General]", 170, (float)l.addBoxY + 16, UITheme::Colors::TextSecondary, Gdiplus::Color(255, 36, 40, 56), &fontSmall, 6, 2);
        UITheme::DrawBadge(g, L"[Dev]", 238, (float)l.addBoxY + 16, UITheme::Colors::PrimaryText, Gdiplus::Color(255, 67, 56, 202), &fontSmall, 6, 2);
        UITheme::DrawBadge(g, L"[AdBlock]", 290, (float)l.addBoxY + 16, UITheme::Colors::Danger, Gdiplus::Color(255, 69, 15, 25), &fontSmall, 6, 2);
        UITheme::DrawBadge(g, L"[Privacy]", 360, (float)l.addBoxY + 16, UITheme::Colors::ActiveGreen, Gdiplus::Color(255, 10, 60, 45), &fontSmall, 6, 2);

        // ----------------------------------------------------
        // 3. Search & Filter Bar
        // ----------------------------------------------------
        float filterY = (float)l.filterY + 6;

        // Filter Tabs: All, Active, Disabled
        float tabX = 20.0f;
        auto drawTab = [&](const std::wstring& label, size_t count, bool isSelected, FilterMode mode) -> float {
            std::wstringstream ss;
            ss << label << L" (" << count << L")";
            std::wstring tabStr = ss.str();
            Gdiplus::RectF b;
            g.MeasureString(tabStr.c_str(), -1, &fontBold, Gdiplus::RectF(0, 0, 500, 100), NULL, &b);
            float tabW = b.Width + 20.0f;
            float tabH = 30.0f;

            Gdiplus::RectF tabRect(tabX, filterY, tabW, tabH);
            Gdiplus::Color bg = isSelected ? UITheme::Colors::Primary : Gdiplus::Color(255, 26, 29, 40);
            Gdiplus::Color border = isSelected ? UITheme::Colors::PrimaryHover : UITheme::Colors::BorderDark;
            Gdiplus::Color text = isSelected ? Gdiplus::Color(255, 255, 255, 255) : UITheme::Colors::TextSecondary;

            UITheme::DrawRoundedRect(g, tabRect, 6.0f, bg, border, 1.0f);

            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::SolidBrush textB(text);
            g.DrawString(tabStr.c_str(), -1, &fontBold, tabRect, &sf, &textB);

            tabX += tabW + 6.0f;
            return tabW;
        };

        drawTab(L"All", g_app.hosts.GetTotalCount(), g_app.filter == FilterMode::All, FilterMode::All);
        drawTab(L"Active", g_app.hosts.GetActiveCount(), g_app.filter == FilterMode::ActiveOnly, FilterMode::ActiveOnly);
        drawTab(L"Disabled", g_app.hosts.GetDisabledCount(), g_app.filter == FilterMode::DisabledOnly, FilterMode::DisabledOnly);

        // Group Filter Button
        float grpFilterX = tabX;
        std::wstring grpFilterLabel = g_app.selectedGroupFilter.empty() ? L"📁 All Groups ▼" : (L"📁 " + g_app.selectedGroupFilter + L" ▼");
        Gdiplus::RectF grpBounds;
        g.MeasureString(grpFilterLabel.c_str(), -1, &fontBold, Gdiplus::RectF(0, 0, 500, 100), NULL, &grpBounds);
        float grpFilterW = grpBounds.Width + 18.0f;
        Gdiplus::RectF grpFilterRect(grpFilterX, filterY, grpFilterW, 30.0f);
        Gdiplus::Color grpFilterBg = !g_app.selectedGroupFilter.empty() ? Gdiplus::Color(255, 67, 56, 202) : Gdiplus::Color(255, 32, 36, 52);
        UITheme::DrawRoundedRect(g, grpFilterRect, 6.0f, grpFilterBg, UITheme::Colors::BorderDark, 1.0f);
        Gdiplus::StringFormat sfCenter;
        sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
        sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(grpFilterLabel.c_str(), -1, &fontBold, grpFilterRect, &sfCenter, &textWhite);

        // Dedicated "Manage Groups" Button
        float mgmtBtnX = grpFilterX + grpFilterW + 6.0f;
        float mgmtBtnW = 92.0f;
        Gdiplus::RectF mgmtRect(mgmtBtnX, filterY, mgmtBtnW, 30.0f);
        UITheme::DrawRoundedRect(g, mgmtRect, 6.0f, Gdiplus::Color(255, 30, 34, 48), UITheme::Colors::BorderDark, 1.0f);
        g.DrawString(L"⚙️ Groups...", -1, &fontSmall, mgmtRect, &sfCenter, &textWhite);

        // Bulk action buttons on the right of filter bar
        float bulkRightX = (float)width - 24.0f;
        
        // "Disable All" button
        Gdiplus::RectF disAllRect(bulkRightX - 85, filterY, 85, 30);
        UITheme::DrawRoundedRect(g, disAllRect, 6.0f, Gdiplus::Color(255, 32, 35, 48), UITheme::Colors::BorderDark, 1.0f);
        g.DrawString(L"Disable All", -1, &fontSmall, disAllRect, &sfCenter, &textMuted);

        // "Enable All" button
        bulkRightX -= 95.0f;
        Gdiplus::RectF enAllRect(bulkRightX - 85, filterY, 85, 30);
        UITheme::DrawRoundedRect(g, enAllRect, 6.0f, Gdiplus::Color(255, 32, 35, 48), UITheme::Colors::BorderDark, 1.0f);
        g.DrawString(L"Enable All", -1, &fontSmall, enAllRect, &sfCenter, &textMuted);

        // ----------------------------------------------------
        // 4. Scrollable Host Entries List
        // ----------------------------------------------------
        int listTotalH = 0;
        for (const auto& r : g_app.visibleRows) {
            listTotalH += GetRowHeight(r) + 8;
        }

        g_app.maxScroll = max(0, listTotalH - l.listH);
        if (g_app.scrollOffset > g_app.maxScroll) g_app.scrollOffset = g_app.maxScroll;
        if (g_app.scrollOffset < 0) g_app.scrollOffset = 0;

        // Clip to list region
        Gdiplus::Rect listClip(0, l.listY, width, l.listH);
        g.SetClip(listClip);

        if (g_app.visibleRows.empty()) {
            std::wstring emptyMsg = g_app.hosts.GetTotalCount() == 0 
                ? L"No entries in hosts file. Use the bar above to add your first domain!"
                : L"No host entries match your current search, filter, or group.";
            Gdiplus::RectF emptyRect(20, (float)l.listY + 40, (float)width - 40, 60);
            g.DrawString(emptyMsg.c_str(), -1, &fontRegular, emptyRect, &sfCenter, &textMuted);
        } else {
            int curY = l.listY - g_app.scrollOffset;

            for (size_t i = 0; i < g_app.visibleRows.size(); ++i) {
                const auto& row = g_app.visibleRows[i];
                int rHeight = GetRowHeight(row);

                if (curY + rHeight >= l.listY && curY <= l.listY + l.listH) {
                    bool isHovered = ((int)i == g_app.hoveredRowIndex);
                    float cardW = (float)width - 48.0f;

                    if (row.type == ListRowType::GroupHeader) {
                        // Section Group Header
                        Gdiplus::RectF grpRect(20.0f, (float)curY, cardW, (float)rHeight);
                        UITheme::DrawRoundedRect(g, grpRect, 6.0f, Gdiplus::Color(255, 24, 28, 40), UITheme::Colors::BorderDark, 1.0f);

                        // Icon and Name
                        std::wstring grpText = L"📁  " + row.groupName;
                        g.DrawString(grpText.c_str(), -1, &fontBold, Gdiplus::PointF(32, (float)curY + 9.0f), &textWhite);

                        // Count badge
                        size_t totalInGrp = g_app.hosts.GetGroupCount(row.groupName);
                        size_t activeInGrp = g_app.hosts.GetGroupActiveCount(row.groupName);
                        std::wstringstream gss;
                        gss << totalInGrp << L" entries (" << activeInGrp << L" active)";
                        UITheme::DrawBadge(g, gss.str(), 60.0f + (float)row.groupName.size() * 9.5f, (float)curY + 7.0f, UITheme::Colors::TextSecondary, Gdiplus::Color(255, 34, 38, 54), &fontSmall, 6, 2);

                        // Toggle Group Button
                        float togW = 95.0f;
                        float togH = 24.0f;
                        float togX = (grpRect.X + grpRect.Width) - togW - 8.0f;
                        float togY = (float)curY + 6.0f;
                        bool togHover = (isHovered && g_app.hoveredButton == 4);
                        Gdiplus::Color togBg = togHover ? UITheme::Colors::PrimaryHover : Gdiplus::Color(255, 36, 40, 56);
                        UITheme::DrawRoundedRect(g, Gdiplus::RectF(togX, togY, togW, togH), 4.0f, togBg, UITheme::Colors::BorderDark, 1.0f);
                        g.DrawString(L"Toggle Group", -1, &fontSmall, Gdiplus::RectF(togX, togY, togW, togH), &sfCenter, &textWhite);

                    } else if (row.type == ListRowType::HostCard) {
                        // Host Entry Card
                        const HostItem* pItem = nullptr;
                        for (const auto& item : g_app.hosts.GetItems()) {
                            if (item.id == row.hostItemId) {
                                pItem = &item;
                                break;
                            }
                        }

                        if (pItem) {
                            Gdiplus::RectF cardRect(20.0f, (float)curY, cardW, (float)rHeight);
                            Gdiplus::Color cardBg = isHovered ? UITheme::Colors::BgCardHover : UITheme::Colors::BgCard;
                            Gdiplus::Color cardBorder = isHovered ? UITheme::Colors::BorderFocus : UITheme::Colors::BorderDark;
                            UITheme::DrawRoundedRect(g, cardRect, 8.0f, cardBg, cardBorder, 1.0f);

                            // Toggle Switch
                            float toggleX = 34.0f;
                            float toggleY = (float)curY + ((float)rHeight - 22.0f) / 2.0f;
                            bool toggleHover = (isHovered && g_app.hoveredButton == 1);
                            UITheme::DrawToggleSwitch(g, toggleX, toggleY, 40.0f, 22.0f, pItem->enabled, toggleHover);

                            // Status Badge
                            float badgeX = toggleX + 50.0f;
                            float badgeY = (float)curY + ((float)rHeight - 20.0f) / 2.0f;
                            if (pItem->enabled) {
                                UITheme::DrawBadge(g, L"ACTIVE", badgeX, badgeY, UITheme::Colors::ActiveGreen, UITheme::Colors::ActiveGreenBg, &fontBadge, 7, 2);
                            } else {
                                UITheme::DrawBadge(g, L"DISABLED", badgeX, badgeY, UITheme::Colors::DisabledGray, UITheme::Colors::DisabledGrayBg, &fontBadge, 7, 2);
                            }

                            // IP Address badge/pill
                            float ipX = badgeX + 75.0f;
                            float ipY = (float)curY + ((float)rHeight - 22.0f) / 2.0f;
                            UITheme::DrawBadge(g, pItem->ip, ipX, ipY, UITheme::Colors::AccentCyan, Gdiplus::Color(255, 18, 32, 46), &fontMono, 8, 3);

                            // Interactive Group Pill Badge (Clickable to change group)
                            float grpBadgeX = ipX + 130.0f;
                            std::wstring grpText = pItem->group.empty() ? L"General" : pItem->group;
                            bool grpBadgeHover = (isHovered && g_app.hoveredButton == 5);
                            Gdiplus::Color grpBadgeBg = grpBadgeHover ? Gdiplus::Color(255, 99, 102, 241) : Gdiplus::Color(255, 67, 56, 202);
                            UITheme::DrawBadge(g, grpText, grpBadgeX, ipY, UITheme::Colors::PrimaryText, grpBadgeBg, &fontBadge, 7, 2);

                            // Domain Name
                            float domX = grpBadgeX + (float)grpText.size() * 8.0f + 25.0f;
                            float domY = (float)curY + 12.0f;
                            Gdiplus::SolidBrush domBrush(pItem->enabled ? UITheme::Colors::TextPrimary : UITheme::Colors::TextMuted);
                            g.DrawString(pItem->domain.c_str(), -1, &fontDomain, Gdiplus::PointF(domX, domY), &domBrush);

                            // Comment if any
                            if (!pItem->comment.empty()) {
                                std::wstring comStr = L"# " + pItem->comment;
                                g.DrawString(comStr.c_str(), -1, &fontSmall, Gdiplus::PointF(domX, domY + 18.0f), &textMuted);
                            }

                            // Action Buttons on Right: Edit & Delete
                            float btnRight = (cardRect.X + cardRect.Width) - 14.0f;

                            // Delete button
                            float delW = 60.0f;
                            float delH = 28.0f;
                            float delX = btnRight - delW;
                            float delY = (float)curY + ((float)rHeight - delH) / 2.0f;
                            bool delHover = (isHovered && g_app.hoveredButton == 3);
                            Gdiplus::Color delBg = delHover ? UITheme::Colors::Danger : Gdiplus::Color(255, 38, 26, 32);
                            Gdiplus::Color delText = delHover ? Gdiplus::Color(255, 255, 255, 255) : UITheme::Colors::Danger;
                            UITheme::DrawRoundedRect(g, Gdiplus::RectF(delX, delY, delW, delH), 5.0f, delBg, UITheme::Colors::BorderDark, 1.0f);
                            Gdiplus::SolidBrush delTextB(delText);
                            g.DrawString(L"Delete", -1, &fontSmall, Gdiplus::RectF(delX, delY, delW, delH), &sfCenter, &delTextB);

                            // Edit button
                            float editW = 55.0f;
                            float editH = 28.0f;
                            float editX = delX - editW - 8.0f;
                            float editY = delY;
                            bool editHover = (isHovered && g_app.hoveredButton == 2);
                            Gdiplus::Color editBg = editHover ? UITheme::Colors::Primary : Gdiplus::Color(255, 32, 36, 50);
                            Gdiplus::Color editText = editHover ? Gdiplus::Color(255, 255, 255, 255) : UITheme::Colors::TextSecondary;
                            UITheme::DrawRoundedRect(g, Gdiplus::RectF(editX, editY, editW, editH), 5.0f, editBg, UITheme::Colors::BorderDark, 1.0f);
                            Gdiplus::SolidBrush editTextB(editText);
                            g.DrawString(L"Edit", -1, &fontSmall, Gdiplus::RectF(editX, editY, editW, editH), &sfCenter, &editTextB);
                        }
                    }
                }
                curY += rHeight + 8;
            }
        }

        // Scrollbar if needed
        if (g_app.maxScroll > 0) {
            float sbTrackX = (float)width - 16.0f;
            float sbTrackY = (float)l.listY;
            float sbTrackH = (float)l.listH;
            Gdiplus::SolidBrush sbTrackBrush(UITheme::Colors::ScrollTrack);
            g.FillRectangle(&sbTrackBrush, sbTrackX, sbTrackY, 8.0f, sbTrackH);

            float thumbH = max(30.0f, sbTrackH * ((float)l.listH / (float)listTotalH));
            float thumbY = sbTrackY + ((float)g_app.scrollOffset / (float)g_app.maxScroll) * (sbTrackH - thumbH);
            Gdiplus::RectF thumbRect(sbTrackX, thumbY, 8.0f, thumbH);
            UITheme::DrawRoundedRect(g, thumbRect, 4.0f, UITheme::Colors::ScrollThumb, Gdiplus::Color(0, 0, 0, 0), 0);
        }

        // Reset clip
        g.ResetClip();

        // ----------------------------------------------------
        // 5. Bottom Action & Status Bar
        // ----------------------------------------------------
        Gdiplus::SolidBrush bottomBg(UITheme::Colors::BgHeader);
        g.FillRectangle(&bottomBg, 0, l.bottomY, width, l.bottomH);
        g.DrawLine(&headerBorder, 0, l.bottomY, width, l.bottomY);

        // Status Toast or File Path
        float statusY = (float)l.bottomY + 22.0f;
        if (!g_app.statusToast.empty()) {
            Gdiplus::SolidBrush toastBrush(UITheme::Colors::ActiveGreen);
            g.DrawString(g_app.statusToast.c_str(), -1, &fontBold, Gdiplus::PointF(24, statusY), &toastBrush);
        } else {
            std::wstring fileInfo = g_app.hosts.GetFilePath();
            if (g_app.hosts.IsModified()) {
                fileInfo += L"  * [UNSAVED CHANGES]";
                Gdiplus::SolidBrush modBrush(UITheme::Colors::AccentAmber);
                g.DrawString(fileInfo.c_str(), -1, &fontRegular, Gdiplus::PointF(24, statusY), &modBrush);
            } else {
                g.DrawString(fileInfo.c_str(), -1, &fontSmall, Gdiplus::PointF(24, statusY), &textMuted);
            }
        }

        // Action Buttons on Right
        float actionRightX = (float)width - 24.0f;

        // Button 5: "Save Changes & Flush DNS" (Big Prominent Button)
        float saveW = 190.0f;
        float saveH = 38.0f;
        float saveX = actionRightX - saveW;
        float saveY = (float)l.bottomY + 13.0f;
        bool saveHover = (g_app.hoveredBottomBtn == 5);
        Gdiplus::Color saveBg = saveHover ? UITheme::Colors::PrimaryHover : UITheme::Colors::Primary;
        UITheme::DrawRoundedRect(g, Gdiplus::RectF(saveX, saveY, saveW, saveH), 6.0f, saveBg, Gdiplus::Color(0, 0, 0, 0), 0);
        g.DrawString(L"Save & Flush DNS", -1, &fontBold, Gdiplus::RectF(saveX, saveY, saveW, saveH), &sfCenter, &whiteBrush);

        // Button 4: "Flush DNS"
        actionRightX -= saveW + 10.0f;
        float flushW = 100.0f;
        float flushX = actionRightX - flushW;
        bool flushHover = (g_app.hoveredBottomBtn == 4);
        Gdiplus::Color flushBg = flushHover ? Gdiplus::Color(255, 45, 50, 70) : Gdiplus::Color(255, 30, 33, 46);
        UITheme::DrawRoundedRect(g, Gdiplus::RectF(flushX, saveY, flushW, saveH), 6.0f, flushBg, UITheme::Colors::BorderDark, 1.0f);
        g.DrawString(L"Flush DNS", -1, &fontRegular, Gdiplus::RectF(flushX, saveY, flushW, saveH), &sfCenter, &textWhite);

        // Button 3: "Backups"
        actionRightX -= flushW + 10.0f;
        float bakW = 85.0f;
        float bakX = actionRightX - bakW;
        bool bakHover = (g_app.hoveredBottomBtn == 3);
        Gdiplus::Color bakBg = bakHover ? Gdiplus::Color(255, 45, 50, 70) : Gdiplus::Color(255, 30, 33, 46);
        UITheme::DrawRoundedRect(g, Gdiplus::RectF(bakX, saveY, bakW, saveH), 6.0f, bakBg, UITheme::Colors::BorderDark, 1.0f);
        g.DrawString(L"Backups", -1, &fontRegular, Gdiplus::RectF(bakX, saveY, bakW, saveH), &sfCenter, &textWhite);

        // Button 2: "Notepad"
        actionRightX -= bakW + 10.0f;
        float npW = 80.0f;
        float npX = actionRightX - npW;
        bool npHover = (g_app.hoveredBottomBtn == 2);
        Gdiplus::Color npBg = npHover ? Gdiplus::Color(255, 45, 50, 70) : Gdiplus::Color(255, 30, 33, 46);
        UITheme::DrawRoundedRect(g, Gdiplus::RectF(npX, saveY, npW, saveH), 6.0f, npBg, UITheme::Colors::BorderDark, 1.0f);
        g.DrawString(L"Notepad", -1, &fontRegular, Gdiplus::RectF(npX, saveY, npW, saveH), &sfCenter, &textWhite);

        // Button 1: "Reload"
        actionRightX -= npW + 10.0f;
        float relW = 75.0f;
        float relX = actionRightX - relW;
        bool relHover = (g_app.hoveredBottomBtn == 1);
        Gdiplus::Color relBg = relHover ? Gdiplus::Color(255, 45, 50, 70) : Gdiplus::Color(255, 30, 33, 46);
        UITheme::DrawRoundedRect(g, Gdiplus::RectF(relX, saveY, relW, saveH), 6.0f, relBg, UITheme::Colors::BorderDark, 1.0f);
        g.DrawString(L"Reload", -1, &fontRegular, Gdiplus::RectF(relX, saveY, relW, saveH), &sfCenter, &textWhite);
    }

    // Blt memory DC to screen
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBM);
    DeleteObject(memBM);
    DeleteDC(memDC);
}

// Reposition child controls (Edit inputs, search box, add button)
void RepositionControls(int width, int height) {
    UILayout l = GetLayout(width, height);

    // Inputs inside Quick Add Bar
    int ipX = 30;
    int ipY = l.addBoxY + 38;
    int ipW = 120;
    int inputH = 26;
    MoveWindow(g_app.hEditIp, ipX, ipY, ipW, inputH, TRUE);

    int grpW = 110;
    int commentW = 160;
    int btnW = 105;

    int domX = ipX + ipW + 8;
    int domW = max(150, width - 40 - domX - grpW - commentW - btnW - 32);
    MoveWindow(g_app.hEditDomain, domX, ipY, domW, inputH, TRUE);

    int grpX = domX + domW + 8;
    MoveWindow(g_app.hEditGroup, grpX, ipY, grpW, inputH, TRUE);

    int comX = grpX + grpW + 8;
    MoveWindow(g_app.hEditComment, comX, ipY, commentW, inputH, TRUE);

    int addX = comX + commentW + 8;
    MoveWindow(g_app.hBtnAdd, addX, ipY - 1, btnW, inputH + 2, TRUE);

    // Search Box (placed on right side of Filter Bar)
    int searchW = 210;
    int searchX = width - 24 - 180 - searchW - 10;
    int searchY = l.filterY + 8;
    MoveWindow(g_app.hEditSearch, searchX, searchY, searchW, 26, TRUE);
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

static int GetRowTopY(int index, int listY, int scrollOffset) {
    int curY = listY - scrollOffset;
    for (int i = 0; i < index; ++i) {
        curY += GetRowHeight(g_app.visibleRows[i]) + 8;
    }
    return curY;
}

// Handle Mouse Click in UI
void HandleMouseClick(int x, int y) {
    RECT rc;
    GetClientRect(g_app.hWndMain, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    UILayout l = GetLayout(width, height);

    // Preset chips click
    if (y >= l.addBoxY + 14 && y <= l.addBoxY + 32) {
        if (x >= 32 && x <= 95) {
            SetWindowTextW(g_app.hEditIp, L"127.0.0.1");
            SetFocus(g_app.hEditDomain);
            return;
        } else if (x >= 100 && x <= 165) {
            SetWindowTextW(g_app.hEditIp, L"0.0.0.0");
            SetFocus(g_app.hEditDomain);
            return;
        } else if (x >= 170 && x <= 230) {
            SetWindowTextW(g_app.hEditGroup, L"General");
            SetFocus(g_app.hEditDomain);
            return;
        } else if (x >= 238 && x <= 285) {
            SetWindowTextW(g_app.hEditGroup, L"Dev");
            SetFocus(g_app.hEditDomain);
            return;
        } else if (x >= 290 && x <= 355) {
            SetWindowTextW(g_app.hEditGroup, L"AdBlock");
            SetFocus(g_app.hEditDomain);
            return;
        } else if (x >= 360 && x <= 430) {
            SetWindowTextW(g_app.hEditGroup, L"Privacy");
            SetFocus(g_app.hEditDomain);
            return;
        }
    }

    // Filter tabs & Group Selector click
    if (y >= l.filterY + 6 && y <= l.filterY + 36) {
        // Tab All
        if (x >= 20 && x <= 80) {
            g_app.filter = FilterMode::All;
            UpdateFilteredList();
            InvalidateRect(g_app.hWndMain, NULL, FALSE);
            return;
        } else if (x >= 88 && x <= 165) {
            g_app.filter = FilterMode::ActiveOnly;
            UpdateFilteredList();
            InvalidateRect(g_app.hWndMain, NULL, FALSE);
            return;
        } else if (x >= 173 && x <= 265) {
            g_app.filter = FilterMode::DisabledOnly;
            UpdateFilteredList();
            InvalidateRect(g_app.hWndMain, NULL, FALSE);
            return;
        } else if (x >= 273 && x <= 395) {
            // Group Filter dropdown
            ShowGroupFilterMenu(g_app.hWndMain);
            return;
        } else if (x >= 400 && x <= 495) {
            // Manage Groups button
            OpenManageGroupsWindow(g_app.hWndMain);
            return;
        }

        // Bulk action buttons
        float bulkRightX = (float)width - 24.0f;
        if (x >= bulkRightX - 85 && x <= bulkRightX) {
            g_app.hosts.SetAllEnabled(false);
            UpdateFilteredList();
            ShowToast(L"All entries set to Disabled.");
            return;
        }
        bulkRightX -= 95.0f;
        if (x >= bulkRightX - 85 && x <= bulkRightX) {
            g_app.hosts.SetAllEnabled(true);
            UpdateFilteredList();
            ShowToast(L"All entries set to Enabled.");
            return;
        }
    }

    // List Item click
    if (y >= l.listY && y <= l.bottomY) {
        int rowIndex = FindRowIndexAt(y, l.listY, g_app.scrollOffset);
        if (rowIndex >= 0 && rowIndex < (int)g_app.visibleRows.size()) {
            const auto& row = g_app.visibleRows[rowIndex];
            float cardW = (float)width - 48.0f;

            if (row.type == ListRowType::GroupHeader) {
                // Group Toggle button
                float togW = 95.0f;
                float togX = (20.0f + cardW) - togW - 8.0f;
                if (x >= togX && x <= togX + togW) {
                    g_app.hosts.ToggleGroup(row.groupName);
                    UpdateFilteredList();
                    ShowToast(L"Toggled group: " + row.groupName);
                    InvalidateRect(g_app.hWndMain, NULL, FALSE);
                    return;
                }
            } else if (row.type == ListRowType::HostCard) {
                int itemId = row.hostItemId;
                float btnRight = 20.0f + cardW - 14.0f;
                float delW = 60.0f;
                float delX = btnRight - delW;
                float editW = 55.0f;
                float editX = delX - editW - 8.0f;

                // Delete button
                if (x >= delX && x <= delX + delW) {
                    if (MessageBoxW(g_app.hWndMain, L"Are you sure you want to delete this host entry?", L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        g_app.hosts.DeleteItem(itemId);
                        UpdateFilteredList();
                        ShowToast(L"Entry deleted.");
                    }
                    return;
                }

                // Edit button
                if (x >= editX && x <= editX + editW) {
                    OpenEditWindow(g_app.hWndMain, itemId);
                    return;
                }

                // Group Pill Badge click -> Quick Assign Group Menu!
                float badgeX = 34.0f + 50.0f;
                float ipX = badgeX + 75.0f;
                float grpBadgeX = ipX + 130.0f;

                const HostItem* pItem = nullptr;
                for (const auto& item : g_app.hosts.GetItems()) {
                    if (item.id == itemId) { pItem = &item; break; }
                }

                if (pItem) {
                    std::wstring grpText = pItem->group.empty() ? L"General" : pItem->group;
                    float grpBadgeW = (float)grpText.size() * 8.5f + 18.0f;
                    if (x >= grpBadgeX && x <= grpBadgeX + grpBadgeW) {
                        ShowAssignGroupMenu(g_app.hWndMain, itemId);
                        return;
                    }
                }

                // Toggle Switch or left card click
                if (x >= 20 && x <= 180) {
                    g_app.hosts.ToggleItem(itemId);
                    UpdateFilteredList();
                    InvalidateRect(g_app.hWndMain, NULL, FALSE);
                    return;
                }
            }
        }
    }

    // Bottom action buttons
    if (y >= l.bottomY + 13 && y <= l.bottomY + 51) {
        float actionRightX = (float)width - 24.0f;
        
        float saveW = 190.0f;
        float saveX = actionRightX - saveW;
        if (x >= saveX && x <= saveX + saveW) {
            SendMessage(g_app.hWndMain, WM_COMMAND, IDC_BTN_SAVE, 0);
            return;
        }

        actionRightX -= saveW + 10.0f;
        float flushW = 100.0f;
        float flushX = actionRightX - flushW;
        if (x >= flushX && x <= flushX + flushW) {
            SendMessage(g_app.hWndMain, WM_COMMAND, IDC_BTN_FLUSHDNS, 0);
            return;
        }

        actionRightX -= flushW + 10.0f;
        float bakW = 85.0f;
        float bakX = actionRightX - bakW;
        if (x >= bakX && x <= bakX + bakW) {
            SendMessage(g_app.hWndMain, WM_COMMAND, IDC_BTN_BACKUP, 0);
            return;
        }

        actionRightX -= bakW + 10.0f;
        float npW = 80.0f;
        float npX = actionRightX - npW;
        if (x >= npX && x <= npX + npW) {
            SendMessage(g_app.hWndMain, WM_COMMAND, IDC_BTN_NOTEPAD, 0);
            return;
        }

        actionRightX -= npW + 10.0f;
        float relW = 75.0f;
        float relX = actionRightX - relW;
        if (x >= relX && x <= relX + relW) {
            SendMessage(g_app.hWndMain, WM_COMMAND, IDC_BTN_RELOAD, 0);
            return;
        }
    }
}

// Handle Mouse Movement (Hover states)
void HandleMouseMove(int x, int y) {
    RECT rc;
    GetClientRect(g_app.hWndMain, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    UILayout l = GetLayout(width, height);

    int prevHoveredRow = g_app.hoveredRowIndex;
    int prevHoveredBtn = g_app.hoveredButton;
    int prevBottomBtn = g_app.hoveredBottomBtn;

    g_app.hoveredRowIndex = -1;
    g_app.hoveredButton = 0;
    g_app.hoveredBottomBtn = 0;

    // Check list item hover
    if (y >= l.listY && y <= l.bottomY) {
        int rowIndex = FindRowIndexAt(y, l.listY, g_app.scrollOffset);
        if (rowIndex >= 0 && rowIndex < (int)g_app.visibleRows.size()) {
            g_app.hoveredRowIndex = rowIndex;
            const auto& row = g_app.visibleRows[rowIndex];
            float cardW = (float)width - 48.0f;

            if (row.type == ListRowType::GroupHeader) {
                float togW = 95.0f;
                float togX = (20.0f + cardW) - togW - 8.0f;
                if (x >= togX && x <= togX + togW) {
                    g_app.hoveredButton = 4; // GroupToggle
                }
            } else if (row.type == ListRowType::HostCard) {
                float btnRight = 20.0f + cardW - 14.0f;
                float delW = 60.0f;
                float delX = btnRight - delW;
                float editW = 55.0f;
                float editX = delX - editW - 8.0f;

                float badgeX = 34.0f + 50.0f;
                float ipX = badgeX + 75.0f;
                float grpBadgeX = ipX + 130.0f;

                const HostItem* pItem = nullptr;
                for (const auto& item : g_app.hosts.GetItems()) {
                    if (item.id == row.hostItemId) { pItem = &item; break; }
                }

                if (x >= 34.0f && x <= 74.0f) {
                    g_app.hoveredButton = 1; // Toggle
                } else if (x >= editX && x <= editX + editW) {
                    g_app.hoveredButton = 2; // Edit
                } else if (x >= delX && x <= delX + delW) {
                    g_app.hoveredButton = 3; // Delete
                } else if (pItem) {
                    std::wstring grpText = pItem->group.empty() ? L"General" : pItem->group;
                    float grpBadgeW = (float)grpText.size() * 8.5f + 18.0f;
                    if (x >= grpBadgeX && x <= grpBadgeX + grpBadgeW) {
                        g_app.hoveredButton = 5; // GroupBadge
                    }
                }
            }
        }
    }

    // Check bottom buttons hover
    if (y >= l.bottomY + 13 && y <= l.bottomY + 51) {
        float actionRightX = (float)width - 24.0f;
        float saveW = 190.0f;
        float saveX = actionRightX - saveW;
        if (x >= saveX && x <= saveX + saveW) {
            g_app.hoveredBottomBtn = 5;
        } else {
            actionRightX -= saveW + 10.0f;
            float flushW = 100.0f;
            float flushX = actionRightX - flushW;
            if (x >= flushX && x <= flushX + flushW) {
                g_app.hoveredBottomBtn = 4;
            } else {
                actionRightX -= flushW + 10.0f;
                float bakW = 85.0f;
                float bakX = actionRightX - bakW;
                if (x >= bakX && x <= bakX + bakW) {
                    g_app.hoveredBottomBtn = 3;
                } else {
                    actionRightX -= bakW + 10.0f;
                    float npW = 80.0f;
                    float npX = actionRightX - npW;
                    if (x >= npX && x <= npX + npW) {
                        g_app.hoveredBottomBtn = 2;
                    } else {
                        actionRightX -= npW + 10.0f;
                        float relW = 75.0f;
                        float relX = actionRightX - relW;
                        if (x >= relX && x <= relX + relW) {
                            g_app.hoveredBottomBtn = 1;
                        }
                    }
                }
            }
        }
    }

    if (prevHoveredRow != g_app.hoveredRowIndex || prevHoveredBtn != g_app.hoveredButton || prevBottomBtn != g_app.hoveredBottomBtn) {
        InvalidateRect(g_app.hWndMain, NULL, FALSE);
    }
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
            } else {
                MessageBoxW(hWnd, err.c_str(), L"Restore Error", MB_OK | MB_ICONERROR);
            }
        }
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
        g_app.hEditIp = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 24, hWnd, (HMENU)IDC_EDIT_IP, g_app.hInstance, NULL);
        g_app.hEditDomain = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 24, hWnd, (HMENU)IDC_EDIT_DOMAIN, g_app.hInstance, NULL);
        g_app.hEditGroup = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"General", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 24, hWnd, (HMENU)IDC_EDIT_GROUP, g_app.hInstance, NULL);
        g_app.hEditComment = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 24, hWnd, (HMENU)IDC_EDIT_COMMENT, g_app.hInstance, NULL);
        g_app.hBtnAdd = CreateWindowW(L"BUTTON", L"+ Add Entry", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 0, 100, 24, hWnd, (HMENU)IDC_BTN_ADD, g_app.hInstance, NULL);
        g_app.hEditSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 24, hWnd, (HMENU)IDC_EDIT_SEARCH, g_app.hInstance, NULL);

        SendMessage(g_app.hEditIp, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);
        SendMessage(g_app.hEditDomain, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);
        SendMessage(g_app.hEditGroup, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);
        SendMessage(g_app.hEditComment, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);
        SendMessage(g_app.hBtnAdd, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);
        SendMessage(g_app.hEditSearch, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);

        // Cue banners (placeholders)
        SendMessageW(g_app.hEditDomain, 0x1501 /*EM_SETCUEBANNER*/, TRUE, (LPARAM)L"Domain (e.g. site.local)");
        SendMessageW(g_app.hEditGroup, 0x1501 /*EM_SETCUEBANNER*/, TRUE, (LPARAM)L"Group (e.g. Dev)");
        SendMessageW(g_app.hEditComment, 0x1501 /*EM_SETCUEBANNER*/, TRUE, (LPARAM)L"Comment (optional)");
        SendMessageW(g_app.hEditSearch, 0x1501 /*EM_SETCUEBANNER*/, TRUE, (LPARAM)L"Search domains, IPs, groups...");

        // Subclass edit controls
        g_OldEditProc = (WNDPROC)SetWindowLongPtrW(g_app.hEditDomain, GWLP_WNDPROC, (LONG_PTR)DarkEditProc);
        SetWindowLongPtrW(g_app.hEditIp, GWLP_WNDPROC, (LONG_PTR)DarkEditProc);
        SetWindowLongPtrW(g_app.hEditGroup, GWLP_WNDPROC, (LONG_PTR)DarkEditProc);
        SetWindowLongPtrW(g_app.hEditComment, GWLP_WNDPROC, (LONG_PTR)DarkEditProc);

        // Load hosts file
        std::wstring loadErr;
        if (!g_app.hosts.Load(loadErr)) {
            MessageBoxW(hWnd, loadErr.c_str(), L"Hosts Manager Warning", MB_OK | MB_ICONWARNING);
        }
        UpdateFilteredList();
        return 0;
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
        lpMMI->ptMinTrackSize.x = 820;
        lpMMI->ptMinTrackSize.y = 500;
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

        // Track mouse leave
        TRACKMOUSEEVENT tme = { sizeof(tme) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hWnd;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE: {
        g_app.hoveredRowIndex = -1;
        g_app.hoveredButton = 0;
        g_app.hoveredBottomBtn = 0;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_app.scrollOffset -= (delta / WHEEL_DELTA) * 45;
        if (g_app.scrollOffset < 0) g_app.scrollOffset = 0;
        if (g_app.scrollOffset > g_app.maxScroll) g_app.scrollOffset = g_app.maxScroll;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_EDIT_SEARCH && code == EN_CHANGE) {
            wchar_t szSearch[512] = {0};
            GetWindowTextW(g_app.hEditSearch, szSearch, 512);
            g_app.searchQuery = szSearch;
            UpdateFilteredList();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (id == IDC_BTN_ADD) {
            wchar_t szIp[256] = {0};
            wchar_t szDomain[1024] = {0};
            wchar_t szGroup[256] = {0};
            wchar_t szComment[1024] = {0};
            GetWindowTextW(g_app.hEditIp, szIp, 256);
            GetWindowTextW(g_app.hEditDomain, szDomain, 1024);
            GetWindowTextW(g_app.hEditGroup, szGroup, 256);
            GetWindowTextW(g_app.hEditComment, szComment, 1024);

            if (!HostsManager::IsValidIp(szIp)) {
                MessageBoxW(hWnd, L"Please enter a valid IP address (e.g. 127.0.0.1 or 0.0.0.0).", L"Validation Error", MB_OK | MB_ICONWARNING);
                SetFocus(g_app.hEditIp);
                return 0;
            }
            if (!HostsManager::IsValidDomain(szDomain)) {
                MessageBoxW(hWnd, L"Please enter a valid domain name (e.g. domain.com).", L"Validation Error", MB_OK | MB_ICONWARNING);
                SetFocus(g_app.hEditDomain);
                return 0;
            }

            std::wstring grp = (szGroup[0] == L'\0') ? L"General" : szGroup;
            g_app.hosts.AddItem(szIp, szDomain, szComment, grp, true);
            UpdateFilteredList();
            SetWindowTextW(g_app.hEditDomain, L"");
            SetWindowTextW(g_app.hEditComment, L"");
            SetFocus(g_app.hEditDomain);
            ShowToast(L"Entry added to group [" + grp + L"].");
            return 0;
        }

        if (id == IDC_BTN_SAVE) {
            std::wstring saveErr;
            if (g_app.hosts.Save(true, saveErr)) {
                WinUtil::FlushDnsCache();
                ShowToast(L"Saved successfully & Windows DNS cache flushed!");
            } else {
                MessageBoxW(hWnd, saveErr.c_str(), L"Save Error", MB_OK | MB_ICONERROR);
            }
            return 0;
        }

        if (id == IDC_BTN_FLUSHDNS) {
            WinUtil::FlushDnsCache();
            ShowToast(L"Windows DNS cache flushed successfully!");
            return 0;
        }

        if (id == IDC_BTN_BACKUP) {
            ShowBackupsMenu(hWnd);
            return 0;
        }

        if (id == IDC_BTN_NOTEPAD) {
            WinUtil::OpenInNotepad(g_app.hosts.GetFilePath());
            return 0;
        }

        if (id == IDC_BTN_RELOAD) {
            std::wstring err;
            if (g_app.hosts.Load(err)) {
                UpdateFilteredList();
                ShowToast(L"Hosts file reloaded from disk.");
            } else {
                MessageBoxW(hWnd, err.c_str(), L"Reload Error", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND hCtl = (HWND)lParam;
        if (hCtl == g_app.hEditIp || hCtl == g_app.hEditDomain || hCtl == g_app.hEditGroup || hCtl == g_app.hEditComment || hCtl == g_app.hEditSearch) {
            SetTextColor(hdc, RGB(241, 245, 249));
            SetBkColor(hdc, RGB(21, 23, 32));
            static HBRUSH hbrInput = CreateSolidBrush(RGB(21, 23, 32));
            return (INT_PTR)hbrInput;
        }
        break;
    }

    case WM_CLOSE: {
        if (g_app.hosts.IsModified()) {
            int res = MessageBoxW(hWnd, L"You have unsaved changes in your hosts file. Do you want to save before exiting?", L"Unsaved Changes", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (res == IDYES) {
                std::wstring err;
                g_app.hosts.Save(true, err);
                WinUtil::FlushDnsCache();
            } else if (res == IDCANCEL) {
                return 0;
            }
        }
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY: {
        if (g_app.hFontRegular) DeleteObject(g_app.hFontRegular);
        if (g_app.hFontBold) DeleteObject(g_app.hFontBold);
        if (g_app.hFontSmall) DeleteObject(g_app.hFontSmall);
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Entry Point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_app.hInstance = hInstance;
    g_app.filter = FilterMode::All;
    g_app.scrollOffset = 0;
    g_app.hoveredRowIndex = -1;
    g_app.hoveredButton = 0;
    g_app.hoveredBottomBtn = 0;

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
    wc.hbrBackground = NULL; // We paint our own background
    wc.lpszClassName = L"HostageMainWindow";
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Failed to register window class.", L"Error", MB_ICONERROR);
        return 1;
    }

    // Default window dimensions
    int defaultW = 960;
    int defaultH = 650;
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
