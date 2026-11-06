// Keyboard identity tables for the macOS port.
//
// Toolscreen stores and compares keys as Windows virtual-key codes (and, for rebinds, PS/2 set-1
// scan codes), so configs are portable between Windows and macOS. This table relates each physical
// key to its VK code, its set-1 scan code (0xE0xx for extended keys), its GLFW key and its macOS
// virtual key code (kVK_*).
#pragma once

#include <cstdint>

namespace mackeys {

struct KeyEntry {
    uint16_t vk;        // generic VK as reported in WM_KEYDOWN (VK_SHIFT, not VK_LSHIFT)
    uint16_t sidedVk;   // VK_LSHIFT/VK_RSHIFT/... for modifiers, otherwise == vk
    uint16_t scan;      // set-1 make code; 0xE000 bit marks an extended (E0-prefixed) key
    int16_t glfwKey;    // GLFW_KEY_*
    int16_t macKeyCode; // kVK_*, or -1 when the key does not exist on Mac keyboards
    const char* name;   // GetKeyNameText-style name
};

constexpr uint16_t kExtendedFlag = 0xE000;

const KeyEntry* FindByGlfwKey(int glfwKey);
const KeyEntry* FindByMacKeyCode(int macKeyCode);
// Exact (scan + extended) lookup; falls back to the non-extended code when `extended` has no match.
const KeyEntry* FindByScan(uint16_t scanWithExtendedFlag);
// Accepts generic and sided VK codes. For generic modifiers the left-hand key is returned.
const KeyEntry* FindByVk(uint16_t vk);
const KeyEntry* FindBySidedVk(uint16_t sidedVk);

// Iteration over the whole table.
int Count();
const KeyEntry& At(int index);

// GLFW constants used by the bridge.
enum : int {
    GLFW_RELEASE_ = 0,
    GLFW_PRESS_ = 1,
    GLFW_REPEAT_ = 2,
    GLFW_MOD_SHIFT_ = 0x0001,
    GLFW_MOD_CONTROL_ = 0x0002,
    GLFW_MOD_ALT_ = 0x0004,
    GLFW_MOD_SUPER_ = 0x0008,
    GLFW_MOD_CAPS_LOCK_ = 0x0010,
    GLFW_MOD_NUM_LOCK_ = 0x0020,
    GLFW_KEY_UNKNOWN_ = -1,
    GLFW_KEY_LEFT_SHIFT_ = 340,
    GLFW_KEY_LEFT_CONTROL_ = 341,
    GLFW_KEY_LEFT_ALT_ = 342,
    GLFW_KEY_LEFT_SUPER_ = 343,
    GLFW_KEY_RIGHT_SHIFT_ = 344,
    GLFW_KEY_RIGHT_CONTROL_ = 345,
    GLFW_KEY_RIGHT_ALT_ = 346,
    GLFW_KEY_RIGHT_SUPER_ = 347,
    GLFW_KEY_LAST_ = 348,
    GLFW_MOUSE_BUTTON_LAST_ = 7,
};

} // namespace mackeys
