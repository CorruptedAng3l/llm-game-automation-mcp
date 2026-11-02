#include "vi_sessions.h"
#include <atomic>
#include <thread>
#include <vector>
#include <fstream>
#include <iterator>

static unsigned failures = 0, checks = 0;
static void Check(bool pass, const char* name) {
    ++checks; if (!pass) ++failures;
    printf("%s %s\n", pass ? "PASS" : "FAIL", name);
}
static bool WriteText(const std::wstring& path, const std::string& text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0; bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &n, nullptr) && n == text.size(); CloseHandle(h); return ok;
}
static std::string ReadText(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {});
}
int wmain(int argc, wchar_t** argv) {
    using namespace vi_session;
    if (argc != 2) { fprintf(stderr, "need a scratch lock path\n"); return 2; }
    const std::wstring path = argv[1];
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) { fprintf(stderr, "scratch lock must not exist yet\n"); return 2; }
    std::string error;
    Check(!InspectLock(path).exists, "missing lock is absent");
    {
        JsonLock a, b;
        Check(a.Acquire(path, "agent \"A\" \\ \xE2\x98\x83", 120, error), "atomic create with escaped UTF-8 owner");
        auto s = InspectLock(path);
        Check(s.exists && s.format_ok && !s.stale && s.owner == "agent \"A\" \\ \xE2\x98\x83" && s.pid == GetCurrentProcessId(), "JSON round trip and live owner positive control");
        const auto before = ReadText(path);
        Check(!b.Acquire(path, "agent B", 120, error), "second owner refused");
        Check(!ReleaseOwnedLock(path, "agent B", error), "foreign release refused");
        Check(!ReleaseOwnedLock(path, s.owner, error, "wrong-token"), "wrong token refused");
        FileIdentity wrong_id = s.identity; wrong_id.low ^= 1;
        Check(!ReleaseOwnedLock(path, s.owner, error, a.Token(), wrong_id), "wrong file identity refused independently of token");
        Check(ReadText(path) == before, "foreign acquisition/release leaves exact bytes unchanged");
        Check(a.Release(error) && !InspectLock(path).exists, "token and identity checked release");
    }
    {
        JsonLock a;
        Check(!a.Acquire(path, "", 120, error) && !a.Acquire(path, "a", 0, error) && !a.Acquire(path, "a", 86401, error), "invalid owner and TTL rejected");
        Check(!a.Acquire(path, std::string("bad\0owner", 9), 120, error) && !a.Acquire(path, "\xFF", 120, error), "NUL and invalid UTF-8 owner rejected");
        Check(!InspectLock(path).exists, "invalid acquire creates no file");
        Check(a.Acquire(path, "raii", 120, error), "RAII lease created");
    }
    Check(!InspectLock(path).exists, "RAII destructor releases its lease");
    {
        JsonLock a;
        Check(a.Acquire(path, "durable", 120, error), "persistent lease created"); a.Detach();
    }
    Check(InspectLock(path).exists && ReleaseOwnedLock(path, "durable", error), "detach persists until matching owner releases");
    {
        JsonLock a;
        Check(a.Acquire(path, "ttl", 1, error), "short lease created");
        Sleep(1100);
        auto s = InspectLock(path);
        Check(s.format_ok && s.expired && s.stale && !s.owner_dead, "TTL expires independently of live process");
        JsonLock b; const auto before = ReadText(path);
        Check(!b.Acquire(path, "other", 120, error) && ReadText(path) == before, "expired foreign lock remains untouched");
        Check(a.Release(error), "original owner releases expired lock");
    }
    {
        const std::string legacy = "Someone busy PID=123; DO NOT DELETE\n";
        Check(WriteText(path, legacy), "legacy test fixture created");
        auto s = InspectLock(path); JsonLock a;
        Check(s.exists && !s.format_ok && !s.stale, "legacy is unknown, never inferred stale");
        Check(!a.Acquire(path, "new", 120, error) && !ReleaseOwnedLock(path, "new", error) && ReadText(path) == legacy, "legacy lock acquisition/release refused and bytes retained");
        Check(DeleteFileW(path.c_str()) != FALSE, "remove our own legacy fixture");
    }
    {
        JsonLock a, replacement;
        Check(a.Acquire(path, "same-owner", 120, error), "replacement test original acquired");
        const auto s = InspectLock(path);
        Check(ReleaseOwnedLock(path, s.owner, error, a.Token()), "remove our own original fixture");
        Check(replacement.Acquire(path, "same-owner", 120, error), "same owner replacement acquired");
        const auto before = ReadText(path);
        Check(!a.Release(error) && ReadText(path) == before, "old RAII lease cannot delete replacement");
        Check(replacement.Release(error), "new lease releases replacement");
    }
    {
        JsonLock a;
        Check(a.Acquire(path, "pid-reuse", 120, error), "PID identity fixture acquired");
        auto s = InspectLock(path); auto body = ReadText(path); a.Detach();
        const std::string good = "\"pid_created_100ns\":\"" + std::to_string(s.pid_created_100ns) + "\"";
        const auto p = body.find(good);
        Check(p != std::string::npos, "identity field fixture found");
        body.replace(p, good.size(), "\"pid_created_100ns\":\"1\"");
        Check(WriteText(path, body), "write mismatched PID creation identity");
        s = InspectLock(path);
        Check(s.format_ok && s.owner_dead && s.stale && !s.expired, "PID reuse mismatch marks stale");
        Check(ReleaseOwnedLock(path, "pid-reuse", error), "owner releases our mismatched fixture");
    }
    {
        JsonLock a;
        Check(a.Acquire(path, "malformed", 120, error), "malformed fixture acquired");
        auto body = ReadText(path); a.Detach();
        body.insert(1, "\"owner\":\"duplicate\",");
        Check(WriteText(path, body), "write duplicate JSON key fixture");
        auto s = InspectLock(path);
        Check(s.exists && !s.format_ok && !ReleaseOwnedLock(path, "malformed", error) && ReadText(path) == body, "duplicate keys rejected without deleting");
        Check(DeleteFileW(path.c_str()) != FALSE, "remove our own malformed fixture");
    }
    {
        JsonLock a;
        Check(a.Acquire(path, "reader-race", 120, error), "reader race fixture acquired");
        std::atomic<bool> reader_ready{false}; std::atomic<bool> reader_opened{false};
        std::thread reader([&] {
            const auto first_status = InspectLock(path);
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            const auto held_status = h != INVALID_HANDLE_VALUE ? InspectLockHandle(h) : LockStatus{};
            reader_opened = h != INVALID_HANDLE_VALUE && first_status.format_ok && held_status.format_ok; reader_ready = true;
            Sleep(70); if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        });
        while (!reader_ready.load()) Sleep(0);
        Check(reader_opened.load(), "concurrent status reader opened");
        const auto start = GetTickCount64(); bool released = a.Release(error); const auto elapsed = GetTickCount64() - start;
        reader.join();
        Check(released && elapsed >= 50 && elapsed < 1000, "release retries past transient status reader");
        Check(!InspectLock(path).exists, "reader race leaves no owned lease behind");
    }
    {
        JsonLock a;
        Check(a.Acquire(path, "busy-reader", 120, error), "bounded reader fixture acquired");
        HANDLE reader = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(reader != INVALID_HANDLE_VALUE, "long-lived reader opened");
        const auto start = GetTickCount64(); bool released = a.Release(error); const auto elapsed = GetTickCount64() - start;
        Check(!released && elapsed >= 200 && elapsed < 1000 && InspectLock(path).exists, "release retry is bounded and retains lease on failure");
        if (reader != INVALID_HANDLE_VALUE) CloseHandle(reader);
        Check(a.Release(error), "later release succeeds after busy reader exits");
    }
    {
        std::atomic<unsigned> ready{0}, attempted{0}, acquired{0}; std::atomic<bool> go{false}, release{false};
        std::vector<std::thread> threads;
        for (unsigned n = 0; n != 8; ++n) threads.emplace_back([&, n] {
            JsonLock a; std::string e; ready.fetch_add(1);
            while (!go.load()) Sleep(0);
            bool held = a.Acquire(path, "parallel-" + std::to_string(n), 120, e);
            if (held) acquired.fetch_add(1);
            attempted.fetch_add(1);
            if (held) while (!release.load()) Sleep(0);
        });
        while (ready.load() != 8) Sleep(0); go = true;
        while (attempted.load() != 8) Sleep(0);
        Check(acquired.load() == 1, "8 concurrent acquirers have exactly one winner");
        release = true; for (auto& t : threads) t.join();
        Check(!InspectLock(path).exists, "concurrency test winner released only its own lock");
    }
    printf("%s: %u checks, %u failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
