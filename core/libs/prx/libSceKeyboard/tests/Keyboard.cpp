#include "prx/libSceKeyboard/include/KeyboardState.hpp"

#include <cstdint>
#include <cstdio>

extern "C" {
int APS5_VABI sceKeyboardInit(void);
int APS5_VABI sceKeyboardOpen(int user_id, std::int32_t type, std::int32_t index, const void* param);
int APS5_VABI sceKeyboardClose(std::int32_t handle);
int APS5_VABI sceKeyboardGetKey2Char(std::int32_t handle, std::int32_t arrange, std::uint32_t led, std::uint32_t modifier_key, std::uint16_t key_code, KeyboardCharData* char_data);
}

namespace {

constexpr std::uint32_t HID_MOD_LEFT_CTRL = 1u;
constexpr std::uint32_t HID_MOD_LEFT_ALT = 4u;

int failures = 0;

void expectEqual(long long actual, long long expected, const char* expression, int line) {
    if (actual != expected) {
        std::printf("    FAIL line %d: %s = %lld (0x%llx), expected %lld (0x%llx)\n",
                    line, expression,
                    actual, static_cast<unsigned long long>(static_cast<std::uint64_t>(actual)),
                    expected, static_cast<unsigned long long>(static_cast<std::uint64_t>(expected)));
        ++failures;
    }
}

#define CHECK_EQ(actual, expected) expectEqual((actual), (expected), #actual, __LINE__)

struct Translation {
    int result = 0;
    KeyboardCharData data{};
};

Translation Translate(std::int32_t handle, std::int32_t arrange, std::uint32_t ledState, std::uint32_t modifierKey, std::uint16_t keyCode) {
    Translation translation;
    translation.result = sceKeyboardGetKey2Char(handle, arrange, ledState, modifierKey, keyCode, &translation.data);
    return translation;
}

void TestValidationPaths() {
    std::printf("[Test] Running TestValidationPaths...\n");
    KeyboardCharData data{};
    CHECK_EQ(sceKeyboardGetKey2Char(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x04, &data), KEYBOARD_ERROR_NOT_INITIALIZED);
    CHECK_EQ(sceKeyboardInit(), KEYBOARD_OK);
    CHECK_EQ(sceKeyboardGetKey2Char(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x04, &data), KEYBOARD_ERROR_INVALID_HANDLE);
    CHECK_EQ(sceKeyboardOpen(-1, 0, 0, nullptr), KEYBOARD_ERROR_INVALID_ARG);
    CHECK_EQ(sceKeyboardOpen(0, 1, 0, nullptr), KEYBOARD_ERROR_INVALID_ARG);
    CHECK_EQ(sceKeyboardOpen(0, 0, 1, nullptr), KEYBOARD_ERROR_INVALID_ARG);
    CHECK_EQ(sceKeyboardOpen(0, 0, 0, nullptr), KEYBOARD_HANDLE);
    CHECK_EQ(sceKeyboardOpen(0, 0, 0, nullptr), KEYBOARD_ERROR_ALREADY_OPENED);
    CHECK_EQ(sceKeyboardGetKey2Char(2, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x04, &data), KEYBOARD_ERROR_INVALID_HANDLE);
    CHECK_EQ(sceKeyboardGetKey2Char(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x04, nullptr), KEYBOARD_ERROR_INVALID_ARG);
    CHECK_EQ(sceKeyboardGetKey2Char(KEYBOARD_HANDLE, 2, 0, 0, 0x04, &data), KEYBOARD_ERROR_INVALID_ARG);
    CHECK_EQ(sceKeyboardGetKey2Char(KEYBOARD_HANDLE, -1, 0, 0, 0x04, &data), KEYBOARD_ERROR_INVALID_ARG);
    CHECK_EQ(sceKeyboardClose(KEYBOARD_HANDLE), KEYBOARD_OK);
    CHECK_EQ(sceKeyboardClose(KEYBOARD_HANDLE), KEYBOARD_ERROR_INVALID_HANDLE);
    CHECK_EQ(sceKeyboardOpen(0, 0, 0, nullptr), KEYBOARD_HANDLE);
    std::printf("  -> PASSED!\n");
}

void TestUsArrangementCharacters() {
    std::printf("[Test] Running TestUsArrangementCharacters...\n");
    const auto a = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x04);
    CHECK_EQ(a.result, KEYBOARD_OK);
    CHECK_EQ(a.data.char_code, 'a');
    const auto shiftedA = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x04);
    CHECK_EQ(shiftedA.data.char_code, 'A');
    const auto capsA = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, KEYBOARD_LED_CAPS_LOCK, 0, 0x04);
    CHECK_EQ(capsA.data.char_code, 'A');
    const auto capsShiftA = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, KEYBOARD_LED_CAPS_LOCK, KEYBOARD_MOD_RIGHT_SHIFT, 0x04);
    CHECK_EQ(capsShiftA.data.char_code, 'a');
    const auto one = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x1e);
    CHECK_EQ(one.data.char_code, '1');
    const auto shiftedOne = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x1e);
    CHECK_EQ(shiftedOne.data.char_code, '!');
    const auto usMinus = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x2d);
    CHECK_EQ(usMinus.data.char_code, '-');
    const auto usShiftMinus = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x2d);
    CHECK_EQ(usShiftMinus.data.char_code, '_');
    const auto usEqual = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x2e);
    CHECK_EQ(usEqual.data.char_code, '=');
    const auto usShiftEqual = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x2e);
    CHECK_EQ(usShiftEqual.data.char_code, '+');
    const auto usGrave = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x35);
    CHECK_EQ(usGrave.data.char_code, '`');
    const auto usShiftGrave = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x35);
    CHECK_EQ(usShiftGrave.data.char_code, '~');
    const auto usSemicolon = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x33);
    CHECK_EQ(usSemicolon.data.char_code, ';');
    const auto usShiftSemicolon = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x33);
    CHECK_EQ(usShiftSemicolon.data.char_code, ':');
    const auto usQuote = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x34);
    CHECK_EQ(usQuote.data.char_code, '\'');
    const auto usShiftQuote = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x34);
    CHECK_EQ(usShiftQuote.data.char_code, '"');
    const auto usBackslash = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x31);
    CHECK_EQ(usBackslash.data.char_code, '\\');
    const auto usShiftBackslash = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x31);
    CHECK_EQ(usShiftBackslash.data.char_code, '|');
    const auto keypadSlash = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x54);
    CHECK_EQ(keypadSlash.data.char_code, '/');
    const auto numLockDigit = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, KEYBOARD_LED_NUM_LOCK, 0, 0x59);
    CHECK_EQ(numLockDigit.data.char_code, '1');
    const auto numLockOffDigit = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x59);
    CHECK_EQ(numLockOffDigit.data.char_code, 0);
    std::printf("  -> PASSED!\n");
}

void TestJpArrangementCharacters() {
    std::printf("[Test] Running TestJpArrangementCharacters...\n");
    const auto jpA = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x04);
    CHECK_EQ(jpA.result, KEYBOARD_OK);
    CHECK_EQ(jpA.data.char_code, 'a');
    const auto jpShiftA = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x04);
    CHECK_EQ(jpShiftA.data.char_code, 'A');
    const auto jpZero = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x27);
    CHECK_EQ(jpZero.data.char_code, '0');
    const auto jpShiftZero = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x27);
    CHECK_EQ(jpShiftZero.data.char_code, '~');
    const auto jpShiftTwo = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x1f);
    CHECK_EQ(jpShiftTwo.data.char_code, '"');
    const auto jpShiftSix = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x23);
    CHECK_EQ(jpShiftSix.data.char_code, '&');
    const auto jpShiftSeven = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x24);
    CHECK_EQ(jpShiftSeven.data.char_code, '\'');
    const auto jpShiftNine = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x26);
    CHECK_EQ(jpShiftNine.data.char_code, ')');
    const auto jpHo = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x2d);
    CHECK_EQ(jpHo.data.char_code, '-');
    const auto jpShiftHo = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x2d);
    CHECK_EQ(jpShiftHo.data.char_code, '=');
    const auto jpHe = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x2e);
    CHECK_EQ(jpHe.data.char_code, '^');
    const auto jpShiftHe = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x2e);
    CHECK_EQ(jpShiftHe.data.char_code, '~');
    const auto jpAt = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x2f);
    CHECK_EQ(jpAt.data.char_code, '@');
    const auto jpShiftAt = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x2f);
    CHECK_EQ(jpShiftAt.data.char_code, '`');
    const auto jpLeftBracket = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x30);
    CHECK_EQ(jpLeftBracket.data.char_code, '[');
    const auto jpShiftLeftBracket = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x30);
    CHECK_EQ(jpShiftLeftBracket.data.char_code, '{');
    const auto jpRightBracket = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x31);
    CHECK_EQ(jpRightBracket.data.char_code, ']');
    const auto jpShiftRightBracket = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x31);
    CHECK_EQ(jpShiftRightBracket.data.char_code, '}');
    const auto jpSemicolon = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x33);
    CHECK_EQ(jpSemicolon.data.char_code, ';');
    const auto jpShiftSemicolon = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x33);
    CHECK_EQ(jpShiftSemicolon.data.char_code, '+');
    const auto jpColon = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x34);
    CHECK_EQ(jpColon.data.char_code, ':');
    const auto jpShiftColon = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x34);
    CHECK_EQ(jpShiftColon.data.char_code, '*');
    const auto jpHankaku = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x35);
    CHECK_EQ(jpHankaku.result, KEYBOARD_OK);
    CHECK_EQ(jpHankaku.data.char_code, 0);
    CHECK_EQ(jpHankaku.data.processed, 0);
    CHECK_EQ(jpHankaku.data.length, 0);
    const auto jpShiftHankaku = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x35);
    CHECK_EQ(jpShiftHankaku.data.char_code, 0);
    CHECK_EQ(jpShiftHankaku.data.processed, 0);
    const auto jpRo = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x64);
    CHECK_EQ(jpRo.data.char_code, '\\');
    const auto jpShiftRo = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, KEYBOARD_MOD_LEFT_SHIFT, 0x64);
    CHECK_EQ(jpShiftRo.data.char_code, '_');
    const auto jpIsoKey = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x32);
    CHECK_EQ(jpIsoKey.data.char_code, 0);
    const auto jpKeyPadDigit = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, KEYBOARD_LED_NUM_LOCK, 0, 0x5a);
    CHECK_EQ(jpKeyPadDigit.data.char_code, '2');
    std::printf("  -> PASSED!\n");
}

void TestModifiersDoNotAlterCharacters() {
    std::printf("[Test] Running TestModifiersDoNotAlterCharacters...\n");
    const auto ctrlA = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, HID_MOD_LEFT_CTRL, 0x04);
    CHECK_EQ(ctrlA.data.char_code, 'a');
    const auto altA = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, HID_MOD_LEFT_ALT, 0x04);
    CHECK_EQ(altA.data.char_code, 'a');
    const auto jpCtrlOne = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, HID_MOD_LEFT_CTRL, 0x1e);
    CHECK_EQ(jpCtrlOne.data.char_code, '1');
    std::printf("  -> PASSED!\n");
}

void TestProcessedAndLengthFields() {
    std::printf("[Test] Running TestProcessedAndLengthFields...\n");
    const auto mapped = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_106, 0, 0, 0x2f);
    CHECK_EQ(mapped.result, KEYBOARD_OK);
    CHECK_EQ(mapped.data.processed, 1);
    CHECK_EQ(mapped.data.length, 1);
    const auto unmapped = Translate(KEYBOARD_HANDLE, KEYBOARD_ARRANGEMENT_101, 0, 0, 0x29);
    CHECK_EQ(unmapped.result, KEYBOARD_OK);
    CHECK_EQ(unmapped.data.char_code, 0);
    CHECK_EQ(unmapped.data.processed, 0);
    CHECK_EQ(unmapped.data.length, 0);
    std::printf("  -> PASSED!\n");
}

}

int main() {
    std::printf("== libSceKeyboard tests ==\n");
    TestValidationPaths();
    TestUsArrangementCharacters();
    TestJpArrangementCharacters();
    TestModifiersDoNotAlterCharacters();
    TestProcessedAndLengthFields();
    if (failures != 0) {
        std::printf("== %d FAILED assertion(s) ==\n", failures);
        return 1;
    }
    std::printf("== ALL PASSED ==\n");
    return 0;
}
