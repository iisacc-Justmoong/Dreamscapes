#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
android_root="$project_dir/build/android"
sdk_source_root="${DREAMSCAPES_SDK_SOURCE_ROOT:-$project_dir/../../SDK}"
sdk_install_root="${DREAMSCAPES_SDK_INSTALL_ROOT:-$HOME/.local/SDK}"
qt_root="${DREAMSCAPES_QT_ROOT:-/Volumes/Storage/Qt/6.8.3}"
prefix="$android_root/sdk"
export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-/opt/homebrew/share/android-commandlinetools}"
export ANDROID_HOME="$ANDROID_SDK_ROOT"
export ANDROID_NDK_ROOT="${ANDROID_NDK_ROOT:-/opt/homebrew/share/android-ndk}"
export JAVA_HOME="${DREAMSCAPES_JAVA_HOME:-/Applications/CLion.app/Contents/jbr/Contents/Home}"
export GRADLE_USER_HOME="$android_root/gradle"
export ANDROID_USER_HOME="$android_root/user"
export PATH="$JAVA_HOME/bin:$ANDROID_SDK_ROOT/platform-tools:$PATH"

for required in "$qt_root/android_arm64_v8a/lib/cmake/Qt6/qt.toolchain.cmake" \
                "$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
                "$JAVA_HOME/bin/java"; do
    if [[ ! -f "$required" ]]; then
        echo "Required Android build file is missing: $required" >&2
        exit 1
    fi
done
mkdir -p "$android_root/downloads" "$android_root/sources/json-c" \
         "$android_root/logs" "$ANDROID_USER_HOME"

run_logged() {
    local label="$1"
    shift
    echo "Android: $label"
    if ! "$@" > "$android_root/logs/$label.log" 2>&1; then
        tail -n 70 "$android_root/logs/$label.log" >&2
        return 1
    fi
}

json_archive="$android_root/downloads/json-c-0.18.tar.gz"
if [[ ! -f "$json_archive" ]]; then
    curl --fail --location --silent --show-error \
        'https://codeload.github.com/json-c/json-c/tar.gz/refs/tags/json-c-0.18-20240915' \
        -o "$json_archive"
fi
json_digest="$(shasum -a 256 "$json_archive")"
if [[ "${json_digest%% *}" != 3112c1f25d39eca661fe3fc663431e130cc6e2f900c081738317fba49d29e298 ]]; then
    echo "json-c source checksum mismatch: $json_archive" >&2
    exit 1
fi
if [[ ! -f "$android_root/sources/json-c/CMakeLists.txt" ]]; then
    tar -xzf "$json_archive" -C "$android_root/sources/json-c" --strip-components=1
fi

common=(
    -G Ninja
    "-DCMAKE_TOOLCHAIN_FILE=$qt_root/android_arm64_v8a/lib/cmake/Qt6/qt.toolchain.cmake"
    "-DQT_HOST_PATH=$qt_root/macos"
    "-DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT"
    "-DANDROID_NDK_ROOT=$ANDROID_NDK_ROOT"
    "-DCMAKE_ANDROID_NDK=$ANDROID_NDK_ROOT"
    -DANDROID_ABI=arm64-v8a
    -DANDROID_PLATFORM=android-28
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_TESTING=OFF
    "-DCMAKE_INSTALL_PREFIX=$prefix"
    "-DCMAKE_PREFIX_PATH=$prefix"
    "-DiiFileProvider_DIR=$prefix/lib/cmake/iiFileProvider"
    "-DiiSocietyContainer_DIR=$prefix/lib/cmake/iiSocietyContainer"
)

build_dependency() {
    local name="$1"
    local source="$2"
    shift 2
    local binary="$android_root/dependencies/$name/build"
    run_logged "$name-configure" cmake -S "$source" -B "$binary" "${common[@]}" "$@"
    run_logged "$name-build" cmake --build "$binary" --parallel 4
    run_logged "$name-install" cmake --install "$binary"
}

# A static json-c keeps iiLocalDiffusion's private dependency inside its real Android library.
build_dependency json-c "$android_root/sources/json-c" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
for package in iiFileProvider iiCSMIDI iiSocietyContainer iiSocietyHelper iiSocietySync; do
    build_dependency "$package" "$sdk_source_root/$package"
done
build_dependency iiLocalDiffusion "$sdk_source_root/iiLocalDiffusion" \
    -DIILD_ENABLE_MLX=OFF -DIILD_ENABLE_LIBTORCH=OFF -DIILD_ENABLE_COREML=OFF \
    -DIILD_BUILD_TOOLS=OFF -DIILD_INSTALL_PYTHON_REFERENCE=OFF \
    "-Djson-c_DIR=$prefix/lib/cmake/json-c"

packages=("-DLVRS_DIR=$sdk_install_root/LVRS/platforms/android/lib/cmake/LVRS")
for package in iiFileProvider iiCSMIDI iiSocietyContainer iiSocietyHelper iiSocietySync iiLocalDiffusion; do
    packages+=("-D${package}_DIR=$prefix/lib/cmake/$package")
done
for package in iiLicenseManager iiPaintEngine iiUpdateManager; do
    packages+=("-D${package}_DIR=$sdk_install_root/$package/platforms/android/lib/cmake/$package")
done
run_logged Dreamscapes-configure cmake -S "$project_dir" -B "$android_root/build" \
    "${common[@]}" "${packages[@]}" -DQT_ANDROID_BUILD_ALL_ABIS=OFF -DCMAKE_BUILD_TYPE=Debug \
    -DDREAMSCAPES_BUILD_ANDROID_PHOTO_TESTS=OFF -DQT_USE_TARGET_ANDROID_BUILD_DIR=OFF
run_logged Dreamscapes-apk cmake --build "$android_root/build" --target apk --parallel 4
apk="$android_root/build/android-build/Dreamscapes.apk"
test -s "$apk"
run_logged Dreamscapes-signature "$ANDROID_SDK_ROOT/build-tools/36.0.0/apksigner" verify "$apk"
echo "APK: $apk"
