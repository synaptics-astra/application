#!/bin/bash

source build/header.rc
source build/install.rc
source build/module/toolchain/${CONFIG_TOOLCHAIN_APPLICATION}.rc

# only for linux runtime
if [ "is${CONFIG_APP_PM}" = "is" ];then
    exit 0
fi

mod_dir=${topdir}/application/powermanaged

[ ${clean} -eq 1 ] && make clean -C ${mod_dir} && exit 0

cflags=${CONFIG_TOOLCHAIN_APPLICATION_CFLAG}
cflags="${cflags} --sysroot=${CONFIG_SYNA_SDK_OUT_TARGET_PATH}/build_sysroot"
cc_name="${CONFIG_TOOLCHAIN_APPLICATION}gcc"

if [ "${CONFIG_TOOLCHAIN_APPLICATION_CC}" = "clang" ]; then
    clangcflag=${CONFIG_TOOLCHAIN_CLANG_CFLAG}
    clangldflag=${CONFIG_TOOLCHAIN_CLANG_LDFLAG}
    cflags="${clangcflag} ${cflags}"
    ldflags="${clangcflag} ${clangldflag} ${ldflags}"
    cc_name="clang"
fi

make all -C ${mod_dir} CC="${cc_name}" E_CFLAG="${cflags}" E_LDFLAG="${CONFIG_TOOLCHAIN_APPLICATION_LDFLAG}"


sysroot=${CONFIG_SYNA_SDK_OUT_TARGET_PATH}/${CONFIG_SYNA_SDK_OUT_SYSYROOT}
rootfs=${CONFIG_SYNA_SDK_OUT_TARGET_PATH}/${CONFIG_SYNA_SDK_OUT_ROOTFS}
INSTALL_F ${mod_dir}/inc/pm_client.h ${sysroot}/usr/include/
INSTALL_F ${mod_dir}/lib/libpm_client.so ${sysroot}/usr/lib/
INSTALL_F ${mod_dir}/lib/libpm_client.so ${rootfs}/usr/lib/
INSTALL_D ${mod_dir}/bin/ ${sysroot}/usr/bin/
INSTALL_D ${mod_dir}/bin/ ${rootfs}/usr/bin
