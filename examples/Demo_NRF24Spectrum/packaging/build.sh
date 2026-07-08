#!/usr/bin/env bash
set -euo pipefail
make
# Verify the ported scan math while we are here (no hardware needed).
./demo-nrf24spectrum --selftest
