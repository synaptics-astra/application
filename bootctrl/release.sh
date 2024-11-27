#!/bin/bash

echo "gpio release"

source build/header.rc
source build/install.rc

mod_dir=$(dirname "$0")
dst_dir=${CONFIG_SYNA_SDK_REL_PATH}/application/bootctrl
INSTALL_D ${mod_dir} ${CONFIG_SYNA_SDK_REL_PATH}/application

rm ${dst_dir}/release.sh

