// Process identities and cooperative JSON locks for the test-only vinput CLI.
// Existing locks are NEVER reclaimed automatically, even if their status is stale.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <map>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <utility>

namespace vi_session {

inline std::uint64_t FileTimeValue(const FILETIME& t) {
    ULARGE_INTEGER v{}; v.LowPart = t.dwLowDateTime; v.HighPart = t.dwHighDateTime; return v.QuadPart;
}
inline std::uint64_t NowUnixMs() {
    FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
    return FileTimeValue(ft) / 10000ULL - 11644473600000ULL;
}
inline std::uint64_t ProcessCreationTime(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    FILETIME c{}, e{}, k{}, u{}; bool ok = GetProcessTimes(h, &c, &e, &k, &u) != FALSE;
    CloseHandle(h); return ok ? FileTimeValue(c) : 0;
}
inline DWORD ParentProcessId() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e{}; e.dwSize = sizeof(e); DWORD parent = 0;
    if (Process32FirstW(snapshot, &e)) do {
        if (e.th32ProcessID == GetCurrentProcessId()) { parent = e.th32ParentProcessID; break; }
    } while (Process32NextW(snapshot, &e));
    CloseHandle(snapshot); return parent;
}
// Access denied is unknown/alive, never grounds for declaring another owner's lock stale.
inline bool ProcessIdentityDead(DWORD pid, std::uint64_t creation) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!h) return GetLastError() == ERROR_INVALID_PARAMETER;
    bool dead = WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
    FILETIME c{}, e{}, k{}, u{};
    if (!dead && creation && GetProcessTimes(h, &c, &e, &k, &u)) dead = FileTimeValue(c) != creation;
    CloseHandle(h); return dead;
}

struct FileIdentity {
    DWORD volume = 0, high = 0, low = 0;
    bool valid = false;
};
inline FileIdentity IdentityOf(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION i{}; FileIdentity f;
    if (GetFileInformationByHandle(h, &i)) { f.volume = i.dwVolumeSerialNumber; f.high = i.nFileIndexHigh; f.low = i.nFileIndexLow; f.valid = true; }
    return f;
}
inline bool SameIdentity(const FileIdentity& a, const FileIdentity& b) {
    return a.valid && b.valid && a.volume == b.volume && a.high == b.high && a.low == b.low;
}

inline std::string JsonEscape(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { r += '\\'; r += static_cast<char>(c); }
        else if (c < 0x20) { char b[7]; sprintf_s(b, "\\u%04x", c); r += b; }
        else r += static_cast<char>(c);
    }
    return r;
}

namespace detail {
struct JsonValue { std::string text; bool quoted = false; };
class LockJsonReader {
    const std::string& s_; size_t p_ = 0;
    void Space() { while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\r' || s_[p_] == '\n' || s_[p_] == '\t')) ++p_; }
    static int Hex(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }
    bool Codepoint(unsigned& cp) {
        if (p_ + 4 > s_.size()) return false;
        cp = 0; for (int i = 0; i != 4; ++i) { int v = Hex(s_[p_++]); if (v < 0) return false; cp = cp * 16 + static_cast<unsigned>(v); }
        return true;
    }
    static void Utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xc0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3f)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xe0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)); out += static_cast<char>(0x80 | (cp & 0x3f)); }
        else { out += static_cast<char>(0xf0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)); out += static_cast<char>(0x80 | (cp & 0x3f)); }
    }
    bool String(std::string& out) {
        if (p_ >= s_.size() || s_[p_++] != '"') return false;
        while (p_ < s_.size()) {
            unsigned char c = static_cast<unsigned char>(s_[p_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (p_ == s_.size()) return false;
            char e = s_[p_++];
            switch (e) {
            case '"': case '\\': case '/': out += e; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
            case 'u': {
                unsigned cp; if (!Codepoint(cp)) return false;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (p_ + 2 > s_.size() || s_[p_++] != '\\' || s_[p_++] != 'u') return false;
                    unsigned lo; if (!Codepoint(lo) || lo < 0xdc00 || lo > 0xdfff) return false;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + lo - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
                Utf8(out, cp); break;
            }
            default: return false;
            }
        }
        return false;
    }
public:
    explicit LockJsonReader(const std::string& s) : s_(s) {}
    bool Read(std::map<std::string, JsonValue>& fields) {
        Space(); if (p_ == s_.size() || s_[p_++] != '{') return false;
        Space(); if (p_ < s_.size() && s_[p_] == '}') { ++p_; Space(); return p_ == s_.size(); }
        for (;;) {
            std::string key; if (!String(key)) return false; Space();
            if (p_ == s_.size() || s_[p_++] != ':') return false; Space();
            JsonValue value;
            if (p_ < s_.size() && s_[p_] == '"') { value.quoted = true; if (!String(value.text)) return false; }
            else {
                size_t begin = p_;
                while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
                if (begin == p_ || (p_ - begin > 1 && s_[begin] == '0')) return false;
                value.text = s_.substr(begin, p_ - begin);
            }
            if (!fields.emplace(key, std::move(value)).second) return false;
            Space(); if (p_ == s_.size()) return false;
            char c = s_[p_++]; if (c == '}') { Space(); return p_ == s_.size(); }
            if (c != ',') return false; Space();
        }
    }
};
inline bool Number(const std::map<std::string, JsonValue>& f, const char* key, std::uint64_t& out, bool quoted = false) {
    auto it = f.find(key); if (it == f.end() || it->second.quoted != quoted || it->second.text.empty()) return false;
    for (char c : it->second.text) if (c < '0' || c > '9') return false;
    errno = 0; char* end = nullptr; auto n = _strtoui64(it->second.text.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end) return false; out = n; return true;
}
inline bool Text(const std::map<std::string, JsonValue>& f, const char* key, std::string& out) {
    auto it = f.find(key); if (it == f.end() || !it->second.quoted) return false; out = it->second.text; return true;
}
} // namespace detail

struct LockStatus {
    bool exists = false, format_ok = false, stale = false, expired = false, owner_dead = false;
    std::string owner, token, error;
    DWORD pid = 0;
    std::uint64_t pid_created_100ns = 0, created_unix_ms = 0, expires_unix_ms = 0;
    FileIdentity identity;
};

inline LockStatus InspectLockHandle(HANDLE h) {
    LockStatus status; status.exists = true; status.identity = IdentityOf(h);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > 8192) { status.error = "lock is empty, too large, or unreadable"; return status; }
    std::string body(static_cast<size_t>(size.QuadPart), '\0'); DWORD n = 0;
    if (!ReadFile(h, &body[0], static_cast<DWORD>(body.size()), &n, nullptr) || n != body.size()) { status.error = "cannot read complete lock"; return status; }
    std::map<std::string, detail::JsonValue> f;
    if (!detail::LockJsonReader(body).Read(f)) { status.error = "legacy or malformed lock (never reclaimed)"; return status; }
    std::string creator; std::uint64_t schema = 0, pid = 0;
    bool valid = detail::Text(f, "creator", creator) && creator == "vinput" &&
        detail::Number(f, "schema", schema) && schema == 1 && detail::Text(f, "owner", status.owner) &&
        !status.owner.empty() && status.owner.size() <= 256 && detail::Text(f, "token", status.token) && !status.token.empty() &&
        detail::Number(f, "pid", pid) && pid > 0 && pid <= MAXDWORD &&
        detail::Number(f, "pid_created_100ns", status.pid_created_100ns, true) && status.pid_created_100ns > 0 &&
        detail::Number(f, "created_unix_ms", status.created_unix_ms) &&
        detail::Number(f, "expires_unix_ms", status.expires_unix_ms) && status.expires_unix_ms > status.created_unix_ms;
    if (!valid) { status.error = "legacy or malformed lock (never reclaimed)"; return status; }
    status.format_ok = true; status.pid = static_cast<DWORD>(pid);
    status.expired = NowUnixMs() >= status.expires_unix_ms;
    status.owner_dead = ProcessIdentityDead(status.pid, status.pid_created_100ns);
    status.stale = status.expired || status.owner_dead;
    return status;
}

inline LockStatus InspectLock(const std::wstring& path) {
    LockStatus status;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError(); status.exists = e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND;
        if (status.exists) status.error = "lock cannot be inspected (win32=" + std::to_string(e) + ")";
        return status;
    }
    status = InspectLockHandle(h); CloseHandle(h); return status;
}

// Only a vinput-created JSON lock with the exact owner may be released. Optional token and file
// identity checks protect a long-running lease against a replacement made under the same name.
inline bool ReleaseOwnedLock(const std::wstring& path, const std::string& owner, std::string& error,
    const std::string& expected_token = {}, const FileIdentity& expected_identity = {}) {
    error.clear();
    HANDLE h = INVALID_HANDLE_VALUE; DWORD open_error = 0;
    const ULONGLONG deadline = GetTickCount64() + 250;
    for (;;) {
        h = CreateFileW(path.c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) break;
        open_error = GetLastError();
        if ((open_error != ERROR_SHARING_VIOLATION && open_error != ERROR_LOCK_VIOLATION) || GetTickCount64() >= deadline) break;
        Sleep(10);
    }
    if (h == INVALID_HANDLE_VALUE) { error = "cannot exclusively open owned lock (win32=" + std::to_string(open_error) + ")"; return false; }
    LockStatus status = InspectLockHandle(h);
    bool allowed = status.format_ok && status.owner == owner &&
        (expected_token.empty() || status.token == expected_token) &&
        (!expected_identity.valid || SameIdentity(status.identity, expected_identity));
    if (!allowed) { CloseHandle(h); error = "lock owner, token, identity, or format does not match; left untouched"; return false; }
    FILE_DISPOSITION_INFO disposition{}; disposition.DeleteFile = TRUE;
    bool ok = SetFileInformationByHandle(h, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
    DWORD e = ok ? 0 : GetLastError(); CloseHandle(h);
    if (!ok) error = "cannot release owned lock (win32=" + std::to_string(e) + ")";
    return ok;
}

class JsonLock {
    std::wstring path_;
    std::string owner_, token_;
    FileIdentity identity_;
    bool held_ = false;
public:
    JsonLock() = default;
    JsonLock(const JsonLock&) = delete;
    JsonLock& operator=(const JsonLock&) = delete;
    ~JsonLock() { if (held_) { std::string ignored; Release(ignored); } }
    bool Held() const { return held_; }
    const std::string& Token() const { return token_; }
    bool Acquire(const std::wstring& path, const std::string& owner, unsigned ttl_sec, std::string& error, DWORD owner_pid = GetCurrentProcessId()) {
        error.clear();
        if (held_) { error = "this lease already holds a lock"; return false; }
        if (path.empty() || owner.empty() || owner.size() > 256 || owner.find('\0') != std::string::npos || ttl_sec < 1 || ttl_sec > 86400) { error = "lock needs a path, owner (1..256 bytes), and ttl (1..86400 seconds)"; return false; }
        int valid_utf8 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, owner.c_str(), static_cast<int>(owner.size()), nullptr, 0);
        if (!valid_utf8) { error = "owner must be valid UTF-8"; return false; }
        std::uint64_t creation = ProcessCreationTime(owner_pid);
        if (!creation || ProcessIdentityDead(owner_pid, creation)) { error = "owner PID must identify a live, queryable process"; return false; }
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            if (e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS) {
                LockStatus old = InspectLock(path);
                error = old.format_ok ? "lock held by " + old.owner + (old.stale ? " (stale; not reclaimed)" : "") : "existing legacy/unreadable lock; left untouched";
            } else error = "cannot atomically create lock (win32=" + std::to_string(e) + ")";
            return false;
        }
        LARGE_INTEGER qpc{}; QueryPerformanceCounter(&qpc);
        std::uint64_t now = NowUnixMs();
        token_ = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(creation) + "-" + std::to_string(qpc.QuadPart);
        std::string body = "{\"creator\":\"vinput\",\"schema\":1,\"owner\":\"" + JsonEscape(owner) + "\",\"pid\":" + std::to_string(owner_pid) +
            ",\"pid_created_100ns\":\"" + std::to_string(creation) + "\",\"created_unix_ms\":" + std::to_string(now) +
            ",\"expires_unix_ms\":" + std::to_string(now + static_cast<std::uint64_t>(ttl_sec) * 1000) + ",\"ttl_sec\":" + std::to_string(ttl_sec) + ",\"token\":\"" + token_ + "\"}\n";
        DWORD n = 0;
        bool ok = WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &n, nullptr) && n == body.size() && FlushFileBuffers(h);
        identity_ = IdentityOf(h); ok = ok && identity_.valid;
        if (!ok) { FILE_DISPOSITION_INFO disposition{}; disposition.DeleteFile = TRUE; SetFileInformationByHandle(h, FileDispositionInfo, &disposition, sizeof(disposition)); CloseHandle(h); error = "could not write complete JSON lock"; return false; }
        CloseHandle(h); path_ = path; owner_ = owner; held_ = true; return true;
    }
    bool Release(std::string& error) {
        if (!held_) return true;
        bool ok = ReleaseOwnedLock(path_, owner_, error, token_, identity_);
        if (ok) held_ = false;
        return ok;
    }
    // Persistent CLI lock acquire: later release requires the same owner. Caller should select a
    // durable owner_pid (the calling agent/shell), since the short-lived CLI PID exits immediately.
    void Detach() { held_ = false; }
};

} // namespace vi_session
