#!/usr/bin/env bash
# Build the vendored Open LabBench single binary (psu_app), which hosts the
# DMM drivers + the 320x170 dmm-compact view. libusb is auto-detected by the
# olb Makefile via pkg-config and enables the userspace USB-TMC backend.
set -euo pipefail
make -C olb app
