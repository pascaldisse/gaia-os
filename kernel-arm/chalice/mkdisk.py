#!/usr/bin/env python3
# CHALICE SLICE3 Atom 3C-1 -- host-side RedSea-spirit FS populate.
#
# Speaks the exact on-disk layout kernel-arm/zoneA/fs.c implements (contiguous
# files, flat 64-entry directory, bump-allocated data), so the 3 stock DLLs
# can be baked into disk.img at BUILD time (no QEMU boot needed -- fs.c's own
# format/write path only exists as interactive shell verbs today, and this
# atom must not require booting to populate the disk). Mirrors fs.c's
# FsFormat/FsWrite semantics field-for-field:
#   CSuper   {u64 magic, u64 total_secs, u64 next_free, pad to 512}  @ sector 1
#   CDirEnt  {u16 attr, char name[38], u64 start, u64 size, u64 mtime} (64B,
#            8 entries/sector) @ sectors 8..15 (DIR_SECS=8)
#   data     bump-allocated from sector 16 (SEC_DATA) upward
# Idempotent: if the disk is already formatted (valid magic), reuses the
# existing superblock/directory rather than reformatting -- "keep existing
# contents" (only total_secs is bumped upward if the disk grew). Existing
# entries with the same name are overwritten in place if their current
# allocation already fits the new content, else reallocated at the bump
# pointer (name_eq semantics: exact byte match, so callers must pass the
# exact lowercase name fs.c's own runtime lookups will use).
import struct
import sys
import os

SEC_SIZE = 512
SEC_SUPER = 1
DIR_SECS = 8
SEC_DIR = 8
SEC_DATA = 16
FS_MAGIC = 0x41455344455230

SUPER_FMT = "<QQQ"           # magic, total_secs, next_free (rest is zero pad)
DIRENT_FMT = "<H38sQQQ"      # attr, name[38], start, size, mtime
DIRENT_SIZE = struct.calcsize(DIRENT_FMT)
assert DIRENT_SIZE == 64, DIRENT_SIZE
ENTRIES_PER_SEC = SEC_SIZE // DIRENT_SIZE
assert ENTRIES_PER_SEC == 8


def die(msg):
    sys.stderr.write("mkdisk: %s\n" % msg)
    sys.exit(1)


def read_sector(f, sec):
    f.seek(sec * SEC_SIZE)
    d = f.read(SEC_SIZE)
    if len(d) < SEC_SIZE:
        d = d + b"\x00" * (SEC_SIZE - len(d))
    return d


def write_sector(f, sec, data):
    assert len(data) == SEC_SIZE
    f.seek(sec * SEC_SIZE)
    f.write(data)


def main():
    if len(sys.argv) < 3:
        die("usage: mkdisk.py <disk.img> <min_size_bytes> [name=path ...]")
    disk_path = sys.argv[1]
    min_size = int(sys.argv[2])
    pairs = []
    for arg in sys.argv[3:]:
        if "=" not in arg:
            die("bad arg (want name=path): %s" % arg)
        name, path = arg.split("=", 1)
        pairs.append((name, path))

    # ---- grow (never shrink) -- preserves existing bytes, per spec "keep
    # existing contents". truncate() zero-fills any newly extended region. ----
    if not os.path.exists(disk_path):
        with open(disk_path, "wb") as f:
            f.truncate(min_size)
        print("mkdisk: created %s (%d bytes)" % (disk_path, min_size))
    else:
        cur = os.path.getsize(disk_path)
        if cur < min_size:
            with open(disk_path, "r+b") as f:
                f.truncate(min_size)
            print("mkdisk: grew %s %d -> %d bytes" % (disk_path, cur, min_size))
        else:
            min_size = cur   # never shrink total_secs below what's already there

    total_secs = min_size // SEC_SIZE

    with open(disk_path, "r+b") as f:
        super_sec = read_sector(f, SEC_SUPER)
        magic, cur_total_secs, next_free = struct.unpack_from(SUPER_FMT, super_sec, 0)

        if magic != FS_MAGIC:
            print("mkdisk: no valid FS magic found -- formatting fresh")
            magic = FS_MAGIC
            next_free = SEC_DATA
            for s in range(DIR_SECS):
                write_sector(f, SEC_DIR + s, b"\x00" * SEC_SIZE)
        else:
            print("mkdisk: existing FS found (next_free=%d) -- preserving" % next_free)
            next_free = max(next_free, SEC_DATA)

        total_secs = max(total_secs, cur_total_secs)
        super_sec = struct.pack(SUPER_FMT, magic, total_secs, next_free) + b"\x00" * (SEC_SIZE - struct.calcsize(SUPER_FMT))
        write_sector(f, SEC_SUPER, super_sec)

        # ---- load directory into memory: list of (sector, idx, attr, name, start, size, mtime) ----
        dir_entries = []
        for s in range(DIR_SECS):
            sec_data = read_sector(f, SEC_DIR + s)
            for i in range(ENTRIES_PER_SEC):
                off = i * DIRENT_SIZE
                attr, rawname, start, size, mtime = struct.unpack_from(DIRENT_FMT, sec_data, off)
                dir_entries.append([SEC_DIR + s, i, attr, rawname, start, size, mtime])

        def find_free_slot():
            for e in dir_entries:
                if e[2] == 0:
                    return e
            return None

        def find_by_name(name_bytes):
            for e in dir_entries:
                if e[2] == 1 and e[3].split(b"\x00", 1)[0] == name_bytes.split(b"\x00", 1)[0]:
                    return e
            return None

        for name, path in pairs:
            if not os.path.exists(path):
                die("source file not found: %s" % path)
            with open(path, "rb") as sf:
                data = sf.read()
            length = len(data)
            need_secs = (length + SEC_SIZE - 1) // SEC_SIZE

            name_bytes = name.encode("ascii")
            if len(name_bytes) > 38:
                die("name too long (max 38): %s" % name)
            name_field = name_bytes + b"\x00" * (38 - len(name_bytes))

            existing = find_by_name(name_bytes)
            if existing:
                have_secs = (existing[5] + SEC_SIZE - 1) // SEC_SIZE
                if need_secs <= have_secs:
                    start = existing[4]
                    entry = existing
                else:
                    start = next_free
                    next_free += need_secs
                    entry = existing
            else:
                slot = find_free_slot()
                if not slot:
                    die("directory full (64 entries)")
                start = next_free
                next_free += need_secs
                entry = slot

            if next_free > total_secs:
                die("disk too small: need %d secs, have %d (grow min_size_bytes)" % (next_free, total_secs))

            # write data, sector by sector, zero-padded tail
            for s in range(need_secs):
                chunk = data[s * SEC_SIZE:(s + 1) * SEC_SIZE]
                if len(chunk) < SEC_SIZE:
                    chunk = chunk + b"\x00" * (SEC_SIZE - len(chunk))
                write_sector(f, start + s, chunk)

            entry[2] = 1
            entry[3] = name_field
            entry[4] = start
            entry[5] = length
            entry[6] = 0

            print("mkdisk: %-16s %9d bytes @sec %d (%d secs)" % (name, length, start, need_secs))

        # ---- flush directory sectors back ----
        for s in range(DIR_SECS):
            sec_bytes = bytearray(SEC_SIZE)
            for i in range(ENTRIES_PER_SEC):
                e = dir_entries[s * ENTRIES_PER_SEC + i]
                off = i * DIRENT_SIZE
                struct.pack_into(DIRENT_FMT, sec_bytes, off, e[2], e[3], e[4], e[5], e[6])
            write_sector(f, SEC_DIR + s, bytes(sec_bytes))

        # ---- final superblock (next_free may have moved) ----
        super_sec = struct.pack(SUPER_FMT, magic, total_secs, next_free) + b"\x00" * (SEC_SIZE - struct.calcsize(SUPER_FMT))
        write_sector(f, SEC_SUPER, super_sec)

    print("mkdisk: OK -- total_secs=%d next_free=%d" % (total_secs, next_free))


if __name__ == "__main__":
    main()
