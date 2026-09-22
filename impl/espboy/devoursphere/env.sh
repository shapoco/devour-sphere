# Sourced by build.sh / flash.sh / monitor.sh: the SDK, its toolchain and
# its Python environment on the PATH. IDF_PATH wins if it is already set.
export IDF_PATH="${IDF_PATH:-${HOME}/esp/ESP8266_RTOS_SDK}"
DS_ESPRESSIF="${IDF_TOOLS_PATH:-${HOME}/.espressif}"
DS_XTENSA="$(ls -d "${DS_ESPRESSIF}"/tools/xtensa-lx106-elf/*/xtensa-lx106-elf/bin 2>/dev/null | tail -1)"
DS_PYENV="$(ls -d "${DS_ESPRESSIF}"/python_env/rtos*_env/bin 2>/dev/null | tail -1)"
if [ -z "${DS_XTENSA}" ] || [ -z "${DS_PYENV}" ]; then
  echo "toolchain or Python environment not found under ${DS_ESPRESSIF} (see impl/espboy/SPEC.md)" >&2
  exit 1
fi
export PATH="${DS_PYENV}:${DS_XTENSA}:${IDF_PATH}/tools:${PATH}"
