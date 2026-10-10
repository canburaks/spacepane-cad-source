// The length unit a STEP or IGES file declares, read from the bytes the worker already preflighted.
//
// This reads no more than the unit: the IGES Global section's unit flag and name (parameters 14 and 15), and the length
// unit that STEP's GLOBAL_UNIT_ASSIGNED_CONTEXT entities point at. Names, authors, product identifiers, timestamps and every
// other string are never copied out of the token stream, and nothing here reaches the kernel. Every function answers
// `LengthUnit::none` for anything it cannot read or that is not unambiguous: a file that declares two different length units
// in different contexts, a unit with no protocol code (miles, kilometres, mils, microinches), a malformed section.
// The protocol's positions are millimetres whatever the answer is; the unit only labels them.
#include "CADWorker.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cad {
namespace {
bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool isAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool isAlnumOrUnderscore(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

std::string upper(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return result;
}

bool equalsIgnoringCase(std::string_view text, std::string_view keyword) {
  return text.size() == keyword.size() && upper(text) == keyword;
}

std::string_view trimmed(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
  return text;
}

// ---- IGES -------------------------------------------------------------------------------------------------------------

constexpr std::size_t maximumGlobalBytes = 8 * 1024;

// Columns 1-72 of the contiguous G records, concatenated. Empty when there is no Global section.
std::string globalSection(std::string_view bytes) {
  std::string global;
  bool began = false;
  std::size_t cursor = 0;
  while (cursor < bytes.size()) {
    const std::size_t end = bytes.find('\n', cursor);
    std::string_view line = bytes.substr(cursor, (end == std::string_view::npos ? bytes.size() : end) - cursor);
    cursor = end == std::string_view::npos ? bytes.size() : end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.size() < 73) {
      if (began) break;
      continue;
    }
    if (line[72] == 'G') {
      began = true;
      global.append(line.substr(0, 72));
      if (global.size() > maximumGlobalBytes) return {};
    } else if (began) {
      break;
    }
  }
  return global;
}

// Reads the Global section's parameters one at a time. Hollerith strings (`nHtext`) may hold delimiters.
struct GlobalReader {
  std::string_view text;
  std::size_t position = 0;
  char parameterDelimiter = ',';
  char recordDelimiter = ';';
  bool finished = false;

  void skipSpaces() {
    while (position < text.size() && text[position] == ' ') ++position;
  }

  enum class Hollerith { none, value, malformed };
  Hollerith hollerith(std::string_view& value) {
    std::size_t index = position, length = 0, digits = 0;
    while (index < text.size() && isDigit(text[index])) {
      length = length * 10 + static_cast<std::size_t>(text[index] - '0');
      ++index;
      if (++digits > 3) return Hollerith::none;
    }
    if (digits == 0 || index >= text.size() || (text[index] != 'H' && text[index] != 'h')) return Hollerith::none;
    const std::size_t start = index + 1;
    if (length > text.size() - start) return Hollerith::malformed;
    value = text.substr(start, length);
    position = start + length;
    return Hollerith::value;
  }

  // Parameters 1 and 2 define the delimiters: empty for the default, or `1Hc`.
  bool readDelimiters() {
    for (int slot = 0; slot < 2; ++slot) {
      skipSpaces();
      if (position >= text.size()) return false;
      std::string_view value;
      switch (hollerith(value)) {
        case Hollerith::value:
          if (value.size() != 1) return false;
          (slot == 0 ? parameterDelimiter : recordDelimiter) = value.front();
          skipSpaces();
          if (position < text.size() && (text[position] == ',' || text[position] == parameterDelimiter)) ++position;
          break;
        case Hollerith::malformed:
          return false;
        case Hollerith::none:
          if (text[position] != parameterDelimiter) return false;
          ++position;
          break;
      }
    }
    return true;
  }
};

// A flag or small integer written as one to four digits; -1 for anything else.
int smallInteger(std::string_view text) {
  if (text.empty() || text.size() > 4) return -1;
  int value = 0;
  for (const char c : text) {
    if (!isDigit(c)) return -1;
    value = value * 10 + (c - '0');
  }
  return value;
}

LengthUnit igesFlagUnit(int flag, std::string_view name) {
  switch (flag) {
    case 1: return LengthUnit::inch;
    case 2: return LengthUnit::millimetre;
    case 3: return lengthUnitNamed(name);
    case 4: return LengthUnit::foot;
    case 6: return LengthUnit::metre;
    case 9: return LengthUnit::micrometre;
    case 10: return LengthUnit::centimetre;
    default: return LengthUnit::none;  // 5 miles, 7 kilometres, 8 mils, 11 microinches and anything else
  }
}

// ---- STEP -------------------------------------------------------------------------------------------------------------

struct Token {
  enum class Kind { end, word, reference, enumeration, string, punctuation, other };
  Kind kind = Kind::end;
  std::string_view text;       // word, enumeration (without its dots) or string content (quotes not included)
  std::uint64_t number = 0;    // reference
  char mark = 0;               // punctuation
};

bool isMark(const Token& token, char mark) { return token.kind == Token::Kind::punctuation && token.mark == mark; }

// Part 21 tokens: comments are skipped, strings are one token, `#12` is a reference, `.NAME.` an enumeration. The text of a
// string is a view into the input; it is compared and dropped, never stored.
class Lexer {
 public:
  explicit Lexer(std::string_view bytes) : bytes_(bytes) {}

  Token next() {
    while (position_ < bytes_.size()) {
      const char c = bytes_[position_];
      if (std::isspace(static_cast<unsigned char>(c))) { ++position_; continue; }
      if (c == '/' && position_ + 1 < bytes_.size() && bytes_[position_ + 1] == '*') {
        const std::size_t end = bytes_.find("*/", position_ + 2);
        if (end == std::string_view::npos) { position_ = bytes_.size(); return {}; }
        position_ = end + 2;
        continue;
      }
      if (c == '\'') return string();
      if (c == '#') return reference();
      if (isAlpha(c) || c == '_') return word();
      if (c == '.') return enumeration();
      ++position_;
      if (c == '(' || c == ')' || c == ',' || c == '=' || c == ';' || c == '$' || c == '*') {
        Token token;
        token.kind = Token::Kind::punctuation;
        token.mark = c;
        return token;
      }
      Token other;
      other.kind = Token::Kind::other;
      return other;
    }
    return {};
  }

 private:
  Token string() {
    std::size_t index = position_ + 1;
    while (index < bytes_.size()) {
      if (bytes_[index] == '\'') {
        if (index + 1 < bytes_.size() && bytes_[index + 1] == '\'') { index += 2; continue; }
        break;
      }
      ++index;
    }
    if (index >= bytes_.size()) { position_ = bytes_.size(); return {};  }
    Token token;
    token.kind = Token::Kind::string;
    token.text = bytes_.substr(position_ + 1, index - position_ - 1);
    position_ = index + 1;
    return token;
  }

  Token reference() {
    std::size_t index = position_ + 1, digits = 0;
    std::uint64_t value = 0;
    while (index < bytes_.size() && isDigit(bytes_[index])) {
      if (digits < 18) value = value * 10 + static_cast<std::uint64_t>(bytes_[index] - '0');
      ++digits;
      ++index;
    }
    Token token;
    if (digits == 0 || digits > 18) {
      token.kind = Token::Kind::other;
      position_ = digits == 0 ? position_ + 1 : index;
      return token;
    }
    token.kind = Token::Kind::reference;
    token.number = value;
    position_ = index;
    return token;
  }

  Token word() {
    std::size_t index = position_;
    while (index < bytes_.size() && isAlnumOrUnderscore(bytes_[index])) ++index;
    Token token;
    token.kind = Token::Kind::word;
    token.text = bytes_.substr(position_, index - position_);
    position_ = index;
    return token;
  }

  Token enumeration() {
    std::size_t index = position_ + 1;
    while (index < bytes_.size() && isAlnumOrUnderscore(bytes_[index])) ++index;
    Token token;
    if (index > position_ + 1 && index < bytes_.size() && bytes_[index] == '.') {
      token.kind = Token::Kind::enumeration;
      token.text = bytes_.substr(position_ + 1, index - position_ - 1);
      position_ = index + 1;
    } else {
      token.kind = Token::Kind::other;
      ++position_;
    }
    return token;
  }

  std::string_view bytes_;
  std::size_t position_ = 0;
};

// Unit and context entities are a few dozen tokens; anything longer is geometry and is dropped unread.
constexpr std::size_t maximumEntityTokens = 256;
constexpr std::size_t maximumUnitEntities = 256;
constexpr std::size_t maximumContexts = 64;
constexpr std::size_t maximumContextReferences = 16;

struct UnitEntity {
  std::uint64_t id;
  LengthUnit unit;
};

// `SI_UNIT(<prefix or $>, .METRE.)` -> the length unit, for the prefixes the protocol has a code for.
LengthUnit siLengthUnit(const std::vector<Token>& tokens, std::size_t at) {
  if (at + 4 >= tokens.size() || !isMark(tokens[at + 1], '(') || !isMark(tokens[at + 3], ',') ||
      tokens[at + 4].kind != Token::Kind::enumeration || !equalsIgnoringCase(tokens[at + 4].text, "METRE"))
    return LengthUnit::none;
  const Token& prefix = tokens[at + 2];
  if (isMark(prefix, '$')) return LengthUnit::metre;
  if (prefix.kind != Token::Kind::enumeration) return LengthUnit::none;
  if (equalsIgnoringCase(prefix.text, "MILLI")) return LengthUnit::millimetre;
  if (equalsIgnoringCase(prefix.text, "CENTI")) return LengthUnit::centimetre;
  if (equalsIgnoringCase(prefix.text, "MICRO")) return LengthUnit::micrometre;
  return LengthUnit::none;
}

struct StepFacts {
  std::vector<UnitEntity> units;
  std::vector<std::vector<std::uint64_t>> contexts;
  bool overflow = false;
};

// One finished entity: remember it when it is a length unit, and remember the unit list of a global unit context.
void inspectEntity(std::uint64_t id, const std::vector<Token>& tokens, StepFacts& facts) {
  bool isLength = false, isConversion = false;
  LengthUnit si = LengthUnit::none, conversion = LengthUnit::none;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].kind != Token::Kind::word) continue;
    const std::string_view word = tokens[i].text;
    if (equalsIgnoringCase(word, "LENGTH_UNIT")) {
      isLength = true;
    } else if (equalsIgnoringCase(word, "SI_UNIT")) {
      si = siLengthUnit(tokens, i);
    } else if (equalsIgnoringCase(word, "CONVERSION_BASED_UNIT")) {
      isConversion = true;
      if (i + 2 < tokens.size() && isMark(tokens[i + 1], '(') && tokens[i + 2].kind == Token::Kind::string)
        conversion = lengthUnitNamed(tokens[i + 2].text);
    } else if (equalsIgnoringCase(word, "GLOBAL_UNIT_ASSIGNED_CONTEXT")) {
      std::vector<std::uint64_t> references;
      int nesting = 0;
      for (std::size_t j = i + 1; j < tokens.size(); ++j) {
        if (isMark(tokens[j], '(')) {
          ++nesting;
        } else if (isMark(tokens[j], ')')) {
          if (--nesting <= 0) break;
        } else if (tokens[j].kind == Token::Kind::reference && nesting >= 1 && references.size() < maximumContextReferences) {
          references.push_back(tokens[j].number);
        }
      }
      if (facts.contexts.size() >= maximumContexts) facts.overflow = true;
      else facts.contexts.push_back(std::move(references));
    }
  }
  if (!isLength) return;
  if (facts.units.size() >= maximumUnitEntities) { facts.overflow = true; return; }
  facts.units.push_back({id, isConversion ? conversion : si});
}

StepFacts scanStep(std::string_view bytes) {
  StepFacts facts;
  Lexer lexer(bytes);
  std::vector<Token> entity;
  std::uint64_t entityId = 0;
  bool inEntity = false, tooLarge = false;
  int depth = 0;
  Token token = lexer.next();
  while (token.kind != Token::Kind::end) {
    if (!inEntity) {
      if (token.kind == Token::Kind::reference) {
        const Token following = lexer.next();
        if (isMark(following, '=')) {
          inEntity = true;
          entityId = token.number;
          entity.clear();
          depth = 0;
          tooLarge = false;
          token = lexer.next();
        } else {
          token = following;  // not an entity head: look at the token that followed
        }
        continue;
      }
      token = lexer.next();
      continue;
    }
    if (isMark(token, ';') && depth == 0) {
      if (!tooLarge) inspectEntity(entityId, entity, facts);
      inEntity = false;
    } else {
      if (isMark(token, '(')) ++depth;
      else if (isMark(token, ')') && depth > 0) --depth;
      if (!tooLarge) {
        if (entity.size() < maximumEntityTokens) entity.push_back(token);
        else tooLarge = true;
      }
    }
    token = lexer.next();
  }
  return facts;
}
}  // namespace

LengthUnit lengthUnitNamed(std::string_view name) {
  struct Entry { const char* name; LengthUnit unit; };
  static const Entry table[] = {
    {"UM", LengthUnit::micrometre}, {"MICRON", LengthUnit::micrometre}, {"MICRONS", LengthUnit::micrometre},
    {"MICROMETRE", LengthUnit::micrometre}, {"MICROMETER", LengthUnit::micrometre},
    {"MICROMETRES", LengthUnit::micrometre}, {"MICROMETERS", LengthUnit::micrometre},
    {"MM", LengthUnit::millimetre}, {"MILLIMETRE", LengthUnit::millimetre}, {"MILLIMETER", LengthUnit::millimetre},
    {"MILLIMETRES", LengthUnit::millimetre}, {"MILLIMETERS", LengthUnit::millimetre},
    {"CM", LengthUnit::centimetre}, {"CENTIMETRE", LengthUnit::centimetre}, {"CENTIMETER", LengthUnit::centimetre},
    {"CENTIMETRES", LengthUnit::centimetre}, {"CENTIMETERS", LengthUnit::centimetre},
    {"M", LengthUnit::metre}, {"METRE", LengthUnit::metre}, {"METER", LengthUnit::metre},
    {"METRES", LengthUnit::metre}, {"METERS", LengthUnit::metre},
    {"IN", LengthUnit::inch}, {"INCH", LengthUnit::inch}, {"INCHES", LengthUnit::inch},
    {"FT", LengthUnit::foot}, {"FOOT", LengthUnit::foot}, {"FEET", LengthUnit::foot},
  };
  const std::string key = upper(trimmed(name));
  for (const Entry& entry : table)
    if (key == entry.name) return entry.unit;
  return LengthUnit::none;
}

LengthUnit igesLengthUnit(std::string_view bytes) {
  const std::string global = globalSection(bytes);
  if (global.empty()) return LengthUnit::none;
  GlobalReader reader{global};
  if (!reader.readDelimiters()) return LengthUnit::none;
  int flag = -1;
  std::string_view name;
  for (int index = 3; index <= 15 && !reader.finished; ++index) {
    reader.skipSpaces();
    if (reader.position >= global.size()) break;
    const char byte = global[reader.position];
    if (byte == reader.recordDelimiter) break;
    if (byte == reader.parameterDelimiter) { ++reader.position; continue; }  // an empty parameter
    std::string_view value;
    const GlobalReader::Hollerith kind = reader.hollerith(value);
    if (kind == GlobalReader::Hollerith::malformed) break;
    if (kind == GlobalReader::Hollerith::value) {
      if (index == 15) name = value;
      reader.skipSpaces();
      if (reader.position < global.size()) {
        if (global[reader.position] == reader.parameterDelimiter) ++reader.position;
        else if (global[reader.position] == reader.recordDelimiter) reader.finished = true;
      }
      continue;
    }
    const std::size_t start = reader.position;
    while (reader.position < global.size() && global[reader.position] != reader.parameterDelimiter &&
           global[reader.position] != reader.recordDelimiter)
      ++reader.position;
    std::size_t end = reader.position;
    while (end > start && global[end - 1] == ' ') --end;
    if (index == 14) flag = smallInteger(std::string_view(global).substr(start, end - start));
    if (reader.position < global.size()) {
      if (global[reader.position] == reader.recordDelimiter) reader.finished = true;
      else ++reader.position;
    }
  }
  return igesFlagUnit(flag, name);
}

LengthUnit stepLengthUnit(std::string_view bytes) {
  const StepFacts facts = scanStep(bytes);
  if (facts.overflow || facts.contexts.empty()) return LengthUnit::none;
  LengthUnit resolved = LengthUnit::none;
  bool first = true;
  for (const auto& references : facts.contexts) {
    int matches = 0;
    LengthUnit unit = LengthUnit::none;
    for (const std::uint64_t reference : references) {
      for (const UnitEntity& entity : facts.units) {
        if (entity.id != reference) continue;
        ++matches;
        unit = entity.unit;
      }
    }
    // Exactly one length unit per context, and one the protocol has a code for.
    if (matches != 1 || unit == LengthUnit::none) return LengthUnit::none;
    if (first) { resolved = unit; first = false; }
    else if (unit != resolved) return LengthUnit::none;
  }
  return resolved;
}

LengthUnit declaredLengthUnit(const std::string& bytes, bool isSTEP) {
  return isSTEP ? stepLengthUnit(bytes) : igesLengthUnit(bytes);
}
}
