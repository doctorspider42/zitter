"""Expose read-only named Keychain lookup through Chromium's OSCrypt provider."""

from pathlib import Path


def prepare(checkout: Path):
    directory = checkout / "components/os_crypt/async/browser"
    edits = {
        "keychain_key_provider.h": (
            ('#include "base/gtest_prod_util.h"',
             '#include <string>\n\n#include "base/gtest_prod_util.h"'),
            ("  KeychainKeyProvider();",
             "  KeychainKeyProvider();\n"
             "  KeychainKeyProvider(std::string existing_service, std::string existing_account);"),
            ("  raw_ptr<crypto::apple::KeychainV2> keychain_for_testing_ = nullptr;",
             "  raw_ptr<crypto::apple::KeychainV2> keychain_for_testing_ = nullptr;\n"
             "  std::string existing_service_;\n"
             "  std::string existing_account_;"),
        ),
        "keychain_key_provider.mm": (
            ("    crypto::apple::KeychainV2* keychain_for_testing) {",
             "    crypto::apple::KeychainV2* keychain_for_testing,\n"
             "    const std::string& existing_service,\n"
             "    const std::string& existing_account) {"),
            ("    if (base::CommandLine::ForCurrentProcess()->HasSwitch(",
             "    if (existing_service.empty() &&\n"
             "        base::CommandLine::ForCurrentProcess()->HasSwitch("),
            ("  KeychainPassword keychain_password(*keychain_to_use);\n"
             "  std::string password = keychain_password.GetPassword();",
             "  std::string password;\n"
             "  if (existing_service.empty()) {\n"
             "    KeychainPassword keychain_password(*keychain_to_use);\n"
             "    password = keychain_password.GetPassword();\n"
             "  } else {\n"
             "    // Migration can use the existing secret, but must not create it.\n"
             "    auto existing = keychain_to_use->FindGenericPassword(\n"
             "        existing_service, existing_account);\n"
             "    if (existing.has_value())\n"
             "      password.assign(existing->begin(), existing->end());\n"
             "  }"),
            ("KeychainKeyProvider::KeychainKeyProvider() = default;",
             "KeychainKeyProvider::KeychainKeyProvider() = default;\n\n"
             "KeychainKeyProvider::KeychainKeyProvider(std::string existing_service,\n"
             "                                         std::string existing_account)\n"
             "    : existing_service_(std::move(existing_service)),\n"
             "      existing_account_(std::move(existing_account)) {}"),
            ("                     keychain_for_testing_),",
             "                     keychain_for_testing_, existing_service_, existing_account_),"),
        ),
    }
    plan = {}
    for name, changes in edits.items():
        path = directory / name
        if path.is_symlink() or not path.resolve().is_relative_to(checkout):
            raise ValueError(f"Keychain provider path escapes the checkout: {name}")
        original = path.read_bytes()
        text = original.decode("utf-8")
        for before, after in changes:
            if text.count(after) == 1:
                continue
            if text.count(before) != 1:
                raise ValueError(f"Unexpected Keychain provider source: {name}")
            text = text.replace(before, after)
        plan[path] = (original, text.encode("utf-8"))
    return plan
