#include "../src/vi_keys.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <set>
#include <chrono>
static int checks = 0, failures = 0;
static void Check(bool ok, const char* text) {
  ++checks;
  if (!ok) { ++failures; std::printf("FAIL %s\n", text); }
}
static void ExpectKey(const char* text, int expected) {
  int vk = 1234;
  bool ok = vi_keys::ParseKey(text, vk) && vk == expected && vi_keys::VkFromName(text) == expected;
  Check(ok, text);
}
static void ExpectChord(const char* text, const std::vector<int>& expected) {
  std::vector<int> actual;
  std::string error = "old error";
  Check(vi_keys::ParseChord(text, actual, &error) && actual == expected && error.empty(), text);
  Check(vi_keys::ParseCombo(text) == expected, text);
}
int main() {
  ExpectKey("VK_LBUTTON", VK_LBUTTON); ExpectKey("lbutton", VK_LBUTTON);
  ExpectKey("VK_RBUTTON", VK_RBUTTON); ExpectKey("rbutton", VK_RBUTTON);
  ExpectKey("VK_CANCEL", VK_CANCEL); ExpectKey("cancel", VK_CANCEL);
  ExpectKey("VK_MBUTTON", VK_MBUTTON); ExpectKey("mbutton", VK_MBUTTON);
  ExpectKey("VK_XBUTTON1", VK_XBUTTON1); ExpectKey("xbutton1", VK_XBUTTON1);
  ExpectKey("VK_XBUTTON2", VK_XBUTTON2); ExpectKey("xbutton2", VK_XBUTTON2);
  ExpectKey("VK_BACK", VK_BACK); ExpectKey("back", VK_BACK);
  ExpectKey("VK_TAB", VK_TAB); ExpectKey("tab", VK_TAB);
  ExpectKey("VK_CLEAR", VK_CLEAR); ExpectKey("clear", VK_CLEAR);
  ExpectKey("VK_RETURN", VK_RETURN); ExpectKey("return", VK_RETURN);
  ExpectKey("VK_SHIFT", VK_SHIFT); ExpectKey("shift", VK_SHIFT);
  ExpectKey("VK_CONTROL", VK_CONTROL); ExpectKey("control", VK_CONTROL);
  ExpectKey("VK_MENU", VK_MENU); ExpectKey("menu", VK_MENU);
  ExpectKey("VK_PAUSE", VK_PAUSE); ExpectKey("pause", VK_PAUSE);
  ExpectKey("VK_CAPITAL", VK_CAPITAL); ExpectKey("capital", VK_CAPITAL);
  ExpectKey("VK_KANA", VK_KANA); ExpectKey("kana", VK_KANA);
  ExpectKey("VK_HANGEUL", VK_HANGEUL); ExpectKey("hangeul", VK_HANGEUL);
  ExpectKey("VK_HANGUL", VK_HANGUL); ExpectKey("hangul", VK_HANGUL);
  ExpectKey("VK_IME_ON", VK_IME_ON); ExpectKey("ime_on", VK_IME_ON);
  ExpectKey("VK_JUNJA", VK_JUNJA); ExpectKey("junja", VK_JUNJA);
  ExpectKey("VK_FINAL", VK_FINAL); ExpectKey("final", VK_FINAL);
  ExpectKey("VK_HANJA", VK_HANJA); ExpectKey("hanja", VK_HANJA);
  ExpectKey("VK_KANJI", VK_KANJI); ExpectKey("kanji", VK_KANJI);
  ExpectKey("VK_IME_OFF", VK_IME_OFF); ExpectKey("ime_off", VK_IME_OFF);
  ExpectKey("VK_ESCAPE", VK_ESCAPE); ExpectKey("escape", VK_ESCAPE);
  ExpectKey("VK_CONVERT", VK_CONVERT); ExpectKey("convert", VK_CONVERT);
  ExpectKey("VK_NONCONVERT", VK_NONCONVERT); ExpectKey("nonconvert", VK_NONCONVERT);
  ExpectKey("VK_ACCEPT", VK_ACCEPT); ExpectKey("accept", VK_ACCEPT);
  ExpectKey("VK_MODECHANGE", VK_MODECHANGE); ExpectKey("modechange", VK_MODECHANGE);
  ExpectKey("VK_SPACE", VK_SPACE); ExpectKey("space", VK_SPACE);
  ExpectKey("VK_PRIOR", VK_PRIOR); ExpectKey("prior", VK_PRIOR);
  ExpectKey("VK_NEXT", VK_NEXT); ExpectKey("next", VK_NEXT);
  ExpectKey("VK_END", VK_END); ExpectKey("end", VK_END);
  ExpectKey("VK_HOME", VK_HOME); ExpectKey("home", VK_HOME);
  ExpectKey("VK_LEFT", VK_LEFT); ExpectKey("left", VK_LEFT);
  ExpectKey("VK_UP", VK_UP); ExpectKey("up", VK_UP);
  ExpectKey("VK_RIGHT", VK_RIGHT); ExpectKey("right", VK_RIGHT);
  ExpectKey("VK_DOWN", VK_DOWN); ExpectKey("down", VK_DOWN);
  ExpectKey("VK_SELECT", VK_SELECT); ExpectKey("select", VK_SELECT);
  ExpectKey("VK_PRINT", VK_PRINT); ExpectKey("print", VK_PRINT);
  ExpectKey("VK_EXECUTE", VK_EXECUTE); ExpectKey("execute", VK_EXECUTE);
  ExpectKey("VK_SNAPSHOT", VK_SNAPSHOT); ExpectKey("snapshot", VK_SNAPSHOT);
  ExpectKey("VK_INSERT", VK_INSERT); ExpectKey("insert", VK_INSERT);
  ExpectKey("VK_DELETE", VK_DELETE); ExpectKey("delete", VK_DELETE);
  ExpectKey("VK_HELP", VK_HELP); ExpectKey("help", VK_HELP);
  ExpectKey("VK_LWIN", VK_LWIN); ExpectKey("lwin", VK_LWIN);
  ExpectKey("VK_RWIN", VK_RWIN); ExpectKey("rwin", VK_RWIN);
  ExpectKey("VK_APPS", VK_APPS); ExpectKey("apps", VK_APPS);
  ExpectKey("VK_SLEEP", VK_SLEEP); ExpectKey("sleep", VK_SLEEP);
  ExpectKey("VK_NUMPAD0", VK_NUMPAD0); ExpectKey("numpad0", VK_NUMPAD0);
  ExpectKey("VK_NUMPAD1", VK_NUMPAD1); ExpectKey("numpad1", VK_NUMPAD1);
  ExpectKey("VK_NUMPAD2", VK_NUMPAD2); ExpectKey("numpad2", VK_NUMPAD2);
  ExpectKey("VK_NUMPAD3", VK_NUMPAD3); ExpectKey("numpad3", VK_NUMPAD3);
  ExpectKey("VK_NUMPAD4", VK_NUMPAD4); ExpectKey("numpad4", VK_NUMPAD4);
  ExpectKey("VK_NUMPAD5", VK_NUMPAD5); ExpectKey("numpad5", VK_NUMPAD5);
  ExpectKey("VK_NUMPAD6", VK_NUMPAD6); ExpectKey("numpad6", VK_NUMPAD6);
  ExpectKey("VK_NUMPAD7", VK_NUMPAD7); ExpectKey("numpad7", VK_NUMPAD7);
  ExpectKey("VK_NUMPAD8", VK_NUMPAD8); ExpectKey("numpad8", VK_NUMPAD8);
  ExpectKey("VK_NUMPAD9", VK_NUMPAD9); ExpectKey("numpad9", VK_NUMPAD9);
  ExpectKey("VK_MULTIPLY", VK_MULTIPLY); ExpectKey("multiply", VK_MULTIPLY);
  ExpectKey("VK_ADD", VK_ADD); ExpectKey("add", VK_ADD);
  ExpectKey("VK_SEPARATOR", VK_SEPARATOR); ExpectKey("separator", VK_SEPARATOR);
  ExpectKey("VK_SUBTRACT", VK_SUBTRACT); ExpectKey("subtract", VK_SUBTRACT);
  ExpectKey("VK_DECIMAL", VK_DECIMAL); ExpectKey("decimal", VK_DECIMAL);
  ExpectKey("VK_DIVIDE", VK_DIVIDE); ExpectKey("divide", VK_DIVIDE);
  ExpectKey("VK_F1", VK_F1); ExpectKey("f1", VK_F1);
  ExpectKey("VK_F2", VK_F2); ExpectKey("f2", VK_F2);
  ExpectKey("VK_F3", VK_F3); ExpectKey("f3", VK_F3);
  ExpectKey("VK_F4", VK_F4); ExpectKey("f4", VK_F4);
  ExpectKey("VK_F5", VK_F5); ExpectKey("f5", VK_F5);
  ExpectKey("VK_F6", VK_F6); ExpectKey("f6", VK_F6);
  ExpectKey("VK_F7", VK_F7); ExpectKey("f7", VK_F7);
  ExpectKey("VK_F8", VK_F8); ExpectKey("f8", VK_F8);
  ExpectKey("VK_F9", VK_F9); ExpectKey("f9", VK_F9);
  ExpectKey("VK_F10", VK_F10); ExpectKey("f10", VK_F10);
  ExpectKey("VK_F11", VK_F11); ExpectKey("f11", VK_F11);
  ExpectKey("VK_F12", VK_F12); ExpectKey("f12", VK_F12);
  ExpectKey("VK_F13", VK_F13); ExpectKey("f13", VK_F13);
  ExpectKey("VK_F14", VK_F14); ExpectKey("f14", VK_F14);
  ExpectKey("VK_F15", VK_F15); ExpectKey("f15", VK_F15);
  ExpectKey("VK_F16", VK_F16); ExpectKey("f16", VK_F16);
  ExpectKey("VK_F17", VK_F17); ExpectKey("f17", VK_F17);
  ExpectKey("VK_F18", VK_F18); ExpectKey("f18", VK_F18);
  ExpectKey("VK_F19", VK_F19); ExpectKey("f19", VK_F19);
  ExpectKey("VK_F20", VK_F20); ExpectKey("f20", VK_F20);
  ExpectKey("VK_F21", VK_F21); ExpectKey("f21", VK_F21);
  ExpectKey("VK_F22", VK_F22); ExpectKey("f22", VK_F22);
  ExpectKey("VK_F23", VK_F23); ExpectKey("f23", VK_F23);
  ExpectKey("VK_F24", VK_F24); ExpectKey("f24", VK_F24);
  ExpectKey("VK_NAVIGATION_VIEW", VK_NAVIGATION_VIEW); ExpectKey("navigation_view", VK_NAVIGATION_VIEW);
  ExpectKey("VK_NAVIGATION_MENU", VK_NAVIGATION_MENU); ExpectKey("navigation_menu", VK_NAVIGATION_MENU);
  ExpectKey("VK_NAVIGATION_UP", VK_NAVIGATION_UP); ExpectKey("navigation_up", VK_NAVIGATION_UP);
  ExpectKey("VK_NAVIGATION_DOWN", VK_NAVIGATION_DOWN); ExpectKey("navigation_down", VK_NAVIGATION_DOWN);
  ExpectKey("VK_NAVIGATION_LEFT", VK_NAVIGATION_LEFT); ExpectKey("navigation_left", VK_NAVIGATION_LEFT);
  ExpectKey("VK_NAVIGATION_RIGHT", VK_NAVIGATION_RIGHT); ExpectKey("navigation_right", VK_NAVIGATION_RIGHT);
  ExpectKey("VK_NAVIGATION_ACCEPT", VK_NAVIGATION_ACCEPT); ExpectKey("navigation_accept", VK_NAVIGATION_ACCEPT);
  ExpectKey("VK_NAVIGATION_CANCEL", VK_NAVIGATION_CANCEL); ExpectKey("navigation_cancel", VK_NAVIGATION_CANCEL);
  ExpectKey("VK_NUMLOCK", VK_NUMLOCK); ExpectKey("numlock", VK_NUMLOCK);
  ExpectKey("VK_SCROLL", VK_SCROLL); ExpectKey("scroll", VK_SCROLL);
  ExpectKey("VK_OEM_NEC_EQUAL", VK_OEM_NEC_EQUAL); ExpectKey("oem_nec_equal", VK_OEM_NEC_EQUAL);
  ExpectKey("VK_OEM_FJ_JISHO", VK_OEM_FJ_JISHO); ExpectKey("oem_fj_jisho", VK_OEM_FJ_JISHO);
  ExpectKey("VK_OEM_FJ_MASSHOU", VK_OEM_FJ_MASSHOU); ExpectKey("oem_fj_masshou", VK_OEM_FJ_MASSHOU);
  ExpectKey("VK_OEM_FJ_TOUROKU", VK_OEM_FJ_TOUROKU); ExpectKey("oem_fj_touroku", VK_OEM_FJ_TOUROKU);
  ExpectKey("VK_OEM_FJ_LOYA", VK_OEM_FJ_LOYA); ExpectKey("oem_fj_loya", VK_OEM_FJ_LOYA);
  ExpectKey("VK_OEM_FJ_ROYA", VK_OEM_FJ_ROYA); ExpectKey("oem_fj_roya", VK_OEM_FJ_ROYA);
  ExpectKey("VK_LSHIFT", VK_LSHIFT); ExpectKey("lshift", VK_LSHIFT);
  ExpectKey("VK_RSHIFT", VK_RSHIFT); ExpectKey("rshift", VK_RSHIFT);
  ExpectKey("VK_LCONTROL", VK_LCONTROL); ExpectKey("lcontrol", VK_LCONTROL);
  ExpectKey("VK_RCONTROL", VK_RCONTROL); ExpectKey("rcontrol", VK_RCONTROL);
  ExpectKey("VK_LMENU", VK_LMENU); ExpectKey("lmenu", VK_LMENU);
  ExpectKey("VK_RMENU", VK_RMENU); ExpectKey("rmenu", VK_RMENU);
  ExpectKey("VK_BROWSER_BACK", VK_BROWSER_BACK); ExpectKey("browser_back", VK_BROWSER_BACK);
  ExpectKey("VK_BROWSER_FORWARD", VK_BROWSER_FORWARD); ExpectKey("browser_forward", VK_BROWSER_FORWARD);
  ExpectKey("VK_BROWSER_REFRESH", VK_BROWSER_REFRESH); ExpectKey("browser_refresh", VK_BROWSER_REFRESH);
  ExpectKey("VK_BROWSER_STOP", VK_BROWSER_STOP); ExpectKey("browser_stop", VK_BROWSER_STOP);
  ExpectKey("VK_BROWSER_SEARCH", VK_BROWSER_SEARCH); ExpectKey("browser_search", VK_BROWSER_SEARCH);
  ExpectKey("VK_BROWSER_FAVORITES", VK_BROWSER_FAVORITES); ExpectKey("browser_favorites", VK_BROWSER_FAVORITES);
  ExpectKey("VK_BROWSER_HOME", VK_BROWSER_HOME); ExpectKey("browser_home", VK_BROWSER_HOME);
  ExpectKey("VK_VOLUME_MUTE", VK_VOLUME_MUTE); ExpectKey("volume_mute", VK_VOLUME_MUTE);
  ExpectKey("VK_VOLUME_DOWN", VK_VOLUME_DOWN); ExpectKey("volume_down", VK_VOLUME_DOWN);
  ExpectKey("VK_VOLUME_UP", VK_VOLUME_UP); ExpectKey("volume_up", VK_VOLUME_UP);
  ExpectKey("VK_MEDIA_NEXT_TRACK", VK_MEDIA_NEXT_TRACK); ExpectKey("media_next_track", VK_MEDIA_NEXT_TRACK);
  ExpectKey("VK_MEDIA_PREV_TRACK", VK_MEDIA_PREV_TRACK); ExpectKey("media_prev_track", VK_MEDIA_PREV_TRACK);
  ExpectKey("VK_MEDIA_STOP", VK_MEDIA_STOP); ExpectKey("media_stop", VK_MEDIA_STOP);
  ExpectKey("VK_MEDIA_PLAY_PAUSE", VK_MEDIA_PLAY_PAUSE); ExpectKey("media_play_pause", VK_MEDIA_PLAY_PAUSE);
  ExpectKey("VK_LAUNCH_MAIL", VK_LAUNCH_MAIL); ExpectKey("launch_mail", VK_LAUNCH_MAIL);
  ExpectKey("VK_LAUNCH_MEDIA_SELECT", VK_LAUNCH_MEDIA_SELECT); ExpectKey("launch_media_select", VK_LAUNCH_MEDIA_SELECT);
  ExpectKey("VK_LAUNCH_APP1", VK_LAUNCH_APP1); ExpectKey("launch_app1", VK_LAUNCH_APP1);
  ExpectKey("VK_LAUNCH_APP2", VK_LAUNCH_APP2); ExpectKey("launch_app2", VK_LAUNCH_APP2);
  ExpectKey("VK_OEM_1", VK_OEM_1); ExpectKey("oem_1", VK_OEM_1);
  ExpectKey("VK_OEM_PLUS", VK_OEM_PLUS); ExpectKey("oem_plus", VK_OEM_PLUS);
  ExpectKey("VK_OEM_COMMA", VK_OEM_COMMA); ExpectKey("oem_comma", VK_OEM_COMMA);
  ExpectKey("VK_OEM_MINUS", VK_OEM_MINUS); ExpectKey("oem_minus", VK_OEM_MINUS);
  ExpectKey("VK_OEM_PERIOD", VK_OEM_PERIOD); ExpectKey("oem_period", VK_OEM_PERIOD);
  ExpectKey("VK_OEM_2", VK_OEM_2); ExpectKey("oem_2", VK_OEM_2);
  ExpectKey("VK_OEM_3", VK_OEM_3); ExpectKey("oem_3", VK_OEM_3);
  ExpectKey("VK_GAMEPAD_A", VK_GAMEPAD_A); ExpectKey("gamepad_a", VK_GAMEPAD_A);
  ExpectKey("VK_GAMEPAD_B", VK_GAMEPAD_B); ExpectKey("gamepad_b", VK_GAMEPAD_B);
  ExpectKey("VK_GAMEPAD_X", VK_GAMEPAD_X); ExpectKey("gamepad_x", VK_GAMEPAD_X);
  ExpectKey("VK_GAMEPAD_Y", VK_GAMEPAD_Y); ExpectKey("gamepad_y", VK_GAMEPAD_Y);
  ExpectKey("VK_GAMEPAD_RIGHT_SHOULDER", VK_GAMEPAD_RIGHT_SHOULDER); ExpectKey("gamepad_right_shoulder", VK_GAMEPAD_RIGHT_SHOULDER);
  ExpectKey("VK_GAMEPAD_LEFT_SHOULDER", VK_GAMEPAD_LEFT_SHOULDER); ExpectKey("gamepad_left_shoulder", VK_GAMEPAD_LEFT_SHOULDER);
  ExpectKey("VK_GAMEPAD_LEFT_TRIGGER", VK_GAMEPAD_LEFT_TRIGGER); ExpectKey("gamepad_left_trigger", VK_GAMEPAD_LEFT_TRIGGER);
  ExpectKey("VK_GAMEPAD_RIGHT_TRIGGER", VK_GAMEPAD_RIGHT_TRIGGER); ExpectKey("gamepad_right_trigger", VK_GAMEPAD_RIGHT_TRIGGER);
  ExpectKey("VK_GAMEPAD_DPAD_UP", VK_GAMEPAD_DPAD_UP); ExpectKey("gamepad_dpad_up", VK_GAMEPAD_DPAD_UP);
  ExpectKey("VK_GAMEPAD_DPAD_DOWN", VK_GAMEPAD_DPAD_DOWN); ExpectKey("gamepad_dpad_down", VK_GAMEPAD_DPAD_DOWN);
  ExpectKey("VK_GAMEPAD_DPAD_LEFT", VK_GAMEPAD_DPAD_LEFT); ExpectKey("gamepad_dpad_left", VK_GAMEPAD_DPAD_LEFT);
  ExpectKey("VK_GAMEPAD_DPAD_RIGHT", VK_GAMEPAD_DPAD_RIGHT); ExpectKey("gamepad_dpad_right", VK_GAMEPAD_DPAD_RIGHT);
  ExpectKey("VK_GAMEPAD_MENU", VK_GAMEPAD_MENU); ExpectKey("gamepad_menu", VK_GAMEPAD_MENU);
  ExpectKey("VK_GAMEPAD_VIEW", VK_GAMEPAD_VIEW); ExpectKey("gamepad_view", VK_GAMEPAD_VIEW);
  ExpectKey("VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON", VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON); ExpectKey("gamepad_left_thumbstick_button", VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON);
  ExpectKey("VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON", VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON); ExpectKey("gamepad_right_thumbstick_button", VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON);
  ExpectKey("VK_GAMEPAD_LEFT_THUMBSTICK_UP", VK_GAMEPAD_LEFT_THUMBSTICK_UP); ExpectKey("gamepad_left_thumbstick_up", VK_GAMEPAD_LEFT_THUMBSTICK_UP);
  ExpectKey("VK_GAMEPAD_LEFT_THUMBSTICK_DOWN", VK_GAMEPAD_LEFT_THUMBSTICK_DOWN); ExpectKey("gamepad_left_thumbstick_down", VK_GAMEPAD_LEFT_THUMBSTICK_DOWN);
  ExpectKey("VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT", VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT); ExpectKey("gamepad_left_thumbstick_right", VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT);
  ExpectKey("VK_GAMEPAD_LEFT_THUMBSTICK_LEFT", VK_GAMEPAD_LEFT_THUMBSTICK_LEFT); ExpectKey("gamepad_left_thumbstick_left", VK_GAMEPAD_LEFT_THUMBSTICK_LEFT);
  ExpectKey("VK_GAMEPAD_RIGHT_THUMBSTICK_UP", VK_GAMEPAD_RIGHT_THUMBSTICK_UP); ExpectKey("gamepad_right_thumbstick_up", VK_GAMEPAD_RIGHT_THUMBSTICK_UP);
  ExpectKey("VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN", VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN); ExpectKey("gamepad_right_thumbstick_down", VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN);
  ExpectKey("VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT", VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT); ExpectKey("gamepad_right_thumbstick_right", VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT);
  ExpectKey("VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT", VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT); ExpectKey("gamepad_right_thumbstick_left", VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT);
  ExpectKey("VK_OEM_4", VK_OEM_4); ExpectKey("oem_4", VK_OEM_4);
  ExpectKey("VK_OEM_5", VK_OEM_5); ExpectKey("oem_5", VK_OEM_5);
  ExpectKey("VK_OEM_6", VK_OEM_6); ExpectKey("oem_6", VK_OEM_6);
  ExpectKey("VK_OEM_7", VK_OEM_7); ExpectKey("oem_7", VK_OEM_7);
  ExpectKey("VK_OEM_8", VK_OEM_8); ExpectKey("oem_8", VK_OEM_8);
  ExpectKey("VK_OEM_AX", VK_OEM_AX); ExpectKey("oem_ax", VK_OEM_AX);
  ExpectKey("VK_OEM_102", VK_OEM_102); ExpectKey("oem_102", VK_OEM_102);
  ExpectKey("VK_ICO_HELP", VK_ICO_HELP); ExpectKey("ico_help", VK_ICO_HELP);
  ExpectKey("VK_ICO_00", VK_ICO_00); ExpectKey("ico_00", VK_ICO_00);
  ExpectKey("VK_PROCESSKEY", VK_PROCESSKEY); ExpectKey("processkey", VK_PROCESSKEY);
  ExpectKey("VK_ICO_CLEAR", VK_ICO_CLEAR); ExpectKey("ico_clear", VK_ICO_CLEAR);
  ExpectKey("VK_PACKET", VK_PACKET); ExpectKey("packet", VK_PACKET);
  ExpectKey("VK_OEM_RESET", VK_OEM_RESET); ExpectKey("oem_reset", VK_OEM_RESET);
  ExpectKey("VK_OEM_JUMP", VK_OEM_JUMP); ExpectKey("oem_jump", VK_OEM_JUMP);
  ExpectKey("VK_OEM_PA1", VK_OEM_PA1); ExpectKey("oem_pa1", VK_OEM_PA1);
  ExpectKey("VK_OEM_PA2", VK_OEM_PA2); ExpectKey("oem_pa2", VK_OEM_PA2);
  ExpectKey("VK_OEM_PA3", VK_OEM_PA3); ExpectKey("oem_pa3", VK_OEM_PA3);
  ExpectKey("VK_OEM_WSCTRL", VK_OEM_WSCTRL); ExpectKey("oem_wsctrl", VK_OEM_WSCTRL);
  ExpectKey("VK_OEM_CUSEL", VK_OEM_CUSEL); ExpectKey("oem_cusel", VK_OEM_CUSEL);
  ExpectKey("VK_OEM_ATTN", VK_OEM_ATTN); ExpectKey("oem_attn", VK_OEM_ATTN);
  ExpectKey("VK_OEM_FINISH", VK_OEM_FINISH); ExpectKey("oem_finish", VK_OEM_FINISH);
  ExpectKey("VK_OEM_COPY", VK_OEM_COPY); ExpectKey("oem_copy", VK_OEM_COPY);
  ExpectKey("VK_OEM_AUTO", VK_OEM_AUTO); ExpectKey("oem_auto", VK_OEM_AUTO);
  ExpectKey("VK_OEM_ENLW", VK_OEM_ENLW); ExpectKey("oem_enlw", VK_OEM_ENLW);
  ExpectKey("VK_OEM_BACKTAB", VK_OEM_BACKTAB); ExpectKey("oem_backtab", VK_OEM_BACKTAB);
  ExpectKey("VK_ATTN", VK_ATTN); ExpectKey("attn", VK_ATTN);
  ExpectKey("VK_CRSEL", VK_CRSEL); ExpectKey("crsel", VK_CRSEL);
  ExpectKey("VK_EXSEL", VK_EXSEL); ExpectKey("exsel", VK_EXSEL);
  ExpectKey("VK_EREOF", VK_EREOF); ExpectKey("ereof", VK_EREOF);
  ExpectKey("VK_PLAY", VK_PLAY); ExpectKey("play", VK_PLAY);
  ExpectKey("VK_ZOOM", VK_ZOOM); ExpectKey("zoom", VK_ZOOM);
  ExpectKey("VK_NONAME", VK_NONAME); ExpectKey("noname", VK_NONAME);
  ExpectKey("VK_PA1", VK_PA1); ExpectKey("pa1", VK_PA1);
  ExpectKey("VK_OEM_CLEAR", VK_OEM_CLEAR); ExpectKey("oem_clear", VK_OEM_CLEAR);
  ExpectKey("num+", VK_ADD); ExpectKey("oem1", VK_OEM_1); ExpectKey("oem102", VK_OEM_102);
  ExpectKey("numpad2", VK_NUMPAD2); ExpectKey("insert", VK_INSERT); ExpectKey("capslock", VK_CAPITAL);
  ExpectKey("lwin", VK_LWIN); ExpectKey("volume_up", VK_VOLUME_UP); ExpectKey("browserback", VK_BROWSER_BACK);
  ExpectKey(" CTRL ", VK_CONTROL); ExpectKey("NumPad+", VK_ADD); ExpectKey("+", VK_OEM_PLUS);
  for (int vk = 1; vk <= 255; ++vk) {
    char hex[16]; sprintf_s(hex, "0x%02X", vk);
    ExpectKey(hex, vk); ExpectKey(("vk" + std::to_string(vk)).c_str(), vk);
    Check(!vi_keys::Name(vk).empty(), "all VKs have listed names");
  }
  for (const char* invalid : {"", " ", "0x", "0x0", "0x100", "0x2Djunk", "0x-1", "vk", "vk0", "vk999", "vk45junk", "f0", "f25", "f1junk", "numpad10", "unknown", "vk99999999999999999999999999"}) {
    int vk = 987;
    Check(!vi_keys::ParseKey(invalid, vk) && vk == 0, invalid);
  }
  ExpectChord("w+space", {'W', VK_SPACE});
  ExpectChord("ctrl+shift+s", {VK_CONTROL, VK_SHIFT, 'S'});
  ExpectChord(" CTRL + SHIFT + S ", {VK_CONTROL, VK_SHIFT, 'S'});
  ExpectChord("num+", {VK_ADD}); ExpectChord("ctrl+num+", {VK_CONTROL, VK_ADD});
  ExpectChord("ctrl+num++w", {VK_CONTROL, VK_ADD, 'W'});
  ExpectChord("ctrl++", {VK_CONTROL, VK_OEM_PLUS});
  ExpectChord("ctrl+++w", {VK_CONTROL, VK_OEM_PLUS, 'W'});
  ExpectChord("+", {VK_OEM_PLUS}); ExpectChord("ctrl+plus+w", {VK_CONTROL, VK_OEM_PLUS, 'W'});
  ExpectChord("w+w+space", {'W', VK_SPACE});
  ExpectChord("0x57+vk32", {'W', VK_SPACE});
  for (const char* invalid : {"ctrl+", "++", "ctrl++space", "w+unknown", "w+vk256", "w+ +space", "+w", ""}) {
    std::vector<int> keys = {'Q'};
    std::string error;
    Check(!vi_keys::ParseChord(invalid, keys, &error) && keys.empty() && !error.empty(), invalid);
  }
  std::vector<int> excessive;
  Check(!vi_keys::ParseChord(std::string(5000, 'a'), excessive), "length bound");
  std::set<std::string> unique;
  for (const auto& entry : vi_keys::Names()) {
    Check(unique.insert(entry.name).second, "unique listed name");
    ExpectKey(entry.name.c_str(), entry.vk);
  }
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000; ++i) vi_keys::ParseCombo("ctrl+num++w+space");
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
  std::printf("checks=%d failures=%d listed_aliases=%zu parse1000_us=%lld\n", checks, failures, vi_keys::Names().size(), elapsed);
  return failures ? 1 : 0;
}


