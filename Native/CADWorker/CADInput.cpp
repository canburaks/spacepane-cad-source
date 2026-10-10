#include "CADWorker.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace cad {
namespace {
int directoryInteger(std::string_view value) {
  std::string trimmed(value);
  char* end = nullptr;
  const long number = std::strtol(trimmed.c_str(), &end, 10);
  if (end == trimmed.c_str()) throw Failure{Exit::malformed};
  while (*end == ' ') ++end;
  if (*end || number < 0 || number > std::numeric_limits<int>::max())
    throw Failure{Exit::malformed};
  return static_cast<int>(number);
}

void preflightIGES(const std::string& bytes) {
  std::size_t directories = 0, parameters = 0, globals = 0, starts = 0, terminates = 0;
  std::size_t cursor = 0;
  char previous = 'S';
  const std::string order = "SGDPT";
  int entityType = 0;
  while (cursor < bytes.size()) {
    const std::size_t end = bytes.find('\n', cursor);
    std::string_view line(bytes.data() + cursor,
                          (end == std::string::npos ? bytes.size() : end) - cursor);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.size() != 80) throw Failure{Exit::malformed};
    const char section = line[72];
    if (order.find(section) == std::string::npos || order.find(section) < order.find(previous))
      throw Failure{Exit::malformed};
    previous = section;
    switch (section) {
      case 'S': ++starts; break;
      case 'G': ++globals; break;
      case 'P': ++parameters; break;
      case 'T': ++terminates; break;
      case 'D': {
        ++directories;
        if (directories > 2 * maximumEntities) throw Failure{Exit::budget};
        const int type = directoryInteger(line.substr(0, 8));
        if (directories % 2) entityType = type;
        else {
          if (type != entityType) throw Failure{Exit::malformed};
          const int form = directoryInteger(line.substr(32, 8));
          if (type == 416 || ((type == 406 || type == 402) && form == 12))
            throw Failure{Exit::unsupported};
        }
        break;
      }
    }
    cursor = end == std::string::npos ? bytes.size() : end + 1;
  }
  if (!starts || !globals || !directories || directories % 2 || !parameters || terminates != 1)
    throw Failure{Exit::malformed};
}

void preflightSTEP(const std::string& bytes) {
  if (bytes.find("ISO-10303-21;") == std::string::npos ||
      bytes.find("END-ISO-10303-21;") == std::string::npos) throw Failure{Exit::malformed};
  std::size_t entities = 0, tokens = 0, depth = 0;
  for (std::size_t i = 0; i < bytes.size();) {
    const unsigned char c = static_cast<unsigned char>(bytes[i]);
    if (std::isspace(c)) { ++i; continue; }
    if (++tokens > 2000000) throw Failure{Exit::budget};
    if (c == '/' && i + 1 < bytes.size() && bytes[i + 1] == '*') {
      const std::size_t end = bytes.find("*/", i + 2);
      if (end == std::string::npos) throw Failure{Exit::malformed};
      i = end + 2;
    } else if (c == '\'' || c == '"') {
      const char quote = bytes[i++];
      bool complete = false;
      while (i < bytes.size()) {
        if (bytes[i++] != quote) continue;
        if (i < bytes.size() && bytes[i] == quote) { ++i; continue; }
        complete = true; break;
      }
      if (!complete) throw Failure{Exit::malformed};
    } else if (c == '#') {
      ++i;
      const std::size_t begin = i;
      while (i < bytes.size() && std::isdigit(static_cast<unsigned char>(bytes[i]))) ++i;
      if (i == begin) throw Failure{Exit::malformed};
      if (i - begin > 9) throw Failure{Exit::budget};
      while (i < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[i]))) ++i;
      if (i < bytes.size() && bytes[i] == '=' && ++entities > maximumEntities)
        throw Failure{Exit::budget};
    } else if (std::isalpha(c) || c == '_') {
      const std::size_t begin = i++;
      while (i < bytes.size() && (std::isalnum(static_cast<unsigned char>(bytes[i])) || bytes[i] == '_')) ++i;
      if (i - begin > 128) throw Failure{Exit::budget};
      std::string identifier(bytes, begin, i - begin);
      std::transform(identifier.begin(), identifier.end(), identifier.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
      if (identifier.find("EXTERNAL") != std::string::npos || identifier == "DOCUMENT_FILE" ||
          identifier == "DOCUMENT_REFERENCE") throw Failure{Exit::unsupported};
    } else {
      if (c == '(' && ++depth > 128) throw Failure{Exit::budget};
      if (c == ')') {
        if (!depth) throw Failure{Exit::malformed};
        --depth;
      }
      ++i;
    }
  }
  if (!entities || depth) throw Failure{Exit::malformed};
}
}

void preflight(const std::string& bytes, bool isSTEP) {
  if (bytes.empty() || bytes.find('\0') != std::string::npos) throw Failure{Exit::malformed};
  if (bytes.size() > maximumInputBytes) throw Failure{Exit::budget};
  if (isSTEP) preflightSTEP(bytes); else preflightIGES(bytes);
}
}
