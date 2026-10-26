// Native (Linux/any) unit tests for logic.hpp. Build: g++ -std=c++17 -I../src test_logic.cpp
#include "logic.hpp"
#include <cassert>
#include <cstdio>
using namespace logic;

static const char* ASSET_DIR = ASSETS;
static std::string tpl(const std::string& n) { return readFile(fs::path(ASSET_DIR) / "glue" / n); }

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int fails = 0;

int main() {
  std::string x = tpl("AccountLogin.xml");
  CHECK(!x.empty());

  auto o = applyCreds(x, "Alice", "p@ss");
  CHECK(o.has_value());
  CHECK(o->find("AccountLoginAccountEdit:SetText(\"Alice\");") != std::string::npos);
  CHECK(o->find("AccountLoginPasswordEdit:SetText(\"p@ss\");") != std::string::npos);
  CHECK(o->find("gsub(\"Alice\", \"@@\", \"@\")") != std::string::npos);
  CHECK(o->find("her-") == std::string::npos);
  auto r = readAccount(*o);
  CHECK(r && r->account == "Alice" && r->hasPassword);

  // repeat login replaces old values
  auto b = applyCreds(*o, "Bob", "two");
  CHECK(b && readAccount(*b)->account == "Bob");
  CHECK(b->find("Alice") == std::string::npos);

  // special characters round-trip and don't break the XML around the block
  std::string pw = "a\"b\\c<d>&e";
  auto s = applyCreds(x, "x", pw);
  CHECK(s.has_value());
  CHECK(s->find("<d>") == std::string::npos);
  auto blk = findBlock(*s);
  auto sp = findLiteral(*s, blk->open, blk->close, "AccountLoginPasswordEdit:SetText(\"");
  CHECK(sp && decode(s->substr(sp->begin, sp->end - sp->begin)) == pw);

  // clearing
  auto c = applyCreds(*o, "", "");
  CHECK(c && readAccount(*c)->account.empty() && !readAccount(*c)->hasPassword);

  // missing block
  CHECK(!applyCreds("<Ui></Ui>", "a", "b").has_value());

  CHECK(isWowExe("C:/x/Wow.exe")); CHECK(isWowExe("/x/WOW.EXE"));
  CHECK(!isWowExe("/x/Wow-64.exe")); CHECK(!isWowExe("/x/notepad.exe"));

  // end-to-end on disk
  fs::path d = fs::temp_directory_path() / "wlnative-test";
  fs::remove_all(d); fs::create_directories(d);
  fs::path exe = d / "Wow.exe"; writeFile(exe, "MZ");
  GlueReport rep; std::string err;
  CHECK(ensureGlue(exe, tpl, rep, err) && rep.installed.size() == 4);
  GlueReport rep2; CHECK(ensureGlue(exe, tpl, rep2, err) && rep2.present.size() == 4);
  CHECK(writeCreds(exe, tpl, "Carol", "secret", err));
  auto ia = readInstalledAccount(exe);
  CHECK(ia && ia->account == "Carol" && ia->hasPassword);
  CHECK(readFile(glueDir(exe) / "AccountLogin.xml.origin") == tpl("AccountLogin.xml.origin"));

  // broken existing xml -> backup + replace
  writeFile(glueDir(exe) / "AccountLogin.xml", "<Ui>custom</Ui>");
  CHECK(writeCreds(exe, tpl, "Dan", "pw", err));
  CHECK(readFile(glueDir(exe) / "AccountLogin.xml.bak") == "<Ui>custom</Ui>");
  CHECK(readInstalledAccount(exe)->account == "Dan");

  // config
  Config cfg; cfg.wowExe = "C:\\w\\Wow.exe"; cfg.account = "Eve";
  CHECK(saveConfig(d / "sub" / "config.ini", cfg));
  Config l = loadConfig(d / "sub" / "config.ini");
  CHECK(l.wowExe == cfg.wowExe && l.account == "Eve");


  // ---- tracking what we create + uninstall
  {
    fs::path g = fs::temp_directory_path() / "wlnative-uninst"; fs::remove_all(g); fs::create_directories(g);
    fs::path exe = g / "Wow.exe"; writeFile(exe, "MZ");
    Config c; c.wowExe = exe.u8string();
    GlueReport r; std::string e;
    CHECK(ensureGlue(exe, tpl, r, e) && r.created.size() == 6);  // Interface, GlueXML + 4 files
    addCreated(c, r.created); addCreated(c, r.created);
    CHECK(c.created.size() == 6);
    std::vector<std::string> more; CHECK(writeCreds(exe, tpl, "Zed", "pw", e, &more) && more.empty());
    CHECK(saveConfig(g / "cfg" / "unofficial-warmane-launcher" / "config.ini", c));
    Config back = loadConfig(g / "cfg" / "unofficial-warmane-launcher" / "config.ini");
    CHECK(back.created == c.created && back.wowExe == c.wowExe);
    writeFile(g / "cache" / "x.ttf", "x");  // (no dir yet: writeFile fails, fine)
    fs::create_directories(g / "cache" / "unofficial-warmane-launcher"); writeFile(g / "cache" / "unofficial-warmane-launcher" / "f.ttf", "x");
    // the player later adds an add-on inside Interface, and one folder that is not ours
    fs::create_directories(g / "Interface" / "AddOns" / "MyAddon"); writeFile(g / "Interface" / "AddOns" / "MyAddon" / "x.lua", "1");
    auto plan = makeUninstallPlan(c, tpl, g / "cfg" / "unofficial-warmane-launcher", g / "cache" / "unofficial-warmane-launcher");
    CHECK(!plan.legacy && plan.files.size() == 4 && plan.dirs.size() == 2 && !plan.xml.empty());
    CHECK(describePlan(plan).find("AccountLogin.xml") != std::string::npos);
    auto log = runUninstall(plan);
    CHECK(!fs::exists(glueDir(exe)));                                   // GlueXML and its 4 files gone
    CHECK(fs::exists(g / "Interface" / "AddOns" / "MyAddon" / "x.lua"));  // user's add-on untouched, so Interface stays
    CHECK(fs::exists(exe));
    CHECK(!fs::exists(g / "cfg" / "unofficial-warmane-launcher") && !fs::exists(g / "cache" / "unofficial-warmane-launcher"));
    CHECK(!log.empty());

    // a clean game folder ends up exactly as it was
    fs::path g2 = fs::temp_directory_path() / "wlnative-uninst2"; fs::remove_all(g2); fs::create_directories(g2);
    fs::path exe2 = g2 / "Wow.exe"; writeFile(exe2, "MZ");
    Config c2; c2.wowExe = exe2.u8string(); GlueReport r2;
    CHECK(ensureGlue(exe2, tpl, r2, e)); addCreated(c2, r2.created);
    std::vector<std::string> mk; CHECK(mkdirTracked(glueDir(exe2).parent_path() / "AddOns", &mk, e)); addCreated(c2, mk);
    CHECK(writeCreds(exe2, tpl, "A", "b", e));
    runUninstall(makeUninstallPlan(c2, tpl, "", ""));
    int left = 0; for (auto& x : fs::directory_iterator(g2)) { (void)x; left++; }
    CHECK(left == 1 && fs::exists(exe2));                               // only Wow.exe remains

    // files that were already there (not created by us) are kept; their saved login is blanked
    fs::path g3 = fs::temp_directory_path() / "wlnative-uninst3"; fs::remove_all(g3); fs::create_directories(g3 / "Interface" / "GlueXML");
    fs::path exe3 = g3 / "Wow.exe"; writeFile(exe3, "MZ");
    writeFile(g3 / "Interface" / "GlueXML" / "AccountLogin.lua", "-- mine");
    Config c3; c3.wowExe = exe3.u8string(); GlueReport r3;
    CHECK(ensureGlue(exe3, tpl, r3, e) && r3.created.size() == 3);      // the 3 missing files only
    addCreated(c3, r3.created);
    CHECK(writeCreds(exe3, tpl, "Q", "secret", e));
    runUninstall(makeUninstallPlan(c3, tpl, "", ""));
    CHECK(readFile(g3 / "Interface" / "GlueXML" / "AccountLogin.lua") == "-- mine");
    CHECK(fs::exists(g3 / "Interface" / "GlueXML") && !fs::exists(g3 / "Interface" / "GlueXML" / "AccountLogin.xml"));

    // safety: entries outside the game folder or with ".." are ignored; folders not named after us are kept
    fs::path outside = fs::temp_directory_path() / "wlnative-outside.txt"; writeFile(outside, "keep");
    Config c4 = c3; c4.created = {outside.u8string(), (g3 / ".." / "wlnative-outside.txt").u8string(), "/etc", g3.u8string()};
    auto p4 = makeUninstallPlan(c4, tpl, g3 / "other", "");
    CHECK(p4.files.empty() && p4.dirs.empty());
    runUninstall(p4);
    CHECK(fs::exists(outside) && fs::exists(g3) && fs::exists(g3 / "other") == false);
    fs::create_directories(g3 / "other"); runUninstall(makeUninstallPlan(c4, tpl, g3 / "other", ""));
    CHECK(fs::exists(g3 / "other"));
    CHECK(isUnder("/a/b/c", "/a/b") && !isUnder("/a/b", "/a/b") && !isUnder("/a/bc", "/a/b") && !isUnder("/a/b/../x", "/a/b"));

    // an older install with no record: only files identical to our bundled ones are recognised
    fs::path g5 = fs::temp_directory_path() / "wlnative-uninst5"; fs::remove_all(g5); fs::create_directories(g5);
    fs::path exe5 = g5 / "Wow.exe"; writeFile(exe5, "MZ"); GlueReport r5; Config c5; c5.wowExe = exe5.u8string();
    CHECK(ensureGlue(exe5, tpl, r5, e) && writeCreds(exe5, tpl, "L", "pw", e));
    writeFile(glueDir(exe5) / "AccountLogin.lua", "-- changed by the player");
    auto p5 = makeUninstallPlan(c5, tpl, "", "");
    CHECK(p5.legacy && p5.files.size() == 3);
    runUninstall(p5);
    CHECK(readFile(glueDir(exe5) / "AccountLogin.lua") == "-- changed by the player" && !fs::exists(glueDir(exe5) / "AccountLogin.xml"));
  }

  printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
  return fails ? 1 : 0;
}
