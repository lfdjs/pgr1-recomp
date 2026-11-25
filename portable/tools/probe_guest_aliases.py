#!/usr/bin/env python3
"""Linux memfd alias/protection probe; no game memory or assets required."""
import ctypes
import errno
import mmap
import os
import sys


def main():
    if not sys.platform.startswith('linux') or not hasattr(os, 'memfd_create'):
        print('SKIP: Linux memfd required')
        return 77
    page = mmap.PAGESIZE
    libc = ctypes.CDLL(None, use_errno=True)
    protect = libc.mprotect
    protect.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int)
    protect.restype = ctypes.c_int
    rw = mmap.PROT_READ | mmap.PROT_WRITE
    fd = os.memfd_create('pgr1-alias-probe', os.MFD_CLOEXEC)
    try:
        os.ftruncate(fd, page * 2)
        with mmap.mmap(fd, page * 2, flags=mmap.MAP_SHARED, prot=rw) as a, \
             mmap.mmap(fd, page * 2, flags=mmap.MAP_SHARED, prot=rw) as b:
            addr = ctypes.addressof(ctypes.c_char.from_buffer(a))
            a[0] = 17
            assert b[0] == 17
            if protect(addr, page, mmap.PROT_READ):
                raise OSError(ctypes.get_errno(), 'mprotect read-only')
            try:
                # Protecting A does not protect B. / Proteger A nao protege B.
                b[0] = 34
                assert a[0] == 34
                b[page-1:page+1] = b'XY'
                assert a[page-1:page+1] == b'XY'
                print('PASS: other alias can mutate protected physical bytes, including a page boundary')
                # Kernel writes need their own coverage. / Escritas do kernel exigem cobertura.
                r, w = os.pipe()
                try:
                    os.write(w, b'Z')
                    with memoryview(a)[:1] as dst:
                        try:
                            os.readv(r, [dst])
                        except OSError as exc:
                            if exc.errno != errno.EFAULT:
                                raise
                        else:
                            raise AssertionError('protected destination accepted readv unexpectedly')
                    assert a[0] == 34
                    print('PASS: readv into read-only alias returns EFAULT, not a user-mode store')
                finally:
                    os.close(r)
                    os.close(w)
            finally:
                if protect(addr, page, rw):
                    raise OSError(ctypes.get_errno(), 'mprotect restore')
            a[0] = 51
            assert b[0] == 51
            print('PASS: restored alias remains shared')
    finally:
        os.close(fd)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
