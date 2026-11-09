"""Rebuild only proven native outputs; never discard packaged siblings."""

import argparse
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat


PRODUCT = "STOMP"
BASE = Path("test/Plugins/Default")
BUNDLE = BASE / (PRODUCT + ".bundle")
FILES = tuple(BUNDLE / relative for relative in (
    f"Contents/MacOS/{PRODUCT}", "Contents/Info.plist",
    "Contents/Resources/English.lproj/InfoPlist.strings",
    "Contents/Resources/constants.xlf", "Contents/Resources/manifest.json",
    "Contents/_CodeSignature/CodeResources",
))
XCODE_DATA = Path("build/XCBuildData")
DIRECTORIES = (
    Path("build") / (PRODUCT + ".build"),
    Path("build/Default") / (PRODUCT + ".build"),
    Path("test/Plugins") / (PRODUCT + ".build"),
    BASE / (PRODUCT + ".build"),
)
DEPENDENCY_FILES = tuple(map(Path, (
    "a/libapr-1.a", "a/libjsoncpp.a", "apr.tar.gz", "jsoncpp.tar.gz",
    "winbuild/STOMP.4DX",
)))
WINDOWS = BUNDLE / "Contents/Windows64/STOMP.4DX"
# Exact roots emitted by the original pinned tarball/configure/CMake recipes.
DEPENDENCY_DIRECTORIES = tuple(map(Path, (
    "apr-1.5.2", "apr-arm64", "apr-x86_64", "out-arm64", "out-x86_64",
    "jsoncpp-1.8.4", "jsoncpp-build",
)))


def generated(path):
    if "\\" in path or PurePosixPath(path).as_posix() != path \
            or PurePosixPath(path).is_absolute() or ".." in PurePosixPath(path).parts:
        return False
    return (path in set(map(str, (*FILES, *DEPENDENCY_FILES, WINDOWS)))
            or bool(re.fullmatch(
                r"build/XCBuildData/(?:build\.db(?:-wal|-shm)?|"
                r"[0-9a-f]{32}\.xcbuilddata/.+)", path))
            or any(path.startswith(str(directory) + "/")
                   for directory in (*DIRECTORIES, *DEPENDENCY_DIRECTORIES)))


def dependency_symlink(path):
    # GNU Libtool's .la backlink, not arbitrary archive/header/cache symlinks.
    if path not in {Path(root) / ".libs/libapr-1.la" for root in ("apr-arm64", "apr-x86_64")}:
        return False
    if os.readlink(path) != "../libapr-1.la":
        raise ValueError(f"Unexpected APR Libtool symlink target: {path}")
    target = path.parent.parent / "libapr-1.la"
    if any(entry.is_symlink() for entry in (target, *target.parents, *path.parents)) \
            or not stat.S_ISREG(target.lstat().st_mode):
        raise ValueError(f"APR Libtool target must be a same-build-root regular file: {path}")
    if path.resolve(strict=True) != target.resolve(strict=True):
        raise ValueError(f"APR Libtool symlink escapes its build root: {path}")
    return True


def validate_paths(paths, *, dependency_links=False):
    root = Path.cwd().resolve()
    for path in paths:
        for parent in (path, *path.parents):
            if parent == Path("."):
                break
            if parent.is_symlink():
                raise ValueError(f"Refusing symlink output: {parent}")
        if not path.resolve().is_relative_to(root):
            raise ValueError(f"Output escapes checkout: {path}")
        if path.is_dir():
            for descendant in path.rglob("*"):
                if descendant.is_symlink() and not (
                    dependency_links and dependency_symlink(descendant)
                ):
                    raise ValueError(f"Refusing symlink output: {descendant}")


def prepare():
    files = (*FILES, WINDOWS)
    directories = DIRECTORIES
    validate_paths((*files, *directories, XCODE_DATA))
    for path in files:
        path.unlink(missing_ok=True)
    for path in directories:
        if path.exists():
            shutil.rmtree(path)


def prepare_windows():
    base = Path("test/Plugins/STOMP.bundle/Contents/Windows64")
    files = tuple(base / ("STOMP" + suffix) for suffix in (".4DX", ".exp", ".lib", ".pdb"))
    directory = Path("Objs/Release")
    validate_paths((*files, directory))
    for path in files:
        path.unlink(missing_ok=True)
    if directory.exists():
        shutil.rmtree(directory)


def require_file(path):
    validate_paths((path,))
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"Missing or empty native output: {path}")


def verify_resources():
    for name in ("manifest.json", "constants.xlf"):
        copied = BUNDLE / "Contents/Resources" / name
        require_file(copied)
        if copied.read_bytes() != Path(name).read_bytes():
            raise ValueError(f"Copied resource differs from source: {copied}")


def verify_windows():
    require_file(WINDOWS)
    require_file(Path("winbuild/STOMP.4DX"))
    if WINDOWS.read_bytes() != Path("winbuild/STOMP.4DX").read_bytes():
        raise ValueError("Merged Windows payload differs from downloaded artifact")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=(
        "clean-macos", "clean-windows", "verify-resources", "validate-windows",
    ))
    phase = parser.parse_args().phase
    if phase == "clean-macos":
        prepare()
    elif phase == "clean-windows":
        prepare_windows()
    elif phase == "verify-resources":
        verify_resources()
    else:
        validate_paths((WINDOWS, Path("winbuild")))
        require_file(Path("winbuild/STOMP.4DX"))
