# Open CASCADE source offer for the SpacePane CAD helper

**Status: pending legal review.** This text records the route decision 027 calls D20 (a): publish the corresponding
source and give a written offer. It takes effect when the public repository named below is published. It has not been
reviewed by counsel, and no release may rely on it until the open questions at the end are answered. Whether to ship 1.0
without STEP and IGES instead is the owner's decision and is not made here.

## What this covers

SpacePane's optional STEP (`.step`, `.stp`) and IGES (`.igs`, `.iges`) previews run in `CADWorker`, a small separate
executable (`Native/CADWorker`). `CADWorker` statically links parts of Open CASCADE Technology (OCCT) 7.9.3, which is
licensed under the GNU Lesser General Public License version 2.1 with the Open CASCADE exception, version 1.0. The
Swift app and both Quick Look extensions never link OCCT; they exchange bytes with the helper through pipes (the Quick
Look preview reaches it through an embedded XPC service, the companion launches it directly). The unmodified licenses
and notices are in `ThirdPartyNotices/OCCT`.

- OCCT 7.9.3, tag `V7_9_3`, commit `a016080bf6738d6aeae020badee4e888ad1540a5`.
- Source archive: <https://codeload.github.com/Open-Cascade-SAS/OCCT/tar.gz/a016080bf6738d6aeae020badee4e888ad1540a5>
- SHA-256 of that archive: `c533f2667b59921bd6bd40ce82e7b9900b0289ccc731af5fdeeba097de80ef0f`

## The offer

On request, SpacePane's publisher will give any third party, for a charge no greater than the cost of performing the
distribution, a machine-readable copy of the following, so that the library can be modified and the helper relinked
for the recipient's own use:

1. The complete corresponding source of OCCT as used: the archive named above, unmodified, plus the build-manifest
   changes the build script applies after unpacking it (`excerpts/narrow_manifests.py.txt`; no upstream parser
   implementation is edited).
2. The complete source of the helper, the "work that uses the Library" (`Native/CADWorker`, including its tests and
   its synthetic fixtures), and its build files.
3. The build and embedding scripts (`script/build_cad_worker.py`, `script/embed_cad_worker.py`) and the helper's
   sandbox entitlements (`Configuration/CADWorker.entitlements`).
4. The license texts and notices in `ThirdPartyNotices/OCCT`, and the dependency audit `docs/dependencies/occt.md`.

The offer is valid for three years from the day a SpacePane release that contains the helper is first distributed.
Requests go to info@spacepane.com. The same materials are downloadable without charge from
<https://github.com/canburaks/spacepane-cad-source>: the repository holds the bundle described below, and each release
of that repository carries a tarball that also includes the pinned OCCT archive, so the library source does not depend
on the upstream host.

The helper's own sources, the build and embedding scripts and the entitlements file are licensed under the MIT License
(`LICENSE`, also at `Native/CADWorker/LICENSE`). The license permits modifying them for the recipient's own use and
reverse engineering them to debug such modifications. The OCCT library and the third-party notices keep their own
terms, which are in `ThirdPartyNotices/OCCT`.

## How the materials are assembled

`python3 script/package_cad_source_offer.py --output <directory>` builds the bundle from the repository alone, with no
network access, and `--check` verifies that it is complete: every file of the helper, the notices, the pinned reference
in `PINNED-SOURCE.txt`, and a `MANIFEST.sha256` of every file. `--include-archive <file>` adds a local copy of the
pinned archive after checking its SHA-256, so the bundle can be rebuilt offline. The bundle mirrors the repository's
layout, so `python3 script/build_cad_worker.py --architecture arm64 --test` runs from its root. That script verifies the
archive checksum before use and refuses a mismatch, unpacks it under `.build/cad`, applies the manifest changes, builds the
static kernel and then links `CADWorker` from `Native/CADWorker`. A recipient who edits the unpacked tree under
`.build/cad` and runs the script again links a helper that contains the edited library.

## Open questions for legal review

These are questions, not conclusions; nothing above settles them.

1. Does publishing source with a written offer satisfy section 6 of the LGPL 2.1 for a helper that is statically linked
   with the library and shipped inside an application, or must the bundle also offer object files, or must the helper
   use a shared library instead?
2. Are the Mac App Store's license and signing terms compatible with section 6's requirement that the terms of the
   combined work permit modification for the user's own use and reverse engineering for debugging? A relinked helper
   cannot carry the store signature.
3. Do the GPL-3.0-or-later Bison parser skeleton (with its parser-output exception) and the Flex header notice, both
   compiled into the STEP reader, add any obligation beyond the retained notices?
4. Where must the offer be shown (About, the store description, the website) and in what words, and who keeps the
   download location and the archive available for the length of the offer?
5. Is the three-year period right for the channel, and does a release that stops shipping the helper end the offer
   for earlier releases?

## Maintenance

Whoever changes the pinned revision, the helper's files, the build scripts or the notices updates this text and runs
`python3 script/package_cad_source_offer.py --check`; the check compares the pinned revision, archive checksum and
archive URL here with the build script, `ThirdPartyNotices/OCCT/NOTICE.txt` and `docs/dependencies/occt.md`.
