#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "app/app.hpp"
#include "app/app_font.hpp"
namespace resynth { Font g_app_font; }

int main() {
	SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED | FLAG_MSAA_4X_HINT);
	InitWindow(1600, 900, "Resynth");

	InitAudioDevice();
	SetTargetFPS(60);

	resynth::g_app_font = LoadFontEx("../../../assets/Inter-VariableFont_opsz,wght.ttf", 32, nullptr, 0);
	if (resynth::g_app_font.texture.id == 0) {
		TraceLog(LOG_ERROR, "Failed to load font!");
	}
	SetTextureFilter(resynth::g_app_font.texture, TEXTURE_FILTER_BILINEAR);
	GuiSetFont(resynth::g_app_font);
	GuiSetStyle(DEFAULT, TEXT_SIZE, 14);
	//GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, ColorToInt(DARKGRAY));

	resynth::App app;
	while (!WindowShouldClose()) app.Update();

	CloseAudioDevice();
	CloseWindow();
	return 0;
}