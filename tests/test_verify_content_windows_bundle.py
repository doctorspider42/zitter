import json
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

with mock.patch.object(sys, "path", [str(Path(__file__).resolve().parents[1] / "tools"), *sys.path]):
    import verify_content_windows_bundle as bundle


REQUIRED = "./zitter_content.exe\n./zitter_content.pak\n./zitter_assistant.exe\nicudtl.dat\n"
PE_HEADER = "  Machine: IMAGE_FILE_MACHINE_AMD64 (0x8664)\nImageOptionalHeader {\n}\n"


class WindowsContentBundleTest(unittest.TestCase):
    def test_manifest_ignores_source_and_debug_paths(self):
        manifest = REQUIRED + (
            "../../source/third_party.dll\n"
            "gen/generated.dll\n"
            "./DBGHELP.DLL\n"
            "./libEGL.dll\n"
            "./locales/en-US.pak\n"
        )
        self.assertEqual(bundle.select_runtime_files(manifest), [
            "icudtl.dat", "libEGL.dll", "locales/en-US.pak",
            "zitter_assistant.exe", "zitter_content.exe", "zitter_content.pak",
        ])

    def test_manifest_requires_packaged_assistant(self):
        with self.assertRaisesRegex(ValueError, "missing zitter_assistant.exe"):
            bundle.select_runtime_files(REQUIRED.replace("./zitter_assistant.exe\n", ""))

    def test_case_collision_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "collide"):
            bundle.select_runtime_files(REQUIRED + "Example.dll\nexample.dll\n")

    def test_manifest_rejects_windows_device_names(self):
        with self.assertRaisesRegex(ValueError, "Reserved Windows runtime path"):
            bundle.select_runtime_files(REQUIRED + "COM1.backup.dll\n")

    def test_existing_archive_is_extracted_without_rewriting_it(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "portable.zip"
            with zipfile.ZipFile(archive, "w") as package:
                package.writestr(bundle.ARCHIVE_PREFIX, "")
                package.writestr(bundle.ARCHIVE_PREFIX + bundle.EXECUTABLE, b"fixture")
                package.writestr(bundle.ARCHIVE_PREFIX + "LICENSE", b"notice")
            original = archive.read_bytes()
            bundle.extract_bundle(archive, root / "extract", [bundle.EXECUTABLE], {"LICENSE"})
            self.assertEqual(archive.read_bytes(), original)
            executable = root / "extract" / bundle.ARCHIVE_PREFIX / bundle.EXECUTABLE
            self.assertEqual(executable.read_bytes(), b"fixture")

    def test_existing_archive_refuses_path_aliases_and_links_before_extracting(self):
        link = zipfile.ZipInfo(bundle.ARCHIVE_PREFIX + "LICENSE")
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        for bad in (bundle.ARCHIVE_PREFIX + "../outside",
                    bundle.ARCHIVE_PREFIX + "ZITTER_CONTENT.EXE", link):
            with self.subTest(entry=bad), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                archive = root / "portable.zip"
                with zipfile.ZipFile(archive, "w") as package:
                    package.writestr(bundle.ARCHIVE_PREFIX + bundle.EXECUTABLE, b"fixture")
                    package.writestr(bad, b"unexpected")
                extracted = root / "extract"
                with self.assertRaisesRegex(ValueError, "Unexpected or duplicate bundle entry"):
                    bundle.extract_bundle(archive, extracted, [bundle.EXECUTABLE], {"LICENSE"})
                self.assertFalse(extracted.exists())

    def test_signature_check_requires_every_pe_image_to_pass(self):
        layout = Path("/fixture") / "Zitter Content Lab"
        signtool = Path("/Windows SDK") / "signtool.exe"
        ok = mock.Mock(returncode=0)
        warning = mock.Mock(returncode=2, stdout="warning", stderr="")
        with mock.patch.object(bundle.subprocess, "run", side_effect=[ok, warning]) as run:
            with self.assertRaisesRegex(ValueError, "Authenticode verification failed for helper.dll"):
                bundle.verify_signatures(layout, ["resources.pak", bundle.EXECUTABLE, "helper.dll"],
                                         signtool)
        self.assertEqual([call.args[0] for call in run.call_args_list], [
            [signtool, "verify", "/pa", "/all", layout / bundle.EXECUTABLE],
            [signtool, "verify", "/pa", "/all", layout / "helper.dll"],
        ])

    def test_delay_import_requires_bundled_runtime(self):
        output = PE_HEADER + "DelayImport {\n  Name: VCRUNTIME140.dll\n}\n"
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            system32 = root / "system32"
            system32.mkdir()
            (system32 / "vcruntime140.dll").touch()
            with mock.patch.object(bundle.subprocess, "run", return_value=mock.Mock(stdout=output)):
                with self.assertRaisesRegex(ValueError, "DLL outside the package"):
                    bundle.validate_imports(root, ["zitter_content.exe"], "llvm-readobj", system32)
                bundle.validate_imports(
                    root, ["zitter_content.exe", "vcruntime140.dll"], "llvm-readobj", system32,
                )

    def test_swiftshader_must_resolve_to_packaged_sibling(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaisesRegex(ValueError, "must ship together"):
                bundle.validate_swiftshader(root)
            (root / "vk_swiftshader.dll").touch()
            with self.assertRaisesRegex(ValueError, "must ship together"):
                bundle.validate_swiftshader(root)
            manifest = root / "vk_swiftshader_icd.json"
            manifest.write_text(json.dumps({"ICD": {"library_path": ".\\vk_swiftshader.dll"}}))
            bundle.validate_swiftshader(root)
            manifest.write_text(json.dumps({"ICD": {"library_path": "..\\vk_swiftshader.dll"}}))
            with self.assertRaisesRegex(ValueError, "outside its packaged DLL"):
                bundle.validate_swiftshader(root)

    def test_sandbox_guards_reject_profile_creation_even_with_error_exit(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            executable = root / bundle.EXECUTABLE
            rejected = mock.Mock(returncode=1, stdout="", stderr="")
            with mock.patch.object(bundle.subprocess, "run", return_value=rejected) as run:
                bundle.verify_sandbox_guards(executable, root, {"PATH": "system"})
            self.assertEqual([call.args[0][-1] for call in run.call_args_list], [
                "--no-sandbox", "--disable-gpu-sandbox", "--single-process", "--in-process-gpu",
                f"--browser-subprocess-path={executable}",
                "--renderer-cmd-prefix", "--gpu-launcher", "--utility-cmd-prefix",
            ])

            def creates_profile(*args, env, **kwargs):
                (Path(env["TEMP"]) / "profile").mkdir()
                return rejected

            with mock.patch.object(bundle.subprocess, "run", side_effect=creates_profile):
                with self.assertRaisesRegex(ValueError, "Sandbox startup guard failed"):
                    bundle.verify_sandbox_guards(executable, root, {"PATH": "system"})

    def test_devtools_smoke_requires_its_own_marker_from_the_packaged_executable(self):
        root = Path("/fixture")
        layout = root / "Zitter Content Lab"
        env = {"PATH": "C:/Windows/System32"}
        standalone = mock.Mock(returncode=0, stdout="Zitter Content Lab smoke passed\n")
        workspace = mock.Mock(returncode=0, stdout="Zitter Content workspace smoke passed\n",
                              stderr="")
        with mock.patch.object(bundle.subprocess, "run",
                               side_effect=[standalone, workspace, workspace]) as run:
            with self.assertRaisesRegex(ValueError, "devtools-smoke-test"):
                bundle.smoke(layout, root, env)
        self.assertEqual(run.call_args.args[0], [
            layout / bundle.EXECUTABLE, "--zitter-content-workspace-smoke-test",
            "--zitter-content-devtools-smoke-test", "--zitter-content-workspace-theme=dark",
            "--no-proxy-server",
        ])
        self.assertEqual(run.call_args.kwargs["cwd"], root)
        self.assertEqual(run.call_args.kwargs["env"], env)

    def test_runtime_probes_use_the_extracted_app_and_restricted_environment(self):
        root = Path("/fixture")
        executable = root / "Zitter Content Lab" / bundle.EXECUTABLE
        env = {"PATH": "C:/Windows/System32"}
        with mock.patch.object(bundle.subprocess, "run", return_value=mock.Mock(stdout="")) as run:
            bundle.verify_runtime_probes(executable, root, env)
        self.assertEqual([(call.args[0][1].name, call.args[0][3:]) for call in run.call_args_list], [
            ("verify_content_workspace_session.py", []),
            ("verify_content_workspace_session.py", ["--private-windows"]),
            ("verify_content_fingerprint.py", ["--workspace", "--cross-process-frame"]),
            ("verify_content_scriptlets.py", ["--private-workspace"]),
            ("verify_content_downloads.py", ["--require-quarantine"]),
        ])
        for call in run.call_args_list:
            self.assertEqual(call.args[0][2], executable)
            self.assertEqual(call.kwargs["cwd"], root)
            self.assertEqual(call.kwargs["env"], env)
            self.assertTrue(call.kwargs["check"])

    def test_extension_probes_keep_signing_tools_outside_the_runtime_path(self):
        root = Path("/fixture")
        executable = root / "Zitter Content Lab" / bundle.EXECUTABLE
        signer = root / "build" / "crx3_build_action.exe"
        openssl = root / "OpenSSL" / "openssl.exe"
        env = {"PATH": "C:/Windows/System32;C:/Windows"}
        with mock.patch.object(bundle.subprocess, "run", return_value=mock.Mock(stdout="")) as run:
            bundle.verify_runtime_probes(executable, root, env, signer=signer, openssl=openssl)
        self.assertEqual(run.call_count, 10)
        self.assertEqual(
            [(call.args[0][1].name, call.args[0][2:]) for call in run.call_args_list[5:7]],
            [
                ("verify_content_tls.py", [executable, "--openssl", openssl, "--virtual-webauthn"]),
                ("verify_content_downloads.py", [
                    executable, "--transport", "--require-quarantine", "--openssl", openssl,
                ]),
            ],
        )
        for call in run.call_args_list[5:]:
            self.assertEqual(call.kwargs["cwd"], root)
            self.assertEqual(call.kwargs["env"], env)
            self.assertTrue(call.kwargs["check"])
        for profile, call in zip(("work", "personal"), run.call_args_list[7:9]):
            self.assertEqual(call.args[0][1].name, "verify_content_workspace_session.py")
            self.assertEqual(call.args[0][2:], [
                executable, "--extension-registry", "--extension-module-worker",
                "--extension-profile", profile, "--crx-signer", signer, "--openssl", openssl,
            ])

        paired = run.call_args_list[9].args[0]
        self.assertEqual(paired[1].name, "verify_content_workspace_session.py")
        self.assertEqual(paired[2:], [
            executable, "--extension-pair", "--crx-signer", signer, "--openssl", openssl,
        ])

    def test_migration_probe_receives_the_extracted_app_and_explicit_node(self):
        root = Path("/fixture")
        executable = root / "Zitter Content Lab" / bundle.EXECUTABLE
        legacy = root / "previous release" / "zitter.exe"
        node = root / "Node.js" / "node.exe"
        env = {"PATH": "C:/Windows/System32;C:/Windows"}
        with mock.patch.object(bundle.subprocess, "run", return_value=mock.Mock(stdout="")) as run:
            bundle.verify_runtime_probes(executable, root, env, legacy=legacy, node=node)
        self.assertEqual(run.call_count, 6)
        migration = run.call_args
        self.assertEqual(migration.args[0][1].name, "verify_content_windows_migration.py")
        self.assertEqual(migration.args[0][2:], [legacy, executable, "--node", node])
        self.assertEqual(migration.kwargs["cwd"], root)
        self.assertEqual(migration.kwargs["env"], env)
        self.assertTrue(migration.kwargs["check"])


if __name__ == "__main__":
    unittest.main()
