# SpacePane CAD helper: source

SpacePane's optional STEP and IGES previews run in `CADWorker`, a small separate executable. It statically links
parts of Open CASCADE Technology (OCCT) 7.9.3, which is licensed under the GNU Lesser General Public
License version 2.1 with the Open CASCADE exception. This repository is the corresponding source for that helper:
it lets you modify the library and relink the helper for your own use. The SpacePane app and its Quick Look
extensions are not part of it and never link OCCT.

## What is here

- `SOURCE-OFFER.md`: the written offer, with the contact address and the download location.
- `PINNED-SOURCE.txt`: the exact OCCT revision, its archive URL and SHA-256.
- `Native/CADWorker`: the helper's source, tests and synthetic fixtures.
- `script/build_cad_worker.py`, `script/embed_cad_worker.py`, `Configuration/CADWorker.entitlements`: the build,
  the embedding step and the sandbox entitlements. `excerpts/` shows the two parts that matter for the library.
- `ThirdPartyNotices/OCCT`: the OCCT license, the Open CASCADE exception and the notices for the generated parser.
- `docs/dependencies/occt.md`: the dependency audit.
- `MANIFEST.sha256`: the SHA-256 of every other file. Check it with `shasum -a 256 -c MANIFEST.sha256`.

## Rebuild and relink

You need CMake, Xcode's command-line tools and Python 3.9 or newer.

```sh
python3 script/build_cad_worker.py --architecture arm64 --test
```

The script does this, in order:

1. It takes the archive from `.build/cad/occt-a016080bf6738d6aeae020badee4e888ad1540a5.tar.gz` when that file exists, otherwise it downloads
   https://codeload.github.com/Open-Cascade-SAS/OCCT/tar.gz/a016080bf6738d6aeae020badee4e888ad1540a5.
2. It refuses an archive whose SHA-256 is not `c533f2667b59921bd6bd40ce82e7b9900b0289ccc731af5fdeeba097de80ef0f`.
3. It unpacks the archive under `.build/cad` and applies the build-manifest edits in
   `excerpts/narrow_manifests.py.txt`.
4. It builds the static kernel, then links `CADWorker` from `Native/CADWorker`.

To link a helper with a modified library, edit the unpacked tree under
`.build/cad/OCCT-a016080bf6738d6aeae020badee4e888ad1540a5` and run the same command again.

## Licenses

Everything written for SpacePane in this repository is under the MIT License (`LICENSE`). OCCT and the
third-party notices keep their own terms (`ThirdPartyNotices/OCCT`).

## Where this comes from

This repository is generated from SpacePane's private repository by `script/package_cad_source_offer.py`, one
release at a time, and tagged with the release. Do not send changes here. Questions and requests: see
`SOURCE-OFFER.md`.
