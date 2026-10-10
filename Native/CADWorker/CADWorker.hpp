#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cad {
// 4 is a resource allowance (input size, entity or token counts, memory, CPU). 6 is the finished model's own ceiling:
// more faces, vertices, triangles or serialized bytes than the protocol carries, which the app words as "too complex".
enum class Exit : int { success = 0, malformed = 2, unsupported = 3, budget = 4, internal = 5, geometry = 6 };
struct Failure { Exit status; };
constexpr std::size_t maximumInputBytes = 16 * 1024 * 1024;
constexpr std::size_t maximumOutputBytes = 32 * 1024 * 1024;
constexpr std::size_t maximumEntities = 100000;
constexpr std::size_t maximumFaces = 100000;
constexpr std::size_t maximumTriangles = 500000;
constexpr std::size_t maximumVertices = 1500000;
struct Vertex { std::array<float, 3> position; std::array<float, 3> normal; };
// The length unit the source file declared, as byte 1 of the protocol's unit word carries it. The positions are always
// millimetres; this only says what the file called its own unit. `none` is also written when the file declares more
// than one unit, a unit the protocol has no code for, or nothing the scanner could read.
enum class LengthUnit : std::uint8_t { none = 0, micrometre = 1, millimetre = 2, centimetre = 3, metre = 4, inch = 5, foot = 6 };
struct Mesh {
  std::array<double, 6> bounds;
  std::uint32_t faces;
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  LengthUnit declaredUnit = LengthUnit::none;
};
// The fourth header word: byte 0 is 1 (positions are normalized millimetres, the only value), byte 1 the declared unit,
// the upper half zero and reserved. A reader of the old protocol saw the constant 1, which is the word for `none`.
constexpr std::uint32_t unitWord(LengthUnit declared) {
  return 1u | (static_cast<std::uint32_t>(declared) << 8);
}
void preflight(const std::string& bytes, bool isSTEP);
Mesh importMesh(const std::string& bytes, bool isSTEP);
// CADUnit.cpp. These read the unit a file declares from the bytes the worker already preflighted: never throw, never
// allocate more than a few kilobytes beyond a bounded token buffer and answer `none` for anything they cannot read.
LengthUnit declaredLengthUnit(const std::string& bytes, bool isSTEP);
LengthUnit igesLengthUnit(std::string_view bytes);
LengthUnit stepLengthUnit(std::string_view bytes);
LengthUnit lengthUnitNamed(std::string_view name);
void writeMesh(int destination, const Mesh& mesh);
}
