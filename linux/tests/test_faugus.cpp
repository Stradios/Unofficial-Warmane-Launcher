#include "faugus.hpp"
#include <sys/stat.h>
#include <cstdio>
using namespace faugus;
int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static void put(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << s; }
int main() {
  fs::path h = fs::temp_directory_path() / "wlgtk-faugus-home"; fs::remove_all(h);
  setenv("HOME", h.c_str(), 1); unsetenv("XDG_DATA_HOME"); unsetenv("XDG_CONFIG_HOME");
  setenv("PATH", "/nonexistent", 1);
  // JSON edge cases
  auto j = parseJson(R"([{"a":"x\"yå😀","n":-1.5e2,"t":true,"z":null,"o":{"k":[1,2,{}]}}])");
  CHECK(j && j->t == J::Arr && j->a[0].str("a") == "x\"y\xc3\xa5\xf0\x9f\x98\x80");
  CHECK(!parseJson("[1,2") && !parseJson("{\"a\" 1}") && !parseJson(""));
  // no Faugus data -> nothing found, no umu, no wine -> error
  CHECK(findWowGames().empty());
  CHECK(!buildLaunch("/g/Wow.exe", std::nullopt).error.empty());
  // Faugus data
  fs::path d = h / ".local/share/faugus-launcher";
  put(d / "games.json", std::string("[") +
    R"({"title":"Other","gameid":"other","path":"/x/game.exe","prefix":"~/Faugus/other","runner":"Proton-GE Latest"},)" +
    R"({"title":"WoW Warmane","gameid":"wow","path":"~/Games/WoW 3.3.5a/WoW.EXE","prefix":"~/Faugus/wow","runner":"Default","protonfix":"umu-12345","launch_arguments":"WINEDLLOVERRIDES=winmm=n,b","game_arguments":""}])");
  put(h / ".config/faugus-launcher/config.json", R"({"default-runner":"\"GE-Proton9-20\""})");
  fs::create_directories(h / ".local/share/Steam/compatibilitytools.d/GE-Proton9-20");
  fs::create_directories(h / ".local/share/Steam/compatibilitytools.d/GE-Proton10-4");
  fs::create_directories(h / ".local/share/Steam/compatibilitytools.d/Proton-9.0");
  put(d / "umu-run", "#!/bin/sh\n"); chmod((d / "umu-run").c_str(), 0755);
  auto all = findWowGames();
  CHECK(all.size() == 1 && all[0].title == "WoW Warmane" && all[0].path == (h / "Games/WoW 3.3.5a/WoW.EXE").string());
  CHECK(defaultRunner() == "GE-Proton9-20");
  auto pr = installedProtons();
  CHECK(pr.size() == 3 && pr[0] == "GE-Proton10-4" && pr[2] == "Proton-9.0");
  CHECK(newestGE(pr) == "GE-Proton10-4");
  fs::path exe = h / "Games/WoW 3.3.5a/WoW.EXE"; put(exe, "MZ");
  CHECK(!pickWow(all, h / "Games/Other/Wow.exe").has_value());  // different Wow.exe: do not borrow settings
  auto L = buildLaunch(exe, pickWow(all, exe));
  printf("%s\n", L.command.c_str());
  CHECK(L.error.empty() && L.via == "Faugus umu-run");
  CHECK(L.command.find("GAMEID='umu-12345'") != std::string::npos);
  CHECK(L.command.find("WINEPREFIX='" + (h / "Faugus/wow").string() + "'") != std::string::npos);
  CHECK(L.command.find("PROTONPATH='GE-Proton9-20'") != std::string::npos);
  CHECK(L.command.find("WINEDLLOVERRIDES=winmm=n,b ") != std::string::npos);
  CHECK(L.command.find("cd '" + exe.parent_path().string() + "' && ") == 0);
  // no Faugus entry: newest GE-Proton, no prefix
  auto L2 = buildLaunch("/other dir/Wow.exe", std::nullopt);
  CHECK(L2.command.find("PROTONPATH='GE-Proton10-4'") != std::string::npos && L2.command.find("WINEPREFIX") == std::string::npos);
  // quoting
  CHECK(shq("a'b") == "'a'\\''b'");
  printf(fails ? "%d FAILED\n" : "all faugus tests passed\n", fails);
  return fails ? 1 : 0;
}
