#pragma once
#include <Arduino.h>

namespace VolumeControl {

    void init();

    void increase(); // one step up, clamped at max, applies immediately
    void decrease(); // one step down, clamped at 0 (mute), applies immediately

    uint8_t currentStep(); // 0-10, for display purposes

    void setKeyBeepEnabled(bool enabled);
    bool isKeyBeepEnabled();

    // Plays a short UI-feedback tone if key-beep is enabled, no-ops
    // otherwise. Centralizes the isKeyBeepEnabled() check in one place so
    // callers (Settings menu navigation, radar screen aircraft selection)
    // don't each need to guard every tone() call themselves.
    void keyBeep(uint16_t hz, uint16_t ms);

} // namespace VolumeControl
