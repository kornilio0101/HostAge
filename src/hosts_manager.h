#pragma once

#include <string>
#include <vector>
#include <memory>

struct HostItem {
    int id;                     // Unique UI ID
    bool enabled;               // True if active, False if commented out (#)
    std::wstring ip;            // IP address (IPv4 or IPv6)
    std::wstring domain;        // Domain name(s)
    std::wstring comment;       // Inline comment (without leading #)
    std::wstring group;         // Group name (e.g. L"General", L"Development")
    size_t lineIndex;           // Original line index in file
};

enum class FileLineType {
    RawCommentOrBlank,
    HostEntryItem
};

struct FileLine {
    FileLineType type;
    std::wstring rawContent;    // Used if RawCommentOrBlank
    int hostItemId;             // Used if HostEntryItem (references HostItem::id)
};

struct BackupInfo {
    std::wstring filename;
    std::wstring fullPath;
    std::wstring timestamp;
};

class HostsManager {
public:
    HostsManager();

    // Load hosts file from disk
    bool Load(std::wstring& outError);

    // Save hosts file to disk (creates backup automatically)
    bool Save(bool createBackup, std::wstring& outError);

    // Get all parsed host items
    std::vector<HostItem>& GetItems() { return m_items; }
    const std::vector<HostItem>& GetItems() const { return m_items; }

    // Add a new entry (default to enabled)
    int AddItem(const std::wstring& ip, const std::wstring& domain, const std::wstring& comment, const std::wstring& group = L"General", bool enabled = true);

    // Update an existing entry
    bool UpdateItem(int id, const std::wstring& ip, const std::wstring& domain, const std::wstring& comment, const std::wstring& group = L"General");

    // Toggle enabled state of an entry
    bool ToggleItem(int id);

    // Set enabled state directly
    bool SetItemEnabled(int id, bool enabled);

    // Delete an entry
    bool DeleteItem(int id);

    // Groups
    std::vector<std::wstring> GetGroups() const;
    bool AddGroup(const std::wstring& groupName);
    bool RenameGroup(const std::wstring& oldName, const std::wstring& newName);
    bool RemoveGroup(const std::wstring& groupName); // Reverts domains to Ungrouped, NOT deleted!
    bool AssignItemGroup(int itemId, const std::wstring& groupName);
    void SetGroupEnabled(const std::wstring& group, bool enabled);
    void ToggleGroup(const std::wstring& group);
    size_t GetGroupCount(const std::wstring& group) const;
    size_t GetGroupActiveCount(const std::wstring& group) const;

    // Enable or disable all entries
    void SetAllEnabled(bool enabled);

    // Check if there are unsaved modifications
    bool IsModified() const { return m_isModified; }
    void SetModified(bool mod = true) { m_isModified = mod; }

    // Backups
    std::vector<BackupInfo> GetAvailableBackups() const;
    bool RestoreBackup(const std::wstring& backupPath, std::wstring& outError);
    bool CreateBackupNow(std::wstring& outBackupPath, std::wstring& outError);

    // Import / Export
    bool ExportToFile(const std::wstring& targetPath, std::wstring& outError);
    bool ImportFromFile(const std::wstring& sourcePath, std::wstring& outError);

    // Statistics
    size_t GetTotalCount() const { return m_items.size(); }
    size_t GetActiveCount() const;
    size_t GetDisabledCount() const;

    // Path
    std::wstring GetFilePath() const { return m_hostsPath; }

    // Helpers
    static bool IsValidIp(const std::wstring& ip);
    static bool IsValidDomain(const std::wstring& domain);

private:
    std::wstring m_hostsPath;
    std::vector<HostItem> m_items;
    std::vector<FileLine> m_lines;
    std::vector<std::wstring> m_customGroups;
    bool m_isModified;
    int m_nextId;

    void ParseLines(const std::vector<std::wstring>& rawLines);
    bool TryParseHostLine(const std::wstring& line, HostItem& outItem, bool& outWasCommented);
    std::wstring FormatHostLine(const HostItem& item) const;
};
