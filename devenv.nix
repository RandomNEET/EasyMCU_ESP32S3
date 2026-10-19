{ pkgs, inputs, ... }:

{
  packages = [
    pkgs.git
    inputs.esp-dev.packages.${pkgs.stdenv.hostPlatform.system}.esp-idf-esp32s3
  ];

  env.IDF_TARGET = "esp32s3";

  # Each board keeps its generated sdkconfig and build output separate.
  scripts.idf-board = {
    description = "ESP-IDF command for n16r8 (default), n8r8 or n8r2";
    exec = ''
      set -euo pipefail
      board="''${1:-n16r8}"
      if [ "$#" -gt 0 ]; then shift; fi
      case "$board" in
        n16r8|n8r8|n8r2) ;;
        *) echo "Usage: idf-board {n16r8|n8r8|n8r2} [idf.py arguments...]" >&2; exit 2 ;;
      esac
      cd "${toString ./.}"
      if [ "$#" -eq 0 ]; then set -- build; fi
      exec idf.py -B "build/$board" \
        -D "SDKCONFIG=$PWD/build/$board/sdkconfig" \
        -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.$board" "$@"
    '';
  };

  enterShell = ''
    echo "ESP32-S3 development environment"
    echo "Build: idf-board n16r8 build"
    echo "Flash: idf-board n16r8 -p /dev/ttyUSB0 flash monitor"
  '';

  enterTest = ''
    idf.py --version
    xtensa-esp-elf-gcc --version
    cmake --version
    ninja --version
    python -c 'import idf_component_manager, serial'
  '';
}
