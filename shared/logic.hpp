// Platform-independent launcher logic: GlueXML credential patching, file
// installation and config. No Windows headers here so it can be unit-tested
// on any OS. All text is UTF-8 std::string; paths are std::filesystem::path.
#pragma once
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <set>
#include <algorithm>

namespace logic {
namespace fs = std::filesystem;

// ---- embedded template access (provided by the platform layer) -----------
// Returns the bytes of one of the four bundled GlueXML files.
using TemplateFn = std::string (*)(const std::string& name);

inline const char* kGlueNames[4] = {"AccountLogin.lua", "AccountLogin.xml",
                                    "AccountLogin.lua.origin", "AccountLogin.xml.origin"};

// ---- small helpers ----------------------------------------------------------
inline std::string readFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss; ss << f.rdbuf();
  return ss.str();
}
inline bool writeFile(const fs::path& p, const std::string& data) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), (std::streamsize)data.size());
  return (bool)f;
}
inline std::string lower(std::string s) {
  for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}
inline bool isWowExe(const fs::path& p) { return lower(p.filename().u8string()) == "wow.exe"; }
// Join a child directory name case-insensitively: on Linux a game folder may hold "interface" while we
// would otherwise create a second "Interface" next to it. Falls back to the plain join.
inline fs::path ciJoin(const fs::path& dir, const std::string& name) {
  std::error_code ec;
  fs::path direct = dir / name;
  if (fs::is_directory(direct, ec)) return direct;
  const std::string want = lower(name);
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (lower(it->path().filename().u8string()) == want && it->is_directory(ec)) return it->path();
  return direct;
}
inline fs::path glueDir(const fs::path& exe) { return ciJoin(ciJoin(exe.parent_path(), "Interface"), "GlueXML"); }

// ---- escaping ---------------------------------------------------------------
inline std::string replaceAll(std::string s, const std::string& a, const std::string& b) {
  size_t i = 0;
  while ((i = s.find(a, i)) != std::string::npos) { s.replace(i, a.size(), b); i += b.size(); }
  return s;
}
inline std::string luaEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '\\') o += "\\\\";
    else if (c == '"') o += "\\\"";
    else if (c == '\r') {}
    else if (c == '\n') o += "\\n";
    else o += c;
  }
  return o;
}
inline std::string luaUnescape(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char c = s[++i];
      o += (c == 'n') ? '\n' : (c == 'r') ? '\r' : c;
    } else o += s[i];
  }
  return o;
}
// The Lua lives inside an XML element: & < > must be escaped or a password
// containing them would corrupt the file.
inline std::string xmlEscape(const std::string& s) {
  return replaceAll(replaceAll(replaceAll(s, "&", "&amp;"), "<", "&lt;"), ">", "&gt;");
}
inline std::string xmlUnescape(const std::string& s) {
  return replaceAll(replaceAll(replaceAll(s, "&lt;", "<"), "&gt;", ">"), "&amp;", "&");
}
inline std::string encode(const std::string& s) { return xmlEscape(luaEscape(s)); }
inline std::string decode(const std::string& s) { return luaUnescape(xmlUnescape(s)); }

// ---- credential patching ------------------------------------------------------
// Find the end of a Lua string literal body starting at `from` (just after the
// opening quote). Returns index of the closing quote or npos.
inline size_t stringEnd(const std::string& t, size_t from) {
  for (size_t i = from; i < t.size(); i++) {
    if (t[i] == '\\') { i++; continue; }
    if (t[i] == '"') return i;
  }
  return std::string::npos;
}

struct Span { size_t begin, end; };  // body of a string literal, quotes excluded

// Locate the first `<prefix>"..."` inside [lo,hi) and return the body span.
inline std::optional<Span> findLiteral(const std::string& t, size_t lo, size_t hi,
                                       const std::string& prefix) {
  size_t p = t.find(prefix, lo);
  if (p == std::string::npos || p >= hi) return std::nullopt;
  size_t b = p + prefix.size();
  size_t e = stringEnd(t, b);
  if (e == std::string::npos || e >= hi) return std::nullopt;
  return Span{b, e};
}

struct Block { size_t open, close; };
inline std::optional<Block> findBlock(const std::string& t) {
  size_t idx = t.find("FirestormAutoLogState:GetText");
  if (idx == std::string::npos) return std::nullopt;
  size_t open = t.rfind("<OnUpdate>", idx);
  size_t close = t.find("</OnUpdate>", idx);
  if (open == std::string::npos || close == std::string::npos) return std::nullopt;
  return Block{open, close};
}

// Replace the three credential spots; returns empty optional on failure.
inline std::optional<std::string> applyCreds(const std::string& text, const std::string& account,
                                             const std::string& password) {
  auto blk = findBlock(text);
  if (!blk) return std::nullopt;
  const std::string a = encode(account), p = encode(password);
  // Order matters: do the replacements from the END of the block backwards so
  // earlier spans stay valid.
  struct Job { std::string prefix; const std::string* val; };
  const Job jobs[3] = {
      {"AccountLoginAccountEdit:SetText(gsub(\"", &a},
      {"AccountLoginPasswordEdit:SetText(\"", &p},
      {"AccountLoginAccountEdit:SetText(\"", &a},
  };
  std::string t = text;
  size_t hi = blk->close;
  std::vector<std::pair<Span, const std::string*>> spans;
  for (const auto& j : jobs) {
    auto s = findLiteral(t, blk->open, hi, j.prefix);
    if (!s) return std::nullopt;
    spans.push_back({*s, j.val});
  }
  // sort by position, descending
  for (size_t i = 0; i < spans.size(); i++)
    for (size_t k = i + 1; k < spans.size(); k++)
      if (spans[k].first.begin > spans[i].first.begin) std::swap(spans[i], spans[k]);
  for (auto& [sp, val] : spans) t.replace(sp.begin, sp.end - sp.begin, *val);
  return t;
}

struct XmlAccount { std::string account; bool hasPassword; };
inline std::optional<XmlAccount> readAccount(const std::string& t) {
  auto blk = findBlock(t);
  if (!blk) return std::nullopt;
  auto a = findLiteral(t, blk->open, blk->close, "AccountLoginAccountEdit:SetText(\"");
  if (!a) return std::nullopt;
  auto p = findLiteral(t, blk->open, blk->close, "AccountLoginPasswordEdit:SetText(\"");
  XmlAccount r;
  r.account = decode(t.substr(a->begin, a->end - a->begin));
  r.hasPassword = p && !decode(t.substr(p->begin, p->end - p->begin)).empty();
  return r;
}

// ---- file operations ----------------------------------------------------------
struct GlueReport {
  std::vector<std::string> installed, present;
  std::vector<std::string> created;  // every folder and file this call created (absolute paths), for a clean uninstall
};

// Create `dir` and any missing parent folders, recording each folder that did not exist before.
inline bool mkdirTracked(const fs::path& dir, std::vector<std::string>* created, std::string& err) {
  std::error_code ec;
  std::vector<fs::path> missing;
  for (fs::path p = dir; !p.empty() && !fs::exists(p, ec); p = p.parent_path()) {
    missing.push_back(p);
    if (p == p.parent_path()) break;
  }
  for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
    fs::create_directory(*it, ec);
    if (ec) { err = ec.message(); return false; }
    if (created) created->push_back(it->u8string());
  }
  return true;
}

// Create Interface\GlueXML and write whichever of the four files are missing.
inline bool ensureGlue(const fs::path& exe, TemplateFn tpl, GlueReport& rep, std::string& err) {
  std::error_code ec;
  if (!mkdirTracked(glueDir(exe), &rep.created, err)) return false;
  for (const char* n : kGlueNames) {
    fs::path target = glueDir(exe) / n;
    if (fs::exists(target, ec)) { rep.present.push_back(n); continue; }
    if (!writeFile(target, tpl(n))) { err = "cannot write " + target.u8string(); return false; }
    rep.installed.push_back(n);
    rep.created.push_back(target.u8string());
  }
  return true;
}

// Write account+password into AccountLogin.xml. If the existing file has no
// recognisable login block, it is backed up (.bak) and replaced by the bundled one.
inline bool writeCreds(const fs::path& exe, TemplateFn tpl, const std::string& account,
                       const std::string& password, std::string& err,
                       std::vector<std::string>* created = nullptr) {
  std::error_code ec;
  if (!mkdirTracked(glueDir(exe), created, err)) return false;
  fs::path target = glueDir(exe) / "AccountLogin.xml";
  const bool targetExisted = fs::exists(target, ec);
  std::optional<std::string> out;
  if (fs::exists(target, ec)) {
    out = applyCreds(readFile(target), account, password);
    if (!out) {
      fs::path bak = glueDir(exe) / "AccountLogin.xml.bak";
      const bool bakExisted = fs::exists(bak, ec);
      fs::copy_file(target, bak, fs::copy_options::overwrite_existing, ec);
      if (!bakExisted && created) created->push_back(bak.u8string());
    }
  }
  if (!out) out = applyCreds(tpl("AccountLogin.xml"), account, password);
  if (!out) { err = "login block not found"; return false; }
  if (!writeFile(target, *out)) { err = "cannot write " + target.u8string(); return false; }
  if (!targetExisted && created) created->push_back(target.u8string());
  return true;
}

inline std::optional<XmlAccount> readInstalledAccount(const fs::path& exe) {
  std::error_code ec;
  fs::path p = glueDir(exe) / "AccountLogin.xml";
  if (!fs::exists(p, ec)) return std::nullopt;
  return readAccount(readFile(p));
}

// ---- config (tiny key=value text file, UTF-8) -----------------------------------
struct Config {
  std::string wowExe, account;
  std::vector<std::string> created;  // folders/files the launcher created in the game folder (see GlueReport)
};
inline void addCreated(Config& c, const std::vector<std::string>& more) {
  for (const auto& m : more) if (std::find(c.created.begin(), c.created.end(), m) == c.created.end()) c.created.push_back(m);
}

inline Config loadConfig(const fs::path& p) {
  Config c;
  std::ifstream f(p, std::ios::binary);
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (k == "wow_exe") c.wowExe = v;
    else if (k == "account") c.account = v;
    else if (k == "created" && !v.empty()) c.created.push_back(v);
  }
  return c;
}
inline bool saveConfig(const fs::path& p, const Config& c) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::string out = "wow_exe=" + c.wowExe + "\naccount=" + c.account + "\n";
  for (const auto& m : c.created) out += "created=" + m + "\n";
  return writeFile(p, out);
}

// ---- uninstall ------------------------------------------------------------------------
// Removes only what the launcher itself created. Everything else in the game folder is left alone.
struct UninstallPlan {
  fs::path gameDir;
  std::vector<fs::path> files;   // files the launcher created
  std::vector<fs::path> dirs;    // folders the launcher created; removed only if empty, deepest first
  fs::path xml;                  // AccountLogin.xml whose saved login gets blanked (empty = none)
  fs::path configDir, cacheDir;  // the launcher's own settings / cache folders (empty = none)
  bool legacy = false;           // no record of created files (older install): recognised by content instead
  bool empty() const { return files.empty() && dirs.empty() && xml.empty() && configDir.empty() && cacheDir.empty(); }
};

// True if `p` is strictly inside `base` (both absolute, no ".." tricks).
inline bool isUnder(const fs::path& p, const fs::path& base) {
  for (const auto& c : p) if (c == "..") return false;
  fs::path a = p.lexically_normal(), b = base.lexically_normal();
  if (!a.is_absolute() || !b.is_absolute()) return false;
  auto ai = a.begin(), bi = b.begin();
  for (; bi != b.end(); ++bi, ++ai) {
    if (bi->empty()) break;  // trailing slash
    if (ai == a.end() || *ai != *bi) return false;
  }
  return ai != a.end();
}

inline UninstallPlan makeUninstallPlan(const Config& cfg, TemplateFn tpl, const fs::path& configDir, const fs::path& cacheDir) {
  UninstallPlan P;
  P.configDir = configDir; P.cacheDir = cacheDir;
  if (cfg.wowExe.empty()) return P;
  fs::path exe(cfg.wowExe);
  P.gameDir = exe.parent_path();
  std::error_code ec;
  fs::path xml = glueDir(exe) / "AccountLogin.xml";
  if (fs::is_regular_file(xml, ec) && readAccount(readFile(xml))) P.xml = xml;
  std::set<std::string> seen;
  for (const auto& s : cfg.created) {
    fs::path p(s);
    if (!isUnder(p, P.gameDir) || !seen.insert(p.string()).second || fs::is_symlink(p, ec)) continue;
    if (fs::is_directory(p, ec)) P.dirs.push_back(p);
    else if (fs::is_regular_file(p, ec)) P.files.push_back(p);
  }
  if (cfg.created.empty()) {  // older install without a record: only files identical to our bundled ones
    for (const char* n : kGlueNames) {
      fs::path f = glueDir(exe) / n;
      if (!fs::is_regular_file(f, ec)) continue;
      std::string have = readFile(f), want = tpl(n);
      bool ours = have == want;
      if (!ours && std::string(n) == "AccountLogin.xml") {
        auto a = applyCreds(have, "", ""), b = applyCreds(want, "", "");
        ours = a && b && *a == *b;
      }
      if (ours) { P.files.push_back(f); P.legacy = true; }
    }
  }
  std::sort(P.dirs.begin(), P.dirs.end(), [](const fs::path& a, const fs::path& b) {
    return a.string().size() > b.string().size();
  });
  return P;
}

inline std::string homeShort(const fs::path& p) {
  std::string s = p.u8string();
  if (const char* h = getenv("HOME")) {
    std::string hs = h;
    if (!hs.empty() && hs != "/" && s.compare(0, hs.size(), hs) == 0 && (s.size() == hs.size() || s[hs.size()] == '/'))
      return "~" + s.substr(hs.size());
  }
  return s;
}
// Short human-readable summary of what the plan removes (for the confirm screen and --uninstall).
inline std::string describePlan(const UninstallPlan& P) {
  std::string o;
  auto rel = [&](const fs::path& p) { std::error_code ec; return fs::relative(p, P.gameDir, ec).u8string(); };
  if (!P.gameDir.empty()) o += "Game folder: " + homeShort(P.gameDir) + "\n";
  if (!P.files.empty()) {
    o += "Remove files: ";
    for (size_t i = 0; i < P.files.size(); i++) o += (i ? ", " : "") + rel(P.files[i]);
    o += "\n";
  }
  if (!P.dirs.empty()) {
    o += "Remove folders if empty: ";
    for (size_t i = 0; i < P.dirs.size(); i++) o += (i ? ", " : "") + rel(P.dirs[i]);
    o += "\n";
  }
  if (!P.xml.empty()) o += "Blank the saved login in AccountLogin.xml\n";
  if (!P.configDir.empty()) o += "Settings: " + homeShort(P.configDir) + "\n";
  if (!P.cacheDir.empty()) o += "Cache: " + homeShort(P.cacheDir) + "\n";
  if (!o.empty() && o.back() == '\n') o.pop_back();
  return o;
}

inline std::vector<std::string> runUninstall(const UninstallPlan& P) {
  std::vector<std::string> log;
  std::error_code ec;
  if (!P.xml.empty()) {
    auto out = applyCreds(readFile(P.xml), "", "");
    if (out && writeFile(P.xml, *out)) log.push_back("Blanked the saved login");
  }
  auto rel = [&](const fs::path& p) { std::error_code e; return fs::relative(p, P.gameDir, e).u8string(); };
  for (const auto& f : P.files) {
    if (!fs::is_regular_file(f, ec) || fs::is_symlink(f, ec)) continue;
    if (fs::remove(f, ec)) log.push_back("Removed " + rel(f));
    else log.push_back("Could not remove " + rel(f) + " (" + ec.message() + ")");
  }
  for (const auto& d : P.dirs) {
    if (!fs::is_directory(d, ec) || fs::is_symlink(d, ec)) continue;
    if (fs::directory_iterator(d, ec) != fs::directory_iterator()) { log.push_back("Kept " + rel(d) + " (not empty)"); continue; }
    if (fs::remove(d, ec)) log.push_back("Removed folder " + rel(d));
  }
  for (const fs::path* own : {&P.configDir, &P.cacheDir}) {   // our own folders only
    if (own->empty() || own->filename() != "unofficial-warmane-launcher" || !own->is_absolute()) continue;
    if (fs::exists(*own, ec)) { fs::remove_all(*own, ec); log.push_back("Removed " + homeShort(*own)); }
  }
  return log;
}

}  // namespace logic
