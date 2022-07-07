// This program takes a list of file paths as command line arguments.
// Each file contains input to pass to `LLVMFuzzerTestOneInput`.
// This program runs each input, and then prints a line containing the name of
// the file followed by the number of microseconds `LLVMFuzzerTestOneInput`
// took to run on the input.

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// Defined in `./glob.cpp`
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(int, char *argv[]) {
  std::ifstream file;
  std::stringstream buffer;
  std::string content;
  for (const char *const *arg = argv + 1; *arg; ++arg) {
    file.close();
    buffer.clear();

    const char *const filename = *arg;
    file.open(filename);
    buffer << file.rdbuf();
    content = buffer.str();
    const auto before = std::chrono::steady_clock::now();
    LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t *>(content.data()), content.size());
    const auto after = std::chrono::steady_clock::now();

    std::cout << filename << '\t'
              << std::chrono::duration_cast<std::chrono::microseconds>(after - before).count()
              << '\n';
  }
}
