// Protocol and scanner checks that need no Open CASCADE: the worker's exit codes, the unit word, the 72-byte header, the
// preflight budgets and the declared-unit scanners. Built by Tests/test_protocol.py with the system C++ compiler from
// CADInput.cpp, CADProtocol.cpp and CADUnit.cpp, or by the CADProtocolTests CMake target.
//
//   ProtocolTests list                       print the case names
//   ProtocolTests run <case> <fixtures-dir>  run one case; exit 0 when every check passed
#include "CADWorker.hpp"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {
using cad::Exit;
using cad::LengthUnit;

struct Context {
  std::string fixtures;
  int failures = 0;
  int checks = 0;
};
Context context;

void check(bool condition, const char* expression, int line) {
  ++context.checks;
  if (condition) return;
  ++context.failures;
  std::fprintf(stderr, "  FAILED line %d: %s\n", line, expression);
}
#define CHECK(condition) check((condition), #condition, __LINE__)
#define CHECK_UNIT(actual, expected) \
  do { \
    const int a = static_cast<int>(actual), e = static_cast<int>(expected); \
    check(a == e, #actual " == " #expected, __LINE__); \
    if (a != e) std::fprintf(stderr, "    got %d, expected %d\n", a, e); \
  } while (0)

std::string readFixture(const std::string& name) {
  std::ifstream stream(context.fixtures + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << stream.rdbuf();
  return text.str();
}

// Runs `body` and returns the exit status of the Failure it throws, or -1 when nothing is thrown.
int failureOf(const std::function<void()>& body) {
  try {
    body();
  } catch (const cad::Failure& failure) {
    return static_cast<int>(failure.status);
  }
  return -1;
}

// ---- IGES builders -------------------------------------------------------------------------------------------------

std::string igesRecord(const std::string& text, char section, int number) {
  std::string line = text.substr(0, 72);
  line.resize(72, ' ');
  line += section;
  char sequence[16];
  std::snprintf(sequence, sizeof sequence, "%7d", number);
  return line + sequence + "\n";
}

// A file with a Start line, the Global section split into 72-column records, and a trailing record of another section.
std::string igesFile(const std::string& global) {
  std::string file = igesRecord("synthetic", 'S', 1);
  int number = 1;
  for (std::size_t offset = 0; offset < global.size(); offset += 72) file += igesRecord(global.substr(offset, 72), 'G', number++);
  file += igesRecord("     402       1       0       0       0       0       0       000000000", 'D', 1);
  return file;
}

// Parameters 1 to 15 with the unit flag and the unit-name field chosen by the caller.
std::string standardGlobal(const std::string& flag, const std::string& nameField) {
  return ",,4HTEST,8Hfile.igs,5Hnativ,5Hpreps,32,308,15,308,15,,1.," + flag + "," + nameField +
         ",1,0.01,15H20000101.000000,1E-07,30.,6Hauthor,3Horg,11,0,15H20000101.000000,;";
}

// ---- STEP builders -------------------------------------------------------------------------------------------------

std::string stepFile(const std::string& body) {
  return "ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('synthetic'),'2;1');\nENDSEC;\nDATA;\n" + body +
         "ENDSEC;\nEND-ISO-10303-21;\n";
}

std::string lengthUnit(int id, const std::string& prefix) {
  return "#" + std::to_string(id) + " = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(" + prefix + ",.METRE.) );\n";
}

std::string conversionUnit(int id, const std::string& name) {
  return "#" + std::to_string(id) + " = ( CONVERSION_BASED_UNIT('" + name + "',#40) LENGTH_UNIT() NAMED_UNIT(#41) );\n";
}

const char* const angleUnits =
    "#2 = ( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) );\n"
    "#3 = ( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() );\n";

std::string globalContext(int id, const std::string& references) {
  return "#" + std::to_string(id) + " = ( GEOMETRIC_REPRESENTATION_CONTEXT(3) \n"
         "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#9)) GLOBAL_UNIT_ASSIGNED_CONTEXT\n(" + references +
         ") REPRESENTATION_CONTEXT('Context #1',\n  '3D Context with UNIT and UNCERTAINTY') );\n";
}

LengthUnit stepUnit(const std::string& body) { return cad::stepLengthUnit(stepFile(body)); }

// ---- Cases ---------------------------------------------------------------------------------------------------------

void exitCodesAreTheDocumentedProtocol() {
  // CADWorkerProcess.swift maps these numbers; changing one silently would change every failure message.
  CHECK(static_cast<int>(Exit::success) == 0);
  CHECK(static_cast<int>(Exit::malformed) == 2);
  CHECK(static_cast<int>(Exit::unsupported) == 3);
  CHECK(static_cast<int>(Exit::budget) == 4);
  CHECK(static_cast<int>(Exit::internal) == 5);
  CHECK(static_cast<int>(Exit::geometry) == 6);
}

void unitWordTable() {
  // Independent literals: byte 0 is 1 whatever the unit, byte 1 the declared unit, the upper half zero.
  CHECK(cad::unitWord(LengthUnit::none) == 0x001u + 0x000u);
  CHECK(cad::unitWord(LengthUnit::micrometre) == 0x101u);
  CHECK(cad::unitWord(LengthUnit::millimetre) == 0x201u);
  CHECK(cad::unitWord(LengthUnit::centimetre) == 0x301u);
  CHECK(cad::unitWord(LengthUnit::metre) == 0x401u);
  CHECK(cad::unitWord(LengthUnit::inch) == 0x501u);
  CHECK(cad::unitWord(LengthUnit::foot) == 0x601u);
  CHECK(static_cast<int>(LengthUnit::none) == 0 && static_cast<int>(LengthUnit::foot) == 6);
}

cad::Mesh triangle(LengthUnit unit) {
  cad::Mesh mesh{};
  mesh.bounds = {0, 0, 0, 10, 10, 0};
  mesh.faces = 1;
  mesh.vertices = {{{-1, -1, 0}, {0, 0, 1}}, {{1, -1, 0}, {0, 0, 1}}, {{0, 1, 0}, {0, 0, 1}}};
  mesh.indices = {0, 1, 2};
  mesh.declaredUnit = unit;
  return mesh;
}

std::string written(const cad::Mesh& mesh) {
  int descriptors[2];
  CHECK(::pipe(descriptors) == 0);
  cad::writeMesh(descriptors[1], mesh);  // 156 bytes: far below the pipe's capacity, so it never blocks
  ::close(descriptors[1]);
  std::string output;
  char buffer[512];
  for (;;) {
    const ssize_t count = ::read(descriptors[0], buffer, sizeof buffer);
    if (count <= 0) break;
    output.append(buffer, static_cast<std::size_t>(count));
  }
  ::close(descriptors[0]);
  return output;
}

std::uint32_t word(const std::string& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (int byte = 3; byte >= 0; --byte) value = (value << 8) | static_cast<unsigned char>(data[offset + static_cast<std::size_t>(byte)]);
  return value;
}

void headerCarriesTheMagicCountsAndUnitWord() {
  const LengthUnit units[] = {LengthUnit::none, LengthUnit::micrometre, LengthUnit::millimetre, LengthUnit::centimetre,
                              LengthUnit::metre, LengthUnit::inch, LengthUnit::foot};
  const std::uint32_t words[] = {0x001, 0x101, 0x201, 0x301, 0x401, 0x501, 0x601};
  for (int index = 0; index < 7; ++index) {
    const std::string data = written(triangle(units[index]));
    CHECK(data.size() == 72 + 3 * 24 + 3 * 4);
    CHECK(data.compare(0, 8, "PRVCAD01") == 0);
    CHECK(word(data, 8) == 3);   // vertices
    CHECK(word(data, 12) == 3);  // indices
    CHECK(word(data, 16) == 1);  // faces
    CHECK(word(data, 20) == words[index]);
    CHECK((word(data, 20) & 0xFF) == 1);
    CHECK((word(data, 20) >> 16) == 0);
  }
  // A mesh nobody assigned a unit to writes the word the protocol had before the field existed.
  cad::Mesh untouched = triangle(LengthUnit::none);
  CHECK(word(written(untouched), 20) == 1);
}

void boundsAndPayloadAreLittleEndianAndExact() {
  const std::string data = written(triangle(LengthUnit::inch));
  double maximumX = 0;
  std::memcpy(&maximumX, data.data() + 24 + 3 * 8, sizeof maximumX);  // bounds: 6 doubles from offset 24
  CHECK(maximumX == 10.0);
  float firstX = 0;
  std::memcpy(&firstX, data.data() + 72, sizeof firstX);
  CHECK(firstX == -1.0f);
  CHECK(word(data, 72 + 3 * 24) == 0 && word(data, 72 + 3 * 24 + 4) == 1 && word(data, 72 + 3 * 24 + 8) == 2);
}

void anOversizeMeshIsAGeometryFailureAndWritesNothing() {
  // 72 + 24 * vertices + 4 * indices against the 32 MiB ceiling: 1,398,098 vertices fit, 1,398,099 do not.
  cad::Mesh fits = triangle(LengthUnit::millimetre);
  fits.vertices.assign(1398098, cad::Vertex{{0, 0, 0}, {0, 0, 1}});
  fits.indices.clear();
  const int sink = ::open("/dev/null", O_WRONLY);
  CHECK(sink >= 0);
  CHECK(failureOf([&] { cad::writeMesh(sink, fits); }) == -1);
  cad::Mesh over = fits;
  over.vertices.push_back(cad::Vertex{{0, 0, 0}, {0, 0, 1}});
  int descriptors[2];
  CHECK(::pipe(descriptors) == 0);
  CHECK(failureOf([&] { cad::writeMesh(descriptors[1], over); }) == static_cast<int>(Exit::geometry));
  ::close(descriptors[1]);
  char byte;
  CHECK(::read(descriptors[0], &byte, 1) == 0);  // EOF with nothing written
  ::close(descriptors[0]);
  ::close(sink);
}

void preflightKeepsItsBudgetsAndTypes() {
  CHECK(failureOf([] { cad::preflight("", true); }) == static_cast<int>(Exit::malformed));
  CHECK(failureOf([] { cad::preflight(std::string("ISO-10303-21;\0x", 15), true); }) == static_cast<int>(Exit::malformed));
  CHECK(failureOf([] { cad::preflight("arbitrary synthetic malformed CAD", false); }) == static_cast<int>(Exit::malformed));
  CHECK(failureOf([] { cad::preflight(std::string(cad::maximumInputBytes + 1, 'x'), true); }) == static_cast<int>(Exit::budget));
  std::string nested;
  for (int i = 0; i < 129; ++i) nested += "#1=(";
  nested += std::string(129, ')');
  CHECK(failureOf([&] { cad::preflight(stepFile(nested), true); }) == static_cast<int>(Exit::budget));
  std::string entities;
  for (int i = 1; i <= 100001; ++i) entities += "#" + std::to_string(i) + "=CARTESIAN_POINT('',(0.,0.,0.));";
  CHECK(failureOf([&] { cad::preflight(stepFile(entities), true); }) == static_cast<int>(Exit::budget));
  CHECK(failureOf([] { cad::preflight(stepFile("#1=EXTERNALLY_DEFINED_ITEM('secret',#2);"), true); }) == static_cast<int>(Exit::unsupported));
  CHECK(failureOf([] { cad::preflight(stepFile("#1=DOCUMENT_FILE('x');"), true); }) == static_cast<int>(Exit::unsupported));
  CHECK(failureOf([] { cad::preflight(stepFile("#1=POINT('',(0.,0.,0.));"), true); }) == -1);
}

void theBoxFixturesPassPreflight() {
  CHECK(failureOf([] { cad::preflight(readFixture("box.step"), true); }) == -1);
  CHECK(failureOf([] { cad::preflight(readFixture("box.iges"), false); }) == -1);
}

void theBoxFixturesDeclareMillimetres() {
  const std::string step = readFixture("box.step"), iges = readFixture("box.iges");
  CHECK(!step.empty() && !iges.empty());
  CHECK_UNIT(cad::declaredLengthUnit(step, true), LengthUnit::millimetre);
  CHECK_UNIT(cad::declaredLengthUnit(iges, false), LengthUnit::millimetre);
  // The same bytes read as the other format declare nothing.
  CHECK_UNIT(cad::declaredLengthUnit(step, false), LengthUnit::none);
  CHECK_UNIT(cad::declaredLengthUnit(iges, true), LengthUnit::none);
}

void igesUnitFlagsMapOntoTheProtocolCodes() {
  struct Row { const char* flag; LengthUnit unit; };
  const Row rows[] = {{"1", LengthUnit::inch}, {"2", LengthUnit::millimetre}, {"4", LengthUnit::foot},
                      {"6", LengthUnit::metre}, {"9", LengthUnit::micrometre}, {"10", LengthUnit::centimetre},
                      // No protocol code: miles, kilometres, mils, microinches, an unassigned flag, no flag at all.
                      {"5", LengthUnit::none}, {"7", LengthUnit::none}, {"8", LengthUnit::none}, {"11", LengthUnit::none},
                      {"12", LengthUnit::none}, {"0", LengthUnit::none}, {"", LengthUnit::none}, {"x", LengthUnit::none},
                      {"99999", LengthUnit::none}};
  for (const Row& row : rows) {
    const LengthUnit actual = cad::igesLengthUnit(igesFile(standardGlobal(row.flag, "2HMM")));
    check(actual == row.unit, row.flag, __LINE__);
  }
}

void igesFlagThreeReadsTheUnitName() {
  struct Row { const char* field; LengthUnit unit; };
  const Row rows[] = {{"4HINCH", LengthUnit::inch}, {"2HIN", LengthUnit::inch}, {"4Hinch", LengthUnit::inch},
                      {"6HINCHES", LengthUnit::inch}, {"2HMM", LengthUnit::millimetre},
                      {"10HMILLIMETER", LengthUnit::millimetre}, {"2HCM", LengthUnit::centimetre},
                      {"5HMETER", LengthUnit::metre}, {"1HM", LengthUnit::metre}, {"4HFEET", LengthUnit::foot},
                      {"6HMICRON", LengthUnit::micrometre}, {"7HFURLONG", LengthUnit::none}, {"2HMI", LengthUnit::none},
                      {"", LengthUnit::none}, {"0H", LengthUnit::none}};
  for (const Row& row : rows) {
    const LengthUnit actual = cad::igesLengthUnit(igesFile(standardGlobal("3", row.field)));
    check(actual == row.unit, row.field, __LINE__);
  }
}

void igesHollerithStringsMayHoldDelimiters() {
  // Product and file names holding the parameter and record delimiters must not shift the parameter count.
  const std::string global = ",,7HA,B;C;D,9Hx,y;z;1;,5Hnativ,5Hpreps,32,308,15,308,15,,1.,1,4HINCH,1,0.01,;";
  CHECK_UNIT(cad::igesLengthUnit(igesFile(global)), LengthUnit::inch);
}

void igesGlobalSectionMayBeSplitAcrossRecordsAndCarriageReturns() {
  std::string file = igesFile(standardGlobal("1", "4HINCH") + std::string(200, ' '));
  std::string crlf;
  for (char c : file) {
    if (c == '\n') crlf += '\r';
    crlf += c;
  }
  CHECK_UNIT(cad::igesLengthUnit(file), LengthUnit::inch);
  CHECK_UNIT(cad::igesLengthUnit(crlf), LengthUnit::inch);
}

void igesExplicitDelimitersAreHonoured() {
  // `1H,` and `1H;` spelled out, then a custom parameter delimiter `/` with its record delimiter `!`.
  CHECK_UNIT(cad::igesLengthUnit(igesFile("1H,,1H;,4HTEST,8Hfile.igs,5Hnativ,5Hpreps,32,308,15,308,15,,1.,6,1HM,1,0.01,;")),
             LengthUnit::metre);
  CHECK_UNIT(cad::igesLengthUnit(igesFile("1H/,1H!/4HTEST/8Hfile.igs/5Hnativ/5Hpreps/32/308/15/308/15//1./10/2HCM/1/0.01/!")),
             LengthUnit::centimetre);
}

void igesGarbageAnswersNone() {
  CHECK_UNIT(cad::igesLengthUnit(""), LengthUnit::none);
  CHECK_UNIT(cad::igesLengthUnit("not an iges file\n"), LengthUnit::none);
  CHECK_UNIT(cad::igesLengthUnit(igesRecord("synthetic", 'S', 1)), LengthUnit::none);  // no Global section
  CHECK_UNIT(cad::igesLengthUnit(igesFile("")), LengthUnit::none);
  CHECK_UNIT(cad::igesLengthUnit(igesFile("x")), LengthUnit::none);
  CHECK_UNIT(cad::igesLengthUnit(igesFile("99999Hshort,")), LengthUnit::none);  // a Hollerith longer than the section
  CHECK_UNIT(cad::igesLengthUnit(igesFile(",,4HTEST,")), LengthUnit::none);     // ends before the unit flag
  // A Global section beyond the 8 KiB bound is not read, even when its unit flag comes first.
  CHECK_UNIT(cad::igesLengthUnit(igesFile(standardGlobal("1", "4HINCH") + std::string(9000, ' '))), LengthUnit::none);
  CHECK_UNIT(cad::igesLengthUnit(igesFile(standardGlobal("1", "4HINCH") + std::string(6000, ' '))), LengthUnit::inch);
  // The flag sits after the section's own end marker: not part of the section.
  CHECK_UNIT(cad::igesLengthUnit(igesFile(",,;,,,,,,,,,,,,,1,2HMM,")), LengthUnit::none);
}

void stepSiUnitsMapOntoTheProtocolCodes() {
  struct Row { const char* prefix; LengthUnit unit; };
  const Row rows[] = {{".MILLI.", LengthUnit::millimetre}, {".CENTI.", LengthUnit::centimetre}, {"$", LengthUnit::metre},
                      {".MICRO.", LengthUnit::micrometre}, {".KILO.", LengthUnit::none}, {".DECI.", LengthUnit::none},
                      {".NANO.", LengthUnit::none}};
  for (const Row& row : rows) {
    const LengthUnit actual = stepUnit(lengthUnit(1, row.prefix) + angleUnits + globalContext(10, "(#1,#2,#3)"));
    check(actual == row.unit, row.prefix, __LINE__);
  }
}

void stepConversionBasedUnitsAreReadByName() {
  struct Row { const char* name; LengthUnit unit; };
  const Row rows[] = {{"INCH", LengthUnit::inch}, {"inch", LengthUnit::inch}, {"FOOT", LengthUnit::foot},
                      {"MILLIMETRE", LengthUnit::millimetre}, {"METRE", LengthUnit::metre}, {"FURLONG", LengthUnit::none},
                      {"", LengthUnit::none}};
  for (const Row& row : rows) {
    // The conversion unit's own measure names millimetres: a second length unit entity that no context points at.
    const std::string body = conversionUnit(1, row.name) + lengthUnit(41, ".MILLI.") + angleUnits +
                             globalContext(10, "(#1,#2,#3)");
    check(stepUnit(body) == row.unit, row.name, __LINE__);
  }
}

void stepOrderAndLineBreaksDoNotMatter() {
  const std::string body = globalContext(10, "(#3,#2,#1)") + angleUnits + lengthUnit(1, ".CENTI.");
  CHECK_UNIT(stepUnit(body), LengthUnit::centimetre);
  const std::string spaced = "#10=(GEOMETRIC_REPRESENTATION_CONTEXT(3)GLOBAL_UNIT_ASSIGNED_CONTEXT((#1 , #2 ,\n#3))"
                             "REPRESENTATION_CONTEXT('a','b'));\n#1=(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT(.MILLI.,.METRE.));\n"
                             "#2=(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.));\n";
  CHECK_UNIT(stepUnit(spaced), LengthUnit::millimetre);
}

void stepSeveralContextsMustAgree() {
  const std::string agree = lengthUnit(1, ".MILLI.") + lengthUnit(4, ".MILLI.") + angleUnits +
                            globalContext(10, "(#1,#2,#3)") + globalContext(11, "(#4,#2,#3)");
  CHECK_UNIT(stepUnit(agree), LengthUnit::millimetre);
  const std::string differ = lengthUnit(1, ".MILLI.") + conversionUnit(4, "INCH") + angleUnits +
                             globalContext(10, "(#1,#2,#3)") + globalContext(11, "(#4,#2,#3)");
  CHECK_UNIT(stepUnit(differ), LengthUnit::none);
}

void stepAmbiguousOrMissingUnitsAnswerNone() {
  CHECK_UNIT(stepUnit(""), LengthUnit::none);
  CHECK_UNIT(stepUnit(angleUnits), LengthUnit::none);                                         // no context
  CHECK_UNIT(stepUnit(lengthUnit(1, ".MILLI.") + angleUnits), LengthUnit::none);              // a unit nobody points at
  CHECK_UNIT(stepUnit(angleUnits + globalContext(10, "(#2,#3)")), LengthUnit::none);          // no length unit in the context
  CHECK_UNIT(stepUnit(lengthUnit(1, ".MILLI.") + lengthUnit(4, ".CENTI.") + angleUnits +
                      globalContext(10, "(#1,#4,#2,#3)")), LengthUnit::none);                 // two length units
  CHECK_UNIT(stepUnit(angleUnits + globalContext(10, "(#77,#2,#3)")), LengthUnit::none);      // a dangling reference
  // A context that lists only its length unit still resolves: the angle units are not needed.
  CHECK_UNIT(stepUnit(lengthUnit(1, ".MILLI.") + globalContext(10, "(#1)")), LengthUnit::millimetre);
}

void stepCommentsAndStringsAreNotCode() {
  const std::string units = lengthUnit(1, ".MILLI.") + angleUnits;
  // Keywords inside a comment or a string are inert.
  CHECK_UNIT(stepUnit(units + "/* #10 = ( GLOBAL_UNIT_ASSIGNED_CONTEXT((#1,#2,#3)) ); */\n"), LengthUnit::none);
  CHECK_UNIT(stepUnit(units + "#10 = NOTE('GLOBAL_UNIT_ASSIGNED_CONTEXT((#1,#2,#3)) ;');\n"), LengthUnit::none);
  CHECK_UNIT(stepUnit(std::string("#1 = ( NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) ) /* LENGTH_UNIT() */;\n") + angleUnits +
                      globalContext(10, "(#1,#2,#3)")), LengthUnit::none);
  // A doubled apostrophe stays inside its string.
  CHECK_UNIT(stepUnit(units + "#9 = NOTE('it''s ; here');\n" + globalContext(10, "(#1,#2,#3)")), LengthUnit::millimetre);
  // An unterminated string or comment ends the scan quietly.
  CHECK_UNIT(stepUnit(units + "#9 = NOTE('never closed;\n" + globalContext(10, "(#1,#2,#3)")), LengthUnit::none);
  CHECK_UNIT(stepUnit(units + "/* never closed\n" + globalContext(10, "(#1,#2,#3)")), LengthUnit::none);
}

void stepLongEntitiesAreDroppedUnread() {
  // A unit entity longer than the buffered token limit is geometry as far as the scanner is concerned, even when its
  // unit declaration comes first.
  std::string padding;
  for (int i = 0; i < 300; ++i) padding += "1,";
  const std::string longUnit = "#1 = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) (" + padding + ") );\n";
  CHECK_UNIT(stepUnit(longUnit + angleUnits + globalContext(10, "(#1,#2,#3)")), LengthUnit::none);
  // The same entity without the padding is read.
  CHECK_UNIT(stepUnit(lengthUnit(1, ".MILLI.") + angleUnits + globalContext(10, "(#1,#2,#3)")), LengthUnit::millimetre);
}

void stepManyUnitEntitiesOverflowToNone() {
  std::string body;
  for (int id = 100; id < 100 + 300; ++id) body += lengthUnit(id, ".MILLI.");
  body += angleUnits + globalContext(10, "(#100,#2,#3)");
  CHECK_UNIT(stepUnit(body), LengthUnit::none);
}

void stepReferencesWithManyDigitsAreInert() {
  const std::string huge = "#99999999999999999999999 = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) );\n";
  CHECK_UNIT(stepUnit(huge + angleUnits + globalContext(10, "(#99999999999999999999999,#2,#3)")), LengthUnit::none);
}

void truncationsNeverCrashAndNeverInventAUnit() {
  const std::string step = readFixture("box.step"), iges = readFixture("box.iges");
  const std::string inch = stepFile(conversionUnit(1, "INCH") + lengthUnit(41, ".MILLI.") + angleUnits +
                                    globalContext(10, "(#1,#2,#3)"));
  for (std::size_t length = 0; length <= step.size(); length += 31) {
    const LengthUnit unit = cad::stepLengthUnit(std::string_view(step).substr(0, length));
    CHECK(unit == LengthUnit::none || unit == LengthUnit::millimetre);
  }
  for (std::size_t length = 0; length <= inch.size(); ++length) {
    const LengthUnit unit = cad::stepLengthUnit(std::string_view(inch).substr(0, length));
    CHECK(unit == LengthUnit::none || unit == LengthUnit::inch);
  }
  for (std::size_t length = 0; length <= iges.size(); length += 13) {
    const LengthUnit unit = cad::igesLengthUnit(std::string_view(iges).substr(0, length));
    CHECK(unit == LengthUnit::none || unit == LengthUnit::millimetre);
  }
  // Every byte value in a unit-looking place.
  for (int byte = 1; byte < 256; ++byte) {
    std::string mutated = step;
    mutated[mutated.find("SI_UNIT(.MILLI.")] = static_cast<char>(byte);
    const LengthUnit unit = cad::stepLengthUnit(mutated);
    CHECK(unit == LengthUnit::none || unit == LengthUnit::millimetre);
  }
}

void unitNamesAreTrimmedAndCaseInsensitive() {
  CHECK_UNIT(cad::lengthUnitNamed("  inch "), LengthUnit::inch);
  CHECK_UNIT(cad::lengthUnitNamed("Millimetre"), LengthUnit::millimetre);
  CHECK_UNIT(cad::lengthUnitNamed("ft"), LengthUnit::foot);
  CHECK_UNIT(cad::lengthUnitNamed("MIL"), LengthUnit::none);
  CHECK_UNIT(cad::lengthUnitNamed("KM"), LengthUnit::none);
  CHECK_UNIT(cad::lengthUnitNamed(""), LengthUnit::none);
}

struct Case {
  const char* name;
  void (*run)();
};
const Case cases[] = {
    {"exitCodesAreTheDocumentedProtocol", exitCodesAreTheDocumentedProtocol},
    {"unitWordTable", unitWordTable},
    {"headerCarriesTheMagicCountsAndUnitWord", headerCarriesTheMagicCountsAndUnitWord},
    {"boundsAndPayloadAreLittleEndianAndExact", boundsAndPayloadAreLittleEndianAndExact},
    {"anOversizeMeshIsAGeometryFailureAndWritesNothing", anOversizeMeshIsAGeometryFailureAndWritesNothing},
    {"preflightKeepsItsBudgetsAndTypes", preflightKeepsItsBudgetsAndTypes},
    {"theBoxFixturesPassPreflight", theBoxFixturesPassPreflight},
    {"theBoxFixturesDeclareMillimetres", theBoxFixturesDeclareMillimetres},
    {"igesUnitFlagsMapOntoTheProtocolCodes", igesUnitFlagsMapOntoTheProtocolCodes},
    {"igesFlagThreeReadsTheUnitName", igesFlagThreeReadsTheUnitName},
    {"igesHollerithStringsMayHoldDelimiters", igesHollerithStringsMayHoldDelimiters},
    {"igesGlobalSectionMayBeSplitAcrossRecordsAndCarriageReturns", igesGlobalSectionMayBeSplitAcrossRecordsAndCarriageReturns},
    {"igesExplicitDelimitersAreHonoured", igesExplicitDelimitersAreHonoured},
    {"igesGarbageAnswersNone", igesGarbageAnswersNone},
    {"stepSiUnitsMapOntoTheProtocolCodes", stepSiUnitsMapOntoTheProtocolCodes},
    {"stepConversionBasedUnitsAreReadByName", stepConversionBasedUnitsAreReadByName},
    {"stepOrderAndLineBreaksDoNotMatter", stepOrderAndLineBreaksDoNotMatter},
    {"stepSeveralContextsMustAgree", stepSeveralContextsMustAgree},
    {"stepAmbiguousOrMissingUnitsAnswerNone", stepAmbiguousOrMissingUnitsAnswerNone},
    {"stepCommentsAndStringsAreNotCode", stepCommentsAndStringsAreNotCode},
    {"stepLongEntitiesAreDroppedUnread", stepLongEntitiesAreDroppedUnread},
    {"stepManyUnitEntitiesOverflowToNone", stepManyUnitEntitiesOverflowToNone},
    {"stepReferencesWithManyDigitsAreInert", stepReferencesWithManyDigitsAreInert},
    {"truncationsNeverCrashAndNeverInventAUnit", truncationsNeverCrashAndNeverInventAUnit},
    {"unitNamesAreTrimmedAndCaseInsensitive", unitNamesAreTrimmedAndCaseInsensitive},
};
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "list") == 0) {
    for (const Case& test : cases) std::printf("%s\n", test.name);
    return 0;
  }
  if (argc == 4 && std::strcmp(argv[1], "run") == 0) {
    context.fixtures = argv[3];
    for (const Case& test : cases) {
      if (std::strcmp(test.name, argv[2]) != 0) continue;
      test.run();
      std::printf("%s: %d checks, %d failed\n", test.name, context.checks, context.failures);
      return context.failures == 0 && context.checks > 0 ? 0 : 1;
    }
    std::fprintf(stderr, "no case named %s\n", argv[2]);
    return 2;
  }
  std::fprintf(stderr, "usage: ProtocolTests list | run <case> <fixtures-directory>\n");
  return 2;
}
