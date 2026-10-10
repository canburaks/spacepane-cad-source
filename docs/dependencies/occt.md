# Open CASCADE CAD worker dependency audit

Recorded 2026-10-04. This is a development adoption and reproducibility record,
not a declaration of release/App Store approval. CAD is disabled by default.

## Identity and reproduction

- Official OCCT 7.9.3 tag `V7_9_3`, peeled commit
  `a016080bf6738d6aeae020badee4e888ad1540a5` in the
  [official repository](https://github.com/Open-Cascade-SAS/OCCT/tree/a016080bf6738d6aeae020badee4e888ad1540a5).
- Immutable-commit [source archive](https://codeload.github.com/Open-Cascade-SAS/OCCT/tar.gz/a016080bf6738d6aeae020badee4e888ad1540a5),
  SHA256 `c533f2667b59921bd6bd40ce82e7b9900b0289ccc731af5fdeeba097de80ef0f`.
- `python3 script/build_cad_worker.py --architecture arm64 --test` checks the
  archive before using it, builds under ignored `.build/cad`, and creates
  `.build/cad/worker-arm64/CADWorker`. No global install/package-manager mutation.
- CMake, Python 3.9+, Apple's compiler/SDK are build tools. No compiler, CMake,
  flex/Bison executable or fixture generator enters product bundles.
- The script can separately prepare `x86_64`; both architectures must be prepared
  for a universal helper. Only arm64 was built/executed in this audit. Intel and
  actual macOS 15 execution remain unverified.

The selected kernel and helper use Release optimization, C++17,
`-fapplication-extension`, retained native exceptions, and a macOS 15 deployment
target. `vtool -show-build` confirmed `minos 15.0`; the selected development SDK
was 27.0. `otool -L` showed only `/usr/lib/libc++.1.dylib` and
`/usr/lib/libSystem.B.dylib`, with no third-party runtime dynamic library or
private framework. The arm64 helper is approximately 20 MiB before distribution
packaging. A deployment flag and library inspection do not replace macOS 15,
signed sandbox inheritance or Finder-host verification.

## Minimal static build

Official OCCT toolkit boundaries combine basic shape readers with XDE/document
readers, whose transitive dependency closure pulls in CAF/XCAF and visualization.
The builder changes only the following downloaded build manifests:

1. Remove `STEPCAFControl` from `TKDESTEP/PACKAGES` and `IGESCAFControl` from
   `TKDEIGES/PACKAGES`.
2. Restrict `DESTEP/FILES` and `DEIGES/FILES` to their `*_Parameters.hxx/.cxx`
   value-type implementations; omit provider/configuration wrappers.
3. Remove the now-unused `TKCAF`, `TKCDF`, `TKLCAF`, `TKXCAF` and `TKDE` entries
   from those two toolkits' `EXTERNLIB` manifests.
4. Add the `DE` package to `TKXSBase/PACKAGES` solely to expose the header-only
   `DE_ShapeFixParameters` value type required by the pinned shape processor and
   STEP/IGES parameter headers.
5. Restrict `DE/FILES` to `DE_ShapeFixParameters.hxx`. No DE provider/configuration
   implementation or `TKDE` library is compiled. The retained header bytes match
   the pinned archive exactly.

The source archive is preserved unchanged. The expanded source includes a dated
`PREV_BUILD_MANIFEST_CHANGES.txt`; the exact transformations are in the repository
builder. No upstream parser implementation is edited. A fresh reproduction can
remove only project-owned `.build/cad` and rerun the builder.

The 2026-10-07 clean build exposed the missing header after removing `TKDE`; the
header-only manifest fix passed six portable closure/idempotence/source-integrity
checks, 52 source-offer tests, and the complete arm64 native build followed by 13
synthetic worker integration tests. Its dynamic link set remains system libc++
and libSystem. This reproduces development import capability, not distribution
permission or signed Finder/TestFlight acceptance.

The actual static link set is `TKDESTEP`, `TKDEIGES`, `TKMesh`, `TKXSBase`,
`TKShHealing`, `TKBool`, `TKBO`, `TKPrim`, `TKTopAlgo`, `TKGeomAlgo`, `TKBRep`,
`TKGeomBase`, `TKG3d`, `TKG2d`, `TKMath`, `TKernel`. All OCCT modules are disabled
and the builder requests only the STEP, IGES and meshing toolkit closure.
Tcl/Tk, FreeType, FreeImage, FFmpeg, OpenVR, RapidJSON, Draco, TBB, Eigen, Xlib,
OpenGL/GLES, Draw and development toolkit executables are disabled. Static OCCT
also compiles with its upstream `OCCT_NO_PLUGINS` setting. The worker does not use
the provider registry, XDE, UI, OCAF, scripting or document metadata APIs.

## Licensing and retained notices

The [pinned OCCT license and exception](https://github.com/Open-Cascade-SAS/OCCT/blob/a016080bf6738d6aeae020badee4e888ad1540a5/OCCT_LGPL_EXCEPTION.txt)
are LGPL 2.1 plus the Open CASCADE exception version 1.0. The exception covers
material incorporated from headers. It does not remove library-source and
applicable relinking obligations for this statically linked development helper.
Unmodified copies and prominent attribution are retained in
`ThirdPartyNotices/OCCT`. Parent packaging copies those notices beside the helper.

The selected toolkit source also contains generated parser dependencies:

| Component | Source/evidence | Retained terms |
| --- | --- | --- |
| OCCT kernel/import/mesh and contributors | Pinned source headers, root LGPL text | LGPL 2.1 and OCCT header exception |
| GNU Bison 3.7.4 generated STEP parser skeleton | [`step.tab.cxx`](https://github.com/Open-Cascade-SAS/OCCT/blob/a016080bf6738d6aeae020badee4e888ad1540a5/src/StepFile/step.tab.cxx) and `.hxx` preambles | GPL-3.0-or-later with the skeleton's parser-output exception; `BISON_NOTICE.txt` and full `LICENSE_GPL_3.txt` retained |
| Flex lexer class header | [`FlexLexer.h`](https://github.com/Open-Cascade-SAS/OCCT/blob/a016080bf6738d6aeae020badee4e888ad1540a5/src/FlexLexer/FlexLexer.h) | University of California copyright, redistribution/no-endorsement/disclaimer clauses in `FLEX_NOTICE.txt` |
| Apple C++ and system runtime | `otool -L` system paths | Supplied by macOS; no copies redistributed here |

No optional third-party OCCT library is enabled. This audit inspects the selected
closure and its known embedded generator notices; future toolkit/version changes
require repeating transitive source/license and runtime dependency checks.

Release remains blocked on a reviewed distribution route that supplies complete
corresponding OCCT source, build-manifest modifications, build scripts and the
required native relinking materials, or another independently approved license
route. Native `.o` files and static archives remain locally in `.build/cad` for
reproduction; their local existence alone does not satisfy distribution duties.
The source archive must remain obtainable independently of a transient cache.
App Store, hardened signing, sandbox behavior and license compatibility must be
rechecked for the chosen release channel. The repository maintainers own pinned
dependency updates, vulnerability review, fixture regression and notice changes.

### Source-offer route and public mirror (decision 027 D20 (a))

The route that needs no fee is LGPL 2.1 section 6: the helper is the only work that
links the library, so its source, the pinned library reference, the manifest edits and
the build scripts are published with a written offer (`Native/CADWorker/SOURCE-OFFER.md`).
The Swift app, the extensions and the XPC service never link OCCT and stay in the private
repository.

- `Native/CADWorker/LICENSE` grants the MIT License for the helper's sources, the build and
  embedding scripts and the entitlements file, so a recipient may modify them for their own
  use and reverse engineer them to debug such modifications.
- `script/package_cad_source_offer.py --output <empty directory>` assembles the bundle from
  the private repository. The public repository `canburaks/spacepane-cad-source` is that
  bundle committed as-is and tagged per release. Its release tarball is built with
  `--tarball` and `--include-archive` so it also carries the pinned archive, which is not
  committed. `--check <directory>` verifies a mirror in place.
- The app's About text names the license and says notices are included. It does not yet
  give the offer's location, and the website and store description do not mention it.
- Open: a legal review of the questions in the offer, publication of the repository and its
  first tagged release, and refreshing the mirror whenever the helper, the scripts, the
  notices or the pin change. The Mac App Store route and the Open CASCADE commercial
  exceptions are separate questions; this route does not depend on a purchased license.

## Import, file access and privacy boundary

The parent supplies a bounded authorized byte snapshot on stdin and a closed
`iges`/`step` argument. The worker never receives the original URL/path.

STEP calls basic `STEPControl_Reader::ReadStream` with a synthetic request name.
The pinned `STEPControl_ActorRead` shape-transfer path has no external-file reader;
the richer CAF external-file resolution implementation is omitted. STEP entity
identifiers containing `EXTERNAL`, `DOCUMENT_FILE` or `DOCUMENT_REFERENCE` are
conservatively rejected outside quoted data/comments before constructing readers.
This intentionally excludes some inert metadata subsets as well as external
assemblies. Headers, names and arbitrary user strings never enter neutral output.

IGES `ReadStream` is not implemented by the pinned
[`IGESSelect_WorkLibrary`](https://github.com/Open-Cascade-SAS/OCCT/blob/a016080bf6738d6aeae020badee4e888ad1540a5/src/IGESSelect/IGESSelect_WorkLibrary.hxx).
The actual supported byte-only path is basic `IGESControl_Reader::ReadFile("")`:
[`igesread.c`](https://github.com/Open-Cascade-SAS/OCCT/blob/a016080bf6738d6aeae020badee4e888ad1540a5/src/IGESFile/igesread.c)
starts with `stdin` and calls `OSD_OpenFile` only for a nonempty filename. The worker
replays its preflighted bytes through an owned anonymous pipe into stdin. It
closes the read endpoint on every reader completion/exception before joining the
producer, and ignores SIGPIPE so early parser termination cannot hang the writer.
There is no temporary file, `/dev/fd` path or original-source reopen. The process
does exactly one request; the library may close stdin. IGES type 416 and type
402/406 form 12 external-reference declarations are rejected during fixed-record
preflight. Referenced sibling/component/material/texture resolution is excluded.

Global OCCT parameters are confined to the disposable process. Output units are
explicitly millimetres; transforms are applied before computing approximate mesh
bounds. Basic geometry has no names, authors, colors, annotations or assembly
labels. Geometry can itself reveal sensitive shape/lettering, so CAD previews
require explicit enablement and geometry thumbnails remain excluded.
All kernel C/C++ report streams are redirected to `/dev/null` before construction;
only the binary mesh protocol uses a preserved output descriptor. Exceptions are
converted to typed exit status without parser error text. No source bytes or
geometry are logged or durably cached by the worker.

## Containment and evidence

Before import: 16 MiB input, 100,000 entity entries, IGES 80-column/ordered-section
records, STEP 128 nested levels/two million tokens. During output collection:
100,000 faces, 500,000 triangles, 1,500,000 vertices, 32 MiB serialized output,
finite/ranged coordinates, nonzero bounded extent, valid indices and unit normals.
Checks occur before collecting/reserving each face's output arrays. Every root
must transfer and every face must retain valid nonzero triangles; failed/partial
transfer or tessellation declines the request. Zero-area pole/seam artifacts are
discarded without discarding a surface face. Flat mesh values approximate the
source and are not engineering measurements.

Native kernel internals may allocate before output checks. A physical-footprint
monitor checks every 10 ms and terminates the child with budget status above
512 MiB; unavailable own-process footprint observation fails closed with internal
status rather than silently disabling the guard. An oversized allocation can
overshoot between samples. The child also
uses a 15-second CPU limit (16-second hard backstop), disables core dumps, and
the parent owns wall timeout/cancellation, kill and reap. Darwin mapped shared
cache makes a small address-space cap unsuitable; no hard allocation/virtual-
memory guarantee is claimed. Longer hostile corpora and real sandbox inheritance
remain validation gates.

`python3 Native/CADWorker/Tests/test_worker.py --worker
.build/cad/worker-arm64/CADWorker --generator
.build/cad/worker-arm64/CADFixtureGenerator` passed 11 process checks: malformed
and oversize input, closed arguments, external references, entity/depth budgets,
box bounds/winding/normals, transforms, units, trimmed-hole area, curved/pole
surfaces, synthetic metadata omission, source hashes/no sibling files, and owned
process kill/reap. The synthetic STEP fixture is AP214; these tests do not
establish all AP203/AP242 or mixed external assembly behavior.

The optional private 4,299,318-byte IGES acceptance case was read locally and
never copied/uploaded into fixtures or output files. One fresh Release worker
request yielded all 720 faces, 18,813 vertices, 27,335 triangles and a 779,604-byte
payload in approximately 1.11 seconds; `/usr/bin/time -l` measured peak RSS
138,018,816 bytes and physical footprint 129,614,376 bytes. Its SHA256 stayed
unchanged. These are one local fresh-process observations, not cold/warm Finder
benchmarks or release targets. Signed-host/provider, minimum-OS, Intel, native
accessibility and longer memory/cancellation checks are owned by integration
validation and remain separate from this kernel result.
