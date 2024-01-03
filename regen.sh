#!/bin/bash

TC_PATH="$HOME/clang-r510928/bin/"

BUILD_ENV="CC=$(echo $TC_PATH)clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=$(echo $TC_PATH)aarch64-linux-gnu-"

KERNEL_MAKE_ENV="DTC_EXT=$(pwd)/tools/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y"

make O=out ARCH=arm64 $BUILD_ENV oldconfig

cp out/.config arch/arm64/configs/r8q_defconfig
