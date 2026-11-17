import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "check_plugin_symbols.py"


class PluginSymbolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def compile(self, name, source, *args):
        source_path = self.directory / (name + ".c")
        source_path.write_text(source)
        output = self.directory / name
        subprocess.run(
            [os.environ.get("CC", "cc"), str(source_path), "-o", str(output), *args],
            check=True, capture_output=True, text=True,
        )
        return output

    def library(self, name, source, *args):
        return self.compile(name, source, "-shared", "-fPIC", *args)

    def host(self, *args):
        return self.compile(
            "host", "#include <stdio.h>\nvoid host_api(void) {}\n"
            "int main(void) { puts(\"host\"); return 0; }\n",
            "-rdynamic", *args,
        )

    def plugin(self, source, *args):
        return self.library(
            "plugin.so", source, "-Wl,--no-as-needed", "-lc", *args,
        )

    def check_plugin(self, plugin, host, *args):
        return subprocess.run(
            [sys.executable, str(SCRIPT), str(plugin), str(host), *args],
            capture_output=True, text=True,
        )

    def test_missing_arbitrary_and_project_functions_are_rejected(self):
        host = self.host()
        plugin = self.plugin(
            "extern void random_missing(void); extern void hyprimate_missing(void);\n"
            "void entry(void) { random_missing(); hyprimate_missing(); }\n"
        )
        result = self.check_plugin(plugin, host)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("random_missing", result.stderr)
        self.assertIn("hyprimate_missing", result.stderr)

    def test_host_and_host_dependency_exports_are_accepted(self):
        self.library("libprovider.so", "void dependency_api(void) {}\n")
        host = self.host("-Wl,--no-as-needed", "-L", str(self.directory),
                         "-lprovider", "-Wl,-rpath,$ORIGIN")
        plugin = self.plugin(
            "extern void host_api(void); extern void dependency_api(void);\n"
            "void entry(void) { host_api(); dependency_api(); }\n"
        )
        result = self.check_plugin(plugin, host)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_optional_weak_undefined_is_accepted(self):
        host = self.host()
        plugin = self.plugin(
            "extern void optional_api(void) __attribute__((weak));\n"
            "void entry(void) { if (optional_api) optional_api(); }\n"
        )
        result = self.check_plugin(plugin, host)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_plugin_dependency_export_is_accepted(self):
        self.library("libprovider.so", "void plugin_dependency(void) {}\n")
        host = self.host()
        plugin = self.plugin(
            "extern void plugin_dependency(void);\n"
            "void entry(void) { plugin_dependency(); }\n",
            "-L", str(self.directory), "-lprovider", "-Wl,-rpath,$ORIGIN",
        )
        result = self.check_plugin(plugin, host)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_default_and_nondefault_versions(self):
        versions = self.directory / "versions.map"
        versions.write_text("V1 {}; V2 {} V1;\n")
        self.library(
            "libprovider.so",
            "void old_api(void) {}\nvoid new_api(void) {}\n"
            '__asm__(".symver old_api,versioned_api@V1");\n'
            '__asm__(".symver new_api,versioned_api@@V2");\n',
            f"-Wl,--version-script={versions}",
        )
        host = self.host("-Wl,--no-as-needed", "-L", str(self.directory),
                         "-lprovider", "-Wl,-rpath,$ORIGIN")
        plugin = self.plugin(
            "extern void versioned_api(void); extern void old_version(void);\n"
            '__asm__(".symver old_version,versioned_api@V1");\n'
            "void entry(void) { versioned_api(); old_version(); }\n",
            "-L", str(self.directory), "-lprovider", "-Wl,-rpath,$ORIGIN",
        )
        result = self.check_plugin(plugin, host)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        plugin = self.plugin("extern void versioned_api(void);\n"
                             "void entry(void) { versioned_api(); }\n")
        result = self.check_plugin(plugin, host)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        versions.write_text("V1 {};\n")
        self.library(
            "libprovider.so", "void implementation(void) {}\n"
            '__asm__(".symver implementation,nondefault_only@V1");\n',
            f"-Wl,--version-script={versions}",
        )
        plugin = self.plugin("extern void nondefault_only(void);\n"
                             "void entry(void) { nondefault_only(); }\n")
        result = self.check_plugin(plugin, host)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("nondefault_only", result.stderr)

    def test_missing_dependency_and_missing_nm_fail_closed(self):
        library = self.library("libprovider.so", "void provider(void) {}\n")
        host = self.host("-Wl,--no-as-needed", "-L", str(self.directory),
                         "-lprovider", "-Wl,-rpath,$ORIGIN")
        plugin = self.plugin("void entry(void) {}\n")
        result = self.check_plugin(plugin, host, "--nm", str(self.directory / "no-nm"))
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("no-nm", result.stderr)
        library.unlink()
        result = self.check_plugin(plugin, host)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("libprovider.so", result.stderr)


if __name__ == "__main__":
    unittest.main()
