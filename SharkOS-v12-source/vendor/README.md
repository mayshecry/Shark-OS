# Offline compiler sources

`tinycc.tar.bz2` is the unmodified source archive for [TinyCC 0.9.27, revision 62c30a4a](https://deb.debian.org/debian/pool/main/t/tcc/tcc_0.9.27+git20200814.62c30a4a.orig.tar.bz2). `musl.tar.gz` is the unmodified [musl 1.2.5 release](https://musl.libc.org/releases/musl-1.2.5.tar.gz). Copyright notices remain in the upstream sources. See `licenses/` and the individual files for license terms. The archives are source, not Linux executables.

`tools/build_native_cc.sh` verifies SHA-256 before extraction. The script disables TinyCC's host runner and thread locks, fixes the version tuple, compiles with the x86-64 no-red-zone ABI, and privately links only reachable library routines. SharkOS supplies file access and storage. Rebuilding needs no network and uses no Linux runtime in the guest. Delete `cc-build/` to rebuild the port and its libraries from scratch.
