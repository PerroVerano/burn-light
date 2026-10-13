#!/usr/bin/env python3
"""Build the isolated, explicitly marked cdrtools backend from pinned source."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHA256 = 'c7e4f732fb299e9b5d836629dadf5512aa5e6a5624ff438ceb1d056f4dcb07c2'
URL = 'https://downloads.sourceforge.net/project/cdrtools/alpha/cdrtools-3.02a09.tar.gz'


def replace_once(path, old, new):
    data = path.read_text(encoding='latin1')
    if data.count(old) != 1:
        raise RuntimeError(f'Unexpected upstream source anchor in {path}: {old[:80]!r}')
    path.chmod(path.stat().st_mode | 0o200)
    path.write_text(data.replace(old, new), encoding='latin1')


def patch(source):
    c = source / 'cdrecord/cdrecord.c'
    replace_once(c, '#include "version.h"', '#include "version.h"\n#include "scsimmc.h"\n#include "appendcd_backend.h"')
    replace_once(c, 'gracetime#,minbuf#";', 'gracetime#,minbuf#,appendcd-probe,appendcd-nwa#";')
    replace_once(c, '&gracetime, &dminbuf)) < 0)', '&gracetime, &dminbuf, &appendcd_probe, &appendcd_expected_nwa)) < 0)')
    replace_once(c, 'if (flags & F_MEDIAINFO) {\n\t\t(*dp->cdr_prdiskstatus)(scgp, dp);',
                 'if (flags & F_MEDIAINFO) {\n\t\tif (appendcd_probe) {\n\t\t\tappendcd_dump_tracks(scgp);\n\t\t\tcomexit(0);\n\t\t}\n\t\t(*dp->cdr_prdiskstatus)(scgp, dp);')
    replace_once(source / 'cdrecord/version.h', '#define\tVERSION\t"3.02a09"', '#define\tVERSION\t"3.02a09-appendcd-dvd1"')
    shutil.copyfile(ROOT / 'backend/appendcd_backend.h', source / 'cdrecord/appendcd_backend.h')


def patch_iokit(source):
    path = source / 'libscg/scsi-mac-iokit.c'
    if 'scsi-mac-iokit.c-1.17-appendcd-' not in path.read_text(encoding='latin1'):
        replace_once(path, '"scsi-mac-iokit.c-1.17"', '"scsi-mac-iokit.c-1.17-appendcd-init"')
        replace_once(path, 'return (_scg_auth_schily);', 'return ("appendcd unofficial");')
        replace_once(path, 'malloc(sizeof (struct scg_local))', 'calloc(1, sizeof (struct scg_local))')
    if 'appendcd IOKit matching' not in path.read_text(encoding='latin1'):
        replace_once(path, '\tdict = NULL;\n\n\tif (scsiObjectIterator == 0 ||',
            '\tdict = NULL;\n\tif (ioReturnValue != kIOReturnSuccess)\n\t\tjs_fprintf((FILE *)scgp->errfile, "appendcd IOKit matching port=%u class=%s result=0x%x iterator=%u\\n", masterPort, realdevice, ioReturnValue, scsiObjectIterator);\n\n\tif (scsiObjectIterator == 0 ||')
    # The IOServiceAuthorize experiment failed with kIOReturnBadMessageID
    # (0xe00002c6); keep the executor's existing IOKit access route.
    experimental_auth = '\t/* appendcd explicit service authorization, using the public macOS API. */\n\tioReturnValue = IOServiceAuthorize(scsiDevice, kIOServiceInteractionAllowed);\n\tif (ioReturnValue != kIOReturnSuccess) {\n\t\tjs_snprintf(scgp->errstr, SCSI_ERRSTR_SIZE, "appendcd optical authorization denied: 0x%x uid=%u euid=%u", ioReturnValue, getuid(), geteuid());\n\t\tgoto out;\n\t}\n'
    if experimental_auth in path.read_text(encoding='latin1'):
        replace_once(path, experimental_auth, '')
    # macOS masterPort was otherwise read uninitialized before the first call
    # to IOMasterPort; calloc initializes all handles, including masterPort.
    for pattern in ('libscg/OBJ/*/scsi-transport.o', 'libscg/OBJ/*/scsitransp.o'):
        for generated in source.glob(pattern):
            generated.unlink()


def patch_dvd(source):
    c = source / 'cdrecord/cdrecord.c'
    if 'appendcd_checked_track = appendcd_select_track' in c.read_text(encoding='latin1'):
        return
    replace_once(c, '#include "appendcd_backend.h"', '#define APPENDCD_BACKEND_IMPLEMENTATION\n#include "appendcd_backend.h"')
    replace_once(c, 'appendcd-probe,appendcd-nwa#";', 'appendcd-probe,appendcd-nwa#,appendcd-reuse,appendcd-track#,appendcd-reserved-count#";')
    replace_once(c, '&appendcd_probe, &appendcd_expected_nwa)) < 0)', '&appendcd_probe, &appendcd_expected_nwa, &appendcd_reuse, &appendcd_physical_track, &appendcd_reserved_count)) < 0)')
    replace_once(c, '\tif ((flags & F_FORMAT) || (dp->cdr_dstat->ds_flags & DSF_NEED_FORMAT)) {',
        '\tif ((flags & F_WRITE) && (flags & F_MULTI) && (dp->cdr_dstat->ds_flags & DSF_DVD)) {\n\t\tlong verified_nwa;\n\t\tappendcd_checked_track = appendcd_select_track(scgp, &verified_nwa);\n\t}\n\tif ((flags & F_FORMAT) || (dp->cdr_dstat->ds_flags & DSF_NEED_FORMAT)) {')
    replace_once(c, '\tif ((tracks + trackno) > MAX_TRACK) {',
        '\t/* DVD emulated TOC numbering can disagree with physical RZones. */\n\tif (appendcd_checked_track > 0)\n\t\ttrackno = appendcd_checked_track - 1;\n\tif ((tracks + trackno) > MAX_TRACK) {')
    replace_once(c, '\t\t\ttrack[i].trackstart = startsec;',
        '\t\t\tif (appendcd_expected_nwa >= 0 && startsec != appendcd_expected_nwa)\n\t\t\t\tcomerrno(EX_BAD, "appendcd write address changed after opening Track.\\n");\n\t\t\ttrack[i].trackstart = startsec;')
    d = source / 'cdrecord/drv_dvd.c'
    replace_once(d, '#include "cdrecord.h"', '#include "cdrecord.h"\n#include "appendcd_backend.h"')
    old = '\t\tif (track_base(trackp)->tracktype & TOCF_MULTI)\n\t\t\tget_trackinfo(scgp, (caddr_t)&rz, TI_TYPE_TRACK, trackp->trackno, sizeof (rz));\n\t\telse\n\t\t\tread_rzone_info(scgp, (caddr_t)&rz, sizeof (struct rzone_info));\n\t\tdvd_next_addr = a_to_4_byte(rz.next_recordable_addr);'
    new = '''\t\tif (track_base(trackp)->tracktype & TOCF_MULTI) {
\t\t\tif (get_trackinfo(scgp, (caddr_t)&rz, TI_TYPE_TRACK, trackp->trackno, sizeof (rz)) < 0)
\t\t\t\treturn (-1);
\t\t} else if (read_rzone_info(scgp, (caddr_t)&rz, sizeof (rz)) < 0)
\t\t\treturn (-1);
\t\tif (appendcd_reuse && rz.rt && rz.blank && !rz.damage && !rz.lra_v)
\t\t\tdvd_next_addr = a_to_4_byte(rz.rzone_start);
\t\telse {
\t\t\tif (!rz.nwa_v)
\t\t\t\treturn (-1);
\t\t\tdvd_next_addr = a_to_4_byte(rz.next_recordable_addr);
\t\t}
\t\tif (appendcd_expected_nwa >= 0 && dvd_next_addr != appendcd_expected_nwa)
\t\t\treturn (-1);'''
    replace_once(d, old, new)
    replace_once(d, '\treturn (reserve_tr_rzone(scgp, sectors));',
        '\tif (appendcd_reuse) {\n\t\tappendcd_check_reserved_size(scgp, trackp->trackno, sectors);\n\t\treturn (0);\n\t}\n\treturn (reserve_tr_rzone(scgp, sectors));')
    replace_once(d, '\tif (track_base(trackp)->tracktype & TOCF_MULTI)\n\t\tscsi_close_tr_session(scgp, CL_TYPE_SESSION, trackno, TRUE);',
        '\tif (appendcd_reuse)\n\t\tappendcd_close_empty_tracks(scgp);\n\tif (track_base(trackp)->tracktype & TOCF_MULTI) {\n\t\tif (scsi_close_tr_session(scgp, CL_TYPE_SESSION, trackno, FALSE) < 0)\n\t\t\tret = -1;\n\t}')
    replace_once(source / 'cdrecord/version.h', '3.02a09-appendcd-dvd1', '3.02a09-appendcd-dvd2')


def patch_finish(source):
    c = source / 'cdrecord/cdrecord.c'
    if 'appendcd_validate_finish(scgp)' in c.read_text(encoding='latin1'):
        return
    replace_once(c, 'appendcd-reserved-count#";', 'appendcd-reserved-count#,appendcd-finish";')
    replace_once(c, '&appendcd_reserved_count)) < 0)', '&appendcd_reserved_count, &appendcd_finish)) < 0)')
    replace_once(c, '\tif ((flags & F_WRITE) && (flags & F_MULTI) && (dp->cdr_dstat->ds_flags & DSF_DVD)) {',
        '\tif (appendcd_finish) {\n\t\tif (!(flags & F_FIX) || !(flags & F_MULTI) || !(dp->cdr_dstat->ds_flags & DSF_DVD))\n\t\t\tcomerrno(EX_BAD, "appendcd finish requires DVD-R -fix -multi.\\n");\n\t\tappendcd_validate_finish(scgp);\n\t}\n\tif ((flags & F_WRITE) && (flags & F_MULTI) && (dp->cdr_dstat->ds_flags & DSF_DVD)) {')
    replace_once(source / 'cdrecord/version.h', '3.02a09-appendcd-dvd2', '3.02a09-appendcd-dvd3')


def patch_finish_dispatch(source):
    c = source / 'cdrecord/cdrecord.c'
    if 'F_WRITE) && !appendcd_finish' in c.read_text(encoding='latin1'):
        return
    # Upstream -fix also sets F_WRITE. It must use the finalization validator,
    # without entering the empty-Track selector used for payload writing.
    replace_once(c, 'if ((flags & F_WRITE) && (flags & F_MULTI) && (dp->cdr_dstat->ds_flags & DSF_DVD)) {',
                    'if ((flags & F_WRITE) && !appendcd_finish && (flags & F_MULTI) && (dp->cdr_dstat->ds_flags & DSF_DVD)) {')
    replace_once(source / 'cdrecord/version.h', '3.02a09-appendcd-dvd3', '3.02a09-appendcd-dvd4')


def patch_cd(source):
    c = source / 'cdrecord/cdrecord.c'
    if 'appendcd_validate_cd(scgp)' in c.read_text(encoding='latin1'):
        return
    replace_once(c, '\tif (appendcd_finish) {',
        '\tif ((flags & F_WRITE) && !(flags & F_FIX) && (flags & F_MULTI) &&\n\t    !(dp->cdr_dstat->ds_flags & DSF_DVD) && appendcd_expected_nwa >= 0)\n\t\tappendcd_validate_cd(scgp);\n\tif (appendcd_finish) {')
    d = source / 'cdrecord/drv_mmc.c'
    replace_once(d, '#include "cdrecord.h"', '#include "cdrecord.h"\n#include "appendcd_backend.h"')
    replace_once(d, '\tnext_addr = a_to_4_byte(track_info.next_writable_addr);',
        '\tif (!track_info.nwa_valid || a_to_2_byte(track_info.data_len) + 2 < 28)\n\t\treturn (-1);\n\tnext_addr = a_to_4_byte(track_info.next_writable_addr);\n\tif (appendcd_expected_nwa >= 0 && next_addr != appendcd_expected_nwa)\n\t\treturn (-1);')
    replace_once(source / 'cdrecord/version.h', '3.02a09-appendcd-dvd4', '3.02a09-appendcd-media5')


def patch_identity(source):
    transport = source / 'libscg/scsi-mac-iokit.c'
    shutil.copyfile(ROOT / 'backend/appendcd_identity.h', source / 'libscg/appendcd_identity.h')
    if 'APPENDCD_DEVICE_REGISTRY_ID' not in transport.read_text(encoding='latin1'):
        replace_once(transport, '#include <mach/mach_error.h>', '#include <mach/mach_error.h>\n#include "appendcd_identity.h"')
        replace_once(transport, '\tioReturnValue = IOCreatePlugInInterfaceForService(scsiDevice,\n\t\t\tkIOMMCDeviceUserClientTypeID,', '''\t/* Bind the selected optical service before creating a writable client. */
\t{
\t\tconst char *expected = getenv("APPENDCD_DEVICE_REGISTRY_ID");
\t\tuint64_t registry_id = 0;
\t\tif (expected != NULL) {
\t\t\tif (IORegistryEntryGetRegistryEntryID(scsiDevice, &registry_id) != kIOReturnSuccess ||
\t\t\t    !appendcd_registry_matches(expected, registry_id)) {
\t\t\t\tjs_snprintf(scgp->errstr, SCSI_ERRSTR_SIZE, "appendcd optical registry identity changed; refusing device access");
\t\t\t\tgoto out;
\t\t\t}
\t\t\tjs_fprintf((FILE *)scgp->errfile, "APPENDCD_IOKIT_TARGET registry_id=%llu\\n", (unsigned long long)registry_id);
\t\t}
\t}
\tioReturnValue = IOCreatePlugInInterfaceForService(scsiDevice,
\t\t\tkIOMMCDeviceUserClientTypeID,''')
        replace_once(transport, 'scsi-mac-iokit.c-1.17-appendcd-init', 'scsi-mac-iokit.c-1.17-appendcd-identity')
        replace_once(source / 'cdrecord/version.h', '3.02a09-appendcd-media5', '3.02a09-appendcd-media6')


def patch_initial(source):
    c = source / 'cdrecord/cdrecord.c'
    if 'appendcd-initial' in c.read_text(encoding='latin1'):
        return
    replace_once(c, 'appendcd-finish";', 'appendcd-finish,appendcd-initial";')
    replace_once(c, '&appendcd_finish)) < 0)', '&appendcd_finish, &appendcd_initial)) < 0)')
    replace_once(source / 'cdrecord/version.h', '3.02a09-appendcd-media6', '3.02a09-appendcd-initial7')


def patch_vat(source):
    """Separate CD-R variable-packet executor; never replace media6/initial7."""
    c = source / 'cdrecord/cdrecord.c'
    if 'appendcd-vat' not in c.read_text(encoding='latin1'):
        replace_once(c, 'appendcd-finish";', 'appendcd-finish,appendcd-vat";')
        replace_once(c, '&appendcd_finish)) < 0)', '&appendcd_finish, &appendcd_vat)) < 0)')
        replace_once(c, '\tif ((flags & F_WRITE) && !(flags & F_FIX) && (flags & F_MULTI) &&',
            '\tif (flags & F_WRITE) {\n\t\tif (!appendcd_vat || appendcd_reuse || appendcd_finish ||\n\t\t    (flags & F_FIX) || !(flags & F_MULTI) || tracks != 1 ||\n\t\t    get_curprofile(scgp) != 0x09 || !is_packet(&track[1]) ||\n\t\t    track[1].pktsize != 0 || track[1].padsecs != 0)\n\t\t\tcomerrno(EX_BAD, "appendcd VAT requires one CD-R variable packet track, -multi and no padding.\\n");\n\t}\n\tif ((flags & F_WRITE) && !(flags & F_FIX) && (flags & F_MULTI) &&')
    driver = source / 'cdrecord/drv_mmc.c'
    if 'appendcd_fixate_packet' not in driver.read_text(encoding='latin1'):
        replace_once(driver, '#include "cdrecord.h"', '#include "cdrecord.h"\n#include "appendcd_backend.h"')
        # fixate_mmc receives the lead-in record, whose PACKET flag upstream
        # never sets. Without the data-track check it only flushes the cache.
        replace_once(driver,
            '\tif (is_tao(trackp) || is_packet(trackp)) {\n\t\tret = scsi_close_tr_session(scgp, CL_TYPE_SESSION, 0,',
            '\tif (is_tao(trackp) || appendcd_fixate_packet(is_packet(trackp), is_packet(track_base(trackp) + 1))) {\n\t\tret = scsi_close_tr_session(scgp, CL_TYPE_SESSION, 0,')
    version = source / 'cdrecord/version.h'
    for previous in ('3.02a09-appendcd-media6', '3.02a09-appendcd-vat8'):
        if previous in version.read_text(encoding='latin1'):
            replace_once(version, previous, '3.02a09-appendcd-vat9')
    if '3.02a09-appendcd-vat9' not in version.read_text(encoding='latin1'):
        raise RuntimeError('Unexpected VAT backend version after Session-fixation patch')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', type=pathlib.Path)
    parser.add_argument('--output-dir', type=pathlib.Path, default=ROOT / 'build/backend')
    parser.add_argument('--resume', action='store_true', help='Resume a previously extracted local build')
    parser.add_argument('--initial', action='store_true', help='Build separate blank-disc executor; does not replace the validated append executor')
    parser.add_argument('--vat', action='store_true', help='Build separate UDF 2.01 CD-R variable-packet executor')
    parser.add_argument('--runtime-dir', type=pathlib.Path, help='Copy the marked binary to an isolated user libexec directory')
    args = parser.parse_args()
    if args.initial and args.vat:
        parser.error('--initial and --vat are mutually exclusive')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    archive = args.archive or output / 'cdrtools-3.02a09.tar.gz'
    if not archive.exists():
        urllib.request.urlretrieve(URL, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError('Official cdrtools source SHA256 mismatch')
    source = output / 'cdrtools-3.02'
    if args.resume:
        if '3.02a09-appendcd-' not in (source / 'cdrecord/version.h').read_text():
            raise RuntimeError('Resume requires the marked local patched source')
        shutil.copyfile(ROOT / 'backend/appendcd_backend.h', source / 'cdrecord/appendcd_backend.h')
    else:
        if source.exists():
            raise RuntimeError(f'Refusing to overwrite source directory: {source}')
        # Exact trusted archive hash checked before extraction; compatible with
        # Apple Python, which predates tar extraction filters.
        with tarfile.open(archive) as tar:
            for entry in tar.getmembers():
                if entry.name.startswith('/') or '..' in pathlib.PurePosixPath(entry.name).parts:
                    raise RuntimeError('Unexpected path in pinned source archive')
            tar.extractall(output)
        patch(source)
    patch_iokit(source)
    patch_dvd(source)
    patch_finish(source)
    patch_finish_dispatch(source)
    patch_cd(source)
    patch_identity(source)
    if args.initial:
        patch_initial(source)
    elif 'appendcd-initial' in (source / 'cdrecord/cdrecord.c').read_text(encoding='latin1'):
        raise RuntimeError('An initial build cannot be resumed as the append-only backend')
    if args.vat:
        patch_vat(source)
    elif 'appendcd-vat' in (source / 'cdrecord/cdrecord.c').read_text(encoding='latin1'):
        raise RuntimeError('A VAT build cannot be resumed as the baseline backend')
    for profiled in source.glob('lib*/*_p.mk'):
        profiled.unlink()
    smake = shutil.which('smake')
    if not smake:
        raise RuntimeError('Install the Homebrew build dependency: brew install smake')
    sdk = pathlib.Path('/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk')
    if not sdk.exists():
        raise RuntimeError('Expected tested macOS 26 SDK is unavailable')
    # The legacy argv/frame-pointer scanner traps under AppleClang 21. The
    # upstream NO_SCANSTACK option disables this optional build-time feature.
    flags = f'-O2 -std=gnu89 -DNO_SCANSTACK -isysroot {sdk} -Wno-implicit-int -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-function-pointer-types'
    for pattern in ('inc/OBJ/*/avoffset', 'inc/OBJ/*/avoffset.o'):
        for generated in source.glob(pattern):
            generated.unlink()
    command = [smake, 'CC=clang', f'CFLAGS={flags}', f'LDOPTX=-isysroot {sdk}', f'INS_BASE={output}', f'INS_RBASE={output}']
    with (output / 'build.log').open('w') as log:
        subprocess.run(command, cwd=source, stdout=log, stderr=subprocess.STDOUT, check=True)
    binaries = list((source / 'cdrecord/OBJ').glob('*/cdrecord'))
    if len(binaries) != 1:
        raise RuntimeError(f'Cannot uniquely locate built cdrecord: {binaries}')
    name = 'appendcd-vat-cdrecord' if args.vat else ('appendcd-initial-cdrecord' if args.initial else 'appendcd-cdrecord')
    target = output / name
    shutil.copyfile(binaries[0], target)
    target.chmod(0o755)
    (output / 'provenance.json').write_text(json.dumps({'sourceUrl':URL,'sourceSha256':SHA256,'binarySha256':hashlib.sha256(target.read_bytes()).hexdigest(),'patchScript':'scripts/build-cdrecord.py','patchHeader':'backend/appendcd_backend.h','identityHeader':'backend/appendcd_identity.h','unofficial':True},indent=2)+'\n')
    print(target)
    subprocess.run([target, '-version'], check=True)
    if args.runtime_dir:
        runtime = args.runtime_dir.expanduser().resolve()
        runtime.mkdir(parents=True, exist_ok=True)
        installed = runtime / name
        if installed.exists() and not installed.is_file():
            raise RuntimeError('Runtime target is not a regular file')
        if installed.is_symlink():
            raise RuntimeError('Refusing to replace a runtime symlink')
        shutil.copyfile(target, installed)
        installed.chmod(0o755)
        shutil.copyfile(output / 'provenance.json', runtime / ('vat-provenance.json' if args.vat else ('initial-provenance.json' if args.initial else 'provenance.json')))
        print(f'Isolated runtime: {installed}')


if __name__ == '__main__':
    main()
