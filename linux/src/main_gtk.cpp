// Unofficial Warmane Launcher - Linux build (GTK4 + Cairo + Pango). Same look and layout as the Windows build:
// the whole window is one custom-drawn 1226x467 surface; only the two login text fields are real widgets.
#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "faugus.hpp"
#include "logic.hpp"

namespace fs = std::filesystem;

// ------------------------------------------------------------ embedded assets (see build.sh)
struct Blob { const unsigned char* p; size_t n; };
#define DECL(n) extern "C" const unsigned char asset_##n##_start[]; extern "C" const unsigned char asset_##n##_end[];
DECL(hero) DECL(navbar) DECL(navlogo) DECL(wotlk) DECL(ftl) DECL(ftr) DECL(fbl) DECL(fbr) DECL(fheader)
DECL(ffooter) DECL(flc) DECL(frc) DECL(fcenter) DECL(bmin) DECL(bminh) DECL(bclose) DECL(bcloseh) DECL(futura)
DECL(video) DECL(g_lua) DECL(g_xml) DECL(g_luao) DECL(g_xmlo)
#define BLOB(n) Blob{asset_##n##_start, (size_t)(asset_##n##_end - asset_##n##_start)}

// ------------------------------------------------------------------ constants
static const int WIN_W = 1226, WIN_H = 467;
static const char* URL_GITHUB = "https://github.com/Stradios/Unofficial-Warmane-Launcher";
static const char* APP_TITLE = "Unofficial Warmane Launcher";
static const char* URL_HOME = "https://www.warmane.com/";
static const char* URL_RETRIEVE = "https://www.warmane.com/account/retrieve";
static const char* FONT_FAMILY = "FuturaEF Book";

enum HotId {
  H_NONE = 0, H_MIN, H_CLOSE, H_LOGO, H_NAV0, /* 6 items: H_NAV0..H_NAV0+5 */ H_ACCT = H_NAV0 + 8,
  H_LBBOX, H_LBGO, H_REMEMBER, H_HELP0 /* 4 */, H_PLAY = H_HELP0 + 4,
  H_VEIL, H_SETUPBOX, H_LOCATE, H_CONTINUE, H_GITHUB, H_OPENGAME, H_OPENADDONS,
  H_UNINST, H_UNINST_APPIMG, H_UNINST_GO, H_UNINST_CANCEL, H_UNINST_CLOSE
};

// -------------------------------------------------------------------- globals
struct Img { cairo_surface_t* s = nullptr; int w = 0, h = 0; };
static struct {
  Img hero, navbar, navlogo, wotlk, ftl, ftr, fbl, fbr, fheader, ffooter, flc, frc, fcenter, bmin, bminh, bclose, bcloseh;
} I;
struct Hot { int id; double x, y, w, h; bool hand; };
struct Run { int x0, y0, x1, y1; };

static GtkWidget *g_win, *g_area, *g_edName, *g_edPass;
static bool g_focusName = false, g_focusPass = false;
static cairo_surface_t *g_back, *g_overlay, *g_vidFrame, *g_measSurf;
static cairo_t* g_meas;
static std::map<int, PangoFontDescription*> g_fonts;
static const unsigned char* g_vidData = nullptr;
static const uint32_t* g_vidOff = nullptr;
static uint32_t g_vidFps = 20, g_vidCount = 0, g_vidIdx = 0;
static bool g_dirty = true;
static std::vector<Run> g_runs;
static std::vector<Hot> g_hot;
static int g_hover = H_NONE;
static bool g_gameRunning = false;
static guint g_statusTimer = 0, g_checkTimer = 0;

enum SetupScreen { SC_CHECK, SC_MAP, SC_ACCEPT };
static bool g_setupDone = false;
static SetupScreen g_screen = SC_CHECK;
static int g_checkTicks = 0;
static std::string g_setupErr, g_setupInfo, g_setupPath;
static bool g_loginOpen = false;
static std::string g_user;
static bool g_remember = true;
static std::string g_loginErr, g_playErr, g_playState;
static fs::path g_wowExe;
static logic::Config g_cfg;
// uninstall dialog: 0 = closed, 1 = asking, 2 = finished
static int g_uninst = 0;
static bool g_delAppImage = false, g_noSave = false;
static logic::UninstallPlan g_plan;
static std::vector<std::string> g_uninstLog;

// ------------------------------------------------------------------- helpers
static void setColor(cairo_t* cr, unsigned rgb, int a = 255) {
  cairo_set_source_rgba(cr, ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0, a / 255.0);
}
static Img loadImg(Blob b) {
  Img im;
  GdkPixbufLoader* l = gdk_pixbuf_loader_new();
  GError* err = nullptr;
  if (gdk_pixbuf_loader_write(l, b.p, b.n, &err) && gdk_pixbuf_loader_close(l, &err)) {
    GdkPixbuf* pb = gdk_pixbuf_loader_get_pixbuf(l);
    if (pb) {
      im.w = gdk_pixbuf_get_width(pb); im.h = gdk_pixbuf_get_height(pb);
      im.s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, im.w, im.h);
      cairo_t* cr = cairo_create(im.s);
      cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
      gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
      cairo_paint(cr);
      cairo_destroy(cr);
    }
  }
  if (err) g_error_free(err);
  g_object_unref(l);
  return im;
}
static std::string glueTemplate(const std::string& name) {
  Blob b = name == "AccountLogin.lua" ? BLOB(g_lua) : name == "AccountLogin.xml" ? BLOB(g_xml)
         : name == "AccountLogin.lua.origin" ? BLOB(g_luao) : BLOB(g_xmlo);
  return std::string((const char*)b.p, b.n);
}
static void openUrl(const std::string& url) {
  GError* ge = nullptr;
  g_app_info_launch_default_for_uri(url.c_str(), nullptr, &ge);
  if (ge) g_error_free(ge);
}
static fs::path configPath() { return fs::path(g_get_user_config_dir()) / "unofficial-warmane-launcher" / "config.ini"; }
static void saveCfg() { if (!g_noSave) logic::saveConfig(configPath(), g_cfg); }
static fs::path cacheDir() { return fs::path(g_get_user_cache_dir()) / "unofficial-warmane-launcher"; }
static const char* appImagePath() { const char* a = getenv("APPIMAGE"); return a && *a ? a : nullptr; }

// ---------------------------------------------------------------- text (Pango)
static PangoFontDescription* fnt(float px) {
  int k = (int)(px * 10);
  auto it = g_fonts.find(k);
  if (it != g_fonts.end()) return it->second;
  PangoFontDescription* d = pango_font_description_new();
  pango_font_description_set_family(d, FONT_FAMILY);
  pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
  g_fonts[k] = d;
  return d;
}
static PangoLayout* layout(cairo_t* cr, const std::string& s, float px) {
  PangoLayout* l = pango_cairo_create_layout(cr);
  cairo_font_options_t* fo = cairo_font_options_create();
  cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
  cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_NONE);
  cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
  pango_cairo_context_set_font_options(pango_layout_get_context(l), fo);
  cairo_font_options_destroy(fo);
  pango_layout_set_font_description(l, fnt(px));
  pango_layout_set_text(l, s.c_str(), -1);
  return l;
}
static void extents(PangoLayout* l, double& w, double& h) {
  PangoRectangle r; pango_layout_get_extents(l, nullptr, &r);
  w = (double)r.width / PANGO_SCALE; h = (double)r.height / PANGO_SCALE;
}
static std::vector<std::string> glyphs(const std::string& s) {
  std::vector<std::string> v;
  for (const char* p = s.c_str(); *p; p = g_utf8_next_char(p)) v.emplace_back(p, g_utf8_next_char(p) - p);
  return v;
}
static float textW(const std::string& s, float px, float spacing = 0) {
  if (s.empty()) return 0;
  double w = 0, h = 0;
  if (spacing > 0) {
    float tot = 0;
    for (auto& c : glyphs(s)) {
      PangoLayout* l = layout(g_meas, c, px); extents(l, w, h); g_object_unref(l);
      tot += (float)w + spacing;
    }
    return tot;
  }
  PangoLayout* l = layout(g_meas, s, px); extents(l, w, h); g_object_unref(l);
  return (float)w;
}
// Single-line text. cy = vertical centre. align: 0 left, 1 centre, 2 right (x is the anchor).
static void text(cairo_t* cr, const std::string& s, float x, float cy, float px, unsigned rgb, int align = 0,
                 float spacing = 0, bool shadow = false, float maxW = 0) {
  if (s.empty()) return;
  auto draw = [&](float ox, float oy, unsigned col) {
    setColor(cr, col);
    if (spacing > 0) {
      float cx = ox;
      float tw = textW(s, px, spacing);
      if (align == 1) cx = ox - tw / 2;
      if (align == 2) cx = ox - tw;
      for (auto& c : glyphs(s)) {
        PangoLayout* l = layout(cr, c, px); double w, h; extents(l, w, h);
        cairo_move_to(cr, cx, oy - h / 2); pango_cairo_show_layout(cr, l); g_object_unref(l);
        cx += (float)w + spacing;
      }
      return;
    }
    PangoLayout* l = layout(cr, s, px); double w, h; extents(l, w, h);
    if (maxW > 0 && w > maxW) {
      pango_layout_set_width(l, (int)(maxW * PANGO_SCALE));
      pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
      w = maxW;
    }
    float lx = ox;
    if (align == 1) lx = ox - (float)w / 2;
    if (align == 2) lx = ox - (float)w;
    cairo_move_to(cr, lx, oy - h / 2); pango_cairo_show_layout(cr, l); g_object_unref(l);
  };
  if (shadow) draw(x + 2, cy + 2, 0x000000);
  draw(x, cy, rgb);
}
// Wrapped paragraph; returns its height.
static float para(cairo_t* cr, const std::string& s, float x, float y, float w, float px, unsigned rgb, bool measureOnly) {
  PangoLayout* l = layout(cr, s, px);
  pango_layout_set_width(l, (int)(w * PANGO_SCALE));
  pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
  double lw, lh; extents(l, lw, lh);
  float h = std::max((float)lh, px * 1.3f);
  if (!measureOnly) { setColor(cr, rgb); cairo_move_to(cr, x, y); pango_cairo_show_layout(cr, l); }
  g_object_unref(l);
  return h;
}

// ---------------------------------------------------------------- shapes / images
static void rectLine(cairo_t* cr, double x, double y, double w, double h, unsigned rgb, int a = 255) {
  setColor(cr, rgb, a); cairo_rectangle(cr, x, y, w, h); cairo_fill(cr);
}
static void rectBorder(cairo_t* cr, double x, double y, double w, double h, unsigned border, unsigned fill) {
  rectLine(cr, x, y, w, h, border); rectLine(cr, x + 1, y + 1, w - 2, h - 2, fill);
}
// Copy a source rectangle of an image to (dx,dy), 1:1.
static void blit(cairo_t* cr, const Img& im, int dx, int dy, int sx, int sy, int sw, int sh) {
  if (!im.s || sw <= 0 || sh <= 0) return;
  cairo_save(cr);
  cairo_rectangle(cr, dx, dy, sw, sh); cairo_clip(cr);
  cairo_set_source_surface(cr, im.s, dx - sx, dy - sy);
  cairo_paint(cr);
  cairo_restore(cr);
}
// Scaled copy of a source rectangle into a destination rectangle (smooth).
static void drawScaled(cairo_t* cr, const Img& im, double dx, double dy, double dw, double dh,
                       double sx, double sy, double sw, double sh) {
  if (!im.s) return;
  cairo_save(cr);
  cairo_rectangle(cr, dx, dy, dw, dh); cairo_clip(cr);
  cairo_translate(cr, dx, dy);
  cairo_scale(cr, dw / sw, dh / sh);
  cairo_set_source_surface(cr, im.s, -sx, -sy);
  cairo_pattern_t* p = cairo_get_source(cr);
  cairo_pattern_set_filter(p, CAIRO_FILTER_BEST);
  cairo_pattern_set_extend(p, CAIRO_EXTEND_PAD);
  cairo_paint(cr);
  cairo_restore(cr);
}
static void tileH(cairo_t* cr, const Img& b, int x0, int x1, int y, int sy, int sh) {
  for (int x = x0; x < x1; x += b.w) blit(cr, b, x, y, 0, sy, std::min(b.w, x1 - x), sh);
}
static void tileV(cairo_t* cr, const Img& b, int x, int y0, int y1, int sx, int sw) {
  for (int y = y0; y < y1; y += b.h) blit(cr, b, x, y, sx, 0, sw, std::min(b.h, y1 - y));
}
static void fillRR(cairo_t* cr, double x, double y, double w, double h, double r, unsigned c1, unsigned c2, bool grad) {
  cairo_new_path(cr);
  cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
  cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2 * M_PI);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, 0.5 * M_PI);
  cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);
  cairo_close_path(cr);
  if (grad) {
    cairo_pattern_t* p = cairo_pattern_create_linear(x, y, x, y + h);
    cairo_pattern_add_color_stop_rgb(p, 0, ((c1 >> 16) & 255) / 255.0, ((c1 >> 8) & 255) / 255.0, (c1 & 255) / 255.0);
    cairo_pattern_add_color_stop_rgb(p, 1, ((c2 >> 16) & 255) / 255.0, ((c2 >> 8) & 255) / 255.0, (c2 & 255) / 255.0);
    cairo_set_source(cr, p); cairo_fill(cr); cairo_pattern_destroy(p);
  } else { setColor(cr, c1); cairo_fill(cr); }
}
static void hot(int id, double x, double y, double w, double h, bool hand = true) { g_hot.push_back({id, x, y, w, h, hand}); }

// Framed panel built from the border tiles.
static void framedBox(cairo_t* cr, int x, int y, int w, int h) {
  rectLine(cr, x, y, w, h, 0x090909);
  double th = I.fcenter.h * (w / (double)I.fcenter.w);
  cairo_save(cr);
  cairo_rectangle(cr, x, y + 8, w, h - 16); cairo_clip(cr);
  for (double yy = y + 8; yy < y + h - 8; yy += th)
    drawScaled(cr, I.fcenter, x, yy, w, th, 0, 0, I.fcenter.w, I.fcenter.h);
  cairo_restore(cr);
  tileH(cr, I.fheader, x + 12, x + w - 12, y, 0, 8);
  tileH(cr, I.ffooter, x + 12, x + w - 12, y + h - 8, 4, 8);
  blit(cr, I.ftl, x, y, 0, 0, 12, 12);
  blit(cr, I.ftr, x + w - 12, y, 0, 0, 12, 12);
  blit(cr, I.fbl, x, y + h - 12, 0, 0, 12, 12);
  blit(cr, I.fbr, x + w - 12, y + h - 12, 0, 0, 12, 12);
}
static void button(cairo_t* cr, int id, const std::string& label, float x, float y, float w, float h, bool disabled = false) {
  bool hv = (g_hover == id) && !disabled;
  fillRR(cr, x, y, w, h, 4, hv ? 0x1e1e1e : 0x1c1c1c, 0x1a1a1a, true);
  text(cr, label, x + w / 2, y + h / 2, 14, disabled ? 0x666666 : 0x999999, 1);
  if (!disabled) hot(id, x, y, w, h);
}

// ------------------------------------------------------------------ the scene
static bool setupVisible() { return !g_setupDone || g_uninst; }

static void drawBackdrop(cairo_t* cr) {
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  rectLine(cr, 0, 0, WIN_W, WIN_H, 0x0F0F0F);
  if (g_vidFrame) {
    cairo_set_source_surface(cr, g_vidFrame, 5, 8);
    cairo_rectangle(cr, 5, 8, 1216, 451); cairo_fill(cr);
  } else if (I.hero.s) {
    double s = std::max(1216.0 / I.hero.w, 451.0 / I.hero.h), sw = 1216.0 / s, sh = 451.0 / s;
    drawScaled(cr, I.hero, 5, 8, 1216, 451, (I.hero.w - sw) / 2, (I.hero.h - sh) / 2, sw, sh);
  }
  cairo_restore(cr);
}
static void drawFrame(cairo_t* cr) {
  rectLine(cr, 0, 0, WIN_W, 8, 0x0F0F0F);
  rectLine(cr, 0, WIN_H - 8, WIN_W, 8, 0x0F0F0F);
  rectLine(cr, 0, 8, 5, 451, 0x0F0F0F);
  rectLine(cr, WIN_W - 5, 8, 5, 451, 0x0F0F0F);
  tileH(cr, I.fheader, 12, WIN_W - 12, 0, 0, 8);
  tileH(cr, I.ffooter, 12, WIN_W - 12, WIN_H - 8, 4, 8);
  tileV(cr, I.flc, 0, 12, WIN_H - 12, 0, 7);
  tileV(cr, I.frc, WIN_W - 7, 12, WIN_H - 12, 5, 7);
  blit(cr, I.ftl, 0, 0, 0, 0, 12, 12);
  blit(cr, I.ftr, WIN_W - 12, 0, 0, 0, 12, 12);
  blit(cr, I.fbl, 0, WIN_H - 12, 0, 0, 12, 12);
  blit(cr, I.fbr, WIN_W - 12, WIN_H - 12, 0, 0, 12, 12);
}
static void drawNav(cairo_t* cr) {
  const int NX = 109, NY = 64;
  drawScaled(cr, I.navbar, NX, NY, 1008, 64, 0, 0, 1005, 64);
  bool lh = g_hover == H_LOGO;
  blit(cr, I.navlogo, NX + 15, NY - 29, 0, lh ? 125 : 0, 125, 125);
  hot(H_LOGO, NX + 15, NY - 29, 125, 125);

  std::vector<std::string> items;
  if (g_user.empty()) items = {"CREATE ACCOUNT", "DOWNLOAD", "FORUM", "INFORMATION", "ARMORY", "LOG IN"};
  else items = {"MY ACCOUNT", "DOWNLOAD", "FORUM", "INFORMATION", "ARMORY", "LOG OUT"};
  float right = (float)(NX + 1008 - 80);
  for (int i = (int)items.size() - 1; i >= 0; i--) {
    float w = textW(items[i], 15);
    float left = right - w;
    text(cr, items[i], left, NY + 32, 15, g_hover == H_NAV0 + i ? 0xb87726 : 0x996019, 0, 0, true);
    hot(H_NAV0 + i, left, NY, w, 64);
    right = left - 28;
  }
  if (!g_user.empty()) {
    float tw = std::min(170.f, std::max(110.f, textW(g_user, 15)));
    float bw = tw + 26;
    rectBorder(cr, NX + 150, NY + 14, bw, 36, 0x321e12, 0x180f08);
    text(cr, g_user, NX + 150 + 13, NY + 32, 15, g_hover == H_ACCT ? 0xb87726 : 0x996019, 0, 0, true, tw);
    hot(H_ACCT, NX + 150, NY + 14, bw, 36);
  }
}
// "Unofficial" label (top-left) and the GitHub link + disclaimer (bottom-right corner).
static void drawUnofficial(cairo_t* cr) {
  text(cr, "UNOFFICIAL WARMANE LAUNCHER", 262, 38, 13, 0xc1b575, 0, 2.34f, true);
  const float R = WIN_W - 18;
  text(cr, "Unofficial — not affiliated with Warmane", R, WIN_H - 40, 12, 0x7f7f7f, 2, 0, true);
  const std::string link = "View on GitHub";
  float w = textW(link, 14);
  bool h = g_hover == H_GITHUB;
  text(cr, link, R, WIN_H - 22, 14, h ? 0xe6d58a : 0xc1b575, 2, 0, true);
  rectLine(cr, R - w, WIN_H - 13, w, 1, h ? 0xe6d58a : 0xc1b575);
  hot(H_GITHUB, R - w - 4, WIN_H - 34, w + 8, 26);
  if (!g_uninst) {  // small "Uninstall…" link, bottom-left
    const std::string u = "Uninstall…";
    float uw = textW(u, 14);
    bool uh = g_hover == H_UNINST;
    text(cr, u, 18, WIN_H - 22, 14, uh ? 0xe6d58a : 0x8a7f55, 0, 0, true);
    rectLine(cr, 18, WIN_H - 13, uw, 1, uh ? 0xe6d58a : 0x8a7f55);
    hot(H_UNINST, 14, WIN_H - 34, uw + 8, 26);
  }
}
static void editBox(cairo_t* cr, bool focused, float x, float y, float w, float h) {
  rectBorder(cr, x, y, w, h, focused ? 0x6b5732 : 0x1e1e1e, 0x000000);
}
static void drawLoginPanel(cairo_t* cr) {
  bool in = !g_user.empty();
  const int BX = 293, BY = 136, BW = 640;
  std::string msg = in ? (g_playErr.empty() ? g_playState : g_playErr) : g_loginErr;
  const int BH = 312;  // fixed height: status/error text lives in the right column instead
  framedBox(cr, BX, BY, BW, BH);
  hot(H_LBBOX, BX, BY, BW, BH, false);
  text(cr, in ? "YOU ARE LOGGED IN" : "LOG IN", 320, 164, 13, 0xc1b575, 0, 2.34f);
  rectLine(cr, 300, 184, 626, 1, 0x1e1e1e);
  rectLine(cr, 613, 185, 1, (float)(BY + BH - 8 - 185), 0x1e1e1e);
  if (!in) {
    text(cr, "Account name", 320, 210, 13, 0x606060);
    editBox(cr, g_focusName, 320, 222, 273, 37);
    text(cr, "Password", 320, 280, 13, 0x606060);
    editBox(cr, g_focusPass, 320, 291, 273, 38);
    rectBorder(cr, 320, 355, 13, 13, 0x444444, 0x000000);
    if (g_remember) {
      setColor(cr, 0xc1b575); cairo_set_line_width(cr, 2); cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
      cairo_move_to(cr, 322.5, 361.5); cairo_line_to(cr, 325.5, 365); cairo_line_to(cr, 331, 357.5); cairo_stroke(cr);
    }
    text(cr, "Remember me", 340, 361, 13, 0x606060);
    hot(H_REMEMBER, 320, 352, 140, 20);
    button(cr, H_LBGO, "LOG IN", 320, 381, 193, 42);
    if (!msg.empty()) para(cr, msg, 634, 388, 273, 14, 0xb05a4a, false);
    const char* help[4] = {"Forgot your account password?", "Forgot your account username?",
                           "Did not receive account activation email?", "Two factor authentication issues?"};
    for (int i = 0; i < 4; i++) {
      float cy = 210.f + 31 * i;
      text(cr, help[i], 634, cy, 14, g_hover == H_HELP0 + i ? 0x688eb6 : 0x6185aa);
      hot(H_HELP0 + i, 634, cy - 9, textW(help[i], 14), 18);
    }
  } else {
    text(cr, "Game folders", 320, 210, 13, 0x606060);
    const char* fl[2] = {"Open game folder", "Open AddOns folder"};
    for (int i = 0; i < 2; i++) {
      float cy = 236.f + 31 * i;
      text(cr, fl[i], 320, cy, 14, g_hover == H_OPENGAME + i ? 0x688eb6 : 0x6185aa);
      hot(H_OPENGAME + i, 320, cy - 9, textW(fl[i], 14), 18);
    }
    text(cr, "Install add-ons and edit config files here.", 320, 300, 12, 0x606060);
    static const struct { const char* n; const char* c; } realms[4] = {
        {"Onyxia", "3112 players"}, {"Lordaeron", "2900 players"}, {"Icecrown", "9310 players"}, {"Blackrock", "655 players"}};
    for (int i = 0; i < 4; i++) {
      float cy = 213.f + 24 * i;
      blit(cr, I.wotlk, 634, (int)(cy - 5), 0, 0, 32, 11);
      text(cr, realms[i].n, 674, cy, 15, 0x5978a6);
      text(cr, realms[i].c, 907, cy, 15, 0x73800b, 2);
    }
    rectLine(cr, 634, 302, 273, 1, 0x1e1e1e);
    text(cr, "Total players online: 15977", 634, 320, 15, 0x73800b);
    para(cr, "Disclaimer — temporary information, may be inaccurate.", 634, 333, 273, 15, 0xc05040, false);
    button(cr, H_PLAY, "PLAY NOW", 320, 381, 193, 42);
    if (!msg.empty()) para(cr, msg, 634, 392, 273, 14, g_playErr.empty() ? 0xc1b575 : 0xb05a4a, false);
  }
}
static void drawSetup(cairo_t* cr) {
  rectLine(cr, 0, 0, WIN_W, WIN_H, 0x050403, 0xF2);
  hot(H_VEIL, 0, 0, WIN_W, WIN_H, false);
  const int BW = 580, BX = (WIN_W - BW) / 2, CX = BX + 7 + 22, CW = BW - 14 - 44;
  std::string dots(g_checkTicks % 4, '.');
  struct Item { int kind; std::string s; unsigned col; };  // 0 para, 1 path box
  std::vector<Item> items;
  std::string btnLabel;
  int btnId = 0;
  if (g_screen == SC_CHECK) {
    items.push_back({0, "Looking for World of Warcraft in your Faugus library" + dots, 0x606060});
    items.push_back({1, (fs::path(faugus::xdgData()) / "faugus-launcher/games.json").string(), 0xc1b575});
  } else if (g_screen == SC_MAP) {
    items.push_back({0, "No 3.3.5a installation was found in your Faugus library.", 0x606060});
    items.push_back({0, "Point the launcher at your 3.3.5a Wow.exe once — it will be remembered.", 0x606060});
    if (!g_setupErr.empty()) items.push_back({0, g_setupErr, 0xb05a4a});
    btnLabel = "LOCATE WOW.EXE"; btnId = H_LOCATE;
  } else {
    items.push_back({0, "Wow.exe located:", 0x606060});
    items.push_back({1, g_setupPath, 0xc1b575});
    items.push_back({0, g_setupInfo, 0x606060});
    btnLabel = "CONTINUE"; btnId = H_CONTINUE;
  }
  float y = 0;
  for (auto& it : items) {
    if (it.kind == 0) y += para(cr, it.s, 0, 0, CW, 14, it.col, true) + 12;
    else y += para(cr, it.s, 0, 0, CW - 24, 14, it.col, true) + 22 + 12;
  }
  if (btnId) y += 4 + 42;
  int BH = 8 + 12 + 17 + 12 + 1 + 20 + (int)y + 22 + 8;
  int BY = (WIN_H - BH) / 2;
  framedBox(cr, BX, BY, BW, BH);
  hot(H_SETUPBOX, BX, BY, BW, BH, false);
  text(cr, "LAUNCHER SETUP", BX + 7 + 20, BY + 8 + 12 + 8, 13, 0xc1b575, 0, 2.34f);
  rectLine(cr, BX + 7, BY + 8 + 12 + 17 + 12, BW - 14, 1, 0x1e1e1e);
  float cy = BY + 8 + 12 + 17 + 12 + 1 + 20;
  for (auto& it : items) {
    if (it.kind == 0) cy += para(cr, it.s, CX, cy, CW, 14, it.col, false) + 12;
    else {
      float h = para(cr, it.s, 0, 0, CW - 24, 14, it.col, true) + 22;
      rectBorder(cr, CX, cy, CW, h, 0x1e1e1e, 0x000000);
      para(cr, it.s, CX + 12, cy + 10, CW - 24, 14, it.col, false);
      cy += h + 12;
    }
  }
  if (btnId) button(cr, btnId, btnLabel, CX, cy + 4, textW(btnLabel, 14) + 48, 42);
}
static void drawUninstall(cairo_t* cr) {
  rectLine(cr, 0, 0, WIN_W, WIN_H, 0x050403, 0xF2);
  hot(H_VEIL, 0, 0, WIN_W, WIN_H, false);
  const int BW = 640, BX = (WIN_W - BW) / 2, CX = BX + 7 + 22, CW = BW - 14 - 44;
  std::vector<std::string> lines;
  if (g_uninst == 1) {
    lines.push_back("This removes everything the launcher added, so nothing is left behind:");
    std::string d = logic::describePlan(g_plan), one;
    for (char c : d) { if (c == '\n') { if (!one.empty()) lines.push_back(one); one.clear(); } else one += c; }
    if (!one.empty()) lines.push_back(one);
    lines.push_back("Your game, your add-ons and your characters are not touched.");
  } else lines = g_uninstLog;
  float y = 0;
  for (auto& l : lines) y += para(cr, l, 0, 0, CW, 14, 0x606060, true) + 8;
  bool askAI = g_uninst == 1 && appImagePath();
  if (askAI) y += 24 + 8;
  y += 12 + 42;
  int BH = 8 + 12 + 17 + 12 + 1 + 20 + (int)y + 22 + 8;
  int BY = (WIN_H - BH) / 2;
  framedBox(cr, BX, BY, BW, BH);
  hot(H_SETUPBOX, BX, BY, BW, BH, false);
  text(cr, g_uninst == 1 ? "UNINSTALL" : "UNINSTALL FINISHED", BX + 7 + 20, BY + 8 + 12 + 8, 13, 0xc1b575, 0, 2.34f);
  rectLine(cr, BX + 7, BY + 8 + 12 + 17 + 12, BW - 14, 1, 0x1e1e1e);
  float cy = BY + 8 + 12 + 17 + 12 + 1 + 20;
  for (size_t i = 0; i < lines.size(); i++) {
    unsigned col = (g_uninst == 1 && i == 0) ? 0xc1b575 : 0x909090;
    cy += para(cr, lines[i], CX, cy, CW, 14, col, false) + 8;
  }
  if (askAI) {
    bool h = g_hover == H_UNINST_APPIMG;
    rectBorder(cr, CX, cy + 2, 18, 18, h ? 0x6b5732 : 0x1e1e1e, 0x000000);
    if (g_delAppImage) rectLine(cr, CX + 5, cy + 7, 8, 8, 0xc1b575);
    text(cr, "Also delete the AppImage file itself", CX + 28, cy + 11, 14, 0xc1b575, 1);
    hot(H_UNINST_APPIMG, CX, cy, 340, 24);
    cy += 24 + 8;
  }
  cy += 12;
  if (g_uninst == 1) {
    button(cr, H_UNINST_GO, "REMOVE EVERYTHING", CX, cy, textW("REMOVE EVERYTHING", 14) + 48, 42);
    float x2 = CX + textW("REMOVE EVERYTHING", 14) + 48 + 12;
    button(cr, H_UNINST_CANCEL, "CANCEL", x2, cy, textW("CANCEL", 14) + 48, 42);
  } else button(cr, H_UNINST_CLOSE, "CLOSE", CX, cy, textW("CLOSE", 14) + 48, 42);
}
static void drawWinButtons(cairo_t* cr) {
  const Img& mn = g_hover == H_MIN ? I.bminh : I.bmin;
  const Img& cl = g_hover == H_CLOSE ? I.bcloseh : I.bclose;
  drawScaled(cr, mn, WIN_W - 14 - 32 - 6 - 32, 14, 32, 28, 0, 0, 64, 56);
  drawScaled(cr, cl, WIN_W - 14 - 32, 14, 32, 28, 0, 0, 64, 56);
  hot(H_MIN, WIN_W - 14 - 32 - 6 - 32, 14, 32, 28);
  hot(H_CLOSE, WIN_W - 14 - 32, 14, 32, 28);
}

// Which 16px tiles of the overlay (inside the video area) are not fully transparent: each video tick only
// has to blend those instead of the whole window.
static void scanRuns() {
  g_runs.clear();
  const int T = 16, VX = 5, VY = 8, VW = 1216, VH = 451;
  cairo_surface_flush(g_overlay);
  const unsigned char* data = cairo_image_surface_get_data(g_overlay);
  int stride = cairo_image_surface_get_stride(g_overlay);
  for (int ty = VY; ty < VY + VH; ty += T) {
    int th = std::min(T, VY + VH - ty), runStart = -1;
    for (int tx = VX; tx <= VX + VW; tx += T) {
      bool any = false;
      if (tx < VX + VW) {
        int tw = std::min(T, VX + VW - tx);
        for (int y = ty; y < ty + th && !any; y++) {
          const unsigned char* row = data + (size_t)y * stride + (size_t)tx * 4;
          for (int x = 0; x < tw; x++) if (row[x * 4 + 3]) { any = true; break; }
        }
      }
      if (any && runStart < 0) runStart = tx;
      if (!any && runStart >= 0) { g_runs.push_back({runStart, ty, std::min(tx, VX + VW), ty + th}); runStart = -1; }
    }
  }
}
static void composeFull() {
  cairo_t* cr = cairo_create(g_back);
  drawBackdrop(cr);
  cairo_set_source_surface(cr, g_overlay, 0, 0); cairo_paint(cr);
  cairo_destroy(cr);
}
static void composeVideo() {
  cairo_t* cr = cairo_create(g_back);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, g_vidFrame, 5, 8);
  cairo_rectangle(cr, 5, 8, 1216, 451); cairo_fill(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  for (const Run& r : g_runs) {
    cairo_set_source_surface(cr, g_overlay, 0, 0);
    cairo_rectangle(cr, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0); cairo_fill(cr);
  }
  cairo_destroy(cr);
}
static void paintScene(cairo_t* cr) {
  g_hot.clear();
  drawFrame(cr);
  drawNav(cr);
  drawUnofficial(cr);
  if (g_uninst) drawUninstall(cr);
  else if (setupVisible()) drawSetup(cr);
  else if (!g_user.empty() || g_loginOpen) drawLoginPanel(cr);
  drawWinButtons(cr);
}
static void renderOverlay() {
  cairo_t* cr = cairo_create(g_overlay);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR); cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  paintScene(cr);
  cairo_destroy(cr);
  cairo_surface_flush(g_overlay);
  g_dirty = false;
  scanRuns();
  composeFull();
}

// ------------------------------------------------------------------- video
static void decodeFrame(uint32_t idx) {
  if (!g_vidCount) return;
  uint32_t a = g_vidOff[idx], n = g_vidOff[idx + 1] - a;
  GdkPixbufLoader* l = gdk_pixbuf_loader_new_with_type("jpeg", nullptr);
  if (!l) return;
  if (gdk_pixbuf_loader_write(l, g_vidData + a, n, nullptr) && gdk_pixbuf_loader_close(l, nullptr)) {
    GdkPixbuf* pb = gdk_pixbuf_loader_get_pixbuf(l);
    if (pb && gdk_pixbuf_get_width(pb) == 1216 && gdk_pixbuf_get_height(pb) >= 451) {
      cairo_t* cr = cairo_create(g_vidFrame);
      cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
      gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
      cairo_rectangle(cr, 0, 0, 1216, 451); cairo_fill(cr);
      cairo_destroy(cr);
    }
  }
  g_object_unref(l);
}
static void initVideo() {
  Blob b = BLOB(video);
  if (b.n < 8) return;
  const uint32_t* h = (const uint32_t*)b.p;
  g_vidFps = std::max<uint32_t>(1, std::min<uint32_t>(60, h[0]));
  g_vidCount = h[1];
  g_vidOff = h + 2;
  g_vidData = (const unsigned char*)(h + 2 + g_vidCount + 1);
  if (!g_vidCount) return;
  g_vidFrame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1216, 451);
  decodeFrame(0);
}

// ------------------------------------------------------------------- controls
static void syncControls() {
  bool show = g_setupDone && !g_uninst && g_loginOpen && g_user.empty();
  gtk_widget_set_visible(g_edName, show);
  gtk_widget_set_visible(g_edPass, show);
}
static void touch() { g_dirty = true; gtk_widget_queue_draw(g_area); }
static void redraw() { syncControls(); touch(); }

// -------------------------------------------------------------------- actions
static gboolean clearStatus(gpointer) { g_statusTimer = 0; g_playState.clear(); touch(); return G_SOURCE_REMOVE; }
static void setStatus(const std::string& s) {
  g_playState = s;
  if (g_statusTimer) g_source_remove(g_statusTimer);
  g_statusTimer = g_timeout_add(6000, clearStatus, nullptr);
}

static bool setWow(const fs::path& p) {
  g_setupErr.clear();
  if (!logic::isWowExe(p)) {
    g_setupErr = p.filename().u8string() + " is not Wow.exe — please select Wow.exe from your 3.3.5a client folder.";
    g_screen = SC_MAP; return false;
  }
  std::error_code ec;
  if (!fs::is_regular_file(p, ec)) { g_setupErr = p.u8string() + " does not exist."; g_screen = SC_MAP; return false; }
  logic::GlueReport rep; std::string err;
  if (!logic::ensureGlue(p, glueTemplate, rep, err)) {
    g_setupErr = "Could not write the login files into " + logic::glueDir(p).u8string() + " (" + err +
                 "). Check that you have write permission for the game folder.";
    g_screen = SC_MAP; return false;
  }
  g_wowExe = p;
  g_cfg.wowExe = p.u8string();
  logic::addCreated(g_cfg, rep.created);
  saveCfg();
  g_setupPath = p.u8string();
  std::string info = "Login interface ready — ";
  if (!rep.installed.empty()) info += std::to_string(rep.installed.size()) + " copied";
  if (!rep.installed.empty() && !rep.present.empty()) info += "; ";
  if (!rep.present.empty()) info += std::to_string(rep.present.size()) + " already present";
  g_setupInfo = info + ".";
  g_screen = SC_ACCEPT;
  return true;
}

static void onLocated(GObject* src, GAsyncResult* res, gpointer) {
  GFile* f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, nullptr);
  if (f) {
    char* path = g_file_get_path(f);
    if (path) { setWow(fs::path(path)); g_free(path); }
    g_object_unref(f);
  }
  redraw();
}
static void doLocate() {
  GtkFileDialog* d = gtk_file_dialog_new();
  gtk_file_dialog_set_title(d, "Locate Wow.exe");
  GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  GtkFileFilter* f1 = gtk_file_filter_new();
  gtk_file_filter_set_name(f1, "Wow.exe");
  gtk_file_filter_add_pattern(f1, "[Ww][Oo][Ww].[Ee][Xx][Ee]");
  GtkFileFilter* f2 = gtk_file_filter_new();
  gtk_file_filter_set_name(f2, "Programs (*.exe)");
  gtk_file_filter_add_pattern(f2, "*.[Ee][Xx][Ee]");
  g_list_store_append(filters, f1); g_list_store_append(filters, f2);
  gtk_file_dialog_set_filters(d, G_LIST_MODEL(filters));
  gtk_file_dialog_open(d, GTK_WINDOW(g_win), nullptr, onLocated, nullptr);
  g_object_unref(f1); g_object_unref(f2); g_object_unref(filters); g_object_unref(d);
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
static void doLogin() {
  if (!g_setupDone || !g_user.empty()) return;
  g_loginErr.clear();
  std::string name = trim(gtk_editable_get_text(GTK_EDITABLE(g_edName))), pw = gtk_editable_get_text(GTK_EDITABLE(g_edPass));
  if (name.empty() || pw.empty()) { redraw(); return; }
  std::string err;
  std::vector<std::string> made;
  bool wrote = logic::writeCreds(g_wowExe, glueTemplate, name, pw, err, &made);
  logic::addCreated(g_cfg, made);
  if (!wrote) {
    g_loginErr = "Could not update the game login file: " + err;
    redraw(); return;
  }
  g_user = name;
  g_cfg.account = g_remember ? name : "";
  saveCfg();
  gtk_editable_set_text(GTK_EDITABLE(g_edName), ""); gtk_editable_set_text(GTK_EDITABLE(g_edPass), "");
  g_loginOpen = false;
  redraw();
}
static void doLogout() {
  g_user.clear();
  g_cfg.account.clear();
  saveCfg();
  std::string err;
  std::vector<std::string> made;
  logic::writeCreds(g_wowExe, glueTemplate, "", "", err, &made);  // don't leave the password on disk
  logic::addCreated(g_cfg, made); saveCfg();
  g_loginOpen = true;
  g_playErr.clear(); g_playState.clear();
  redraw();
  gtk_widget_grab_focus(g_edName);
}

static void onGameExit(GPid pid, gint, gpointer) { g_spawn_close_pid(pid); g_gameRunning = false; }
static void doPlay() {
  g_playErr.clear(); g_playState.clear();
  if (g_gameRunning) { setStatus("World of Warcraft is already running."); redraw(); return; }
  logic::GlueReport rep; std::string err;
  if (!logic::ensureGlue(g_wowExe, glueTemplate, rep, err)) {
    g_playErr = "Client files unavailable: " + err; redraw(); return;
  }
  if (!rep.created.empty()) { logic::addCreated(g_cfg, rep.created); saveCfg(); }
  auto pick = faugus::pickWow(faugus::findWowGames(), g_wowExe);
  faugus::Launch L = faugus::buildLaunch(g_wowExe, pick);
  if (!L.error.empty()) { g_playErr = L.error; redraw(); return; }
  gchar* argv[] = {(gchar*)"/bin/sh", (gchar*)"-c", (gchar*)L.command.c_str(), nullptr};
  GPid pid; GError* ge = nullptr;
  if (!g_spawn_async(nullptr, argv, nullptr, G_SPAWN_DO_NOT_REAP_CHILD, nullptr, nullptr, &pid, &ge)) {
    g_playErr = std::string("Could not start the game: ") + (ge ? ge->message : "unknown error");
    if (ge) g_error_free(ge);
    redraw(); return;
  }
  g_gameRunning = true;
  g_child_watch_add(pid, onGameExit, nullptr);
  setStatus("World of Warcraft is starting…");
  redraw();
}

// Open the game folder (where Wow.exe is) or Interface/AddOns in the file manager.
static void openFolder(bool addons) {
  if (g_wowExe.empty()) return;
  fs::path dir = g_wowExe.parent_path();
  if (addons) {
    dir = logic::ciJoin(logic::ciJoin(dir, "Interface"), "AddOns");
    std::vector<std::string> made; std::string e;
    logic::mkdirTracked(dir, &made, e);
    if (!made.empty()) { logic::addCreated(g_cfg, made); saveCfg(); }
  }
  gchar* uri = g_filename_to_uri(dir.c_str(), nullptr, nullptr);
  GError* ge = nullptr;
  bool ok = uri && g_app_info_launch_default_for_uri(uri, nullptr, &ge);
  if (ge) g_error_free(ge);
  g_free(uri);
  if (!ok) { setStatus("Could not open " + dir.u8string()); redraw(); }
}

static void openUninstall() {
  g_plan = logic::makeUninstallPlan(g_cfg, glueTemplate, configPath().parent_path(), cacheDir());
  g_delAppImage = false; g_uninst = 1; redraw();
}
static void doUninstall() {
  g_uninstLog = logic::runUninstall(g_plan);
  if (g_delAppImage && appImagePath()) {
    if (unlink(appImagePath()) == 0) g_uninstLog.push_back(std::string("Removed ") + logic::homeShort(appImagePath()));
    else g_uninstLog.push_back(std::string("Could not delete ") + appImagePath());
  } else if (appImagePath()) g_uninstLog.push_back(std::string("The AppImage itself was kept: ") + logic::homeShort(appImagePath()));
  g_uninstLog.push_back("All done. Nothing of the launcher is left on this computer" +
                        std::string(g_delAppImage ? "." : " except the AppImage file — delete it whenever you like."));
  g_noSave = true; g_uninst = 2; redraw();
}
static void onClick(int id) {
  switch (id) {
    case H_MIN: gtk_window_minimize(GTK_WINDOW(g_win)); break;
    case H_CLOSE: gtk_window_destroy(GTK_WINDOW(g_win)); break;
    case H_LOGO: openUrl(URL_HOME); break;
    case H_GITHUB: openUrl(URL_GITHUB); break;
    case H_UNINST: openUninstall(); break;
    case H_UNINST_APPIMG: g_delAppImage = !g_delAppImage; redraw(); break;
    case H_UNINST_GO: doUninstall(); break;
    case H_UNINST_CANCEL: g_uninst = 0; redraw(); break;
    case H_UNINST_CLOSE: gtk_window_destroy(GTK_WINDOW(g_win)); break;
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
        static const char* out[6] = {"https://www.warmane.com/account/create", "https://www.warmane.com/download",
                                     "https://forum.warmane.com/", "https://www.warmane.com/information",
                                     "http://armory.warmane.com/", nullptr};
        static const char* in[6] = {"https://www.warmane.com/account", "https://www.warmane.com/download",
                                    "https://forum.warmane.com/", "https://www.warmane.com/information",
                                    "http://armory.warmane.com/", nullptr};
        if (i == 5) {
          if (g_user.empty()) { g_loginErr.clear(); g_loginOpen = true; redraw(); gtk_widget_grab_focus(g_edName); }
          else doLogout();
        } else openUrl(g_user.empty() ? out[i] : in[i]);
      }
  }
}
static int hitTest(double x, double y) {
  for (int i = (int)g_hot.size() - 1; i >= 0; i--) {
    const Hot& h = g_hot[i];
    if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) return h.id;
  }
  return H_NONE;
}
static bool hitIsHand(double x, double y) {
  for (int i = (int)g_hot.size() - 1; i >= 0; i--) {
    const Hot& h = g_hot[i];
    if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) return h.hand;
  }
  return false;
}

// Wow.exe next to the launcher (case-insensitive), e.g. when the AppImage sits in the game folder.
static fs::path wowNextToUs() {
  fs::path dir;
  if (const char* ai = getenv("APPIMAGE")) dir = fs::path(ai).parent_path();
  else { std::error_code ec; dir = fs::read_symlink("/proc/self/exe", ec).parent_path(); }
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (logic::isWowExe(it->path()) && it->is_regular_file(ec)) return it->path();
  return {};
}
static void boot() {
  g_cfg = logic::loadConfig(configPath());
  std::error_code ec;
  fs::path exe = g_cfg.wowExe.empty() ? fs::path() : fs::path(g_cfg.wowExe);
  if (!exe.empty() && fs::is_regular_file(exe, ec)) {
    g_wowExe = exe;
    if (!g_cfg.account.empty()) {  // "Remember me": the account stored in the game file must still match
      auto x = logic::readInstalledAccount(exe);
      if (x && (x->account != g_cfg.account || !x->hasPassword)) { g_cfg.account.clear(); saveCfg(); }
    }
    finishSetup();
    return;
  }
  g_cfg.wowExe.clear();
  fs::path local = wowNextToUs();
  if (!local.empty() && setWow(local)) { finishSetup(); return; }
  g_setupErr.clear();
  g_screen = SC_CHECK;
  g_checkTicks = 0;
  g_checkTimer = g_timeout_add(350, [](gpointer) -> gboolean {
    g_checkTicks++;
    if (g_checkTicks >= 3) {
      g_checkTimer = 0;
      bool done = false;
      for (const faugus::Game& g : faugus::findWowGames()) {
        std::error_code e2;
        if (fs::is_regular_file(g.path, e2) && setWow(fs::path(g.path))) { done = true; break; }
      }
      if (!done) { g_setupErr.clear(); g_screen = SC_MAP; }
      touch();
      return G_SOURCE_REMOVE;
    }
    touch();
    return G_SOURCE_CONTINUE;
  }, nullptr);
}

// ----------------------------------------------------------------- GTK glue
static void drawCb(GtkDrawingArea*, cairo_t* cr, int, int, gpointer) {
  if (g_dirty) renderOverlay();
  cairo_set_source_surface(cr, g_back, 0, 0);
  cairo_paint(cr);
}
static void onPressed(GtkGestureClick* gest, int, double x, double y, gpointer) {
  int id = hitTest(x, y);
  bool inPanel = id == H_LBBOX || id == H_LBGO || id == H_REMEMBER || (id >= H_HELP0 && id < H_HELP0 + 4);
  bool navLogin = id == H_NAV0 + 5 || id == H_NAV0;
  if (g_setupDone && g_loginOpen && g_user.empty() && !inPanel && !navLogin) { g_loginOpen = false; redraw(); }
  if (id == H_NONE) {  // empty area: drag the window
    GdkSurface* s = gtk_native_get_surface(gtk_widget_get_native(g_area));
    GdkDevice* dev = gtk_gesture_get_device(GTK_GESTURE(gest));
    if (s && GDK_IS_TOPLEVEL(s))
      gdk_toplevel_begin_move(GDK_TOPLEVEL(s), dev, 1, x, y, gtk_event_controller_get_current_event_time(GTK_EVENT_CONTROLLER(gest)));
    return;
  }
  onClick(id);
}
static void onMotion(GtkEventControllerMotion*, double x, double y, gpointer) {
  int id = hitTest(x, y);
  gtk_widget_set_cursor_from_name(g_area, hitIsHand(x, y) ? "pointer" : "default");
  if (id != g_hover) { g_hover = id; touch(); }
}
static void onLeave(GtkEventControllerMotion*, gpointer) {
  if (g_hover != H_NONE) { g_hover = H_NONE; touch(); }
}
static gboolean videoTick(gpointer) {
  bool minimized = false;
  GdkSurface* s = gtk_native_get_surface(gtk_widget_get_native(g_area));
  if (s && GDK_IS_TOPLEVEL(s)) minimized = gdk_toplevel_get_state(GDK_TOPLEVEL(s)) & GDK_TOPLEVEL_STATE_MINIMIZED;
  // Only animate while visible, after setup (the setup veil hides the video) and while the game is not running.
  if (!minimized && g_setupDone && !g_gameRunning && !g_dirty && gtk_widget_get_mapped(g_area)) {
    g_vidIdx = (g_vidIdx + 1) % g_vidCount;
    decodeFrame(g_vidIdx);
    composeVideo();
    gtk_widget_queue_draw(g_area);
  }
  return G_SOURCE_CONTINUE;
}
static gboolean onEditKey(GtkEventControllerKey*, guint keyval, guint, GdkModifierType, gpointer user) {
  GtkWidget* self = (GtkWidget*)user;
  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) { doLogin(); return TRUE; }
  if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) {
    gtk_widget_grab_focus(self == g_edName ? g_edPass : g_edName);
    return TRUE;
  }
  return FALSE;
}
static void onEditFocus(GObject* ctl, GParamSpec*, gpointer user) {
  bool f = gtk_event_controller_focus_contains_focus(GTK_EVENT_CONTROLLER_FOCUS(ctl));
  if (user == (gpointer)g_edName) g_focusName = f; else g_focusPass = f;
  touch();
}
static GtkWidget* makeEntry(int y, int maxLen, bool password) {
  GtkWidget* e = gtk_entry_new();
  gtk_widget_add_css_class(e, "wl");
  gtk_entry_set_max_length(GTK_ENTRY(e), maxLen);
  if (password) { gtk_entry_set_visibility(GTK_ENTRY(e), FALSE); gtk_entry_set_input_purpose(GTK_ENTRY(e), GTK_INPUT_PURPOSE_PASSWORD); }
  gtk_widget_set_halign(e, GTK_ALIGN_START); gtk_widget_set_valign(e, GTK_ALIGN_START);
  gtk_widget_set_margin_start(e, 331); gtk_widget_set_margin_top(e, y + 9);
  gtk_widget_set_size_request(e, 251, 20);
  gtk_widget_set_visible(e, FALSE);
  GtkEventController* kc = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_CAPTURE);
  g_signal_connect(kc, "key-pressed", G_CALLBACK(onEditKey), e);
  gtk_widget_add_controller(e, kc);
  GtkEventController* fc = gtk_event_controller_focus_new();
  g_signal_connect(fc, "notify::contains-focus", G_CALLBACK(onEditFocus), e);
  gtk_widget_add_controller(e, fc);
  return e;
}

static void registerFont() {
  Blob b = BLOB(futura);
  gchar* path = g_build_filename(g_get_user_cache_dir(), "unofficial-warmane-launcher", "futura.ttf", nullptr);
  gchar* dir = g_path_get_dirname(path);
  g_mkdir_with_parents(dir, 0700);
  g_file_set_contents(path, (const gchar*)b.p, b.n, nullptr);
  FcConfigAppFontAddFile(nullptr, (const FcChar8*)path);
  g_free(dir); g_free(path);
}

static void loadStyle() {
  const char* css =
      "window.wl { background: #0f0f0f; }"
      "entry.wl { background: #000000; background-image: none; color: #c1b575; border: none; border-radius: 0;"
      "  box-shadow: none; outline: none; padding: 0; min-height: 20px; font-family: \"FuturaEF Book\"; font-size: 15px; caret-color: #c1b575; }"
      "entry.wl:focus-within { outline: none; box-shadow: none; }"
      "entry.wl text { background: transparent; color: #c1b575; padding: 0; min-height: 0; }"
      "entry.wl text selection { background: #6b5732; color: #ffffff; }";
  GtkCssProvider* p = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
  gtk_css_provider_load_from_string(p, css);
#else
  gtk_css_provider_load_from_data(p, css, -1);
#endif
  gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(p);
}

static int cliUninstall(bool yes, bool delAI) {
  g_cfg = logic::loadConfig(configPath());
  auto plan = logic::makeUninstallPlan(g_cfg, glueTemplate, configPath().parent_path(), cacheDir());
  if (plan.empty() && !appImagePath()) { printf("Nothing to remove — the launcher has not changed anything on this computer.\n"); return 0; }
  printf("Unofficial Warmane Launcher — uninstall\n\n%s\n", logic::describePlan(plan).c_str());
  bool tty = isatty(0);
  if (!yes) {
    if (!tty) { fprintf(stderr, "Not a terminal: add --yes to confirm.\n"); return 2; }
    printf("Remove all of this? [y/N] "); fflush(stdout);
    char b[16] = {0}; if (!fgets(b, sizeof b, stdin) || (b[0] != 'y' && b[0] != 'Y')) { printf("Cancelled.\n"); return 1; }
    if (appImagePath() && !delAI) {
      printf("Also delete the AppImage (%s)? [y/N] ", appImagePath()); fflush(stdout);
      char c[16] = {0}; if (fgets(c, sizeof c, stdin) && (c[0] == 'y' || c[0] == 'Y')) delAI = true;
    }
  }
  for (auto& l : logic::runUninstall(plan)) printf("%s\n", l.c_str());
  if (delAI && appImagePath()) printf(unlink(appImagePath()) == 0 ? "Removed %s\n" : "Could not delete %s\n", appImagePath());
  return 0;
}

int main(int argc, char** argv) {
  {
    bool un = false, yes = false, ai = false;
    for (int i = 1; i < argc; i++) {
      std::string a = argv[i];
      if (a == "--uninstall") un = true; else if (a == "--yes" || a == "-y") yes = true; else if (a == "--delete-appimage") ai = true;
      else if (a == "--help" || a == "-h") {
        printf("Unofficial Warmane Launcher\n  --uninstall [--yes] [--delete-appimage]   remove everything the launcher created\n");
        return 0;
      }
    }
    if (un) return cliUninstall(yes, ai);
  }
  g_set_prgname("unofficial-warmane-launcher");
  g_set_application_name(APP_TITLE);
  registerFont();
  gtk_init();
  loadStyle();

  g_measSurf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4);
  g_meas = cairo_create(g_measSurf);
  struct { Img* i; Blob b; const char* n; } list[] = {
      {&I.hero, BLOB(hero), "hero"}, {&I.navbar, BLOB(navbar), "navbar"}, {&I.navlogo, BLOB(navlogo), "navlogo"},
      {&I.wotlk, BLOB(wotlk), "wotlk"}, {&I.ftl, BLOB(ftl), "ftl"}, {&I.ftr, BLOB(ftr), "ftr"}, {&I.fbl, BLOB(fbl), "fbl"},
      {&I.fbr, BLOB(fbr), "fbr"}, {&I.fheader, BLOB(fheader), "fheader"}, {&I.ffooter, BLOB(ffooter), "ffooter"},
      {&I.flc, BLOB(flc), "flc"}, {&I.frc, BLOB(frc), "frc"}, {&I.fcenter, BLOB(fcenter), "fcenter"},
      {&I.bmin, BLOB(bmin), "bmin"}, {&I.bminh, BLOB(bminh), "bminh"}, {&I.bclose, BLOB(bclose), "bclose"},
      {&I.bcloseh, BLOB(bcloseh), "bcloseh"}};
  for (auto& e : list) {
    *e.i = loadImg(e.b);
    if (!e.i->s) { g_printerr("Missing or unreadable image: %s\n", e.n); return 1; }
  }
  initVideo();
  g_back = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, WIN_W, WIN_H);
  g_overlay = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, WIN_W, WIN_H);

  g_win = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(g_win), APP_TITLE);
  gtk_window_set_icon_name(GTK_WINDOW(g_win), "unofficial-warmane-launcher");
  gtk_window_set_decorated(GTK_WINDOW(g_win), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(g_win), FALSE);
  gtk_widget_add_css_class(g_win, "wl");

  g_area = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(g_area), WIN_W);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(g_area), WIN_H);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_area), drawCb, nullptr, nullptr);
  GtkGesture* click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
  g_signal_connect(click, "pressed", G_CALLBACK(onPressed), nullptr);
  gtk_widget_add_controller(g_area, GTK_EVENT_CONTROLLER(click));
  GtkEventController* mc = gtk_event_controller_motion_new();
  g_signal_connect(mc, "motion", G_CALLBACK(onMotion), nullptr);
  g_signal_connect(mc, "leave", G_CALLBACK(onLeave), nullptr);
  gtk_widget_add_controller(g_area, mc);

  GtkWidget* ov = gtk_overlay_new();
  gtk_overlay_set_child(GTK_OVERLAY(ov), g_area);
  g_edName = makeEntry(222, 16, false);
  g_edPass = makeEntry(291, 32, true);
  gtk_overlay_add_overlay(GTK_OVERLAY(ov), g_edName);
  gtk_overlay_add_overlay(GTK_OVERLAY(ov), g_edPass);
  gtk_window_set_child(GTK_WINDOW(g_win), ov);

  boot();
  if (g_vidCount) g_timeout_add(1000 / g_vidFps, videoTick, nullptr);
  gtk_window_present(GTK_WINDOW(g_win));
  while (g_list_model_get_n_items(gtk_window_get_toplevels()) > 0) g_main_context_iteration(nullptr, TRUE);
  return 0;
}
