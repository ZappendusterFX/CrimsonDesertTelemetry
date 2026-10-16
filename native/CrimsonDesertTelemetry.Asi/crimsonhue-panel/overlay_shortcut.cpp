#include "overlay_shortcut.h"
#include <windows.h>
#include <atomic>

namespace crimson::overlay {
namespace {
std::atomic<unsigned> current{'H' | 0x100 | 0x200};
std::string KeyLabel(int key) {
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) return std::string(1, static_cast<char>(key));
    if (key >= VK_F1 && key <= VK_F24) return "F" + std::to_string(key - VK_F1 + 1);
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) return "Numpad " + std::to_string(key - VK_NUMPAD0);
    switch (key) {
    case VK_BACK: return "Backspace"; case VK_TAB: return "Tab"; case VK_RETURN: return "Enter";
    case VK_PAUSE: return "Pause"; case VK_ESCAPE: return "Escape"; case VK_SPACE: return "Space";
    case VK_PRIOR: return "Page Up"; case VK_NEXT: return "Page Down"; case VK_END: return "End";
    case VK_HOME: return "Home"; case VK_LEFT: return "Left"; case VK_UP: return "Up";
    case VK_RIGHT: return "Right"; case VK_DOWN: return "Down"; case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete"; case VK_SCROLL: return "Scroll Lock";
    case VK_MULTIPLY: return "Numpad *"; case VK_ADD: return "Numpad +"; case VK_SEPARATOR: return "Numpad separator";
    case VK_SUBTRACT: return "Numpad -"; case VK_DECIMAL: return "Numpad decimal"; case VK_DIVIDE: return "Numpad /";
    }
    // Translate OEM punctuation using the owner's current Windows keyboard layout.
    wchar_t name[64]{};
    const auto scan = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
    if (scan && GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64)) {
        char utf8[256]{};
        if (WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, 256, nullptr, nullptr)) return utf8;
    }
    return "Key " + std::to_string(key);
}
}
Shortcut CurrentShortcut() noexcept {
    const auto value = current.load();
    return {static_cast<int>(value & 255), (value & 0x100) != 0, (value & 0x200) != 0, (value & 0x400) != 0};
}
void UpdateShortcut(const Shortcut& value) noexcept {
    if (AllowedShortcutKey(value.key)) current = static_cast<unsigned>(value.key) |
        (value.control ? 0x100u : 0u) | (value.shift ? 0x200u : 0u) | (value.alt ? 0x400u : 0u);
}
std::string ShortcutLabel(const Shortcut& value) {
    return std::string(value.control ? "Ctrl + " : "") + (value.shift ? "Shift + " : "") +
        (value.alt ? "Alt + " : "") + KeyLabel(value.key);
}
const std::vector<std::pair<int, std::string>>& ShortcutKeys() {
    static const auto keys = [] {
        std::vector<std::pair<int, std::string>> values;
        for (int key = 1; key < 256; ++key) if (AllowedShortcutKey(key)) values.emplace_back(key, KeyLabel(key));
        return values;
    }();
    return keys;
}
}
