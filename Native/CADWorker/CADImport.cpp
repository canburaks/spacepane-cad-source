#include "CADWorker.hpp"
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESData_IGESModel.hxx>
#include <IMeshData_Status.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Interface_CheckIterator.hxx>
#include <Interface_InterfaceModel.hxx>
#include <Interface_Static.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Controller.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <UnitsMethods.hxx>
#include <XSControl_TransferReader.hxx>
#include <XSControl_WorkSession.hxx>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace cad {
namespace {
struct RawVertex { gp_XYZ position; gp_XYZ normal; };

void checkPoint(const gp_XYZ& point) {
  for (int axis = 1; axis <= 3; ++axis)
    if (!std::isfinite(point.Coord(axis)) || std::abs(point.Coord(axis)) > 1e12)
      throw Failure{Exit::malformed};
}

template <typename Reader> TopoDS_Shape transfer(Reader& reader) {
  if (reader.Model().IsNull() || reader.Model()->NbEntities() <= 0)
    throw Failure{Exit::malformed};
  if (reader.Model()->NbEntities() > static_cast<int>(maximumEntities))
    throw Failure{Exit::budget};
  if (!reader.WS()->ModelCheckList().IsEmpty(Standard_True)) throw Failure{Exit::malformed};
  const int roots = reader.NbRootsForTransfer();
  if (roots <= 0) throw Failure{Exit::unsupported};
  if (roots > static_cast<int>(maximumEntities)) throw Failure{Exit::budget};
  if (reader.TransferRoots() != roots || reader.NbShapes() <= 0 ||
      !reader.WS()->TransferReader()->LastCheckList().IsEmpty(Standard_True))
    throw Failure{Exit::unsupported};
  for (int index = 1; index <= reader.NbShapes(); ++index) {
    TopExp_Explorer faces(reader.Shape(index), TopAbs_FACE);
    if (!faces.More()) throw Failure{Exit::unsupported};
  }
  const TopoDS_Shape shape = reader.OneShape();
  if (shape.IsNull()) throw Failure{Exit::unsupported};
  return shape;
}

TopoDS_Shape readShape(const std::string& bytes, bool isSTEP) {
  UnitsMethods::SetCasCadeLengthUnit(1.0);  // system unit is millimetres
  STEPControl_Controller::Init();
  IGESControl_Controller::Init();
  Interface_Static::SetCVal("xstep.cascade.unit", "MM");
  Interface_Static::SetIVal("read.step.product.mode", 1);
  Interface_Static::SetIVal("read.iges.onlyvisible", 0);
  if (isSTEP) {
    std::istringstream stream(bytes);
    STEPControl_Reader reader;
    if (reader.ReadStream("bounded-request", stream) != IFSelect_RetDone)
      throw Failure{Exit::malformed};
    reader.SetSystemLengthUnit(1.0);
    return transfer(reader);
  }
  IGESControl_Reader reader;
  reader.SetReadVisible(Standard_False);
  // OCCT 7.9.3's IGES work library does not implement ReadStream. Its pinned C
  // reader explicitly uses stdin for the public ReadFile("") call and skips
  // OSD_OpenFile. Replay the already bounded/preflighted bytes through an
  // anonymous pipe; no original path or durable byte snapshot is available.
  int descriptors[2];
  if (::pipe(descriptors) != 0) throw Failure{Exit::internal};
  if (::dup2(descriptors[0], STDIN_FILENO) < 0) {
    ::close(descriptors[0]); ::close(descriptors[1]);
    throw Failure{Exit::internal};
  }
  ::close(descriptors[0]);
  std::clearerr(stdin);
  std::thread writer([&bytes, destination = descriptors[1]] {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      const ssize_t count = ::write(destination, bytes.data() + offset, bytes.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) break;  // early parser close produces EPIPE; SIGPIPE is ignored
      offset += static_cast<std::size_t>(count);
    }
    ::close(destination);
  });
  IFSelect_ReturnStatus status = IFSelect_RetFail;
  try {
    status = reader.ReadFile("");
  } catch (...) {
    ::close(STDIN_FILENO); writer.join(); throw;
  }
  // Reader normally fclose's stdin; closing the owned read endpoint also makes
  // early failure unblock the producer before join. This process has one request.
  ::close(STDIN_FILENO);
  writer.join();
  if (status != IFSelect_RetDone) throw Failure{Exit::malformed};
  return transfer(reader);
}
}

Mesh importMesh(const std::string& bytes, bool isSTEP) {
  const TopoDS_Shape shape = readShape(bytes, isSTEP);
  std::size_t faceCount = 0;
  for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next())
    if (++faceCount > maximumFaces) throw Failure{Exit::geometry};
  if (!faceCount) throw Failure{Exit::unsupported};

  Bnd_Box shapeBounds;
  BRepBndLib::Add(shape, shapeBounds, Standard_False);
  if (shapeBounds.IsVoid() || shapeBounds.IsOpen()) throw Failure{Exit::malformed};
  std::array<double, 6> initialBounds;
  shapeBounds.Get(initialBounds[0], initialBounds[1], initialBounds[2],
                  initialBounds[3], initialBounds[4], initialBounds[5]);
  double maximumExtent = 0;
  for (int axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(initialBounds[axis]) || !std::isfinite(initialBounds[axis + 3]))
      throw Failure{Exit::malformed};
    maximumExtent = std::max(maximumExtent, initialBounds[axis + 3] - initialBounds[axis]);
  }
  if (!std::isfinite(maximumExtent) || maximumExtent < 1e-6 || maximumExtent > 1e9)
    throw Failure{Exit::unsupported};

  IMeshTools_Parameters parameters;
  parameters.Deflection = std::max(maximumExtent / 300.0, 1e-6);
  parameters.DeflectionInterior = parameters.Deflection;
  parameters.Angle = 0.35;
  parameters.AngleInterior = 0.35;
  parameters.MinSize = std::max(maximumExtent / 10000.0, 1e-7);
  parameters.InParallel = Standard_False;
  parameters.Relative = Standard_False;
  parameters.CleanModel = Standard_True;
  BRepMesh_IncrementalMesh mesher(shape, parameters);
  constexpr int invalidMesh = IMeshData_OpenWire | IMeshData_SelfIntersectingWire |
    IMeshData_Failure | IMeshData_UnorientedWire | IMeshData_TooFewPoints | IMeshData_UserBreak;
  if (!mesher.IsDone() || (mesher.GetStatusFlags() & invalidMesh))
    throw Failure{Exit::unsupported};

  Mesh mesh{};
  mesh.faces = static_cast<std::uint32_t>(faceCount);
  for (int axis = 0; axis < 3; ++axis) {
    mesh.bounds[axis] = std::numeric_limits<double>::infinity();
    mesh.bounds[axis + 3] = -std::numeric_limits<double>::infinity();
  }
  std::vector<RawVertex> vertices;
  for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
    const TopoDS_Face face = TopoDS::Face(explorer.Current());
    TopLoc_Location location;
    const Handle(Poly_Triangulation) triangles = BRep_Tool::Triangulation(face, location);
    if (triangles.IsNull() || triangles->NbTriangles() <= 0 || triangles->NbNodes() <= 0)
      throw Failure{Exit::unsupported};
    const std::size_t nodeCount = static_cast<std::size_t>(triangles->NbNodes());
    const std::size_t indexCount = static_cast<std::size_t>(triangles->NbTriangles()) * 3;
    if (nodeCount > maximumVertices - vertices.size() ||
        indexCount > maximumTriangles * 3 - mesh.indices.size() ||
        72 + (vertices.size() + nodeCount) * 24 + (mesh.indices.size() + indexCount) * 4 > maximumOutputBytes)
      throw Failure{Exit::geometry};
    vertices.reserve(vertices.size() + nodeCount);
    mesh.indices.reserve(mesh.indices.size() + indexCount);
    std::vector<std::int32_t> mapped(nodeCount, -1);
    const gp_Trsf transform = location.Transformation();
    const std::size_t faceIndicesBegin = mesh.indices.size();
    for (int triangle = 1; triangle <= triangles->NbTriangles(); ++triangle) {
      int local[3];
      triangles->Triangle(triangle).Get(local[0], local[1], local[2]);
      if (face.Orientation() == TopAbs_REVERSED) std::swap(local[1], local[2]);
      gp_XYZ points[3];
      for (int corner = 0; corner < 3; ++corner) {
        if (local[corner] < 1 || local[corner] > triangles->NbNodes()) throw Failure{Exit::malformed};
        points[corner] = triangles->Node(local[corner]).Transformed(transform).XYZ();
        checkPoint(points[corner]);
      }
      const gp_XYZ normal = (points[1] - points[0]).Crossed(points[2] - points[0]);
      if (!std::isfinite(normal.SquareModulus())) throw Failure{Exit::malformed};
      // Collapsed surface poles and trim seams can generate zero-area mesh
      // artifacts. They cover no surface; discard them, but never discard a face.
      if (normal.SquareModulus() <= 1e-30) continue;
      for (int corner = 0; corner < 3; ++corner) {
        auto& index = mapped[static_cast<std::size_t>(local[corner] - 1)];
        if (index < 0) {
          index = static_cast<std::int32_t>(vertices.size());
          vertices.push_back({points[corner], gp_XYZ(0, 0, 0)});
          for (int axis = 0; axis < 3; ++axis) {
            mesh.bounds[axis] = std::min(mesh.bounds[axis], points[corner].Coord(axis + 1));
            mesh.bounds[axis + 3] = std::max(mesh.bounds[axis + 3], points[corner].Coord(axis + 1));
          }
        }
        vertices[static_cast<std::size_t>(index)].normal += normal;
        mesh.indices.push_back(static_cast<std::uint32_t>(index));
      }
    }
    if (mesh.indices.size() == faceIndicesBegin) throw Failure{Exit::unsupported};
  }
  if (vertices.empty() || mesh.indices.empty()) throw Failure{Exit::unsupported};
  maximumExtent = 0;
  gp_XYZ center;
  for (int axis = 0; axis < 3; ++axis) {
    maximumExtent = std::max(maximumExtent, mesh.bounds[axis + 3] - mesh.bounds[axis]);
    center.SetCoord(axis + 1, mesh.bounds[axis] + (mesh.bounds[axis + 3] - mesh.bounds[axis]) * 0.5);
  }
  if (!std::isfinite(maximumExtent) || maximumExtent < 1e-6 || maximumExtent > 1e9)
    throw Failure{Exit::unsupported};
  const double scale = 2.0 / maximumExtent;
  mesh.vertices.reserve(vertices.size());
  for (const auto& source : vertices) {
    const gp_XYZ position = (source.position - center) * scale;
    const double magnitude = source.normal.Modulus();
    if (!std::isfinite(magnitude) || magnitude <= 1e-30) throw Failure{Exit::unsupported};
    const gp_XYZ normal = source.normal / magnitude;
    Vertex output{};
    for (int axis = 0; axis < 3; ++axis) {
      output.position[axis] = static_cast<float>(position.Coord(axis + 1));
      output.normal[axis] = static_cast<float>(normal.Coord(axis + 1));
    }
    mesh.vertices.push_back(output);
  }
  return mesh;
}
}
