#!/bin/bash
# Builds and runs every contract test in the PoC, asserting exact check counts.
# This is the fork's fast regression suite: it needs only Qt6 + Rust + CMake,
# no tdesktop build, no Docker. Runs in seconds.
set -euo pipefail
cd "$(dirname "$0")"

EXPECT_POC_CHECKS=34
EXPECT_S1_CHECKS=10
EXPECT_CPP_PLUGIN_CHECKS=4

echo "===== 1/4 host ====="
cmake -S host -B host/build -DCMAKE_BUILD_TYPE=Release
cmake --build host/build -j"$(nproc)"

echo "===== 2/4 rust plugin ====="
cmake -S rustplugin -B rustplugin/build -DCMAKE_BUILD_TYPE=Release
cmake --build rustplugin/build -j"$(nproc)"

echo "===== 3/4 cpp plugin ====="
# Qt headers live in different places per distro (Arch: /usr/include/qt6,
# Debian/Ubuntu: /usr/include/<triplet>/qt6). Ask Qt rather than guess.
QT_INC=$(qmake6 -query QT_INSTALL_HEADERS 2>/dev/null || true)
if [ ! -d "$QT_INC/QtCore" ]; then
	for candidate in /usr/include/qt6 /usr/include/*/qt6; do
		[ -d "$candidate/QtCore" ] && { QT_INC=$candidate; break; }
	done
fi
[ -d "$QT_INC/QtCore" ] || { echo "Qt6 headers not found"; exit 1; }
g++ -std=c++20 -O2 -fPIC -shared \
	-I. -I"$QT_INC" -I"$QT_INC/QtCore" \
	-o cppplugin/libcpp_plugin.so cppplugin/main.cpp

echo "===== 4/4 run ====="
./host/build/host \
	"$(realpath rustplugin/build/libtdp_qt_plugin.so)" \
	"$(realpath cppplugin/libcpp_plugin.so)" \
	| tee run.log

total=$(sed -n 's/^\([0-9]*\) checks, .*/\1/p' run.log | tail -1)
failures=$(sed -n 's/^[0-9]* checks, \([0-9]*\) failures/\1/p' run.log | tail -1)
echo
echo "PoC: $total checks, $failures failures (expected $EXPECT_POC_CHECKS/0)"
[ "$total" = "$EXPECT_POC_CHECKS" ] || { echo "FAIL: check count drifted"; exit 1; }
[ "$failures" = "0" ] || { echo "FAIL: $failures failures"; exit 1; }

echo
echo "===== S1: static-Qt loader probe ====="
( cd s1_static && ./build.sh ) | tee s1_static/run.log

s1_total=$(grep -c '^  PASS\|^  FAIL' s1_static/run.log)
s1_fail=$(grep -c '^  FAIL' s1_static/run.log || true)
echo
echo "S1: $s1_total checks, $s1_fail failures (expected $EXPECT_S1_CHECKS/0)"
[ "$s1_total" = "$EXPECT_S1_CHECKS" ] || { echo "FAIL: check count drifted"; exit 1; }
[ "$s1_fail" = "0" ] || { echo "FAIL: $s1_fail failures"; exit 1; }

echo
echo "ALL SUITES PASSED"
