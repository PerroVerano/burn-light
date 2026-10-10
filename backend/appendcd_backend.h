/*
 * appendcd local DVD backend additions, 2026-10-09.
 * Included by original CDDL-licensed cdrecord files. Upstream notices remain
 * intact. This is an unofficial experimental variant, with no upstream support.
 */
#ifndef APPENDCD_BACKEND_H
#define APPENDCD_BACKEND_H
extern int appendcd_probe, appendcd_expected_nwa, appendcd_reuse, appendcd_finish;
extern int appendcd_initial;
extern int appendcd_physical_track, appendcd_reserved_count, appendcd_checked_track;
extern int appendcd_select_track(SCSI *, long *);
extern void appendcd_check_reserved_size(SCSI *, int, long);
extern void appendcd_close_empty_tracks(SCSI *);
extern void appendcd_validate_finish(SCSI *);
extern void appendcd_validate_cd(SCSI *);

#ifdef APPENDCD_BACKEND_IMPLEMENTATION
int appendcd_probe;
int appendcd_expected_nwa = -1;
int appendcd_reuse;
int appendcd_finish;
int appendcd_initial;
int appendcd_physical_track;
int appendcd_reserved_count;
int appendcd_checked_track;

static void appendcd_read_disk(SCSI *scgp, struct disk_info *di, int *first, int *last) {
    if (get_curprofile(scgp) != 0x11)
        comerrno(EX_BAD, "appendcd backend requires DVD-R sequential recording.\n");
    fillbytes((caddr_t)di, sizeof (*di), '\0');
    if (get_diskinfo(scgp, di, sizeof (*di)) < 0)
        comerrno(EX_BAD, "appendcd cannot read physical disk information.\n");
    *first = di->first_track_ls + di->first_track_ls_msb * 256;
    *last = di->last_track_ls + di->last_track_ls_msb * 256;
    if (*first < 1 || *last < *first || *last > 99 ||
        (appendcd_initial ? (di->disk_status != 0 || di->sess_status != 0 || *first != 1 || *last != 1 || appendcd_reuse || appendcd_finish) : di->disk_status != 1))
        comerrno(EX_BAD, "appendcd physical Track range or appendability is unsupported.\n");
}

static void appendcd_read_track(SCSI *scgp, int t, struct rzone_info *rz) {
    fillbytes((caddr_t)rz, sizeof (*rz), '\0');
    if (get_trackinfo(scgp, (caddr_t)rz, TI_TYPE_TRACK, t, sizeof (*rz)) < 0)
        comerrno(EX_BAD, "appendcd cannot read physical Track %d.\n", t);
    if (a_to_2_byte(rz->data_len) + 2 < 36 ||
        rz->rzone_num_lsb + rz->rzone_num_msb * 256 != t)
        comerrno(EX_BAD, "appendcd Track information is truncated or mismatched.\n");
}

static int appendcd_empty_reserved(struct rzone_info *rz) {
    return rz->rt && rz->blank && !rz->damage && !rz->lra_v &&
        rz->incremental && rz->datamode == 1 && rz->trackmode == 4 &&
        a_to_4_byte(rz->free_blocks) == a_to_4_byte(rz->rzone_size) &&
        a_to_4_byte(rz->rzone_size) > 0 &&
        a_to_4_byte(rz->rzone_size) <= 4096 &&
        a_to_4_byte(rz->rzone_size) % 16 == 0;
}

int appendcd_select_track(SCSI *scgp, long *address) {
    struct disk_info di;
    struct rzone_info rz;
    int first, last, chosen, t;
    long start, previous_end = -1;
    appendcd_read_disk(scgp, &di, &first, &last);
    if (appendcd_initial ? appendcd_expected_nwa != 0 : appendcd_expected_nwa <= 0)
        comerrno(EX_BAD, "appendcd requires an explicit positive expected NWA.\n");
    chosen = last;
    if (appendcd_reuse) {
        if (di.sess_status != 1 || appendcd_physical_track != first ||
            last - first != appendcd_reserved_count ||
            appendcd_reserved_count < 1 || appendcd_reserved_count > 2)
            comerrno(EX_BAD, "appendcd recovery requires one or two exact empty reserved Tracks.\n");
        chosen = first;
        for (t = first; t < last; ++t) {
            appendcd_read_track(scgp, t, &rz);
            start = a_to_4_byte(rz.rzone_start);
            if (!appendcd_empty_reserved(&rz) || start <= previous_end ||
                (rz.nwa_v && a_to_4_byte(rz.next_recordable_addr) != start))
                comerrno(EX_BAD, "appendcd recovery refuses nonempty/damaged/inconsistent Track %d.\n", t);
            previous_end = start + a_to_4_byte(rz.rzone_size) - 1;
        }
    } else if (first != last || di.sess_status != 0 || appendcd_physical_track)
        comerrno(EX_BAD, "appendcd found an unfinished Session; use explicit recovery, not burn.\n");
    appendcd_read_track(scgp, last, &rz);
    if (!rz.blank || rz.rt || rz.damage || rz.lra_v || !rz.nwa_v ||
        a_to_4_byte(rz.free_blocks) <= 0 ||
        a_to_4_byte(rz.next_recordable_addr) < a_to_4_byte(rz.rzone_start) ||
        a_to_4_byte(rz.next_recordable_addr) <= previous_end)
        comerrno(EX_BAD, "appendcd invisible Track has no valid writable boundary.\n");
    appendcd_read_track(scgp, chosen, &rz);
    start = appendcd_reuse ? a_to_4_byte(rz.rzone_start) : a_to_4_byte(rz.next_recordable_addr);
    if (appendcd_initial && (chosen != 1 || a_to_4_byte(rz.rzone_start) != 0))
        comerrno(EX_BAD, "appendcd initial DVD-R must be blank at Track 1 LBA 0.\n");
    if (start != appendcd_expected_nwa)
        comerrno(EX_BAD, "appendcd expected NWA %d differs from physical address %ld.\n", appendcd_expected_nwa, start);
    *address = start;
    printf("APPENDCD_DVD_TARGET track=%d start=%ld recovery=%d\n", chosen, start, appendcd_reuse);
    return chosen;
}

void appendcd_check_reserved_size(SCSI *scgp, int t, long sectors) {
    struct rzone_info rz;
    long address;
    if (appendcd_select_track(scgp, &address) != t)
        comerrno(EX_BAD, "appendcd recovery Track changed before write.\n");
    appendcd_read_track(scgp, t, &rz);
    if (!appendcd_empty_reserved(&rz) || sectors != a_to_4_byte(rz.rzone_size))
        comerrno(EX_BAD, "appendcd recovery image must exactly fill the reserved Track.\n");
}

void appendcd_close_empty_tracks(SCSI *scgp) {
    struct disk_info di;
    struct rzone_info rz;
    int first, last, t;
    appendcd_read_disk(scgp, &di, &first, &last);
    if (first != appendcd_physical_track || last - first != appendcd_reserved_count)
        comerrno(EX_BAD, "appendcd physical recovery geometry changed.\n");
    appendcd_read_track(scgp, first, &rz);
    if (rz.rt || rz.blank || rz.damage || rz.nwa_v || !rz.lra_v || a_to_4_byte(rz.free_blocks) != 0 ||
        a_to_4_byte(rz.rzone_start) != appendcd_expected_nwa ||
        a_to_4_byte(rz.last_recorded_addr) != a_to_4_byte(rz.rzone_start) + a_to_4_byte(rz.rzone_size) - 1)
        comerrno(EX_BAD, "appendcd refuses closure before recovery image is fully recorded.\n");
    for (t = first + 1; t < last; ++t) {
        appendcd_read_track(scgp, t, &rz);
        if (!rz.rt && !rz.blank && !rz.damage && !rz.nwa_v && !rz.lra_v &&
            a_to_4_byte(rz.free_blocks) == 0 && a_to_4_byte(rz.last_recorded_addr) == 0) {
            /* This drive keeps LRA invalid when CLOSE TRACK pads an entirely
               empty RZone. The CLI separately reads every byte as zero before
               permitting an explicit finish after an interrupted closure. */
            printf("APPENDCD_DVD_ALREADY_PADDED track=%d\n", t);
            continue;
        }
        if (!appendcd_empty_reserved(&rz))
            comerrno(EX_BAD, "appendcd refuses padding a nonempty/damaged Track.\n");
        printf("APPENDCD_DVD_PAD_EMPTY track=%d start=%ld blocks=%ld\n", t,
            a_to_4_byte(rz.rzone_start), a_to_4_byte(rz.rzone_size));
        /* Existing cdrecord executor: close Track pads ONLY the verified empty
           reserved RZone. Never blank the disc or touch completed Tracks. */
        if (scsi_close_tr_session(scgp, CL_TYPE_TRACK, t, FALSE) < 0)
            comerrno(EX_BAD, "appendcd could not close the empty reserved Track.\n");
        appendcd_read_track(scgp, t, &rz);
        if (rz.rt || rz.blank || rz.damage || rz.nwa_v || a_to_4_byte(rz.free_blocks) != 0 ||
            (rz.lra_v && a_to_4_byte(rz.last_recorded_addr) != a_to_4_byte(rz.rzone_start) + a_to_4_byte(rz.rzone_size) - 1) ||
            (!rz.lra_v && a_to_4_byte(rz.last_recorded_addr) != 0))
            comerrno(EX_BAD, "appendcd empty Track padding did not complete.\n");
    }
}

void appendcd_validate_finish(SCSI *scgp) {
    struct disk_info di;
    struct rzone_info rz;
    int first, last, t;
    appendcd_read_disk(scgp, &di, &first, &last);
    if (di.sess_status != 1 || !appendcd_reuse || first != appendcd_physical_track ||
        last - first != appendcd_reserved_count || appendcd_reserved_count < 1 || appendcd_reserved_count > 2)
        comerrno(EX_BAD, "appendcd refuses unverified Session finalization geometry.\n");
    for (t = first; t < last; ++t) {
        appendcd_read_track(scgp, t, &rz);
        if (rz.rt || rz.blank || rz.damage || rz.nwa_v || a_to_4_byte(rz.free_blocks) != 0 ||
            (!rz.lra_v && (t == first || a_to_4_byte(rz.last_recorded_addr) != 0)) ||
            (rz.lra_v && (t != first || a_to_4_byte(rz.last_recorded_addr) != a_to_4_byte(rz.rzone_start) + a_to_4_byte(rz.rzone_size) - 1)) ||
            (t == first && a_to_4_byte(rz.rzone_start) != appendcd_expected_nwa))
            comerrno(EX_BAD, "appendcd finalization found an incomplete/damaged/non-padding Track.\n");
    }
    appendcd_read_track(scgp, last, &rz);
    if (!rz.blank || rz.rt || rz.damage || rz.lra_v || !rz.nwa_v || a_to_4_byte(rz.free_blocks) <= 0)
        comerrno(EX_BAD, "appendcd finalization has no writable tail.\n");
    appendcd_checked_track = first;
}

static void appendcd_read_cd(SCSI *scgp, struct disk_info *di, struct track_info *ti) {
    if (get_curprofile(scgp) != 0x09 || appendcd_reuse || appendcd_finish)
        comerrno(EX_BAD, "appendcd CD path requires CD-R, without DVD recovery options.\n");
    fillbytes((caddr_t)di, sizeof (*di), '\0');
    fillbytes((caddr_t)ti, sizeof (*ti), '\0');
    if (get_diskinfo(scgp, di, sizeof (*di)) < 0 ||
        (appendcd_initial ? (di->disk_status != 0 || di->sess_status != 0 || di->first_track_ls != 1 || di->last_track_ls != 1) :
         (di->disk_status != 1 || (di->sess_status != 0 && di->sess_status != 2))))
        comerrno(EX_BAD, "appendcd CD-R has no complete/empty appendable Session boundary.\n");
    /* The official MMC CD driver queries the invisible track as 0xFF. */
    if (get_trackinfo(scgp, (caddr_t)ti, TI_TYPE_TRACK, 0xFF, sizeof (*ti)) < 0 ||
        a_to_2_byte(ti->data_len) + 2 < 32 || !ti->nwa_valid || !ti->blank ||
        ti->rt || ti->damage || ti->packet || ti->fp ||
        (appendcd_initial ? (a_to_4_byte(ti->next_writable_addr) != 0 || a_to_4_byte(ti->track_start) != 0) :
         a_to_4_byte(ti->next_writable_addr) <= 0) || a_to_4_byte(ti->free_blocks) <= 0)
        comerrno(EX_BAD, "appendcd CD-R invisible Track has no valid streaming NWA.\n");
}

void appendcd_validate_cd(SCSI *scgp) {
    struct disk_info di;
    struct track_info ti;
    long nwa;
    appendcd_read_cd(scgp, &di, &ti);
    nwa = a_to_4_byte(ti.next_writable_addr);
    if ((appendcd_initial ? appendcd_expected_nwa != 0 : appendcd_expected_nwa <= 0) || nwa != appendcd_expected_nwa)
        comerrno(EX_BAD, "appendcd CD-R expected NWA differs from physical writable address.\n");
    printf("APPENDCD_CD_TARGET track=%d start=%ld\n", ti.track_number, nwa);
}

static void appendcd_dump_tracks(SCSI *scgp) {
    struct disk_info di;
    struct rzone_info rz;
    int first, last, t;
    long address;
    if (get_curprofile(scgp) == 0x09) {
        struct track_info ti;
        appendcd_read_cd(scgp, &di, &ti);
        printf("APPENDCD_CD_DISK disk_status=%d session_status=%d first=%d last=%d\n",
            di.disk_status, di.sess_status, di.first_track_ls, di.last_track_ls);
        printf("APPENDCD_CD_TRACK number=%d session=%d reserved=%d blank=%d damage=%d nwa_valid=%d start=%ld nwa=%ld free=%ld size=%ld last_recorded=0 lra_valid=0 incremental=%d datamode=%d trackmode=%d\n",
            ti.track_number, ti.session_number, ti.rt, ti.blank, ti.damage, ti.nwa_valid,
            a_to_4_byte(ti.track_start), a_to_4_byte(ti.next_writable_addr),
            a_to_4_byte(ti.free_blocks), a_to_4_byte(ti.track_size), ti.packet, ti.data_mode, ti.track_mode);
        if (appendcd_expected_nwa >= 0) appendcd_validate_cd(scgp);
        return;
    }
    appendcd_read_disk(scgp, &di, &first, &last);
    printf("APPENDCD_DVD_DISK disk_status=%d session_status=%d first=%d last=%d\n",
        di.disk_status, di.sess_status, first, last);
    for (t = first; t <= last; ++t) {
        appendcd_read_track(scgp, t, &rz);
        printf("APPENDCD_DVD_TRACK number=%d session=%d reserved=%d blank=%d damage=%d nwa_valid=%d start=%ld nwa=%ld free=%ld size=%ld last_recorded=%ld lra_valid=%d incremental=%d datamode=%d trackmode=%d\n",
            rz.rzone_num_lsb + rz.rzone_num_msb * 256,
            rz.border_num_lsb + rz.border_num_msb * 256,
            rz.rt, rz.blank, rz.damage, rz.nwa_v,
            a_to_4_byte(rz.rzone_start), a_to_4_byte(rz.next_recordable_addr),
            a_to_4_byte(rz.free_blocks), a_to_4_byte(rz.rzone_size),
            a_to_4_byte(rz.last_recorded_addr), rz.lra_v,
            rz.incremental, rz.datamode, rz.trackmode);
    }
    if (appendcd_expected_nwa >= 0)
        appendcd_select_track(scgp, &address);
}
#endif
#endif
