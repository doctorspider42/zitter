// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <sys/stat.h>
#include <unistd.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace {

NSString* const kService = @"Zitter Content Lab Safe Storage";
NSString* const kAccount = @"Zitter Content Lab";
NSString* const kLegacyService = @"Zitter Safe Storage";
NSString* const kLegacyAccount = @"Zitter";
NSString* const kImportService = @"Chrome Safe Storage";
NSString* const kImportAccount = @"Chrome";
SecKeychainRef fixture_keychain;
bool shared_keychain;
bool legacy_writer;
bool browser_import;

bool IsGenericPassword(CFDictionaryRef query) {
  NSDictionary* attributes = (__bridge NSDictionary*)query;
  return [attributes[(__bridge NSString*)kSecClass]
      isEqual:(__bridge NSString*)kSecClassGenericPassword];
}

bool IsFixturePassword(CFDictionaryRef query) {
  NSDictionary* attributes = (__bridge NSDictionary*)query;
  return [attributes[(__bridge NSString*)kSecAttrService]
      isEqual:browser_import ? kImportService :
          shared_keychain ? kLegacyService : kService] &&
      [attributes[(__bridge NSString*)kSecAttrAccount]
          isEqual:browser_import ? kImportAccount :
              shared_keychain ? kLegacyAccount : kAccount];
}

NSMutableDictionary* ScopedQuery(CFDictionaryRef query) {
  NSMutableDictionary* attributes = [(__bridge NSDictionary*)query mutableCopy];
  attributes[(__bridge NSString*)kSecMatchSearchList] =
      @[(__bridge id)fixture_keychain];
  return attributes;
}

OSStatus CopyMatching(CFDictionaryRef query, CFTypeRef* result) {
  if (!IsGenericPassword(query))
    return SecItemCopyMatching(query, result);
  if (!fixture_keychain || !IsFixturePassword(query))
    return errSecNotAvailable;
  if (browser_import) {
    std::puts("Content browser import looked up its fixture key");
    if (std::getenv("ZITTER_CONTENT_TEST_KEYCHAIN_LOOKUP_FAILURE"))
      return errSecAuthFailed;
  }
  if (shared_keychain && !legacy_writer) {
    const char* failure = std::getenv("ZITTER_CONTENT_TEST_KEYCHAIN_LOOKUP_FAILURE");
    if (failure) {
      return std::strcmp(failure, "missing") == 0 ? errSecItemNotFound
                                                  : errSecAuthFailed;
    }
  }
  return SecItemCopyMatching((__bridge CFDictionaryRef)ScopedQuery(query), result);
}

SecAccessRef CreateFixtureAccess(const char* reader, const char* verifier) {
  if (!reader)
    return nullptr;
  auto* applications = [NSMutableArray array];
  // A null path identifies the process creating the item.
  for (const char* path : {static_cast<const char*>(nullptr), reader, verifier}) {
    if (!path && [applications count] > 0)
      continue;
    SecTrustedApplicationRef application = nullptr;
    if (SecTrustedApplicationCreateFromPath(path, &application) != noErr)
      return nullptr;
    [applications addObject:CFBridgingRelease(application)];
  }
  SecAccessRef access = nullptr;
  if (SecAccessCreate(CFSTR("Disposable Zitter Keychain fixture"),
                      (__bridge CFArrayRef)applications, &access) != noErr) {
    return nullptr;
  }
  return access;
}

OSStatus Add(CFDictionaryRef query, CFTypeRef* result) {
  if (!IsGenericPassword(query))
    return SecItemAdd(query, result);
  if (browser_import) {
    std::puts("Content browser import attempted to write its source key");
    return errSecNotAvailable;
  }
  if (shared_keychain && !legacy_writer) {
    std::puts("Content file Keychain rejected a write attempt");
    return errSecNotAvailable;
  }
  if (!fixture_keychain || !IsFixturePassword(query))
    return errSecNotAvailable;
  NSMutableDictionary* attributes = [(__bridge NSDictionary*)query mutableCopy];
  attributes[(__bridge NSString*)kSecUseKeychain] = (__bridge id)fixture_keychain;
  const char* upgrade_reader = std::getenv("ZITTER_CONTENT_TEST_KEYCHAIN_UPGRADE_READER");
  if (shared_keychain || upgrade_reader) {
    SecAccessRef access = shared_keychain
        ? CreateFixtureAccess(std::getenv("ZITTER_CONTENT_TEST_KEYCHAIN_READER"),
                              std::getenv("ZITTER_CONTENT_TEST_KEYCHAIN_VERIFIER"))
        : CreateFixtureAccess(upgrade_reader, nullptr);
    if (!access)
      return errSecNotAvailable;
    attributes[(__bridge NSString*)kSecAttrAccess] = CFBridgingRelease(access);
  }
  const OSStatus status = SecItemAdd((__bridge CFDictionaryRef)attributes, result);
  if (status == noErr)
    std::puts("Content file Keychain created its isolated password item");
  return status;
}

OSStatus Update(CFDictionaryRef query, CFDictionaryRef changes) {
  if (!IsGenericPassword(query))
    return SecItemUpdate(query, changes);
  if (browser_import) {
    std::puts("Content browser import attempted to write its source key");
    return errSecNotAvailable;
  }
  if (shared_keychain) {
    std::puts("Content file Keychain rejected a write attempt");
    return errSecNotAvailable;
  }
  if (!fixture_keychain || !IsFixturePassword(query))
    return errSecNotAvailable;
  return SecItemUpdate((__bridge CFDictionaryRef)ScopedQuery(query), changes);
}

#define INTERPOSE(replacement, original)                                  \
  __attribute__((used, section("__DATA,__interpose"))) static const struct { \
    const void* replacement_function;                                      \
    const void* original_function;                                         \
  } interpose_##original = {reinterpret_cast<const void*>(&replacement),    \
                            reinterpret_cast<const void*>(&original)}

INTERPOSE(CopyMatching, SecItemCopyMatching);
INTERPOSE(Add, SecItemAdd);
INTERPOSE(Update, SecItemUpdate);

}  // namespace

extern "C" __attribute__((visibility("default"))) bool
ZitterInstallFileKeychainFixture(const char* root) {
  const char* unlock = std::getenv("ZITTER_CONTENT_TEST_KEYCHAIN_UNLOCK");
  if (!unlock || std::strlen(unlock) != 64 || fixture_keychain)
    return false;
  if (std::getenv("ZITTER_CONTENT_TEST_KEYCHAIN_UPGRADE_READER") &&
      SecKeychainSetUserInteractionAllowed(false) != noErr) {
    return false;
  }
  if (const char* shared_root = std::getenv("ZITTER_CONTENT_TEST_SHARED_KEYCHAIN_ROOT")) {
    struct stat info;
    if (lstat(shared_root, &info) != 0 || !S_ISDIR(info.st_mode) ||
        info.st_uid != getuid() || (info.st_mode & 077) != 0 ||
        SecKeychainSetUserInteractionAllowed(false) != noErr) {
      return false;
    }
    shared_keychain = true;
    root = shared_root;
  }
  NSString* path = [[NSString stringWithUTF8String:root]
      stringByAppendingPathComponent:@"fixture.keychain-db"];
  const char* file = path.fileSystemRepresentation;
  struct stat info;
  if (lstat(file, &info) == 0) {
    return S_ISREG(info.st_mode) &&
           SecKeychainOpen(file, &fixture_keychain) == noErr &&
           SecKeychainUnlock(fixture_keychain, 64, unlock, true) == noErr;
  }
  if (errno != ENOENT || (shared_keychain && !legacy_writer) ||
      SecKeychainCreate(file, 64, unlock, false, nullptr, &fixture_keychain) != noErr) {
    return false;
  }
  if (browser_import) {
    NSDictionary* attributes = @{
      (__bridge NSString*)kSecClass : (__bridge NSString*)kSecClassGenericPassword,
      (__bridge NSString*)kSecAttrService : kImportService,
      (__bridge NSString*)kSecAttrAccount : kImportAccount,
      (__bridge NSString*)kSecUseKeychain : (__bridge id)fixture_keychain,
      (__bridge NSString*)kSecValueData :
          [@"zitter-import-fixture" dataUsingEncoding:NSUTF8StringEncoding],
    };
    return SecItemAdd((__bridge CFDictionaryRef)attributes, nullptr) == noErr;
  }
  if (!std::getenv("ZITTER_CONTENT_TEST_SEED_LEGACY_KEY"))
    return true;
  NSDictionary* attributes = @{
    (__bridge NSString*)kSecClass : (__bridge NSString*)kSecClassGenericPassword,
    (__bridge NSString*)kSecAttrService : (shared_keychain ? kLegacyService : kService),
    (__bridge NSString*)kSecAttrAccount : (shared_keychain ? kLegacyAccount : kAccount),
    // The pinned Chromium fake Keychain uses this password for legacy fixtures.
    (__bridge NSString*)kSecValueData : [@"mock_password" dataUsingEncoding:NSUTF8StringEncoding],
  };
  return Add((__bridge CFDictionaryRef)attributes, nullptr) == noErr;
}

extern "C" __attribute__((visibility("default"))) bool
ZitterInstallBrowserImportKeychainFixture(const char* root) {
  if (std::getenv("ZITTER_CONTENT_TEST_SHARED_KEYCHAIN_ROOT") ||
      SecKeychainSetUserInteractionAllowed(false) != noErr)
    return false;
  browser_import = true;
  return ZitterInstallFileKeychainFixture(root);
}

__attribute__((constructor)) static void InstallLegacyFileKeychainFixture() {
  if (std::strcmp(getprogname(), "Chromium") != 0 ||
      !std::getenv("ZITTER_CONTENT_TEST_OPEN_LEGACY_KEYCHAIN")) {
    return;
  }
  const char* root = std::getenv("ZITTER_CONTENT_TEST_SHARED_KEYCHAIN_ROOT");
  legacy_writer = true;
  if (!root || !ZitterInstallFileKeychainFixture(root)) {
    std::fputs("Could not initialize the legacy file Keychain fixture\n", stderr);
    std::_Exit(1);
  }
}
