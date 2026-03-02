#pragma once
#include "types.h"

// ============================================================================
// Window enumeration and screenshot capture via Win32 GDI
// ============================================================================

namespace Capture {

// Enumerate visible windows, returning (HWND, title) pairs
std::vector<std::pair<HWND, std::string>> EnumerateWindows();

// Find Entropia Universe window specifically (currently unused)
// HWND FindEntropiaWindow();

// Capture entire window as a Bitmap (client area)
Bitmap CaptureWindow(HWND hwnd);

// Capture a region of the screen
Bitmap CaptureScreen(RECT region);

// Capture the primary monitor
Bitmap CaptureFullScreen();

// Get window client rect in screen coordinates (currently unused)
// RECT GetWindowClientScreen(HWND hwnd);

// Compute a fast hash of a bitmap region for change detection (currently unused)
// RegionHash HashRegion(const Bitmap& bmp, int x, int y, int w, int h);

}  // namespace Capture
