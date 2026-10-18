#include <switch.h>
#include <cstdio>

int main() {
  consoleInit(nullptr);
  std::printf("Carbon SDK build smoke test; this is not the game.\n");
  consoleUpdate(nullptr);
  consoleExit(nullptr);
  return 0;
}
