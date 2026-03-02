#include "font_atlas.h"

namespace FontEngine {

FontAtlas LoadFont(const std::string& fontPath, int pointSize, bool bold, bool italic, int quality,
                   const std::string& charset) {
    FontAtlas atlas;
    atlas.fontPath = fontPath;
    atlas.pointSize = pointSize;
    atlas.renderQuality = quality;

    // Add the font resource temporarily
    int fontsAdded = AddFontResourceExA(fontPath.c_str(), FR_PRIVATE, 0);
    if (fontsAdded == 0) {
        // Try to get font name from path for system fonts
        // Fall through and try with the name anyway
    }

    // Extract font family name from the file
    // We'll use GDI's EnumFontFamilies or just try to create with common names
    // For now, load via AddFontResourceEx and enumerate

    HDC hdc = CreateCompatibleDC(nullptr);

    // Get DPI for point size conversion
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    int pixelHeight = -MulDiv(pointSize, dpi, 72);

    // Try to get the font face name from the file
    // We load it and enumerate to find the name
    std::string faceName;

    // Use GetFontResourceInfo if available, otherwise parse filename
    // Simple heuristic: extract name from filename
    size_t lastSlash = fontPath.find_last_of("\\/");
    size_t lastDot = fontPath.find_last_of('.');
    std::string filename = fontPath.substr(
        lastSlash != std::string::npos ? lastSlash + 1 : 0,
        lastDot != std::string::npos ? lastDot - (lastSlash != std::string::npos ? lastSlash + 1 : 0) : std::string::npos);

    // Map common EU font filenames to face names
    struct FontNameMap {
        const char* file;
        const char* face;
    };
    static const FontNameMap nameMap[] = {{"notosans-regular", "Noto Sans"},
                                          {"notosans-bold", "Noto Sans"},
                                          {"notosans-italic", "Noto Sans"},
                                          {"notosans-semibold", "Noto Sans SemiBold"},
                                          {"montserrat-regular", "Montserrat"},
                                          {"montserrat-bold", "Montserrat"},
                                          {"montserrat-light", "Montserrat Light"},
                                          {"montserrat-italic", "Montserrat"},
                                          {"arial-unicode-ms", "Arial Unicode MS"},
                                          {"arial-unicode-bold", "Arial Unicode MS"},
                                          {"dejavu", "DejaVu Sans"},
                                          {"dejavu_b", "DejaVu Sans"},
                                          {"dejavu_i", "DejaVu Sans"},
                                          {"dejavu_bi", "DejaVu Sans"},
                                          {"dejavu_mono", "DejaVu Sans Mono"},
                                          {"orbitron-regular", "Orbitron"},
                                          {"orbitron-bold", "Orbitron"},
                                          {"orbitron-medium", "Orbitron Medium"},
                                          {"orbitron-semibold", "Orbitron SemiBold"},
                                          {"orbitron-extrabold", "Orbitron ExtraBold"},
                                          {"orbitron-black", "Orbitron Black"},
                                          {"oswald-regular", "Oswald"},
                                          {"oswald-bold", "Oswald"},
                                          {"oswald-light", "Oswald Light"},
                                          {"quicksand-regular", "Quicksand"},
                                          {"quicksand-bold", "Quicksand"},
                                          {"quicksand-light", "Quicksand Light"},
                                          {"steelfib", "SteelFish"},
                                          {"vera", "Bitstream Vera Sans"},
                                          {"verabd", "Bitstream Vera Sans"},
                                          {"verabi", "Bitstream Vera Sans"},
                                          {"verait", "Bitstream Vera Sans"},
                                          {"veramono", "Bitstream Vera Sans Mono"},
                                          {nullptr, nullptr}};

    // Convert filename to lowercase for matching
    std::string lowerFile = filename;
    for (auto& c : lowerFile) c = (char)tolower((unsigned char)c);

    for (int i = 0; nameMap[i].file; i++) {
        if (lowerFile == nameMap[i].file) {
            faceName = nameMap[i].face;
            break;
        }
    }

    if (faceName.empty()) {
        faceName = filename;  // last resort
    }

    atlas.fontName = faceName;

    // Create the font
    int weight = bold ? FW_BOLD : FW_NORMAL;
    if (lowerFile.find("bold") != std::string::npos || lowerFile.find("_b") != std::string::npos) weight = FW_BOLD;
    if (lowerFile.find("semibold") != std::string::npos) weight = FW_SEMIBOLD;
    if (lowerFile.find("light") != std::string::npos) weight = FW_LIGHT;

    BOOL isItalic = italic || lowerFile.find("italic") != std::string::npos || lowerFile.find("_i") != std::string::npos;

    // Map quality parameter to GDI quality
    DWORD gdiQuality = ANTIALIASED_QUALITY;
    if (quality == 0)
        gdiQuality = NONANTIALIASED_QUALITY;
    else if (quality == 2)
        gdiQuality = CLEARTYPE_QUALITY;

    HFONT hFont = CreateFontA(pixelHeight,          // height
                              0,                    // width (auto)
                              0,                    // escapement
                              0,                    // orientation
                              weight,               // weight
                              isItalic,             // italic
                              FALSE,                // underline
                              FALSE,                // strikeout
                              DEFAULT_CHARSET,      // charset
                              OUT_TT_PRECIS,        // output precision
                              CLIP_DEFAULT_PRECIS,  // clip precision
                              gdiQuality,           // quality - match game rendering
                              DEFAULT_PITCH | FF_DONTCARE, faceName.c_str());

    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    // Get font metrics
    TEXTMETRICA tm;
    GetTextMetricsA(hdc, &tm);
    atlas.lineHeight = tm.tmHeight;
    atlas.baseline = tm.tmAscent;

    // Create a bitmap for rendering glyphs
    int bmpSize = tm.tmHeight * 2;  // generous size
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bmpSize;
    bmi.bmiHeader.biHeight = -bmpSize;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp || !bits) {
        OutputDebugStringA("EU Skill Reader: CreateDIBSection failed in LoadFont\n");
        DeleteObject(hFont);
        DeleteDC(hdc);
        return atlas;  // empty atlas
    }
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdc, hBmp);

    // Set up rendering
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkColor(hdc, RGB(0, 0, 0));

    // Build set of characters to render
    std::string chars;
    if (charset.empty()) {
        for (int ch = 32; ch < 127; ch++) chars += (char)ch;
    } else {
        chars = charset;
        if (charset.find(' ') == std::string::npos) chars += ' ';
    }

    // Render each character
    for (char c : chars) {
        int ch = (unsigned char)c;
        // Clear the bitmap
        memset(bits, 0, bmpSize * bmpSize * 4);

        // Get character width
        SIZE charSize;
        GetTextExtentPoint32A(hdc, &c, 1, &charSize);

        // Get ABC widths for precise positioning
        ABC abc;
        if (!GetCharABCWidthsA(hdc, ch, ch, &abc)) {
            abc.abcA = 0;
            abc.abcB = charSize.cx;
            abc.abcC = 0;
        }

        // Render at origin with some padding
        int renderX = (std::max)(0, -abc.abcA) + 2;
        int renderY = 2;
        TextOutA(hdc, renderX, renderY, &c, 1);
        GdiFlush();

        // Extract the glyph from the bitmap
        uint8_t* px = (uint8_t*)bits;

        // Find bounding box of non-zero pixels
        int minX = bmpSize, maxX = 0, minY = bmpSize, maxY = 0;
        for (int y = 0; y < (std::min)(bmpSize, (int)tm.tmHeight + 8); y++) {
            for (int x = 0; x < (std::min)(bmpSize, (int)charSize.cx + 8); x++) {
                int idx = (y * bmpSize + x) * 4;
                uint8_t val = px[idx + 2];  // R channel (we rendered white)
                if (val > 0) {
                    minX = (std::min)(minX, x);
                    maxX = (std::max)(maxX, x);
                    minY = (std::min)(minY, y);
                    maxY = (std::max)(maxY, y);
                }
            }
        }

        GlyphInfo glyph;
        glyph.ch = c;
        glyph.advanceX = abc.abcA + abc.abcB + abc.abcC;
        glyph.bearingX = abc.abcA;
        glyph.bearingY = renderY;  // relative to the render position

        if (ch == 32 || minX > maxX) {
            // Space or empty glyph
            glyph.width = 0;
            glyph.height = 0;
        } else {
            glyph.width = maxX - minX + 1;
            glyph.height = maxY - minY + 1;
            glyph.alpha.resize(glyph.width * glyph.height);

            for (int y = 0; y < glyph.height; y++) {
                for (int x = 0; x < glyph.width; x++) {
                    int srcIdx = ((minY + y) * bmpSize + (minX + x)) * 4;
                    // Use max of RGB as alpha (white text on black = grayscale)
                    uint8_t val = (std::max)({px[srcIdx], px[srcIdx + 1], px[srcIdx + 2]});
                    glyph.alpha[y * glyph.width + x] = val;
                }
            }

            // Adjust bearing relative to render origin
            glyph.bearingX = minX - renderX + abc.abcA;
            glyph.bearingY = minY - renderY;
        }

        atlas.glyphs[c] = std::move(glyph);
    }

    // Cleanup
    SelectObject(hdc, hOldBmp);
    SelectObject(hdc, hOldFont);
    DeleteObject(hBmp);
    DeleteObject(hFont);
    DeleteDC(hdc);

    RemoveFontResourceExA(fontPath.c_str(), FR_PRIVATE, 0);

    return atlas;
}

std::vector<uint8_t> RenderString(const FontAtlas& atlas, const std::string& text, int& outWidth, int& outHeight) {
    // Calculate total width
    int totalWidth = 0;
    for (char c : text) {
        auto it = atlas.glyphs.find(c);
        if (it != atlas.glyphs.end()) {
            totalWidth += it->second.advanceX;
        }
    }

    outWidth = totalWidth + 4;
    outHeight = atlas.lineHeight + 4;

    std::vector<uint8_t> result(outWidth * outHeight, 0);

    int curX = 2;
    for (char c : text) {
        auto it = atlas.glyphs.find(c);
        if (it == atlas.glyphs.end()) continue;

        const GlyphInfo& g = it->second;
        if (g.width == 0) {
            curX += g.advanceX;
            continue;
        }

        int drawX = curX + g.bearingX;
        int drawY = 2 + g.bearingY;

        for (int y = 0; y < g.height; y++) {
            for (int x = 0; x < g.width; x++) {
                int dx = drawX + x;
                int dy = drawY + y;
                if (dx >= 0 && dx < outWidth && dy >= 0 && dy < outHeight) {
                    uint8_t val = g.alpha[y * g.width + x];
                    // Composite (max blend)
                    uint8_t& dst = result[dy * outWidth + dx];
                    dst = (std::max)(dst, val);
                }
            }
        }

        curX += g.advanceX;
    }

    return result;
}

// IdentifyFont — currently unused
// FontMatch IdentifyFont(
//     const Bitmap& screenshot,
//     int textX, int textY, int textW, int textH,
//     const std::string& knownText,
//     const std::vector<std::string>& fontPaths,
//     bool bold
// ) {
//     FontMatch best;
//     best.fontIndex = -1;
//     best.pointSize = 0;
//     best.confidence = 0;
//
//     std::vector<uint8_t> target(textW * textH);
//     for (int y = 0; y < textH; y++) {
//         for (int x = 0; x < textW; x++) {
//             Pixel p = screenshot.pixel(textX + x, textY + y);
//             target[y * textW + x] = (uint8_t)p.brightness();
//         }
//     }
//
//     int histogram[256] = {};
//     for (uint8_t v : target) histogram[v]++;
//
//     int total = textW * textH;
//     float sum = 0;
//     for (int i = 0; i < 256; i++) sum += i * histogram[i];
//
//     float sumB = 0, wB = 0, wF = 0;
//     float maxVar = 0;
//     int threshold = 128;
//
//     for (int t = 0; t < 256; t++) {
//         wB += histogram[t];
//         if (wB == 0) continue;
//         wF = total - wB;
//         if (wF == 0) break;
//
//         sumB += t * histogram[t];
//         float mB = sumB / wB;
//         float mF = (sum - sumB) / wF;
//         float var = wB * wF * (mB - mF) * (mB - mF);
//         if (var > maxVar) {
//             maxVar = var;
//             threshold = t;
//         }
//     }
//
//     std::vector<uint8_t> targetBin(textW * textH);
//     for (int i = 0; i < (int)target.size(); i++) {
//         targetBin[i] = target[i] > threshold ? 255 : 0;
//     }
//
//     std::vector<int> sizes = {8, 9, 10, 11, 12, 13, 14, 15, 16, 18};
//
//     for (int fi = 0; fi < (int)fontPaths.size(); fi++) {
//         for (int size : sizes) {
//             FontAtlas atlas = LoadFont(fontPaths[fi], size, bold);
//
//             int rw, rh;
//             auto rendered = RenderString(atlas, knownText, rw, rh);
//
//             if (rw <= 0 || rh <= 0) continue;
//
//             std::vector<uint8_t> rendBin(rw * rh);
//             for (int i = 0; i < rw * rh; i++) {
//                 rendBin[i] = rendered[i] > 64 ? 255 : 0;
//             }
//
//             float bestScore = 0;
//
//             int searchW = textW - rw;
//             int searchH = textH - rh;
//             if (searchW < 0 || searchH < 0) continue;
//
//             for (int oy = 0; oy <= searchH; oy++) {
//                 for (int ox = 0; ox <= searchW; ox++) {
//                     float sumAB = 0, sumAA = 0, sumBB = 0;
//                     for (int y = 0; y < rh; y++) {
//                         for (int x = 0; x < rw; x++) {
//                             float a = targetBin[(oy + y) * textW + (ox + x)] / 255.0f;
//                             float b = rendBin[y * rw + x] / 255.0f;
//                             sumAB += a * b;
//                             sumAA += a * a;
//                             sumBB += b * b;
//                         }
//                     }
//
//                     float denom = sqrtf(sumAA * sumBB);
//                     float score = denom > 0 ? sumAB / denom : 0;
//                     bestScore = (std::max)(bestScore, score);
//                 }
//             }
//
//             if (bestScore > best.confidence) {
//                 best.fontIndex = fi;
//                 best.pointSize = size;
//                 best.confidence = bestScore;
//             }
//         }
//     }
//
//     return best;
// }

// GenerateCandidateAtlases — currently unused
// std::vector<FontAtlas> GenerateCandidateAtlases(
//     const std::vector<std::string>& fontPaths,
//     const std::vector<int>& sizes
// ) {
//     std::vector<FontAtlas> atlases;
//     for (const auto& path : fontPaths) {
//         for (int sz : sizes) {
//             atlases.push_back(LoadFont(path, sz));
//         }
//     }
//     return atlases;
// }

}  // namespace FontEngine
