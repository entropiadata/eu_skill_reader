#include "capture.h"

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace Capture {

struct EnumData {
    std::vector<std::pair<HWND, std::string>>* results;
    DWORD ownPid;
};

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumData*>(lParam);

    if (!IsWindowVisible(hwnd)) return TRUE;

    // Skip our own process
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == data->ownPid) return TRUE;

    char title[256];
    int len = GetWindowTextA(hwnd, title, sizeof(title));
    if (len == 0) return TRUE;

    // Skip tiny windows, but allow minimized (IsIconic) windows through
    if (!IsIconic(hwnd)) {
        RECT r;
        GetWindowRect(hwnd, &r);
        if ((r.right - r.left) < 100 || (r.bottom - r.top) < 100) return TRUE;
    }

    data->results->push_back({hwnd, std::string(title, len)});
    return TRUE;
}

std::vector<std::pair<HWND, std::string>> EnumerateWindows() {
    std::vector<std::pair<HWND, std::string>> results;
    EnumData data{&results, GetCurrentProcessId()};
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&data));
    return results;
}

// FindEntropiaWindow — currently unused
// HWND FindEntropiaWindow() {
//     std::string planetName;
//     HKEY hKey;
//     if (RegOpenKeyExA(HKEY_CURRENT_USER,
//         "Software\\MindArk\\Entropia Universe\\UserInfo",
//         0, KEY_READ, &hKey) == ERROR_SUCCESS) {
//
//         char buf[256] = {};
//         DWORD bufSize = sizeof(buf);
//         DWORD type = 0;
//         if (RegQueryValueExA(hKey, "LastVisitedPlanet", nullptr, &type,
//             (LPBYTE)buf, &bufSize) == ERROR_SUCCESS && type == REG_SZ) {
//             planetName = buf;
//         }
//         RegCloseKey(hKey);
//     }
//
//     if (!planetName.empty()) {
//         std::string exactTitle = "Entropia Universe Client (64bit) [" + planetName + "]";
//         HWND hwnd = FindWindowA(nullptr, exactTitle.c_str());
//         if (hwnd) return hwnd;
//     }
//
//     auto windows = EnumerateWindows();
//     for (auto& [hwnd, title] : windows) {
//         if (title.find("Entropia Universe Client") != std::string::npos)
//             return hwnd;
//     }
//
//     return nullptr;
// }

Bitmap CaptureWindow(HWND hwnd) {
    Bitmap bmp;

    if (!hwnd || !IsWindow(hwnd)) return bmp;

    // Get client area size
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int w = clientRect.right - clientRect.left;
    int h = clientRect.bottom - clientRect.top;

    if (w <= 0 || h <= 0) return bmp;

    // Use PrintWindow for better compatibility with DX/overlay windows
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, w, h);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // Try PrintWindow first (works with most game windows)
    // PW_CLIENTONLY = 1, PW_RENDERFULLCONTENT = 2
    BOOL ok = PrintWindow(hwnd, hdcMem, 1 | 2);

    if (!ok) {
        // Fallback to BitBlt from window DC
        HDC hdcWin = GetDC(hwnd);
        BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);
        ReleaseDC(hwnd, hdcWin);
    }

    // Extract pixel data
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h;  // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    bmp.create(w, h);
    GetDIBits(hdcMem, hBitmap, 0, h, bmp.data.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(hdcMem, hOld);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    return bmp;
}

Bitmap CaptureScreen(RECT region) {
    Bitmap bmp;
    int w = region.right - region.left;
    int h = region.bottom - region.top;
    if (w <= 0 || h <= 0) return bmp;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, w, h);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBitmap);

    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, region.left, region.top, SRCCOPY);

    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    bmp.create(w, h);
    GetDIBits(hdcMem, hBitmap, 0, h, bmp.data.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(hdcMem, hOld);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    return bmp;
}

Bitmap CaptureFullScreen() {
    RECT r;
    r.left = 0;
    r.top = 0;
    r.right = GetSystemMetrics(SM_CXSCREEN);
    r.bottom = GetSystemMetrics(SM_CYSCREEN);
    return CaptureScreen(r);
}

// GetWindowClientScreen — currently unused
// RECT GetWindowClientScreen(HWND hwnd) {
//     RECT r = {};
//     POINT pt = {0, 0};
//     ClientToScreen(hwnd, &pt);
//     RECT client;
//     GetClientRect(hwnd, &client);
//     r.left = pt.x;
//     r.top = pt.y;
//     r.right = pt.x + client.right;
//     r.bottom = pt.y + client.bottom;
//     return r;
// }

// HashRegion — currently unused
// RegionHash HashRegion(const Bitmap& bmp, int x, int y, int w, int h) {
//     uint64_t hash = 14695981039346656037ULL;
//     const uint64_t prime = 1099511628211ULL;
//
//     for (int sy = y; sy < y + h && sy < bmp.height; sy += 4) {
//         for (int sx = x; sx < x + w && sx < bmp.width; sx += 4) {
//             Pixel p = bmp.pixel(sx, sy);
//             hash ^= p.r; hash *= prime;
//             hash ^= p.g; hash *= prime;
//             hash ^= p.b; hash *= prime;
//         }
//     }
//
//     return {hash};
// }

}  // namespace Capture
