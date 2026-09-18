#!/bin/bash
# Build the esp32s3 target with the Xiamocon SDK (XMC_REPO_PATH: the SDK repo).
# Sourced here, in a child shell, so the SDK's environment stays out of the caller's.

set -eu

source "${XMC_REPO_PATH:?set XMC_REPO_PATH to the Xiamocon SDK repository}/setup.shrc"
xmc build -p esp32s3_pio_arduino
