#!/bin/bash

export PATH="/usr/lib/ccache:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

LLVM_PATH="/usr/bin"

read -p "Digite um nome para o kernel: " KERNEL_NAME

if [ -z "$KERNEL_NAME" ]; then
    echo "Nome do kernel não pode ser vazio. Saindo."
    exit 1
fi

TC_PATH="/usr/bin/"
GCC_PATH="/usr/bin/"
BUILD_ENV="CC=${TC_PATH}clang \
LD=${TC_PATH}ld.lld \
AR=${TC_PATH}llvm-ar \
NM=${TC_PATH}llvm-nm \
OBJCOPY=${TC_PATH}llvm-objcopy \
OBJDUMP=${TC_PATH}llvm-objdump \
STRIP=${TC_PATH}llvm-strip \
CROSS_COMPILE=${GCC_PATH}aarch64-linux-gnu- \
CROSS_COMPILE_ARM32=${GCC_PATH}arm-linux-gnueabi- \
LLVM=1 LLVM_IAS=1 PATH=$LLVM_PATH:$PATH"  

KERNEL_MAKE_ENV="DTC_EXT=$(pwd)/tools/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y"

mkdir -p out
make O=out ARCH=arm64 $BUILD_ENV r8q_defconfig
make O=out ARCH=arm64 $BUILD_ENV prepare
make O=out ARCH=arm64 $BUILD_ENV vdso_prepare

DATE_START=$(date +"%s")

make -j$(nproc --all) O=out ARCH=arm64 $KERNEL_MAKE_ENV $BUILD_ENV Image.gz
make -j$(nproc --all) O=out ARCH=arm64 $KERNEL_MAKE_ENV $BUILD_ENV dtbs

DTB_OUT="out/arch/arm64/boot/dts/vendor/qcom"
IMAGE="out/arch/arm64/boot/Image.gz"

cat $DTB_OUT/*.dtb > AnyKernel3/dtb

DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))
echo "Tempo de compilação: $(($DIFF / 60)) minutos(s) and $(($DIFF % 60)) segundos."

cp $IMAGE AnyKernel3/Image.gz
cd AnyKernel3
rm -f *.zip # O -f impede o script de travar caso não existam zips antigos
zip -r9 ${KERNEL_NAME}.zip .
