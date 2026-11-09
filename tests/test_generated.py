"""The final publication exemption is smaller than the bundle or build roots."""

import unittest

from helper_support import source
import native


class GeneratedOutputTests(unittest.TestCase):
    def test_exact_native_files_and_controlled_directory_descendants(self):
        expected = (
            *map(str, native.FILES),
            str(native.WINDOWS),
            *map(str, native.DEPENDENCY_FILES),
            *(str(path / "generated-output") for path in native.DEPENDENCY_DIRECTORIES),
            "build/XCBuildData/build.db",
            "build/XCBuildData/build.db-wal",
            "build/XCBuildData/build.db-shm",
            "build/XCBuildData/0123456789abcdef0123456789abcdef.xcbuilddata/build-request.json",
            "build/STOMP.build/Objects-normal/arm64/main.o",
            "build/Default/STOMP.build/build.db",
            "test/Plugins/STOMP.build/Objects/file.o",
            "test/Plugins/Default/STOMP.build/Objects/file.o",
        )
        for path in expected:
            with self.subTest(path=path):
                self.assertTrue(native.generated(path))

    def test_every_resource_source_or_unproven_output_stays_protected(self):
        excluded = (
            "test/Plugins/STOMP.bundle/Contents/Windows/STOMP.4DX",
            "test/Plugins/STOMP.bundle/Contents/Windows64/STOMP.4DX",
            "test/Plugins/STOMP.bundle/Contents/MacOS/runtime.dylib",
            "test/Plugins/STOMP.bundle/Contents/QuickLook/Thumbnail.png",
            "test/Plugins/STOMP.bundle/Contents/Info.plist",
            "apr-1.5.2-evil/configure", "apr-arm64-evil/build.mk",
            "out-arm64-evil/include/apr.h", "jsoncpp-build-evil/libjsoncpp.a",
            "test/Plugins/Default/keep", "build/XCBuildData-evil/build.db",
            "build/unexpected-source.cpp",
            "build/XCBuildData/unexpected-source.cpp",
            "build/XCBuildData/build.db-evil",
            "build/XCBuildData/evil.xcbuilddata/build-request.json",
            "build/STOMP.build-evil/injected.cpp",
            "test/Plugins/STOMP.bundle/Contents/_CodeSignature-evil/file",
            "test/Plugins/STOMP.bundle/Contents/Info.plist.backup",
            *map(str, source.SOURCE_PATHS),
        )
        for path in excluded:
            with self.subTest(path=path):
                self.assertFalse(native.generated(path))
