#!/usr/bin/env python3
"""Embed a prepared CAD helper in an app or XPC service using Xcode bundle paths.

The service has its own sandbox; CADWorker inherits the sandbox of whichever
target owns it. No network access or source build occurs inside Xcode.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(os.environ['SRCROOT'])
architectures = os.environ['ARCHS'].split()
workers = [root / '.build/cad' / ('worker-' + arch) / 'CADWorker' for arch in architectures]
for arch, worker in zip(architectures, workers):
    if not worker.is_file():
        raise SystemExit(f'Prepare CAD dependency first: python3 script/build_cad_worker.py --architecture {arch}')
# Keep these paths relative to the current target. The preview extension embeds
# CADImportService.xpc; that target, rather than the extension, owns its worker.
destination = Path(os.environ['TARGET_BUILD_DIR']) / os.environ['CONTENTS_FOLDER_PATH'] / 'Helpers/CADWorker'
destination.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='cad-sign-', dir=os.environ['TARGET_TEMP_DIR']) as temporary:
    staged = Path(temporary) / 'CADWorker'
    if len(workers) == 1:
        shutil.copy2(workers[0], staged)
    else:
        subprocess.run(['/usr/bin/lipo', '-create', *map(str, workers), '-output', str(staged)], check=True)
    if os.environ.get('CODE_SIGNING_ALLOWED') != 'NO':
        identity = os.environ.get('EXPANDED_CODE_SIGN_IDENTITY') or '-'
        # A bare tool has no Info.plist, so name its signing identifier under the owning
        # bundle as Apple's embedded-helper guidance requires for App Store distribution.
        signing_identifier = os.environ['PRODUCT_BUNDLE_IDENTIFIER'] + '.CADWorker'
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', identity, '--identifier', signing_identifier,
                        '--options', 'runtime', '--timestamp=none',
                        '--entitlements', str(root / 'Configuration/CADWorker.entitlements'), str(staged)], check=True)
    shutil.copy2(staged, destination)
notices = Path(os.environ['TARGET_BUILD_DIR']) / os.environ['UNLOCALIZED_RESOURCES_FOLDER_PATH'] / 'OCCT-Notices'
shutil.copytree(root / 'ThirdPartyNotices/OCCT', notices, dirs_exist_ok=True)
