// Pure-memory test of the exact production h_GetKeyState/GetKeyboardState bodies.
// extract_keystatetoggle_subject.py supplies the chosen before/after source.
// No Windows header, desktop API, physical input or native key-state call occurs.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#define WINAPI
using BYTE = std::uint8_t;
using SHORT = std::int16_t;
using USHORT = std::uint16_t;
using BOOL = int;
using PBYTE = BYTE*;
constexpr BOOL TRUE = 1, FALSE = 0;
constexpr int VK_CAPITAL = 0x14, VK_NUMLOCK = 0x90, VK_SCROLL = 0x91;
constexpr unsigned ERROR_NOACCESS = 998;
enum ReaderKind { ReaderKey, ReaderKeyboard };
static bool gMask = true, gWritable = true, gStore = true, gNativeSuccess = true;
static SHORT gNativeSingle = 0;
static BYTE gNativeBulk[256]{};
static unsigned gLastError = 0, gSingleCalls = 0, gBulkCalls = 0;
static std::atomic<std::uint8_t> g_vk[256]{};
struct Counters { unsigned gks = 0, gkbs = 0; } C;
struct SingleTrace {
    USHORT value = 0; bool down = false; unsigned calls = 0;
    void record(USHORT v, bool d) { value = v; down = d; ++calls; }
};
struct BulkTrace {
    BYTE value = 0; unsigned calls = 0;
    void record(BYTE v) { value = v; ++calls; }
};
struct KeyTrace { SingleTrace gks; BulkTrace gkbs; } g_keyTrace[256]{};
static bool ReaderMask(ReaderKind) { return gMask; }
static void SetLastError(unsigned error) { gLastError = error; }
static bool ReaderBufferWritable(PBYTE, std::size_t) { return gWritable; }
static bool ReaderBufferStore(PBYTE out, const BYTE* in, std::size_t bytes) {
    if (!gStore) return false;
    std::memcpy(out, in, bytes); return true;
}
static SHORT NativeSingle(int) { ++gSingleCalls; return gNativeSingle; }
static BOOL NativeBulk(PBYTE out) {
    ++gBulkCalls;
    if (!out || !gNativeSuccess) { SetLastError(87); return FALSE; }
    std::memcpy(out, gNativeBulk, sizeof(gNativeBulk)); return TRUE;
}
static SHORT (*o_GetKeyState)(int) = &NativeSingle;
static BOOL (*o_GetKeyboardState)(PBYTE) = &NativeBulk;

#include <vi_keystatetoggle_subject.inl>

static unsigned gPassed = 0, gFailed = 0;
static const char* gFirstFailures[8]{};
static void Check(bool good, const char* name) {
    if (good) { ++gPassed; return; }
    if (gFailed < 8) gFirstFailures[gFailed] = name;
    ++gFailed;
}
static void Reset() {
    for (int key = 0; key < 256; ++key) { g_vk[key] = 0; g_keyTrace[key] = {}; }
    C = {}; gWritable = gStore = gNativeSuccess = true;
    gLastError = gSingleCalls = gBulkCalls = 0;
}
static bool Toggle(int key) {
    return key == VK_CAPITAL || key == VK_NUMLOCK || key == VK_SCROLL;
}
static void SingleCases() {
    constexpr int keys[] = {VK_CAPITAL, VK_NUMLOCK, VK_SCROLL, 'A', 'W', 0x75, 1, 2, 0, 255, 256, -1};
    constexpr USHORT native[] = {0, 1, 0x8000, 0x8001, 0x00ff, 0xffff};
    for (bool mask : {false, true}) for (int key : keys) for (USHORT value : native) for (bool held : {false, true}) {
        Reset(); gMask = mask; gNativeSingle = static_cast<SHORT>(value);
        const bool valid = key > 0 && key < 256;
        if (valid) g_vk[key] = held ? 1 : 0;
        USHORT expected = !mask ? value : Toggle(key) ? static_cast<USHORT>(value & 1u) : 0;
        if (valid && held) expected = static_cast<USHORT>(expected | 0xff80u);
        const auto actual = static_cast<USHORT>(h_GetKeyState(key));
        Check(actual == expected, mask ? "masked_single_exact_toggle_and_virtual_merge" : "native_single_passthrough_and_virtual_merge");
        Check(C.gks == 1, "single_counter_unchanged");
        Check(gSingleCalls == ((!mask || Toggle(key)) ? 1u : 0u), "only_masked_three_toggles_query_native");
        if (valid) Check(g_keyTrace[key].gks.calls == 1 && g_keyTrace[key].gks.value == actual && g_keyTrace[key].gks.down == ((actual & 0x8000u) != 0), "single_trace_reports_returned_value");
    }
}
static void BulkCases() {
    constexpr BYTE native[] = {0, 1, 0x80, 0x81, 0xff};
    for (bool mask : {false, true}) for (BYTE value : native) for (bool held : {false, true}) {
        Reset(); gMask = mask; std::memset(gNativeBulk, value, sizeof(gNativeBulk));
        if (held) { g_vk['W'] = 1; g_vk[VK_CAPITAL] = 1; g_vk[VK_NUMLOCK] = 1; g_vk[VK_SCROLL] = 1; }
        BYTE output[258]; std::memset(output, 0xa5, sizeof(output));
        const BOOL actual = h_GetKeyboardState(output + 1);
        Check(actual == TRUE && output[0] == 0xa5 && output[257] == 0xa5, "bulk_success_and_exact_256_byte_write");
        for (int key = 0; key < 256; ++key) {
            BYTE expected = !mask ? value : Toggle(key) ? static_cast<BYTE>(value & 1u) : 0;
            if (g_vk[key]) expected = static_cast<BYTE>(expected | 0x80u);
            Check(output[key + 1] == expected, mask ? "masked_bulk_exact_toggle_and_virtual_merge" : "native_bulk_passthrough_and_virtual_merge");
            Check(g_keyTrace[key].gkbs.calls == 1 && g_keyTrace[key].gkbs.value == output[key + 1], "bulk_trace_reports_returned_value");
        }
    }
}
static void BulkValidationCases() {
    for (bool mask : {false, true}) {
        Reset(); gMask = mask;
        Check(h_GetKeyboardState(nullptr) == FALSE && gBulkCalls == 1 && gLastError == 87, "null_output_delegates_native_error");
        Reset(); gMask = mask; gWritable = false;
        BYTE output[256]; std::memset(output, 0xa5, sizeof(output));
        Check(h_GetKeyboardState(output) == FALSE && gBulkCalls == 0 && gLastError == ERROR_NOACCESS && output[0] == 0xa5 && output[255] == 0xa5, "invalid_output_refused_before_native_or_write");
        Reset(); gMask = mask; gStore = false;
        Check(h_GetKeyboardState(output) == FALSE && gLastError == ERROR_NOACCESS && output[0] == 0xa5 && output[255] == 0xa5, "store_failure_preserves_caller_output");
        Reset(); gMask = mask; gNativeSuccess = false; g_vk['W'] = 1;
        std::memset(output, 0xa5, sizeof(output));
        Check(h_GetKeyboardState(output) == TRUE && output['W'] == 0x80 && output[VK_CAPITAL] == 0 && output[VK_NUMLOCK] == 0 && output[VK_SCROLL] == 0 && output['A'] == 0, "native_failure_retains_existing_virtual_only_success_fallback");
    }
}
int main() {
    SingleCases(); BulkCases(); BulkValidationCases();
    std::printf("{\"schema\":1,\"passed\":%u,\"failed\":%u,\"native_api_execution\":false,\"core_sha256\":\"%s\",\"body_sha256\":\"%s\",\"first_failures\":[", gPassed, gFailed, VI_KEY_STATE_CORE_SHA, VI_KEY_STATE_BODY_SHA);
    const unsigned shown = gFailed < 8 ? gFailed : 8;
    for (unsigned i = 0; i < shown; ++i) std::printf("%s\"%s\"", i ? "," : "", gFirstFailures[i]);
    std::printf("]}\n"); return gFailed ? 1 : 0;
}
