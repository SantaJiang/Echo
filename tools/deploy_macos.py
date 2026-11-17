#!/usr/bin/env python3
"""Deploy only Echo's Qt plugins and verify that the bundle has no SDK dependencies."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess


def run(*args):
    try:
        return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT).stdout
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f"Command failed: {args}\n{error.stdout}") from error


def macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as source:
        return source.read(4) in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe",
                                 b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf")


def dependencies(path):
    return [line.strip().split(" (", 1)[0] for line in run("otool", "-L", str(path)).splitlines()[1:]
            if " (compatibility version" in line]


def rpaths(path):
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset", run("otool", "-l", str(path)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--qt", required=True, type=Path)
    parser.add_argument("--qml", required=True, type=Path)
    parser.add_argument("--macdeployqt", required=True)
    parser.add_argument("--extra-executable", action="append", default=[])
    args = parser.parse_args()
    bundle = args.bundle.resolve()
    qt = args.qt.resolve()
    contents = bundle / "Contents"
    frameworks = contents / "Frameworks"
    executable = contents / "MacOS/Echo"
    if not executable.is_file():
        raise RuntimeError(f"Not an Echo bundle: {bundle}")

    plugins = contents / "PlugIns"
    # Regenerate the deployment-owned directories, so stale optional SQL drivers
    # cannot survive a repeated install. Music and user data never live here.
    shutil.rmtree(plugins, ignore_errors=True)
    shutil.rmtree(contents / "Resources/qml", ignore_errors=True)
    required = ["platforms/libqcocoa.dylib", "sqldrivers/libqsqlite.dylib"]
    optional = ["imageformats/libqjpeg.dylib", "imageformats/libqgif.dylib",
                "imageformats/libqicns.dylib", "imageformats/libqsvg.dylib",
                "imageformats/libqwebp.dylib"]
    # Homebrew's unified qt formula keeps plugins under share/qt, aqt/SDK under plugins.
    plugin_root = qt / "plugins"
    if not plugin_root.is_dir():
        plugin_root = qt / "share/qt/plugins"
    copied = []
    for name in required + optional:
        source = plugin_root / name
        if not source.is_file():
            if name in required:
                raise RuntimeError(f"Required Qt plugin missing: {source}")
            continue
        destination = plugins / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        copied.append(destination)

    command = [args.macdeployqt, str(bundle), "-always-overwrite", "-no-plugins",
               f"-qmldir={args.qml.resolve()}", f"-libpath={qt / 'lib'}", "-no-codesign"]
    for path in copied + [Path(p).resolve() for p in args.extra_executable]:
        command.append(f"-executable={path}")
    result = run(*command)
    print(result, end="")
    # macdeployqt can exit with status zero after printing ERROR. Do not accept it.
    if re.search(r"^ERROR:", result, re.MULTILINE):
        raise RuntimeError("macdeployqt reported dependency errors")
    # -no-plugins also suppresses macdeployqt's qt.conf. Keep runtime lookup
    # inside the app rather than falling back to the developer SDK.
    (contents / "Resources/qt.conf").write_text(
        "[Paths]\nPlugins = PlugIns\nQmlImports = Resources/qml\n", encoding="utf-8")

    binaries = [p for p in contents.rglob("*") if macho(p)]
    # Homebrew frameworks carry cross-keg absolute install names that macdeployqt
    # leaves untouched. Rebase every bundled framework onto @rpath.
    bundled = {f.stem for f in frameworks.glob("*.framework")}
    for name in sorted(bundled):
        binary = frameworks / f"{name}.framework/Versions/A/{name}"
        if binary.is_file():
            run("install_name_tool", "-id", f"@rpath/{name}.framework/Versions/A/{name}", str(binary))
    # Third-party dylibs (webp, ffmpeg, ...) keep their SDK install names too.
    bundled_dylibs = {p.name for p in frameworks.glob("*.dylib")}
    for name in sorted(bundled_dylibs):
        run("install_name_tool", "-id", f"@rpath/{name}", str(frameworks / name))
    for consumer in binaries:
        for dependency in dependencies(consumer):
            match = re.fullmatch(r"/.*/([^/]+)\.framework/Versions/A/\1", dependency)
            if match and match.group(1) in bundled:
                name = match.group(1)
                run("install_name_tool", "-change", dependency,
                    f"@rpath/{name}.framework/Versions/A/{name}", str(consumer))
            elif dependency.startswith("/") and dependency.rsplit("/", 1)[-1] in bundled_dylibs:
                run("install_name_tool", "-change", dependency,
                    "@rpath/" + dependency.rsplit("/", 1)[-1], str(consumer))
    for binary in binaries:
        # All Qt and bundled third-party libraries are in Contents/Frameworks.
        # QML plugins originally carry loader-relative SDK /lib paths, which no
        # longer describe their deployed location.
        for old in dict.fromkeys(rpaths(binary)):
            run("install_name_tool", "-delete_rpath", old, str(binary))
        relative = os.path.relpath(frameworks, binary.parent)
        run("install_name_tool", "-add_rpath", "@loader_path/" + relative, str(binary))

    failures = []
    for binary in binaries:
        for dependency in dependencies(binary):
            if "QtWidgets" in dependency:
                failures.append(f"Widgets dependency in {binary}: {dependency}")
            if dependency.startswith(("/System/Library/", "/usr/lib/")):
                continue
            if dependency.startswith("@rpath/"):
                candidate = frameworks / dependency[len("@rpath/"):]
            elif dependency.startswith("@loader_path/"):
                candidate = binary.parent / dependency[len("@loader_path/"):]
            elif dependency.startswith("@executable_path/"):
                candidate = executable.parent / dependency[len("@executable_path/"):]
            else:
                failures.append(f"{binary.relative_to(bundle)} -> {dependency}")
                continue
            if not candidate.is_file() or not candidate.resolve().is_relative_to(bundle):
                failures.append(f"{binary.relative_to(bundle)} -> {dependency}")
    if failures:
        raise RuntimeError("Unresolved bundle dependencies:\n" + "\n".join(failures))

    # Rewriting load commands invalidates the SDK signatures. Sign after repair.
    for binary in sorted(binaries, key=lambda p: len(p.parts), reverse=True):
        run("codesign", "--force", "--sign", "-", str(binary))
    run("codesign", "--force", "--deep", "--sign", "-", str(bundle))
    run("codesign", "--verify", "--deep", "--strict", str(bundle))
    print(f"Verified {len(binaries)} Mach-O files; QML, Cocoa and SQLite deployed in {bundle}")


if __name__ == "__main__":
    main()
