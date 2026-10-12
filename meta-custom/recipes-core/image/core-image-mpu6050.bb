require recipes-core/images/core-image-minimal.bb

IMAGE_INSTALL:append = " \
    mpu6050-app \
    kernel-module-mpu6050-mod \
    packagegroup-core-ssh-dropbear \
    kernel-module-g-ether \
"
