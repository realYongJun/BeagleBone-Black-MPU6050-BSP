SUMMARY = "MPU6050 User Application"
LICENSE = "MIT"

LIC_FILES_CHKSUM = "file://LICENSE;md5=3d972ba52f22ecb10fb364f844be4892"

SRC_URI = "file://Makefile \
           file://mpu6050_user.c \
           file://LICENSE"

S = "${WORKDIR}"

do_compile() {
    oe_runmake
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 mpu6050_user ${D}${bindir}
}
