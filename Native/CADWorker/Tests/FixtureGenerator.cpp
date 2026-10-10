// These analytic constructions are independently specified synthetic fixtures.
// The generator is a development/test target and is never embedded in the app.
#include <APIHeaderSection_MakeHeader.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Writer.hxx>
#include <IGESData_IGESModel.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Writer.hxx>
#include <StepData_StepModel.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TopoDS_Face.hxx>
#include <UnitsMethods.hxx>
#include <filesystem>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

void save(const std::filesystem::path& root, const char* name, const TopoDS_Shape& shape) {
  STEPControl_Writer step;
  if (step.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) throw 1;
  APIHeaderSection_MakeHeader header(step.Model());
  header.SetName(new TCollection_HAsciiString("synthetic"));
  header.SetTimeStamp(new TCollection_HAsciiString("2000-01-01T00:00:00"));
  header.SetAuthorValue(1, new TCollection_HAsciiString("synthetic"));
  header.SetOrganizationValue(1, new TCollection_HAsciiString("synthetic"));
  header.Apply(step.Model());
  if (step.Write((root / (std::string(name) + ".step")).c_str()) != IFSelect_RetDone) throw 1;
  IGESControl_Writer iges(std::string(name) == "inch" ? "INCH" : "MM", 0);
  if (!iges.AddShape(shape)) throw 1;
  auto& global = iges.Model()->ChangeGlobalSection();
  global.SetAuthorName(new TCollection_HAsciiString("synthetic"));
  global.SetCompanyName(new TCollection_HAsciiString("synthetic"));
  global.SetFileName(new TCollection_HAsciiString("synthetic.iges"));
  global.SetDate(new TCollection_HAsciiString("20000101.000000"));
  global.SetLastChangeDate(new TCollection_HAsciiString("20000101.000000"));
  if (!iges.Write((root / (std::string(name) + ".iges")).c_str())) throw 1;
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    IGESControl_Controller::Init();
    UnitsMethods::SetCasCadeLengthUnit(1.0);
    Interface_Static::SetCVal("xstep.cascade.unit", "MM");
    Interface_Static::SetCVal("write.step.unit", "MM");
    save(root, "box", BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    gp_Trsf translation;
    translation.SetTranslation(gp_Vec(100, 200, 300));
    save(root, "translated", BRepPrimAPI_MakeBox(10, 20, 30).Shape().Moved(TopLoc_Location(translation)));
    save(root, "cylinder", BRepPrimAPI_MakeCylinder(10, 30).Shape());
    save(root, "cone", BRepPrimAPI_MakeCone(10, 0, 30).Shape());
    save(root, "sphere", BRepPrimAPI_MakeSphere(10).Shape());
    BRepBuilderAPI_MakePolygon exterior;
    for (const auto& point : {gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0), gp_Pnt(10, 10, 0), gp_Pnt(0, 10, 0)})
      exterior.Add(point);
    exterior.Close();
    BRepBuilderAPI_MakePolygon hole;
    for (const auto& point : {gp_Pnt(3, 3, 0), gp_Pnt(3, 7, 0), gp_Pnt(7, 7, 0), gp_Pnt(7, 3, 0)})
      hole.Add(point);
    hole.Close();
    BRepBuilderAPI_MakeFace face(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), exterior.Wire());
    face.Add(hole.Wire());
    save(root, "trimmed", face.Face());
    Interface_Static::SetCVal("write.step.unit", "INCH");
    save(root, "inch", BRepPrimAPI_MakeBox(25.4, 50.8, 76.2).Shape());
    return 0;
  } catch (...) { return 5; }
}
