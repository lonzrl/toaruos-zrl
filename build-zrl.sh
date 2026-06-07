#!/bin/bash
set -e
echo "========================================"
echo "  ZRL OS Build Script (WSL Native)"
echo "========================================"
echo

cd /mnt/d/Projects/touros/toaruos-master

echo "[1/6] Checking submodules..."
if [ ! -f kuroko/src/kuroko.c ]; then
    echo "  Cloning kuroko..."
    GIT_SSL_NO_VERIFY=1 git clone --depth 1 https://github.com/kuroko-lang/kuroko.git kuroko
fi
if [ ! -f bim/bim.c ]; then
    echo "  Cloning bim..."
    GIT_SSL_NO_VERIFY=1 git clone --depth 1 https://github.com/klange/bim.git bim
fi
echo "  kuroko/bim OK"
echo

echo "[2/6] Checking toolchain sources..."
# build-toolchain.sh expects these at PROJECT_ROOT level
if [ ! -d binutils-gdb ] || [ ! -f binutils-gdb/configure ]; then
    echo "  Cloning binutils-gdb (forked, ~200MB)..."
    rm -rf binutils-gdb
    GIT_SSL_NO_VERIFY=1 git clone --depth 1 https://github.com/klange/binutils-gdb.git binutils-gdb
else
    echo "  binutils-gdb: OK"
fi
if [ ! -d gcc ] || [ ! -f gcc/configure ]; then
    echo "  Cloning gcc (forked, ~300MB)..."
    rm -rf gcc
    GIT_SSL_NO_VERIFY=1 git clone --depth 1 https://github.com/klange/gcc.git gcc
else
    echo "  gcc: OK"
fi
echo "  Toolchain sources ready"
echo

echo "[3/6] Building cross-toolchain (~10-20 min)..."
bash util/build-toolchain.sh
echo "  Toolchain built!"
echo

echo "[4/6] Building Kuroko interpreter..."
make util/local/bin/kuroko
echo

echo "[5/6] Building libc..."
make base/lib/libc.so
echo

echo "[6/6] Building full system (kernel + apps + iso)..."
make -j$(nproc)
echo

echo "========================================"
echo "  BUILD RESULTS"
echo "========================================"
if [ -f image.iso ]; then
    echo "  SUCCESS! image.iso created:"
    ls -lh image.iso
    echo
    echo "  Run with:"
    echo "  qemu-system-x86_64 -m 1G -cdrom image.iso -enable-kvm"
elif [ -f misaka-kernel ]; then
    echo "  Kernel built but ISO missing."
    echo "  Try running: make image.iso"
else
    echo "  BUILD FAILED"
    exit 1
fi
