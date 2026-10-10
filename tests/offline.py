#!/usr/bin/env python3
"""Synthetic Joliet fixtures; never represents Windows/optical acceptance."""
import hashlib
import pathlib
import shutil
import subprocess
import sys

CLI = pathlib.Path(sys.argv[1]).resolve()
ROOT = CLI.parent.parent
OUT = ROOT / "artifacts" / "offline"
if OUT.exists():
    shutil.rmtree(OUT)
OUT.mkdir(parents=True)
log = []
checks = []

def run(*args, ok=True):
    result = subprocess.run([str(x) for x in args], capture_output=True, text=True)
    log.append("$ " + " ".join(str(x) for x in args) + "\n" + result.stdout + result.stderr)
    if ok and result.returncode:
        raise AssertionError(log[-1])
    if not ok and result.returncode == 0:
        raise AssertionError("Expected failure: " + log[-1])
    return result.stdout + result.stderr

def check(label, condition):
    assert condition, label
    checks.append(label)

def file(folder, name, data):
    path = folder / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)

def build(source, last, next_lba, files, image, base=0):
    return run(CLI, "build", files, image, "--source", source, "--last", last,
               "--next", next_lba, "--source-base", base)

def corrupt(image, label, mutate):
    destination = OUT / (label + ".iso")
    data = bytearray(image.read_bytes())
    mutate(data)
    destination.write_bytes(data)
    manifest = image.with_suffix(".iso.manifest").read_text()
    manifest = manifest.replace(hashlib.sha256(image.read_bytes()).hexdigest(), hashlib.sha256(data).hexdigest())
    destination.with_suffix(".iso.manifest").write_text(manifest)
    return run(CLI, "verify", destination, ok=False)

def mutate_vss(data):
    position = 16 * 2048 + 80
    value = int.from_bytes(data[position:position+4], "little") + 1
    data[position:position+4] = value.to_bytes(4, "little")
    data[position+4:position+8] = value.to_bytes(4, "big")

try:
    first = OUT / "session1-files"
    second = OUT / "session2-files"
    third = OUT / "session3-files"
    for folder in (first, second, third):
        folder.mkdir()
    file(first, "hello-from-windows.txt", b"synthetic Windows-like Joliet old payload\n")
    file(first, "测试文件.xlsx", b"XLSX name test only, not an Excel workbook\n")
    file(first, "MixedCase.TXT", b"case preserved\n")
    file(first, "目录/二级/旧文件.txt", b"nested old\n")
    file(first, "empty-old.txt", b"")
    file(first, "large-old.bin", bytes(range(256)) * 4096)
    file(first, ".DS_Store", b"legacy Finder cache on disc\n")
    longname = "LongName_" + "abcdef" * 12 + ".txt"
    file(first, longname, b"long old filename\n")
    file(second, "hello-from-macos.txt", b"new macOS payload\n")
    file(second, "中文测试.txt", "新数据\n".encode())
    file(second, "目录/二级/新文件.txt", b"nested new\n")
    file(second, "empty-new.txt", b"")
    file(second, "大小写AbCd.txt", b"case mixed\n")
    file(second, ".DS_Store", b"new Finder cache must not replace legacy cache\n")
    file(second, "目录/二级/.DS_Store", b"nested Finder cache must not enter the image\n")
    first_iso = OUT / "session1.iso"
    run("/opt/homebrew/bin/mkisofs", "-quiet", "-iso-level", "3", "-J", "-joliet-long", "-input-charset", "utf-8", "-V", "WINFIXTURE", "-o", first_iso, first)
    listing = run(CLI, "list", "--source", first_iso, "--last", 0)
    check("Joliet-only Chinese/long/case/nested names imported", all(x in listing for x in ("测试文件.xlsx", longname, "MixedCase.TXT", "目录/二级/旧文件.txt")))
    legacy_cache = next(line for line in listing.splitlines() if line.startswith('FILE ".DS_Store"'))
    inspect = run(CLI, "inspect", "--source", first_iso, "--last", 0)
    check("PVD/SVD and both-endian path tables parsed", "Joliet SVD" in inspect and "ISO9660 PVD" in inspect)
    second_iso = OUT / "session2.iso"
    output = build(first_iso, 0, 12128, second, second_iso)
    check("No old blocks read by generator", "Old-source reads during generation: 0" in output)
    check("Joliet parent defect corrected before strict verification", "Joliet parent records=2" in output)
    check("Old 1 MiB payload not copied into continuation", second_iso.stat().st_size < 200000)
    check("Old extents, new hashes, names, PVD/SVD verified", "Verified:" in run(CLI, "verify", second_iso))
    independent = run("/opt/homebrew/bin/isoinfo", "-N", 12128, "-J", "-f", "-i", second_iso)
    check("Independent isoinfo handles absolute continuation offset", "hello-from-windows.txt" in independent and "hello-from-macos.txt" in independent)
    check("New Volume Space Size is session block count", int.from_bytes(second_iso.read_bytes()[16*2048+80:16*2048+84], "little") == second_iso.stat().st_size // 2048)
    combined = OUT / "composite.iso"
    with combined.open("wb") as f:
        f.write(first_iso.read_bytes())
        f.seek(12128 * 2048)
        f.write(second_iso.read_bytes())
    listing = run(CLI, "list", "--source", combined, "--last", 12128)
    check("Composite last session resolves old and new trees", "中文测试.txt" in listing and "测试文件.xlsx" in listing)
    check("Finder caches excluded recursively; legacy cache name/size/extents unchanged",
          legacy_cache in listing.splitlines() and "目录/二级/.DS_Store" not in listing and listing.count('FILE ".DS_Store"') == 1)
    file(third, "目录/二级/第三轮中文.txt", b"third session\n")
    third_iso = OUT / "session3.iso"
    build(combined, 12128, 24576, third, third_iso)
    check("Second continuation preserves extents across two old sessions", "Verified:" in run(CLI, "verify", third_iso))
    shifted = OUT / "shifted.iso"
    shifted.write_bytes(first_iso.read_bytes())
    # Session-only source needs its original absolute descriptors/extents.
    sliced = OUT / "session2-only.iso"
    sliced.write_bytes(second_iso.read_bytes())
    check("Session-only source maps absolute metadata LBA", "hello-from-macos.txt" in run(CLI, "list", "--source", sliced, "--source-base", 12128, "--last", 12128))
    collision = OUT / "collision"
    collision.mkdir()
    file(collision, "mixedcase.txt", b"must fail\n")
    failure = run(CLI, "build", collision, OUT / "collision.iso", "--source", first_iso, "--last", 0, "--next", 12128, ok=False)
    check("Case conflict fails before output", "collision" in failure and not (OUT / "collision.iso").exists())
    conflict = OUT / "conflict"
    conflict.mkdir()
    file(conflict, "hello-from-windows.txt", b"must fail\n")
    check("Existing files cannot be silently replaced", "conflict" in run(CLI, "build", conflict, OUT / "conflict.iso", "--source", first_iso, "--last", 0, "--next", 12128, ok=False))
    symlinks = OUT / "symlinks"
    symlinks.mkdir()
    (symlinks / "linked").symlink_to(first / "large-old.bin")
    check("Symlinks rejected", "symlink" in run(CLI, "build", symlinks, OUT / "symlink.iso", "--source", first_iso, "--last", 0, "--next", 12128, ok=False))
    check("Output overwrite refused", "already exists" in run(CLI, "build", second, second_iso, "--source", first_iso, "--last", 0, "--next", 12128, ok=False))
    check("Wrong endian field rejected after rebinding hash", "endian" in corrupt(second_iso, "bad-endian", lambda b: b.__setitem__(16*2048+84, b[16*2048+84] ^ 1)))
    check("Wrong Volume Space Size rejected", "Volume Space Size" in corrupt(second_iso, "bad-vss", mutate_vss))
    check("Corrupt path table rejected", "path table" in corrupt(second_iso, "bad-path-table", lambda b: b.__setitem__(int.from_bytes(b[17*2048+148:17*2048+152], "big")*2048 - 12128*2048 + 7, 2)))
    data = bytearray(second_iso.read_bytes())
    data[-1] ^= 1
    unbound = OUT / "bad-image-hash.iso"
    unbound.write_bytes(data)
    shutil.copyfile(str(second_iso) + ".manifest", str(unbound) + ".manifest")
    check("Image/manifest hash binding enforced", "hash differs" in run(CLI, "verify", unbound, ok=False))
    rr = OUT / "different-rr.iso"
    run("/opt/homebrew/bin/mkisofs", "-quiet", "-R", "-J", "-joliet-long", "-input-charset", "utf-8", "-o", rr, first)
    data = bytearray(rr.read_bytes())
    original = b"hello-from-windows.txt"
    hits = 0
    for i in range(len(data) - 5 - len(original)):
        if data[i:i+2] == b"NM" and data[i+5:i+5+len(original)] == original:
            data[i+5:i+5+len(original)] = b"R" * len(original)
            hits += 1
    assert hits, "RR fixture patch failed"
    rr.write_bytes(data)
    imported = run(CLI, "list", "--source", rr, "--last", 0)
    check("Joliet preferred even when RR names disagree", "hello-from-windows.txt" in imported and "R" * len(original) not in imported)
    build(rr, 0, 12128, second, OUT / "rr-continuation.iso")
    print("PASS: " + str(len(checks)) + " offline assertions")
    (OUT / "result.txt").write_text("SYNTHETIC_OFFLINE_PASS\nWINDOWS_NOT_TESTED\n" + "\n".join(checks) + "\n")
finally:
    (OUT / "commands.log").write_text("\n\n".join(log))
