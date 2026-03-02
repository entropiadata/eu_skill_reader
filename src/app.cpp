#include "app.h"

#include <shlobj.h>
#include <wincrypt.h>

#include "skill_data.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb/stb_image_write.h"

// miniz: public domain ZIP/deflate library (amalgamated single-file build)
#pragma warning(push)
#pragma warning(disable : 4127)  // conditional expression is constant
#include "vendor/miniz/miniz.c"
#pragma warning(pop)

static const char* APP_CACHE_FOLDER = "eu_skill_reader";

App::App() {}

App::~App() { StopMonitoring(); }

void App::SetTargetWindow(HWND hwnd) {
    m_targetWindow = hwnd;
    m_layout.valid = false;
}

void App::Log(const std::string& msg) {
    m_status = msg;
    if (m_logCb) m_logCb(msg);
}

// Entropia Universe pak decryption: modified RC4 with MD5-derived key.
// Based on Luigi Auriemma's entropia_universe.c (public BMS script).
static void entropia_decrypt(const uint8_t* skey, int skeysz, const uint8_t* filex, int filexsz, int cycles, uint8_t* data,
                             int len) {
    // MD5(skey || filex) -> 16-byte RC4 key
    uint8_t key[16] = {};
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextA(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return;  // cannot decrypt without crypto provider
    if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptHashData(hHash, skey, skeysz, 0);
        CryptHashData(hHash, filex, filexsz, 0);
        DWORD hashLen = 16;
        CryptGetHashParam(hHash, HP_HASHVAL, key, &hashLen, 0);
        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);

    // RC4 key schedule
    uint8_t ctx[0x102];
    for (int i = 0; i < 0x100; i++) ctx[i] = (uint8_t)i;
    ctx[0x100] = 0;
    ctx[0x101] = 0;

    uint8_t c = 0;
    for (int i = 0; i < 0x100; i++) {
        uint8_t d = ctx[i];
        uint8_t b = key[i & 0xf];
        c += d + b;
        // XOR swap (must match game's implementation exactly)
        ctx[i] ^= ctx[c];
        ctx[c] ^= ctx[i];
        ctx[i] ^= ctx[c];
    }

    // RC4 round function
    auto rc4_round = [&]() -> uint8_t {
        uint8_t x = ctx[0x100];
        uint8_t y = ctx[0x101];
        x++;
        y += ctx[x];
        ctx[x] ^= ctx[y];
        ctx[y] ^= ctx[x];
        ctx[x] ^= ctx[y];
        uint8_t d = ctx[y] + ctx[x];
        ctx[0x100] = x;
        ctx[0x101] = y;
        return ctx[d];
    };

    // Extra cycles (game-specific modification to standard RC4)
    for (int i = 0; i < cycles; i++) rc4_round();

    // Decrypt
    for (int i = 0; i < len; i++) data[i] ^= rc4_round();
}

// SHA-256 of the correct 16-byte pak decryption key
static const uint8_t EXPECTED_KEY_SHA256[32] = {0xC3, 0x05, 0xBC, 0x81, 0x29, 0x03, 0xC6, 0xAF, 0xB3, 0xBC, 0xA9,
                                                0x00, 0x2D, 0x35, 0xA6, 0xBC, 0x9E, 0x7E, 0xBF, 0x8B, 0xBB, 0x99,
                                                0x31, 0xF5, 0x66, 0xDA, 0xD2, 0x17, 0x36, 0xF1, 0xB6, 0x2A};

// SHA-256 of the expected extracted font file (arial-unicode-bold.ttf, ~18 MB)
static const uint8_t EXPECTED_FONT_SHA256[32] = {0x13, 0x1F, 0xD0, 0x25, 0x06, 0x27, 0x5C, 0xD2, 0xAE, 0x56, 0xCA,
                                                 0x96, 0x6C, 0x6D, 0xE0, 0x86, 0xEB, 0x5B, 0x7B, 0x27, 0xE9, 0xE2,
                                                 0xD3, 0x8D, 0x4C, 0xA4, 0xD9, 0x55, 0x19, 0x3D, 0x45, 0x30};

// ============================================================================
// SHA-256 helpers (WinCrypt)
// ============================================================================

static bool ComputeSHA256(const uint8_t* data, size_t len, uint8_t out[32]) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextA(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return false;
    bool ok = false;
    if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        if (CryptHashData(hHash, data, (DWORD)len, 0)) {
            DWORD hashLen = 32;
            ok = CryptGetHashParam(hHash, HP_HASHVAL, out, &hashLen, 0) != 0;
        }
        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);
    return ok;
}

static bool ComputeFileSHA256(const char* path, uint8_t out[32]) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    bool ok = false;
    if (CryptAcquireContextA(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            uint8_t buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) CryptHashData(hHash, buf, (DWORD)n, 0);
            DWORD hashLen = 32;
            ok = CryptGetHashParam(hHash, HP_HASHVAL, out, &hashLen, 0) != 0;
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    fclose(f);
    return ok;
}

// ============================================================================
// Key parsing helpers
// ============================================================================

static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parse plain hex string: "B5A3BADB..." or "B5 A3 BA DB ..."
static bool ParseHexString(const char* s, std::vector<uint8_t>& out) {
    out.clear();
    while (*s) {
        if (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
            s++;
            continue;
        }
        int hi = HexVal(*s++);
        if (hi < 0 || !*s) return false;
        int lo = HexVal(*s++);
        if (lo < 0) return false;
        out.push_back((uint8_t)((hi << 4) | lo));
    }
    return !out.empty();
}

// Parse \x-escaped hex: "\xB5\xA3\xBA..."
static bool ParseEscapedHex(const char* s, std::vector<uint8_t>& out) {
    out.clear();
    while (*s) {
        if (*s == '\\' && *(s + 1) == 'x') {
            s += 2;
            int hi = HexVal(*s++);
            if (hi < 0 || !*s) return false;
            int lo = HexVal(*s++);
            if (lo < 0) return false;
            out.push_back((uint8_t)((hi << 4) | lo));
        } else if (*s == ' ' || *s == '\t' || *s == ',' || *s == '\r' || *s == '\n') {
            s++;
        } else {
            return false;  // unexpected character
        }
    }
    return !out.empty();
}

// Base64 decode
static bool Base64Decode(const char* s, std::vector<uint8_t>& out) {
    static const int T[128] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62,
                               -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,
                               1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
                               23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
                               39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};
    out.clear();
    int val = 0, bits = 0;
    while (*s) {
        char c = *s++;
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        if ((unsigned char)c >= 128 || T[(unsigned char)c] < 0) return false;
        val = (val << 6) | T[(unsigned char)c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((val >> bits) & 0xFF));
        }
    }
    return !out.empty();
}

// Auto-detect format and parse key string into 16 bytes
static bool ParseKeyString(const char* input, uint8_t out[16], std::string& error) {
    // Skip leading/trailing whitespace
    std::string s(input);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();

    if (s.empty()) {
        error = "Input is empty";
        return false;
    }

    // Strip QuickBMS prefix: 'set KEY binary "...' -> just the key part
    {
        auto pos = s.find("binary ");
        if (pos != std::string::npos) {
            s = s.substr(pos + 7);
            // Re-trim whitespace
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
        }
    }

    // Strip surrounding quotes (user may copy "..." from script source)
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }

    std::vector<uint8_t> bytes;

    // Try \x-escaped hex first (if it starts with \x)
    if (s.size() >= 2 && s[0] == '\\' && s[1] == 'x') {
        if (!ParseEscapedHex(s.c_str(), bytes)) {
            error = "Invalid \\x hex escape sequence";
            return false;
        }
    }
    // Try plain hex (if all chars are hex digits or spaces)
    else {
        bool allHex = true;
        for (char c : s) {
            if (c != ' ' && c != '\t' && HexVal(c) < 0) {
                allHex = false;
                break;
            }
        }
        if (allHex) {
            if (!ParseHexString(s.c_str(), bytes)) {
                error = "Invalid hex string";
                return false;
            }
        } else {
            // Try base64
            if (!Base64Decode(s.c_str(), bytes)) {
                error = "Could not parse as hex or base64";
                return false;
            }
        }
    }

    if (bytes.size() < 16) {
        error = "Key must be at least 16 bytes (got " + std::to_string(bytes.size()) + ")";
        return false;
    }

    // Accept longer keys (e.g. 56-byte QuickBMS key) — only the first 16 bytes are used
    memcpy(out, bytes.data(), 16);
    return true;
}

static bool ValidateKey(const uint8_t key[16]) {
    uint8_t hash[32];
    if (!ComputeSHA256(key, 16, hash)) return false;
    return memcmp(hash, EXPECTED_KEY_SHA256, 32) == 0;
}

// ============================================================================
// Registry helpers: HKCU\Software\EUSkillReader\PakKey (REG_SZ hex)
// ============================================================================

static const char* REG_KEY_PATH = "Software\\EUSkillReader";
static const char* REG_VALUE_NAME = "PakKey";

static bool ReadKeyFromRegistry(uint8_t key[16]) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_KEY_PATH, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    char buf[128] = {};
    DWORD size = sizeof(buf) - 1;
    DWORD type = 0;
    bool ok = false;
    if (RegQueryValueExA(hKey, REG_VALUE_NAME, nullptr, &type, (BYTE*)buf, &size) == ERROR_SUCCESS && type == REG_SZ) {
        std::vector<uint8_t> bytes;
        if (ParseHexString(buf, bytes) && bytes.size() == 16) {
            memcpy(key, bytes.data(), 16);
            ok = true;
        }
    }
    RegCloseKey(hKey);
    return ok;
}

static bool SaveKeyToRegistry(const uint8_t key[16]) {
    HKEY hKey;
    DWORD disp;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY_PATH, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, &disp) != ERROR_SUCCESS)
        return false;
    // Store as 32-char hex string
    char hex[33] = {};
    for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02X", key[i]);
    bool ok = RegSetValueExA(hKey, REG_VALUE_NAME, 0, REG_SZ, (const BYTE*)hex, 33) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return ok;
}

// Sanitize a path for display: replace user-specific prefixes with env var names
static std::string SanitizePath(const std::string& path) {
    std::string result = path;
    // Try replacements longest-first to avoid partial matches
    struct {
        const char* envVar;
        const char* display;
    } vars[] = {
        {"TEMP", "%TEMP%"},
        {"TMP", "%TMP%"},
        {"USERPROFILE", "%USERPROFILE%"},
        {"ALLUSERSPROFILE", "%ALLUSERSPROFILE%"},
        {"APPDATA", "%APPDATA%"},
        {"LOCALAPPDATA", "%LOCALAPPDATA%"},
    };
    for (auto& v : vars) {
        char val[MAX_PATH] = {};
        if (GetEnvironmentVariableA(v.envVar, val, MAX_PATH) > 0) {
            size_t len = strlen(val);
            // Remove trailing backslash for matching
            if (len > 0 && (val[len - 1] == '\\' || val[len - 1] == '/')) val[--len] = '\0';
            size_t pos = 0;
            while ((pos = result.find(val, pos)) != std::string::npos) {
                result.replace(pos, len, v.display);
                pos += strlen(v.display);
            }
        }
    }
    return result;
}

KeyParseResult App::ParseAndValidateKey(const char* input) {
    KeyParseResult r;
    if (!ParseKeyString(input, r.key, r.error)) return r;
    if (!ValidateKey(r.key)) {
        r.error = "Key format OK, but this is not the correct key";
        memset(r.key, 0, 16);
        return r;
    }
    r.valid = true;
    return r;
}

// Extract arial-unicode-bold.ttf from the game's pak file.
// The pak is ZIP-structured but uses custom encryption (method 2).
// Caller is responsible for cache checks; this always extracts.
// After writing, verifies the extracted font's SHA-256.
static std::string ExtractFontFromPak(const uint8_t key[16], std::function<void(const std::string&)> log) {
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    std::string cacheDir = std::string(tempDir) + APP_CACHE_FOLDER;
    std::string cachedFont = cacheDir + "\\arial-unicode-bold.ttf";

    // Check if pak file exists
    char allUsersDir[MAX_PATH];
    GetEnvironmentVariableA("ALLUSERSPROFILE", allUsersDir, MAX_PATH);
    std::string pakPath = std::string(allUsersDir) +
                          "\\entropia universe\\public_users_data"
                          "\\dynamic_content\\shared_platform_07.pak";
    DWORD attr = GetFileAttributesA(pakPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        if (log) log("Game pak not found: " + SanitizePath(pakPath));
        return "";
    }

    CreateDirectoryA(cacheDir.c_str(), nullptr);
    if (log) log("Extracting font from game pak file...");

    FILE* f = fopen(pakPath.c_str(), "rb");
    if (!f) {
        if (log) log("Failed to open pak file");
        return "";
    }

    const char* target = "shared/platform/fonts/arial-unicode-bold.ttf";
    std::string result;

    // Find the entry via the central directory (entries aren't stored sequentially)
    // Step 1: Find End of Central Directory record
    _fseeki64(f, 0, SEEK_END);
    int64_t fsize = _ftelli64(f);
    int64_t searchStart = (std::max)((int64_t)0, fsize - 65536);
    _fseeki64(f, searchStart, SEEK_SET);

    std::vector<uint8_t> tail((size_t)(fsize - searchStart));
    fread(tail.data(), 1, tail.size(), f);

    int64_t eocdOff = -1;
    for (int64_t i = (int64_t)tail.size() - 22; i >= 0; i--) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4B && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            eocdOff = searchStart + i;
            break;
        }
    }

    if (eocdOff < 0) {
        if (log) log("No EOCD record found in pak");
        fclose(f);
        return "";
    }

    // Step 2: Parse EOCD to find central directory
    _fseeki64(f, eocdOff + 4, SEEK_SET);
    uint16_t disk, cdDisk, cdEntriesDisk, cdEntries;
    uint32_t cdSize, cdOffset;
    if (fread(&disk, 2, 1, f) != 1 || fread(&cdDisk, 2, 1, f) != 1 || fread(&cdEntriesDisk, 2, 1, f) != 1 ||
        fread(&cdEntries, 2, 1, f) != 1 || fread(&cdSize, 4, 1, f) != 1 || fread(&cdOffset, 4, 1, f) != 1) {
        if (log) log("Truncated EOCD record in pak");
        fclose(f);
        return "";
    }

    // Step 3: Walk central directory to find the font entry's local offset
    _fseeki64(f, cdOffset, SEEK_SET);
    uint32_t localOffset = 0;
    uint32_t crc = 0, comp_size = 0, uncomp_size = 0;
    uint16_t method = 0;
    bool found = false;

    for (int i = 0; i < cdEntries; i++) {
        uint32_t sig;
        if (fread(&sig, 4, 1, f) != 1 || sig != 0x02014b50) break;

        uint16_t verMade, verNeed, flags, m, mtime, mdate;
        uint32_t c, cs, us;
        uint16_t nlen, elen, clen, diskStart;
        uint16_t iattr;
        uint32_t eattr, loff;

        if (fread(&verMade, 2, 1, f) != 1 || fread(&verNeed, 2, 1, f) != 1 || fread(&flags, 2, 1, f) != 1 ||
            fread(&m, 2, 1, f) != 1 || fread(&mtime, 2, 1, f) != 1 || fread(&mdate, 2, 1, f) != 1 || fread(&c, 4, 1, f) != 1 ||
            fread(&cs, 4, 1, f) != 1 || fread(&us, 4, 1, f) != 1 || fread(&nlen, 2, 1, f) != 1 || fread(&elen, 2, 1, f) != 1 ||
            fread(&clen, 2, 1, f) != 1 || fread(&diskStart, 2, 1, f) != 1 || fread(&iattr, 2, 1, f) != 1 ||
            fread(&eattr, 4, 1, f) != 1 || fread(&loff, 4, 1, f) != 1)
            break;  // truncated entry

        if (nlen > 1024) break;  // sanity check on filename length
        std::string name(nlen, '\0');
        if (fread(&name[0], 1, nlen, f) != nlen) break;
        _fseeki64(f, elen + clen, SEEK_CUR);

        if (name == target) {
            localOffset = loff;
            crc = c;
            comp_size = cs;
            uncomp_size = us;
            method = m;
            found = true;
            break;
        }
    }

    if (!found) {
        if (log) log("Font entry not found in pak central directory");
        fclose(f);
        return "";
    }

    // Sanity-check sizes before allocating (font is ~18 MB uncompressed)
    static const uint32_t MAX_PAK_ENTRY = 50 * 1024 * 1024;  // 50 MB
    if (comp_size > MAX_PAK_ENTRY || uncomp_size > MAX_PAK_ENTRY) {
        if (log)
            log("Unreasonable entry size in pak (comp=" + std::to_string(comp_size) + " uncomp=" + std::to_string(uncomp_size) +
                ")");
        fclose(f);
        return "";
    }

    // Step 4: Read local file header to get to the data
    _fseeki64(f, localOffset, SEEK_SET);
    uint32_t lsig;
    if (fread(&lsig, 4, 1, f) != 1 || lsig != 0x04034b50) {
        if (log) log("Bad local header at offset " + std::to_string(localOffset));
        fclose(f);
        return "";
    }

    // Skip local header fields (26 bytes) to get name_len and extra_len
    _fseeki64(f, 22, SEEK_CUR);  // skip to name_len
    uint16_t lNameLen, lExtraLen;
    if (fread(&lNameLen, 2, 1, f) != 1 || fread(&lExtraLen, 2, 1, f) != 1) {
        if (log) log("Truncated local header");
        fclose(f);
        return "";
    }
    _fseeki64(f, lNameLen + lExtraLen, SEEK_CUR);  // skip name + extra

    // Step 5: Read compressed data
    std::vector<uint8_t> comp(comp_size);
    if (fread(comp.data(), 1, comp_size, f) != comp_size) {
        if (log) log("Failed to read " + std::to_string(comp_size) + " bytes of compressed data");
        fclose(f);
        return "";
    }
    fclose(f);

    if (method == 2) {
        // Decrypt: modified RC4 with MD5(key || crc)
        uint8_t crc_bytes[4];
        memcpy(crc_bytes, &crc, 4);
        entropia_decrypt(key, 16, crc_bytes, 4, 0x300, comp.data(), (int)comp_size);

        // Inflate (raw deflate, no zlib header)
        std::vector<uint8_t> decomp(uncomp_size);
        size_t decomp_len = tinfl_decompress_mem_to_mem(decomp.data(), uncomp_size, comp.data(), comp_size, 0);

        if (decomp_len == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
            if (log) log("Deflate decompression failed");
            return "";
        }

        // Write to cache
        FILE* out = fopen(cachedFont.c_str(), "wb");
        if (!out) {
            if (log) log("Failed to write cached font");
            return "";
        }
        fwrite(decomp.data(), 1, decomp_len, out);
        fclose(out);

        if (log) log("Font extracted: " + std::to_string(decomp_len) + " bytes");

        // Verify extracted font integrity
        uint8_t fontHash[32];
        if (ComputeFileSHA256(cachedFont.c_str(), fontHash) && memcmp(fontHash, EXPECTED_FONT_SHA256, 32) != 0) {
            if (log) log("Extracted font hash mismatch — wrong key or corrupt pak");
            DeleteFileA(cachedFont.c_str());
            return "";
        }
        result = cachedFont;
    } else {
        if (log) log("Unexpected method " + std::to_string(method) + " for font entry");
    }

    return result;
}

bool App::LoadFonts() {
    // 1. Check cached font
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    std::string cachedFont = std::string(tempDir) + APP_CACHE_FOLDER + "\\arial-unicode-bold.ttf";
    std::string fontPath;

    {
        DWORD attr = GetFileAttributesA(cachedFont.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES) {
            uint8_t hash[32];
            if (ComputeFileSHA256(cachedFont.c_str(), hash) && memcmp(hash, EXPECTED_FONT_SHA256, 32) == 0) {
                fontPath = cachedFont;
                Log("Using cached font");
            } else {
                Log("Cached font hash mismatch, re-extracting...");
                DeleteFileA(cachedFont.c_str());
            }
        }
    }

    // 2. Need to extract — obtain a valid key
    if (fontPath.empty()) {
        uint8_t key[16] = {};
        bool haveKey = false;

        // 2a. Try registry
        if (ReadKeyFromRegistry(key) && ValidateKey(key)) {
            haveKey = true;
            Log("Using key from registry");
        }

        // 2b. Prompt user
        if (!haveKey) {
            if (!m_keyPromptCb) {
                Log("Error: No decryption key available and no prompt callback set");
                return false;
            }
            if (!m_keyPromptCb(key)) {
                Log("Key entry cancelled by user");
                return false;
            }
            // Callback already validated via ParseAndValidateKey, but double-check
            if (!ValidateKey(key)) {
                Log("Error: Provided key failed validation");
                return false;
            }
            SaveKeyToRegistry(key);
            Log("Key saved to registry");
        }

        // 3. Extract
        fontPath = ExtractFontFromPak(key, m_logCb);
        SecureZeroMemory(key, 16);
        if (fontPath.empty()) {
            Log("Error: Could not extract game font");
            return false;
        }
    }

    if (fontPath.empty()) {
        Log("Error: Could not obtain game font");
        return false;
    }

    // Load the single known-good atlas: 9pt, bold, NONANTIALIASED
    // Only load glyphs needed for skill names and numbers
    auto atlas =
        FontEngine::LoadFont(fontPath, 9, true, false, 0, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -/");
    if (atlas.glyphs.empty()) {
        Log("Error: Failed to load font atlas");
        return false;
    }

    Log("Font loaded: " + atlas.fontName + " 9pt, " + std::to_string(atlas.glyphs.size()) + " glyphs");

    m_nameFont = atlas;
    m_rankFont = atlas;
    m_numberFont = atlas;
    m_fontsLoaded = true;
    m_fontsCalibrated = true;
    return true;
}

bool App::Calibrate() {
    m_state = AppState::Calibrating;

    // Step 0: Load skill list if needed
    if (!SkillData::IsLoaded()) {
        // Get exe directory for external file lookup
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string exeDir(exePath);
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) exeDir = exeDir.substr(0, lastSlash + 1);

        std::string err = SkillData::Load(exeDir);
        if (!err.empty()) {
            Log("Error: " + err);
            m_state = AppState::Idle;
            return false;
        }
        Log("Skills loaded: " + std::to_string(SkillData::GetSkillList().size()) + " entries (" + SkillData::GetSource() + ")");
    }

    // Step 1: Load fonts if needed
    if (!m_fontsLoaded) {
        if (!LoadFonts()) {
            m_state = AppState::Idle;
            return false;
        }
    }

    // Step 2: Capture the game window
    if (m_targetWindow && IsWindow(m_targetWindow)) {
        if (IsIconic(m_targetWindow)) {
            ShowWindow(m_targetWindow, SW_RESTORE);
            Sleep(500);
        }
        m_lastCapture = Capture::CaptureWindow(m_targetWindow);
    } else {
        m_lastCapture = Capture::CaptureFullScreen();
    }

    if (m_lastCapture.width == 0) {
        Log("Error: Failed to capture screenshot");
        m_state = AppState::Idle;
        return false;
    }

    Log("Captured " + std::to_string(m_lastCapture.width) + "x" + std::to_string(m_lastCapture.height));

    // Step 3: Detect the Skills window
    std::string detectDiag;
    m_layout = SkillWindow::Detect(m_lastCapture, detectDiag);

    if (!m_layout.valid) {
        Log("Error: Skills window not found. " + detectDiag);
        m_state = AppState::Idle;
        return false;
    }

    int sw = m_layout.windowRect.right - m_layout.windowRect.left;
    int sh = m_layout.windowRect.bottom - m_layout.windowRect.top;
    Log("Skills window: " + std::to_string(sw) + "x" + std::to_string(sh) + " at (" + std::to_string(m_layout.windowRect.left) +
        "," + std::to_string(m_layout.windowRect.top) + ")" + " rows=" + std::to_string(m_layout.maxRows) +
        " rowH=" + std::to_string(m_layout.rowHeight));

    // Step 4: Diagnostic pre-read using ordered skill list
    int wl = m_layout.windowRect.left;
    int wt = m_layout.windowRect.top;
    {
        const auto& skillList = SkillData::GetSkillList();
        int nameW = m_layout.skillNameColW + 10;
        int rowH = m_layout.rowHeight;
        int listCursor = 0;
        TextReader::ReadConfig numCfg;
        int ptsTextH = rowH / 2 + 1;

        std::vector<DebugRowData> debugRows;
        for (int row = 0; row < m_layout.maxRows; row++) {
            int rowY = wt + m_layout.firstRowY + row * rowH;
            int readX = wl + m_layout.skillNameColX - 10;
            if (readX < wl) readX = wl;

            int textPx = 0, orangePx = 0;
            for (int dy = 0; dy < rowH; dy++)
                for (int dx = 0; dx < nameW; dx++) {
                    Pixel p = m_lastCapture.pixel(readX + dx, rowY + dy);
                    if (p.brightness() > 80 || p.isOrange()) textPx++;
                    if (p.isOrange()) orangePx++;
                }

            bool isOrange = orangePx > 10;
            auto features = TextReader::ExtractFeatures(m_lastCapture, readX, rowY, nameW, rowH, isOrange);
            auto candidateIndices = SkillData::GetNextCandidates(listCursor);

            std::string matchName;
            float matchConf = 0.0f;
            if (candidateIndices.size() == 1) {
                int idx = candidateIndices[0];
                matchName = skillList[idx].name;
                listCursor = idx + 1;
            } else {
                std::vector<std::string> candidateNames;
                for (int idx : candidateIndices) candidateNames.push_back(skillList[idx].name);

                auto match =
                    TextReader::MatchSkillName(m_lastCapture, readX, rowY, nameW, rowH, m_nameFont, candidateNames, isOrange);

                matchConf = match.confidence;
                if (match.valid) {
                    matchName = match.name;
                    for (int idx : candidateIndices) {
                        if (skillList[idx].name == match.name) {
                            listCursor = idx + 1;
                            break;
                        }
                    }
                } else {
                    int idx = candidateIndices.back();
                    matchName = skillList[idx].name;
                    listCursor = idx + 1;
                }
            }

            auto pts = TextReader::ReadNumber(m_lastCapture, wl + m_layout.pointsColX, rowY, m_layout.pointsColW, ptsTextH,
                                              m_numberFont, numCfg);

            Log("  R" + std::to_string(row) + ": " + matchName + " = " + std::to_string(pts.value) +
                (isOrange ? " [sel]" : "") +
                (candidateIndices.size() > 1 ? " (" + std::to_string(candidateIndices.size()) + " cand)" : ""));

            DebugRowData drd;
            drd.row = (uint8_t)row;
            drd.name = matchName;
            drd.points = pts.value;
            drd.pointsValid = pts.valid;
            drd.isOrange = isOrange;
            drd.candidateCount = (uint8_t)candidateIndices.size();
            drd.matchConfidence = matchConf;
            drd.textPx = (uint16_t)textPx;
            drd.orangePx = (uint16_t)orangePx;
            drd.featWidth = (uint16_t)features.width;
            drd.featHeight = (uint16_t)features.height;
            drd.featPixelCount = (uint16_t)features.pixelCount;
            drd.featSegments = (uint8_t)features.segments;
            drd.featHasDescender = features.hasDescender;
            drd.featValid = features.valid;
            drd.numberDiag = pts.diag;
            debugRows.push_back(drd);
        }
        Log("DBG|" + PackDebugRows(debugRows));

        // Generate debug PNGs in memory, then package into a ZIP
        std::vector<uint8_t> rowsPng, pointsPng;

        // Debug PNG: rows — raw on left, binarized on right
        {
            int debugRowCount = m_layout.maxRows + 1;
            int debugW = nameW * 2 + 10;
            int debugH = debugRowCount * rowH;

            std::vector<uint8_t> debugImg(debugW * debugH * 3, 0);

            for (int rowIdx = 0; rowIdx < debugRowCount; rowIdx++) {
                int r = rowIdx - 1;
                int rowY = wt + m_layout.firstRowY + r * rowH;
                int readX = wl + m_layout.skillNameColX - 10;
                if (readX < wl) readX = wl;
                if (rowY < 0) continue;

                auto bin = TextReader::BinarizeRegion(m_lastCapture, readX, rowY, nameW, rowH);

                for (int dy = 0; dy < rowH; dy++)
                    for (int dx = 0; dx < nameW; dx++) {
                        int srcPx = (rowY + dy) * m_lastCapture.width + (readX + dx);
                        if (srcPx < 0 || srcPx >= m_lastCapture.width * m_lastCapture.height) continue;

                        int dst = (rowIdx * rowH + dy) * debugW + dx;
                        if (dst * 3 + 2 >= (int)debugImg.size()) continue;
                        debugImg[dst * 3 + 0] = m_lastCapture.data[srcPx * 4 + 2];  // R
                        debugImg[dst * 3 + 1] = m_lastCapture.data[srcPx * 4 + 1];  // G
                        debugImg[dst * 3 + 2] = m_lastCapture.data[srcPx * 4 + 0];  // B

                        int dst2 = (rowIdx * rowH + dy) * debugW + (nameW + 10 + dx);
                        if (dst2 * 3 + 2 >= (int)debugImg.size()) continue;
                        uint8_t v = bin[dy * nameW + dx];
                        debugImg[dst2 * 3 + 0] = v;
                        debugImg[dst2 * 3 + 1] = v;
                        debugImg[dst2 * 3 + 2] = v;
                    }
            }

            int pngLen = 0;
            unsigned char* png = stbi_write_png_to_mem(debugImg.data(), debugW * 3, debugW, debugH, 3, &pngLen);
            if (png) {
                rowsPng.assign(png, png + pngLen);
                STBIW_FREE(png);
            }
        }

        // Debug PNG: POINTS column — raw on left, color-filtered on right
        {
            int ptsW = m_layout.pointsColW;
            int debugW = ptsW * 2 + 5;
            int debugH = m_layout.maxRows * ptsTextH;

            std::vector<uint8_t> debugImg(debugW * debugH * 3, 0);

            for (int row = 0; row < m_layout.maxRows; row++) {
                int rowY = wt + m_layout.firstRowY + row * rowH;
                int ptsX = wl + m_layout.pointsColX;

                std::vector<uint8_t> bin(ptsW * ptsTextH, 0);
                for (int dy = 0; dy < ptsTextH; dy++)
                    for (int dx = 0; dx < ptsW; dx++) {
                        Pixel p = m_lastCapture.pixel(ptsX + dx, rowY + dy);
                        int br = p.brightness();
                        bool keep = false;
                        if (p.isOrange() && br > 60)
                            keep = true;
                        else if (br > 100) {
                            int maxC = p.r;
                            if (p.g > maxC) maxC = p.g;
                            if (p.b > maxC) maxC = p.b;
                            int minC = p.r;
                            if (p.g < minC) minC = p.g;
                            if (p.b < minC) minC = p.b;
                            if (maxC - minC < TextReader::WHITE_TEXT_MAX_SATURATION) keep = true;
                        }
                        if (keep) bin[dy * ptsW + dx] = 255;
                    }

                int outY = row * ptsTextH;
                for (int dy = 0; dy < ptsTextH; dy++)
                    for (int dx = 0; dx < ptsW; dx++) {
                        Pixel p = m_lastCapture.pixel(ptsX + dx, rowY + dy);
                        int dst = (outY + dy) * debugW + dx;
                        if (dst * 3 + 2 < (int)debugImg.size()) {
                            debugImg[dst * 3 + 0] = p.r;
                            debugImg[dst * 3 + 1] = p.g;
                            debugImg[dst * 3 + 2] = p.b;
                        }

                        int dst2 = (outY + dy) * debugW + (ptsW + 5 + dx);
                        if (dst2 * 3 + 2 < (int)debugImg.size()) {
                            uint8_t v = bin[dy * ptsW + dx];
                            debugImg[dst2 * 3 + 0] = v;
                            debugImg[dst2 * 3 + 1] = v;
                            debugImg[dst2 * 3 + 2] = v;
                        }
                    }
            }

            int pngLen = 0;
            unsigned char* png = stbi_write_png_to_mem(debugImg.data(), debugW * 3, debugW, debugH, 3, &pngLen);
            if (png) {
                pointsPng.assign(png, png + pngLen);
                STBIW_FREE(png);
            }
        }

        // Package into a ZIP
        {
            char tmpDir[MAX_PATH];
            GetTempPathA(MAX_PATH, tmpDir);
            std::string zipPath = std::string(tmpDir) + "eu_debug.zip";

            mz_zip_archive zip = {};
            if (mz_zip_writer_init_file(&zip, zipPath.c_str(), 0)) {
                if (!rowsPng.empty())
                    mz_zip_writer_add_mem(&zip, "eu_debug_rows.png", rowsPng.data(), rowsPng.size(), MZ_BEST_COMPRESSION);
                if (!pointsPng.empty())
                    mz_zip_writer_add_mem(&zip, "eu_debug_points.png", pointsPng.data(), pointsPng.size(), MZ_BEST_COMPRESSION);
                mz_zip_writer_finalize_archive(&zip);
                mz_zip_writer_end(&zip);
                Log("  Debug saved: " + SanitizePath(zipPath));
            }
        }
    }

    m_skillListCursor = 0;  // start from beginning on calibration
    auto parseResult = SkillWindow::ParsePage(m_lastCapture, m_layout, m_nameFont, m_rankFont, m_numberFont, m_skillListCursor);

    if (parseResult.valid) {
        m_skillListCursor = parseResult.listCursorOut;
        AddSkills(parseResult.skills, m_currentCategory);
        m_currentPage = parseResult.currentPage;
        m_totalPages = parseResult.totalPages;

        // Store points signature so CaptureCurrentPage can detect unchanged pages
        // Pad to maxRows with zeros so size matches the quick-read (which always reads maxRows)
        m_lastPagePoints.clear();
        for (const auto& s : parseResult.skills) m_lastPagePoints.push_back(s.points);
        while ((int)m_lastPagePoints.size() < m_layout.maxRows) m_lastPagePoints.push_back(0);

        Log("Page " + std::to_string(m_currentPage) + "/" + std::to_string(m_totalPages) + ": " +
            std::to_string(parseResult.skills.size()) + " skills");
    } else {
        Log("Warning: No skills found. Adjust window position.");
    }

    m_state = AppState::Idle;
    Log("Calibration complete.");
    return true;
}

void App::StartMonitoring() {
    if (!m_layout.valid || !m_fontsCalibrated) {
        Log("Error: Must calibrate before monitoring");
        return;
    }

    m_monitoring = true;
    m_lastPagePoints.clear();
    m_state = AppState::Monitoring;
    Log("Monitoring started.");
}

void App::StopMonitoring() {
    m_monitoring = false;
    m_state = AppState::Idle;
    Log("Monitoring stopped.");
}

void App::PollForChanges() {
    if (!m_monitoring || !m_layout.valid) return;

    // Capture current state
    Bitmap current;
    if (m_targetWindow && IsWindow(m_targetWindow)) {
        if (IsIconic(m_targetWindow)) return;
        current = Capture::CaptureWindow(m_targetWindow);
    } else {
        current = Capture::CaptureFullScreen();
    }

    if (current.width == 0) return;

    // Quick-read just the POINTS column to detect page changes
    int wl = m_layout.windowRect.left;
    int wt = m_layout.windowRect.top;
    TextReader::ReadConfig numConfig;
    int ptsTextH = m_layout.rowHeight / 2 + 1;

    std::vector<int> currentPoints;
    for (int row = 0; row < m_layout.maxRows; row++) {
        int rowY = wt + m_layout.firstRowY + row * m_layout.rowHeight;
        auto pts = TextReader::ReadNumber(current, wl + m_layout.pointsColX, rowY, m_layout.pointsColW, ptsTextH, m_numberFont,
                                          numConfig);
        currentPoints.push_back(pts.valid ? pts.value : 0);
    }

    // Same page as last time? Skip.
    if (currentPoints == m_lastPagePoints) return;

    // New page detected — save the points signature and do the full read
    m_lastPagePoints = currentPoints;
    m_lastCapture = std::move(current);

    auto parseResult = SkillWindow::ParsePage(m_lastCapture, m_layout, m_nameFont, m_rankFont, m_numberFont, m_skillListCursor);

    if (parseResult.valid) {
        m_skillListCursor = parseResult.listCursorOut;
        AddSkills(parseResult.skills, m_currentCategory);
        m_currentPage = parseResult.currentPage;
        m_totalPages = parseResult.totalPages;

        Log("Page " + std::to_string(m_currentPage) + "/" + std::to_string(m_totalPages) + ": " +
            std::to_string(parseResult.skills.size()) + " skills (total " + std::to_string(m_allSkills.size()) + ")");
    }
}

bool App::CaptureCurrentPage() {
    if (!m_layout.valid || !m_fontsCalibrated) {
        Log("Error: Must calibrate first");
        return false;
    }

    // Capture
    if (m_targetWindow && IsWindow(m_targetWindow)) {
        if (IsIconic(m_targetWindow)) {
            ShowWindow(m_targetWindow, SW_RESTORE);
            Sleep(500);
        }
        m_lastCapture = Capture::CaptureWindow(m_targetWindow);
    } else {
        m_lastCapture = Capture::CaptureFullScreen();
    }

    if (m_lastCapture.width == 0) {
        Log("Error: Failed to capture");
        return false;
    }

    // Quick-read points column to detect if page actually changed
    if (!m_lastPagePoints.empty()) {
        int wl = m_layout.windowRect.left;
        int wt = m_layout.windowRect.top;
        TextReader::ReadConfig qCfg;
        int qPtsH = m_layout.rowHeight / 2 + 1;

        std::vector<int> currentPoints;
        for (int row = 0; row < m_layout.maxRows; row++) {
            int rowY = wt + m_layout.firstRowY + row * m_layout.rowHeight;
            auto pts = TextReader::ReadNumber(m_lastCapture, wl + m_layout.pointsColX, rowY, m_layout.pointsColW, qPtsH,
                                              m_numberFont, qCfg);
            currentPoints.push_back(pts.valid ? pts.value : 0);
        }

        if (currentPoints == m_lastPagePoints) {
            Log("Page has not changed - scroll to the next page in-game first");
            return false;
        }
    }

    // Diagnostic: compact per-row matching before parsing
    {
        const auto& skillList = SkillData::GetSkillList();
        int nameW = m_layout.skillNameColW + 10;
        int rowH = m_layout.rowHeight;
        int wl = m_layout.windowRect.left;
        int wt = m_layout.windowRect.top;
        int diagCursor = m_skillListCursor;
        TextReader::ReadConfig numCfg;
        int ptsTextH = rowH / 2 + 1;

        std::vector<DebugRowData> debugRows;
        for (int row = 0; row < m_layout.maxRows; row++) {
            int rowY = wt + m_layout.firstRowY + row * rowH;
            int readX = wl + m_layout.skillNameColX - 10;
            if (readX < wl) readX = wl;

            int textPx = 0, orangePx = 0;
            for (int dy = 0; dy < rowH; dy++)
                for (int dx = 0; dx < nameW; dx++) {
                    Pixel p = m_lastCapture.pixel(readX + dx, rowY + dy);
                    if (p.brightness() > 80 || p.isOrange()) textPx++;
                    if (p.isOrange()) orangePx++;
                }
            if (textPx < 5) continue;
            bool isOrange = orangePx > 10;

            auto features = TextReader::ExtractFeatures(m_lastCapture, readX, rowY, nameW, rowH, isOrange);
            auto candidateIndices = SkillData::GetNextCandidates(diagCursor);

            std::string matchName;
            float matchConf = 0.0f;
            if (candidateIndices.size() == 1) {
                int idx = candidateIndices[0];
                matchName = skillList[idx].name;
                diagCursor = idx + 1;
            } else {
                std::vector<std::string> candidateNames;
                for (int idx : candidateIndices) candidateNames.push_back(skillList[idx].name);

                auto match =
                    TextReader::MatchSkillName(m_lastCapture, readX, rowY, nameW, rowH, m_nameFont, candidateNames, isOrange);

                matchConf = match.confidence;
                if (match.valid) {
                    matchName = match.name;
                    for (int idx : candidateIndices) {
                        if (skillList[idx].name == match.name) {
                            diagCursor = idx + 1;
                            break;
                        }
                    }
                } else {
                    int idx = candidateIndices.back();
                    matchName = skillList[idx].name;
                    diagCursor = idx + 1;
                }
            }

            auto pts = TextReader::ReadNumber(m_lastCapture, wl + m_layout.pointsColX, rowY, m_layout.pointsColW, ptsTextH,
                                              m_numberFont, numCfg);

            Log("  R" + std::to_string(row) + ": " + matchName + " = " + std::to_string(pts.value) +
                (isOrange ? " [sel]" : "") +
                (candidateIndices.size() > 1 ? " (" + std::to_string(candidateIndices.size()) + " cand)" : ""));

            DebugRowData drd;
            drd.row = (uint8_t)row;
            drd.name = matchName;
            drd.points = pts.value;
            drd.pointsValid = pts.valid;
            drd.isOrange = isOrange;
            drd.candidateCount = (uint8_t)candidateIndices.size();
            drd.matchConfidence = matchConf;
            drd.textPx = (uint16_t)textPx;
            drd.orangePx = (uint16_t)orangePx;
            drd.featWidth = (uint16_t)features.width;
            drd.featHeight = (uint16_t)features.height;
            drd.featPixelCount = (uint16_t)features.pixelCount;
            drd.featSegments = (uint8_t)features.segments;
            drd.featHasDescender = features.hasDescender;
            drd.featValid = features.valid;
            drd.numberDiag = pts.diag;
            debugRows.push_back(drd);
        }
        Log("DBG|" + PackDebugRows(debugRows));
    }

    auto parseResult = SkillWindow::ParsePage(m_lastCapture, m_layout, m_nameFont, m_rankFont, m_numberFont, m_skillListCursor);

    if (parseResult.valid) {
        m_skillListCursor = parseResult.listCursorOut;
        AddSkills(parseResult.skills, m_currentCategory);
        m_currentPage = parseResult.currentPage;
        m_totalPages = parseResult.totalPages;

        // Update points signature for next duplicate check
        // Pad to maxRows with zeros so size matches the quick-read (which always reads maxRows)
        m_lastPagePoints.clear();
        for (const auto& s : parseResult.skills) m_lastPagePoints.push_back(s.points);
        while ((int)m_lastPagePoints.size() < m_layout.maxRows) m_lastPagePoints.push_back(0);

        Log("Page " + std::to_string(m_currentPage) + "/" + std::to_string(m_totalPages) + ": " +
            std::to_string(parseResult.skills.size()) + " skills");
        return true;
    }

    Log("Warning: No skills found on current page");
    return false;
}

void App::AddSkills(const std::vector<SkillEntry>& newSkills, const std::string& category) {
    for (const auto& skill : newSkills) {
        if (skill.name.empty()) continue;

        SkillEntry entry = skill;
        if (!category.empty()) entry.category = category;

        // Update or add to the map (deduplication by name)
        auto it = m_skillMap.find(entry.name);
        if (it != m_skillMap.end()) {
            // Update with newer data (higher points = more recent)
            if (entry.points >= it->second.points) {
                it->second = entry;
            }
        } else {
            m_skillMap[entry.name] = entry;
        }
    }

    // Rebuild the flat list from the map
    m_allSkills.clear();
    m_allSkills.reserve(m_skillMap.size());
    for (auto& [name, entry] : m_skillMap) {
        m_allSkills.push_back(entry);
    }

    // Sort by name
    std::sort(m_allSkills.begin(), m_allSkills.end(), [](const SkillEntry& a, const SkillEntry& b) { return a.name < b.name; });

    if (m_skillUpdateCb) m_skillUpdateCb();
}

void App::ClearSkills() {
    m_allSkills.clear();
    m_skillMap.clear();
    m_currentPage = 0;
    m_totalPages = 0;
    m_skillListCursor = 0;
    m_currentCategory.clear();
    m_lastPagePoints.clear();
    m_lastCapture = Bitmap();
    m_layout.valid = false;
    m_fontsCalibrated = false;
    m_monitoring = false;
    m_state = AppState::Idle;
    if (m_skillUpdateCb) m_skillUpdateCb();
    Log("Skill data cleared.");
}

bool App::ExportCSV(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "Skill Name,Points\n";
    for (const auto& skill : m_allSkills) {
        // CSV escape: wrap in quotes if contains comma
        auto csvField = [](const std::string& s) -> std::string {
            if (s.find(',') != std::string::npos || s.find('"') != std::string::npos) {
                std::string escaped = "\"";
                for (char c : s) {
                    if (c == '"')
                        escaped += "\"\"";
                    else
                        escaped += c;
                }
                escaped += "\"";
                return escaped;
            }
            return s;
        };

        f << csvField(skill.name) << "," << skill.points << "\n";
    }

    f.close();
    Log("Exported " + std::to_string(m_allSkills.size()) + " skills to " + SanitizePath(path));
    return true;
}

bool App::LoadScreenshot(const std::string& path) {
    // Load PNG/BMP using GDI+
    // For simplicity, support BMP natively, use GDI+ for PNG

    // Try loading as BMP first
    HBITMAP hBmp = (HBITMAP)LoadImageA(nullptr, path.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

    if (!hBmp) {
        Log("Error: Could not load image: " + SanitizePath(path));
        return false;
    }

    BITMAP bm;
    GetObject(hBmp, sizeof(bm), &bm);

    m_lastCapture.create(bm.bmWidth, bm.bmHeight);

    HDC hdc = CreateCompatibleDC(nullptr);
    HBITMAP hOld = (HBITMAP)SelectObject(hdc, hBmp);

    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(bi);
    bi.biWidth = bm.bmWidth;
    bi.biHeight = -bm.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    GetDIBits(hdc, hBmp, 0, bm.bmHeight, m_lastCapture.data.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(hdc, hOld);
    DeleteDC(hdc);
    DeleteObject(hBmp);

    Log("Loaded screenshot: " + std::to_string(bm.bmWidth) + "x" + std::to_string(bm.bmHeight));
    return true;
}

bool App::SaveScreenshot(const std::string& path) const {
    if (m_lastCapture.width == 0) return false;

    int w = m_lastCapture.width;
    int h = m_lastCapture.height;
    int stride = ((w * 3 + 3) / 4) * 4;  // BMP row alignment

    BITMAPFILEHEADER bf = {};
    bf.bfType = 0x4D42;  // "BM"
    bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bf.bfSize = bf.bfOffBits + stride * h;

    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = h;  // bottom-up for BMP file
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    f.write((char*)&bf, sizeof(bf));
    f.write((char*)&bi, sizeof(bi));

    // Write rows bottom-to-top
    std::vector<uint8_t> row(stride, 0);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            Pixel p = m_lastCapture.pixel(x, y);
            row[x * 3 + 0] = p.b;
            row[x * 3 + 1] = p.g;
            row[x * 3 + 2] = p.r;
        }
        f.write((char*)row.data(), stride);
    }

    f.close();
    return true;
}

bool App::SaveScreenshotPNG(const std::string& path) const {
    if (m_lastCapture.width == 0) return false;

    int w = m_lastCapture.width;
    int h = m_lastCapture.height;

    // Convert Pixel array to RGB byte array (stbi expects R,G,B order)
    std::vector<uint8_t> rgb(w * h * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            Pixel p = m_lastCapture.pixel(x, y);
            int idx = (y * w + x) * 3;
            rgb[idx + 0] = p.r;
            rgb[idx + 1] = p.g;
            rgb[idx + 2] = p.b;
        }
    }

    return stbi_write_png(path.c_str(), w, h, 3, rgb.data(), w * 3) != 0;
}
