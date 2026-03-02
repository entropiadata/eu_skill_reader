#pragma once
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// Core data types for EU Skill Reader
// ============================================================================

struct Pixel {
    uint8_t r, g, b;

    int brightness() const { return (std::max)({(int)r, (int)g, (int)b}); }

    bool isOrange() const { return r > 170 && g > 70 && g < 160 && b < 90; }

    bool isTeal() const { return r < 120 && g > 140 && b > 140; }

    bool isWhiteText() const { return r > 140 && g > 140 && b > 140 && brightness() > 160; }

    bool isBrightText() const { return brightness() > 120; }
};

struct Bitmap {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;  // RGBA, row-major, top-to-bottom

    Pixel pixel(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return {0, 0, 0};
        int idx = (y * width + x) * 4;
        return {data[idx + 2], data[idx + 1], data[idx + 0]};  // BGRA -> RGB
    }

    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        int idx = (y * width + x) * 4;
        data[idx + 0] = b;
        data[idx + 1] = g;
        data[idx + 2] = r;
        data[idx + 3] = a;
    }

    void create(int w, int h) {
        width = w;
        height = h;
        data.resize(w * h * 4, 0);
    }

    Bitmap subregion(int x, int y, int w, int h) const {
        Bitmap sub;
        sub.create(w, h);
        for (int sy = 0; sy < h; sy++) {
            for (int sx = 0; sx < w; sx++) {
                Pixel p = pixel(x + sx, y + sy);
                sub.setPixel(sx, sy, p.r, p.g, p.b);
            }
        }
        return sub;
    }
};

struct GlyphInfo {
    char ch;
    int width;
    int height;
    int advanceX;                // distance to next character origin
    int bearingX;                // offset from origin to left edge of glyph
    int bearingY;                // offset from baseline to top of glyph
    std::vector<uint8_t> alpha;  // grayscale alpha mask, row-major
};

struct FontAtlas {
    std::string fontName;
    std::string fontPath;
    int pointSize;
    int lineHeight;
    int baseline;
    int renderQuality;  // 0=NONANTIALIASED, 1=ANTIALIASED, 2=CLEARTYPE
    std::map<char, GlyphInfo> glyphs;
};

struct SkillEntry {
    std::string name;
    std::string rank;
    int points;
    std::string category;
};

struct SkillWindowLayout {
    // Window region within the game screenshot
    RECT windowRect;

    // Column positions (relative to windowRect.left)
    int skillNameColX;
    int skillNameColW;
    int rankColX;
    int rankColW;
    int pointsColX;
    int pointsColW;

    // Row layout
    int firstRowY;  // Y of first data row (relative to windowRect.top)
    int rowHeight;  // spacing between rows
    int maxRows;    // maximum visible rows

    // Page indicator
    int pageIndicatorY;

    // Category sidebar
    int categoryX;
    int categoryW;

    bool valid = false;
};

// Comparison for detecting page changes
struct RegionHash {
    uint64_t hash;
    bool operator==(const RegionHash& o) const { return hash == o.hash; }
    bool operator!=(const RegionHash& o) const { return hash != o.hash; }
};

// Application state
enum class AppState { Idle, SelectingWindow, Calibrating, Monitoring, Paused };

// Callback types
using LogCallback = std::function<void(const std::string&)>;

// ============================================================================
// Debug row data — packed binary + base64 for compact log output
// ============================================================================

struct DebugRowData {
    uint8_t row = 0;
    std::string name;
    int32_t points = 0;
    bool pointsValid = false;
    bool isOrange = false;
    uint8_t candidateCount = 0;
    float matchConfidence = 0.0f;
    uint16_t textPx = 0;
    uint16_t orangePx = 0;
    uint16_t featWidth = 0;
    uint16_t featHeight = 0;
    uint16_t featPixelCount = 0;
    uint8_t featSegments = 0;
    bool featHasDescender = false;
    bool featValid = false;
    std::string numberDiag;
};

inline std::string Base64Encode(const uint8_t* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)data[i + 2];
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? table[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? table[n & 0x3F] : '=';
    }
    return out;
}

inline std::string PackDebugRows(const std::vector<DebugRowData>& rows) {
    std::vector<uint8_t> buf;
    auto push8 = [&](uint8_t v) { buf.push_back(v); };
    auto push16 = [&](uint16_t v) {
        buf.push_back((uint8_t)(v & 0xFF));
        buf.push_back((uint8_t)(v >> 8));
    };
    auto push32 = [&](uint32_t v) {
        push16((uint16_t)(v & 0xFFFF));
        push16((uint16_t)(v >> 16));
    };
    auto pushF = [&](float v) {
        uint32_t u;
        memcpy(&u, &v, 4);
        push32(u);
    };

    push8(1);  // version
    push8((uint8_t)rows.size());

    for (const auto& r : rows) {
        push8(r.row);
        uint8_t flags = (r.isOrange ? 1 : 0) | (r.pointsValid ? 2 : 0) | (r.featHasDescender ? 4 : 0) | (r.featValid ? 8 : 0);
        push8(flags);
        uint32_t ptsU;
        memcpy(&ptsU, &r.points, 4);
        push32(ptsU);
        push8(r.candidateCount);
        pushF(r.matchConfidence);
        push16(r.textPx);
        push16(r.orangePx);
        push16(r.featWidth);
        push16(r.featHeight);
        push16(r.featPixelCount);
        push8(r.featSegments);

        uint8_t nameLen = (uint8_t)(std::min)(r.name.size(), (size_t)255);
        push8(nameLen);
        for (uint8_t i = 0; i < nameLen; i++) buf.push_back((uint8_t)r.name[i]);

        uint16_t diagLen = (uint16_t)(std::min)(r.numberDiag.size(), (size_t)65535);
        push16(diagLen);
        for (uint16_t i = 0; i < diagLen; i++) buf.push_back((uint8_t)r.numberDiag[i]);
    }

    return Base64Encode(buf.data(), buf.size());
}
