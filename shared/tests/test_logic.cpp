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
  Config cfg{"C:\\w\\Wow.exe", "Eve"};
  CHECK(saveConfig(d / "sub" / "config.ini", cfg));
  Config l = loadConfig(d / "sub" / "config.ini");
  CHECK(l.wowExe == cfg.wowExe && l.account == "Eve");

  printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
  return fails ? 1 : 0;
}
