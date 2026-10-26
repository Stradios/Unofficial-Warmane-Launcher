// Finding the player's World of Warcraft entry in Faugus Launcher and building the same umu-run command Faugus
// would use. Pure C++ (no GTK) so it can be unit-tested. Based on how Faugus (MIT) stores its data:
//   ~/.local/share/faugus-launcher/games.json   list of games {title, gameid, path, prefix, runner, protonfix, ...}
//   ~/.config/faugus-launcher/config.json       settings, incl. "default-runner"
//   ~/.local/share/faugus-launcher/umu-run      the umu-run build Faugus downloads and uses
//   ~/.local/share/Steam/compatibilitytools.d   installed Proton versions
#pragma once
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace faugus {
namespace fs = std::filesystem;

// ------------------------------------------------------------------ tiny JSON
struct J {
  enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<J> a;
  std::vector<std::pair<std::string, J>> o;
  const J* get(const std::string& k) const {
    for (auto& p : o) if (p.first == k) return &p.second;
    return nullptr;
  }
  std::string str(const std::string& k, const std::string& def = "") const {
    const J* v = get(k);
    return v && v->t == Str ? v->s : def;
  }
};

struct Parser {
  const std::string& s;
  size_t i = 0;
  bool ok = true;
  explicit Parser(const std::string& text) : s(text) {}
  void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++; }
  static void utf8(std::string& o, unsigned cp) {
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 63)); o += char(0x80 | (cp & 63)); }
    else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 63)); o += char(0x80 | ((cp >> 6) & 63)); o += char(0x80 | (cp & 63)); }
  }
  unsigned hex4() {
    if (i + 4 > s.size()) { ok = false; return 0; }
    unsigned v = 0;
    for (int k = 0; k < 4; k++) {
      char c = s[i++]; v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else ok = false;
    }
    return v;
  }
  std::string str() {
    std::string o;
    i++;  // opening quote
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') { o += c; continue; }
      if (i >= s.size()) { ok = false; break; }
      char e = s[i++];
      switch (e) {
        case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
        case 'b': o += '\b'; break; case 'f': o += '\f'; break;
        case 'u': {
          unsigned cp = hex4();
          if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            i += 2; unsigned lo = hex4();
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          utf8(o, cp); break;
        }
        default: o += e;
      }
    }
    if (i >= s.size()) ok = false; else i++;
    return o;
  }
  J value(int depth = 0) {
    J v;
    ws();
    if (!ok || i >= s.size() || depth > 64) { ok = false; return v; }
    char c = s[i];
    if (c == '{') {
      v.t = J::Obj; i++; ws();
      if (i < s.size() && s[i] == '}') { i++; return v; }
      while (ok) {
        ws();
        if (i >= s.size() || s[i] != '"') { ok = false; break; }
        std::string k = str(); ws();
        if (i >= s.size() || s[i] != ':') { ok = false; break; }
        i++;
        v.o.emplace_back(k, value(depth + 1)); ws();
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == '}') { i++; break; }
        ok = false;
      }
    } else if (c == '[') {
      v.t = J::Arr; i++; ws();
      if (i < s.size() && s[i] == ']') { i++; return v; }
      while (ok) {
        v.a.push_back(value(depth + 1)); ws();
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == ']') { i++; break; }
        ok = false;
      }
    } else if (c == '"') { v.t = J::Str; v.s = str(); }
    else if (s.compare(i, 4, "true") == 0) { v.t = J::Bool; v.b = true; i += 4; }
    else if (s.compare(i, 5, "false") == 0) { v.t = J::Bool; i += 5; }
    else if (s.compare(i, 4, "null") == 0) { i += 4; }
    else {
      size_t st = i;
      while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
      if (st == i) { ok = false; return v; }
      v.t = J::Num; v.n = atof(s.substr(st, i - st).c_str());
    }
    return v;
  }
};
inline std::optional<J> parseJson(const std::string& text) {
  Parser p(text);
  J v = p.value();
  p.ws();
  if (!p.ok) return std::nullopt;
  return v;
}

// -------------------------------------------------------------------- helpers
inline std::string readAll(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss; ss << f.rdbuf();
  return ss.str();
}
inline std::string lowerS(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32; return s; }
inline std::string home() { const char* h = getenv("HOME"); return h && *h ? h : "/"; }
inline std::string xdgData() { const char* d = getenv("XDG_DATA_HOME"); return d && *d ? d : home() + "/.local/share"; }
inline std::string xdgConfig() { const char* d = getenv("XDG_CONFIG_HOME"); return d && *d ? d : home() + "/.config"; }
inline std::string expandPath(std::string p) {
  if (p == "~") return home();
  if (p.rfind("~/", 0) == 0) return home() + p.substr(1);
  return p;
}
// POSIX shell single-quote.
inline std::string shq(const std::string& s) {
  std::string o = "'";
  for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
  return o + "'";
}
inline bool isExec(const fs::path& p) { return !p.empty() && access(p.c_str(), X_OK) == 0 && fs::is_regular_file(p); }

// ------------------------------------------------------------------- Faugus data
struct Game {
  std::string title, gameid, path, prefix, runner, protonfix, launchArgs, gameArgs;
};

inline std::vector<fs::path> faugusDataDirs() {
  return {fs::path(xdgData()) / "faugus-launcher",
          fs::path(home()) / ".var/app/io.github.Faugus.faugus-launcher/data/faugus-launcher"};
}
inline std::vector<fs::path> faugusConfigFiles() {
  return {fs::path(xdgConfig()) / "faugus-launcher/config.json",
          fs::path(home()) / ".var/app/io.github.Faugus.faugus-launcher/config/faugus-launcher/config.json"};
}

inline std::vector<Game> loadGames(const fs::path& gamesJson) {
  std::vector<Game> out;
  auto j = parseJson(readAll(gamesJson));
  if (!j || j->t != J::Arr) return out;
  for (const J& g : j->a) {
    if (g.t != J::Obj) continue;
    Game x;
    x.title = g.str("title"); x.gameid = g.str("gameid"); x.path = expandPath(g.str("path"));
    x.prefix = expandPath(g.str("prefix")); x.runner = g.str("runner"); x.protonfix = g.str("protonfix");
    x.launchArgs = g.str("launch_arguments"); x.gameArgs = g.str("game_arguments");
    out.push_back(x);
  }
  return out;
}

// All Faugus entries whose program is Wow.exe (case-insensitive), from every known Faugus location.
inline std::vector<Game> findWowGames() {
  std::vector<Game> out;
  for (const fs::path& d : faugusDataDirs())
    for (Game& g : loadGames(d / "games.json"))
      if (lowerS(fs::path(g.path).filename().string()) == "wow.exe") out.push_back(g);
  return out;
}

// The entry for `preferredExe`; with no preference, the first entry. Never an entry for a different Wow.exe.
inline std::optional<Game> pickWow(const std::vector<Game>& all, const fs::path& preferredExe) {
  if (all.empty()) return std::nullopt;
  if (preferredExe.empty()) return all.front();
  for (const Game& g : all) {          // only borrow settings from the entry for THIS Wow.exe
    std::error_code ec;
    if (fs::equivalent(g.path, preferredExe, ec)) return g;
  }
  return std::nullopt;
}

inline std::string defaultRunner() {
  for (const fs::path& p : faugusConfigFiles()) {
    auto j = parseJson(readAll(p));
    if (j && j->t == J::Obj) {
      std::string r = j->str("default-runner");
      while (!r.empty() && (r.front() == '"' || r.front() == '\'')) r.erase(r.begin());
      while (!r.empty() && (r.back() == '"' || r.back() == '\'')) r.pop_back();
      if (!r.empty()) return r;
    }
  }
  return "";
}

inline std::vector<fs::path> compatDirs() {
  return {fs::path(xdgData()) / "Steam/compatibilitytools.d",
          fs::path(home()) / ".var/app/com.valvesoftware.Steam/.local/share/Steam/compatibilitytools.d"};
}
inline std::vector<std::string> versionParts(const std::string& s, std::vector<long>& nums) {
  nums.clear();
  long cur = -1;
  for (char c : s) {
    if (isdigit((unsigned char)c)) cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
    else if (cur >= 0) { nums.push_back(cur); cur = -1; }
  }
  if (cur >= 0) nums.push_back(cur);
  return {};
}
// Installed Proton builds (names of folders in compatibilitytools.d), newest first.
inline std::vector<std::string> installedProtons() {
  std::vector<std::string> names;
  for (const fs::path& d : compatDirs()) {
    std::error_code ec;
    for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec))
      if (it->is_directory(ec)) {
        std::string n = it->path().filename().string();
        if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
      }
  }
  std::sort(names.begin(), names.end(), [](const std::string& x, const std::string& y) {
    std::vector<long> a, b; versionParts(x, a); versionParts(y, b);
    if (a != b) return a > b;
    return x > y;
  });
  return names;
}
inline std::string newestGE(const std::vector<std::string>& protons) {
  for (const std::string& n : protons) if (n.rfind("GE-Proton", 0) == 0) return n;
  return "";
}
inline fs::path findCompat(const std::string& name) {
  for (const fs::path& d : compatDirs()) { std::error_code ec; if (fs::is_directory(d / name, ec)) return d / name; }
  return {};
}

struct Umu { std::string path; bool isFaugus = false; };
inline std::optional<Umu> findUmu() {
  for (const fs::path& d : faugusDataDirs())
    if (isExec(d / "umu-run")) return Umu{(d / "umu-run").string(), true};
  if (const char* path = getenv("PATH")) {
    std::stringstream ss(path); std::string dir;
    while (std::getline(ss, dir, ':'))
      if (!dir.empty() && isExec(fs::path(dir) / "umu-run")) return Umu{(fs::path(dir) / "umu-run").string(), false};
  }
  return std::nullopt;
}
inline std::string findInPath(const std::string& bin) {
  if (const char* path = getenv("PATH")) {
    std::stringstream ss(path); std::string dir;
    while (std::getline(ss, dir, ':'))
      if (!dir.empty() && isExec(fs::path(dir) / bin)) return (fs::path(dir) / bin).string();
  }
  return "";
}

// The shell command that starts Wow.exe like Faugus would. `game` may be empty (no Faugus entry): then umu's own
// defaults are used with the newest installed GE-Proton, or "GE-Proton" (umu downloads it).
struct Launch { std::string command, error, via; };
inline Launch buildLaunch(const fs::path& wowExe, const std::optional<Game>& game) {
  Launch L;
  std::string dir = wowExe.parent_path().string();
  std::vector<std::string> env;
  std::string runner, prefix, gameid, launchArgs, gameArgs;
  if (game) {
    runner = game->runner; prefix = game->prefix; gameid = game->protonfix;
    launchArgs = game->launchArgs; gameArgs = game->gameArgs;
    if (runner == "Default" || runner.empty()) runner = defaultRunner();
  }
  auto umu = findUmu();
  if (umu) {
    L.via = umu->isFaugus ? "Faugus umu-run" : "umu-run";
    if (!gameid.empty()) env.push_back("GAMEID=" + shq(gameid));
    if (!prefix.empty()) env.push_back("WINEPREFIX=" + shq(prefix));
    std::string proton;
    if (runner == "Linux-Native" || runner == "Steam") runner.clear();  // not applicable to a Windows game
    if (!runner.empty()) {
      std::error_code ec;
      if (fs::is_directory(runner, ec)) proton = runner;
      else if (runner.size() > 7 && runner.compare(runner.size() - 7, 7, " Latest") == 0) {
        // "Proton-GE Latest" etc. are understood by Faugus's own umu-run; plain umu-run only knows GE-Proton
        proton = umu->isFaugus ? runner : "GE-Proton";
      } else {
        fs::path c = findCompat(runner);
        proton = c.empty() ? runner : (c.parent_path() == compatDirs()[0] ? runner : c.string());
      }
    } else if (!game) {
      std::string ge = newestGE(installedProtons());
      proton = ge.empty() ? "GE-Proton" : ge;
    }
    if (!proton.empty()) env.push_back("PROTONPATH=" + shq(proton));
    std::string cmd = "cd " + shq(dir) + " && ";
    for (auto& e : env) cmd += e + " ";
    if (!launchArgs.empty()) cmd += launchArgs + " ";
    cmd += shq(umu->path) + " " + shq(wowExe.string());
    if (!gameArgs.empty()) cmd += " " + gameArgs;
    L.command = cmd;
    return L;
  }
  std::string wine = findInPath("wine");
  if (!wine.empty()) {
    L.via = "wine";
    std::string cmd = "cd " + shq(dir) + " && ";
    if (!prefix.empty()) cmd += "WINEPREFIX=" + shq(prefix) + " ";
    cmd += shq(wine) + " " + shq(wowExe.string());
    L.command = cmd;
    return L;
  }
  L.error = "Neither umu-run (installed with Faugus) nor wine was found, so the game cannot be started.";
  return L;
}

}  // namespace faugus
