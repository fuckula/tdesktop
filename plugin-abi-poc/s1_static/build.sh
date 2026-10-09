#!/bin/bash
# S1 build: same plugin source, two builds, one difference — QT_STATICPLUGIN.
set -e
cd "$(dirname "$0")"

CXX=${CXX:-g++}
# Qt installs differ per distro; ask qmake6, then probe, instead of hardcoding.
QTINC=$(qmake6 -query QT_INSTALL_HEADERS 2>/dev/null || true)
QTLIB=$(qmake6 -query QT_INSTALL_LIBS 2>/dev/null || true)
QT_LIBEXECS=$(qmake6 -query QT_INSTALL_LIBEXECS 2>/dev/null || true)

if [ ! -d "$QTINC/QtCore" ]; then
	for candidate in /usr/include/qt6 /usr/include/*/qt6; do
		[ -d "$candidate/QtCore" ] && { QTINC=$candidate; break; }
	done
fi
[ -n "$QTLIB" ] || QTLIB=/usr/lib
[ -n "$QT_LIBEXECS" ] || QT_LIBEXECS=/usr/lib/qt6

MOC=${MOC:-$QT_LIBEXECS/moc}
[ -x "$MOC" ] || MOC=$(command -v moc6 || command -v moc || true)
[ -x "$MOC" ] || { echo "moc not found (looked in $QT_LIBEXECS)"; exit 1; }
[ -d "$QTINC/QtCore" ] || { echo "Qt headers not found (looked in $QTINC)"; exit 1; }

CFLAGS="-std=c++20 -O1 -fPIC -I$QTINC -I$QTINC/QtCore -I$QTINC/QtGui"
LIBS="-L$QTLIB -lQt6Core"

echo "== moc =="
"$MOC" s1_plugin.cpp -o s1_plugin.moc

echo "== plugin: shared Qt (no QT_STATICPLUGIN) =="
$CXX $CFLAGS -shared -o libs1_shared.so s1_plugin.cpp $LIBS

echo "== plugin: static Qt (-DQT_STATICPLUGIN) =="
$CXX $CFLAGS -DQT_STATICPLUGIN -shared -o libs1_static.so s1_plugin.cpp $LIBS

echo "== host (release-like: static libstdc++/libgcc, -O2) =="
# -static-libstdc++ mirrors the release build's self-contained runtime. Some
# distros ship g++ without libstdc++.a; fall back rather than fail, and say so,
# because that difference is worth seeing in the log.
if $CXX -std=c++20 -O2 -fPIC -static-libstdc++ -static-libgcc \
	-I$QTINC -I$QTINC/QtCore -I$QTINC/QtGui \
	-o s1_host s1_host.cpp $LIBS -ldl 2>/dev/null; then
	echo "   (host linked with -static-libstdc++ -static-libgcc)"
else
	echo "   (no static libstdc++; host linked against the shared one)"
	$CXX -std=c++20 -O2 -fPIC \
		-I$QTINC -I$QTINC/QtCore -I$QTINC/QtGui \
		-o s1_host s1_host.cpp $LIBS -ldl
fi

echo
echo "############ ELF evidence ############"
for so in libs1_shared.so libs1_static.so; do
	echo "--- $so ---"
	printf '  section .note.qt.metadata : '
	if readelf -S "$so" | grep -q 'note.qt.metadata'; then echo PRESENT; else echo ABSENT; fi
	printf '  dynsym qt_plugin_instance : '
	if nm -D --defined-only "$so" 2>/dev/null | grep -q 'qt_plugin_instance'; then
		echo PRESENT; else echo ABSENT; fi
	printf '  dynsym s1_c_abi_ping      : '
	if nm -D --defined-only "$so" 2>/dev/null | grep -q 's1_c_abi_ping'; then
		echo PRESENT; else echo ABSENT; fi
done

echo
echo "############ RUN ############"
echo "### A: shared Qt (QPluginLoader usable) ###"
./s1_host qt-loadable "$PWD/libs1_shared.so"
echo
echo "### B: static Qt (QPluginLoader unusable) ###"
./s1_host qt-rejected "$PWD/libs1_static.so"
