#!/bin/bash
# Serial log. ds::trace() writes the bring-up progress here.
set -eux
DS_IDF_PATH="${DS_IDF_PATH:-${HOME}/esp/5.5}"
cd "$(dirname "$0")"
source "${DS_IDF_PATH}/esp-idf/export.sh"
if [ $# -ge 1 ]; then idf.py -p "$1" monitor; else idf.py monitor; fi
