#include <datadog/opentracing.h>
#include <exception>
#include <iostream>
#include <ios>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

int main(int argc, char* argv[]) {
  datadog::opentracing::TracerOptions tracer_options{"dd-agent", 8126, "david-hack-priority"};
  auto tracer = datadog::opentracing::makeTracer(tracer_options);

  std::unordered_map<std::string, std::unique_ptr<ot::Span>> spans;
  std::string line;
  std::stringstream stream;
  stream.exceptions(std::ios::failbit);
  std::string word;
  std::string value;
  while (std::getline(std::cin, line)) {
    try {
      stream.clear();
      stream.str(line);
      stream >> word;
      if (word == "start") {
        stream >> word;
        spans.emplace(word, tracer->StartSpan(word));
      } else if (word == "finish") {
        stream >> word;
        spans.erase(word);
      } else if (word == "set") {
        auto& name = word;
        auto& key = line;
        stream >> name >> key >> value;
        spans.at(name)->SetTag(key, value);
      } else {
        std::cerr << "Unknown command: " << line << "\nUse \"start <name>\" and \"finish <name>\" instead.\n";
      }
    } catch (const std::exception& error) {
      std::cerr << "Exception thrown: " << error.what() << '\n';
    }
  }

  tracer->Close();
  return 0;
}
