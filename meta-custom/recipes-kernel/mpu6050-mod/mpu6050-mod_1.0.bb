SUMMARY = "MPU6050 Linux Kernel Driver Module"
LICENSE = "MIT"

LIC_FILES_CHKSUM = "file://LICENSE;md5=3d972ba52f22ecb10fb364f844be4892"

inherit module

SRC_URI = "file://LICENSE \
           file://Makefile \
           file://mpu6050_driver.c"

S = "${WORKDIR}"

RPROVIDES:${PN} += "kernel-module-mpu6050-mod"

