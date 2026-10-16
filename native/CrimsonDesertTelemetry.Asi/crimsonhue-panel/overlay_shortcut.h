#pragma once
#include <string>
#include <vector>

namespace crimson::overlay {
struct Shortcut {
    int key = 'H';
    bool control = true, shift = true, alt = false;
    bool operator==(const Shortcut&) const = default;
};
inline bool AllowedShortcutKey(int key) noexcept {
    return (key >= 0x30 && key <= 0x39) || (key >= 0x41 && key <= 0x5a) ||
        (key >= 0x60 && key <= 0x87) || key == 0x08 || key == 0x09 || key == 0x0d ||
        key == 0x13 || key == 0x1b || (key >= 0x20 && key <= 0x28) || key == 0x2d ||
        key == 0x2e || key == 0x91 || (key >= 0xba && key <= 0xc0) || (key >= 0xdb && key <= 0xdf);
}
// Track the main key even outside game focus, and require exact modifiers.
// A held key, modifier change, refocus or shortcut rebind must not toggle again.
class ShortcutGate {
    Shortcut binding;
    bool down = false;
public:
    bool Update(Shortcut next, bool keyDown, bool control, bool shift, bool alt, bool foreground) noexcept {
        if (binding != next) { binding = next; down = keyDown; return false; }
        const bool pressed = keyDown && !down;
        down = keyDown;
        return pressed && foreground && control == binding.control && shift == binding.shift && alt == binding.alt;
    }
};
Shortcut CurrentShortcut() noexcept;
void UpdateShortcut(const Shortcut& value) noexcept;
std::string ShortcutLabel(const Shortcut& value);
const std::vector<std::pair<int, std::string>>& ShortcutKeys();
}
