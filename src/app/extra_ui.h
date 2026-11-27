// Remaining source-observed UI commands: wallpaper, full screen, thumbnail.
#pragma once

#include <windows.h>

#include "../core/image.h"

bool SetAsWallpaper(pr::Image* image, bool tiled);
void ToggleFullScreen(HWND hwnd);
bool IsFullScreen();
void ToggleThumbnail(HWND owner, pr::Image* image);
void UpdateThumbnailImage(pr::Image* image);
bool IsThumbnailVisible();
