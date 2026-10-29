"""Deploy and sign one application in place, including plain helper executables."""
import argparse
from contextlib import contextmanager
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


def reject_retired_runtime(path):
    name = Path(path).name.lower()
    if "onnx" in name or "opencv_dnn" in name or name.startswith("libpiper."):
        raise RuntimeError(f"Retired model runtime is forbidden in product deployment: {path}")


def purge_retired_runtimes(app):
    removed = []
    for path in sorted(app.rglob("*"), key=lambda item: len(item.parts), reverse=True):
        try:
            reject_retired_runtime(path)
        except RuntimeError:
            removed.append(str(path.relative_to(app)))
            if path.is_symlink() or path.is_file():
                path.unlink()
            elif path.is_dir():
                shutil.rmtree(path)
    return removed


def native_sources(executable, libraries, search_paths, runtime_plugins=()):
    """Resolve before rewriting; explicitly selected SDK versions take precedence."""
    preferred = {}
    for library in libraries:
        if library.suffix != ".dylib":
            continue
        preferred[library.name] = library
        for name in identities(library):
            preferred[Path(name).name] = library
    result = {}
    for plugin in runtime_plugins:
        reject_retired_runtime(plugin)
        if not plugin.is_file():
            raise RuntimeError(f"Missing native runtime plugin: {plugin}")
        result[plugin.name] = plugin.resolve()
    inherited = rpaths(executable)
    queue = [executable] + list(result.values())
    for binary in queue:
        for link in dependencies(binary):
            if link.startswith(SYSTEM_PREFIXES) or not link.endswith(".dylib"):
                continue
            name = Path(link).name
            reject_retired_runtime(name)
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


def prepare_native(app, libraries, search_paths, executable=None, runtime_plugins=()):
    executable = executable or app / "Contents/MacOS" / app.stem
    frameworks = app / "Contents/Frameworks"
    sources = native_sources(executable, libraries, search_paths, runtime_plugins)
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
    result = {path.resolve() for path in app.rglob("*.dylib") if path.is_file()
              and not any(parent.name.endswith(".dSYM") for parent in path.parents)}
    for framework in app.rglob("*.framework"):
        binary = framework / framework.stem
        if binary.is_file():
            result.add(binary.resolve())
    for bundle in [app] + list(app.rglob("*.app")):
        result.add(bundle / "Contents/MacOS" / bundle.stem)
    result.update(path for path in (app / "Contents/MacOS").iterdir() if path.is_file())
    return sorted(result)


def deploy_qml_modules(destination, modules, compact=False, quick_style=None, embedded_resources_modules=()):
    """Copy imported modules without recursively deploying unrelated child modules."""
    modules = [module for module in modules if module.get("type") == "module"
               and module.get("path") and module.get("relativePath")]
    styles = set()
    for module in modules:
        if module.get("name") == "QtQuick.Controls":
            manifest = Path(module["path"]) / "qmldir"
            if manifest.is_file():
                styles.update(re.findall(r"^(?:optional|default) import (QtQuick\.Controls\.\w+)\b",
                                         manifest.read_text(), re.MULTILINE))
    if quick_style:
        selected_style = "QtQuick.Controls." + quick_style
        if not any(module.get("name") == selected_style for module in modules):
            raise RuntimeError(f"The requested Qt Quick Controls style is unavailable: {quick_style}")
        modules = [module for module in modules if not any(
            (module.get("name") == style or module.get("name", "").startswith(style + "."))
            and style != selected_style for style in styles)]
    if destination.exists():
        shutil.rmtree(destination)
    copied = []
    for module in modules:
        source, relative = Path(module["path"]), Path(module["relativePath"])
        if relative.is_absolute() or ".." in relative.parts:
            raise RuntimeError(f"Invalid QML module destination: {relative}")

        def ignore(directory, names):
            if not compact:
                return []
            directory = Path(directory)
            skipped = []
            for name in names:
                path = directory / name
                if name == "designer" or name.endswith((".dSYM", ".qmltypes")):
                    skipped.append(name)
                elif (path / "qmldir").is_file():
                    declaration = re.search(r"^module\s+(\S+)",
                                            (path / "qmldir").read_text(), re.MULTILINE)
                    if declaration and declaration.group(1) != module.get("name"):
                        skipped.append(name)
                elif (module.get("name") in embedded_resources_modules
                      and path.is_relative_to(source / "resources")
                      and path.suffix.lower() in (".svg", ".ttf", ".otf", ".png", ".jpg", ".webp", ".qsb")):
                    skipped.append(name)
            return skipped

        shutil.copytree(source, destination / relative, symlinks=False,
                        dirs_exist_ok=True, ignore=ignore)
        copied.append(module.get("name"))
    return copied


def prune_native(app, runtime_plugins=()):
    """Remove stale Frameworks entries outside the executable and plugin dependency closure."""
    frameworks = app / "Contents/Frameworks"
    native = set(binaries(app))
    roots = {binary for binary in native if not binary.is_relative_to(frameworks)}
    roots.update((frameworks / plugin.name).resolve() for plugin in runtime_plugins)
    reachable = set()
    queue = list(roots)
    for binary in queue:
        if binary in reachable:
            continue
        reachable.add(binary)
        for link in dependencies(binary):
            if link.startswith(SYSTEM_PREFIXES):
                continue
            match = re.search(r"([^/]+\.framework/.+)$", link)
            target = frameworks / (match.group(1) if match else Path(link).name)
            if not target.is_file():
                raise RuntimeError(f"Bundle is missing a dependency: {binary} -> {link}")
            queue.append(target.resolve())
    removed = []
    for path in sorted(frameworks.iterdir()):
        if path.suffix == ".framework":
            keep = (path / path.stem).is_file() and (path / path.stem).resolve() in reachable
        elif path.suffix == ".dylib":
            keep = path.resolve() in reachable
        else:
            continue
        if not keep:
            removed.append(str(path.relative_to(app)))
            if path.is_symlink() or path.is_file():
                path.unlink()
            else:
                shutil.rmtree(path)
    return removed


def prune_framework_development_files(app):
    """Keep Qt framework runtime resources without SDK headers or module maps."""
    removed = []
    for framework in sorted((app / "Contents/Frameworks").glob("Qt*.framework")):
        if framework.is_symlink():
            continue
        roots = [framework]
        versions = framework / "Versions"
        if versions.is_dir() and not versions.is_symlink():
            roots.extend(path for path in versions.iterdir() if path.is_dir() and not path.is_symlink())
        for root in roots:
            for name in ("Headers", "PrivateHeaders", "Modules"):
                path = root / name
                if path.is_symlink():
                    path.unlink()
                elif path.is_dir():
                    shutil.rmtree(path)
                else:
                    continue
                removed.append(str(path.relative_to(app)))
    return removed


def thin_native(app):
    """Keep only architectures supported by the enclosing product executable."""
    executable = app / "Contents/MacOS" / app.stem
    required = set(run("lipo", "-archs", executable).split())
    saved = 0
    for binary in binaries(app):
        available = set(run("lipo", "-archs", binary).split())
        if not required.issubset(available):
            raise RuntimeError(f"Runtime architectures do not match {sorted(required)}: {binary}")
        extra = available - required
        if not extra:
            continue
        before = binary.stat().st_size
        temporary = binary.with_name("." + binary.name + ".thin")
        # Removing slices can leave the original universal-file padding intact.
        # A one-architecture product needs a genuinely thin Mach-O file.
        options = (["-thin", next(iter(required))] if len(required) == 1 else
                   [arg for architecture in sorted(extra) for arg in ("-remove", architecture)])
        try:
            run("lipo", binary, *options, "-output", temporary)
            shutil.copymode(binary, temporary)
            os.replace(temporary, binary)
        finally:
            temporary.unlink(missing_ok=True)
        saved += before - binary.stat().st_size
    return saved


def strip_native(app):
    """Strip local/debug symbols only; retain exported symbols used by plugins and SDK APIs."""
    saved = 0
    for binary in binaries(app):
        before = binary.stat().st_size
        binary.chmod(binary.stat().st_mode | 0o200)
        run("strip", "-S", "-x", binary)
        saved += before - binary.stat().st_size
    return saved


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


def qt_plugins(qt, libraries):
    """Include the platform media backend when this app links Qt Multimedia."""
    plugins = [qt / "plugins/sqldrivers/libqsqlite.dylib",
               qt / "plugins/tls/libqsecuretransportbackend.dylib",
               qt / "plugins/platforms/libqcocoa.dylib", qt / "plugins/platforms/libqoffscreen.dylib"]
    categories = ["imageformats", "iconengines", "networkinformation"]
    if any(library.name == "QtMultimedia" for library in libraries):
        categories.append("multimedia")
    for category in categories:
        plugins += sorted((qt / "plugins" / category).glob("*.dylib"))
    return plugins


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


def remove_signing_temporaries(app):
    """Discard incomplete codesign output inside the generated bundle only."""
    removed = []
    for path in app.rglob("*.cstemp"):
        if path.is_file() or path.is_symlink():
            removed.append(str(path.relative_to(app)))
            path.unlink()
    return removed


def sign(app, identity, entitlements):
    remove_signing_temporaries(app)
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


@contextmanager
def deployment_lock(app):
    """Serialize bundle writers using a persistent lock outside the bundle."""
    import fcntl
    app = app.resolve()
    lock_path = app.parent / f".{app.name}.deploy.lock"
    with lock_path.open("a") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        try:
            yield app
        finally:
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)


def deploy(args):
    with deployment_lock(args.app) as app:
        _deploy_locked(args, app)


def _deploy_locked(args, app):
    removed_retired_runtimes = purge_retired_runtimes(app)
    removed_signing_temporaries = remove_signing_temporaries(app)
    helpers = [path for path in (app / "Contents/MacOS").iterdir()
               if path.is_file() and path.name != app.stem]
    qt = args.macdeployqt.resolve().parent.parent
    search = args.search_path + [app / "Contents/Frameworks"]
    args.log.parent.mkdir(parents=True, exist_ok=True)
    runtime_plugins = getattr(args, "runtime_plugin", [])
    sources = prepare_native(app, args.library, search, runtime_plugins=runtime_plugins)
    for helper in helpers:
        sources.update(prepare_native(app, args.library, search, helper))
    plugins = qt_plugins(qt,args.library)
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
        copied_modules = deploy_qml_modules(app / "Contents/Resources/qml", modules,
                                             args.compact, args.quick_style, args.embedded_resources_module)
    config = app / "Contents/Resources/qt.conf"
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text("[Paths]\nPlugins = PlugIns\nQmlImports = Resources/qml\nQml2Imports = Resources/qml\n")
    report = {"qmlModules": copied_modules if args.qml_dir else [],
              "removedSigningTemporaries": removed_signing_temporaries,
              "compact": args.compact, "quickStyle": args.quick_style,
              "embeddedResourceModules": args.embedded_resources_module,
              "runtimePlugins": [plugin.name for plugin in runtime_plugins]}
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
    if args.compact:
        # The plugin closure must exist before pruning validates every link.
        report["removedNativeEntries"] = prune_native(app, runtime_plugins)
        report["removedFrameworkDevelopmentEntries"] = prune_framework_development_files(app)
    report["removedRetiredRuntimes"] = removed_retired_runtimes + purge_retired_runtimes(app)
    localize(app)
    if args.compact and args.configuration in ("Release", "MinSizeRel"):
        report["thinnedBytes"] = thin_native(app)
        report["strippedBytes"] = strip_native(app)
    if list(app.rglob("*.app")):
        raise RuntimeError("Nested .app bundles violate the single application output contract")
    sign(app, args.identity, args.entitlements)
    args.log.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--macdeployqt", type=Path, required=True)
    parser.add_argument("--qml-dir", type=Path)
    parser.add_argument("--qml-import", type=Path, action="append", default=[])
    parser.add_argument("--library", type=Path, action="append", default=[])
    parser.add_argument("--runtime-plugin", type=Path, action="append", default=[],
                        help="SDK-selected lazy native plugin to bundle with its dependency closure")
    parser.add_argument("--search-path", type=Path, action="append", default=[])
    parser.add_argument("--identity", default="-")
    parser.add_argument("--entitlements", type=Path)
    parser.add_argument("--compact", action="store_true")
    parser.add_argument("--quick-style", help="Deploy only the configured Qt Quick Controls style")
    parser.add_argument("--configuration", default="", help="Release/MinSizeRel enable symbol stripping with --compact")
    parser.add_argument("--embedded-resources-module", action="append", default=[],
                        help="Module whose fonts/images/icons are already embedded; retain filesystem licenses")
    parser.add_argument("--log", type=Path, required=True)
    deploy(parser.parse_args())
