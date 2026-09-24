"""Deploy and sign one application in place, including plain helper executables."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


SYSTEM_PREFIXES = ("/System/", "/usr/lib/")


def run(*args):
    try:
        return subprocess.check_output([str(arg) for arg in args], text=True, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f"{args[0]} failed:\n{error.output}") from error


def cached_macho(function):
    cache = {}
    def read(binary):
        binary = Path(binary).resolve()
        metadata = binary.stat()
        key = (str(binary), metadata.st_mtime_ns, metadata.st_size)
        if key not in cache:
            cache[key] = function(binary)
        return cache[key]
    return read


@cached_macho
def identities(binary):
    return [line.strip() for line in run("otool", "-D", binary).splitlines()
            if line.strip() and not line.rstrip().endswith(":")]


@cached_macho
def dependencies(binary):
    names = identities(binary)
    return list(dict.fromkeys(line.strip().split(" (compatibility", 1)[0]
            for line in run("otool", "-L", binary).splitlines()[1:]
            if line[:1].isspace() and line.strip().split(" (compatibility", 1)[0] not in names))


@cached_macho
def rpaths(binary):
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset", run("otool", "-l", binary))


def expand(path, loader, executable):
    return Path(path.replace("@loader_path", str(loader.parent))
                .replace("@executable_path", str(executable.parent)))


def native_sources(executable, libraries, search_paths):
    """Resolve before rewriting; explicitly selected SDK versions take precedence."""
    preferred = {}
    for library in libraries:
        if library.suffix != ".dylib":
            continue
        preferred[library.name] = library
        for name in identities(library):
            preferred[Path(name).name] = library
    result = {}
    inherited = rpaths(executable)
    queue = [executable]
    for binary in queue:
        for link in dependencies(binary):
            if link.startswith(SYSTEM_PREFIXES) or not link.endswith(".dylib"):
                continue
            name = Path(link).name
            if name in result or binary != executable and name == binary.name:
                continue
            candidates = [preferred[name]] if name in preferred else []
            if link.startswith("@rpath/"):
                candidates += [expand(path, binary, executable) / link[len("@rpath/"):]
                               for path in rpaths(binary) + inherited]
            else:
                candidates.append(expand(link, binary, executable))
            candidates += [directory / name for directory in search_paths]
            source = next((path.resolve() for path in candidates if path.is_file()), None)
            if source is None:
                raise RuntimeError(f"Missing runtime dependency: {binary} -> {link}")
            result[name] = source
            queue.append(source)
    return result


def prepare_native(app, libraries, search_paths, executable=None):
    executable = executable or app / "Contents/MacOS" / app.stem
    frameworks = app / "Contents/Frameworks"
    sources = native_sources(executable, libraries, search_paths)
    frameworks.mkdir(parents=True, exist_ok=True)
    for name, source in sources.items():
        target = frameworks / name
        if source != target.resolve():
            if target.is_symlink():
                target.unlink()
            elif target.exists():
                target.chmod(target.stat().st_mode | 0o200)
            shutil.copy2(source, target)
        target.chmod(target.stat().st_mode | 0o200)
        if name == "libmlx.dylib" and (source.parent / "mlx.metallib").is_file():
            # Keep GPU data in Resources; Frameworks entries must be signed code.
            resource = app / "Contents/Resources/mlx.metallib"
            resource.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source.parent / "mlx.metallib", resource)
            colocated = frameworks / "mlx.metallib"
            if colocated.exists() or colocated.is_symlink():
                colocated.unlink()
            colocated.symlink_to("../Resources/mlx.metallib")
    for binary in [executable] + [frameworks / name for name in sources]:
        changes = []
        for link in dependencies(binary):
            if link.startswith(SYSTEM_PREFIXES) or not link.endswith(".dylib"):
                continue
            target = frameworks / Path(link).name
            if not target.is_file() or target == binary:
                continue
            relocated = "@loader_path/" + os.path.relpath(target, binary.parent)
            if link != relocated:
                changes += ["-change", link, relocated]
        if changes:
            run("install_name_tool", *changes, binary)
    return sources


def binaries(app):
    result = {path.resolve() for path in app.rglob("*.dylib") if path.is_file()}
    for framework in app.rglob("*.framework"):
        binary = framework / framework.stem
        if binary.is_file():
            result.add(binary.resolve())
    for bundle in [app] + list(app.rglob("*.app")):
        result.add(bundle / "Contents/MacOS" / bundle.stem)
    result.update(path for path in (app / "Contents/MacOS").iterdir() if path.is_file())
    return sorted(result)


def containing_app(binary):
    return next(parent for parent in binary.parents if (parent / "Contents/Info.plist").is_file())


def complete_frameworks(app, qt):
    """Complete plugin-only Qt dependencies without redeploying the app per plugin."""
    queue = list(binaries(app))
    seen = set()
    for binary in queue:
        if binary in seen:
            continue
        seen.add(binary)
        for link in dependencies(binary):
            match = re.search(r"(Qt[^/]+\.framework)/(.+)$", link)
            if not match:
                continue
            framework, relative = match.groups()
            target = app / "Contents/Frameworks" / framework
            if not (target / relative).is_file():
                source = qt / "lib" / framework
                if not (source / relative).is_file():
                    raise RuntimeError(f"Missing Qt framework: {link}")
                shutil.copytree(source, target, symlinks=True, dirs_exist_ok=True)
            queue.append((target / relative).resolve())


def localize(app):
    """Remove development paths, including those retained in transitive libraries."""
    for binary in binaries(app):
        owner = containing_app(binary)
        frameworks = owner / "Contents/Frameworks"
        changes = []
        for link in dependencies(binary):
            if link.startswith(SYSTEM_PREFIXES):
                continue
            framework = re.search(r"([^/]+\.framework/.+)$", link)
            target = frameworks / (framework.group(1) if framework else Path(link).name)
            if not target.is_file():
                raise RuntimeError(f"Bundle is missing a dependency: {binary} -> {link}")
            relocated = "@loader_path/" + os.path.relpath(target, binary.parent)
            if target.resolve() != binary.resolve() and link != relocated:
                changes += ["-change", link, relocated]
        for path in dict.fromkeys(rpaths(binary)):
            if path.startswith("/") and not path.startswith(SYSTEM_PREFIXES):
                changes += ["-delete_rpath", path]
        if changes:
            binary.chmod(binary.stat().st_mode | 0o200)
            run("install_name_tool", *changes, binary)


def sign(app, identity, entitlements):
    for binary in binaries(app):
        if binary == app / "Contents/MacOS" / app.stem:
            continue  # The main executable is sealed with its bundle.
        if binary.parent.name == "MacOS":
            run("codesign", "--force", "--sign", identity, "--timestamp=none",
                *(["--entitlements", entitlements] if entitlements else []), binary)
            continue
        run("codesign", "--force", "--sign", identity, "--timestamp=none", binary)
    for framework in sorted(app.rglob("*.framework"), key=lambda p: len(p.parts), reverse=True):
        run("codesign", "--force", "--sign", identity, "--timestamp=none", framework)
    for bundle in sorted([app] + list(app.rglob("*.app")), key=lambda p: len(p.parts), reverse=True):
        run("codesign", "--force", "--sign", identity, "--timestamp=none",
            *(["--entitlements", entitlements] if entitlements else []), bundle)
    run("codesign", "--verify", "--deep", "--strict", app)


def deploy(args):
    app = args.app.resolve()
    helpers = [path for path in (app / "Contents/MacOS").iterdir()
               if path.is_file() and path.name != app.stem]
    qt = args.macdeployqt.resolve().parent.parent
    search = args.search_path + [app / "Contents/Frameworks"]
    args.log.parent.mkdir(parents=True, exist_ok=True)
    sources = prepare_native(app, args.library, search)
    for helper in helpers:
        sources.update(prepare_native(app, args.library, search, helper))
    plugins = [qt / "plugins/sqldrivers/libqsqlite.dylib",
               qt / "plugins/tls/libqsecuretransportbackend.dylib",
               qt / "plugins/platforms/libqcocoa.dylib", qt / "plugins/platforms/libqoffscreen.dylib"]
    for category in ["imageformats", "iconengines", "networkinformation"]:
        plugins += sorted((qt / "plugins" / category).glob("*.dylib"))
    for source in plugins:
        plugin = app / "Contents/PlugIns" / source.parent.name / source.name
        plugin.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, plugin)
    modules = []
    if args.qml_dir:
        command = [qt / "libexec/qmlimportscanner", "-rootPath", args.qml_dir]
        for path in args.qml_import + [qt / "qml"]:
            command += ["-importPath", path]
        modules = json.loads(run(*command))
        qml = app / "Contents/Resources/qml"
        if qml.exists():
            shutil.rmtree(qml)
        for module in modules:
            source, relative = module.get("path"), module.get("relativePath")
            if module.get("type") != "module" or not source or not relative:
                continue
            relative = Path(relative)
            if relative.is_absolute() or ".." in relative.parts:
                raise RuntimeError(f"Invalid QML module destination: {relative}")
            shutil.copytree(source, qml / relative, symlinks=False, dirs_exist_ok=True)
    config = app / "Contents/Resources/qt.conf"
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text("[Paths]\nPlugins = PlugIns\nQmlImports = Resources/qml\nQml2Imports = Resources/qml\n")
    args.log.write_text(json.dumps({"qmlModules": [m.get("name") for m in modules]}, indent=2))
    complete_frameworks(app, qt)
    # Stage plugin/framework-specific non-Qt libraries too. Each executable's
    # dependency graph is resolved before relocation, with selected SDKs first.
    extra_sources = {}
    for binary in binaries(app):
        extra_sources.update(native_sources(binary, args.library, search + [qt / "lib"]))
    for name, source in extra_sources.items():
        target = app / "Contents/Frameworks" / name
        if source != target.resolve():
            if target.is_symlink():
                target.unlink()
            elif target.exists():
                target.chmod(target.stat().st_mode | 0o200)
            shutil.copy2(source, target)
    complete_frameworks(app, qt)
    localize(app)
    if list(app.rglob("*.app")):
        raise RuntimeError("Nested .app bundles violate the single application output contract")
    sign(app, args.identity, args.entitlements)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--macdeployqt", type=Path, required=True)
    parser.add_argument("--qml-dir", type=Path)
    parser.add_argument("--qml-import", type=Path, action="append", default=[])
    parser.add_argument("--library", type=Path, action="append", default=[])
    parser.add_argument("--search-path", type=Path, action="append", default=[])
    parser.add_argument("--identity", default="-")
    parser.add_argument("--entitlements", type=Path)
    parser.add_argument("--log", type=Path, required=True)
    deploy(parser.parse_args())
