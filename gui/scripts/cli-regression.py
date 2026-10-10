#!/usr/bin/env python3
"""Run original six CTest groups with copied binaries in a fresh evidence root.

The original Python suites remove their output folders; relocating the copied
CLI keeps every historical artifact intact. No C++ source/build is modified.
"""
import hashlib
import json
import pathlib
import os
import shutil
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
evidence = pathlib.Path(os.environ.get('BURNLIGHT_EVIDENCE_DIR', str(root / 'gui/evidence')))
evidence.mkdir(parents=True, exist_ok=True)
scratch = pathlib.Path(tempfile.mkdtemp(prefix='burnlight-original-regression-', dir='/private/tmp'))
bundle = scratch / 'Relocated Local.app'
shutil.copytree(root / 'gui/output/BurnLight.app', bundle)
build = bundle / 'Contents/MacOS'
for name in ('backend_target_test', 'recovery_image_test', 'drive_selection_test'):
    shutil.copy2(root / 'gui/.adapter-build' / name, build / name)
    for lib in ('libisofs.6.dylib', 'libudfread.3.dylib'):
        original = '/opt/homebrew/opt/' + ('libisofs' if lib.startswith('libisofs') else 'libudfread') + '/lib/' + lib
        subprocess.run(['/usr/bin/install_name_tool', '-change', original, '@executable_path/../Frameworks/' + lib, str(build / name)], check=True)
    subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', str(build / name)], check=True)
# Generate the six definitions from checked-in CMake instead of depending on a
# developer's untracked build/CTestTestfile.cmake. This does not build or alter it.
definitions = scratch / 'definitions'
configured = subprocess.run(['cmake', '-S', str(root), '-B', str(definitions),
                            '-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk',
                            '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0'],
                           text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(evidence / 'cli-regression-configure.log').write_text(configured.stdout)
if configured.returncode:
    raise SystemExit(configured.returncode)
text = (definitions / 'CTestTestfile.cmake').read_text()
text = text.replace(str(definitions), str(build)).replace(str(root / 'artifacts/udf-tests'), str(bundle / 'Contents/artifacts/udf-tests'))
(build / 'CTestTestfile.cmake').write_text(text)
process = subprocess.run(['ctest', '--test-dir', str(build), '--output-on-failure'], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(evidence / 'cli-regression.log').write_text(process.stdout)
report = dict(exitCode=process.returncode, source='unchanged original six CTest definitions, bundled CLI and separately compiled core tests; drive_selection adds Session-address contradiction cases', bundledCLIsha256=hashlib.sha256((build / 'appendcd').read_bytes()).hexdigest(), scratch=str(scratch), physicalWrites=0, originalCLIsha256=hashlib.sha256((root / 'build/appendcd').read_bytes()).hexdigest())
(evidence / 'cli-regression.json').write_text(json.dumps(report, indent=2) + '\n')
print(process.stdout)
raise SystemExit(process.returncode)
