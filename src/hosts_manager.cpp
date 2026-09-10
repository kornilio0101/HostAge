#include "hosts_manager.h"
#include "win_util.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cwctype>

static std::wstring Trim(const std::wstring& s) {
    auto start = s.begin();
    while (start != s.end() && std::iswspace(*start)) {
        start++;
    }
    auto end = s.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::iswspace(*end));
    return std::wstring(start, end + 1);
}

HostsManager::HostsManager()
    : m_isModified(false), m_nextId(1)
{
    m_hostsPath = WinUtil::GetHostsFilePath();
}

bool HostsManager::IsValidIp(const std::wstring& ip) {
    if (ip.empty()) return false;
    
    // Check IPv4
    int dots = 0;
    bool hasDigits = false;
    bool allValidIpv4Chars = true;
    for (wchar_t c : ip) {
        if (c == L'.') {
            dots++;
        } else if (std::iswdigit(c)) {
            hasDigits = true;
        } else {
            allValidIpv4Chars = false;
            break;
        }
    }
    if (allValidIpv4Chars && dots == 3 && hasDigits) {
        // Validate octets 0-255
        std::wstringstream ss(ip);
        std::wstring part;
        int count = 0;
        while (std::getline(ss, part, L'.')) {
            if (part.empty() || part.size() > 3) return false;
            int val = std::stoi(part);
            if (val < 0 || val > 255) return false;
            count++;
        }
        return (count == 4);
    }

    // Check IPv6 (contains ':' and hex digits)
    int colons = 0;
    bool allValidIpv6Chars = true;
    for (wchar_t c : ip) {
        if (c == L':') {
            colons++;
        } else if (std::iswxdigit(c)) {
            // ok
        } else {
            allValidIpv6Chars = false;
            break;
        }
    }
    if (allValidIpv6Chars && colons >= 2) {
        return true;
    }

    return false;
}

bool HostsManager::IsValidDomain(const std::wstring& domain) {
    if (domain.empty() || domain.size() > 255) return false;
    for (wchar_t c : domain) {
        if (!std::iswalnum(c) && c != L'.' && c != L'-' && c != L'_' && c != L' ') {
            return false;
        }
    }
    return true;
}

bool HostsManager::TryParseHostLine(const std::wstring& line, HostItem& outItem, bool& outWasCommented) {
    std::wstring trimmed = Trim(line);
    if (trimmed.empty()) return false;

    outWasCommented = false;
    if (trimmed[0] == L'#') {
        outWasCommented = true;
        // Strip leading '#' characters and whitespace
        size_t idx = 0;
        while (idx < trimmed.size() && (trimmed[idx] == L'#' || std::iswspace(trimmed[idx]))) {
            idx++;
        }
        trimmed = trimmed.substr(idx);
    }

    if (trimmed.empty()) return false;

    // Split by inline comment '#' if present
    std::wstring contentPart = trimmed;
    std::wstring commentPart = L"";
    size_t commentPos = trimmed.find(L'#');
    if (commentPos != std::wstring::npos) {
        contentPart = Trim(trimmed.substr(0, commentPos));
        commentPart = Trim(trimmed.substr(commentPos + 1));
    }

    if (contentPart.empty()) return false;

    // Tokens: First token must be IP, second and subsequent tokens are domain(s)
    std::wstringstream ss(contentPart);
    std::wstring ipToken, domainToken;
    ss >> ipToken;
    if (!IsValidIp(ipToken)) {
        return false;
    }

    std::wstring allDomains;
    while (ss >> domainToken) {
        if (!allDomains.empty()) allDomains += L" ";
        allDomains += domainToken;
    }

    if (allDomains.empty()) {
        return false;
    }

    outItem.ip = ipToken;
    outItem.domain = allDomains;
    outItem.comment = commentPart;
    outItem.enabled = !outWasCommented;
    return true;
}

static bool TryExtractGroupHeader(const std::wstring& line, std::wstring& outGroupName) {
    std::wstring s = Trim(line);
    if (s.empty() || s[0] != L'#') return false;

    // Remove leading # and spaces
    size_t i = 1;
    while (i < s.size() && (s[i] == L'#' || std::iswspace(s[i]))) i++;
    std::wstring content = Trim(s.substr(i));
    if (content.empty()) return false;

    // Pattern 1: [GroupName]
    if (content.size() >= 3 && content.front() == L'[' && content.back() == L']') {
        std::wstring g = Trim(content.substr(1, content.size() - 2));
        if (!g.empty() && g.size() < 40) {
            outGroupName = g;
            return true;
        }
    }
    // Pattern 2: === GroupName === or --- GroupName ---
    if ((content.rfind(L"===", 0) == 0 && content.size() > 6) ||
        (content.rfind(L"---", 0) == 0 && content.size() > 6)) {
        size_t start = content.find_first_not_of(L"=- ");
        size_t end = content.find_last_not_of(L"=- ");
        if (start != std::wstring::npos && end != std::wstring::npos && end >= start) {
            std::wstring g = Trim(content.substr(start, end - start + 1));
            if (!g.empty() && g.size() < 40) {
                outGroupName = g;
                return true;
            }
        }
    }
    // Pattern 3: Group: GroupName
    if (content.rfind(L"Group:", 0) == 0 || content.rfind(L"group:", 0) == 0) {
        std::wstring g = Trim(content.substr(6));
        if (!g.empty() && g.size() < 40) {
            outGroupName = g;
            return true;
        }
    }

    return false;
}

void HostsManager::ParseLines(const std::vector<std::wstring>& rawLines) {
    m_items.clear();
    m_lines.clear();

    std::wstring currentGroup = L"General";

    for (size_t i = 0; i < rawLines.size(); ++i) {
        const auto& line = rawLines[i];
        
        std::wstring headerGroup;
        if (TryExtractGroupHeader(line, headerGroup)) {
            currentGroup = headerGroup;
        }

        HostItem item;
        bool wasCommented = false;

        if (TryParseHostLine(line, item, wasCommented)) {
            item.id = m_nextId++;
            item.lineIndex = i;

            // Check if comment has [Group] tag
            if (!item.comment.empty() && item.comment.front() == L'[' && item.comment.find(L']') != std::wstring::npos) {
                size_t closeP = item.comment.find(L']');
                item.group = Trim(item.comment.substr(1, closeP - 1));
                item.comment = Trim(item.comment.substr(closeP + 1));
            } else {
                item.group = currentGroup;
            }

            m_items.push_back(item);

            FileLine fl;
            fl.type = FileLineType::HostEntryItem;
            fl.hostItemId = item.id;
            m_lines.push_back(fl);
        } else {
            FileLine fl;
            fl.type = FileLineType::RawCommentOrBlank;
            fl.rawContent = line;
            fl.hostItemId = 0;
            m_lines.push_back(fl);
        }
    }
}

bool HostsManager::Load(std::wstring& outError) {
    HANDLE hFile = CreateFileW(
        m_hostsPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            // hosts file doesn't exist yet, we can create empty
            m_items.clear();
            m_lines.clear();
            m_isModified = false;
            return true;
        }
        outError = L"Could not open hosts file (Error " + std::to_wstring(err) + L"). Administrator privileges are required.";
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        outError = L"Failed to query hosts file size.";
        return false;
    }

    std::vector<char> buffer(fileSize + 1, 0);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL)) {
        CloseHandle(hFile);
        outError = L"Failed to read hosts file content.";
        return false;
    }
    CloseHandle(hFile);

    // Decode buffer (detect UTF-8 BOM or UTF-8 or ANSI)
    const char* p = buffer.data();
    size_t len = bytesRead;
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3;
        len -= 3;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, p, (int)len, NULL, 0);
    std::wstring wideContent;
    if (wlen > 0) {
        wideContent.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, p, (int)len, &wideContent[0], wlen);
    } else {
        // Fallback to ANSI
        wlen = MultiByteToWideChar(CP_ACP, 0, p, (int)len, NULL, 0);
        if (wlen > 0) {
            wideContent.resize(wlen);
            MultiByteToWideChar(CP_ACP, 0, p, (int)len, &wideContent[0], wlen);
        }
    }

    // Split lines
    std::vector<std::wstring> lines;
    std::wstring curLine;
    for (wchar_t ch : wideContent) {
        if (ch == L'\r') {
            continue;
        } else if (ch == L'\n') {
            lines.push_back(curLine);
            curLine.clear();
        } else {
            curLine += ch;
        }
    }
    if (!curLine.empty()) {
        lines.push_back(curLine);
    }

    ParseLines(lines);
    m_isModified = false;
    return true;
}

std::wstring HostsManager::FormatHostLine(const HostItem& item) const {
    std::wstring res;
    if (!item.enabled) {
        res += L"# ";
    }
    res += item.ip;

    // Spacing alignment
    if (item.ip.size() < 16) {
        res.append(16 - item.ip.size(), L' ');
    } else {
        res += L" ";
    }

    res += item.domain;

    if (!item.comment.empty()) {
        res += L"    # ";
        res += item.comment;
    }

    return res;
}

bool HostsManager::CreateBackupNow(std::wstring& outBackupPath, std::wstring& outError) {
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t timestamp[64];
    swprintf_s(timestamp, L"%04d%02d%02d_%02d%02d%02d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    outBackupPath = m_hostsPath + L".bak_" + timestamp;

    if (!CopyFileW(m_hostsPath.c_str(), outBackupPath.c_str(), FALSE)) {
        DWORD err = GetLastError();
        outError = L"Backup creation failed (Error " + std::to_wstring(err) + L").";
        return false;
    }
    return true;
}

bool HostsManager::Save(bool createBackup, std::wstring& outError) {
    if (createBackup) {
        std::wstring backupPath, backupErr;
        CreateBackupNow(backupPath, backupErr);
    }

    // Prepare content
    std::wstring outputText;
    std::vector<int> handledIds;

    for (const auto& fl : m_lines) {
        if (fl.type == FileLineType::RawCommentOrBlank) {
            outputText += fl.rawContent;
            outputText += L"\r\n";
        } else if (fl.type == FileLineType::HostEntryItem) {
            auto it = std::find_if(m_items.begin(), m_items.end(), [&](const HostItem& hi) {
                return hi.id == fl.hostItemId;
            });
            if (it != m_items.end()) {
                outputText += FormatHostLine(*it);
                outputText += L"\r\n";
                handledIds.push_back(it->id);
            }
        }
    }

    // Any newly added items that were not in m_lines yet
    std::wstring lastGroup = L"";
    for (const auto& item : m_items) {
        if (std::find(handledIds.begin(), handledIds.end(), item.id) == handledIds.end()) {
            std::wstring g = item.group.empty() ? L"General" : item.group;
            if (g != lastGroup && g != L"General") {
                outputText += L"\r\n# [";
                outputText += g;
                outputText += L"]\r\n";
                lastGroup = g;
            }
            outputText += FormatHostLine(item);
            outputText += L"\r\n";
        }
    }

    // Convert wide text to UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, outputText.c_str(), (int)outputText.size(), NULL, 0, NULL, NULL);
    std::string utf8Data;
    if (utf8Len > 0) {
        utf8Data.resize(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0, outputText.c_str(), (int)outputText.size(), &utf8Data[0], utf8Len, NULL, NULL);
    }

    // Write to hosts file
    HANDLE hFile = CreateFileW(
        m_hostsPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        outError = L"Cannot write to hosts file (Error " + std::to_wstring(err) + L"). Ensure application is running as Administrator.";
        return false;
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(hFile, utf8Data.data(), (DWORD)utf8Data.size(), &bytesWritten, NULL)) {
        CloseHandle(hFile);
        outError = L"Error writing data to hosts file.";
        return false;
    }

    FlushFileBuffers(hFile);
    CloseHandle(hFile);

    m_isModified = false;
    return true;
}

int HostsManager::AddItem(const std::wstring& ip, const std::wstring& domain, const std::wstring& comment, const std::wstring& group, bool enabled) {
    HostItem item;
    item.id = m_nextId++;
    item.ip = Trim(ip);
    item.domain = Trim(domain);
    item.comment = Trim(comment);
    item.group = Trim(group.empty() ? L"General" : group);
    item.enabled = enabled;
    item.lineIndex = m_lines.size();

    m_items.push_back(item);

    FileLine fl;
    fl.type = FileLineType::HostEntryItem;
    fl.hostItemId = item.id;
    m_lines.push_back(fl);

    m_isModified = true;
    return item.id;
}

bool HostsManager::UpdateItem(int id, const std::wstring& ip, const std::wstring& domain, const std::wstring& comment, const std::wstring& group) {
    for (auto& item : m_items) {
        if (item.id == id) {
            item.ip = Trim(ip);
            item.domain = Trim(domain);
            item.comment = Trim(comment);
            item.group = Trim(group.empty() ? L"General" : group);
            m_isModified = true;
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> HostsManager::GetGroups() const {
    std::vector<std::wstring> groups;
    bool hasDefault = false;

    for (const auto& item : m_items) {
        std::wstring g = item.group.empty() ? L"General" : item.group;
        if (g == L"General" || g == L"Ungrouped") {
            hasDefault = true;
        } else if (std::find(groups.begin(), groups.end(), g) == groups.end()) {
            groups.push_back(g);
        }
    }

    for (const auto& cg : m_customGroups) {
        if (cg == L"General" || cg == L"Ungrouped") {
            hasDefault = true;
        } else if (std::find(groups.begin(), groups.end(), cg) == groups.end()) {
            groups.push_back(cg);
        }
    }

    std::sort(groups.begin(), groups.end());
    if (hasDefault || groups.empty()) {
        groups.insert(groups.begin(), L"General");
    }
    return groups;
}

bool HostsManager::AddGroup(const std::wstring& groupName) {
    std::wstring g = Trim(groupName);
    if (g.empty()) return false;
    auto groups = GetGroups();
    if (std::find(groups.begin(), groups.end(), g) == groups.end()) {
        m_customGroups.push_back(g);
        m_isModified = true;
        return true;
    }
    return false;
}

bool HostsManager::RenameGroup(const std::wstring& oldName, const std::wstring& newName) {
    std::wstring o = Trim(oldName);
    std::wstring n = Trim(newName);
    if (o.empty() || n.empty() || o == n) return false;

    bool found = false;
    for (auto& item : m_items) {
        std::wstring g = item.group.empty() ? L"General" : item.group;
        if (g == o) {
            item.group = n;
            found = true;
        }
    }

    for (auto& cg : m_customGroups) {
        if (cg == o) {
            cg = n;
            found = true;
        }
    }

    if (found) {
        m_isModified = true;
        return true;
    }
    return false;
}

bool HostsManager::RemoveGroup(const std::wstring& groupName) {
    std::wstring g = Trim(groupName);
    if (g.empty()) return false;

    // CRITICAL: Revert all items in this group to "Ungrouped", do NOT delete them!
    bool changed = false;
    for (auto& item : m_items) {
        std::wstring ig = item.group.empty() ? L"General" : item.group;
        if (ig == g) {
            item.group = L"Ungrouped";
            changed = true;
        }
    }

    auto it = std::remove(m_customGroups.begin(), m_customGroups.end(), g);
    if (it != m_customGroups.end()) {
        m_customGroups.erase(it, m_customGroups.end());
        changed = true;
    }

    if (changed) {
        m_isModified = true;
        return true;
    }
    return false;
}

bool HostsManager::AssignItemGroup(int itemId, const std::wstring& groupName) {
    std::wstring g = Trim(groupName.empty() ? L"Ungrouped" : groupName);
    for (auto& item : m_items) {
        if (item.id == itemId) {
            if (item.group != g) {
                item.group = g;
                m_isModified = true;
            }
            return true;
        }
    }
    return false;
}

void HostsManager::SetGroupEnabled(const std::wstring& group, bool enabled) {
    bool changed = false;
    for (auto& item : m_items) {
        std::wstring g = item.group.empty() ? L"General" : item.group;
        if (g == group) {
            if (item.enabled != enabled) {
                item.enabled = enabled;
                changed = true;
            }
        }
    }
    if (changed) {
        m_isModified = true;
    }
}

void HostsManager::ToggleGroup(const std::wstring& group) {
    // If any item in the group is disabled, enable all; else disable all
    bool hasDisabled = false;
    for (const auto& item : m_items) {
        std::wstring g = item.group.empty() ? L"General" : item.group;
        if (g == group && !item.enabled) {
            hasDisabled = true;
            break;
        }
    }
    SetGroupEnabled(group, hasDisabled);
}

size_t HostsManager::GetGroupCount(const std::wstring& group) const {
    size_t count = 0;
    for (const auto& item : m_items) {
        std::wstring g = item.group.empty() ? L"General" : item.group;
        if (g == group) count++;
    }
    return count;
}

size_t HostsManager::GetGroupActiveCount(const std::wstring& group) const {
    size_t count = 0;
    for (const auto& item : m_items) {
        std::wstring g = item.group.empty() ? L"General" : item.group;
        if (g == group && item.enabled) count++;
    }
    return count;
}

bool HostsManager::ToggleItem(int id) {
    for (auto& item : m_items) {
        if (item.id == id) {
            item.enabled = !item.enabled;
            m_isModified = true;
            return true;
        }
    }
    return false;
}

bool HostsManager::SetItemEnabled(int id, bool enabled) {
    for (auto& item : m_items) {
        if (item.id == id) {
            if (item.enabled != enabled) {
                item.enabled = enabled;
                m_isModified = true;
            }
            return true;
        }
    }
    return false;
}

bool HostsManager::DeleteItem(int id) {
    auto it = std::remove_if(m_items.begin(), m_items.end(), [id](const HostItem& hi) {
        return hi.id == id;
    });
    if (it != m_items.end()) {
        m_items.erase(it, m_items.end());

        // Also remove from m_lines
        auto lit = std::remove_if(m_lines.begin(), m_lines.end(), [id](const FileLine& fl) {
            return (fl.type == FileLineType::HostEntryItem && fl.hostItemId == id);
        });
        m_lines.erase(lit, m_lines.end());

        m_isModified = true;
        return true;
    }
    return false;
}

void HostsManager::SetAllEnabled(bool enabled) {
    bool changed = false;
    for (auto& item : m_items) {
        if (item.enabled != enabled) {
            item.enabled = enabled;
            changed = true;
        }
    }
    if (changed) {
        m_isModified = true;
    }
}

size_t HostsManager::GetActiveCount() const {
    size_t count = 0;
    for (const auto& item : m_items) {
        if (item.enabled) count++;
    }
    return count;
}

size_t HostsManager::GetDisabledCount() const {
    size_t count = 0;
    for (const auto& item : m_items) {
        if (!item.enabled) count++;
    }
    return count;
}

std::vector<BackupInfo> HostsManager::GetAvailableBackups() const {
    std::vector<BackupInfo> backups;

    // Search directory of hosts file
    size_t lastSlash = m_hostsPath.find_last_of(L"\\/");
    std::wstring dir = (lastSlash != std::wstring::npos) ? m_hostsPath.substr(0, lastSlash + 1) : L"";
    std::wstring searchPattern = dir + L"hosts.bak*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                BackupInfo bi;
                bi.filename = ffd.cFileName;
                bi.fullPath = dir + ffd.cFileName;
                
                // Parse timestamp from name if available
                std::wstring name = ffd.cFileName;
                size_t tsPos = name.find(L".bak_");
                if (tsPos != std::wstring::npos) {
                    bi.timestamp = name.substr(tsPos + 5);
                } else {
                    bi.timestamp = L"Manual";
                }
                backups.push_back(bi);
            }
        } while (FindNextFileW(hFind, &ffd) != 0);
        FindClose(hFind);
    }

    // Sort newest first
    std::sort(backups.begin(), backups.end(), [](const BackupInfo& a, const BackupInfo& b) {
        return a.filename > b.filename;
    });

    return backups;
}

bool HostsManager::RestoreBackup(const std::wstring& backupPath, std::wstring& outError) {
    if (!CopyFileW(backupPath.c_str(), m_hostsPath.c_str(), FALSE)) {
        DWORD err = GetLastError();
        outError = L"Restore failed (Error " + std::to_wstring(err) + L").";
        return false;
    }
    return Load(outError);
}

bool HostsManager::ExportToFile(const std::wstring& targetPath, std::wstring& outError) {
    std::wstring outputText;
    for (const auto& fl : m_lines) {
        if (fl.type == FileLineType::RawCommentOrBlank) {
            outputText += fl.rawContent;
            outputText += L"\r\n";
        } else if (fl.type == FileLineType::HostEntryItem) {
            auto it = std::find_if(m_items.begin(), m_items.end(), [&](const HostItem& hi) {
                return hi.id == fl.hostItemId;
            });
            if (it != m_items.end()) {
                outputText += FormatHostLine(*it);
                outputText += L"\r\n";
            }
        }
    }

    HANDLE hFile = CreateFileW(
        targetPath.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        outError = L"Could not export file (Error " + std::to_wstring(err) + L").";
        return false;
    }

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, outputText.c_str(), (int)outputText.size(), NULL, 0, NULL, NULL);
    std::string utf8Str;
    if (utf8Len > 0) {
        utf8Str.resize(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0, outputText.c_str(), (int)outputText.size(), &utf8Str[0], utf8Len, NULL, NULL);
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(hFile, utf8Str.data(), (DWORD)utf8Str.size(), &bytesWritten, NULL)) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        outError = L"Failed writing export file (Error " + std::to_wstring(err) + L").";
        return false;
    }
    CloseHandle(hFile);
    return true;
}

bool HostsManager::ImportFromFile(const std::wstring& sourcePath, std::wstring& outError) {
    HANDLE hFile = CreateFileW(
        sourcePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        outError = L"Could not open source file (Error " + std::to_wstring(err) + L").";
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        outError = L"Failed to query file size.";
        return false;
    }

    std::vector<char> buffer(fileSize + 1, 0);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL)) {
        CloseHandle(hFile);
        outError = L"Failed to read file.";
        return false;
    }
    CloseHandle(hFile);

    const char* p = buffer.data();
    size_t len = bytesRead;
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3;
        len -= 3;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, p, (int)len, NULL, 0);
    std::wstring wideContent;
    if (wlen > 0) {
        wideContent.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, p, (int)len, &wideContent[0], wlen);
    } else {
        wlen = MultiByteToWideChar(CP_ACP, 0, p, (int)len, NULL, 0);
        if (wlen > 0) {
            wideContent.resize(wlen);
            MultiByteToWideChar(CP_ACP, 0, p, (int)len, &wideContent[0], wlen);
        }
    }

    std::vector<std::wstring> lines;
    std::wstring curLine;
    for (wchar_t ch : wideContent) {
        if (ch == L'\r') continue;
        if (ch == L'\n') {
            lines.push_back(curLine);
            curLine.clear();
        } else {
            curLine += ch;
        }
    }
    if (!curLine.empty()) lines.push_back(curLine);

    int importedCount = 0;
    for (const auto& l : lines) {
        HostItem item;
        bool wasCommented = false;
        if (TryParseHostLine(l, item, wasCommented)) {
            AddItem(item.ip, item.domain, item.comment, L"Imported", item.enabled);
            importedCount++;
        }
    }

    if (importedCount == 0) {
        outError = L"No valid hosts entries found in the imported file.";
        return false;
    }

    m_isModified = true;
    return true;
}

