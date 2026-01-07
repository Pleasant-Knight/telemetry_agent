#include <cstdlib>
#include <iostream>
#include <string>

int main() {
#ifdef _WIN32
  const std::string exe = "full_agent.exe";
#else
  const std::string exe = "./full_agent";
#endif
  const std::string cmd = exe + " run --scenario B";
  const int rc = std::system(cmd.c_str());
  if (rc != 0) {
    std::cerr << "full_agent scenario B failed, rc=" << rc << "\n";
    return 1;
  }
  return 0;
}
