#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
# A separate directory keeps parallel host test runs independent.
bin_dir=$(mktemp -d /tmp/easymcu-tests.XXXXXX)
cc -std=c11 -Wall -Wextra -Werror -Wno-sign-compare \
  -I tests/stubs -I tests/ecx_stubs -I main/ecx -I main/easycon -I main/hid \
  tests/test_script_status.c tests/legacy_ecx_stub.c main/easycon/easycon.c \
  -o "$bin_dir/status"
"$bin_dir/status"
cc -std=c11 -Wall -Wextra -Werror \
  -I tests/oled_stubs -I main/display -I main/easycon -I main/hid \
  tests/test_oled_spi.c main/display/oled_status.c -o "$bin_dir/oled"
"$bin_dir/oled"
cc -std=c11 -Wall -Wextra -Werror -Wno-sign-compare \
  -I tests/ecx_stubs -I main/ecx -I main/easycon -I main/hid \
  tests/test_ecx_runtime.c main/easycon/easycon.c main/ecx/ecx_runtime.c \
  main/ecx/vendor/ecs_vm.c -lm -o "$bin_dir/ecx"
"$bin_dir/ecx"
ECX_TEST_BINARY="$bin_dir/ecx" python3 tests/test_ecx_flash.py
