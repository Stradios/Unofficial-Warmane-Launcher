// Unofficial Warmane Launcher - native Win32 + GDI+ build (no WebView2, runs fine under Wine).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "logic.hpp"

using namespace Gdiplus;
namespace fs = std::filesystem;

// ------------------------------------------------------------------ constants
static const int WIN_W = 1226, WIN_H = 467;
static const wchar_t* DEFAULT_EXE =
    L"C:\\Program Files (x86)\\World of Warcraft 3.3.5a (no install)\\Wow.exe";
static const wchar_t* URL_GITHUB = L"https://github.com/Stradios/Unofficial-Warmane-Launcher";
static const wchar_t* APP_TITLE = L"Unofficial Warmane Launcher";
static const wchar_t* URL_HOME = L"https://www.warmane.com/";
static const wchar_t* URL_RETRIEVE = L"https://www.warmane.com/account/retrieve";

enum HotId {
  H_NONE = 0, H_MIN, H_CLOSE, H_LOGO, H_NAV0, /* 6 items: H_NAV0..H_NAV0+5 */ H_ACCT = H_NAV0 + 8,
  H_LBBOX, H_LBGO, H_REMEMBER, H_HELP0 /* 4 */, H_PLAY = H_HELP0 + 4,
  H_VEIL, H_SETUPBOX, H_LOCATE, H_CONTINUE, H_GITHUB, H_OPENGAME, H_OPENADDONS
};

// -------------------------------------------------------------------- globals
static HINSTANCE g_inst;
static HWND g_wnd, g_edName, g_edPass;
static ULONG_PTR g_gdiToken;
static Bitmap* g_back = nullptr;
static Graphics* g_meas = nullptr;
static Bitmap* g_measBmp = nullptr;
static PrivateFontCollection* g_pfc = nullptr;
static FontFamily* g_family = nullptr;
static std::map<int, Font*> g_fonts;

struct Imgs {
  Bitmap *hero, *navbar, *navlogo, *wotlk, *ftl, *ftr, *fbl, *fbr, *fheader, *ffooter, *flc, *frc,
      *fcenter, *bmin, *bminh, *bclose, *bcloseh;
} I;

struct Hot { int id; RectF r; bool hand; };
// ---- background video: pre-rendered JPEG frame sequence (see tools/pack_video.py)
static const BYTE* g_vidData = nullptr;
static const uint32_t* g_vidOff = nullptr;
static uint32_t g_vidFps = 24, g_vidCount = 0, g_vidIdx = 0;
static Bitmap* g_vidFrame = nullptr;
static Bitmap* g_overlay = nullptr;  // cached, mostly-static interface layer (transparent over the video)
static bool g_dirty = true;
static std::vector<RECT> g_runs;      // overlay regions (inside the video area) that are not fully transparent
static HANDLE g_gameProc = nullptr;   // running game process, used to pause the video
static std::vector<Hot> g_hot;
static int g_hover = H_NONE;
static bool g_tracking = false;

// state
enum SetupScreen { SC_CHECK, SC_MAP, SC_ACCEPT };
static bool g_setupDone = false;
static SetupScreen g_screen = SC_CHECK;
static int g_checkTicks = 0;
static std::wstring g_setupErr, g_setupInfo, g_setupPath;
static bool g_loginOpen = false;
static std::string g_user;            // logged-in account ("" = logged out)
static bool g_remember = true;
static std::wstring g_loginErr, g_playErr, g_playState;
static fs::path g_wowExe;
static logic::Config g_cfg;

// ------------------------------------------------------------------- helpers
static std::wstring W(const std::string& u8) {
  int n = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  if (n) MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), &w[0], n);
  return w;
}
static std::string U8(const std::wstring& w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  if (n) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}
static Color C(unsigned rgb, BYTE a = 255) { return Color(a, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

static std::pair<const void*, DWORD> resData(const wchar_t* name) {
  HRSRC r = FindResourceW(g_inst, name, RT_RCDATA);
  if (!r) return {nullptr, 0};
  HGLOBAL h = LoadResource(g_inst, r);
  return {LockResource(h), SizeofResource(g_inst, r)};
}
static Bitmap* loadImg(const wchar_t* name) {
  auto [p, n] = resData(name);
  if (!p) return nullptr;
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
  memcpy(GlobalLock(h), p, n);
  GlobalUnlock(h);
  IStream* s = nullptr;
  if (CreateStreamOnHGlobal(h, TRUE, &s) != S_OK) return nullptr;
  Bitmap* out = nullptr;
  {
    Bitmap src(s);
    UINT w = src.GetWidth(), hgt = src.GetHeight();
    out = new Bitmap(w, hgt, PixelFormat32bppPARGB);
    Graphics g(out);
    g.SetCompositingMode(CompositingModeSourceCopy);
    g.DrawImage(&src, Rect(0, 0, (INT)w, (INT)hgt), 0, 0, (INT)w, (INT)hgt, UnitPixel);
  }
  s->Release();
  return out;
}
static void decodeFrame(uint32_t idx) {
  if (!g_vidCount) return;
  uint32_t a = g_vidOff[idx], n = g_vidOff[idx + 1] - a;
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
  if (!h) return;
  memcpy(GlobalLock(h), g_vidData + a, n);
  GlobalUnlock(h);
  IStream* s = nullptr;
  if (CreateStreamOnHGlobal(h, TRUE, &s) != S_OK) return;
  {
    Bitmap src(s);
    if (src.GetLastStatus() == Ok && src.GetWidth() == 1216 && src.GetHeight() >= 451) {
      Graphics g(g_vidFrame);
      g.SetCompositingMode(CompositingModeSourceCopy);
      g.DrawImage(&src, Rect(0, 0, 1216, 451), 0, 0, 1216, 451, UnitPixel);
    }
  }
  s->Release();
}
static void initVideo() {
  auto [p, n] = resData(L"VIDEO");
  if (!p || n < 8) return;
  const uint32_t* h = (const uint32_t*)p;
  g_vidFps = std::max<uint32_t>(1, std::min<uint32_t>(60, h[0]));
  g_vidCount = h[1];
  g_vidOff = h + 2;
  g_vidData = (const BYTE*)(h + 2 + g_vidCount + 1);
  if (!g_vidCount) return;
  g_vidFrame = new Bitmap(1216, 451, PixelFormat32bppPARGB);
  decodeFrame(0);
}

// Template provider for logic.hpp.
static std::string glueTemplate(const std::string& name) {
  const wchar_t* res = name == "AccountLogin.lua" ? L"G_LUA"
                       : name == "AccountLogin.xml" ? L"G_XML"
                       : name == "AccountLogin.lua.origin" ? L"G_LUAO" : L"G_XMLO";
  auto [p, n] = resData(res);
  return p ? std::string((const char*)p, n) : std::string();
}

static void openUrl(const wchar_t* url) { ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }

static std::wstring fmtErr(DWORD e) {
  wchar_t buf[512] = {0};
  FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, buf, 511, nullptr);
  std::wstring s = buf;
  while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
  return s.empty() ? L"error " + std::to_wstring(e) : s;
}

static fs::path configPath() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH * 2);
  fs::path base = n ? fs::path(buf) : fs::path(L".");
  return base / L"WarmaneLauncher" / L"config.ini";
}
static void saveCfg() { logic::saveConfig(configPath(), g_cfg); }

// -------------------------------------------------------------------- fonts
static Font* fnt(float px) {
  int k = (int)(px * 10);
  auto it = g_fonts.find(k);
  if (it != g_fonts.end()) return it->second;
  Font* f = new Font(g_family, px, FontStyleRegular, UnitPixel);
  g_fonts[k] = f;
  return f;
}
static void initFonts() {
  auto [p, n] = resData(L"FUTURA");
  g_pfc = new PrivateFontCollection();
  int found = 0;
  if (p) {
    DWORD cnt = 0;
    AddFontMemResourceEx((PVOID)p, n, nullptr, &cnt);  // for the GDI edit controls
    g_pfc->AddMemoryFont(p, (INT)n);
    FontFamily fam[4];
    g_pfc->GetFamilies(4, fam, &found);
    if (found > 0) g_family = fam[0].Clone();
    if (!g_family) {  // fallback: write to a temp file
      wchar_t tmp[MAX_PATH];
      GetTempPathW(MAX_PATH, tmp);
      std::wstring path = std::wstring(tmp) + L"warmane-futura.ttf";
      HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
      if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, p, n, &w, nullptr); CloseHandle(f); }
      g_pfc->AddFontFile(path.c_str());
      g_pfc->GetFamilies(4, fam, &found);
      if (found > 0) g_family = fam[0].Clone();
    }
  }
  if (!g_family) g_family = FontFamily::GenericSansSerif()->Clone();
}

// ------------------------------------------------------------ text primitives
static StringFormat* typo() {
  static StringFormat* f = nullptr;
  if (!f) {
    f = new StringFormat(StringFormat::GenericTypographic());
    f->SetFormatFlags(f->GetFormatFlags() | StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    f->SetLineAlignment(StringAlignmentCenter);
  }
  return f;
}
static float textW(const std::wstring& s, float px, float spacing = 0) {
  if (s.empty()) return 0;
  if (spacing > 0) {
    float w = 0;
    for (wchar_t c : s) {
      RectF b; std::wstring one(1, c);
      g_meas->MeasureString(one.c_str(), 1, fnt(px), PointF(0, 0), typo(), &b);
      w += b.Width + spacing;
    }
    return w;
  }
  RectF b;
  g_meas->MeasureString(s.c_str(), (INT)s.size(), fnt(px), PointF(0, 0), typo(), &b);
  return b.Width;
}
// Draw single-line text. cy = vertical centre. align: 0 left, 1 centre, 2 right (x is the anchor).
static void text(Graphics& g, const std::wstring& s, float x, float cy, float px, unsigned rgb,
                 int align = 0, float spacing = 0, bool shadow = false, float maxW = 0) {
  if (s.empty()) return;
  Font* f = fnt(px);
  StringFormat fmt(typo());
  fmt.SetLineAlignment(StringAlignmentCenter);
  if (maxW > 0) { fmt.SetTrimming(StringTrimmingEllipsisCharacter); }
  auto draw = [&](float ox, float oy, Color col) {
    SolidBrush br(col);
    if (spacing > 0) {
      float cx = ox;
      if (align == 1) cx = ox - textW(s, px, spacing) / 2;
      if (align == 2) cx = ox - textW(s, px, spacing);
      fmt.SetAlignment(StringAlignmentNear);
      for (wchar_t c : s) {
        std::wstring one(1, c);
        g.DrawString(one.c_str(), 1, f, RectF(cx, oy - 20, 60, 40), &fmt, &br);
        RectF b;
        g_meas->MeasureString(one.c_str(), 1, f, PointF(0, 0), typo(), &b);
        cx += b.Width + spacing;
      }
      return;
    }
    float w = maxW > 0 ? maxW : 1200;
    float rx = ox;
    fmt.SetAlignment(StringAlignmentNear);
    if (align == 1) { rx = ox - w / 2; fmt.SetAlignment(StringAlignmentCenter); }
    if (align == 2) { rx = ox - w; fmt.SetAlignment(StringAlignmentFar); }
    g.DrawString(s.c_str(), (INT)s.size(), f, RectF(rx, oy - 20, w, 40), &fmt, &br);
  };
  if (shadow) draw(x + 2, cy + 2, C(0x000000));
  draw(x, cy, C(rgb));
}
// Wrapped paragraph; returns its height.
static float para(Graphics& g, const std::wstring& s, float x, float y, float w, float px, unsigned rgb, bool measureOnly) {
  StringFormat fmt(StringFormat::GenericTypographic());
  fmt.SetFormatFlags(fmt.GetFormatFlags() & ~StringFormatFlagsNoWrap);
  RectF lay(x, y, w, 5000), out;
  g.MeasureString(s.c_str(), (INT)s.size(), fnt(px), lay, &fmt, &out);
  float h = std::max(out.Height, px * 1.3f);
  if (!measureOnly) {
    SolidBrush br(C(rgb));
    g.DrawString(s.c_str(), (INT)s.size(), fnt(px), RectF(x, y, w, h + 4), &fmt, &br);
  }
  return h;
}

// ------------------------------------------------------------ image primitives
static void blit(Graphics& g, Bitmap* b, int dx, int dy, int sx, int sy, int sw, int sh) {
  if (!b || sw <= 0 || sh <= 0) return;
  g.DrawImage(b, Rect(dx, dy, sw, sh), sx, sy, sw, sh, UnitPixel);
}
static void tileH(Graphics& g, Bitmap* b, int x0, int x1, int y, int sy, int sh) {
  int sw = b->GetWidth();
  for (int x = x0; x < x1; x += sw) blit(g, b, x, y, 0, sy, std::min(sw, x1 - x), sh);
}
static void tileV(Graphics& g, Bitmap* b, int x, int y0, int y1, int sx, int sw) {
  int sh = b->GetHeight();
  for (int y = y0; y < y1; y += sh) blit(g, b, x, y, sx, 0, sw, std::min(sh, y1 - y));
}
static void fillRR(Graphics& g, float x, float y, float w, float h, float r, Color c1, Color c2, bool grad) {
  GraphicsPath p;
  p.AddArc(x, y, 2 * r, 2 * r, 180, 90);
  p.AddArc(x + w - 2 * r, y, 2 * r, 2 * r, 270, 90);
  p.AddArc(x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, 0, 90);
  p.AddArc(x, y + h - 2 * r, 2 * r, 2 * r, 90, 90);
  p.CloseFigure();
  if (grad) { LinearGradientBrush b(PointF(x, y), PointF(x, y + h), c1, c2); g.FillPath(&b, &p); }
  else { SolidBrush b(c1); g.FillPath(&b, &p); }
}
static void rectLine(Graphics& g, float x, float y, float w, float h, unsigned rgb) {
  SolidBrush b(C(rgb)); g.FillRectangle(&b, x, y, w, h);
}
static void rectBorder(Graphics& g, float x, float y, float w, float h, unsigned border, unsigned fill) {
  rectLine(g, x, y, w, h, border);
  rectLine(g, x + 1, y + 1, w - 2, h - 2, fill);
}

static void hot(int id, float x, float y, float w, float h, bool hand = true) { g_hot.push_back({id, RectF(x, y, w, h), hand}); }

// Framed panel built from the border tiles.
static void framedBox(Graphics& g, int x, int y, int w, int h) {
  rectLine(g, (float)x, (float)y, (float)w, (float)h, 0x090909);
  // side/centre texture, scaled to panel width, repeated vertically
  float th = I.fcenter->GetHeight() * (w / (float)I.fcenter->GetWidth());
  g.SetClip(Rect(x, y + 8, w, h - 16));
  g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
  for (float yy = (float)(y + 8); yy < y + h - 8; yy += th)
    g.DrawImage(I.fcenter, RectF((float)x, yy, (float)w, th), 0, 0, (float)I.fcenter->GetWidth(), (float)I.fcenter->GetHeight(), UnitPixel);
  g.SetInterpolationMode(InterpolationModeNearestNeighbor);
  g.ResetClip();
  tileH(g, I.fheader, x + 12, x + w - 12, y, 0, 8);
  tileH(g, I.ffooter, x + 12, x + w - 12, y + h - 8, 4, 8);
  blit(g, I.ftl, x, y, 0, 0, 12, 12);
  blit(g, I.ftr, x + w - 12, y, 0, 0, 12, 12);
  blit(g, I.fbl, x, y + h - 12, 0, 0, 12, 12);
  blit(g, I.fbr, x + w - 12, y + h - 12, 0, 0, 12, 12);
}

static void button(Graphics& g, int id, const std::wstring& label, float x, float y, float w, float h, bool disabled = false) {
  bool hv = (g_hover == id) && !disabled;
  fillRR(g, x, y, w, h, 4, C(hv ? 0x1e1e1e : 0x1c1c1c), C(0x1a1a1a), true);
  text(g, label, x + w / 2, y + h / 2, 14, disabled ? 0x666666 : 0x999999, 1);
  if (!disabled) hot(id, x, y, w, h);
}

// ------------------------------------------------------------------ the scene
static bool setupVisible() { return !g_setupDone; }

// Moving part: base colour + current video frame (or the still). Drawn fresh every video tick.
static void drawBackdrop(Graphics& g) {
  g.SetCompositingMode(CompositingModeSourceCopy);
  g.SetInterpolationMode(InterpolationModeNearestNeighbor);
  g.SetPixelOffsetMode(PixelOffsetModeHalf);
  rectLine(g, 0, 0, WIN_W, WIN_H, 0x0F0F0F);
  if (g_vidFrame) {
    blit(g, g_vidFrame, 5, 8, 0, 0, 1216, 451);
  } else {
    float s = std::max(1216.f / I.hero->GetWidth(), 451.f / I.hero->GetHeight());
    float sw = 1216.f / s, sh = 451.f / s;
    float sx = (I.hero->GetWidth() - sw) / 2, sy = (I.hero->GetHeight() - sh) / 2;
    g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
    g.DrawImage(I.hero, RectF(5, 8, 1216, 451), sx, sy, sw, sh, UnitPixel);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);
  }
  g.SetCompositingMode(CompositingModeSourceOver);
}

// Static part (window frame): drawn into the cached overlay.
static void drawFrame(Graphics& g) {
  rectLine(g, 0, 0, WIN_W, 8, 0x0F0F0F);
  rectLine(g, 0, WIN_H - 8, WIN_W, 8, 0x0F0F0F);
  rectLine(g, 0, 8, 5, 451, 0x0F0F0F);
  rectLine(g, WIN_W - 5, 8, 5, 451, 0x0F0F0F);
  tileH(g, I.fheader, 12, WIN_W - 12, 0, 0, 8);
  tileH(g, I.ffooter, 12, WIN_W - 12, WIN_H - 8, 4, 8);
  tileV(g, I.flc, 0, 12, WIN_H - 12, 0, 7);
  tileV(g, I.frc, WIN_W - 7, 12, WIN_H - 12, 5, 7);
  blit(g, I.ftl, 0, 0, 0, 0, 12, 12);
  blit(g, I.ftr, WIN_W - 12, 0, 0, 0, 12, 12);
  blit(g, I.fbl, 0, WIN_H - 12, 0, 0, 12, 12);
  blit(g, I.fbr, WIN_W - 12, WIN_H - 12, 0, 0, 12, 12);
}

static void drawNav(Graphics& g) {
  const int NX = 109, NY = 64;
  g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
  g.DrawImage(I.navbar, Rect(NX, NY, 1008, 64), 0, 0, 1005, 64, UnitPixel);
  g.SetInterpolationMode(InterpolationModeNearestNeighbor);
  bool lh = g_hover == H_LOGO;
  blit(g, I.navlogo, NX + 15, NY - 29, 0, lh ? 125 : 0, 125, 125);
  hot(H_LOGO, (float)(NX + 15), (float)(NY - 29), 125, 125);

  std::vector<std::wstring> items;
  if (g_user.empty()) items = {L"CREATE ACCOUNT", L"DOWNLOAD", L"FORUM", L"INFORMATION", L"ARMORY", L"LOG IN"};
  else items = {L"MY ACCOUNT", L"DOWNLOAD", L"FORUM", L"INFORMATION", L"ARMORY", L"LOG OUT"};
  float right = (float)(NX + 1008 - 80);
  for (int i = (int)items.size() - 1; i >= 0; i--) {
    float w = textW(items[i], 15);
    float left = right - w;
    text(g, items[i], left, NY + 32, 15, g_hover == H_NAV0 + i ? 0xb87726 : 0x996019, 0, 0, true);
    hot(H_NAV0 + i, left, (float)NY, w, 64);
    right = left - 28;
  }
  if (!g_user.empty()) {
    std::wstring nm = W(g_user);
    float tw = std::min(170.f, std::max(110.f, textW(nm, 15)));
    float bw = tw + 26;
    rectBorder(g, (float)(NX + 150), (float)(NY + 14), bw, 36, 0x321e12, 0x180f08);
    text(g, nm, (float)(NX + 150 + 13), (float)(NY + 32), 15, g_hover == H_ACCT ? 0xb87726 : 0x996019, 0, 0, true, tw);
    hot(H_ACCT, (float)(NX + 150), (float)(NY + 14), bw, 36);
  }
}

// "Unofficial" label (top-left) and the GitHub link + disclaimer (bottom-right corner).
static void drawUnofficial(Graphics& g) {
  text(g, L"UNOFFICIAL WARMANE LAUNCHER", 262, 38, 13, 0xc1b575, 0, 2.34f, true);
  const float R = WIN_W - 18;
  text(g, L"Unofficial \x2014 not affiliated with Warmane", R, WIN_H - 40, 12, 0x7f7f7f, 2, 0, true);
  const std::wstring link = L"View on GitHub";
  float w = textW(link, 14);
  bool h = g_hover == H_GITHUB;
  text(g, link, R, WIN_H - 22, 14, h ? 0xe6d58a : 0xc1b575, 2, 0, true);
  rectLine(g, R - w, WIN_H - 13, w, 1, h ? 0xe6d58a : 0xc1b575);
  hot(H_GITHUB, R - w - 4, WIN_H - 34, w + 8, 26);
}

static void editBox(Graphics& g, HWND ed, float x, float y, float w, float h) {
  rectBorder(g, x, y, w, h, GetFocus() == ed ? 0x6b5732 : 0x1e1e1e, 0x000000);
}

static void drawLoginPanel(Graphics& g) {
  bool in = !g_user.empty();
  const int BX = 293, BY = 136, BW = 640;
  std::wstring msg = in ? (g_playErr.empty() ? g_playState : g_playErr) : g_loginErr;
  const int BH = 312;  // fixed height: status/error text lives in the right column instead
  framedBox(g, BX, BY, BW, BH);
  hot(H_LBBOX, BX, BY, BW, BH, false);
  text(g, in ? L"YOU ARE LOGGED IN" : L"LOG IN", 320, 164, 13, 0xc1b575, 0, 2.34f);
  rectLine(g, 300, 184, 626, 1, 0x1e1e1e);
  rectLine(g, 613, 185, 1, (float)(BY + BH - 8 - 185), 0x1e1e1e);
  if (!in) {
    text(g, L"Account name", 320, 210, 13, 0x606060);
    editBox(g, g_edName, 320, 222, 273, 37);
    text(g, L"Password", 320, 280, 13, 0x606060);
    editBox(g, g_edPass, 320, 291, 273, 38);
    // remember-me checkbox
    rectBorder(g, 320, 355, 13, 13, 0x444444, 0x000000);
    if (g_remember) {
      Pen p(C(0xc1b575), 2); g.SetSmoothingMode(SmoothingModeAntiAlias);
      g.DrawLine(&p, 322.5f, 361.5f, 325.5f, 365.f); g.DrawLine(&p, 325.5f, 365.f, 331.f, 357.5f);
      g.SetSmoothingMode(SmoothingModeNone);
    }
    text(g, L"Remember me", 340, 361, 13, 0x606060);
    hot(H_REMEMBER, 320, 352, 140, 20);
    button(g, H_LBGO, L"LOG IN", 320, 381, 193, 42);
    if (!msg.empty()) para(g, msg, 634, 388, 273, 14, 0xb05a4a, false);
    const wchar_t* help[4] = {L"Forgot your account password?", L"Forgot your account username?",
                              L"Did not receive account activation email?", L"Two factor authentication issues?"};
    for (int i = 0; i < 4; i++) {
      float cy = 210.f + 31 * i;
      text(g, help[i], 634, cy, 14, g_hover == H_HELP0 + i ? 0x688eb6 : 0x6185aa);
      hot(H_HELP0 + i, 634, cy - 9, textW(help[i], 14), 18);
    }
  } else {
    // left column: shortcuts to the game folders
    text(g, L"Game folders", 320, 210, 13, 0x606060);
    const wchar_t* fl[2] = {L"Open game folder", L"Open AddOns folder"};
    for (int i = 0; i < 2; i++) {
      float cy = 236.f + 31 * i;
      text(g, fl[i], 320, cy, 14, g_hover == H_OPENGAME + i ? 0x688eb6 : 0x6185aa);
      hot(H_OPENGAME + i, 320, cy - 9, textW(fl[i], 14), 18);
    }
    text(g, L"Install add-ons and edit config files here.", 320, 300, 12, 0x606060);
    // right column: realms and player counts
    static const struct { const wchar_t* n; const wchar_t* c; } realms[4] = {
        {L"Onyxia", L"3112 players"}, {L"Lordaeron", L"2900 players"},
        {L"Icecrown", L"9310 players"}, {L"Blackrock", L"655 players"}};
    for (int i = 0; i < 4; i++) {
      float cy = 213.f + 24 * i;
      blit(g, I.wotlk, 634, (int)(cy - 5), 0, 0, 32, 11);
      text(g, realms[i].n, 674, cy, 15, 0x5978a6);
      text(g, realms[i].c, 907, cy, 15, 0x73800b, 2);
    }
    rectLine(g, 634, 302, 273, 1, 0x1e1e1e);
    text(g, L"Total players online: 15977", 634, 320, 15, 0x73800b);
    para(g, L"Disclaimer \x2014 temporary information, may be inaccurate.", 634, 333, 273, 15, 0xc05040, false);
    button(g, H_PLAY, L"PLAY NOW", 320, 381, 193, 42);
    if (!msg.empty()) para(g, msg, 634, 392, 273, 14, g_playErr.empty() ? 0xc1b575 : 0xb05a4a, false);
  }
}

static void drawSetup(Graphics& g) {
  SolidBrush veil(C(0x050403, 0xF2));
  g.FillRectangle(&veil, 0, 0, WIN_W, WIN_H);
  hot(H_VEIL, 0, 0, WIN_W, WIN_H, false);

  const int BW = 580, BX = (WIN_W - BW) / 2, CX = BX + 7 + 22, CW = BW - 14 - 44;
  std::wstring dots(g_checkTicks % 4, L'.');
  struct Item { int kind; std::wstring s; unsigned col; };  // 0 para, 1 path box, 2 button
  std::vector<Item> items;
  std::wstring btnLabel;
  int btnId = 0;
  if (g_screen == SC_CHECK) {
    items.push_back({0, L"Checking" + dots, 0x606060});
    items.push_back({1, DEFAULT_EXE, 0xc1b575});
  } else if (g_screen == SC_MAP) {
    items.push_back({0, L"No installation was found at", 0x606060});
    items.push_back({1, DEFAULT_EXE, 0xc1b575});
    items.push_back({0, L"Point the launcher at your 3.3.5a Wow.exe once \x2014 it will be remembered.", 0x606060});
    if (!g_setupErr.empty()) items.push_back({0, g_setupErr, 0xb05a4a});
    btnLabel = L"LOCATE WOW.EXE"; btnId = H_LOCATE;
  } else {
    items.push_back({0, L"Wow.exe located:", 0x606060});
    items.push_back({1, g_setupPath, 0xc1b575});
    items.push_back({0, g_setupInfo, 0x606060});
    btnLabel = L"CONTINUE"; btnId = H_CONTINUE;
  }
  // pass 1: measure
  float y = 0;
  for (auto& it : items) {
    if (it.kind == 0) y += para(g, it.s, 0, 0, (float)CW, 14, it.col, true) + 12;
    else y += para(g, it.s, 0, 0, (float)CW - 24, 14, it.col, true) + 22 + 12;
  }
  if (btnId) y += 4 + 42;
  int BH = 8 + 12 + 17 + 12 + 1 + 20 + (int)y + 22 + 8;
  int BY = (WIN_H - BH) / 2;
  framedBox(g, BX, BY, BW, BH);
  hot(H_SETUPBOX, (float)BX, (float)BY, (float)BW, (float)BH, false);
  text(g, L"LAUNCHER SETUP", (float)(BX + 7 + 20), (float)(BY + 8 + 12 + 8), 13, 0xc1b575, 0, 2.34f);
  rectLine(g, (float)(BX + 7), (float)(BY + 8 + 12 + 17 + 12), (float)(BW - 14), 1, 0x1e1e1e);
  float cy = (float)(BY + 8 + 12 + 17 + 12 + 1 + 20);
  for (auto& it : items) {
    if (it.kind == 0) cy += para(g, it.s, (float)CX, cy, (float)CW, 14, it.col, false) + 12;
    else {
      float h = para(g, it.s, 0, 0, (float)CW - 24, 14, it.col, true) + 22;
      rectBorder(g, (float)CX, cy, (float)CW, h, 0x1e1e1e, 0x000000);
      para(g, it.s, (float)CX + 12, cy + 10, (float)CW - 24, 14, it.col, false);
      cy += h + 12;
    }
  }
  if (btnId) button(g, btnId, btnLabel, (float)CX, cy + 4, textW(btnLabel, 14) + 48, 42);
}

static void drawWinButtons(Graphics& g) {
  Bitmap* mn = g_hover == H_MIN ? I.bminh : I.bmin;
  Bitmap* cl = g_hover == H_CLOSE ? I.bcloseh : I.bclose;
  g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
  g.DrawImage(mn, Rect(WIN_W - 14 - 32 - 6 - 32, 14, 32, 28), 0, 0, 64, 56, UnitPixel);
  g.DrawImage(cl, Rect(WIN_W - 14 - 32, 14, 32, 28), 0, 0, 64, 56, UnitPixel);
  g.SetInterpolationMode(InterpolationModeNearestNeighbor);
  hot(H_MIN, (float)(WIN_W - 14 - 32 - 6 - 32), 14, 32, 28);
  hot(H_CLOSE, (float)(WIN_W - 14 - 32), 14, 32, 28);
}

// Find which 16px tiles of the overlay (inside the video area) contain any non-transparent pixel,
// so each video tick only has to alpha-blend those instead of the whole window.
static void scanRuns() {
  g_runs.clear();
  const int T = 16, VX = 5, VY = 8, VW = 1216, VH = 451;
  BitmapData bd;
  Rect all(0, 0, WIN_W, WIN_H);
  if (g_overlay->LockBits(&all, ImageLockModeRead, PixelFormat32bppPARGB, &bd) != Ok) {
    g_runs.push_back(RECT{VX, VY, VX + VW, VY + VH});
    return;
  }
  for (int ty = VY; ty < VY + VH; ty += T) {
    int th = std::min(T, VY + VH - ty);
    int runStart = -1;
    for (int tx = VX; tx <= VX + VW; tx += T) {
      bool any = false;
      if (tx < VX + VW) {
        int tw = std::min(T, VX + VW - tx);
        for (int y = ty; y < ty + th && !any; y++) {
          const BYTE* row = (const BYTE*)bd.Scan0 + (size_t)y * bd.Stride + (size_t)tx * 4;
          for (int x = 0; x < tw; x++) if (row[x * 4 + 3]) { any = true; break; }
        }
      }
      if (any && runStart < 0) runStart = tx;
      if (!any && runStart >= 0) { g_runs.push_back(RECT{runStart, ty, std::min(tx, VX + VW), ty + th}); runStart = -1; }
    }
  }
  g_overlay->UnlockBits(&bd);
}
static void composeFull() {
  Graphics bg(g_back);
  drawBackdrop(bg);
  bg.SetPixelOffsetMode(PixelOffsetModeHalf);
  bg.DrawImage(g_overlay, Rect(0, 0, WIN_W, WIN_H), 0, 0, WIN_W, WIN_H, UnitPixel);
}
static void composeVideo() {
  Graphics bg(g_back);
  bg.SetInterpolationMode(InterpolationModeNearestNeighbor);
  bg.SetPixelOffsetMode(PixelOffsetModeHalf);
  bg.SetCompositingMode(CompositingModeSourceCopy);
  blit(bg, g_vidFrame, 5, 8, 0, 0, 1216, 451);
  bg.SetCompositingMode(CompositingModeSourceOver);
  for (const RECT& r : g_runs)
    bg.DrawImage(g_overlay, Rect(r.left, r.top, r.right - r.left, r.bottom - r.top), r.left, r.top,
                 r.right - r.left, r.bottom - r.top, UnitPixel);
}

static void paintScene(Graphics& g) {
  g.SetTextRenderingHint(TextRenderingHintAntiAlias);
  g.SetInterpolationMode(InterpolationModeNearestNeighbor);
  g.SetPixelOffsetMode(PixelOffsetModeHalf);
  g_hot.clear();
  drawFrame(g);
  drawNav(g);
  drawUnofficial(g);
  if (setupVisible()) drawSetup(g);
  else if (!g_user.empty() || g_loginOpen) drawLoginPanel(g);
  drawWinButtons(g);
}

// ------------------------------------------------------------------- controls
static void syncControls() {
  bool show = g_setupDone && g_loginOpen && g_user.empty();
  ShowWindow(g_edName, show ? SW_SHOW : SW_HIDE);
  ShowWindow(g_edPass, show ? SW_SHOW : SW_HIDE);
}
static void touch(HWND h) { g_dirty = true; InvalidateRect(h, nullptr, FALSE); }
static void redraw() { syncControls(); touch(g_wnd); }

// -------------------------------------------------------------------- actions
static void setStatus(const std::wstring& s) {
  g_playState = s;
  SetTimer(g_wnd, 2, 6000, nullptr);
}

static bool setWow(const fs::path& p) {
  g_setupErr.clear();
  if (!logic::isWowExe(p)) {
    g_setupErr = p.filename().wstring() + L" is not Wow.exe \x2014 please select Wow.exe from your 3.3.5a client folder.";
    g_screen = SC_MAP; return false;
  }
  std::error_code ec;
  if (!fs::is_regular_file(p, ec)) { g_setupErr = p.wstring() + L" does not exist."; g_screen = SC_MAP; return false; }
  logic::GlueReport rep; std::string err;
  if (!logic::ensureGlue(p, glueTemplate, rep, err)) {
    g_setupErr = L"Could not write the login files into " + (p.parent_path() / L"Interface" / L"GlueXML").wstring() +
                 L" (" + W(err) + L"). Run the launcher as administrator, or move the client out of Program Files.";
    g_screen = SC_MAP; return false;
  }
  g_wowExe = p;
  g_cfg.wowExe = U8(p.wstring());
  saveCfg();
  g_setupPath = p.wstring();
  std::wstring info = L"Login interface ready \x2014 ";
  if (!rep.installed.empty()) info += std::to_wstring(rep.installed.size()) + L" copied";
  if (!rep.installed.empty() && !rep.present.empty()) info += L"; ";
  if (!rep.present.empty()) info += std::to_wstring(rep.present.size()) + L" already present";
  g_setupInfo = info + L".";
  g_screen = SC_ACCEPT;
  return true;
}

static void doLocate() {
  wchar_t file[MAX_PATH * 2] = L"";
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = g_wnd;
  ofn.lpstrFilter = L"Wow.exe\0Wow.exe\0Programs (*.exe)\0*.exe\0\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH * 2;
  ofn.lpstrTitle = L"Locate Wow.exe";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (GetOpenFileNameW(&ofn)) setWow(fs::path(file));
  redraw();
}

static void finishSetup() {
  g_setupDone = true;
  if (!g_cfg.account.empty()) { g_user = g_cfg.account; g_loginOpen = true; }
  redraw();
}

static std::string trim(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static std::string editText(HWND h) {
  wchar_t buf[256]; int n = GetWindowTextW(h, buf, 255); return U8(std::wstring(buf, n));
}

static void doLogin() {
  if (!g_setupDone || !g_user.empty()) return;
  g_loginErr.clear();
  std::string name = trim(editText(g_edName)), pw = editText(g_edPass);
  if (name.empty() || pw.empty()) { redraw(); return; }
  std::string err;
  if (!logic::writeCreds(g_wowExe, glueTemplate, name, pw, err)) {
    g_loginErr = L"Could not update the game login file: " + W(err);
    redraw(); return;
  }
  g_user = name;
  g_cfg.account = g_remember ? name : "";
  saveCfg();
  SetWindowTextW(g_edName, L""); SetWindowTextW(g_edPass, L"");
  g_loginOpen = false;
  redraw();
}

static void doLogout() {
  g_user.clear();
  g_cfg.account.clear();
  saveCfg();
  std::string err;
  logic::writeCreds(g_wowExe, glueTemplate, "", "", err);  // don't leave the password on disk
  g_loginOpen = true;
  g_playErr.clear(); g_playState.clear();
  redraw();
  SetFocus(g_edName);
}

static bool launchGame(const fs::path& exe, std::wstring& err) {
  std::wstring cmd = L"\"" + exe.wstring() + L"\"", dir = exe.parent_path().wstring();
  STARTUPINFOW si{}; si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
    CloseHandle(pi.hThread);
    if (g_gameProc) CloseHandle(g_gameProc);
    g_gameProc = pi.hProcess;
    return true;
  }
  DWORD e = GetLastError();
  if (e == ERROR_ELEVATION_REQUIRED) {  // client is flagged "run as administrator"
    HINSTANCE r = ShellExecuteW(g_wnd, L"runas", exe.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r > 32) return true;
  }
  err = fmtErr(e);
  return false;
}

static void doPlay() {
  g_playErr.clear(); g_playState.clear();
  logic::GlueReport rep; std::string err;
  if (!logic::ensureGlue(g_wowExe, glueTemplate, rep, err)) {
    g_playErr = L"Client files unavailable: " + W(err); redraw(); return;
  }
  std::wstring e;
  if (!launchGame(g_wowExe, e)) { g_playErr = L"Could not start Wow.exe: " + e; redraw(); return; }
  setStatus(L"World of Warcraft is starting\x2026");
  redraw();
}

// Open the game folder (where Wow.exe is) or Interface\AddOns in the file manager.
static void openFolder(bool addons) {
  if (g_wowExe.empty()) return;
  fs::path dir = g_wowExe.parent_path();
  if (addons) {
    dir = dir / L"Interface" / L"AddOns";
    std::error_code ec; fs::create_directories(dir, ec);
  }
  HINSTANCE r = ShellExecuteW(g_wnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  if ((INT_PTR)r <= 32) { setStatus(L"Could not open " + dir.wstring()); redraw(); }
}

static void onClick(int id) {
  switch (id) {
    case H_MIN: ShowWindow(g_wnd, SW_MINIMIZE); break;
    case H_CLOSE: DestroyWindow(g_wnd); break;
    case H_LOGO: openUrl(URL_HOME); break;
    case H_GITHUB: openUrl(URL_GITHUB); break;
    case H_OPENGAME: openFolder(false); break;
    case H_OPENADDONS: openFolder(true); break;
    case H_ACCT: break;
    case H_LBGO: doLogin(); break;
    case H_REMEMBER: g_remember = !g_remember; redraw(); break;
    case H_PLAY: doPlay(); break;
    case H_LOCATE: doLocate(); break;
    case H_CONTINUE: finishSetup(); break;
    default:
      if (id >= H_HELP0 && id < H_HELP0 + 4) openUrl(URL_RETRIEVE);
      else if (id >= H_NAV0 && id < H_NAV0 + 6) {
        int i = id - H_NAV0;
        static const wchar_t* out[6] = {L"https://www.warmane.com/account/create", L"https://www.warmane.com/download",
                                        L"https://forum.warmane.com/", L"https://www.warmane.com/information",
                                        L"http://armory.warmane.com/", nullptr};
        static const wchar_t* in[6] = {L"https://www.warmane.com/account", L"https://www.warmane.com/download",
                                       L"https://forum.warmane.com/", L"https://www.warmane.com/information",
                                       L"http://armory.warmane.com/", nullptr};
        if (i == 5) {
          if (g_user.empty()) { g_loginErr.clear(); g_loginOpen = true; redraw(); SetFocus(g_edName); }
          else doLogout();
        } else openUrl(g_user.empty() ? out[i] : in[i]);
      }
  }
}

static int hitTest(int x, int y) {
  for (int i = (int)g_hot.size() - 1; i >= 0; i--)
    if (g_hot[i].r.Contains((float)x, (float)y)) return g_hot[i].id;
  return H_NONE;
}
static bool hitIsHand(int x, int y) {
  for (int i = (int)g_hot.size() - 1; i >= 0; i--)
    if (g_hot[i].r.Contains((float)x, (float)y)) return g_hot[i].hand;
  return false;
}

// Boot: restore saved state.
static void boot() {
  g_cfg = logic::loadConfig(configPath());
  std::error_code ec;
  fs::path exe = g_cfg.wowExe.empty() ? fs::path() : fs::path(W(g_cfg.wowExe));
  if (!exe.empty() && fs::is_regular_file(exe, ec)) {
    g_wowExe = exe;
    // "Remember me": the account stored in the game file must still match.
    if (!g_cfg.account.empty()) {
      auto x = logic::readInstalledAccount(exe);
      if (x && (x->account != g_cfg.account || !x->hasPassword)) { g_cfg.account.clear(); saveCfg(); }
    }
    finishSetup();
    return;
  }
  g_cfg.wowExe.clear();
  {  // installed into the game folder? then Wow.exe sits right next to us
    wchar_t self[MAX_PATH * 2];
    if (GetModuleFileNameW(nullptr, self, MAX_PATH * 2)) {
      fs::path local = fs::path(self).parent_path() / L"Wow.exe";
      if (fs::is_regular_file(local, ec) && setWow(local)) { finishSetup(); return; }
      g_setupErr.clear();
    }
  }
  g_screen = SC_CHECK;
  g_checkTicks = 0;
  SetTimer(g_wnd, 1, 350, nullptr);
}

// ----------------------------------------------------------------- window proc
static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) { doLogin(); return 0; }
    if (w == VK_TAB) { SetFocus(h == g_edName ? g_edPass : g_edName); return 0; }
  }
  if (m == WM_CHAR && (w == VK_RETURN || w == VK_TAB)) return 0;
  return DefSubclassProc(h, m, w, l);
}

static HFONT g_editFont;
static HBRUSH g_blackBrush;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      g_wnd = h;
      g_editFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"FuturaEF Book");
      g_blackBrush = CreateSolidBrush(RGB(0, 0, 0));
      auto mk = [&](int y, DWORD extra, int limit) {
        HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | extra, 331, y + 9, 251, 20, h, nullptr, g_inst, nullptr);
        SendMessageW(e, WM_SETFONT, (WPARAM)g_editFont, TRUE);
        SendMessageW(e, EM_LIMITTEXT, limit, 0);
        SetWindowSubclass(e, EditProc, 1, 0);
        return e;
      };
      g_edName = mk(222, 0, 16);
      g_edPass = mk(291, ES_PASSWORD, 32);
      boot();
      if (g_vidCount) SetTimer(h, 3, 1000 / g_vidFps, nullptr);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      {
        if (g_dirty) {
          Graphics og(g_overlay);
          og.SetCompositingMode(CompositingModeSourceCopy);
          og.Clear(Color(0, 0, 0, 0));
          og.SetCompositingMode(CompositingModeSourceOver);
          paintScene(og);
          g_dirty = false;
          scanRuns();
          composeFull();
        }
        // g_back is always up to date (composeVideo runs on every video tick); just present it.
        const RECT& rc = ps.rcPaint;
        int w = rc.right - rc.left, hh = rc.bottom - rc.top;
        if (w > 0 && hh > 0) {
          Graphics scr(dc);
          scr.SetPixelOffsetMode(PixelOffsetModeHalf);
          scr.DrawImage(g_back, Rect(rc.left, rc.top, w, hh), rc.left, rc.top, w, hh, UnitPixel);
        }
      }
      EndPaint(h, &ps);
      return 0;
    }
    case WM_CTLCOLOREDIT: {
      HDC dc = (HDC)w;
      SetTextColor(dc, RGB(0xc1, 0xb5, 0x75));
      SetBkColor(dc, RGB(0, 0, 0));
      return (LRESULT)g_blackBrush;
    }
    case WM_COMMAND:
      if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) touch(h);
      return 0;
    case WM_MOUSEMOVE: {
      int x = (short)LOWORD(l), y = (short)HIWORD(l);
      int id = hitTest(x, y);
      if (!g_tracking) { TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0}; TrackMouseEvent(&t); g_tracking = true; }
      if (id != g_hover) { g_hover = id; touch(h); }
      return 0;
    }
    case WM_MOUSELEAVE:
      g_tracking = false;
      if (g_hover != H_NONE) { g_hover = H_NONE; touch(h); }
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(l) == HTCLIENT) {
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        SetCursor(LoadCursor(nullptr, hitIsHand(p.x, p.y) ? IDC_HAND : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN: {
      int x = (short)LOWORD(l), y = (short)HIWORD(l);
      int id = hitTest(x, y);
      // clicking outside the (logged-out) login panel closes it
      bool inPanel = id == H_LBBOX || id == H_LBGO || id == H_REMEMBER || (id >= H_HELP0 && id < H_HELP0 + 4);
      bool navLogin = id == H_NAV0 + 5 || id == H_NAV0;
      if (g_setupDone && g_loginOpen && g_user.empty() && !inPanel && !navLogin) { g_loginOpen = false; redraw(); }
      if (id == H_NONE) {  // empty area: drag the window
        ReleaseCapture();
        SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
      }
      onClick(id);
      return 0;
    }
    case WM_TIMER:
      if (w == 1) {
        g_checkTicks++;
        if (g_checkTicks >= 3) {
          KillTimer(h, 1);
          std::error_code ec;
          if (fs::is_regular_file(fs::path(DEFAULT_EXE), ec)) setWow(fs::path(DEFAULT_EXE));
          else g_screen = SC_MAP;
        }
        touch(h);
      } else if (w == 3) {
        bool gameRunning = false;
        if (g_gameProc) {
          if (WaitForSingleObject(g_gameProc, 0) == WAIT_TIMEOUT) gameRunning = true;
          else { CloseHandle(g_gameProc); g_gameProc = nullptr; }
        }
        // Only animate while visible, after setup (the setup veil hides the video) and
        // while the game is not running (don't burn CPU behind WoW).
        if (!IsIconic(h) && g_setupDone && !gameRunning && !g_dirty) {
          g_vidIdx = (g_vidIdx + 1) % g_vidCount;
          decodeFrame(g_vidIdx);
          composeVideo();
          RECT vr{5, 8, 1221, 459};
          InvalidateRect(h, &vr, FALSE);
        }
      } else if (w == 2) {
        KillTimer(h, 2); g_playState.clear(); touch(h);
      }
      return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  g_inst = inst;
  INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);
  GdiplusStartupInput in;
  GdiplusStartup(&g_gdiToken, &in, nullptr);
  g_measBmp = new Bitmap(4, 4, PixelFormat32bppPARGB);
  g_meas = new Graphics(g_measBmp);
  g_meas->SetTextRenderingHint(TextRenderingHintAntiAlias);
  initFonts();
  struct { Bitmap** b; const wchar_t* n; } list[] = {
      {&I.hero, L"HERO"}, {&I.navbar, L"NAVBAR"}, {&I.navlogo, L"NAVLOGO"}, {&I.wotlk, L"WOTLK"},
      {&I.ftl, L"FTL"}, {&I.ftr, L"FTR"}, {&I.fbl, L"FBL"}, {&I.fbr, L"FBR"}, {&I.fheader, L"FHEADER"},
      {&I.ffooter, L"FFOOTER"}, {&I.flc, L"FLC"}, {&I.frc, L"FRC"}, {&I.fcenter, L"FCENTER"},
      {&I.bmin, L"BMIN"}, {&I.bminh, L"BMINH"}, {&I.bclose, L"BCLOSE"}, {&I.bcloseh, L"BCLOSEH"}};
  for (auto& e : list) {
    *e.b = loadImg(e.n);
    if (!*e.b) { MessageBoxW(nullptr, (std::wstring(L"Missing resource ") + e.n).c_str(), APP_TITLE, MB_ICONERROR); return 1; }
  }
  initVideo();
  g_back = new Bitmap(WIN_W, WIN_H, PixelFormat32bppPARGB);
  g_overlay = new Bitmap(WIN_W, WIN_H, PixelFormat32bppPARGB);

  WNDCLASSEXW wc{sizeof wc};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  wc.hIconSm = wc.hIcon;
  wc.lpszClassName = L"WarmaneLauncherWnd";
  RegisterClassExW(&wc);
  int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
  HWND h = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, APP_TITLE,
                           WS_POPUP | WS_MINIMIZEBOX | WS_CLIPCHILDREN | WS_SYSMENU, (sx - WIN_W) / 2, (sy - WIN_H) / 2,
                           WIN_W, WIN_H, nullptr, nullptr, inst, nullptr);
  if (!h) return 1;
  ShowWindow(h, show);
  UpdateWindow(h);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  GdiplusShutdown(g_gdiToken);
  return 0;
}
