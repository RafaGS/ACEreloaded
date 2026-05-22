#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build-rp2350"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

unset CC CXX
export PICO_GCC_TRIPLE=arm-none-eabi
export PICO_TOOLCHAIN_PATH=/usr/bin

cmake -DFRANKLIN_TARGET_RP2350=ON -DPICO_COMPILER=pico_arm_cortex_m33_gcc "${ROOT_DIR}"
make -j"$(nproc)"

echo "Build RP2350 completado: ${BUILD_DIR}/systems/franklinACE/franklinACE.uf2"
