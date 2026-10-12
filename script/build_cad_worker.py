#!/usr/bin/env python3
"""Build the pinned, project-local CAD helper; never installs system packages."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

REVISION = "a016080bf6738d6aeae020badee4e888ad1540a5"  # official V7_9_3
ARCHIVE_SHA256 = "c533f2667b59921bd6bd40ce82e7b9900b0289ccc731af5fdeeba097de80ef0f"
ARCHIVE_URL = "https://codeload.github.com/Open-Cascade-SAS/OCCT/tar.gz/" + REVISION
ROOT = Path(__file__).resolve().parents[1]
# Written into an extracted tree last. A tree without it, or with another value, is never reused.
EXTRACTION_MARKER = ".prev-occt-extracted"
# License-notice folders the app carries: (folder under ThirdPartyNotices, folder name inside Contents/Resources).
# script/embed_cad_worker.py copies these pairs into each Xcode target that runs it (it cannot import this module
# under the script sandbox, so it spells them out) and script/stage_companion.py copies them into the prototype;
# the About page opens the folders by these names (Apps/PrevApp/Models/BundledNotices.swift). test_build_cad_worker.py
# runs the embed script and fails when any of these places disagree.
NOTICE_FOLDERS = (("OCCT", "OCCT-Notices"), ("ColorSchemes", "ColorSchemes-Notices"))


def run(arguments, log):
    with log.open("a") as stream:
        result = subprocess.run(arguments, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print("\n".join(log.read_text(errors="replace").splitlines()[-35:]))
        raise SystemExit(result.returncode)


def narrow_manifests(source):
    # Build-manifest changes only: no upstream parser implementation is changed.
    # OCCT 7.9.3's shape processor and reader parameters include this header-only
    # value type from DE. Export it through the existing TKXSBase toolkit without
    # enabling TKDE's providers, registry or configuration implementations.
    packages = source / "src" / "TKXSBase" / "PACKAGES"
    selected = packages.read_text().splitlines()
    if "DE" not in selected:
        packages.write_text("\n".join(selected + ["DE"]) + "\n")
    (source / "src" / "DE" / "FILES").write_text("DE_ShapeFixParameters.hxx\n")
    for toolkit, omitted, parameters in (
        ("TKDESTEP", "STEPCAFControl", "DESTEP"),
        ("TKDEIGES", "IGESCAFControl", "DEIGES"),
    ):
        packages = source / "src" / toolkit / "PACKAGES"
        packages.write_text("\n".join(p for p in packages.read_text().splitlines()
                                      if p != omitted) + "\n")
        (source / "src" / parameters / "FILES").write_text(
            parameters + "_Parameters.hxx\n" + parameters + "_Parameters.cxx\n")
        deps = source / "src" / toolkit / "EXTERNLIB"
        deps.write_text("\n".join(d for d in deps.read_text().splitlines()
                                  if d not in {"TKCAF", "TKCDF", "TKLCAF", "TKXCAF", "TKDE"}) + "\n")
    (source / "PREV_BUILD_MANIFEST_CHANGES.txt").write_text(
        "2026-10-04: Prev build omits STEPCAFControl/IGESCAFControl, DE provider and "
        "configuration code, and their unused CAF/XCAF/TKDE dependencies. Only "
        "DESTEP_Parameters/DEIGES_Parameters remain. "
        "2026-10-07: TKXSBase additionally exports DE/DE_ShapeFixParameters.hxx; "
        "DE/FILES contains only this header-only value type. No TKDE library is added. "
        "No source implementation changes. "
        "The unmodified source is the SHA256-pinned archive beside this directory.\n")


def extraction_marker():
    return "OCCT " + REVISION + " sha256 " + ARCHIVE_SHA256 + "\n"


def extracted_tree_is_current(source):
    """True only for a real directory that this script finished extracting from the verified archive."""
    marker = source / EXTRACTION_MARKER
    try:
        return (source.is_dir() and not source.is_symlink()
                and marker.is_file() and not marker.is_symlink()
                and marker.read_text() == extraction_marker())
    except OSError:
        return False


def check_archive_members(members, destination):
    """Accept regular files and directories that stay inside `destination`; nothing else."""
    root = destination.resolve()
    for member in members:
        target = (destination / member.name).resolve()
        if (member.name.startswith("/") or not target.is_relative_to(root)
                or not (member.isreg() or member.isdir())):
            raise SystemExit("Unsafe OCCT archive member.")


def remove_path(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        shutil.rmtree(path)


def extract_pinned_source(archive, workspace, source):
    """Unpack the checksum-verified archive into a fresh directory and move it to `source` only when complete.

    A partial extraction, an older tree without the marker and anything else already at `source` are
    replaced, never reused. The members are checked first, and the `data` extraction filter (Python 3.12 and
    the security releases of older versions) additionally refuses links outside the tree and device files
    and drops special mode bits; an interpreter without it gets the same mode and ownership reset here."""
    for leftover in workspace.glob("occt-extract-*"):
        remove_path(leftover)
    scratch = Path(tempfile.mkdtemp(prefix="occt-extract-", dir=workspace))
    try:
        with tarfile.open(archive) as bundle:
            members = bundle.getmembers()
            check_archive_members(members, scratch)
            if hasattr(tarfile, "data_filter"):
                bundle.extractall(scratch, members=members, filter="data")
            else:
                for member in members:
                    member.mode &= 0o755
                    member.uid = member.gid = 0
                    member.uname = member.gname = ""
                bundle.extractall(scratch, members=members)
        extracted = scratch / source.name
        if not extracted.is_dir():
            raise SystemExit("OCCT archive does not contain the expected source directory.")
        (extracted / EXTRACTION_MARKER).write_text(extraction_marker())
        remove_path(source)
        os.replace(extracted, source)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def refresh_bundle_filelists(workspace):
    # Individual file permissions are required by Xcode's user-script sandbox.
    # The same lists expand in each owning target: app or CADImportService.xpc.
    prepared = [workspace / ("worker-" + architecture) / "CADWorker"
                for architecture in ("arm64", "x86_64")]
    # The published CAD source bundle holds only the OCCT notices, and a recipient's relink ends here, so a missing
    # folder lists nothing instead of failing; the embed script still refuses to copy a folder that is not there.
    notices = {name: sorted(path for path in (ROOT / "ThirdPartyNotices" / source).iterdir() if path.is_file())
               if (ROOT / "ThirdPartyNotices" / source).is_dir() else []
               for source, name in NOTICE_FOLDERS}
    notice_files = [path for files in notices.values() for path in files]
    (workspace / "bundle-inputs.xcfilelist").write_text(
        "\n".join(str(path) for path in prepared + notice_files if path.is_file()) + "\n")
    helper = "$(TARGET_BUILD_DIR)/$(CONTENTS_FOLDER_PATH)/Helpers"
    resources = "$(TARGET_BUILD_DIR)/$(UNLOCALIZED_RESOURCES_FOLDER_PATH)"
    outputs = [helper, helper + "/CADWorker"]
    for name, files in notices.items():
        if files:
            outputs += [resources + "/" + name] + [resources + "/" + name + "/" + path.name for path in files]
    (workspace / "bundle-outputs.xcfilelist").write_text("\n".join(outputs) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--architecture", choices=("arm64", "x86_64"),
                        default="arm64" if platform.machine() == "arm64" else "x86_64")
    parser.add_argument("--jobs", type=int, default=min(10, os.cpu_count() or 2))
    parser.add_argument("--test", action="store_true", help="Run synthetic worker integration checks")
    parser.add_argument("--refresh-bundle-filelists", action="store_true",
                        help="Refresh Xcode input/output lists without downloading or rebuilding OCCT")
    args = parser.parse_args()
    workspace = ROOT / ".build" / "cad"
    workspace.mkdir(parents=True, exist_ok=True)
    if args.refresh_bundle_filelists:
        refresh_bundle_filelists(workspace)
        return
    cmake = shutil.which("cmake") or "/opt/homebrew/bin/cmake"
    if not Path(cmake).is_file():
        raise SystemExit("CMake is required; no system package is installed by this script.")
    archive = workspace / ("occt-" + REVISION + ".tar.gz")
    if not archive.exists():
        temporary = archive.with_suffix(".download")
        urllib.request.urlretrieve(ARCHIVE_URL, temporary)
        temporary.rename(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise SystemExit("OCCT archive checksum mismatch; refusing to build.")
    source = workspace / ("OCCT-" + REVISION)
    if not extracted_tree_is_current(source):
        extract_pinned_source(archive, workspace, source)
    narrow_manifests(source)
    kernel = workspace / ("kernel-" + args.architecture)
    log = workspace / ("build-" + args.architecture + ".log")
    configure = [cmake, "-S", str(source), "-B", str(kernel), "-G", "Unix Makefiles",
                 "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0",
                 "-DCMAKE_OSX_ARCHITECTURES=" + args.architecture,
                 "-DCMAKE_CXX_FLAGS=-fapplication-extension", "-DCMAKE_C_FLAGS=-fapplication-extension",
                 "-DBUILD_LIBRARY_TYPE=Static", "-DBUILD_RELEASE_DISABLE_EXCEPTIONS=OFF",
                 "-DBUILD_ADDITIONAL_TOOLKITS=TKDESTEP TKDEIGES TKMesh", "-DUSE_MMGR_TYPE=NATIVE"]
    for module in ("FoundationClasses", "ModelingData", "ModelingAlgorithms", "Visualization",
                   "ApplicationFramework", "DataExchange", "DETools", "Draw"):
        configure.append("-DBUILD_MODULE_" + module + "=OFF")
    for optional in ("TK", "FREETYPE", "FREEIMAGE", "FFMPEG", "OPENVR", "RAPIDJSON", "DRACO",
                     "TBB", "EIGEN", "XLIB", "OPENGL", "GLES2"):
        configure.append("-DUSE_" + optional + "=OFF")
    print("Building pinned OCCT shape readers and mesh kernel (" + args.architecture + ")", flush=True)
    run(configure, log)
    run([cmake, "--build", str(kernel), "--parallel", str(max(1, args.jobs))], log)
    worker = workspace / ("worker-" + args.architecture)
    run([cmake, "-S", str(ROOT / "Native" / "CADWorker"), "-B", str(worker),
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0",
         "-DCMAKE_OSX_ARCHITECTURES=" + args.architecture, "-DOCCT_BUILD=" + str(kernel)], log)
    run([cmake, "--build", str(worker), "--parallel", str(max(1, args.jobs))], log)
    # Universal builds prepare both architectures before embedding.
    refresh_bundle_filelists(workspace)
    print(worker / "CADWorker")
    if args.test:
        subprocess.run([os.sys.executable, str(ROOT / "Native/CADWorker/Tests/test_worker.py"),
                        "--worker", str(worker / "CADWorker"), "--generator",
                        str(worker / "CADFixtureGenerator")], check=True)


if __name__ == "__main__":
    main()
