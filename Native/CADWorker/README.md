# CAD worker

`python3 script/build_cad_worker.py --architecture arm64 --test` creates an
architecture-specific Release helper at `.build/cad/worker-arm64/CADWorker`.
Use `--architecture x86_64` to prepare the other architecture; test it on a
compatible host. The build downloads and checksums an official source archive
inside ignored `.build/cad`; it never installs system packages. CMake, Xcode's
SDK/compiler and Python 3.9 or later are required. Direct Xcode builds must
prepare each requested helper architecture first. The builder writes Xcode
input/output file lists for the parent bundle/signing phase.

The request is one `iges` or `step` argument and at most 16 MiB of source bytes
on stdin. No source path is accepted. STEP uses `ReadStream`; the pinned IGES
implementation uses the documented empty-name stdin branch of `ReadFile`,
fed through a private anonymous pipe after preflight. No source snapshot,
texture/material/document sidecar or external assembly is read or written.

Protocol v1 is little endian: `PRVCAD01`, UInt32 vertex count, index count,
face count and unit word, six Float64 tessellated bounds in millimetres, six
Float32 values per vertex (centred normalized position and unit normal), then
UInt32 indices. The header is 72 bytes. Maximum extent is normalized to 2.
The only output is validated geometry and numeric bounds/counts. Kernel stdout
and stderr reports are discarded. Exit statuses are 0 success, 2 malformed,
3 unsupported/external dependency, 4 resource allowance (input size, entity or
token counts, memory, CPU), 5 internal error and 6 geometry ceiling (the finished
model has more faces, vertices, triangles or serialized bytes than the protocol
carries; the app words it as "too complex" instead of "too large").

The unit word is `1 | (code << 8)`. Byte 0 is always 1: the bounds and positions
are millimetres, whatever the file declared. Byte 1 is the length unit the source
file declared, `CADUnit.cpp` reading it from the bytes the worker already
preflighted: 0 none, 1 micrometre, 2 millimetre, 3 centimetre, 4 metre, 5 inch,
6 foot. The upper 16 bits are reserved and zero, so a writer that grows the word
cannot be mistaken for an older one. Before the field existed the word was the
constant 1, which is the word for "none". IGES reads the Global section's unit
flag (1 inch, 2 millimetre, 4 foot, 6 metre, 9 micron, 10 centimetre) and, for
flag 3, a unit name from a short table; miles, kilometres, mils and microinches
have no code. STEP reads the length unit that each `GLOBAL_UNIT_ASSIGNED_CONTEXT`
points at (an SI unit with the milli, centi, micro or no prefix, or a
`CONVERSION_BASED_UNIT` named inch, foot or one of the metric names). A file with
no readable context, several contexts that disagree, two length units in one
context or a unit without a code reports 0. The decoder is `CADMeshDecoder.swift`.

`Tests/test_protocol.py` compiles `Tests/ProtocolTests.cpp` with `CADInput.cpp`,
`CADProtocol.cpp` and `CADUnit.cpp` (no OCCT needed) and checks the exit codes,
the unit word, the header, the preflight budgets and both unit scanners, with the
address and undefined-behaviour sanitizers where the compiler has them. The same
files build as the CMake target `CADProtocolTests`, which the default build skips.
`Tests/test_worker.py` still needs the OCCT build (`script/build_cad_worker.py --test`).

`SOURCE-OFFER.md` is the written offer for the library's source and relinking
materials (pending legal review) and `LICENSE` is the MIT License for the
helper's sources. `script/package_cad_source_offer.py` assembles and checks the
bundle from the repository alone. The public repository
`canburaks/spacepane-cad-source` mirrors that bundle, one tagged release at a
time: `python3 script/package_cad_source_offer.py --output <empty directory>`.

Limits are 100,000 input entities/faces, 500,000 triangles, 1,500,000 vertices
and 32 MiB serialized output. STEP token count/depth and IGES record structure
are checked before import. Each process has 15 CPU seconds and a sampled
512 MiB physical-footprint guard; the parent owns deadline/cancellation and
kill/reap. Sampling is not a hard allocation-before-use bound and can overshoot.
Darwin address-space limits are unsuitable for a mapped shared-cache process;
no hard virtual-memory cap is claimed. Kernel work is contained in one disposable
process and CAD remains disabled by default pending broader hostile/host checks.

All transferred geometric roots must produce faces. Every face must triangulate
successfully and retain nonzero surface triangles. Zero-area tessellation
artifacts at collapsed poles/trim seams are removed. Loose curve-only roots,
failed transfer/meshing and external dependencies decline the whole request.
The initial result is a flat approximate surface mesh without document names,
annotations, materials, colors, assembly hierarchy or curve primitives.

The complete dependency/relinking audit is in `docs/dependencies/occt.md`.
