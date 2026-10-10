#include "CADWorker.hpp"
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace cad {
namespace {
template <typename T> void append(std::vector<unsigned char>& output, T value) {
  static_assert(sizeof(T) == 4 || sizeof(T) == 8);
  unsigned char representation[sizeof(T)];
  std::memcpy(representation, &value, sizeof(T));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  for (std::size_t i = sizeof(T); i > 0; --i) output.push_back(representation[i - 1]);
#else
  output.insert(output.end(), representation, representation + sizeof(T));
#endif
}
}
void writeMesh(int destination, const Mesh& mesh) {
  const std::size_t size = 72 + mesh.vertices.size() * 24 + mesh.indices.size() * 4;
  if (size > maximumOutputBytes) throw Failure{Exit::geometry};
  std::vector<unsigned char> output;
  output.reserve(size);
  constexpr char magic[] = "PRVCAD01";
  output.insert(output.end(), magic, magic + 8);
  append(output, static_cast<std::uint32_t>(mesh.vertices.size()));
  append(output, static_cast<std::uint32_t>(mesh.indices.size()));
  append(output, mesh.faces);
  append(output, unitWord(mesh.declaredUnit));  // 1 = millimetres, plus the unit the file declared
  for (const double coordinate : mesh.bounds) append(output, coordinate);
  for (const auto& vertex : mesh.vertices) {
    for (const float coordinate : vertex.position) append(output, coordinate);
    for (const float normal : vertex.normal) append(output, normal);
  }
  for (const auto index : mesh.indices) append(output, index);
  std::size_t written = 0;
  while (written < output.size()) {
    const ssize_t count = ::write(destination, output.data() + written, output.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) throw Failure{Exit::internal};
    written += static_cast<std::size_t>(count);
  }
}
}
