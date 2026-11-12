#include "app/hud.h"

#include <windows.h>
#include <objidl.h>
#include <shlwapi.h>

#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>

#include "core/i18n.h"
#include "core/util.h"

namespace kayra {

namespace {

constexpr float kPi = 3.14159265358979f;

// icon font glyphs (Segoe Fluent Icons, same code points in Segoe MDL2 Assets)
enum : unsigned {
    I_HOME = 0xE80F, I_SLIDERS = 0xE9E9, I_ROBOT = 0xE99A, I_CHAT = 0xE8BD, I_GRID = 0xE8A9, I_PUZZLE = 0xEA86,
    I_HISTORY = 0xE81C, I_GEAR = 0xE713, I_MIC = 0xE720, I_VOLUME = 0xE995, I_BELL = 0xEA8F, I_SEND = 0xE724,
    I_ATTACH = 0xE723, I_EXPAND = 0xE740, I_COLLAPSE = 0xE73F, I_MORE = 0xE712, I_CPU = 0xE950, I_MEMORY = 0xE964,
    I_DISK = 0xEDA2, I_BATTERY = 0xE83F, I_PULSE = 0xE95E, I_CHART = 0xE9D9, I_GLOBE = 0xE774, I_THERMO = 0xE9CA,
    I_SUN = 0xE706, I_CLOCK = 0xE917, I_PLUS = 0xE710, I_MONITOR = 0xE7F4, I_FOLDER = 0xE8B7, I_BOLT = 0xE945,
    I_CHEVRON = 0xE70D, I_TERMINAL = 0xE756, I_WRENCH = 0xE90F,
};

ImU32 col(const ImVec4& c, float a = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, std::clamp(c.w * a, 0.0f, 1.0f)));
}
ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
ImVec2 polar(ImVec2 c, float r, float a) { return ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r); }

const ImVec4 kCyan(0.0f, 0.80f, 1.0f, 1.0f);
const ImVec4 kAmber(1.0f, 0.70f, 0.25f, 1.0f);
const ImVec4 kText(0.90f, 0.96f, 1.0f, 1.0f);
const ImVec4 kDim(0.52f, 0.66f, 0.76f, 1.0f);
const ImVec4 kRed(1.0f, 0.36f, 0.36f, 1.0f);
const ImVec4 kGreen(0.25f, 0.95f, 0.55f, 1.0f);
const ImVec4 kPanel(0.025f, 0.085f, 0.135f, 0.80f);

std::string utf8(unsigned cp) {
    char b[5] = {};
    if (cp < 0x80) {
        b[0] = (char)cp;
    } else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
    } else {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
    }
    return b;
}

float tw(ImFont* f, float size, const std::string& s) { return f->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x; }

// align 0 left, 0.5 center, 1 right; returns the width
float text(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 c, const std::string& s, float align = 0) {
    float w = tw(f, size, s);
    dl->AddText(f, size, ImVec2(std::floor(pos.x - w * align), std::floor(pos.y)), c, s.c_str());
    return w;
}

float spaced_width(ImFont* f, float size, const std::string& s, float spacing) {
    float w = 0;
    int n = 0;
    for (const char* p = s.c_str(); *p;) {
        unsigned int ch;
        int len = ImTextCharFromUtf8(&ch, p, nullptr);
        if (len <= 0) break;
        w += f->CalcTextSizeA(size, FLT_MAX, 0, p, p + len).x;
        p += len;
        n++;
    }
    return w + spacing * std::max(0, n - 1);
}

float spaced(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 c, const std::string& s, float spacing, float align = 0) {
    float w = spaced_width(f, size, s, spacing);
    float x = pos.x - w * align;
    for (const char* p = s.c_str(); *p;) {
        unsigned int ch;
        int len = ImTextCharFromUtf8(&ch, p, nullptr);
        if (len <= 0) break;
        dl->AddText(f, size, ImVec2(x, pos.y), c, p, p + len);
        x += f->CalcTextSizeA(size, FLT_MAX, 0, p, p + len).x + spacing;
        p += len;
    }
    return w;
}

// cuts s with "…" to fit max_w
std::string fit(ImFont* f, float size, std::string s, float max_w) {
    if (tw(f, size, s) <= max_w) return s;
    while (!s.empty() && tw(f, size, s + "…") > max_w) {
        while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
    }
    return s + "…";
}

void arc(ImDrawList* dl, ImVec2 c, float r, float a0, float a1, ImU32 color, float thick) {
    int seg = std::max(6, (int)(std::fabs(a1 - a0) * r / 5.0f));
    dl->PathArcTo(c, r, a0, a1, seg);
    dl->PathStroke(color, 0, thick);
}

void glow(ImDrawList* dl, unsigned int tex, ImVec2 c, float rx, float ry, ImU32 color) {
    if (!tex) return;
    dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(c.x - rx, c.y - ry), ImVec2(c.x + rx, c.y + ry), ImVec2(0, 0), ImVec2(1, 1), color);
}

void weather_icon(ImDrawList* dl, ImVec2 c, float s, int code, double t) {
    ImU32 sun = col(kAmber, 0.95f), cloud = col(ImVec4(0.80f, 0.90f, 1.0f, 1), 0.95f), rain = col(kCyan, 0.9f);
    bool clear = code <= 1, part = code == 2, snowy = (code >= 71 && code <= 77) || code == 85 || code == 86;
    bool wet = (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95;
    if (clear || part) {
        ImVec2 sc = part ? ImVec2(c.x - s * 0.2f, c.y - s * 0.18f) : c;
        float r = s * (part ? 0.26f : 0.34f);
        dl->AddCircleFilled(sc, r, sun, 32);
        for (int i = 0; i < 8; i++) {
            float a = i * kPi / 4 + (float)t * 0.3f;
            dl->AddLine(polar(sc, r * 1.3f, a), polar(sc, r * 1.65f, a), sun, s * 0.05f);
        }
        if (clear) return;
    }
    ImVec2 cc(c.x + s * 0.05f, c.y + s * 0.08f);
    dl->AddCircleFilled(ImVec2(cc.x - s * 0.22f, cc.y + s * 0.02f), s * 0.2f, cloud, 24);
    dl->AddCircleFilled(ImVec2(cc.x + s * 0.02f, cc.y - s * 0.1f), s * 0.27f, cloud, 24);
    dl->AddCircleFilled(ImVec2(cc.x + s * 0.27f, cc.y + s * 0.04f), s * 0.18f, cloud, 24);
    dl->AddRectFilled(ImVec2(cc.x - s * 0.22f, cc.y + s * 0.02f), ImVec2(cc.x + s * 0.27f, cc.y + s * 0.22f), cloud, s * 0.08f);
    if (wet || snowy) {
        for (int i = 0; i < 4; i++) {
            float x = cc.x - s * 0.24f + i * s * 0.16f;
            float ph = std::fmod((float)t * 1.6f + i * 0.37f, 1.0f);
            float y = cc.y + s * 0.3f + ph * s * 0.3f;
            if (snowy) dl->AddCircleFilled(ImVec2(x, y), s * 0.035f, cloud);
            else dl->AddLine(ImVec2(x, y), ImVec2(x - s * 0.05f, y + s * 0.12f), rain, s * 0.04f);
        }
    }
}

void drop_icon(ImDrawList* dl, ImVec2 c, float s, ImU32 color) {
    dl->AddCircleFilled(ImVec2(c.x, c.y + s * 0.18f), s * 0.3f, color, 16);
    dl->AddTriangleFilled(ImVec2(c.x, c.y - s * 0.45f), ImVec2(c.x - s * 0.27f, c.y + s * 0.08f), ImVec2(c.x + s * 0.27f, c.y + s * 0.08f), color);
}

void wind_icon(ImDrawList* dl, ImVec2 c, float s, ImU32 color, float th) {
    for (int i = 0; i < 3; i++) {
        float y = c.y - s * 0.3f + i * s * 0.3f, len = s * (i == 1 ? 0.8f : 0.55f);
        dl->AddLine(ImVec2(c.x - s * 0.45f, y), ImVec2(c.x - s * 0.45f + len, y), color, th);
        arc(dl, ImVec2(c.x - s * 0.45f + len, y - s * 0.1f), s * 0.1f, kPi * 0.5f, kPi * 2.0f, color, th);
    }
}

std::string mmss(double secs) {
    long long s = (long long)std::ceil(std::max(0.0, secs));
    char b[32];
    if (s >= 3600) snprintf(b, sizeof b, "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else snprintf(b, sizeof b, "%02lld:%02lld", s / 60, s % 60);
    return b;
}

std::string pct(double v) {
    char b[16];
    snprintf(b, sizeof b, is_turkish() ? "%%%.0f" : "%.0f%%", v);
    return b;
}

std::string gb(double v) {
    char b[32];
    if (v >= 1000) snprintf(b, sizeof b, "%.0f TB", v / 1000.0);
    else snprintf(b, sizeof b, "%.0f GB", v);
    return b;
}

std::string uptime_text(uint64_t up) {
    char b[64];
    unsigned long long d = up / 86400, h = (up / 3600) % 24, m = (up / 60) % 60;
    bool tr = is_turkish();
    if (d > 0) snprintf(b, sizeof b, tr ? "%llu gün %llu sa" : "%llu d %llu h", d, h);
    else snprintf(b, sizeof b, tr ? "%llu sa %llu dk" : "%llu h %llu m", h, m);
    return b;
}

// 60 samples in 0..1 (older values padded with the first one)
std::vector<float> series(const std::vector<float>& v) {
    std::vector<float> out(60, v.empty() ? 0.0f : v.front());
    bool percent = false;
    for (float x : v) percent = percent || x > 1.5f;
    for (size_t i = 0; i < v.size() && i < 60; i++) out[60 - std::min<size_t>(v.size(), 60) + i] = v[v.size() - std::min<size_t>(v.size(), 60) + i];
    for (auto& x : out) x = std::clamp(percent ? x / 100.0f : x, 0.0f, 1.0f);
    return out;
}

void area_chart(ImDrawList* dl, ImVec4 r, const std::vector<float>& v, const ImVec4& c, float k, double t) {
    float x0 = r.x, y0 = r.y, w = r.z, h = r.w;
    for (int i = 1; i < 4; i++) {
        float y = y0 + h * i / 4.0f;
        for (float x = x0; x < x0 + w; x += 6 * k) dl->AddLine(ImVec2(x, y), ImVec2(x + 2 * k, y), col(c, 0.10f), 1);
    }
    if (v.size() < 2) return;
    std::vector<ImVec2> pts;
    for (size_t i = 0; i < v.size(); i++) pts.push_back(ImVec2(x0 + w * i / (float)(v.size() - 1), y0 + h - h * (0.06f + 0.88f * v[i])));
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        ImVec2 a = pts[i], b = pts[i + 1];
        dl->AddRectFilledMultiColor(ImVec2(a.x, std::min(a.y, b.y)), ImVec2(b.x + 0.5f, y0 + h), col(c, 0.20f), col(c, 0.20f),
                                    col(c, 0.0f), col(c, 0.0f));
    }
    dl->AddPolyline(pts.data(), (int)pts.size(), col(c, 0.18f), 0, 5.0f * k);
    dl->AddPolyline(pts.data(), (int)pts.size(), col(c, 0.95f), 0, 1.6f * k);
    for (size_t i = 3; i < pts.size(); i += 7) dl->AddCircleFilled(pts[i], 2.0f * k, col(c, 0.9f));
    ImVec2 last = pts.back();
    float p = 0.5f + 0.5f * std::sin((float)t * 4);
    dl->AddCircleFilled(last, (3.0f + 2.0f * p) * k, col(c, 0.25f));
    dl->AddCircleFilled(last, 2.6f * k, col(ImVec4(1, 1, 1, 1)));
}

unsigned int load_png_resource(const wchar_t* name, int size) {
    HRSRC res = FindResourceW(nullptr, name, MAKEINTRESOURCEW(10));  // RT_RCDATA
    if (!res) return 0;
    HGLOBAL h = LoadResource(nullptr, res);
    DWORD n = SizeofResource(nullptr, res);
    const void* data = h ? LockResource(h) : nullptr;
    if (!data || !n) return 0;
    static ULONG_PTR token = [] {
        ULONG_PTR t = 0;
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&t, &in, nullptr);
        return t;
    }();
    (void)token;
    IStream* st = SHCreateMemStream((const BYTE*)data, n);
    if (!st) return 0;
    unsigned int tex = 0;
    {
        Gdiplus::Bitmap src(st);
        if (src.GetLastStatus() == Gdiplus::Ok) {
            // resample once with a good filter; the GL texture has no mipmaps
            Gdiplus::Bitmap bmp(size, size, PixelFormat32bppARGB);
            {
                Gdiplus::Graphics g(&bmp);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                g.Clear(Gdiplus::Color(0, 0, 0, 0));
                g.DrawImage(&src, 0, 0, size, size);
            }
            Gdiplus::Rect rc(0, 0, size, size);
            Gdiplus::BitmapData bd;
            if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
                std::vector<unsigned char> px((size_t)size * size * 4);
                for (int y = 0; y < size; y++) {
                    const unsigned char* row = (const unsigned char*)bd.Scan0 + (ptrdiff_t)y * bd.Stride;
                    for (int x = 0; x < size; x++) {
                        unsigned char* p = &px[((size_t)y * size + x) * 4];
                        p[0] = row[x * 4 + 2];
                        p[1] = row[x * 4 + 1];
                        p[2] = row[x * 4 + 0];
                        p[3] = row[x * 4 + 3];
                    }
                }
                bmp.UnlockBits(&bd);
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);  // GL_CLAMP_TO_EDGE
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            }
        }
    }
    st->Release();
    return tex;
}

struct P3 {
    float x, y, z;
};
const std::vector<P3>& sphere_points() {
    static const std::vector<P3> pts = [] {
        std::vector<P3> v;
        const int n = 2200;
        const float golden = kPi * (3.0f - std::sqrt(5.0f));
        for (int i = 0; i < n; i++) {
            float y = 1.0f - 2.0f * (i + 0.5f) / n;
            float rr = std::sqrt(std::max(0.0f, 1.0f - y * y));
            float th = golden * i;
            v.push_back({std::cos(th) * rr, y, std::sin(th) * rr});
        }
        return v;
    }();
    return pts;
}

}  // namespace

ImVec4 look_color(Look l) {
    switch (l) {
        case Look::Thinking: return kAmber;
        case Look::Hearing: return ImVec4(0.40f, 0.95f, 1.0f, 1.0f);
        case Look::Listening: return ImVec4(0.2f, 0.9f, 1.0f, 1.0f);
        case Look::Transcribing: return ImVec4(0.3f, 1.0f, 0.85f, 1.0f);
        case Look::Speaking: return ImVec4(0.1f, 0.85f, 1.0f, 1.0f);
        case Look::Offline: return ImVec4(0.45f, 0.58f, 0.68f, 1.0f);
        case Look::Error: return kRed;
        default: return kCyan;
    }
}

void apply_style(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 10;
    st.FrameRounding = 6;
    st.GrabRounding = 6;
    st.ChildRounding = 6;
    st.PopupRounding = 8;
    st.TabRounding = 6;
    st.ScrollbarRounding = 6;
    st.WindowBorderSize = 1;
    st.FrameBorderSize = 1;
    st.PopupBorderSize = 1;
    st.ScrollbarSize = 8;
    st.ItemSpacing = ImVec2(10, 8);
    st.FramePadding = ImVec2(10, 6);
    st.WindowPadding = ImVec2(18, 16);
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.075f, 0.12f, 0.98f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.03f, 0.09f, 0.14f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.0f, 0.80f, 1.0f, 0.30f);
    c[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.13f, 0.20f, 0.85f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.03f, 0.20f, 0.30f, 0.9f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.04f, 0.26f, 0.38f, 0.95f);
    c[ImGuiCol_TitleBg] = ImVec4(0.02f, 0.07f, 0.11f, 1);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.02f, 0.12f, 0.19f, 1);
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kDim;
    c[ImGuiCol_Button] = ImVec4(0.0f, 0.45f, 0.62f, 0.40f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.6f, 0.8f, 0.65f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.75f, 0.95f, 0.85f);
    c[ImGuiCol_Header] = ImVec4(0.0f, 0.5f, 0.7f, 0.35f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.6f, 0.8f, 0.55f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.7f, 0.9f, 0.75f);
    c[ImGuiCol_Tab] = ImVec4(0.0f, 0.3f, 0.45f, 0.45f);
    c[ImGuiCol_TabHovered] = ImVec4(0.0f, 0.6f, 0.8f, 0.8f);
    c[ImGuiCol_TabSelected] = ImVec4(0.0f, 0.5f, 0.7f, 0.9f);
    c[ImGuiCol_CheckMark] = kCyan;
    c[ImGuiCol_SliderGrab] = kCyan;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.6f, 0.95f, 1, 1);
    c[ImGuiCol_Separator] = ImVec4(0.0f, 0.80f, 1.0f, 0.18f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.0f, 0.65f, 0.85f, 0.45f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.0f, 0.75f, 0.95f, 0.6f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.6f, 0.9f, 0.35f);
    st.ScaleAllSizes(scale);
}

void Hud::init(float scale) {
    scale_ = scale;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x0300, 0x036F, 0x2000, 0x206F,
                                     0x20A0, 0x20CF, 0x2100, 0x214F, 0x2190, 0x21FF, 0x2200, 0x22FF,
                                     0x2500, 0x25FF, 0x2600, 0x26FF, 0};
    std::string fonts = std::string(getenv("WINDIR") ? getenv("WINDIR") : "C:\\Windows") + "\\Fonts\\";
    auto load = [&](const char* file, float size, const char* fallback) -> ImFont* {
        ImFontConfig fc;
        fc.OversampleH = 2;
        fc.OversampleV = 2;
        std::string p = fonts + file;
        if (!file_exists(p) && fallback) p = fonts + fallback;
        ImFont* f = nullptr;
        if (file_exists(p)) f = io.Fonts->AddFontFromFileTTF(p.c_str(), size * scale, &fc, ranges);
        if (!f) {
            ImFontConfig d;
            d.SizePixels = size * scale;
            f = io.Fonts->AddFontDefault(&d);
        }
        std::string sym = fonts + "seguisym.ttf";
        if (file_exists(sym)) {
            ImFontConfig m;
            m.MergeMode = true;
            static const ImWchar sr[] = {0x2190, 0x21FF, 0x2300, 0x23FF, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0};
            io.Fonts->AddFontFromFileTTF(sym.c_str(), size * scale, &m, sr);
        }
        return f;
    };
    f_body = load("segoeui.ttf", 16, nullptr);
    f_small = load("segoeui.ttf", 13, nullptr);
    f_semi = load("seguisb.ttf", 16, "segoeui.ttf");
    f_head = load("bahnschrift.ttf", 16, "seguisb.ttf");
    f_big = load("bahnschrift.ttf", 34, "seguisb.ttf");
    f_title = load("bahnschrift.ttf", 30, "seguibl.ttf");
    f_mono = load("consola.ttf", 14, nullptr);
    {
        static const ImWchar icons[] = {
            I_HOME, I_HOME, I_SLIDERS, I_SLIDERS, I_ROBOT, I_ROBOT, I_CHAT, I_CHAT, I_GRID, I_GRID, I_PUZZLE, I_PUZZLE,
            I_HISTORY, I_HISTORY, I_GEAR, I_GEAR, I_MIC, I_MIC, I_VOLUME, I_VOLUME, I_BELL, I_BELL, I_SEND, I_SEND,
            I_ATTACH, I_ATTACH, I_EXPAND, I_EXPAND, I_COLLAPSE, I_COLLAPSE, I_MORE, I_MORE, I_CPU, I_CPU, I_MEMORY, I_MEMORY,
            I_DISK, I_DISK, I_BATTERY, I_BATTERY, I_PULSE, I_PULSE, I_CHART, I_CHART, I_GLOBE, I_GLOBE, I_THERMO, I_THERMO,
            I_SUN, I_SUN, I_CLOCK, I_CLOCK, I_PLUS, I_PLUS, I_MONITOR, I_MONITOR, I_FOLDER, I_FOLDER, I_BOLT, I_BOLT,
            I_CHEVRON, I_CHEVRON, I_TERMINAL, I_TERMINAL, I_WRENCH, I_WRENCH, 0};
        std::string p = fonts + "SegoeIcons.ttf";
        if (!file_exists(p)) p = fonts + "segmdl2.ttf";
        ImFontConfig fc;
        fc.OversampleH = 2;
        fc.OversampleV = 2;
        f_icon = file_exists(p) ? io.Fonts->AddFontFromFileTTF(p.c_str(), 20 * scale, &fc, icons) : f_body;
        if (!f_icon) f_icon = f_body;
    }
    io.Fonts->Build();

    const int N = 128;
    std::vector<unsigned char> px(N * N * 4);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float dx = (x + 0.5f - N / 2.0f) / (N / 2.0f), dy = (y + 0.5f - N / 2.0f) / (N / 2.0f);
            float d = std::sqrt(dx * dx + dy * dy);
            float a = std::pow(std::clamp(1.0f - d, 0.0f, 1.0f), 2.2f);
            unsigned char* p = &px[(y * N + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = (unsigned char)(a * 255);
        }
    if (!glow_tex_) glGenTextures(1, &glow_tex_);
    glBindTexture(GL_TEXTURE_2D, glow_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    if (k_tex_) glDeleteTextures(1, &k_tex_);
    if (logo_tex_) glDeleteTextures(1, &logo_tex_);
    k_tex_ = load_png_resource(L"KAYRA_K", std::clamp((int)(150 * scale), 64, 400));
    logo_tex_ = load_png_resource(L"KAYRA_K", std::clamp((int)(64 * scale), 32, 200));
    apply_style(scale);
}

void Hud::set_input(const std::string& t) {
    snprintf(input_, sizeof input_, "%s", t.c_str());
    refocus_ = true;
    cursor_end_ = true;
    if (page_ != Page::Home && page_ != Page::Chat) page_ = Page::Home;
}

// ------------------------------------------------------------------ helpers

void Hud::card(ImDrawList* dl, ImVec4 r, bool lit) {
    float k = k_, rd = 9 * k;
    ImVec2 a(r.x, r.y), b(r.x + r.z, r.y + r.w);
    for (int i = 3; i >= 1; i--)
        dl->AddRect(ImVec2(a.x - i * 1.6f * k, a.y - i * 1.6f * k), ImVec2(b.x + i * 1.6f * k, b.y + i * 1.6f * k),
                    col(kCyan, (lit ? 0.07f : 0.035f) / i), rd + i * 1.6f * k, 0, 1.6f * k);
    dl->AddRectFilled(a, b, col(kPanel), rd);
    dl->AddRectFilledMultiColor(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, a.y + std::min(60 * k, r.w * 0.5f)), col(kCyan, 0.05f),
                                col(kCyan, 0.05f), col(kCyan, 0), col(kCyan, 0));
    dl->AddRect(a, b, col(kCyan, lit ? 0.6f : 0.30f), rd, 0, 1.2f * k);
    // brighter top edge fading out to the right
    dl->AddRectFilledMultiColor(ImVec2(a.x + rd, a.y - 0.5f * k), ImVec2(a.x + r.z * 0.45f, a.y + 1.0f * k), col(kCyan, 0.8f),
                                col(kCyan, 0), col(kCyan, 0), col(kCyan, 0.8f));
}

void Hud::icon(ImDrawList* dl, ImVec2 c, float size, unsigned cp, ImU32 color) {
    std::string g = utf8(cp);
    ImVec2 sz = f_icon->CalcTextSizeA(size, FLT_MAX, 0, g.c_str());
    dl->AddText(f_icon, size, ImVec2(std::floor(c.x - sz.x / 2), std::floor(c.y - sz.y / 2)), color, g.c_str());
}

void Hud::card_title(ImDrawList* dl, ImVec4 r, unsigned ic, const std::string& title) {
    float k = k_;
    icon(dl, ImVec2(r.x + 26 * k, r.y + 24 * k), 17 * k, ic, col(kCyan));
    text(dl, f_semi, 15.5f * k, ImVec2(r.x + 46 * k, r.y + 14 * k), col(kText), title);
}

bool Hud::hit(const char* id, ImVec2 a, ImVec2 b) {
    if (b.x - a.x < 1 || b.y - a.y < 1) return false;
    ImGui::SetCursorScreenPos(a);
    bool clicked = ImGui::InvisibleButton(id, ImVec2(b.x - a.x, b.y - a.y));
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
}

// ------------------------------------------------------------------ frame

void Hud::background(ImDrawList* dl, const HudState& s, ImVec2 size, ImVec2 c) {
    float k = k_;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(4, 12, 20, 255), IM_COL32(4, 12, 20, 255), IM_COL32(3, 9, 15, 255),
                                IM_COL32(3, 9, 15, 255));
    glow(dl, glow_tex_, c, size.x * 0.42f, size.y * 0.62f, col(color_, 0.10f));
    glow(dl, glow_tex_, ImVec2(size.x * 0.5f, size.y), size.x * 0.35f, 70 * k, col(kCyan, 0.10f));
    // faint circuit lines in the corners
    ImU32 line = col(kCyan, 0.05f);
    for (int i = 0; i < 6; i++) {
        float o = i * 22 * k;
        dl->AddLine(ImVec2(size.x - 420 * k + o, size.y), ImVec2(size.x - 300 * k + o, size.y - 120 * k), line, 1);
        dl->AddLine(ImVec2(size.x - 300 * k + o, size.y - 120 * k), ImVec2(size.x, size.y - 120 * k), line, 1);
    }
    for (int i = 0; i < 5; i++) {
        float o = i * 20 * k;
        dl->AddLine(ImVec2(250 * k, 112 * k + o), ImVec2(330 * k, 112 * k + o), line, 1);
    }
    // glowing floor line under the dock
    float fy = size.y - 22 * k;
    dl->AddRectFilledMultiColor(ImVec2(c.x - 360 * k, fy), ImVec2(c.x, fy + 1.5f * k), col(kCyan, 0), col(kCyan, 0.55f),
                                col(kCyan, 0.55f), col(kCyan, 0));
    dl->AddRectFilledMultiColor(ImVec2(c.x, fy), ImVec2(c.x + 360 * k, fy + 1.5f * k), col(kCyan, 0.55f), col(kCyan, 0),
                                col(kCyan, 0), col(kCyan, 0.55f));
    (void)s;
}

void Hud::sidebar(ImDrawList* dl, const HudState& s, ImVec2 size, HudActions& act) {
    float k = k_, w = 234 * k;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, size.y), IM_COL32(3, 11, 18, 235), IM_COL32(5, 16, 26, 235),
                                IM_COL32(5, 16, 26, 235), IM_COL32(3, 11, 18, 235));
    dl->AddRectFilledMultiColor(ImVec2(w - 1, 0), ImVec2(w, size.y), col(kCyan, 0.05f), col(kCyan, 0.05f), col(kCyan, 0.25f),
                                col(kCyan, 0.25f));
    // logo
    ImVec2 lc(58 * k, 82 * k);
    glow(dl, glow_tex_, lc, 52 * k, 52 * k, col(kCyan, 0.30f));
    if (logo_tex_) {
        float h = 27 * k;
        dl->AddImage((ImTextureID)(intptr_t)logo_tex_, ImVec2(lc.x - h, lc.y - h), ImVec2(lc.x + h, lc.y + h));
    }
    spaced(dl, f_title, 29 * k, ImVec2(94 * k, 66 * k), col(kText), "KAYRA", 5 * k);
    text(dl, f_small, 11.5f * k, ImVec2(216 * k, 99 * k), col(kDim), "v" + s.version, 1.0f);

    struct Item {
        unsigned ic;
        const char* key;
        Page page;
        int settings;  // >= 0: opens that settings tab
    };
    static const Item items[] = {{I_HOME, "nav_home", Page::Home, -1},        {I_SLIDERS, "nav_system", Page::System, -1},
                                 {I_GLOBE, "nav_brain", Page::Home, 1},       {I_CHAT, "nav_chat", Page::Chat, -1},
                                 {I_GRID, "nav_tools", Page::Tools, -1},      {I_PUZZLE, "nav_modules", Page::Modules, -1},
                                 {I_HISTORY, "nav_timers", Page::Timers, -1}, {I_GEAR, "nav_settings", Page::Home, 0}};
    float y = 158 * k;
    for (auto& it : items) {
        ImVec2 a(14 * k, y), b(220 * k, y + 44 * k);
        bool sel = it.settings < 0 && page_ == it.page;
        bool hov = ImGui::IsMouseHoveringRect(a, b) && !ImGui::IsAnyItemActive();
        if (sel) {
            glow(dl, glow_tex_, ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2), 120 * k, 34 * k, col(kCyan, 0.10f));
            dl->AddRectFilled(a, b, col(kCyan, 0.12f), 8 * k);
            dl->AddRect(a, b, col(kCyan, 0.75f), 8 * k, 0, 1.3f * k);
        } else if (hov) {
            dl->AddRectFilled(a, b, col(kCyan, 0.06f), 8 * k);
        }
        icon(dl, ImVec2(44 * k, y + 22 * k), 17 * k, it.ic, col(sel ? kText : kDim, sel ? 1.0f : 0.95f));
        text(dl, f_body, 15 * k, ImVec2(70 * k, y + 12 * k), col(sel ? kText : kText, sel ? 1.0f : 0.80f), T(it.key));
        if (hit(it.key, a, b)) {
            if (it.settings >= 0) {
                act.open_settings = true;
                act.settings_tab = it.settings;
            } else {
                page_ = it.page;
            }
        }
        y += 48.5f * k;
    }

    // status box
    float by = size.y - 150 * k;
    if (by > y + 10 * k) {
        ImVec2 a(22 * k, by), b(214 * k, by + 38 * k);
        dl->AddRectFilled(a, b, col(kPanel), 7 * k);
        dl->AddRect(a, b, col(kCyan, 0.28f), 7 * k, 0, 1.1f * k);
        ImVec4 st = s.online ? kGreen : kDim;
        float p = 0.6f + 0.4f * std::sin((float)s.t * 3);
        dl->AddCircleFilled(ImVec2(a.x + 16 * k, by + 19 * k), 4.5f * k, col(st, s.online ? p : 0.8f));
        text(dl, f_body, 14.5f * k, ImVec2(a.x + 28 * k, by + 9 * k), col(st), T(s.online ? "chip_online" : "chip_offline"));
        for (int i = 0; i < 9; i++) {
            float v = 0.3f + 0.7f * std::fabs(std::sin((float)s.t * 2.3f + i * 0.9f) * std::sin((float)s.t * 0.7f + i * 0.45f));
            if (!s.wave.empty() && s.mic_on) v = std::max(0.15f, s.wave[s.wave.size() - 1 - std::min<size_t>(s.wave.size() - 1, i * 2)]);
            float x = b.x - 70 * k + i * 6.5f * k, hgt = 4 * k + v * 14 * k;
            dl->AddRectFilled(ImVec2(x, by + 27 * k - hgt), ImVec2(x + 3 * k, by + 27 * k), col(kCyan, 0.85f), 1 * k);
        }
        text(dl, f_body, 13.5f * k, ImVec2(28 * k, by + 52 * k), col(kText, 0.85f), "KAYRA     v" + s.version);
        text(dl, f_small, 12.5f * k, ImVec2(28 * k, by + 70 * k), col(kDim), T("footer_sub"));
    }
    // wireframe hills
    float base = size.y;
    for (int layer = 0; layer < 3; layer++) {
        std::vector<ImVec2> pts;
        for (int i = 0; i <= 24; i++) {
            float x = w * i / 24.0f;
            float hgt = (14 + layer * 10) * k + (22 + layer * 6) * k * std::fabs(std::sin(i * 0.55f + layer * 1.3f)) *
                                                    (0.5f + 0.5f * std::sin(i * 0.21f + layer));
            pts.push_back(ImVec2(x, base - 8 * k - hgt));
        }
        for (size_t i = 0; i + 1 < pts.size(); i += 2) dl->AddLine(pts[i], ImVec2(pts[i].x, base), col(kCyan, 0.05f), 1);
        dl->AddPolyline(pts.data(), (int)pts.size(), col(kCyan, 0.10f + 0.08f * layer), 0, 1.0f * k);
    }
}

void Hud::top_bar(ImDrawList* dl, const HudState& s, ImVec2 size, HudActions& act) {
    float k = k_;
    struct Tab {
        unsigned ic;
        const char* key;
        Page page;
        int settings;
    };
    static const Tab tabs[] = {{I_HOME, "tab_dashboard", Page::Home, -1},
                               {I_SUN, "tab_brain", Page::Home, 1},
                               {I_MONITOR, "tab_system", Page::System, -1},
                               {I_GRID, "tab_tools", Page::Tools, -1},
                               {I_GEAR, "tab_settings", Page::Home, 0}};
    float gap = 26 * k, total = 0;
    float widths[5];
    for (int i = 0; i < 5; i++) {
        widths[i] = tw(f_body, 15 * k, T(tabs[i].key)) + 62 * k;
        total += widths[i] + (i ? gap : 0);
    }
    float cx = (234 * k + size.x - 240 * k) / 2;
    float x = cx - total / 2, y0 = 42 * k, y1 = 86 * k;
    // frame around the tabs
    float fx0 = x - 34 * k, fx1 = x + total + 34 * k;
    ImVec2 frame[6] = {{fx0 - 30 * k, 18 * k}, {fx0, 18 * k}, {fx0 + 18 * k, y1 + 8 * k}, {fx1 - 18 * k, y1 + 8 * k}, {fx1, 18 * k}, {fx1 + 30 * k, 18 * k}};
    dl->AddPolyline(frame, 6, col(kCyan, 0.22f), 0, 1.2f * k);
    for (int i = 0; i < 5; i++) {
        const Tab& t = tabs[i];
        bool sel = t.settings < 0 && page_ == t.page && !(t.page == Page::Home && i != 0);
        ImVec2 a(x, y0), b(x + widths[i], y1);
        bool hov = ImGui::IsMouseHoveringRect(a, b);
        if (sel) {
            dl->AddRectFilled(a, b, col(kCyan, 0.10f), 7 * k);
            dl->AddRect(a, b, col(kCyan, 0.7f), 7 * k, 0, 1.2f * k);
            glow(dl, glow_tex_, ImVec2((a.x + b.x) / 2, b.y), widths[i] * 0.6f, 10 * k, col(kCyan, 0.8f));
            dl->AddLine(ImVec2(a.x + 14 * k, b.y), ImVec2(b.x - 14 * k, b.y), col(ImVec4(0.7f, 0.97f, 1, 1)), 2.0f * k);
        } else if (hov) {
            dl->AddRectFilled(a, b, col(kCyan, 0.06f), 7 * k);
        }
        icon(dl, ImVec2(a.x + 26 * k, (a.y + b.y) / 2), 16 * k, t.ic, col(sel ? kText : kText, sel ? 1.0f : 0.8f));
        text(dl, f_body, 15 * k, ImVec2(a.x + 44 * k, a.y + 11 * k), col(kText, sel ? 1.0f : 0.82f), T(t.key));
        if (hit(t.key, a, b)) {
            if (t.settings >= 0) {
                act.open_settings = true;
                act.settings_tab = t.settings;
            } else {
                page_ = t.page;
            }
        }
        x += widths[i] + gap;
    }
    // bell, clock, date
    float rx = size.x - 28 * k;
    float dw = std::max(tw(f_big, 38 * k, s.clock), tw(f_small, 13 * k, s.date));
    float sep = rx - dw - 22 * k;
    dl->AddLine(ImVec2(sep, 46 * k), ImVec2(sep, 102 * k), col(kCyan, 0.25f), 1.0f * k);
    ImVec2 bc(sep - 32 * k, 72 * k);
    bool ring = !s.timers.empty();
    icon(dl, bc, 20 * k, I_BELL, col(ring ? kAmber : kCyan, 0.95f));
    if (ring) {
        dl->AddCircleFilled(ImVec2(bc.x + 9 * k, bc.y - 9 * k), 7 * k, col(kAmber));
        text(dl, f_small, 10.5f * k, ImVec2(bc.x + 9 * k, bc.y - 16.5f * k), IM_COL32(10, 10, 10, 255), std::to_string(s.timers.size()), 0.5f);
    }
    if (hit("bell", ImVec2(bc.x - 16 * k, bc.y - 16 * k), ImVec2(bc.x + 16 * k, bc.y + 16 * k))) page_ = Page::Timers;
    text(dl, f_big, 38 * k, ImVec2(rx, 38 * k), col(kText), s.clock, 1.0f);
    text(dl, f_small, 13 * k, ImVec2(rx, 86 * k), col(kDim), s.date, 1.0f);
}

void Hud::greeting(ImDrawList* dl, const HudState& s, ImVec4 r) {
    float k = k_;
    float x = r.x, y = r.y;
    // corner frame
    dl->AddRectFilledMultiColor(ImVec2(x - 14 * k, y), ImVec2(x + 360 * k, y + 1.2f * k), col(kCyan, 0.5f), col(kCyan, 0), col(kCyan, 0),
                                col(kCyan, 0.5f));
    dl->AddRectFilledMultiColor(ImVec2(x - 14 * k, y), ImVec2(x - 12.8f * k, y + 92 * k), col(kCyan, 0.5f), col(kCyan, 0.5f), col(kCyan, 0),
                                col(kCyan, 0));
    dl->AddLine(ImVec2(x - 14 * k, y + 1), ImVec2(x + 22 * k, y + 1), col(kCyan, 0.95f), 2.2f * k);
    dl->AddLine(ImVec2(x - 13 * k, y), ImVec2(x - 13 * k, y + 20 * k), col(kCyan, 0.95f), 2.2f * k);

    SYSTEMTIME st;
    GetLocalTime(&st);
    const char* hi = st.wHour < 5 ? "hi_night" : st.wHour < 12 ? "hi_morning" : st.wHour < 18 ? "hi_day" : st.wHour < 22 ? "hi_evening" : "hi_night";
    std::string g = T(hi);
    float gx = x + 4 * k, gy = y + 16 * k;
    if (s.user_name.empty()) {
        text(dl, f_body, 16 * k, ImVec2(gx, gy), col(kText, 0.85f), g + ".");
    } else {
        gx += text(dl, f_body, 16 * k, ImVec2(gx, gy), col(kText, 0.85f), g + ", ");
        gx += text(dl, f_semi, 16 * k, ImVec2(gx, gy), col(kCyan), s.user_name);
        text(dl, f_body, 16 * k, ImVec2(gx, gy), col(kText, 0.85f), ".");
    }
    const char* head = "head_ready";
    switch (s.look) {
        case Look::Listening: head = "head_listening"; break;
        case Look::Hearing: head = "head_hearing"; break;
        case Look::Transcribing: head = "head_transcribing"; break;
        case Look::Thinking: head = "head_thinking"; break;
        case Look::Speaking: head = "head_speaking"; break;
        case Look::Error: head = "head_error"; break;
        default:
            if (s.download >= 0 || s.status == T("ui_preparing")) head = "head_preparing";
    }
    float hx = x + 2 * k, hy = y + 38 * k;
    hx += spaced(dl, f_title, 34 * k, ImVec2(hx, hy), col(kText), "KAYRA", 3.5f * k) + 12 * k;
    text(dl, f_big, 32 * k, ImVec2(hx, hy + 1.5f * k), col(s.look == Look::Error ? kRed : kText), T(head));
    std::string sub = T("sub_ok");
    if (s.look == Look::Error && !s.hint.empty()) sub = s.hint;
    if (s.download >= 0) sub = F("ui_downloading", {std::to_string((int)(s.download * 100))});
    text(dl, f_body, 15 * k, ImVec2(x + 4 * k, y + 82 * k), col(s.look == Look::Error ? kRed : kText, 0.8f), fit(f_body, 15 * k, sub, r.z));
    if (s.download >= 0) {
        float bw = 260 * k, by = y + 106 * k;
        dl->AddRectFilled(ImVec2(x + 4 * k, by), ImVec2(x + 4 * k + bw, by + 3 * k), col(kCyan, 0.15f), 2 * k);
        dl->AddRectFilled(ImVec2(x + 4 * k, by), ImVec2(x + 4 * k + bw * s.download, by + 3 * k), col(kCyan), 2 * k);
    }
}

void Hud::chips(ImDrawList* dl, const HudState& s, float right, float y) {
    float k = k_, h = 42 * k;
    bool busy = s.stats.cpu > 85 || s.stats.ram > 92;
    struct Chip {
        int kind;  // 0 dot, else icon
        unsigned ic;
        std::string label;
        ImVec4 c;
    };
    Chip list[3] = {{0, 0, T(s.online ? "chip_online" : "chip_offline"), s.online ? kGreen : kDim},
                    {1, I_ROBOT, T(s.brain_on ? "chip_brain_on" : "chip_brain_off"), kCyan},
                    {1, I_PULSE, T(busy ? "chip_busy" : "chip_stable"), busy ? kAmber : kCyan}};
    float x = right;
    for (int i = 2; i >= 0; i--) {
        float w = tw(f_body, 15 * k, list[i].label) + 58 * k;
        x -= w;
        ImVec2 a(x, y), b(x + w, y + h);
        float sk = 10 * k;
        ImVec2 poly[6] = {{a.x + sk, a.y}, {b.x, a.y}, {b.x, b.y - sk}, {b.x - sk, b.y}, {a.x, b.y}, {a.x, a.y + sk}};
        dl->AddConvexPolyFilled(poly, 6, col(kPanel));
        dl->AddPolyline(poly, 6, col(kCyan, 0.45f), ImDrawFlags_Closed, 1.2f * k);
        ImVec2 ic(a.x + 24 * k, y + h / 2);
        if (list[i].kind == 0) {
            float p = 0.6f + 0.4f * std::sin((float)s.t * 3);
            glow(dl, glow_tex_, ic, 12 * k, 12 * k, col(list[i].c, 0.6f * p));
            dl->AddCircleFilled(ic, 4.5f * k, col(list[i].c));
        } else {
            icon(dl, ic, 16 * k, list[i].ic, col(list[i].c));
        }
        text(dl, f_body, 15 * k, ImVec2(a.x + 42 * k, y + 11 * k), col(kText, 0.92f), list[i].label);
        x -= 10 * k;
    }
}

// ------------------------------------------------------------------ core

void Hud::core(ImDrawList* dl, const HudState& s, ImVec2 c, float r) {
    float k = k_, t = (float)s.t, L = std::clamp(s.level, 0.0f, 1.0f);
    float spin = 0.22f;
    switch (s.look) {
        case Look::Thinking: spin = 1.4f; break;
        case Look::Hearing: spin = 0.6f; break;
        case Look::Transcribing: spin = 0.9f; break;
        case Look::Speaking: spin = 0.45f; break;
        case Look::Offline: spin = 0.05f; break;
        default: break;
    }
    float dt = (float)std::clamp(s.t - last_t_, 0.0, 0.1);
    last_t_ = s.t;
    yaw_ += dt * spin;
    ring_ += dt * (0.08f + spin * 0.25f);
    ImVec4 C = color_, W = mix(color_, ImVec4(1, 1, 1, 1), 0.7f);

    glow(dl, glow_tex_, c, r * 2.1f, r * 2.1f, col(C, 0.20f + 0.25f * L));

    // outer rings
    dl->AddCircle(c, r * 1.25f, col(C, 0.10f), 160, 1.0f * k);
    for (int i = 0; i < 48; i++) {
        float a = ring_ * 0.4f + i * 2 * kPi / 48;
        arc(dl, c, r * 1.11f, a, a + 0.05f, col(C, 0.22f), 2.2f * k);
    }
    struct Arc {
        float a, len, rf, th, al;
        int dir;
    };
    static const Arc arcs[] = {{3.55f, 0.80f, 1.20f, 4.5f, 0.95f, 1},  {0.30f, 0.70f, 1.20f, 4.5f, 0.95f, 1},
                               {2.55f, 0.95f, 1.29f, 1.3f, 0.45f, -1}, {5.25f, 1.05f, 1.29f, 1.3f, 0.45f, -1},
                               {1.25f, 0.45f, 1.16f, 2.0f, 0.65f, -1}, {4.35f, 0.55f, 1.16f, 2.0f, 0.65f, -1},
                               {1.95f, 0.25f, 1.35f, 3.0f, 0.55f, 1},  {5.00f, 0.22f, 1.35f, 3.0f, 0.55f, 1}};
    for (auto& a : arcs) {
        float a0 = a.a + ring_ * a.dir * (a.dir > 0 ? 1.0f : 0.6f);
        float th = a.th * k * (a.th > 4 ? 1.0f + 0.6f * L : 1.0f);
        if (a.th > 4) arc(dl, c, r * a.rf, a0, a0 + a.len, col(C, 0.12f + 0.15f * L), th * 3.2f);
        arc(dl, c, r * a.rf, a0, a0 + a.len, col(a.th > 4 ? W : C, a.al), th);
    }
    for (int sector = 0; sector < 2; sector++) {
        float base = sector ? 0.2f : kPi + 0.2f;
        for (int i = 0; i < 22; i++) {
            float a = base - ring_ * 0.3f + i * 0.045f;
            dl->AddLine(polar(c, r * 1.39f, a), polar(c, r * (i % 5 == 0 ? 1.46f : 1.43f), a), col(C, 0.30f), 1.0f * k);
        }
    }
    if (s.look == Look::Listening || s.look == Look::Hearing) {
        for (int i = 0; i < 3; i++) {
            float ph = std::fmod(t * 0.6f + i / 3.0f, 1.0f);
            dl->AddCircle(c, r * (1.0f + ph * 0.45f), col(C, 0.35f * (1 - ph)), 128, 1.6f * k);
        }
    }
    if (s.look == Look::Thinking || s.look == Look::Transcribing) {
        for (int i = 0; i < 16; i++) {
            float a = t * 3.2f - i * 0.05f;
            dl->AddCircleFilled(polar(c, r * 1.20f, a), (4.5f - i * 0.25f) * k, col(W, 1.0f - i / 16.0f));
        }
    }

    // particle sphere
    float cy = std::cos(yaw_), sy = std::sin(yaw_), ct = std::cos(0.42f), st = std::sin(0.42f);
    float amp = 0.035f + 0.11f * L + (s.look == Look::Thinking ? 0.03f : 0.0f);
    float flow = t * (0.6f + spin * 0.8f);
    for (const P3& p : sphere_points()) {
        float w1 = std::sin(p.y * 5.0f + flow * 1.1f + p.x * 2.0f);
        float w2 = std::sin(p.x * 4.0f - flow * 0.9f + p.z * 3.0f);
        float d = 1.0f + amp * (w1 * 0.6f + w2 * 0.4f);
        float band = std::sin(p.y * 6.5f + 2.4f * std::sin(p.x * 2.3f + flow * 0.7f) + p.z * 1.6f + flow * 0.5f);
        float b = 0.30f + 0.70f * std::pow(std::max(0.0f, band), 5.0f);
        float x1 = p.x * cy + p.z * sy, z1 = -p.x * sy + p.z * cy;
        float y2 = p.y * ct - z1 * st, z2 = p.y * st + z1 * ct;
        float depth = (z2 + 1.0f) * 0.5f;
        float rim = 1.0f - std::fabs(z2);
        float a = (0.10f + 0.90f * depth * depth) * (0.40f + 0.60f * b) + 0.35f * rim * rim * rim;
        if (a < 0.03f) continue;
        float px = c.x + x1 * r * d, py = c.y + y2 * r * d;
        float sz = (1.0f + 1.1f * depth + b * 1.0f) * k;
        dl->AddRectFilled(ImVec2(px - sz * 0.5f, py - sz * 0.5f), ImVec2(px + sz * 0.5f, py + sz * 0.5f),
                          col(mix(C, W, 0.15f + 0.65f * b * depth), std::min(1.0f, a)));
    }
    // flowing sheets: bundles of dotted strands sweeping round the sphere
    for (int i = 0; i < 4; i++) {
        for (int strand = 0; strand < 7; strand++) {
            float off = (strand - 3) * 0.035f;
            for (int j = 0; j < 100; j++) {
                float th = j * 2 * kPi / 100 + i * 0.8f;
                float lat = -0.62f + i * 0.40f + off * (1.0f + 0.6f * std::sin(th * 3 + flow)) +
                            0.32f * std::sin(2 * th + flow * 0.55f + i * 1.7f) + 0.10f * std::sin(5 * th - flow * 1.1f + i);
                float rr = 1.0f + amp * 0.7f * std::sin(3 * th + flow) + 0.015f * (strand - 3);
                float px3 = std::cos(lat) * std::cos(th), py3 = std::sin(lat), pz3 = std::cos(lat) * std::sin(th);
                float x1 = px3 * cy + pz3 * sy, z1 = -px3 * sy + pz3 * cy;
                float y2 = py3 * ct - z1 * st, z2 = py3 * st + z1 * ct;
                float depth = (z2 + 1.0f) * 0.5f;
                if (depth < 0.25f) continue;
                float edge = 1.0f - std::fabs(strand - 3) / 3.5f;
                float a = (depth - 0.25f) / 0.75f * (0.35f + 0.65f * edge);
                float px = c.x + x1 * r * rr, py = c.y + y2 * r * rr, sz = (1.0f + 1.2f * depth * edge) * k;
                dl->AddRectFilled(ImVec2(px - sz * 0.5f, py - sz * 0.5f), ImVec2(px + sz * 0.5f, py + sz * 0.5f), col(W, a));
            }
        }
    }
    dl->AddCircle(c, r * 1.01f, col(C, 0.30f), 128, 1.2f * k);
    glow(dl, glow_tex_, c, r * 1.15f, r * 1.15f, col(C, 0.10f + 0.12f * L));

    // the emblem
    glow(dl, glow_tex_, c, r * 0.42f, r * 0.42f, IM_COL32(2, 9, 16, 200));
    glow(dl, glow_tex_, c, r * (0.40f + 0.15f * L), r * (0.40f + 0.15f * L), col(C, 0.40f + 0.35f * L));
    float ks = r * (0.25f + 0.02f * L);
    if (k_tex_) {
        dl->AddImage((ImTextureID)(intptr_t)k_tex_, ImVec2(c.x - ks, c.y - ks), ImVec2(c.x + ks, c.y + ks), ImVec2(0, 0), ImVec2(1, 1),
                     col(mix(ImVec4(1, 1, 1, 1), C, s.look == Look::Thinking || s.look == Look::Error ? 0.6f : 0.0f)));
    } else {
        text(dl, f_title, ks * 1.6f, ImVec2(c.x, c.y - ks * 0.8f), col(W), "K", 0.5f);
    }
}

void Hud::core_title(ImDrawList* dl, const HudState& s, float cx, float y, float maxw) {
    float k = k_;
    glow(dl, glow_tex_, ImVec2(cx, y + 16 * k), 160 * k, 26 * k, col(color_, 0.18f));
    spaced(dl, f_title, 30 * k, ImVec2(cx, y), col(kText), "KAYRA", 16 * k, 0.5f);
    spaced(dl, f_body, 12.5f * k, ImVec2(cx, y + 42 * k), col(kDim), T("core_sub"), 6 * k, 0.5f);
    for (int side = -1; side <= 1; side += 2) {
        float x0 = cx + side * 112 * k, x1 = cx + side * 172 * k, x2 = cx + side * 190 * k;
        ImVec2 pts[3] = {{x0, y + 50 * k}, {x1, y + 50 * k}, {x2, y + 30 * k}};
        dl->AddPolyline(pts, 3, col(kCyan, 0.45f), 0, 1.2f * k);
        dl->AddCircleFilled(ImVec2(x2, y + 30 * k), 2 * k, col(kCyan, 0.8f));
    }
    std::string line;
    ImVec4 c = kDim;
    if (!s.heard.empty()) {
        line = "“" + s.heard + "”";
        c = kText;
    } else if (!s.hint.empty() && s.look != Look::Error) {
        line = s.hint;
    }
    if (!line.empty()) text(dl, f_body, 15 * k, ImVec2(cx, y + 70 * k), col(c, 0.9f), fit(f_body, 15 * k, line, maxw), 0.5f);
}

void Hud::dock(ImDrawList* dl, const HudState& s, float cx, float y, HudActions& act) {
    float k = k_, w = 84 * k, h = 104 * k, gap = 11 * k;
    int active = (int)s.tools_on + (int)s.files_on + (int)s.web_on + (int)s.shell_on;
    struct Btn {
        const char* id;
        unsigned ic;
        std::string label, sub;
        bool on;
    };
    bool talking = s.look == Look::Listening || s.look == Look::Hearing;
    Btn btns[5] = {{"dock_mic", I_MIC, T("btn_mic"), T(s.mic_on ? "on" : "off"), s.mic_on},
                   {"dock_voice", I_VOLUME, T("btn_voice"), T(s.voice_on ? "on" : "off"), s.voice_on},
                   {"dock_talk", I_CHAT, T("btn_talk"), "Ctrl + Shift + K", talking},
                   {"dock_system", I_SLIDERS, T("btn_system"), T("btn_system_sub"), page_ == Page::System},
                   {"dock_modules", I_GRID, T("btn_modules"), F("n_active", {std::to_string(active)}), page_ == Page::Modules}};
    float x = cx - (5 * w + 4 * gap) / 2;
    for (int i = 0; i < 5; i++) {
        ImVec2 a(x, y), b(x + w, y + h);
        bool hov = ImGui::IsMouseHoveringRect(a, b);
        bool on = btns[i].on;
        for (int g = 3; g >= 1; g--)
            dl->AddRect(ImVec2(a.x - g * 1.6f * k, a.y - g * 1.6f * k), ImVec2(b.x + g * 1.6f * k, b.y + g * 1.6f * k),
                        col(kCyan, (on ? 0.09f : 0.04f) / g), 10 * k, 0, 1.6f * k);
        dl->AddRectFilled(a, b, col(kPanel), 10 * k);
        dl->AddRectFilledMultiColor(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), col(kCyan, hov ? 0.12f : 0.05f), col(kCyan, hov ? 0.12f : 0.05f),
                                    col(kCyan, 0.0f), col(kCyan, 0.0f));
        dl->AddRect(a, b, col(kCyan, on ? 0.75f : 0.40f), 10 * k, 0, 1.3f * k);
        icon(dl, ImVec2(x + w / 2, y + 30 * k), 22 * k, btns[i].ic, col(on ? ImVec4(0.75f, 0.97f, 1, 1) : kCyan));
        text(dl, f_semi, 13.5f * k, ImVec2(x + w / 2, y + 54 * k), col(kText), btns[i].label, 0.5f);
        text(dl, f_small, 11.5f * k, ImVec2(x + w / 2, y + 75 * k), col(on || i >= 2 ? kCyan : kDim), btns[i].sub, 0.5f);
        if (hit(btns[i].id, a, b)) {
            switch (i) {
                case 0: act.toggle_mic = true; break;
                case 1: act.toggle_voice = true; break;
                case 2: act.talk = true; break;
                case 3: page_ = page_ == Page::System ? Page::Home : Page::System; break;
                case 4: page_ = page_ == Page::Modules ? Page::Home : Page::Modules; break;
            }
        }
        x += w + gap;
    }
}

// ------------------------------------------------------------------ cards

void Hud::system_card(ImDrawList* dl, const HudState& s, ImVec4 r) {
    float k = k_;
    card(dl, r);
    card_title(dl, r, I_CPU, T("card_system"));
    float body = r.y + 44 * k, bh = r.w - 52 * k;
    ImVec2 gc(r.x + 70 * k, body + bh / 2);
    float gr = std::min(44 * k, bh * 0.44f);
    float v = (float)std::clamp(s.stats.cpu / 100.0, 0.0, 1.0);
    arc(dl, gc, gr, 0, 2 * kPi, col(kCyan, 0.14f), 9 * k);
    float a0 = -kPi / 2, a1 = a0 + std::max(0.02f, v) * 2 * kPi;
    arc(dl, gc, gr, a0, a1, col(kCyan, 0.18f), 18 * k);
    arc(dl, gc, gr, a0, a1, col(kCyan), 8 * k);
    dl->AddCircleFilled(polar(gc, gr, a1), 4 * k, col(ImVec4(0.8f, 0.98f, 1, 1)));
    dl->AddCircle(gc, gr - 10 * k, col(kCyan, 0.18f), 64, 1.0f * k);
    char num[16];
    snprintf(num, sizeof num, "%.0f", s.stats.cpu);
    float nw = tw(f_big, 30 * k, num), pw = tw(f_big, 17 * k, "%");
    bool tr = is_turkish();
    float nx = gc.x - (nw + pw) / 2;
    if (tr) {
        text(dl, f_big, 17 * k, ImVec2(nx, gc.y - 12 * k), col(kText, 0.85f), "%");
        text(dl, f_big, 30 * k, ImVec2(nx + pw, gc.y - 22 * k), col(kText), num);
    } else {
        text(dl, f_big, 30 * k, ImVec2(nx, gc.y - 22 * k), col(kText), num);
        text(dl, f_big, 17 * k, ImVec2(nx + nw, gc.y - 12 * k), col(kText, 0.85f), "%");
    }
    text(dl, f_small, 11.5f * k, ImVec2(gc.x, gc.y + 10 * k), col(kDim), T("lbl_cpu"), 0.5f);

    struct Row {
        unsigned ic;
        std::string label, value;
        float v;
        bool bar;
    };
    double used_disk = s.stats.disk_total_gb - s.stats.disk_free_gb;
    Row rows[4] = {{I_CPU, T("lbl_cpu"), pct(s.stats.cpu), (float)s.stats.cpu / 100, true},
                   {I_MEMORY, T("lbl_ram"), pct(s.stats.ram), (float)s.stats.ram / 100, true},
                   {I_DISK, T("lbl_disk"), gb(used_disk) + " / " + gb(s.stats.disk_total_gb),
                    s.stats.disk_total_gb > 0 ? (float)(used_disk / s.stats.disk_total_gb) : 0, true},
                   {I_BATTERY, T("lbl_battery"), pct(s.stats.battery), s.stats.battery / 100.0f, true}};
    if (s.stats.battery < 0) rows[3] = {I_CLOCK, T("lbl_uptime"), uptime_text(s.stats.uptime_s), 0, false};
    float x0 = r.x + 138 * k, x1 = r.x + r.z - 18 * k;
    float step = bh / 4.0f;
    for (int i = 0; i < 4; i++) {
        float y = body + i * step + step * 0.5f - 12 * k;
        icon(dl, ImVec2(x0 + 8 * k, y + 8 * k), 14 * k, rows[i].ic, col(kCyan, 0.9f));
        text(dl, f_body, 13.5f * k, ImVec2(x0 + 26 * k, y), col(kText, 0.9f), rows[i].label);
        text(dl, f_body, 13.5f * k, ImVec2(x1, y), col(kText), rows[i].value, 1.0f);
        if (rows[i].bar) {
            float by = y + 21 * k, bx0 = x0 + 26 * k;
            float fv = std::clamp(rows[i].v, 0.0f, 1.0f);
            bool bad = (i < 3 && fv > 0.9f) || (i == 3 && fv < 0.15f);
            ImVec4 bc = bad ? kRed : kCyan;
            dl->AddRectFilled(ImVec2(bx0, by), ImVec2(x1, by + 3.5f * k), col(kCyan, 0.12f), 2 * k);
            dl->AddRectFilledMultiColor(ImVec2(bx0, by), ImVec2(bx0 + (x1 - bx0) * fv, by + 3.5f * k), col(bc, 0.55f), col(bc), col(bc),
                                        col(bc, 0.55f));
        }
    }
}

void Hud::perf_card(ImDrawList* dl, const HudState& s, ImVec4 r) {
    float k = k_;
    card(dl, r);
    card_title(dl, r, I_CHART, T("card_perf"));
    std::string label = T(perf_ram_ ? "perf_ram" : "perf_cpu");
    float lw = tw(f_small, 12 * k, label) + 34 * k;
    ImVec2 a(r.x + r.z - 16 * k - lw, r.y + 12 * k), b(r.x + r.z - 16 * k, r.y + 36 * k);
    dl->AddRectFilled(a, b, col(kCyan, ImGui::IsMouseHoveringRect(a, b) ? 0.14f : 0.07f), 6 * k);
    dl->AddRect(a, b, col(kCyan, 0.35f), 6 * k, 0, 1.0f * k);
    text(dl, f_small, 12 * k, ImVec2(a.x + 10 * k, a.y + 4 * k), col(kText, 0.85f), label);
    icon(dl, ImVec2(b.x - 13 * k, (a.y + b.y) / 2), 10 * k, I_CHEVRON, col(kText, 0.8f));
    if (hit("perf_toggle", a, b)) perf_ram_ = !perf_ram_;
    ImVec4 area(r.x + 16 * k, r.y + 46 * k, r.z - 32 * k, r.w - 58 * k);
    if (area.w > 10 * k) area_chart(dl, area, series(perf_ram_ ? s.ram_hist : s.cpu_hist), kCyan, k, s.t);
}

void Hud::weather_card(ImDrawList* dl, const HudState& s, ImVec4 r) {
    float k = k_;
    card(dl, r);
    card_title(dl, r, I_SUN, T("card_weather"));
    const Weather& w = s.weather;
    float mid = r.y + 44 * k + (r.w - 44 * k) / 2;
    if (!w.ok) {
        text(dl, f_body, 15 * k, ImVec2(r.x + 26 * k, mid - 10 * k), col(kDim), s.weather_loading ? T("weather_loading") : T("weather_none"));
        return;
    }
    weather_icon(dl, ImVec2(r.x + 52 * k, mid + 2 * k), 54 * k, w.code, s.t);
    float x = r.x + 98 * k;
    std::string place = w.place;
    if (lower(place) == place) place = capitalize(place);
    text(dl, f_body, 13.5f * k, ImVec2(x, mid - 36 * k), col(kText, 0.9f), fit(f_body, 13.5f * k, place, r.z * 0.585f - 104 * k));
    char t[16];
    snprintf(t, sizeof t, "%.0f°C", w.temp);
    text(dl, f_big, 30 * k, ImVec2(x - 2 * k, mid - 18 * k), col(kText), t);
    text(dl, f_small, 12.5f * k, ImVec2(x, mid + 16 * k), col(kDim), fit(f_small, 12.5f * k, capitalize(weather_text(w.code, is_turkish())), 120 * k));
    float cx = r.x + r.z * 0.585f, rx = r.x + r.z - 18 * k;
    char hum[16], wind[24], feels[16];
    snprintf(hum, sizeof hum, is_turkish() ? "%%%d" : "%d%%", w.humidity);
    snprintf(wind, sizeof wind, "%.0f km/s", w.wind);
    if (!is_turkish()) snprintf(wind, sizeof wind, "%.0f km/h", w.wind);
    snprintf(feels, sizeof feels, "%.0f°C", w.feels);
    float ys[3] = {mid - 30 * k, mid - 6 * k, mid + 18 * k};
    ImU32 ic = col(kCyan, 0.9f);
    drop_icon(dl, ImVec2(cx + 7 * k, ys[0] + 9 * k), 13 * k, ic);
    wind_icon(dl, ImVec2(cx + 7 * k, ys[1] + 9 * k), 14 * k, ic, 1.4f * k);
    icon(dl, ImVec2(cx + 7 * k, ys[2] + 9 * k), 13 * k, I_THERMO, ic);
    const char* labels[3] = {"lbl_humidity", "lbl_wind", "lbl_feels"};
    const char* values[3] = {hum, wind, feels};
    for (int i = 0; i < 3; i++) {
        text(dl, f_small, 12.5f * k, ImVec2(cx + 22 * k, ys[i]), col(kText, 0.85f), T(labels[i]));
        text(dl, f_small, 12.5f * k, ImVec2(rx, ys[i]), col(kText), values[i], 1.0f);
    }
}

void Hud::timers_card(ImDrawList* dl, const HudState& s, ImVec4 r, bool big, HudActions& act) {
    float k = k_;
    card(dl, r, !s.timers.empty());
    card_title(dl, r, I_CLOCK, T("card_timers"));
    ImVec2 pa(r.x + r.z - 44 * k, r.y + 11 * k), pb(r.x + r.z - 16 * k, r.y + 37 * k);
    dl->AddRectFilled(pa, pb, col(kCyan, ImGui::IsMouseHoveringRect(pa, pb) ? 0.18f : 0.08f), 6 * k);
    dl->AddRect(pa, pb, col(kCyan, 0.45f), 6 * k, 0, 1.0f * k);
    icon(dl, ImVec2((pa.x + pb.x) / 2, (pa.y + pb.y) / 2), 12 * k, I_PLUS, col(kText));
    if (hit(big ? "timer_add_big" : "timer_add", pa, pb)) set_input(T("timer_prefix"));
    float x = r.x + 22 * k, w = r.z - 44 * k, y = r.y + 50 * k;
    if (s.timers.empty()) {
        text(dl, f_body, 14 * k, ImVec2(r.x + r.z / 2, r.y + 44 * k + (r.w - 44 * k) / 2 - 10 * k), col(kDim), T("no_timers"), 0.5f);
        return;
    }
    float fs = big ? 1.4f : 1.0f;
    for (auto& t : s.timers) {
        if (y > r.y + r.w - 34 * k * fs) break;
        double left = t.due - s.now;
        std::string label = t.label.empty() ? T("timer_default") : t.label;
        text(dl, f_body, 14 * k * fs, ImVec2(x, y), col(kText), fit(f_body, 14 * k * fs, label, w - 90 * k * fs));
        text(dl, f_big, 19 * k * fs, ImVec2(x + w, y - 3 * k), col(kAmber), mmss(left), 1.0f);
        float p = t.total > 0 ? (float)std::clamp(1.0 - left / t.total, 0.0, 1.0) : 0;
        float by = y + 24 * k * fs;
        dl->AddRectFilled(ImVec2(x, by), ImVec2(x + w, by + 3.5f * k), col(kAmber, 0.14f), 2 * k);
        dl->AddRectFilled(ImVec2(x, by), ImVec2(x + w * p, by + 3.5f * k), col(kAmber, 0.9f), 2 * k);
        y += 40 * k * fs;
    }
    (void)act;
}

// ------------------------------------------------------------------ conversation

void Hud::chat_panel(const HudState& s, ImVec4 r, HudActions& act) {
    float k = k_;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    card(bg, r);
    card_title(bg, r, I_CHAT, T("card_chat"));

    ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
    ImGui::SetNextWindowSize(ImVec2(r.z, r.w));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##chat", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* wl = ImGui::GetWindowDrawList();
    // header buttons
    ImVec2 ex(r.x + r.z - 62 * k, r.y + 24 * k), mo(r.x + r.z - 28 * k, r.y + 24 * k);
    bool full = page_ == Page::Chat;
    icon(wl, ex, 14 * k, full ? I_COLLAPSE : I_EXPAND, col(kText, 0.85f));
    icon(wl, mo, 16 * k, I_MORE, col(kText, 0.85f));
    if (hit("chat_expand", ImVec2(ex.x - 14 * k, ex.y - 14 * k), ImVec2(ex.x + 14 * k, ex.y + 14 * k))) page_ = full ? Page::Home : Page::Chat;
    if (hit("chat_more", ImVec2(mo.x - 14 * k, mo.y - 14 * k), ImVec2(mo.x + 14 * k, mo.y + 14 * k))) ImGui::OpenPopup("##chatmenu");
    ImGui::PushFont(f_body);
    if (ImGui::BeginPopup("##chatmenu")) {
        if (ImGui::MenuItem(U("menu_clear"))) act.clear = true;
        ImGui::EndPopup();
    }
    ImGui::PopFont();

    float input_h = s.show_input ? 62 * k : 0;
    ImGui::SetCursorScreenPos(ImVec2(r.x + 12 * k, r.y + 46 * k));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4 * k, 4 * k));
    ImGui::BeginChild("##log", ImVec2(r.z - 18 * k, r.w - 46 * k - input_h), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float avail = ImGui::GetContentRegionAvail().x;
    float body_size = (full ? 15.5f : 14.5f) * k;
    for (size_t i = 0; i < s.chat.size(); i++) {
        const ChatEntry& e = s.chat[i];
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec4 c = e.user ? kAmber : (e.notice ? kAmber : kCyan);
        std::string who = e.user ? T("ui_you") : "KAYRA";
        float hx = p.x + 12 * k;
        float ww = spaced(dl, f_semi, 13 * k, ImVec2(hx, p.y), col(c), who, 1.2f * k);
        text(dl, f_small, 11.5f * k, ImVec2(hx + ww + 10 * k, p.y + 1 * k), col(kDim, 0.85f), e.time);
        ImGui::Dummy(ImVec2(1, 19 * k));
        float tag_w = 0;
        if (!e.tool.empty()) tag_w = tw(f_small, 11.5f * k, "↗ " + e.tool) + 18 * k;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12 * k);
        ImGui::PushFont(f_body);
        ImGui::SetWindowFontScale(body_size / (16.0f * k));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail - 30 * k);
        ImGui::PushStyleColor(ImGuiCol_Text, e.user ? ImVec4(1.0f, 0.92f, 0.80f, 1) : kText);
        std::string body = e.text;
        if (e.streaming && std::fmod(s.t, 1.0) < 0.5) body += " ▍";
        ImGui::TextUnformatted(body.c_str());
        ImVec2 te = ImGui::GetItemRectMax();
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();
        dl->AddRectFilled(ImVec2(p.x + 3 * k, p.y + 1 * k), ImVec2(p.x + 5 * k, te.y + 2 * k), col(c, e.user ? 0.7f : 0.9f), 1 * k);
        if (tag_w > 0) {
            float tx1 = p.x + avail - 8 * k, ty = te.y + 6 * k;
            ImVec2 ta(tx1 - tag_w, ty), tb(tx1, ty + 20 * k);
            dl->AddRectFilled(ta, tb, col(kCyan, 0.08f), 5 * k);
            dl->AddRect(ta, tb, col(kCyan, 0.4f), 5 * k, 0, 1.0f * k);
            text(dl, f_small, 11.5f * k, ImVec2(ta.x + 9 * k, ty + 2.5f * k), col(kCyan, 0.95f), "↗ " + e.tool);
            ImGui::Dummy(ImVec2(1, 24 * k));
        }
        ImGui::Dummy(ImVec2(1, 9 * k));
    }
    if (s.chat_version != seen_version_) {
        ImGui::SetScrollHereY(1.0f);
        seen_version_ = s.chat_version;
    }
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0) ImGui::SetScrollY(ImGui::GetScrollY() - wheel * 60 * k);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if (s.show_input) {
        ImVec2 a(r.x + 12 * k, r.y + r.w - input_h + 8 * k), b(r.x + r.z - 12 * k, r.y + r.w - 12 * k);
        wl->AddRectFilled(a, b, col(ImVec4(0.02f, 0.06f, 0.10f, 0.9f)), 9 * k);
        wl->AddRect(a, b, col(kCyan, ImGui::IsMouseHoveringRect(a, b) ? 0.6f : 0.35f), 9 * k, 0, 1.2f * k);
        float mh = (a.y + b.y) / 2;
        icon(wl, ImVec2(a.x + 22 * k, mh), 15 * k, I_ATTACH, col(kText, 0.75f));
        if (hit("attach", ImVec2(a.x + 6 * k, a.y), ImVec2(a.x + 38 * k, b.y))) act.attach = true;
        icon(wl, ImVec2(b.x - 24 * k, mh), 17 * k, I_SEND, col(kCyan));
        bool send = hit("send", ImVec2(b.x - 42 * k, a.y), ImVec2(b.x - 4 * k, b.y));

        ImGui::PushFont(f_body);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * k, (b.y - a.y - ImGui::GetFontSize()) / 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::SetCursorScreenPos(ImVec2(a.x + 42 * k, a.y));
        ImGui::SetNextItemWidth(b.x - a.x - 90 * k);
        if (!s.typing.empty()) {
            std::string shown = s.typing + (std::fmod(s.t, 1.0) < 0.5 ? "|" : "");
            char buf[1024];
            snprintf(buf, sizeof buf, "%s", shown.c_str());
            ImGui::InputText("##typed", buf, sizeof buf, ImGuiInputTextFlags_ReadOnly);
        } else {
            if (refocus_) {
                ImGui::SetKeyboardFocusHere();
                refocus_ = false;
            }
            auto cb = [](ImGuiInputTextCallbackData* d) -> int {
                bool* end = (bool*)d->UserData;
                if (*end) {
                    d->CursorPos = d->SelectionStart = d->SelectionEnd = d->BufTextLen;
                    *end = false;
                }
                return 0;
            };
            bool enter = ImGui::InputTextWithHint("##in", U("input_hint"), input_, sizeof input_,
                                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways, cb, &cursor_end_);
            if ((enter || send) && input_[0]) {
                act.submit = input_;
                input_[0] = 0;
                refocus_ = true;
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ------------------------------------------------------------------ pages

void Hud::page_system(ImDrawList* dl, const HudState& s, ImVec4 r) {
    float k = k_, gap = 16 * k;
    float lw = r.z * 0.62f;
    float ch = (r.w - gap) / 2;
    for (int i = 0; i < 2; i++) {
        ImVec4 cr(r.x, r.y + i * (ch + gap), lw, ch);
        card(dl, cr);
        card_title(dl, cr, i ? I_MEMORY : I_CPU, T(i ? "lbl_ram" : "lbl_cpu"));
        double v = i ? s.stats.ram : s.stats.cpu;
        text(dl, f_big, 30 * k, ImVec2(cr.x + cr.z - 22 * k, cr.y + 10 * k), col(kText), pct(v), 1.0f);
        text(dl, f_small, 12 * k, ImVec2(cr.x + cr.z - 110 * k, cr.y + 20 * k), col(kDim), T("last_60"), 1.0f);
        area_chart(dl, ImVec4(cr.x + 20 * k, cr.y + 58 * k, cr.z - 40 * k, cr.w - 76 * k), series(i ? s.ram_hist : s.cpu_hist), kCyan, k, s.t);
    }
    ImVec4 dr(r.x + lw + gap, r.y, r.z - lw - gap, r.w);
    card(dl, dr);
    card_title(dl, dr, I_MONITOR, T("sys_details"));
    char ram[64];
    snprintf(ram, sizeof ram, "%.1f / %.0f GB", s.stats.ram_used_gb, s.stats.ram_total_gb);
    std::vector<std::pair<std::string, std::string>> rows = {
        {T("sys_computer"), s.pc_name},
        {T("sys_cpu_model"), s.cpu_name},
        {T("lbl_cpu"), pct(s.stats.cpu)},
        {T("sys_ram_used"), ram},
        {T("lbl_disk"), gb(s.stats.disk_total_gb - s.stats.disk_free_gb) + " / " + gb(s.stats.disk_total_gb)},
        {T("sys_disk_free"), gb(s.stats.disk_free_gb)},
        {T("lbl_battery"), s.stats.battery < 0 ? "—" : pct(s.stats.battery) + (s.stats.charging ? "  ⚡" : "")},
        {T("lbl_uptime"), uptime_text(s.stats.uptime_s)},
        {T("sys_processes"), std::to_string(s.stats.processes)},
        {T("tab_brain"), s.brain_label},
        {T("btn_voice"), s.voice_label},
    };
    float y = dr.y + 58 * k, x0 = dr.x + 24 * k, x1 = dr.x + dr.z - 24 * k;
    float step = std::min(46 * k, (dr.w - 76 * k) / rows.size());
    for (auto& [label, value] : rows) {
        text(dl, f_small, 12.5f * k, ImVec2(x0, y), col(kDim), label);
        text(dl, f_body, 14.5f * k, ImVec2(x1, y + 14 * k), col(kText), fit(f_body, 14.5f * k, value, dr.z - 48 * k), 1.0f);
        dl->AddLine(ImVec2(x0, y + step - 6 * k), ImVec2(x1, y + step - 6 * k), col(kCyan, 0.08f), 1);
        y += step;
    }
}

void Hud::page_tools(ImDrawList* dl, const HudState& s, ImVec4 r, HudActions& act) {
    float k = k_;
    struct Group {
        unsigned ic;
        const char* title_tr;
        const char* title_en;
        const char* ex_tr[3];
        const char* ex_en[3];
    };
    static const Group groups[] = {
        {I_SUN, "Bilgi", "Information", {"Saat kaç?", "Hava nasıl?", "Nikola Tesla kimdir?"}, {"What time is it?", "How's the weather?", "Who is Nikola Tesla?"}},
        {I_GLOBE, "İnternet", "Internet", {"Yapay zeka hakkında araştır", "Son dakika haberleri", "Teknoloji haberleri"},
         {"Research artificial intelligence", "Read the news", "Technology news"}},
        {I_FOLDER, "Dosyalar", "Files", {"Masaüstünde ne var?", "İndirilenler klasöründe neler var?", "Belgeler klasörünü aç"},
         {"What's on my desktop?", "What's in my downloads folder?", "Open my documents folder"}},
        {I_GRID, "Uygulamalar", "Apps", {"Hesap makinesini aç", "YouTube'da lofi müzik ara", "Not defterini aç"},
         {"Open calculator", "Search YouTube for lofi music", "Open notepad"}},
        {I_BOLT, "Hesap ve hafıza", "Math and memory", {"240'ın yüzde 15'i kaç?", "Kök 144 artı 3 üzeri 2", "Neleri hatırlıyorsun?"},
         {"15 percent of 240", "Square root of 144 plus 3 squared", "What do you remember?"}},
        {I_MONITOR, "Bilgisayar", "Computer", {"Sistem durumu nasıl?", "Ekran görüntüsü al", "Sesi yüzde 40 yap"},
         {"How is the system doing?", "Take a screenshot", "Set the volume to 40 percent"}},
    };
    bool tr = is_turkish();
    text(dl, f_body, 15 * k, ImVec2(r.x + 4 * k, r.y - 4 * k), col(kDim), T("tools_hint"));
    float top = r.y + 26 * k, gap = 16 * k;
    int cols = 3, rows = 2;
    float cw = (r.z - gap * (cols - 1)) / cols, chh = (r.w - 26 * k - gap * (rows - 1)) / rows;
    for (int i = 0; i < 6; i++) {
        const Group& g = groups[i];
        ImVec4 cr(r.x + (i % cols) * (cw + gap), top + (i / cols) * (chh + gap), cw, chh);
        card(dl, cr);
        card_title(dl, cr, g.ic, tr ? g.title_tr : g.title_en);
        float y = cr.y + 54 * k;
        for (int j = 0; j < 3; j++) {
            std::string ex = tr ? g.ex_tr[j] : g.ex_en[j];
            ImVec2 a(cr.x + 18 * k, y), b(cr.x + cr.z - 18 * k, y + 36 * k);
            if (b.y > cr.y + cr.w - 8 * k) break;
            bool hov = ImGui::IsMouseHoveringRect(a, b);
            dl->AddRectFilled(a, b, col(kCyan, hov ? 0.14f : 0.05f), 7 * k);
            dl->AddRect(a, b, col(kCyan, hov ? 0.6f : 0.25f), 7 * k, 0, 1.0f * k);
            text(dl, f_body, 14 * k, ImVec2(a.x + 14 * k, a.y + 8 * k), col(kText, 0.92f), "“" + ex + "”");
            icon(dl, ImVec2(b.x - 16 * k, (a.y + b.y) / 2), 12 * k, I_SEND, col(kCyan, hov ? 1.0f : 0.5f));
            std::string id = "ex" + std::to_string(i * 3 + j);
            if (hit(id.c_str(), a, b)) {
                act.submit = ex;
                page_ = Page::Home;
            }
            y += 44 * k;
        }
    }
    (void)s;
}

void Hud::page_modules(ImDrawList* dl, const HudState& s, ImVec4 r, HudActions& act) {
    float k = k_;
    struct Mod {
        const char* id;
        unsigned ic;
        const char* title;
        const char* desc;
        bool on;
    };
    Mod mods[4] = {{"tools", I_WRENCH, "mod_tools", "set_tools", s.tools_on},
                   {"files", I_FOLDER, "mod_files", "set_files", s.files_on},
                   {"web", I_GLOBE, "mod_web", "set_web", s.web_on},
                   {"shell", I_TERMINAL, "mod_shell", "set_shell", s.shell_on}};
    text(dl, f_body, 15 * k, ImVec2(r.x + 4 * k, r.y - 4 * k), col(kDim), T("modules_hint"));
    float top = r.y + 26 * k, gap = 16 * k;
    float cw = (r.z - gap) / 2, chh = std::min(170 * k, (r.w - 26 * k - gap) / 2);
    for (int i = 0; i < 4; i++) {
        ImVec4 cr(r.x + (i % 2) * (cw + gap), top + (i / 2) * (chh + gap), cw, chh);
        card(dl, cr, mods[i].on);
        icon(dl, ImVec2(cr.x + 40 * k, cr.y + 44 * k), 26 * k, mods[i].ic, col(mods[i].on ? kCyan : kDim));
        text(dl, f_semi, 17 * k, ImVec2(cr.x + 76 * k, cr.y + 22 * k), col(kText), T(mods[i].title));
        text(dl, f_small, 13 * k, ImVec2(cr.x + 76 * k, cr.y + 48 * k), col(kDim), fit(f_small, 13 * k, T(mods[i].desc), cr.z - 180 * k));
        // switch
        ImVec2 a(cr.x + cr.z - 86 * k, cr.y + 26 * k), b(cr.x + cr.z - 26 * k, cr.y + 54 * k);
        bool on = mods[i].on;
        dl->AddRectFilled(a, b, col(on ? kCyan : kDim, on ? 0.35f : 0.18f), 14 * k);
        dl->AddRect(a, b, col(on ? kCyan : kDim, 0.8f), 14 * k, 0, 1.2f * k);
        ImVec2 knob(on ? b.x - 14 * k : a.x + 14 * k, (a.y + b.y) / 2);
        if (on) glow(dl, glow_tex_, knob, 18 * k, 18 * k, col(kCyan, 0.6f));
        dl->AddCircleFilled(knob, 10 * k, col(on ? ImVec4(0.85f, 0.98f, 1, 1) : kDim));
        text(dl, f_small, 12 * k, ImVec2(cr.x + 76 * k, cr.y + cr.w - 34 * k), col(on ? kGreen : kDim), T(on ? "mod_on" : "mod_off"));
        std::string id = std::string("mod_") + mods[i].id;
        if (hit(id.c_str(), ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.z, cr.y + cr.w))) act.toggle_module = mods[i].id;
    }
}

void Hud::page_timers(ImDrawList* dl, const HudState& s, ImVec4 r, HudActions& act) {
    float k = k_, gap = 16 * k;
    float lw = r.z * 0.58f;
    timers_card(dl, s, ImVec4(r.x, r.y, lw, r.w), true, act);
    ImVec4 qr(r.x + lw + gap, r.y, r.z - lw - gap, r.w);
    card(dl, qr);
    card_title(dl, qr, I_HISTORY, T("quick_timer"));
    static const int mins[] = {1, 3, 5, 10, 15, 25, 30, 45, 60};
    float bw = (qr.z - 48 * k - 2 * 12 * k) / 3, bh = 58 * k;
    for (int i = 0; i < 9; i++) {
        ImVec2 a(qr.x + 24 * k + (i % 3) * (bw + 12 * k), qr.y + 60 * k + (i / 3) * (bh + 12 * k)), b(a.x + bw, a.y + bh);
        if (b.y > qr.y + qr.w - 70 * k) break;
        bool hov = ImGui::IsMouseHoveringRect(a, b);
        dl->AddRectFilled(a, b, col(kCyan, hov ? 0.16f : 0.06f), 8 * k);
        dl->AddRect(a, b, col(kCyan, hov ? 0.7f : 0.3f), 8 * k, 0, 1.1f * k);
        text(dl, f_big, 22 * k, ImVec2((a.x + b.x) / 2, a.y + 8 * k), col(kText), std::to_string(mins[i]), 0.5f);
        text(dl, f_small, 11.5f * k, ImVec2((a.x + b.x) / 2, a.y + 36 * k), col(kDim), T("minutes"), 0.5f);
        std::string id = "quick" + std::to_string(mins[i]);
        if (hit(id.c_str(), a, b)) act.submit = F("timer_quick", {std::to_string(mins[i])});
    }
    ImVec2 a(qr.x + 24 * k, qr.y + qr.w - 58 * k), b(qr.x + qr.z - 24 * k, qr.y + qr.w - 20 * k);
    bool hov = ImGui::IsMouseHoveringRect(a, b);
    dl->AddRectFilled(a, b, col(kAmber, hov ? 0.2f : 0.08f), 8 * k);
    dl->AddRect(a, b, col(kAmber, 0.6f), 8 * k, 0, 1.1f * k);
    text(dl, f_body, 14.5f * k, ImVec2((a.x + b.x) / 2, a.y + 9 * k), col(kText), T("custom_reminder"), 0.5f);
    if (hit("custom_reminder", a, b)) set_input(T("timer_prefix"));
}

// ------------------------------------------------------------------ draw

HudActions Hud::draw(const HudState& s) {
    HudActions act;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 size = io.DisplaySize;
    k_ = std::clamp(std::min(size.y / 900.0f, size.x / 1600.0f), 0.6f, 2.0f);
    io.FontGlobalScale = k_ / scale_;
    color_ = mix(color_, look_color(s.look), 0.08f);
    float k = k_;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // layout: left anchored by k, right anchored to the window edge, the middle takes the rest
    float x0 = 246 * k, x1 = size.x - 18 * k;
    float y0 = 250 * k, y1 = size.y - 78 * k;
    float left_w = 352 * k, chat_w = std::min(480 * k, (x1 - x0) * 0.36f);
    float mid0 = x0 + left_w, mid1 = x1 - chat_w - 14 * k;
    float cx = (mid0 + mid1) / 2;
    float dock_y = y1 - 104 * k;
    float title_y = dock_y - 108 * k;
    float extra = std::max(0.0f, (size.y - 900 * k) * 0.35f);
    float r = std::min({150 * k, (mid1 - mid0) * 0.30f, (title_y - 150 * k) * 0.34f});
    ImVec2 core_c(cx, std::min(372 * k + extra, title_y - r * 1.45f));

    background(dl, s, size, page_ == Page::Home ? core_c : ImVec2(size.x * 0.55f, size.y * 0.5f));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##hud", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing);
    sidebar(dl, s, size, act);
    top_bar(dl, s, size, act);
    greeting(dl, s, ImVec4(x0 + 14 * k, 124 * k, std::max(200 * k, mid0 + (mid1 - mid0) * 0.45f - x0), 100 * k));
    chips(dl, s, x1 - 8 * k, 152 * k);

    bool chat_visible = true;
    ImVec4 chat_r(x1 - chat_w, y0 - 3 * k, chat_w, y1 - y0 + 3 * k);
    ImVec4 content(x0, y0 + 14 * k, x1 - x0, y1 - y0 - 14 * k);
    switch (page_) {
        case Page::Home: {
            const float base[4] = {189, 113, 135, 91};
            float avail = y1 - y0 - 3 * 14 * k, f = avail / (528 * k);
            float y = y0;
            ImVec4 rs[4];
            for (int i = 0; i < 4; i++) {
                rs[i] = ImVec4(x0, y, left_w - 14 * k, base[i] * k * f);
                y += rs[i].w + 14 * k;
            }
            system_card(dl, s, rs[0]);
            perf_card(dl, s, rs[1]);
            weather_card(dl, s, rs[2]);
            timers_card(dl, s, rs[3], false, act);
            core(dl, s, core_c, r);
            core_title(dl, s, cx, title_y, mid1 - mid0 - 20 * k);
            dock(dl, s, cx, dock_y, act);
            break;
        }
        case Page::Chat:
            chat_r = ImVec4(x0, y0 - 3 * k, x1 - x0, y1 - y0 + 3 * k);
            break;
        case Page::System:
            chat_visible = false;
            page_system(dl, s, ImVec4(x0, y0, x1 - x0, y1 - y0));
            break;
        case Page::Tools:
            chat_visible = false;
            page_tools(dl, s, content, act);
            break;
        case Page::Modules:
            chat_visible = false;
            page_modules(dl, s, content, act);
            break;
        case Page::Timers:
            chat_visible = false;
            page_timers(dl, s, ImVec4(x0, y0, x1 - x0, y1 - y0), act);
            break;
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    if (chat_visible) chat_panel(s, chat_r, act);
    return act;
}

}  // namespace kayra
