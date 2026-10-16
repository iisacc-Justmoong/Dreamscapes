"""Build the macOS inference runtime without the optional AMD LibTorch backend.

The SDK checkout and its installed package remain untouched. All generated
files, downloaded sources and the product-owned package stay under build/.
"""
import argparse
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-source", type=Path, default=ROOT.parents[1] / "SDK/iiLocalDiffusion")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--configure-only", action="store_true")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("This profile is for macOS; other platforms retain their own inference backends.")
    sdk = args.sdk_source.resolve()
    if not (sdk / "CMakeLists.txt").is_file():
        parser.error("Provide the iiLocalDiffusion SDK checkout with --sdk-source.")
    build = ROOT / "build/apple-runtime/build"
    prefix = ROOT / "build/runtime-sdk/iiLocalDiffusion-apple"
    cache = sdk / "build/_deps"
    options = []
    product_cache = ROOT / "build/CMakeCache.txt"
    if product_cache.is_file():
        for package in ("iiFileProvider_DIR", "iiFileProviderNative_DIR"):
            value = re.search(r"^" + package + r":[^=]+=(.+)$", product_cache.read_text(), re.MULTILINE)
            if value:
                options.append(f"-D{package}={value.group(1)}")
    dependencies = {"METAL_CPP": "metal_cpp", "JSON": "json",
                    "FMT": "fmt", "IILD_ONNXRUNTIME": "iild_onnxruntime"}
    for name, folder in dependencies.items():
        source = cache / (folder + "-src")
        if source.is_dir():
            options.append(f"-DFETCHCONTENT_SOURCE_DIR_{name}={source}")
    mlx_source = cache / "mlx-src"
    if mlx_source.is_dir():
        owned_mlx = ROOT / "build/apple-runtime/source/mlx"
        if not owned_mlx.exists():
            shutil.copytree(mlx_source, owned_mlx, symlinks=True,
                            ignore=shutil.ignore_patterns(".git"))
        # libc++ no longer supplies <iterator> through unrelated headers.
        for relative, header in (("mlx/distributed/jaccl/lib/jaccl/rdma.h", "iterator"),
                                 ("mlx/3rdparty/pocketfft.h", "exception")):
            source = owned_mlx / relative
            if source.is_file():
                contents = source.read_text()
                include = f"#include <{header}>"
                if include not in contents:
                    source.write_text(include + "\n" + contents)
        bridge = owned_mlx / "mlx/distributed/jaccl/jaccl.cpp"
        if bridge.is_file():
            contents = bridge.read_text()
            for header in ("group.h", "jaccl.h"):
                contents = contents.replace(f"<jaccl/{header}>", f'"lib/jaccl/{header}"')
            bridge.write_text(contents)
        # jaccl's public headers also include one another with the installed
        # prefix. Resolve these siblings locally to prevent header mixing.
        for source in (owned_mlx / "mlx/distributed/jaccl/lib/jaccl").glob("*"):
            if source.suffix in (".h", ".cpp"):
                contents = source.read_text()
                patched = contents.replace('#include "jaccl/', '#include "')
                if patched != contents:
                    source.write_text(patched)
        options.append(f"-DFETCHCONTENT_SOURCE_DIR_MLX={owned_mlx}")
    native_source = cache / "iild_sdcpp-src"
    if native_source.is_dir():
        # SDK configuration applies its owned patches. Use a private source
        # copy so configuring this profile cannot patch another build's tree.
        owned_source = ROOT / "build/apple-runtime/source/stable-diffusion"
        if not owned_source.exists():
            shutil.copytree(native_source, owned_source, symlinks=True,
                            ignore=shutil.ignore_patterns(".git"))
            run("git", "init", "--quiet", owned_source)
        # This upstream Metal source uses unique_ptr without its owning header.
        # Keep the compatibility fix in our private copy, never the shared SDK.
        metal_source = owned_source / "ggml/src/ggml-metal/ggml-metal.cpp"
        if metal_source.is_file():
            contents = metal_source.read_text()
            if "#include <memory>" not in contents:
                metal_source.write_text("#include <memory>\n" + contents)
        options.append(f"-DFETCHCONTENT_SOURCE_DIR_IILD_SDCPP={owned_source}")
    run("cmake", "-S", sdk, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_INSTALL_PREFIX={prefix}", "-DBUILD_TESTING=ON", "-DIILD_BUILD_TOOLS=ON",
        "-DIILD_ENABLE_NATIVE_DIFFUSION=ON", "-DIILD_ENABLE_POSE=ON",
        "-DIILD_ENABLE_MLX=ON", "-DIILD_ENABLE_COREML=ON", "-DIILD_ENABLE_LIBTORCH=OFF",
        "-DIILD_MLX_GPU_BACKEND=auto", "-DFETCHCONTENT_UPDATES_DISCONNECTED=ON", *options)
    if args.configure_only:
        return
    run("cmake", "--build", build, "--parallel", max(1, args.jobs), "--target",
        "iiLocalDiffusion", "iild-run", "iild-model-package", "ComputeTests", "RocmTests", "NativeDiffusionTests",
        "NativePoseTests", "NativePoseDisabledTests")
    run("ctest", "--test-dir", build, "--output-on-failure", "-R",
        "^(ComputeTests|RocmTests|NativeDiffusionTests|NativePoseTests|NativePoseDisabledTests)$")
    run("cmake", "--install", build)
    links = subprocess.check_output(["otool", "-L", prefix / "lib/libiiLocalDiffusion.dylib"], text=True)
    if "libtorch" in links or "libc10" in links:
        raise RuntimeError("The Apple runtime still links the optional LibTorch backend.")
    (build.parent / "profile.json").write_text(json.dumps({
        "sdkSource": str(sdk), "prefix": str(prefix), "libtorch": False,
        "mlx": True, "coreml": True, "nativeDiffusion": True, "pose": True,
        "sdkTestsPassed": True,
    }, indent=2) + "\n")
    run("cmake", "-S", ROOT, "-B", ROOT / "build",
        f"-DiiLocalDiffusion_DIR={prefix / 'lib/cmake/iiLocalDiffusion'}")


if __name__ == "__main__":
    main()
