#include "platform/mac/mac_keymap.h"

namespace mackeys {
namespace {

constexpr uint16_t E = kExtendedFlag;

// Order matters for reverse lookups: the first entry for a VK/scan/GLFW key wins.
const KeyEntry kKeys[] = {
    // vk               sidedVk           scan      glfw  mac    name
    { VK_ESCAPE,        VK_ESCAPE,        0x01,     256,  0x35, "Esc" },
    { '1',              '1',              0x02,     49,   0x12, "1" },
    { '2',              '2',              0x03,     50,   0x13, "2" },
    { '3',              '3',              0x04,     51,   0x14, "3" },
    { '4',              '4',              0x05,     52,   0x15, "4" },
    { '5',              '5',              0x06,     53,   0x17, "5" },
    { '6',              '6',              0x07,     54,   0x16, "6" },
    { '7',              '7',              0x08,     55,   0x1A, "7" },
    { '8',              '8',              0x09,     56,   0x1C, "8" },
    { '9',              '9',              0x0A,     57,   0x19, "9" },
    { '0',              '0',              0x0B,     48,   0x1D, "0" },
    { VK_OEM_MINUS,     VK_OEM_MINUS,     0x0C,     45,   0x1B, "-" },
    { VK_OEM_PLUS,      VK_OEM_PLUS,      0x0D,     61,   0x18, "=" },
    { VK_BACK,          VK_BACK,          0x0E,     259,  0x33, "Backspace" },
    { VK_TAB,           VK_TAB,           0x0F,     258,  0x30, "Tab" },
    { 'Q',              'Q',              0x10,     81,   0x0C, "Q" },
    { 'W',              'W',              0x11,     87,   0x0D, "W" },
    { 'E',              'E',              0x12,     69,   0x0E, "E" },
    { 'R',              'R',              0x13,     82,   0x0F, "R" },
    { 'T',              'T',              0x14,     84,   0x11, "T" },
    { 'Y',              'Y',              0x15,     89,   0x10, "Y" },
    { 'U',              'U',              0x16,     85,   0x20, "U" },
    { 'I',              'I',              0x17,     73,   0x22, "I" },
    { 'O',              'O',              0x18,     79,   0x1F, "O" },
    { 'P',              'P',              0x19,     80,   0x23, "P" },
    { VK_OEM_4,         VK_OEM_4,         0x1A,     91,   0x21, "[" },
    { VK_OEM_6,         VK_OEM_6,         0x1B,     93,   0x1E, "]" },
    { VK_RETURN,        VK_RETURN,        0x1C,     257,  0x24, "Enter" },
    { VK_CONTROL,       VK_LCONTROL,      0x1D,     341,  0x3B, "Ctrl" },
    { 'A',              'A',              0x1E,     65,   0x00, "A" },
    { 'S',              'S',              0x1F,     83,   0x01, "S" },
    { 'D',              'D',              0x20,     68,   0x02, "D" },
    { 'F',              'F',              0x21,     70,   0x03, "F" },
    { 'G',              'G',              0x22,     71,   0x05, "G" },
    { 'H',              'H',              0x23,     72,   0x04, "H" },
    { 'J',              'J',              0x24,     74,   0x26, "J" },
    { 'K',              'K',              0x25,     75,   0x28, "K" },
    { 'L',              'L',              0x26,     76,   0x25, "L" },
    { VK_OEM_1,         VK_OEM_1,         0x27,     59,   0x29, ";" },
    { VK_OEM_7,         VK_OEM_7,         0x28,     39,   0x27, "'" },
    { VK_OEM_3,         VK_OEM_3,         0x29,     96,   0x32, "`" },
    { VK_SHIFT,         VK_LSHIFT,        0x2A,     340,  0x38, "Shift" },
    { VK_OEM_5,         VK_OEM_5,         0x2B,     92,   0x2A, "\\" },
    { 'Z',              'Z',              0x2C,     90,   0x06, "Z" },
    { 'X',              'X',              0x2D,     88,   0x07, "X" },
    { 'C',              'C',              0x2E,     67,   0x08, "C" },
    { 'V',              'V',              0x2F,     86,   0x09, "V" },
    { 'B',              'B',              0x30,     66,   0x0B, "B" },
    { 'N',              'N',              0x31,     78,   0x2D, "N" },
    { 'M',              'M',              0x32,     77,   0x2E, "M" },
    { VK_OEM_COMMA,     VK_OEM_COMMA,     0x33,     44,   0x2B, "," },
    { VK_OEM_PERIOD,    VK_OEM_PERIOD,    0x34,     46,   0x2F, "." },
    { VK_OEM_2,         VK_OEM_2,         0x35,     47,   0x2C, "/" },
    { VK_SHIFT,         VK_RSHIFT,        0x36,     344,  0x3C, "Right Shift" },
    { VK_MULTIPLY,      VK_MULTIPLY,      0x37,     332,  0x43, "Num *" },
    { VK_MENU,          VK_LMENU,         0x38,     342,  0x3A, "Alt" },
    { VK_SPACE,         VK_SPACE,         0x39,     32,   0x31, "Space" },
    { VK_CAPITAL,       VK_CAPITAL,       0x3A,     280,  0x39, "Caps Lock" },
    { VK_F1,            VK_F1,            0x3B,     290,  0x7A, "F1" },
    { VK_F2,            VK_F2,            0x3C,     291,  0x78, "F2" },
    { VK_F3,            VK_F3,            0x3D,     292,  0x63, "F3" },
    { VK_F4,            VK_F4,            0x3E,     293,  0x76, "F4" },
    { VK_F5,            VK_F5,            0x3F,     294,  0x60, "F5" },
    { VK_F6,            VK_F6,            0x40,     295,  0x61, "F6" },
    { VK_F7,            VK_F7,            0x41,     296,  0x62, "F7" },
    { VK_F8,            VK_F8,            0x42,     297,  0x64, "F8" },
    { VK_F9,            VK_F9,            0x43,     298,  0x65, "F9" },
    { VK_F10,           VK_F10,           0x44,     299,  0x6D, "F10" },
    { VK_NUMLOCK,       VK_NUMLOCK,       E | 0x45, 282,  0x47, "Num Lock" }, // Mac keypad "Clear"
    { VK_SCROLL,        VK_SCROLL,        0x46,     281,  -1,   "Scroll Lock" },
    { VK_NUMPAD7,       VK_NUMPAD7,       0x47,     327,  0x59, "Num 7" },
    { VK_NUMPAD8,       VK_NUMPAD8,       0x48,     328,  0x5B, "Num 8" },
    { VK_NUMPAD9,       VK_NUMPAD9,       0x49,     329,  0x5C, "Num 9" },
    { VK_SUBTRACT,      VK_SUBTRACT,      0x4A,     333,  0x4E, "Num -" },
    { VK_NUMPAD4,       VK_NUMPAD4,       0x4B,     324,  0x56, "Num 4" },
    { VK_NUMPAD5,       VK_NUMPAD5,       0x4C,     325,  0x57, "Num 5" },
    { VK_NUMPAD6,       VK_NUMPAD6,       0x4D,     326,  0x58, "Num 6" },
    { VK_ADD,           VK_ADD,           0x4E,     334,  0x45, "Num +" },
    { VK_NUMPAD1,       VK_NUMPAD1,       0x4F,     321,  0x53, "Num 1" },
    { VK_NUMPAD2,       VK_NUMPAD2,       0x50,     322,  0x54, "Num 2" },
    { VK_NUMPAD3,       VK_NUMPAD3,       0x51,     323,  0x55, "Num 3" },
    { VK_NUMPAD0,       VK_NUMPAD0,       0x52,     320,  0x52, "Num 0" },
    { VK_DECIMAL,       VK_DECIMAL,       0x53,     330,  0x41, "Num Del" },
    { VK_OEM_102,       VK_OEM_102,       0x56,     161,  0x0A, "\\" },          // ISO section key / WORLD_1
    { VK_F11,           VK_F11,           0x57,     300,  0x67, "F11" },
    { VK_F12,           VK_F12,           0x58,     301,  0x6F, "F12" },
    { VK_OEM_PLUS,      VK_OEM_PLUS,      0x59,     336,  0x51, "Num =" },       // keypad '=' (GLFW_KEY_KP_EQUAL)
    { VK_F13,           VK_F13,           0x64,     302,  0x69, "F13" },
    { VK_F14,           VK_F14,           0x65,     303,  0x6B, "F14" },
    { VK_F15,           VK_F15,           0x66,     304,  0x71, "F15" },
    { VK_F16,           VK_F16,           0x67,     305,  0x6A, "F16" },
    { VK_F17,           VK_F17,           0x68,     306,  0x40, "F17" },
    { VK_F18,           VK_F18,           0x69,     307,  0x4F, "F18" },
    { VK_F19,           VK_F19,           0x6A,     308,  0x50, "F19" },
    { VK_F20,           VK_F20,           0x6B,     309,  0x5A, "F20" },
    { VK_F21,           VK_F21,           0x6C,     310,  -1,   "F21" },
    { VK_F22,           VK_F22,           0x6D,     311,  -1,   "F22" },
    { VK_F23,           VK_F23,           0x6E,     312,  -1,   "F23" },
    { VK_F24,           VK_F24,           0x76,     313,  -1,   "F24" },
    { VK_RETURN,        VK_RETURN,        E | 0x1C, 335,  0x4C, "Num Enter" },
    { VK_CONTROL,       VK_RCONTROL,      E | 0x1D, 345,  0x3E, "Right Ctrl" },
    { VK_DIVIDE,        VK_DIVIDE,        E | 0x35, 331,  0x4B, "Num /" },
    { VK_SNAPSHOT,      VK_SNAPSHOT,      E | 0x37, 283,  -1,   "Prnt Scrn" },
    { VK_MENU,          VK_RMENU,         E | 0x38, 346,  0x3D, "Right Alt" },
    { VK_PAUSE,         VK_PAUSE,         0x45,     284,  -1,   "Pause" },
    { VK_HOME,          VK_HOME,          E | 0x47, 268,  0x73, "Home" },
    { VK_UP,            VK_UP,            E | 0x48, 265,  0x7E, "Up" },
    { VK_PRIOR,         VK_PRIOR,         E | 0x49, 266,  0x74, "Page Up" },
    { VK_LEFT,          VK_LEFT,          E | 0x4B, 263,  0x7B, "Left" },
    { VK_RIGHT,         VK_RIGHT,         E | 0x4D, 262,  0x7C, "Right" },
    { VK_END,           VK_END,           E | 0x4F, 269,  0x77, "End" },
    { VK_DOWN,          VK_DOWN,          E | 0x50, 264,  0x7D, "Down" },
    { VK_NEXT,          VK_NEXT,          E | 0x51, 267,  0x79, "Page Down" },
    { VK_INSERT,        VK_INSERT,        E | 0x52, 260,  0x72, "Insert" },        // Mac "Help"
    { VK_DELETE,        VK_DELETE,        E | 0x53, 261,  0x75, "Delete" },
    { VK_LWIN,          VK_LWIN,          E | 0x5B, 343,  0x37, "Left Command" },
    { VK_RWIN,          VK_RWIN,          E | 0x5C, 347,  0x36, "Right Command" },
    { VK_APPS,          VK_APPS,          E | 0x5D, 348,  0x6E, "Application" },
    { VK_VOLUME_MUTE,   VK_VOLUME_MUTE,   E | 0x20, -1,   0x4A, "Mute" },
    { VK_VOLUME_DOWN,   VK_VOLUME_DOWN,   E | 0x2E, -1,   0x49, "Volume Down" },
    { VK_VOLUME_UP,     VK_VOLUME_UP,     E | 0x30, -1,   0x48, "Volume Up" },
};

constexpr int kKeyCount = static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0]));

} // namespace

const KeyEntry* FindByGlfwKey(int glfwKey) {
    if (glfwKey < 0) return nullptr;
    for (const KeyEntry& k : kKeys)
        if (k.glfwKey == glfwKey) return &k;
    return nullptr;
}

const KeyEntry* FindByMacKeyCode(int macKeyCode) {
    if (macKeyCode < 0) return nullptr;
    for (const KeyEntry& k : kKeys)
        if (k.macKeyCode == macKeyCode) return &k;
    return nullptr;
}

const KeyEntry* FindByScan(uint16_t scan) {
    for (const KeyEntry& k : kKeys)
        if (k.scan == scan) return &k;
    const uint16_t base = scan & 0xFF;
    for (const KeyEntry& k : kKeys)
        if ((k.scan & 0xFF) == base) return &k;
    return nullptr;
}

const KeyEntry* FindBySidedVk(uint16_t sidedVk) {
    for (const KeyEntry& k : kKeys)
        if (k.sidedVk == sidedVk) return &k;
    return nullptr;
}

const KeyEntry* FindByVk(uint16_t vk) {
    switch (vk) {
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_LMENU:
    case VK_RMENU:
        return FindBySidedVk(vk);
    default:
        break;
    }
    for (const KeyEntry& k : kKeys)
        if (k.vk == vk) return &k;
    return nullptr;
}

int Count() { return kKeyCount; }
const KeyEntry& At(int index) { return kKeys[index]; }

} // namespace mackeys
