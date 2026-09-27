#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
NDK="${ANDROID_NDK_HOME:-$SDK/ndk/27.1.12297006}"
BT="$SDK/build-tools/35.0.0"
HOST=darwin-x86_64
[[ "$(uname)" == Linux ]] && HOST=linux-x86_64
CC="$NDK/toolchains/llvm/prebuilt/$HOST/bin"
mkdir -p build/apk/lib/{arm64-v8a,armeabi-v7a} dist
for spec in 'arm64-v8a:aarch64-linux-android' 'armeabi-v7a:armv7a-linux-androideabi'; do
  abi="${spec%%:*}"; target="${spec#*:}"
  "$CC/${target}26-clang" -std=c11 -O2 -g -fPIC -shared -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration -Isrc -Ivendor -I"$NDK/sources/android/native_app_glue" src/main.c "$NDK/sources/android/native_app_glue/android_native_app_glue.c" -o "build/apk/lib/$abi/libember.so" -landroid -llog -lEGL -lGLESv2 -lm -Wl,-u,ANativeActivity_onCreate -Wl,-z,max-page-size=16384
 done
"$BT/aapt" package -f -M AndroidManifest.xml -S res -A assets -I "$SDK/platforms/android-34/android.jar" -F build/unsigned.apk
(cd build/apk && zip -q -r ../unsigned.apk lib)
"$BT/zipalign" -f -p 4 build/unsigned.apk build/aligned.apk
if [[ ! -f build/debug.keystore ]]; then
  keytool -genkeypair -keystore build/debug.keystore -storepass android -keypass android -alias androiddebugkey -dname 'CN=Android Debug,O=Android,C=US' -keyalg RSA -validity 10000 >/dev/null 2>&1
fi
"$BT/apksigner" sign --ks build/debug.keystore --ks-pass pass:android --out dist/ember-cinema.apk build/aligned.apk
"$BT/apksigner" verify dist/ember-cinema.apk
printf 'Built %s/dist/ember-cinema.apk\n' "$PWD"
