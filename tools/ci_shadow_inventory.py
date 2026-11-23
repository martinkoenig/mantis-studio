"""Isolated optional-inventory reader; only regular, stable descriptors qualify.

This worker has no project executor or Git access. Its caller bounds process
lifetime and output. Filesystem calls may block in the OS; caller isolation, not
a Python clock check, provides the qualified timeout mechanism.
"""
import errno
import os
import stat
import sys

MAX_BYTES = 2_000_000


class ReadError(Exception):
    def __init__(self, status):
        self.status = status


def identity(metadata):
    return (metadata.st_dev, metadata.st_ino, metadata.st_mode, metadata.st_size,
            metadata.st_mtime_ns, metadata.st_ctime_ns)


def read_regular(path, max_bytes=MAX_BYTES):
    descriptor = None
    try:
        before = os.lstat(path)
        if not stat.S_ISREG(before.st_mode):
            raise ReadError(2)
        if before.st_size > max_bytes:
            raise ReadError(4)
        try:
            descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW | os.O_CLOEXEC)
        except OSError as exc:
            raise ReadError(2 if exc.errno == errno.ELOOP else 5) from None
        opened = os.fstat(descriptor)
        if not stat.S_ISREG(opened.st_mode):
            raise ReadError(2)
        if identity(before) != identity(opened):
            raise ReadError(3)
        data = bytearray()
        while True:
            block = os.read(descriptor, min(65536, max_bytes + 1 - len(data)))
            if not block:
                break
            data.extend(block)
            if len(data) > max_bytes:
                raise ReadError(4)
        after = os.fstat(descriptor)
        current = os.lstat(path)
        if (identity(before) != identity(after) or identity(after) != identity(current)
                or len(data) != after.st_size):
            raise ReadError(3)
        return bytes(data)
    except OSError:
        raise ReadError(5) from None
    finally:
        if descriptor is not None:
            os.close(descriptor)


def main():
    if len(sys.argv) != 2:
        return 5
    try:
        data = read_regular(sys.argv[1])
        sys.stdout.buffer.write(data)
    except ReadError as exc:
        return exc.status
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
