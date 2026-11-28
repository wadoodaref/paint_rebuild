// Modal Paint dialogs: Stretch/Skew, Flip/Rotate and Attributes.
#pragma once

#include <windows.h>

struct StretchSkewOptions {
    int horizontalPercent = 100;
    int verticalPercent = 100;
    int skewHorizontal = 0;
    int skewVertical = 0;
};

enum class FlipRotateChoice { FlipHorizontal, FlipVertical, Rotate90, Rotate180, Rotate270 };

struct FlipRotateOptions {
    FlipRotateChoice choice = FlipRotateChoice::Rotate90;
};

// REA: PaintUI.RotateCustomDialogViewModel exposes RotationAngle/Orientation.
struct RotateCustomOptions {
    double angleDegrees = 0.0;   // degrees, signed; + = clockwise
    bool flipHorizontal = false;
    bool flipVertical = false;
};

bool ShowStretchSkewDialog(HWND owner, StretchSkewOptions& options);
bool ShowFlipRotateDialog(HWND owner, FlipRotateOptions& options);
bool ShowAttributesDialog(HWND owner, int& width, int& height);
bool ShowRotateCustomDialog(HWND owner, RotateCustomOptions& options);
