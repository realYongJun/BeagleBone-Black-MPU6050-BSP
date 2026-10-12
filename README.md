# BeagleBone Black BSP Development

Yocto-based Linux BSP development project for BeagleBone Black Rev C.

## Development Environment

- Hardware: BeagleBone Black Rev C
- Yocto Project: Kirkstone
- Linux Kernel: TI Linux 5.10.168
- Sensor: MPU-6050 (I2C)

## Implementation

- Linux Character Device Driver
- Device Tree Integration
- Yocto Custom Layer
- Custom Root Filesystem Image

## Build

Build configuration: `manifests/kirkstone.yml`

Note: Independent build reproducibility has not yet been verified.
