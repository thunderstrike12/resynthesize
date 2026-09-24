#pragma once
#include "raylib.h"

namespace resynth {
    bool HeldButton(Rectangle bounds, const char* text) {
        bool hover = CheckCollisionPointRec(GetMousePosition(), bounds);
        bool held = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

        DrawRectangleRec(bounds, held ? SKYBLUE : (hover ? LIGHTGRAY : RAYWHITE));
        DrawRectangleLinesEx(bounds, 1, GRAY);
        GuiLabel(Rectangle{ bounds.x + 8, bounds.y, bounds.width, bounds.height }, text);

        return held;
    }
}