// ============================================================================
// EU Skill Reader - Main Application Window
//
// A Windows application that captures and reads skill data from the
// Entropia Universe game client Skills window using font-based OCR.
//
// Build: cl /O2 /EHsc /std:c++17 /Fe:eu_skill_reader.exe src\*.cpp
//        /link user32.lib gdi32.lib dwmapi.lib comctl32.lib comdlg32.lib
// ============================================================================

#include "app.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <gdiplus.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")

// ============================================================================
// Dark Mode Support (runtime-loaded, graceful fallback)
// ============================================================================

static bool g_darkMode = false;
static HBRUSH g_darkBgBrush = nullptr;
static HBRUSH g_darkEditBrush = nullptr;
static const COLORREF DARK_BG = RGB(32, 32, 32);
static const COLORREF DARK_EDIT_BG = RGB(43, 43, 43);
static const COLORREF DARK_TEXT = RGB(220, 220, 220);
static const COLORREF DARK_HEADER = RGB(55, 55, 55);
static const COLORREF DARK_SEP = RGB(80, 80, 80);
static HBRUSH g_darkHeaderBrush = nullptr;
static HBRUSH g_darkSepBrush = nullptr;

// Undocumented uxtheme ordinals (Windows 10 1809+)
using fnSetPreferredAppMode = int(WINAPI*)(int);             // ordinal 135
using fnAllowDarkModeForWindow = bool(WINAPI*)(HWND, bool);  // ordinal 133
using fnShouldAppsUseDarkMode = bool(WINAPI*)();             // ordinal 132

static fnSetPreferredAppMode pSetPreferredAppMode = nullptr;
static fnAllowDarkModeForWindow pAllowDarkModeForWindow = nullptr;

static void DarkMode_Init() {
    // Check if user has dark mode enabled via registry
    HKEY hKey = nullptr;
    DWORD useLightTheme = 1;
    DWORD size = sizeof(useLightTheme);
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ,
                      &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "AppsUseLightTheme", nullptr, nullptr, (BYTE*)&useLightTheme, &size);
        RegCloseKey(hKey);
    }
    if (useLightTheme == 1) return;  // User prefers light mode

    // Try to load undocumented uxtheme dark mode APIs
    HMODULE hUxTheme = LoadLibraryA("uxtheme.dll");
    if (!hUxTheme) return;

    pSetPreferredAppMode = (fnSetPreferredAppMode)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
    pAllowDarkModeForWindow = (fnAllowDarkModeForWindow)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133));

    if (pSetPreferredAppMode) {
        pSetPreferredAppMode(1);  // AllowDark
    }

    g_darkMode = true;
    g_darkBgBrush = CreateSolidBrush(DARK_BG);
    g_darkEditBrush = CreateSolidBrush(DARK_EDIT_BG);
    g_darkHeaderBrush = CreateSolidBrush(DARK_HEADER);
    g_darkSepBrush = CreateSolidBrush(DARK_SEP);
}

static void DarkMode_ApplyToWindow(HWND hwnd) {
    if (!g_darkMode) return;

    // Dark title bar (official DWM API, Windows 10 2004+)
    BOOL useDark = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
    DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));

    if (pAllowDarkModeForWindow) {
        pAllowDarkModeForWindow(hwnd, true);
    }
}

static void DarkMode_ApplyToChild(HWND hwnd) {
    if (!g_darkMode || !pAllowDarkModeForWindow) return;
    pAllowDarkModeForWindow(hwnd, true);
    SendMessageA(hwnd, WM_THEMECHANGED, 0, 0);
}

// Control IDs
enum {
    ID_BTN_FIND_WINDOW = 1001,
    ID_BTN_CALIBRATE,
    ID_BTN_CAPTURE,
    ID_BTN_MONITOR,
    ID_BTN_EXPORT,
    ID_BTN_CLEAR,
    ID_BTN_DEBUG,
    ID_BTN_SHOW_LOG,
    ID_BTN_GUIDE_TOGGLE,
    ID_LBL_GUIDE,
    ID_COMBO_WINDOWS,
    ID_LIST_SKILLS,
    ID_EDIT_LOG,
    ID_TIMER_POLL,
    ID_TIMER_REFRESH,
    ID_LBL_STATUS,
    ID_LBL_STATS,
};

// Globals
static App* g_app = nullptr;
static HWND g_hwndMain = nullptr;
static HWND g_hwndComboWindows = nullptr;
static HWND g_hwndListSkills = nullptr;
static HWND g_hwndEditLog = nullptr;
static HWND g_hwndBtnCalibrate = nullptr;
static HWND g_hwndBtnCapture = nullptr;
static HWND g_hwndBtnMonitor = nullptr;
static HWND g_hwndBtnExport = nullptr;
static HWND g_hwndLblStatus = nullptr;
static HWND g_hwndLblStats = nullptr;
static HWND g_hwndLblLog = nullptr;
static HWND g_hwndSeparator = nullptr;
static HWND g_hwndBtnShowLog = nullptr;
static HWND g_hwndBtnDebug = nullptr;
static HWND g_hwndBtnGuideToggle = nullptr;
static HWND g_hwndLblGuide = nullptr;
static HWND g_hwndLblGuideText = nullptr;
static HWND g_hwndLblCalibStatus = nullptr;
static bool g_guideVisible = false;
static bool g_diagnosticMode = false;
static const int GUIDE_TEXT_H = 96;  // height of guide text area including padding

// Window dimensions
static const int WINDOW_W = 550;
static const int WINDOW_H_COMPACT = 620;
static const int LOG_AREA_H = 210;  // log label + edit + debug button + padding

static std::vector<std::pair<HWND, std::string>> g_windowList;

// ListView subclass proc to intercept header notifications
// ListView subclass proc to intercept header custom draw
static WNDPROC g_origListViewProc = nullptr;
static LRESULT CALLBACK ListViewSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_darkMode && msg == WM_NOTIFY) {
        NMHDR* nmh = (NMHDR*)lParam;
        if (nmh->code == NM_CUSTOMDRAW) {
            NMCUSTOMDRAW* nmcd = (NMCUSTOMDRAW*)lParam;
            HWND hHeader = ListView_GetHeader(hwnd);
            if (nmh->hwndFrom == hHeader) {
                switch (nmcd->dwDrawStage) {
                    case CDDS_PREPAINT: {
                        RECT headerRc;
                        GetClientRect(hHeader, &headerRc);
                        FillRect(nmcd->hdc, &headerRc, g_darkHeaderBrush);
                        // Bottom separator line
                        HPEN pen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
                        HPEN oldPen = (HPEN)SelectObject(nmcd->hdc, pen);
                        MoveToEx(nmcd->hdc, headerRc.left, headerRc.bottom - 1, nullptr);
                        LineTo(nmcd->hdc, headerRc.right, headerRc.bottom - 1);
                        SelectObject(nmcd->hdc, oldPen);
                        DeleteObject(pen);
                        return CDRF_NOTIFYITEMDRAW;
                    }
                    case CDDS_ITEMPREPAINT: {
                        FillRect(nmcd->hdc, &nmcd->rc, g_darkHeaderBrush);
                        char text[128] = {};
                        HDITEMA hdi = {};
                        hdi.mask = HDI_TEXT | HDI_FORMAT;
                        hdi.pszText = text;
                        hdi.cchTextMax = sizeof(text);
                        Header_GetItem(hHeader, nmcd->dwItemSpec, &hdi);
                        SetTextColor(nmcd->hdc, DARK_TEXT);
                        SetBkMode(nmcd->hdc, TRANSPARENT);
                        RECT rc = nmcd->rc;
                        rc.left += 6;
                        rc.right -= 6;
                        UINT fmt = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
                        if (hdi.fmt & HDF_RIGHT)
                            fmt |= DT_RIGHT;
                        else if (hdi.fmt & HDF_CENTER)
                            fmt |= DT_CENTER;
                        DrawTextA(nmcd->hdc, text, -1, &rc, fmt);
                        return CDRF_SKIPDEFAULT;
                    }
                }
            }
        }
    }
    return CallWindowProc(g_origListViewProc, hwnd, msg, wParam, lParam);
}

// ============================================================================
// EU Not Found Dialog (dark-mode-aware)
// ============================================================================

enum { ID_EUNF_TITLE = 190, ID_EUNF_TEXT = 191, ID_EUNF_OK = 192 };

static INT_PTR CALLBACK EUNotFoundDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM /*lParam*/) {
    switch (msg) {
        case WM_INITDIALOG: {
            HFONT hFont = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                      "Segoe UI");
            HFONT hFontBold = CreateFontA(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                          0, "Segoe UI");
            SendDlgItemMessage(hwnd, ID_EUNF_TITLE, WM_SETFONT, (WPARAM)hFontBold, TRUE);
            SendDlgItemMessage(hwnd, ID_EUNF_TEXT, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, ID_EUNF_OK, WM_SETFONT, (WPARAM)hFont, TRUE);

            if (g_darkMode) {
                DarkMode_ApplyToWindow(hwnd);
                HWND hOk = GetDlgItem(hwnd, ID_EUNF_OK);
                if (hOk) {
                    DarkMode_ApplyToChild(hOk);
                    SetWindowTheme(hOk, L"DarkMode_Explorer", nullptr);
                }
            }

            SetFocus(GetDlgItem(hwnd, ID_EUNF_OK));
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_EUNF_OK || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, 0);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            EndDialog(hwnd, 0);
            return TRUE;
        case WM_CTLCOLORSTATIC:
            if (g_darkMode) {
                HDC hdc = (HDC)wParam;
                SetTextColor(hdc, DARK_TEXT);
                SetBkColor(hdc, DARK_BG);
                return (INT_PTR)g_darkBgBrush;
            }
            break;
        case WM_CTLCOLORDLG:
            if (g_darkMode) return (INT_PTR)g_darkBgBrush;
            break;
        case WM_CTLCOLORBTN:
            if (g_darkMode) return (INT_PTR)g_darkBgBrush;
            break;
    }
    return FALSE;
}

static void ShowEUNotFound(HINSTANCE hInstance) {
    int dlgW = 260, dlgH = 100;
    int pad = 8;
    int btnW = 50, btnH = 14;
    int textTop = pad + 14;
    int btnY = dlgH - btnH - 10;
    int textH = btnY - textTop - 4;
    int btnX = (dlgW - btnW) / 2;

    alignas(4) BYTE buf[1024] = {};
    BYTE* p = buf;
    const BYTE* const bufEnd = buf + sizeof(buf);

    auto align4 = [](BYTE*& ptr) { ptr = (BYTE*)(((ULONG_PTR)ptr + 3) & ~3); };

    auto* dlg = (DLGTEMPLATE*)p;
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlg->cdit = 3;
    dlg->cx = (short)dlgW;
    dlg->cy = (short)dlgH;
    p += sizeof(DLGTEMPLATE);
    *(WORD*)p = 0;
    p += sizeof(WORD);
    *(WORD*)p = 0;
    p += sizeof(WORD);
    const WCHAR* title = L"EU Skill Reader";
    size_t tb = (wcslen(title) + 1) * sizeof(WCHAR);
    memcpy(p, title, tb);
    p += tb;

    auto addItem = [&](DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const WCHAR* text) {
        align4(p);
        size_t textBytes = (wcslen(text) + 1) * sizeof(WCHAR);
        size_t needed = sizeof(DLGITEMTEMPLATE) + 2 * sizeof(WORD) + textBytes + sizeof(WORD);
        if (p + needed > bufEnd) {
            OutputDebugStringA("EU Skill Reader: dialog template buffer overflow in ShowEUNotFound\n");
            return;
        }
        auto* item = (DLGITEMTEMPLATE*)p;
        item->style = WS_CHILD | WS_VISIBLE | style;
        item->x = x;
        item->y = y;
        item->cx = cx;
        item->cy = cy;
        item->id = id;
        p += sizeof(DLGITEMTEMPLATE);
        *(WORD*)p = 0xFFFF;
        p += sizeof(WORD);
        *(WORD*)p = cls;
        p += sizeof(WORD);
        memcpy(p, text, textBytes);
        p += textBytes;
        *(WORD*)p = 0;
        p += sizeof(WORD);
    };

    addItem(SS_LEFT, (short)pad, (short)pad, (short)(dlgW - pad * 2), 10, ID_EUNF_TITLE, 0x0082,
            L"Entropia Universe Not Found");
    addItem(SS_LEFT, (short)pad, (short)textTop, (short)(dlgW - pad * 2), (short)textH, ID_EUNF_TEXT, 0x0082,
            L"Please install and launch Entropia Universe at least once before using this program.");
    addItem(BS_DEFPUSHBUTTON | WS_TABSTOP, (short)btnX, (short)btnY, (short)btnW, (short)btnH, ID_EUNF_OK, 0x0080, L"OK");

    DialogBoxIndirectA(hInstance, (DLGTEMPLATE*)buf, nullptr, EUNotFoundDlgProc);
}

// ============================================================================
// Disclaimer Dialog (using DialogBoxIndirect — proper modal dialog)
// ============================================================================

enum { ID_DISC_AGREE = 200, ID_DISC_DISAGREE = 201, ID_DISC_TEXT = 202, ID_DISC_TITLE = 203 };

static const char* g_disclaimerContent =
    "This software will extract and use a font file from the Entropia Universe "
    "game data files to enable accurate OCR of the Skills window. A copy of this "
    "font will be written to your computer's temporary files folder. No other game "
    "files are read/extracted and none are modified.\r\n\r\n"
    "This software is provided \"as is\", without warranty of any kind, express or "
    "implied, including but not limited to the warranties of merchantability or "
    "fitness for a particular purpose. Use at your own risk.\r\n\r\n"
    "EU Skill Reader is fan-developed software and is not affiliated with MindArk PE AB or it's partners. "
    "Entropia Universe is a trademark of MindArk PE AB.\r\n\r\n"
    "Continued use of this software implies understanding and consent of the above.";

static INT_PTR CALLBACK DisclaimerDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM /*lParam*/) {
    switch (msg) {
        case WM_INITDIALOG: {
            // Set the actual content text (placeholder in template)
            SetDlgItemTextA(hwnd, ID_DISC_TEXT, g_disclaimerContent);

            // Apply fonts
            HFONT hFont = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                      "Segoe UI");
            HFONT hFontBold = CreateFontA(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                          0, "Segoe UI");
            SendDlgItemMessage(hwnd, ID_DISC_TITLE, WM_SETFONT, (WPARAM)hFontBold, TRUE);
            SendDlgItemMessage(hwnd, ID_DISC_TEXT, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, ID_DISC_AGREE, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, ID_DISC_DISAGREE, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Dark mode
            if (g_darkMode) {
                DarkMode_ApplyToWindow(hwnd);
                HWND hAgree = GetDlgItem(hwnd, ID_DISC_AGREE);
                HWND hDisagree = GetDlgItem(hwnd, ID_DISC_DISAGREE);
                if (hAgree) {
                    DarkMode_ApplyToChild(hAgree);
                    SetWindowTheme(hAgree, L"DarkMode_Explorer", nullptr);
                }
                if (hDisagree) {
                    DarkMode_ApplyToChild(hDisagree);
                    SetWindowTheme(hDisagree, L"DarkMode_Explorer", nullptr);
                }
            }

            // Set focus to I Disagree (return FALSE to prevent dialog overriding focus)
            SetFocus(GetDlgItem(hwnd, ID_DISC_DISAGREE));
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_DISC_AGREE) {
                EndDialog(hwnd, 1);
                return TRUE;
            }
            if (LOWORD(wParam) == ID_DISC_DISAGREE || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, 0);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            EndDialog(hwnd, 0);
            return TRUE;
        case WM_CTLCOLORSTATIC:
            if (g_darkMode) {
                HDC hdc = (HDC)wParam;
                SetTextColor(hdc, DARK_TEXT);
                SetBkColor(hdc, DARK_BG);
                return (INT_PTR)g_darkBgBrush;
            }
            break;
        case WM_CTLCOLORDLG:
            if (g_darkMode) return (INT_PTR)g_darkBgBrush;
            break;
        case WM_CTLCOLORBTN:
            if (g_darkMode) return (INT_PTR)g_darkBgBrush;
            break;
    }
    return FALSE;
}

static bool ShowDisclaimer(HINSTANCE hInstance) {
    // In-memory dialog template (all sizes in dialog units)
    int dlgW = 280, dlgH = 200;
    int pad = 8;
    int btnW = 60, btnH = 14;
    int textTop = pad + 14;
    int btnY = dlgH - btnH - 10;
    int textH = btnY - textTop - 4;
    int totalBtnW = btnW * 2 + 6;
    int btnX = (dlgW - totalBtnW) / 2;

    alignas(4) BYTE buf[2048] = {};
    BYTE* p = buf;
    const BYTE* const bufEnd = buf + sizeof(buf);

    auto align4 = [](BYTE*& ptr) { ptr = (BYTE*)(((ULONG_PTR)ptr + 3) & ~3); };

    // DLGTEMPLATE header
    auto* dlg = (DLGTEMPLATE*)p;
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlg->cdit = 4;
    dlg->cx = (short)dlgW;
    dlg->cy = (short)dlgH;
    p += sizeof(DLGTEMPLATE);
    *(WORD*)p = 0;
    p += sizeof(WORD);  // no menu
    *(WORD*)p = 0;
    p += sizeof(WORD);  // default class
    const WCHAR* title = L"EU Skill Reader - Disclaimer";
    size_t tb = (wcslen(title) + 1) * sizeof(WCHAR);
    memcpy(p, title, tb);
    p += tb;

    // Helper: add a dialog item (with bounds check)
    auto addItem = [&](DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const WCHAR* text) {
        align4(p);
        size_t textBytes = (wcslen(text) + 1) * sizeof(WCHAR);
        size_t needed = sizeof(DLGITEMTEMPLATE) + 2 * sizeof(WORD) + textBytes + sizeof(WORD);
        if (p + needed > bufEnd) {  // overflow guard
            OutputDebugStringA("EU Skill Reader: dialog template buffer overflow in ShowDisclaimer\n");
            return;
        }
        auto* item = (DLGITEMTEMPLATE*)p;
        item->style = WS_CHILD | WS_VISIBLE | style;
        item->x = x;
        item->y = y;
        item->cx = cx;
        item->cy = cy;
        item->id = id;
        p += sizeof(DLGITEMTEMPLATE);
        *(WORD*)p = 0xFFFF;
        p += sizeof(WORD);
        *(WORD*)p = cls;
        p += sizeof(WORD);
        memcpy(p, text, textBytes);
        p += textBytes;
        *(WORD*)p = 0;
        p += sizeof(WORD);  // no creation data
    };

    // 0x0082 = Static, 0x0080 = Button
    addItem(SS_LEFT, (short)pad, (short)pad, (short)(dlgW - pad * 2), 10, ID_DISC_TITLE, 0x0082, L"Disclaimer");
    addItem(SS_LEFT, (short)pad, (short)textTop, (short)(dlgW - pad * 2), (short)textH, ID_DISC_TEXT, 0x0082, L" ");
    addItem(BS_PUSHBUTTON | WS_TABSTOP, (short)btnX, (short)btnY, (short)btnW, (short)btnH, ID_DISC_AGREE, 0x0080, L"I Agree");
    addItem(BS_DEFPUSHBUTTON | WS_TABSTOP, (short)(btnX + btnW + 6), (short)btnY, (short)btnW, (short)btnH, ID_DISC_DISAGREE,
            0x0080, L"I Disagree");

    INT_PTR result = DialogBoxIndirectA(hInstance, (DLGTEMPLATE*)buf, nullptr, DisclaimerDlgProc);
    return result == 1;
}

// ============================================================================
// Key Prompt Dialog (same pattern as Disclaimer — DialogBoxIndirect)
// ============================================================================

enum {
    ID_KEY_LABEL = 300,
    ID_KEY_EDIT = 301,
    ID_KEY_HINT = 302,
    ID_KEY_ERROR = 303,
    ID_KEY_OK = 304,
    ID_KEY_CANCEL = 305,
    ID_KEY_HELP = 306,
    ID_KEY_BROWSE = 307,
    ID_KEY_SEP = 308,
    ID_KEY_BROWSE_LABEL = 309,
    ID_KEY_DL_TEXT = 310,
    ID_KEY_LINK_DIRECT = 311,
    ID_KEY_LINK_WAYBACK = 312,
    ID_KEY_LINK1_LABEL = 313,
    ID_KEY_LINK2_LABEL = 314,
    ID_KEY_INTRO_LABEL = 315,
    ID_KEY_INTRO_TEXT = 316,
    ID_KEY_INTRO_SEP = 317
};

struct KeyDialogData {
    uint8_t key[16];
    bool accepted;
};

// Search a .bms file for a line like: set KEY binary "..."
// Returns the key portion (everything after "binary ") or empty string.
static std::string ExtractKeyFromBmsFile(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return "";
    char line[1024];
    std::string result;
    while (fgets(line, sizeof(line), f)) {
        // Look for "set KEY binary" (case-insensitive on the command)
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (_strnicmp(p, "set", 3) != 0) continue;
        p += 3;
        if (*p != ' ' && *p != '\t') continue;
        while (*p == ' ' || *p == '\t') p++;
        // Skip variable name
        while (*p && *p != ' ' && *p != '\t') p++;
        while (*p == ' ' || *p == '\t') p++;
        if (_strnicmp(p, "binary", 6) != 0) continue;
        p += 6;
        if (*p != ' ' && *p != '\t') continue;
        while (*p == ' ' || *p == '\t') p++;
        // p now points to the value (e.g. "\xB5\xA3..." with or without quotes)
        // Trim trailing whitespace/newline
        size_t len = strlen(p);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == '\n' || p[len - 1] == ' ')) p[--len] = '\0';
        result = p;
        break;
    }
    fclose(f);
    return result;
}

static INT_PTR CALLBACK KeyDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, lParam);

            HFONT hFont = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                      "Segoe UI");
            HFONT hFontBold = CreateFontA(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                          0, "Segoe UI");
            HFONT hFontSmall = CreateFontA(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                           CLEARTYPE_QUALITY, 0, "Segoe UI");
            SendDlgItemMessage(hwnd, ID_KEY_INTRO_LABEL, WM_SETFONT, (WPARAM)hFontBold, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_INTRO_TEXT, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_LABEL, WM_SETFONT, (WPARAM)hFontBold, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_EDIT, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_HINT, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_ERROR, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_OK, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_CANCEL, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_BROWSE, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_BROWSE_LABEL, WM_SETFONT, (WPARAM)hFontBold, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_HELP, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_DL_TEXT, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_LINK1_LABEL, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_LINK2_LABEL, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

            // Underlined font for links
            HFONT hFontLink = CreateFontA(-11, 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                          0, "Segoe UI");
            SendDlgItemMessage(hwnd, ID_KEY_LINK_DIRECT, WM_SETFONT, (WPARAM)hFontLink, TRUE);
            SendDlgItemMessage(hwnd, ID_KEY_LINK_WAYBACK, WM_SETFONT, (WPARAM)hFontLink, TRUE);

            // OK starts disabled until user types something
            EnableWindow(GetDlgItem(hwnd, ID_KEY_OK), FALSE);

            if (g_darkMode) {
                DarkMode_ApplyToWindow(hwnd);
                HWND hOk = GetDlgItem(hwnd, ID_KEY_OK);
                HWND hCancel = GetDlgItem(hwnd, ID_KEY_CANCEL);
                HWND hBrowse = GetDlgItem(hwnd, ID_KEY_BROWSE);
                HWND hEdit = GetDlgItem(hwnd, ID_KEY_EDIT);
                if (hOk) {
                    DarkMode_ApplyToChild(hOk);
                    SetWindowTheme(hOk, L"DarkMode_Explorer", nullptr);
                }
                if (hCancel) {
                    DarkMode_ApplyToChild(hCancel);
                    SetWindowTheme(hCancel, L"DarkMode_Explorer", nullptr);
                }
                if (hBrowse) {
                    DarkMode_ApplyToChild(hBrowse);
                    SetWindowTheme(hBrowse, L"DarkMode_Explorer", nullptr);
                }
                if (hEdit) {
                    DarkMode_ApplyToChild(hEdit);
                    SetWindowTheme(hEdit, L"DarkMode_Explorer", nullptr);
                }
            }

            // Clear error label
            SetDlgItemTextA(hwnd, ID_KEY_ERROR, "");
            SetFocus(GetDlgItem(hwnd, ID_KEY_EDIT));
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_KEY_EDIT && HIWORD(wParam) == EN_CHANGE) {
                int len = GetWindowTextLengthA(GetDlgItem(hwnd, ID_KEY_EDIT));
                EnableWindow(GetDlgItem(hwnd, ID_KEY_OK), len > 0);
                return TRUE;
            }
            if (LOWORD(wParam) == ID_KEY_OK) {
                char buf[512] = {};
                GetDlgItemTextA(hwnd, ID_KEY_EDIT, buf, sizeof(buf) - 1);
                auto result = App::ParseAndValidateKey(buf);
                if (result.valid) {
                    auto* data = (KeyDialogData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
                    memcpy(data->key, result.key, 16);
                    data->accepted = true;
                    EndDialog(hwnd, 1);
                } else {
                    SetDlgItemTextA(hwnd, ID_KEY_ERROR, result.error.c_str());
                }
                return TRUE;
            }
            if (LOWORD(wParam) == ID_KEY_BROWSE) {
                // Use IFileOpenDialog — reliably opens to Downloads folder
                // (GetOpenFileNameA caches last-used dir and ignores lpstrInitialDir)
                std::string selectedFile;
                {
                    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                    IFileOpenDialog* pDlg = nullptr;
                    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg));
                    if (SUCCEEDED(hr)) {
                        // Set file filter to .bms only
                        COMDLG_FILTERSPEC filter = {L"QuickBMS Scripts (*.bms)", L"*.bms"};
                        pDlg->SetFileTypes(1, &filter);
                        pDlg->SetTitle(L"Select QuickBMS Script");

                        // Set Downloads as the default folder
                        IShellItem* pFolder = nullptr;
                        if (SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_Downloads, 0, nullptr, IID_PPV_ARGS(&pFolder)))) {
                            pDlg->SetFolder(pFolder);
                            pFolder->Release();
                        }

                        if (SUCCEEDED(pDlg->Show(hwnd))) {
                            IShellItem* pItem = nullptr;
                            if (SUCCEEDED(pDlg->GetResult(&pItem))) {
                                PWSTR wFilePath = nullptr;
                                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &wFilePath))) {
                                    char aPath[MAX_PATH] = {};
                                    WideCharToMultiByte(CP_ACP, 0, wFilePath, -1, aPath, MAX_PATH, nullptr, nullptr);
                                    selectedFile = aPath;
                                    CoTaskMemFree(wFilePath);
                                }
                                pItem->Release();
                            }
                        }
                        pDlg->Release();
                    }
                }
                if (!selectedFile.empty()) {
                    std::string keyStr = ExtractKeyFromBmsFile(selectedFile.c_str());
                    if (keyStr.empty()) {
                        SetDlgItemTextA(hwnd, ID_KEY_ERROR, "No key found in selected file");
                    } else {
                        auto result = App::ParseAndValidateKey(keyStr.c_str());
                        if (result.valid) {
                            auto* data = (KeyDialogData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
                            memcpy(data->key, result.key, 16);
                            data->accepted = true;
                            EndDialog(hwnd, 1);
                        } else {
                            SetDlgItemTextA(hwnd, ID_KEY_ERROR, result.error.c_str());
                        }
                    }
                }
                return TRUE;
            }
            if (LOWORD(wParam) == ID_KEY_LINK_DIRECT) {
                ShellExecuteA(hwnd, "open", "https://aluigi.altervista.org/bms/entropia.bms", nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            if (LOWORD(wParam) == ID_KEY_LINK_WAYBACK) {
                ShellExecuteA(hwnd, "open", "https://web.archive.org/web/https://aluigi.altervista.org/bms/entropia.bms",
                              nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            if (LOWORD(wParam) == ID_KEY_CANCEL || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, 0);
                return TRUE;
            }
            break;
        case WM_SETCURSOR: {
            HWND hCtrl = (HWND)wParam;
            int id = GetDlgCtrlID(hCtrl);
            if (id == ID_KEY_LINK_DIRECT || id == ID_KEY_LINK_WAYBACK) {
                static HCURSOR hHand = LoadCursor(nullptr, IDC_HAND);
                SetCursor(hHand);
                SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            EndDialog(hwnd, 0);
            return TRUE;
        case WM_CTLCOLORSTATIC: {
            HWND hCtrl = (HWND)lParam;
            HDC hdc = (HDC)wParam;
            if (GetDlgCtrlID(hCtrl) == ID_KEY_SEP || GetDlgCtrlID(hCtrl) == ID_KEY_INTRO_SEP) {
                return (INT_PTR)(g_darkMode ? g_darkSepBrush : GetSysColorBrush(COLOR_BTNSHADOW));
            }
            if (GetDlgCtrlID(hCtrl) == ID_KEY_LINK_DIRECT || GetDlgCtrlID(hCtrl) == ID_KEY_LINK_WAYBACK) {
                SetTextColor(hdc, g_darkMode ? RGB(100, 180, 255) : RGB(0, 102, 204));
                SetBkColor(hdc, g_darkMode ? DARK_BG : GetSysColor(COLOR_3DFACE));
                return (INT_PTR)(g_darkMode ? g_darkBgBrush : GetSysColorBrush(COLOR_3DFACE));
            }
            if (GetDlgCtrlID(hCtrl) == ID_KEY_ERROR) {
                SetTextColor(hdc, RGB(255, 80, 80));
                SetBkColor(hdc, g_darkMode ? DARK_BG : GetSysColor(COLOR_3DFACE));
                return (INT_PTR)(g_darkMode ? g_darkBgBrush : GetSysColorBrush(COLOR_3DFACE));
            }
            if (g_darkMode) {
                SetTextColor(hdc, DARK_TEXT);
                SetBkColor(hdc, DARK_BG);
                return (INT_PTR)g_darkBgBrush;
            }
            break;
        }
        case WM_CTLCOLOREDIT:
            if (g_darkMode) {
                HDC hdc = (HDC)wParam;
                SetTextColor(hdc, DARK_TEXT);
                SetBkColor(hdc, DARK_EDIT_BG);
                return (INT_PTR)g_darkEditBrush;
            }
            break;
        case WM_CTLCOLORDLG:
            if (g_darkMode) return (INT_PTR)g_darkBgBrush;
            break;
        case WM_CTLCOLORBTN:
            if (g_darkMode) return (INT_PTR)g_darkBgBrush;
            break;
    }
    return FALSE;
}

static bool ShowKeyDialog(uint8_t out_key[16]) {
    int dlgW = 300;
    int pad = 8;
    int btnW = 50, btnH = 14;
    int browseW = 75;
    int introLabelY = 4;
    int introTextY = introLabelY + 12;
    int introTextH = 36;
    int introSepY = introTextY + introTextH + 3;
    int labelY = introSepY + 6;
    int editY = labelY + 12;
    int editH = 14;
    int hintY = editY + editH + 2;
    int errorY = hintY + 10;
    int sepY = errorY + 14;
    int browseLabelY = sepY + 5;
    int browseY = browseLabelY + 12;
    int helpY = browseY + btnH + 3;
    int helpH = 18;
    int dlTextY = helpY + helpH + 2;
    int dlTextH = 18;
    int link1Y = dlTextY + dlTextH - 2;
    int link2Y = link1Y + 10;
    int btnY = link2Y + 12;
    int dlgH = btnY + btnH + 8;
    int totalBtnW = btnW * 2 + 6;
    int btnX = (dlgW - totalBtnW) / 2;

    alignas(4) BYTE buf[6144] = {};
    BYTE* p = buf;
    const BYTE* const bufEnd = buf + sizeof(buf);

    auto align4 = [](BYTE*& ptr) { ptr = (BYTE*)(((ULONG_PTR)ptr + 3) & ~3); };

    auto* dlg = (DLGTEMPLATE*)p;
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlg->cdit = 18;
    dlg->cx = (short)dlgW;
    dlg->cy = (short)dlgH;
    p += sizeof(DLGTEMPLATE);
    *(WORD*)p = 0;
    p += sizeof(WORD);  // no menu
    *(WORD*)p = 0;
    p += sizeof(WORD);  // default class
    const WCHAR* title = L"EU Skill Reader - Decryption Key";
    size_t tb = (wcslen(title) + 1) * sizeof(WCHAR);
    memcpy(p, title, tb);
    p += tb;

    // Helper: add a dialog item (with bounds check)
    auto addItem = [&](DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const WCHAR* text) {
        align4(p);
        size_t textBytes = (wcslen(text) + 1) * sizeof(WCHAR);
        size_t needed = sizeof(DLGITEMTEMPLATE) + 2 * sizeof(WORD) + textBytes + sizeof(WORD);
        if (p + needed > bufEnd) {  // overflow guard
            OutputDebugStringA("EU Skill Reader: dialog template buffer overflow in ShowKeyDialog\n");
            return;
        }
        auto* item = (DLGITEMTEMPLATE*)p;
        item->style = WS_CHILD | WS_VISIBLE | style;
        item->x = x;
        item->y = y;
        item->cx = cx;
        item->cy = cy;
        item->id = id;
        p += sizeof(DLGITEMTEMPLATE);
        *(WORD*)p = 0xFFFF;
        p += sizeof(WORD);
        *(WORD*)p = cls;
        p += sizeof(WORD);
        memcpy(p, text, textBytes);
        p += textBytes;
        *(WORD*)p = 0;
        p += sizeof(WORD);
    };

    // 0x0082 = Static, 0x0081 = Edit, 0x0080 = Button
    addItem(SS_LEFT, (short)pad, (short)introLabelY, (short)(dlgW - pad * 2), 10, ID_KEY_INTRO_LABEL, 0x0082,
            L"We need a file decryption key from you...");
    addItem(SS_LEFT, (short)pad, (short)introTextY, (short)(dlgW - pad * 2), (short)introTextH, ID_KEY_INTRO_TEXT, 0x0082,
            L"This app extracts a font file from the game's .pak files to "
            L"read skill data. To decrypt those files we need you to supply "
            L"the key. You only need to do this once \x2014 the key is saved "
            L"for future use. You can either paste the key directly, or "
            L"browse for a QuickBMS script (.bms) file to extract it from.");
    addItem(SS_LEFT, (short)pad, (short)introSepY, (short)(dlgW - pad * 2), 1, ID_KEY_INTRO_SEP, 0x0082, L"");
    addItem(SS_LEFT, (short)pad, (short)labelY, (short)(dlgW - pad * 2), 10, ID_KEY_LABEL, 0x0082,
            L"Option 1: Paste the key directly");
    addItem(ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, (short)pad, (short)editY, (short)(dlgW - pad * 2), (short)editH,
            ID_KEY_EDIT, 0x0081, L"");
    addItem(SS_LEFT, (short)pad, (short)hintY, (short)(dlgW - pad * 2), 10, ID_KEY_HINT, 0x0082,
            L"Formats: hex (AABB01...), \\x hex, or base64");
    addItem(SS_LEFT, (short)pad, (short)errorY, (short)(dlgW - pad * 2), 14, ID_KEY_ERROR, 0x0082, L"");
    addItem(SS_LEFT, (short)pad, (short)sepY, (short)(dlgW - pad * 2), 1, ID_KEY_SEP, 0x0082, L"");
    addItem(SS_LEFT, (short)pad, (short)browseLabelY, (short)(dlgW - pad * 2), 12, ID_KEY_BROWSE_LABEL, 0x0082,
            L"Option 2: Browse for QuickBMS script");
    addItem(BS_PUSHBUTTON | WS_TABSTOP, (short)pad, (short)browseY, (short)browseW, (short)btnH, ID_KEY_BROWSE, 0x0080,
            L"Browse .bms...");
    addItem(SS_LEFT, (short)pad, (short)helpY, (short)(dlgW - pad * 2), (short)helpH, ID_KEY_HELP, 0x0082,
            L"The key can be found in the QuickBMS script for Entropia "
            L"Universe (.bms file). Select it to extract the key "
            L"automatically.");
    addItem(SS_LEFT, (short)pad, (short)dlTextY, (short)(dlgW - pad * 2), (short)dlTextH, ID_KEY_DL_TEXT, 0x0082,
            L"Download the QuickBMS script (direct link, or Wayback "
            L"Machine mirror if the main link is down):");
    int linkLabelW = 28;
    addItem(SS_LEFT, (short)pad, (short)link1Y, (short)linkLabelW, 10, ID_KEY_LINK1_LABEL, 0x0082, L"Direct:");
    addItem(SS_LEFT | SS_NOTIFY, (short)(pad + linkLabelW), (short)link1Y, (short)(dlgW - pad * 2 - linkLabelW), 10,
            ID_KEY_LINK_DIRECT, 0x0082, L"https://aluigi.altervista.org/bms/entropia.bms");
    addItem(SS_LEFT, (short)pad, (short)link2Y, (short)linkLabelW, 10, ID_KEY_LINK2_LABEL, 0x0082, L"Mirror:");
    addItem(SS_LEFT | SS_NOTIFY, (short)(pad + linkLabelW), (short)link2Y, (short)(dlgW - pad * 2 - linkLabelW), 10,
            ID_KEY_LINK_WAYBACK, 0x0082, L"https://web.archive.org/web/https://aluigi.altervista.org/bms/entropia.bms");
    addItem(BS_DEFPUSHBUTTON | WS_TABSTOP, (short)btnX, (short)btnY, (short)btnW, (short)btnH, ID_KEY_OK, 0x0080, L"OK");
    addItem(BS_PUSHBUTTON | WS_TABSTOP, (short)(btnX + btnW + 6), (short)btnY, (short)btnW, (short)btnH, ID_KEY_CANCEL, 0x0080,
            L"Cancel");

    KeyDialogData data = {};
    INT_PTR result =
        DialogBoxIndirectParamA(GetModuleHandle(nullptr), (DLGTEMPLATE*)buf, g_hwndMain, KeyDlgProc, (LPARAM)&data);

    if (result == 1 && data.accepted) {
        memcpy(out_key, data.key, 16);
        SecureZeroMemory(&data, sizeof(data));
        return true;
    }
    SecureZeroMemory(&data, sizeof(data));
    return false;
}

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void CreateControls(HWND hwnd);
void RefreshWindowList();
void UpdateSkillList();
void UpdateStats();
void AdjustListViewColumns();
void AppendLog(const std::string& text);
void ToggleDiagnosticMode();
void ToggleUserGuide();
void CopySelectedSkillsToClipboard();

// ============================================================================
// Entry Point
// ============================================================================

#ifdef DEBUG_CONSOLE
int main() { return WinMain(GetModuleHandle(nullptr), nullptr, GetCommandLineA(), SW_SHOW); }
#endif

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR /*lpCmdLine*/, int nCmdShow) {
    // Prevent multiple instances via named mutex
    HANDLE hMutex = CreateMutexA(nullptr, TRUE, "EUSkillReader_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Bring existing instance to front if possible
        HWND existing = FindWindowA("EUSkillReader", nullptr);
        if (existing) {
            SetForegroundWindow(existing);
            if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdipSI;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipSI, nullptr);

    // Initialize common controls
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    // Initialize dark mode (before window creation)
    DarkMode_Init();

    // Check that Entropia Universe has been installed and launched at least once
    {
        HKEY hKey = nullptr;
        LONG rc = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\MindArk\\Entropia Universe\\UserInfo", 0, KEY_READ, &hKey);
        if (rc == ERROR_SUCCESS) {
            RegCloseKey(hKey);
        } else {
            ShowEUNotFound(hInstance);
            Gdiplus::GdiplusShutdown(gdipToken);
            CloseHandle(hMutex);
            return 0;
        }
    }

    // Show disclaimer — exit if user disagrees
    if (!ShowDisclaimer(hInstance)) {
        Gdiplus::GdiplusShutdown(gdipToken);
        CloseHandle(hMutex);
        return 0;
    }

    // Register window class
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_darkMode ? g_darkBgBrush : (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "EUSkillReader";
    wc.hIcon = (HICON)LoadImageA(GetModuleHandle(nullptr), "IDI_APPICON", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageA(GetModuleHandle(nullptr), "IDI_APPICON", IMAGE_ICON, 16, 16, LR_DEFAULTSIZE);
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExA(&wc);

    // Create main window (fixed size, not resizable)
    g_hwndMain = CreateWindowExA(WS_EX_TOPMOST, "EUSkillReader", "EU Skill Reader - Entropia Universe Skill Scanner",
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                 CW_USEDEFAULT, WINDOW_W, WINDOW_H_COMPACT, nullptr, nullptr, hInstance, nullptr);

    if (!g_hwndMain) return 1;

    DarkMode_ApplyToWindow(g_hwndMain);
    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        // Handle Ctrl+A and Ctrl+C for edit and list controls
        if (msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
            HWND focus = GetFocus();
            if (msg.wParam == 'A') {
                if (focus == g_hwndEditLog) {
                    SendMessage(focus, EM_SETSEL, 0, -1);
                    continue;
                }
                if (focus == g_hwndListSkills) {
                    // Select all items
                    int count = ListView_GetItemCount(g_hwndListSkills);
                    for (int i = 0; i < count; i++) ListView_SetItemState(g_hwndListSkills, i, LVIS_SELECTED, LVIS_SELECTED);
                    continue;
                }
            }
            if (msg.wParam == 'C' && focus == g_hwndListSkills) {
                CopySelectedSkillsToClipboard();
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Gdiplus::GdiplusShutdown(gdipToken);
    CloseHandle(hMutex);
    return (int)msg.wParam;
}

// ============================================================================
// UI Creation
// ============================================================================

static HWND CreateLabel(HWND parent, const char* text, int x, int y, int w, int h, int id = 0) {
    return CreateWindowA("STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr,
                         nullptr);
}

static HWND CreateButton(HWND parent, const char* text, int x, int y, int w, int h, int id) {
    return CreateWindowA("BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr,
                         nullptr);
}

void CreateControls(HWND hwnd) {
    HFONT hFont =
        CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    HFONT hFontBold =
        CreateFontA(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    int y = 10;
    int x = 10;
    int fullW = 510;

    HFONT hFontSmall =
        CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    // --- Section: Help (collapsible) ---
    g_hwndLblGuide = CreateWindowA("STATIC", "User Guide", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY, x + 24, y - 3, 100, 20,
                                   hwnd, (HMENU)(INT_PTR)ID_LBL_GUIDE, nullptr, nullptr);
    HFONT hFontGuideLink =
        CreateFontA(-13, 0, 0, 0, FW_BOLD, FALSE, TRUE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    SendMessage(g_hwndLblGuide, WM_SETFONT, (WPARAM)hFontGuideLink, TRUE);

    // Owner-drawn chevron (static control — no button click animation)
    g_hwndBtnGuideToggle = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY, x, y - 4, 20, 20, hwnd,
                                         (HMENU)(INT_PTR)ID_BTN_GUIDE_TOGGLE, nullptr, nullptr);

    y += 24;
    g_hwndLblGuideText =
        CreateLabel(hwnd,
                    "1. Select the game window below and click Refresh if needed.\r\n"
                    "2. Use windowed mode - make the game window fully visible.\r\n"
                    "3. Open the Skills window and drag it to the top-left corner of the game window.\r\n"
                    "4. Ensure \"ALL CATEGORIES\" is selected and \"SKILL NAME\" is sorted so Agility is first.\r\n"
                    "5. Ensure you are on the first page of skills.\r\n"
                    "6. Click Calibrate, then use Capture Page to read each page.",
                    x, y, fullW, 92);
    SendMessage(g_hwndLblGuideText, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
    ShowWindow(g_hwndLblGuideText, SW_HIDE);
    y -= 24;  // guide hidden, so revert the y advance
    y += 26;  // just space for the toggle button

    // --- Section: Game Window ---
    HWND lbl = CreateLabel(hwnd, "Select Game Window:", x, y, 150, 20);
    SendMessage(lbl, WM_SETFONT, (WPARAM)hFontBold, TRUE);

    y += 22;
    int btnW = 120;
    int btnH = 28;
    int btnGap = 6;

    g_hwndComboWindows = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, x, y,
                                       fullW - btnW - btnGap, 300, hwnd, (HMENU)(INT_PTR)ID_COMBO_WINDOWS, nullptr, nullptr);
    SendMessage(g_hwndComboWindows, WM_SETFONT, (WPARAM)hFont, TRUE);

    auto btnRefresh = CreateButton(hwnd, "Refresh", x + fullW - btnW, y - 1, btnW, btnH, ID_BTN_FIND_WINDOW);
    SendMessage(btnRefresh, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Horizontal separator
    y += btnH + 7;
    g_hwndSeparator =
        CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, fullW, 1, hwnd, nullptr, nullptr, nullptr);
    y += 8;

    // --- Stage 1: Calibrate ---
    lbl = CreateLabel(hwnd, "Step 1:", x, y + 4, 55, 20);
    SendMessage(lbl, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    g_hwndBtnCalibrate = CreateButton(hwnd, "Calibrate OCR", x + 58, y, btnW, btnH, ID_BTN_CALIBRATE);
    g_hwndLblCalibStatus = CreateLabel(hwnd, "Select Game Window Above First", x + 58 + btnW + 8, y + 5, 200, 20);
    SendMessage(g_hwndLblCalibStatus, WM_SETFONT, (WPARAM)hFont, TRUE);
    auto btnClear = CreateButton(hwnd, "Clear Data", x + fullW - btnW, y, btnW, btnH, ID_BTN_CLEAR);

    // --- Stage 2: Capture ---
    y += btnH + 4;
    lbl = CreateLabel(hwnd, "Step 2:", x, y + 4, 55, 20);
    SendMessage(lbl, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    g_hwndBtnCapture = CreateButton(hwnd, "Capture Page", x + 58, y, btnW, btnH, ID_BTN_CAPTURE);
    g_hwndBtnShowLog = CreateButton(hwnd, "Show Log", x + fullW - btnW, y, btnW, btnH, ID_BTN_SHOW_LOG);
    // Monitor button hidden — not stable yet
    g_hwndBtnMonitor = CreateButton(hwnd, "Start Monitor", 0, 0, 0, 0, ID_BTN_MONITOR);
    ShowWindow(g_hwndBtnMonitor, SW_HIDE);

    // --- Stage 3: Export ---
    y += btnH + 4;
    lbl = CreateLabel(hwnd, "Step 3:", x, y + 4, 55, 20);
    SendMessage(lbl, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    g_hwndBtnExport = CreateButton(hwnd, "Export CSV", x + 58, y, btnW, btnH, ID_BTN_EXPORT);

    // Disable until calibration succeeds with 12 skills
    EnableWindow(g_hwndBtnCalibrate, FALSE);
    EnableWindow(g_hwndBtnCapture, FALSE);
    EnableWindow(g_hwndBtnMonitor, FALSE);
    EnableWindow(g_hwndBtnExport, FALSE);

    HWND btns[] = {g_hwndBtnCalibrate, g_hwndBtnCapture, g_hwndBtnMonitor, g_hwndBtnExport, btnClear, g_hwndBtnShowLog};
    for (auto b : btns) SendMessage(b, WM_SETFONT, (WPARAM)hFont, TRUE);

    // --- Section: Skill List ---
    y += 38;
    lbl = CreateLabel(hwnd, "Captured Skills:", x, y, 120, 20);
    SendMessage(lbl, WM_SETFONT, (WPARAM)hFontBold, TRUE);

    g_hwndLblStats = CreateWindowA("STATIC", "Skills: 0  |  Points: 0", WS_CHILD | WS_VISIBLE | SS_RIGHT, x + 120, y,
                                   fullW - 120, 20, hwnd, (HMENU)(INT_PTR)ID_LBL_STATS, nullptr, nullptr);
    SendMessage(g_hwndLblStats, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += 22;
    int listH = 300;
    g_hwndListSkills =
        CreateWindowExA(g_darkMode ? 0 : WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                        WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_VSCROLL | WS_HSCROLL | (g_darkMode ? WS_BORDER : 0), x, y,
                        fullW, listH, hwnd, (HMENU)(INT_PTR)ID_LIST_SKILLS, nullptr, nullptr);
    SendMessage(g_hwndListSkills, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Full row select (no gridlines in dark mode — matches Explorer)
    DWORD lvStyle = LVS_EX_FULLROWSELECT;
    if (!g_darkMode) lvStyle |= LVS_EX_GRIDLINES;
    ListView_SetExtendedListViewStyle(g_hwndListSkills, lvStyle);

    // Add columns — fixed widths, Points sized for 7 monospace chars (~70px)
    RECT lvClient;
    GetClientRect(g_hwndListSkills, &lvClient);
    int pointsColW = 70;
    int nameColW = lvClient.right - pointsColW;

    LVCOLUMNA col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;

    col.pszText = (LPSTR) "Skill Name";
    col.cx = nameColW;
    col.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(g_hwndListSkills, 0, &col);

    col.pszText = (LPSTR) "Points";
    col.cx = pointsColW;
    col.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(g_hwndListSkills, 1, &col);

    // Lock columns — disable header drag resize
    HWND hHeader = ListView_GetHeader(g_hwndListSkills);
    if (hHeader) {
        LONG style = GetWindowLong(hHeader, GWL_STYLE);
        style |= HDS_NOSIZING;
        SetWindowLong(hHeader, GWL_STYLE, style);
    }

    // --- Section: Status (below list) ---
    y += listH + 6;
    g_hwndLblStatus = CreateLabel(hwnd, "Status: Ready for calibration", x, y, fullW, 20, ID_LBL_STATUS);
    SendMessage(g_hwndLblStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

    // --- Section: Log (hidden by default, shown in diagnostic mode) ---
    y += 24;
    g_hwndLblLog = CreateLabel(hwnd, "Debug Log:", x, y, 80, 20);
    SendMessage(g_hwndLblLog, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    ShowWindow(g_hwndLblLog, SW_HIDE);

    y += 22;
    g_hwndEditLog = CreateWindowExA(g_darkMode ? 0 : WS_EX_CLIENTEDGE, "EDIT", "",
                                    WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL |
                                        WS_HSCROLL | (g_darkMode ? WS_BORDER : 0),
                                    x, y, fullW, 150, hwnd, (HMENU)(INT_PTR)ID_EDIT_LOG, nullptr, nullptr);

    HFONT hFontMono =
        CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
    SendMessage(g_hwndEditLog, WM_SETFONT, (WPARAM)hFontMono, TRUE);

    // Raise text limit from default ~32KB to 2MB so log doesn't truncate
    SendMessage(g_hwndEditLog, EM_SETLIMITTEXT, 2000000, 0);

    // Save Debug button — below log, hidden by default
    y += 157;
    g_hwndBtnDebug = CreateButton(hwnd, "Save Debug", x, y, btnW, btnH, ID_BTN_DEBUG);
    SendMessage(g_hwndBtnDebug, WM_SETFONT, (WPARAM)hFont, TRUE);
    ShowWindow(g_hwndBtnDebug, SW_HIDE);

    // Apply dark mode to child controls
    if (g_darkMode) {
        // Combo box
        DarkMode_ApplyToChild(g_hwndComboWindows);
        SetWindowTheme(g_hwndComboWindows, L"DarkMode_CFD", nullptr);

        // Buttons — DarkMode_Explorer gives them dark themed rendering
        for (auto b : btns) {
            DarkMode_ApplyToChild(b);
            SetWindowTheme(b, L"DarkMode_Explorer", nullptr);
        }
        DarkMode_ApplyToChild(btnRefresh);
        SetWindowTheme(btnRefresh, L"DarkMode_Explorer", nullptr);
        // Guide toggle is a static, no theme needed

        // ListView dark colors + dark scrollbars/headers
        ListView_SetTextColor(g_hwndListSkills, DARK_TEXT);
        ListView_SetTextBkColor(g_hwndListSkills, DARK_EDIT_BG);
        ListView_SetBkColor(g_hwndListSkills, DARK_EDIT_BG);
        SetWindowTheme(g_hwndListSkills, L"DarkMode_Explorer", nullptr);

        // Subclass ListView to custom draw header with white text
        g_origListViewProc = (WNDPROC)SetWindowLongPtr(g_hwndListSkills, GWLP_WNDPROC, (LONG_PTR)ListViewSubclassProc);

        // Edit control dark scrollbars
        SetWindowTheme(g_hwndEditLog, L"DarkMode_Explorer", nullptr);

        // Debug button
        DarkMode_ApplyToChild(g_hwndBtnDebug);
        SetWindowTheme(g_hwndBtnDebug, L"DarkMode_Explorer", nullptr);
    }

    // Initial window list refresh
    RefreshWindowList();
}

// ============================================================================
// UI Updates
// ============================================================================

void RefreshWindowList() {
    SendMessage(g_hwndComboWindows, CB_RESETCONTENT, 0, 0);
    auto allWindows = Capture::EnumerateWindows();

    // Only show Entropia Universe windows
    g_windowList.clear();
    for (auto& w : allWindows) {
        if (w.second.find("Entropia Universe") != std::string::npos) {
            g_windowList.push_back(std::move(w));
        }
    }

    for (auto& [hwnd, title] : g_windowList) {
        std::string entry = title + " [" + std::to_string((uintptr_t)hwnd) + "]";
        SendMessageA(g_hwndComboWindows, CB_ADDSTRING, 0, (LPARAM)entry.c_str());
    }

    if (!g_windowList.empty()) {
        SendMessage(g_hwndComboWindows, CB_SETCURSEL, 0, 0);
        EnableWindow(g_hwndBtnCalibrate, TRUE);
        SetWindowTextA(g_hwndLblCalibStatus, "Needs Calibration");
    } else {
        EnableWindow(g_hwndBtnCalibrate, FALSE);
        SetWindowTextA(g_hwndLblCalibStatus, "Select Game Window Above First");
    }
}

void AdjustListViewColumns() {
    if (!g_hwndListSkills) return;
    RECT rc;
    GetClientRect(g_hwndListSkills, &rc);
    int avail = rc.right;
    // If vertical scrollbar is visible, client rect already excludes it
    // so no extra subtraction needed — just use client width directly
    int pointsColW = 70;
    int nameColW = avail - pointsColW;
    ListView_SetColumnWidth(g_hwndListSkills, 0, nameColW);
    ListView_SetColumnWidth(g_hwndListSkills, 1, pointsColW);
}

void UpdateSkillList() {
    if (!g_app) return;

    const auto& skills = g_app->GetSkills();

    // Clear and repopulate
    ListView_DeleteAllItems(g_hwndListSkills);

    for (int i = 0; i < (int)skills.size(); i++) {
        LVITEMA item = {};
        item.mask = LVIF_TEXT;
        item.iItem = i;

        item.pszText = (LPSTR)skills[i].name.c_str();
        ListView_InsertItem(g_hwndListSkills, &item);

        std::string pts = std::to_string(skills[i].points);
        ListView_SetItemText(g_hwndListSkills, i, 1, (LPSTR)pts.c_str());
    }

    // Scroll to bottom so user sees the latest skills
    if (!skills.empty()) {
        ListView_EnsureVisible(g_hwndListSkills, (int)skills.size() - 1, FALSE);
    }

    // Defer column adjustment — posting avoids reentrant issues during callbacks
    PostMessage(GetParent(g_hwndListSkills), WM_APP + 1, 0, 0);
    UpdateStats();
}

void UpdateStats() {
    if (!g_app) return;

    std::string stats = "Skills: " + std::to_string(g_app->GetTotalSkillsRead());
    stats += "  |  Points: " + std::to_string(g_app->GetTotalPoints());
    if (g_app->GetTotalPages() > 0) {
        stats += "  |  Page: " + std::to_string(g_app->GetCurrentPage()) + "/" + std::to_string(g_app->GetTotalPages());
    }
    SetWindowTextA(g_hwndLblStats, stats.c_str());
}

void AppendLog(const std::string& text) {
    if (!g_hwndEditLog) return;

    // Get current text length
    int len = GetWindowTextLengthA(g_hwndEditLog);

    // Append with newline
    std::string line = text + "\r\n";
    SendMessageA(g_hwndEditLog, EM_SETSEL, len, len);
    SendMessageA(g_hwndEditLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());

    // Scroll to bottom
    SendMessageA(g_hwndEditLog, EM_SCROLLCARET, 0, 0);
}

static void SetStatus(const char* text) {
    std::string s = "Status: ";
    s += text;
    SetWindowTextA(g_hwndLblStatus, s.c_str());
}

void CopySelectedSkillsToClipboard() {
    if (!g_app) return;
    const auto& skills = g_app->GetSkills();
    int selCount = ListView_GetSelectedCount(g_hwndListSkills);
    if (selCount == 0) return;

    std::string csv;
    // Include headers for multi-row selection
    if (selCount > 1) csv = "Skill Name,Points\r\n";

    int idx = -1;
    while ((idx = ListView_GetNextItem(g_hwndListSkills, idx, LVNI_SELECTED)) != -1) {
        if (idx < (int)skills.size()) {
            csv += skills[idx].name + "," + std::to_string(skills[idx].points) + "\r\n";
        }
    }

    if (csv.empty()) return;

    if (OpenClipboard(g_hwndMain)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, csv.size() + 1);
        if (hMem) {
            void* ptr = GlobalLock(hMem);
            if (ptr) {
                memcpy(ptr, csv.c_str(), csv.size() + 1);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
    }
}

void ToggleUserGuide() {
    g_guideVisible = !g_guideVisible;
    int delta = g_guideVisible ? GUIDE_TEXT_H : -GUIDE_TEXT_H;

    // Suppress painting during layout changes
    SendMessage(g_hwndMain, WM_SETREDRAW, FALSE, 0);

    ShowWindow(g_hwndLblGuideText, g_guideVisible ? SW_SHOW : SW_HIDE);

    // Shift all child windows below the guide toggle in one batch
    RECT toggleRc;
    GetWindowRect(g_hwndBtnGuideToggle, &toggleRc);
    MapWindowPoints(HWND_DESKTOP, g_hwndMain, (POINT*)&toggleRc, 2);
    int threshold = toggleRc.bottom;

    // Count children to move
    int count = 0;
    HWND child = GetWindow(g_hwndMain, GW_CHILD);
    while (child) {
        if (child != g_hwndBtnGuideToggle && child != g_hwndLblGuide && child != g_hwndLblGuideText) {
            RECT rc;
            GetWindowRect(child, &rc);
            MapWindowPoints(HWND_DESKTOP, g_hwndMain, (POINT*)&rc, 2);
            if (rc.top >= threshold) count++;
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }

    HDWP hdwp = BeginDeferWindowPos(count);
    if (hdwp) {
        child = GetWindow(g_hwndMain, GW_CHILD);
        while (child && hdwp) {
            if (child != g_hwndBtnGuideToggle && child != g_hwndLblGuide && child != g_hwndLblGuideText) {
                RECT rc;
                GetWindowRect(child, &rc);
                MapWindowPoints(HWND_DESKTOP, g_hwndMain, (POINT*)&rc, 2);
                if (rc.top >= threshold) {
                    hdwp = DeferWindowPos(hdwp, child, nullptr, rc.left, rc.top + delta, 0, 0,
                                          SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
            child = GetWindow(child, GW_HWNDNEXT);
        }
        if (hdwp) EndDeferWindowPos(hdwp);
    }

    // Resize main window
    RECT mainRc;
    GetWindowRect(g_hwndMain, &mainRc);
    int curH = mainRc.bottom - mainRc.top;
    SetWindowPos(g_hwndMain, nullptr, 0, 0, WINDOW_W, curH + delta, SWP_NOMOVE | SWP_NOZORDER);

    // Re-enable painting and redraw everything at once
    SendMessage(g_hwndMain, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_hwndMain, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void ToggleDiagnosticMode() {
    g_diagnosticMode = !g_diagnosticMode;
    int show = g_diagnosticMode ? SW_SHOW : SW_HIDE;
    ShowWindow(g_hwndLblLog, show);
    ShowWindow(g_hwndEditLog, show);
    ShowWindow(g_hwndBtnDebug, show);
    SetWindowTextA(g_hwndBtnShowLog, g_diagnosticMode ? "Hide Log" : "Show Log");

    // Resize window to show/hide log area, accounting for guide state
    int baseH = WINDOW_H_COMPACT + (g_guideVisible ? GUIDE_TEXT_H : 0);
    int newH = g_diagnosticMode ? baseH + LOG_AREA_H : baseH;
    SetWindowPos(g_hwndMain, nullptr, 0, 0, WINDOW_W, newH, SWP_NOZORDER | SWP_NOMOVE);
}

static std::string PickFolderDialog(HWND parent) {
    std::string result;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileDialog, (void**)&pfd))) {
        DWORD opts = 0;
        pfd->GetOptions(&opts);
        pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        pfd->SetTitle(L"Select Folder for Debug Files");
        pfd->SetOkButtonLabel(L"Select Folder");
        if (SUCCEEDED(pfd->Show(parent))) {
            IShellItem* psi = nullptr;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    char buf[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, path, -1, buf, MAX_PATH, nullptr, nullptr);
                    result = buf;
                    CoTaskMemFree(path);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
    CoUninitialize();
    return result;
}

static std::string SaveFileDialog(HWND parent) {
    char path[MAX_PATH] = "skills.csv";

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = "CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "csv";

    if (GetSaveFileNameA(&ofn)) {
        return path;
    }
    return "";
}

// ============================================================================
// Window Procedure
// ============================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_app = new App();
            g_app->SetLogCallback(AppendLog);
            g_app->SetSkillUpdateCallback(UpdateSkillList);
            g_app->SetKeyPromptCallback(ShowKeyDialog);

            CreateControls(hwnd);

            // Start a UI refresh timer
            SetTimer(hwnd, ID_TIMER_REFRESH, 500, nullptr);

            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);

            switch (id) {
                case ID_BTN_FIND_WINDOW:
                    RefreshWindowList();
                    break;

                case ID_COMBO_WINDOWS:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int sel = (int)SendMessage(g_hwndComboWindows, CB_GETCURSEL, 0, 0);
                        EnableWindow(g_hwndBtnCalibrate, sel >= 0);
                        SetWindowTextA(g_hwndLblCalibStatus, sel >= 0 ? "Needs Calibration" : "Select Game Window Above First");
                    }
                    break;

                case ID_BTN_CALIBRATE: {
                    // Get selected window
                    int sel = (int)SendMessage(g_hwndComboWindows, CB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < (int)g_windowList.size()) {
                        g_app->SetTargetWindow(g_windowList[sel].first);
                    }

                    SetStatus("Calibrating...");
                    SetCursor(LoadCursor(nullptr, IDC_WAIT));

                    bool ok = g_app->Calibrate();

                    SetCursor(LoadCursor(nullptr, IDC_ARROW));

                    if (ok && g_app->GetTotalSkillsRead() >= 12) {
                        EnableWindow(g_hwndBtnCalibrate, FALSE);
                        EnableWindow(g_hwndBtnCapture, TRUE);
                        EnableWindow(g_hwndBtnMonitor, TRUE);
                        EnableWindow(g_hwndBtnExport, TRUE);
                        SetWindowTextA(g_hwndLblCalibStatus, "Calibration Good");
                        SetStatus("Ready for next page");
                    } else if (ok) {
                        AppendLog("Calibration succeeded but only found " + std::to_string(g_app->GetTotalSkillsRead()) +
                                  " skills (need 12). Try again with the Skills window visible.");
                        SetWindowTextA(g_hwndLblCalibStatus, "Calibration Failed");
                        SetStatus("Error detected - Show Log for more details");
                    } else {
                        SetWindowTextA(g_hwndLblCalibStatus, "Calibration Failed");
                        SetStatus("Error detected - Show Log for more details");
                    }
                    break;
                }

                case ID_BTN_CAPTURE:
                    if (g_app->CaptureCurrentPage()) {
                        if (g_app->GetCurrentPage() > 0 && g_app->GetCurrentPage() >= g_app->GetTotalPages()) {
                            SetStatus("Last page detected");
                        } else {
                            SetStatus("Ready for next page");
                        }
                    } else {
                        SetStatus("Capture failed - Show Log for more details");
                        MessageBoxA(hwnd,
                                    "Page has not changed.\n\nScroll to the next page in the game's Skills window before "
                                    "clicking Capture Page.",
                                    "Page Not Changed", MB_OK | MB_ICONWARNING);
                    }
                    break;

                case ID_BTN_MONITOR: {
                    if (g_app->IsMonitoring()) {
                        g_app->StopMonitoring();
                        SetWindowTextA(g_hwndBtnMonitor, "Start Monitor");
                        EnableWindow(g_hwndBtnCapture, TRUE);
                        KillTimer(hwnd, ID_TIMER_POLL);
                        SetStatus("Monitoring stopped");
                    } else {
                        g_app->StartMonitoring();
                        SetWindowTextA(g_hwndBtnMonitor, "Stop Monitor");
                        EnableWindow(g_hwndBtnCapture, FALSE);
                        SetTimer(hwnd, ID_TIMER_POLL, 500, nullptr);
                        SetStatus("Monitoring for page changes...");
                    }
                    break;
                }

                case ID_BTN_EXPORT: {
                    auto path = SaveFileDialog(hwnd);
                    if (!path.empty()) {
                        if (g_app->ExportCSV(path)) {
                            MessageBoxA(
                                hwnd,
                                ("Exported " + std::to_string(g_app->GetTotalSkillsRead()) + " skills to:\n" + path).c_str(),
                                "Export Complete", MB_ICONINFORMATION);
                        }
                    }
                    break;
                }

                case ID_BTN_CLEAR:
                    if (MessageBoxA(hwnd, "Clear all captured skill data and log?", "Confirm", MB_YESNO | MB_ICONQUESTION) ==
                        IDYES) {
                        g_app->ClearSkills();
                        SetWindowTextA(g_hwndEditLog, "");
                        {
                            int sel = (int)SendMessage(g_hwndComboWindows, CB_GETCURSEL, 0, 0);
                            EnableWindow(g_hwndBtnCalibrate, sel >= 0);
                            SetWindowTextA(g_hwndLblCalibStatus,
                                           sel >= 0 ? "Needs Calibration" : "Select Game Window Above First");
                        }
                        EnableWindow(g_hwndBtnCapture, FALSE);
                        EnableWindow(g_hwndBtnMonitor, FALSE);
                        EnableWindow(g_hwndBtnExport, FALSE);
                        SetStatus("Ready for calibration");
                    }
                    break;

                case ID_BTN_SHOW_LOG:
                    ToggleDiagnosticMode();
                    break;

                case ID_BTN_GUIDE_TOGGLE:
                case ID_LBL_GUIDE:
                    ToggleUserGuide();
                    break;

                case ID_BTN_DEBUG: {
                    // Let user pick a folder for debug files
                    std::string folder = PickFolderDialog(hwnd);
                    if (folder.empty()) break;

                    std::string pngPath = folder + "\\eu_debug_capture.png";
                    std::string logPath = folder + "\\eu_debug_log.txt";

                    bool savedPng = g_app->SaveScreenshotPNG(pngPath);

                    if (savedPng) {
                        // Also log the layout info
                        auto& layout = g_app->GetLayout();
                        if (layout.valid) {
                            AppendLog("Layout: window=(" + std::to_string(layout.windowRect.left) + "," +
                                      std::to_string(layout.windowRect.top) + ")-(" + std::to_string(layout.windowRect.right) +
                                      "," + std::to_string(layout.windowRect.bottom) + ")");
                            AppendLog("  NAME x=" + std::to_string(layout.skillNameColX) + " RANK x=" +
                                      std::to_string(layout.rankColX) + " POINTS x=" + std::to_string(layout.pointsColX));
                            AppendLog("  Row0 y=" + std::to_string(layout.firstRowY) + " spacing=" +
                                      std::to_string(layout.rowHeight) + " rows=" + std::to_string(layout.maxRows));
                        }
                    }

                    // Save log text
                    int logLen = GetWindowTextLengthA(g_hwndEditLog);
                    bool savedLog = false;
                    if (logLen > 0) {
                        std::string logText(logLen + 1, '\0');
                        GetWindowTextA(g_hwndEditLog, &logText[0], logLen + 1);
                        logText.resize(logLen);
                        std::ofstream logFile(logPath);
                        if (logFile.is_open()) {
                            logFile << logText;
                            logFile.close();
                            savedLog = true;
                        }
                    }

                    // Report what was saved
                    if (savedPng || savedLog) {
                        std::string notice = "Debug files saved to:\n" + folder + "\n\n";
                        if (savedPng) notice += "  - eu_debug_capture.png\n";
                        if (savedLog) notice += "  - eu_debug_log.txt\n";
                        MessageBoxA(hwnd, notice.c_str(), "Debug", MB_ICONINFORMATION);
                    } else {
                        MessageBoxA(hwnd, "No screenshot captured yet. Run Calibrate first.", "Debug", MB_ICONWARNING);
                    }
                    break;
                }
            }
            return 0;
        }

        case WM_APP + 1:
            AdjustListViewColumns();
            return 0;

        case WM_TIMER: {
            if (wParam == ID_TIMER_POLL) {
                int prevSkills = g_app->GetTotalSkillsRead();
                g_app->PollForChanges();
                if (g_app->GetTotalSkillsRead() > prevSkills) {
                    // New skills were read
                    if (g_app->GetCurrentPage() > 0 && g_app->GetCurrentPage() >= g_app->GetTotalPages()) {
                        SetStatus("Last page detected");
                    } else {
                        SetStatus("Ready for next page");
                    }
                }
            } else if (wParam == ID_TIMER_REFRESH) {
                UpdateStats();
            }
            return 0;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlID == ID_BTN_GUIDE_TOGGLE) {
                FillRect(dis->hDC, &dis->rcItem, g_darkMode ? g_darkBgBrush : (HBRUSH)(COLOR_BTNFACE + 1));
                COLORREF triColor = g_darkMode ? DARK_TEXT : RGB(60, 60, 60);
                Gdiplus::Graphics gfx(dis->hDC);
                gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::SolidBrush brush(Gdiplus::Color(GetRValue(triColor), GetGValue(triColor), GetBValue(triColor)));
                float cx = (dis->rcItem.left + dis->rcItem.right) / 2.0f;
                float cy = (dis->rcItem.top + dis->rcItem.bottom) / 2.0f;
                Gdiplus::PointF pts[3];
                if (g_guideVisible) {
                    pts[0] = {cx - 5, cy - 3};
                    pts[1] = {cx + 5, cy - 3};
                    pts[2] = {cx, cy + 4};
                } else {
                    pts[0] = {cx - 3, cy - 5};
                    pts[1] = {cx - 3, cy + 5};
                    pts[2] = {cx + 4, cy};
                }
                gfx.FillPolygon(&brush, pts, 3);
                return TRUE;
            }
            break;
        }

        case WM_SETCURSOR: {
            // Hand cursor over the guide toggle
            if ((HWND)wParam == g_hwndBtnGuideToggle || (HWND)wParam == g_hwndLblGuide) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            if ((HWND)lParam == g_hwndSeparator) {
                return (LRESULT)(g_darkMode ? g_darkSepBrush : GetSysColorBrush(COLOR_BTNSHADOW));
            }
            if ((HWND)lParam == g_hwndLblGuide) {
                SetTextColor(hdc, g_darkMode ? RGB(100, 180, 255) : RGB(0, 102, 204));
                SetBkMode(hdc, TRANSPARENT);
                return (LRESULT)(g_darkMode ? g_darkBgBrush : GetStockObject(NULL_BRUSH));
            }
            if (!g_darkMode) break;
            SetTextColor(hdc, DARK_TEXT);
            SetBkColor(hdc, DARK_BG);
            return (LRESULT)g_darkBgBrush;
        }

        case WM_CTLCOLOREDIT: {
            if (!g_darkMode) break;
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, DARK_TEXT);
            SetBkColor(hdc, DARK_EDIT_BG);
            return (LRESULT)g_darkEditBrush;
        }

        case WM_CTLCOLORLISTBOX: {
            if (!g_darkMode) break;
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, DARK_TEXT);
            SetBkColor(hdc, DARK_EDIT_BG);
            return (LRESULT)g_darkEditBrush;
        }

        case WM_CTLCOLORBTN: {
            if (!g_darkMode) break;
            return (LRESULT)g_darkBgBrush;
        }

        case WM_DESTROY:
            KillTimer(hwnd, ID_TIMER_POLL);
            KillTimer(hwnd, ID_TIMER_REFRESH);
            delete g_app;
            g_app = nullptr;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
