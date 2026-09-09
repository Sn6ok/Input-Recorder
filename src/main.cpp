// Input Recorder — application entry point.
//
// Phase 0 bootstrap: a minimal, verifiable executable that proves the
// build/run loop works end to end. The real Win32 message loop, tray icon and
// UI are introduced in later phases (spec §566-§568). Until then this is a
// console program that reports its version and exits cleanly.

#include "core/version.hpp"

#include <cstdio>
#include <string>

int main() {
    const std::string name(ir::kAppName);
    std::printf("%s %s\n", name.c_str(), ir::version_string().c_str());
    std::printf("Local input recovery utility. All data stays on this machine.\n");
    return 0;
}
