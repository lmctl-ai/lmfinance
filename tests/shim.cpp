// Test shim for Windows, where lmcode discovery spawns "lmfinance" without a shell and so cannot run
// the sh script the Unix tests use. Same behavior: for each `--lmcode FILE` call it keeps a copy of
// every program file and records the host's exit code, then runs the real host.
// Built by test.sh with -DHOST_PATH="..." -DSHIM_DIR="..." (both native mixed-slash paths).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;

static int run_host(int argc, char** argv, int from) {
  std::vector<const char*> args{HOST_PATH};
  for (int i = from; i < argc; i++) args.push_back(argv[i]);
  args.push_back(nullptr);
  intptr_t code = _spawnv(_P_WAIT, HOST_PATH, args.data());
  return code < 0 ? 125 : static_cast<int>(code);
}

int main(int argc, char** argv) {
  if (argc < 2 || std::string(argv[1]) != "--lmcode") return run_host(argc, argv, 1);
  fs::path runs = fs::path(SHIM_DIR) / "runs";
  fs::create_directories(runs);
  fs::path tag = runs / ("run." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(GetTickCount64()));
  fs::create_directories(tag);
  int i = 0;
  for (int a = 1; a < argc; a++) {
    std::string arg = argv[a];
    if (arg.rfind("--", 0) == 0) continue;
    fs::copy_file(arg, tag / ("p" + std::to_string(i++)), fs::copy_options::overwrite_existing);
  }
  int code = run_host(argc, argv, 1);
  std::ofstream(tag / "code") << code << "\n";
  std::ofstream(fs::path(SHIM_DIR) / "last") << tag.generic_string() << "\n";
  return code;
}
