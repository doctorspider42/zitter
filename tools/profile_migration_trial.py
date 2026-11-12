#!/usr/bin/env python3
"""Prepare an offline Zitter profile migration trial and a fresh rollback copy."""

import argparse
import base64
from bisect import bisect_left
from contextlib import closing
import hashlib
from io import BytesIO
import ipaddress
import json
import math
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import shutil
import sqlite3
import stat
import subprocess
import sys
from tempfile import TemporaryDirectory
from urllib.parse import urlsplit
from uuid import NAMESPACE_URL, UUID, uuid5


PROFILE_DIRECTORY = re.compile(
    r"(?:ZitterWork|ZitterPersonal|ZitterProfile-"
    r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12})"
)
ROOT_LOCKS = {"SingletonLock", "SingletonCookie", "SingletonSocket", "lockfile"}
MANIFEST = "baseline-manifest.json"
EXTENSION_ID = re.compile(r"[a-p]{32}")
EXTENSION_STORAGE = "Local Extension Settings"
EXTENSION_STATE = "Extension State"
LEGACY_EXTENSION_RECORD = "ContentLegacyExtension.json"
LEGACY_EXTENSION_RECORDS = "ContentLegacyExtensions"
MAX_LEGACY_EXTENSION_RECORD_BYTES = 16 * 1024
SQLITE_SUFFIXES = ("", "-journal", "-wal", "-shm")
COOKIE_FILES = tuple("Cookies" + suffix for suffix in SQLITE_SUFFIXES)
HISTORY_FILES = tuple("History" + suffix for suffix in SQLITE_SUFFIXES)
FAVICON_FILES = tuple("Favicons" + suffix for suffix in SQLITE_SUFFIXES)
FAVICON_SCHEMA_VERSION = 9
LOGIN_FILES = tuple("Login Data" + suffix for suffix in SQLITE_SUFFIXES)
HISTORY_SCHEMA_VERSION = 70
IMPORTED_DOWNLOAD_MARKER = "zitter-imported-download-v1"
PASSWORD_PREFS = "Content Password Preferences"
ADBLOCK_PREFS = "Content Ad Blocking Preferences"
PASSWORD_SAVING_PREF = "credentials_enable_service"
LOCAL_STORAGE = "Local Storage"
WINDOWS_LOCAL_STATE = "ContentWorkspaceLocalState.json"
PAGE_ZOOM_PREFS = "Content Page Zoom Preferences"
PREPARED_WORKSPACE_MARKER = "ContentWorkspaceLayout"
WORKSPACE_SESSION_FILES = {
    "workspace": "ContentWorkspaceSession.json",
    "windows-dpapi": "ContentWorkspaceSession.json",
    "keychain": "ContentKeychainWorkspaceSession.json",
    "mock-keychain": "ContentMockKeychainWorkspaceSession.json",
}
WORKSPACE_SETTINGS_FILES = {
    "workspace": "WorkspaceSettings.json",
    "windows-dpapi": "WorkspaceSettings.json",
    "keychain": "KeychainWorkspaceSettings.json",
    "mock-keychain": "MockKeychainWorkspaceSettings.json",
}
WORKSPACE_PROFILES = ("work", "personal")
MAX_SESSION_BYTES = 16 * 1024 * 1024
ASSISTANT_SESSIONS_FILE = "ZitterAssistantSessions.json"
MAX_ASSISTANT_SESSIONS_BYTES = 64 * 1024 * 1024
MAX_ASSISTANT_CONVERSATIONS = 200
MAX_TIMELINE_BYTES = 64 * 1024 * 1024
MAX_SESSION_TABS = 500
MAX_TAB_URL_BYTES = 32768
MAX_TAB_TITLE_UNITS = 8192
MAX_SPLIT_PANES = 4

CLIPBOARD_PREFS = "Content Site Permissions"
EXACT_SITE_PATTERN = re.compile(r"(https?)://(\[[0-9a-f:]+\]|[a-z0-9.-]+):([1-9][0-9]{0,4}),\*")
DNS_LABEL = re.compile(r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?")


def is_link_info(info):
    return stat.S_ISLNK(info.st_mode) or bool(
        getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
    )


def is_linked_path(path):
    try:
        return is_link_info(path.lstat())
    except FileNotFoundError:
        return False


def is_regular_file(info):
    return stat.S_ISREG(info.st_mode) and not is_link_info(info)


def regular_file_entry(path, info):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(block)
    return {"kind": "file", "mode": stat.S_IMODE(info.st_mode),
            "size": info.st_size, "sha256": digest.hexdigest()}


def inventory(root):
    """Record regular files, including database sidecars, and directory modes."""
    entries = {}

    def visit(directory):
        for path in sorted(directory.iterdir()):
            relative = path.relative_to(root).as_posix()
            if directory == root and path.name in ROOT_LOCKS:
                raise ValueError(f"Source contains a browser lock: {relative}")
            info = path.lstat()
            if is_link_info(info):
                raise ValueError(f"Source contains a link or reparse point: {relative}")
            if stat.S_ISDIR(info.st_mode):
                entries[relative] = {"kind": "directory", "mode": stat.S_IMODE(info.st_mode)}
                visit(path)
            elif stat.S_ISREG(info.st_mode):
                entries[relative] = regular_file_entry(path, info)
            else:
                raise ValueError(f"Source contains a link or special file: {relative}")

    if is_linked_path(root) or not root.is_dir():
        raise ValueError(f"Expected a real directory: {root}")
    visit(root)
    return entries


def profile_mapping(root):
    config = json.loads((root / "ZitterWorkspaces.json").read_text(encoding="utf-8"))
    local_state = json.loads((root / "Local State").read_text(encoding="utf-8"))
    if not isinstance(config, dict) or not isinstance(local_state, dict):
        raise ValueError("Expected object-valued ZitterWorkspaces.json and Local State")

    profiles = config.get("profiles", [
        {"id": "work-profile", "name": "Work", "directory": "ZitterWork"},
        {"id": "personal-profile", "name": "Personal", "directory": "ZitterPersonal"},
    ])
    if not isinstance(profiles, list) or not profiles:
        raise ValueError("Expected at least one profile")
    directories = set()
    mapping = {}
    for profile in profiles:
        if not isinstance(profile, dict):
            raise ValueError("Invalid profile entry")
        identifier = profile.get("id")
        name = profile.get("name")
        directory = profile.get("directory")
        if (not isinstance(identifier, str) or not identifier or identifier in mapping
                or not isinstance(name, str) or not name.strip()
                or not isinstance(directory, str)
                or not PROFILE_DIRECTORY.fullmatch(directory)
                or directory in directories or not (root / directory).is_dir()):
            raise ValueError("Invalid, duplicate or missing profile directory")
        mapping[identifier] = directory
        directories.add(directory)

    workspaces = config.get("workspaces")
    if not isinstance(workspaces, list) or not workspaces:
        raise ValueError("Expected at least one workspace")
    associations = {}
    for workspace in workspaces:
        if not isinstance(workspace, dict):
            raise ValueError("Invalid workspace entry")
        identifier = workspace.get("id")
        name = workspace.get("name")
        profile = workspace.get("profile_id")
        if (not isinstance(identifier, str) or not identifier or identifier in associations
                or not isinstance(name, str) or not name
                or not isinstance(profile, str) or profile not in mapping):
            raise ValueError("Invalid workspace or unknown workspace profile")
        associations[identifier] = profile
    return {"profiles": mapping, "workspaces": associations}


def separate_destination(source, destination):
    if is_linked_path(destination) or destination.exists():
        raise ValueError(f"Destination already exists: {destination}")
    destination = destination.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("Source and destination must be separate directories")
    return destination


def copy_verified(source, destination, expected):
    def reject_links(directory, names):
        directory = Path(directory)
        if is_linked_path(directory) or any(is_linked_path(directory / name) for name in names):
            raise ValueError(f"The offline copy encountered a link or reparse point in {directory}")
        return ()

    if is_linked_path(source) or is_linked_path(destination):
        raise ValueError("The profile snapshot copy encountered a linked root")
    # copytree can recurse into Windows junctions even with symlinks=True.
    shutil.copytree(source, destination, symlinks=True, ignore=reject_links, dirs_exist_ok=True)
    if inventory(destination) != expected or inventory(source) != expected:
        raise ValueError("The profile snapshot changed while it was copied")


def prepare(source, trial):
    if is_linked_path(source):
        raise ValueError("The source root must not be a link or reparse point")
    source = source.resolve()
    trial = separate_destination(source, trial)
    expected = inventory(source)

    trial.mkdir(mode=0o700)
    try:
        baseline = trial / "baseline"
        copy_verified(source, baseline, expected)
        mapping = profile_mapping(baseline)
        copy_verified(baseline, trial / "candidate", expected)
        manifest = {"version": 1, "layout": mapping, "entries": expected}
        (trial / MANIFEST).write_text(
            json.dumps(manifest, ensure_ascii=True, indent=2) + "\n", encoding="utf-8"
        )
    except BaseException:
        shutil.rmtree(trial)
        raise
    return len(mapping["profiles"])


def read_trial_manifest(trial):
    manifest_path = trial / MANIFEST
    if is_linked_path(manifest_path):
        raise ValueError("The baseline manifest must not be a link or reparse point")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("version") != 1:
        raise ValueError("Unsupported baseline manifest")
    return manifest


def rollback(trial, destination):
    if is_linked_path(trial):
        raise ValueError("The trial root must not be a link or reparse point")
    trial = trial.resolve()
    destination = separate_destination(trial, destination)
    manifest = read_trial_manifest(trial)
    baseline = trial / "baseline"
    expected = manifest.get("entries")
    if inventory(baseline) != expected or profile_mapping(baseline) != manifest.get("layout"):
        raise ValueError("The baseline has changed. Rollback was refused.")

    destination.mkdir(mode=0o700)
    try:
        copy_verified(baseline, destination, expected)
    except BaseException:
        shutil.rmtree(destination)
        raise


def legacy_extension_record(candidate, profile_dir, extension_id, entries):
    records = []
    for name in ("Secure Preferences", "Preferences"):
        relative = Path(profile_dir, name)
        expected = entries.get(relative.as_posix())
        if not expected:
            continue
        path = candidate / relative
        if (
            is_linked_path(path) or expected.get("kind") != "file"
            or path.stat().st_size != expected.get("size")
            or path.stat().st_size > 16 * 1024 * 1024
        ):
            raise ValueError("Invalid candidate extension preferences file")
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected.get("sha256"):
            raise ValueError("The candidate extension preferences changed after preparation")
        document = json.loads(data)
        extensions = document.get("extensions", {}) if isinstance(document, dict) else None
        settings = extensions.get("settings", {}) if isinstance(extensions, dict) else None
        if not isinstance(settings, dict):
            raise ValueError("Invalid candidate extension preferences")
        record = settings.get(extension_id)
        if record is not None:
            records.append(record)
    if len(records) != 1 or not isinstance(records[0], dict):
        raise ValueError("Expected one saved legacy extension record")
    record = records[0]
    manifest = record.get("manifest", {})
    if not isinstance(manifest, dict):
        raise ValueError("Invalid saved legacy extension manifest")
    sender_policy = {}
    if "externally_connectable" in manifest:
        external = manifest["externally_connectable"]
        if not isinstance(external, dict):
            raise ValueError("Invalid legacy extension external messaging policy")
        ids = external.get("ids")
        if (external.keys() - {"ids", "matches", "accepts_tls_channel_id"}
                or external.get("matches", []) != []
                or external.get("accepts_tls_channel_id", False) is not False
                or not isinstance(ids, list) or not ids
                or any(not isinstance(value, str) or
                       (value != "*" and not EXTENSION_ID.fullmatch(value)) for value in ids)):
            raise ValueError("Unsupported legacy extension external messaging policy")
        sender_policy["external_extension_ids"] = ["*"] if "*" in ids else sorted(set(ids))
    reasons = record.get("disable_reasons")
    if (
        not isinstance(reasons, list)
        or any(type(reason) is not int or reason <= 0 for reason in reasons)
        or record.get("incognito", False) is not False
        or record.get("withholding_permissions") is not False
        or (record.get("state", 1) != 1 and not reasons)
    ):
        raise ValueError("Unsupported legacy extension permissions or state")
    permissions = {}
    expected_keys = {"api", "manifest_permissions", "explicit_host", "scriptable_host"}
    for name in ("active_permissions", "granted_permissions"):
        value = record.get(name)
        if not isinstance(value, dict) or value.keys() != expected_keys or any(
            not isinstance(entries, list) or any(not isinstance(entry, str) for entry in entries)
            for entries in value.values()
        ):
            raise ValueError("Unsupported legacy extension grant representation")
        permissions[name] = value
    return {
        "version": 2 if sender_policy else 1,
        "extension_id": extension_id,
        "disable_reasons": reasons,
        "withholding_permissions": False,
        **permissions,
        **sender_policy,
    }


def candidate_profile(trial, profile_id, manifest):
    candidate = trial / "candidate"
    if is_linked_path(candidate) or any(
        is_linked_path(candidate / name)
        for name in ("ZitterWorkspaces.json", "Local State")
    ):
        raise ValueError("The candidate has a linked root or profile mapping")
    layout = profile_mapping(candidate)
    if layout != manifest.get("layout") or profile_id not in layout["profiles"]:
        raise ValueError("Unknown profile or changed candidate mapping")
    profile = candidate / layout["profiles"][profile_id]
    if is_linked_path(profile) or profile.resolve() != profile:
        raise ValueError("The candidate profile path contains a link")
    return profile


def exact_site_origin(pattern, permission, require_secure):
    match = EXACT_SITE_PATTERN.fullmatch(pattern)
    if not match:
        raise ValueError(f"Unsupported {permission} rule scope: {pattern!r}")
    scheme, host, port = match.groups()
    if int(port) > 65535:
        raise ValueError(f"Invalid {permission} rule port: {pattern!r}")

    address = None
    try:
        address = ipaddress.ip_address(host.strip("[]"))
    except ValueError:
        labels = host.split(".")
        if (len(host) > 253 or not all(DNS_LABEL.fullmatch(label) for label in labels)
                or re.fullmatch(r"(?:[0-9]+|0x[0-9a-f]*)", labels[-1])):
            raise ValueError(f"Invalid {permission} rule host: {pattern!r}") from None
        if any(label.startswith("xn--") for label in labels):
            try:
                host.encode("ascii").decode("idna")
            except UnicodeError as error:
                raise ValueError(f"Invalid {permission} rule IDN: {pattern!r}") from error
    if address is not None:
        canonical = f"[{address.compressed}]" if address.version == 6 else address.compressed
        if host != canonical:
            raise ValueError(f"Noncanonical {permission} rule host: {pattern!r}")
    if require_secure and scheme == "http" and not (
        (address is not None and address.is_loopback)
        or host == "localhost" or host.endswith(".localhost")
    ):
        raise ValueError(f"Insecure {permission} rule: {pattern!r}")

    default_port = "443" if scheme == "https" else "80"
    suffix = "" if port == default_port else ":" + port
    return f"{scheme}://{host}{suffix}"


SITE_RULE_METADATA = {
    "setting", "last_modified", "last_used", "last_visit", "expiration",
    "lifetime", "model", "autorevocation_bypassed_by_user",
    "decided_by_related_website_sets",
}


def chromium_timestamp(value):
    if isinstance(value, str) and re.fullmatch(r"[+-]?[0-9]{1,19}", value):
        parsed = int(value)
        if -(1 << 63) <= parsed < (1 << 63):
            return str(parsed)
    return None


def clipboard_rules_from_preferences(document, times=None):
    def object_at(parent, key):
        value = parent.get(key, {})
        if not isinstance(value, dict):
            raise ValueError(f"Invalid legacy Clipboard preference object: {key}")
        return value

    if not isinstance(document, dict):
        raise ValueError("Invalid legacy Preferences object")
    profile = object_at(document, "profile")
    defaults = object_at(profile, "default_content_setting_values")
    default = defaults.get("clipboard", 3)
    if type(default) is not int or default not in (0, 3):
        raise ValueError("Unsupported legacy default Clipboard setting")
    managed = object_at(profile, "managed_default_content_settings")
    if ("clipboard" in managed or
            "managed_clipboard_allowed_for_urls" in profile or
            "managed_clipboard_blocked_for_urls" in profile):
        raise ValueError("Managed Clipboard settings require a policy migration")

    content_settings = object_at(profile, "content_settings")
    exceptions = object_at(content_settings, "exceptions")
    rules = object_at(exceptions, "clipboard")
    converted = {}
    for pattern, record in rules.items():
        origin = exact_site_origin(pattern, "Clipboard", require_secure=True)
        if origin in converted:
            raise ValueError(f"Duplicate legacy Clipboard origin: {origin}")
        if not isinstance(record, dict) or type(record.get("setting")) is not int:
            raise ValueError(f"Malformed legacy Clipboard rule: {pattern!r}")
        if record.keys() - SITE_RULE_METADATA:
            raise ValueError(f"Unknown legacy Clipboard rule metadata: {pattern!r}")
        model = record.get("model", 0)
        if (type(model) is not int or model != 0
                or record.get("expiration", "0") != "0"
                or record.get("lifetime", "0") != "0"):
            raise ValueError(f"Temporary or expiring legacy Clipboard rule: {pattern!r}")
        if record.get("last_visit", "0") != "0":
            raise ValueError(f"Auto-revocable legacy Clipboard rule: {pattern!r}")
        if record.get("decided_by_related_website_sets", False) is not False:
            raise ValueError(f"Related-site Clipboard rule requires review: {pattern!r}")
        if record["setting"] not in (1, 2, 3):
            raise ValueError(f"Unsupported legacy Clipboard decision: {pattern!r}")
        converted[origin] = record["setting"]
        timestamp = chromium_timestamp(record.get("last_modified"))
        if times is not None and record["setting"] != 3 and timestamp not in (None, "0"):
            times[origin] = timestamp

    result = {origin: setting for origin, setting in converted.items() if setting != 3}
    if len(result) > 256:
        raise ValueError("Too many rules for the Content Clipboard preferences")
    return result


def popup_settings_from_preferences(document, times=None):
    if not isinstance(document, dict):
        raise ValueError("Invalid legacy Preferences object")

    def object_at(parent, key):
        value = parent.get(key, {})
        if not isinstance(value, dict):
            raise ValueError(f"Invalid legacy popup preference object: {key}")
        return value

    profile = object_at(document, "profile")
    defaults = object_at(profile, "default_content_setting_values")
    default = defaults.get("popups", 2)
    if type(default) is not int or default not in (0, 1, 2):
        raise ValueError("Unsupported legacy default popup setting")
    managed = object_at(profile, "managed_default_content_settings")
    if ("popups" in managed or "managed_popups_allowed_for_urls" in profile
            or "managed_popups_blocked_for_urls" in profile):
        raise ValueError("Managed popup settings require a policy migration")

    settings = object_at(profile, "content_settings")
    exceptions = object_at(settings, "exceptions")
    converted = {}
    for pattern, record in object_at(exceptions, "popups").items():
        origin = exact_site_origin(pattern, "popup", require_secure=False)
        if origin in converted or len(origin) > 512:
            raise ValueError(f"Duplicate or oversized legacy popup origin: {origin}")
        if (not isinstance(record, dict) or type(record.get("setting")) is not int
                or record["setting"] not in (1, 2)):
            raise ValueError(f"Malformed legacy popup rule: {pattern!r}")
        if (record.keys() - SITE_RULE_METADATA
                or record.get("expiration", "0") != "0"
                or record.get("lifetime", "0") != "0"
                or type(record.get("model", 0)) is not int
                or record.get("model", 0) != 0
                or record.get("last_visit", "0") != "0"
                or record.get("decided_by_related_website_sets", False) is not False):
            raise ValueError(f"Non-durable legacy popup rule: {pattern!r}")
        converted[origin] = record["setting"]
        timestamp = chromium_timestamp(record.get("last_modified"))
        if times is not None and timestamp not in (None, "0"):
            times[origin] = timestamp
    if len(converted) > 256:
        raise ValueError("Too many rules for the Content popup preferences")
    return default == 1, converted


def unique_preferences_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate key in legacy JSON: {key!r}")
        result[key] = value
    return result


def read_verified_file(candidate, path, entries, label, max_bytes):
    relative = path.relative_to(candidate).as_posix()
    expected = entries.get(relative)
    if expected is None and not path.exists() and not is_linked_path(path):
        return None
    info = path.lstat()
    if (not expected or expected.get("kind") != "file" or
            not is_regular_file(info) or info.st_size != expected.get("size") or
            info.st_size > max_bytes):
        raise ValueError(f"Invalid candidate {label} file")
    with path.open("rb") as file:
        data = file.read(max_bytes + 1)
    if len(data) != info.st_size or hashlib.sha256(data).hexdigest() != expected.get("sha256"):
        raise ValueError(f"The candidate {label} changed after preparation")
    return data


def read_verified_json(candidate, path, entries, label):
    data = read_verified_file(candidate, path, entries, label, MAX_SESSION_BYTES)
    if data is None:
        return None
    document = json.loads(data, object_pairs_hook=unique_preferences_object)
    if not isinstance(document, dict):
        raise ValueError(f"Invalid candidate {label} object")
    return document


def read_clipboard_rules(candidate, profile, entries, times):
    document = read_verified_json(candidate, profile / "Preferences", entries, "profile preferences")
    return clipboard_rules_from_preferences(document or {}, times)


def write_site_permission_rules(destination, clipboard, popups=None, popups_allowed=False,
                                *, clipboard_times=None, popup_times=None):
    settings = {"clipboard_permissions": clipboard}
    if popups:
        settings["popup_permissions"] = popups
    if popups_allowed:
        settings["popups_allowed"] = True
    if clipboard_times:
        settings["clipboard_permission_times"] = clipboard_times
    if popup_times:
        settings["popup_permission_times"] = popup_times
    (destination / CLIPBOARD_PREFS).write_text(
        json.dumps({"zitter": settings}, sort_keys=True) + "\n", encoding="utf-8",
    )


def copy_clipboard_permissions(trial, profile_id, destination_profile):
    """Convert durable, exact-origin rules into a new Content profile directory."""
    if is_linked_path(trial):
        raise ValueError("The trial root must not be a link or reparse point")
    trial = trial.resolve()
    destination = separate_destination(trial, destination_profile)
    manifest = read_trial_manifest(trial)
    profile = candidate_profile(trial, profile_id, manifest)
    times = {}
    converted = read_clipboard_rules(trial / "candidate", profile, manifest["entries"], times)

    destination.mkdir(mode=0o700)
    try:
        write_site_permission_rules(destination, converted, clipboard_times=times)
    except BaseException:
        shutil.rmtree(destination)
        raise
    return len(converted)


def copy_cookie_store(candidate, profile, entries, destination):
    cookie_stores = []
    for directory in (profile, profile / "Network"):
        if is_linked_path(directory) or directory.resolve() != directory:
            raise ValueError("The candidate cookie directory contains a link")
        files = {}
        for name in COOKIE_FILES:
            path = directory / name
            relative = path.relative_to(candidate).as_posix()
            expected = entries.get(relative)
            if expected is None and not path.exists() and not is_linked_path(path):
                continue
            if not expected or expected["kind"] != "file" or not is_regular_file(path.lstat()):
                raise ValueError("Invalid candidate cookie database or sidecar")
            files[name] = expected
        if files:
            if "Cookies" not in files:
                raise ValueError("Candidate cookie sidecars have no database")
            cookie_stores.append((directory, files))
    if len(cookie_stores) > 1:
        raise ValueError("Candidate contains two cookie databases")
    if not cookie_stores:
        return
    directory, files = cookie_stores[0]
    network = destination / "Network"
    network.mkdir(mode=0o700)
    for name in files:
        shutil.copy2(directory / name, network / name, follow_symlinks=False)
    if inventory(network) != files:
        raise ValueError("The candidate cookie database changed after preparation")


def write_browser_storage(candidate, profile, entries, destination):
    storage = profile / LOCAL_STORAGE
    relative_storage = storage.relative_to(candidate).as_posix()
    storage_entry = entries.get(relative_storage)
    if storage_entry is not None or storage.exists() or is_linked_path(storage):
        if (not storage_entry or storage_entry["kind"] != "directory" or
                is_linked_path(storage) or storage.resolve() != storage):
            raise ValueError("Invalid candidate localStorage directory")
        prefix = relative_storage + "/"
        storage_files = {name[len(prefix):]: entry for name, entry in entries.items()
                         if name.startswith(prefix)}

    copy_cookie_store(candidate, profile, entries, destination)
    if storage_entry is not None:
        copy_verified(storage, destination / LOCAL_STORAGE, storage_files)


def copy_browser_storage(trial, profile_id, destination_profile):
    """Copy encrypted cookies and localStorage from a prepared offline profile."""
    if is_linked_path(trial):
        raise ValueError("The trial root must not be a link or reparse point")
    trial = trial.resolve()
    destination = separate_destination(trial, destination_profile)
    manifest = read_trial_manifest(trial)
    profile = candidate_profile(trial, profile_id, manifest)
    destination.mkdir(mode=0o700)
    try:
        write_browser_storage(trial / "candidate", profile, manifest["entries"], destination)
    except BaseException:
        shutil.rmtree(destination)
        raise


def copy_sqlite_store(candidate, profile, entries, destination, names):
    database = names[0]
    copied = set()
    for name in names:
        source = profile / name
        relative = source.relative_to(candidate).as_posix()
        expected = entries.get(relative)
        if expected is None and not source.exists() and not is_linked_path(source):
            continue
        if not expected or expected["kind"] != "file" or not is_regular_file(source.lstat()):
            raise ValueError(f"Invalid candidate {database} database or sidecar")
        target = destination / name
        shutil.copy2(source, target, follow_symlinks=False)
        info = target.lstat()
        if not is_regular_file(info) or regular_file_entry(target, info) != expected:
            raise ValueError(f"The candidate {database} database changed after preparation")
        copied.add(name)
    if copied and database not in copied:
        raise ValueError(f"Candidate {database} sidecars have no database")
    return bool(copied)


def write_password_saving_preference(destination, preferences):
    if PASSWORD_SAVING_PREF not in preferences:
        return
    enabled = preferences[PASSWORD_SAVING_PREF]
    if type(enabled) is not bool:
        raise ValueError("Invalid legacy password saving preference")
    (destination / PASSWORD_PREFS).write_text(
        json.dumps({PASSWORD_SAVING_PREF: enabled}) + "\n", encoding="utf-8",
    )


def write_page_zoom_preference(destination, preferences):
    partition = preferences.get("partition", {})
    if not isinstance(partition, dict):
        raise ValueError("Invalid legacy partition preferences")
    defaults = partition.get("default_zoom_level", {})
    per_host = partition.get("per_host_zoom_levels", {})
    if not isinstance(defaults, dict) or not isinstance(per_host, dict):
        raise ValueError("Invalid legacy page zoom preferences")
    # Chromium hex-encodes the relative partition path after the x prefix.
    # The main partition is the profile directory itself, so the path is empty.
    default = defaults.get("x", 0.0)
    hosts = per_host.get("x", {})
    # Chromium desktop zoom uses a factor of 1.2 per level and allows 25%–500%.
    minimum = math.log(0.25) / math.log(1.2)
    maximum = math.log(5.0) / math.log(1.2)

    def valid_level(level):
        return type(level) in (float, int) and minimum <= level <= maximum

    if not valid_level(default):
        raise ValueError("Invalid legacy default page zoom")
    if not isinstance(hosts, dict):
        raise ValueError("Invalid legacy per-site page zoom")
    converted = {}
    for host, value in hosts.items():
        level = value.get("zoom_level") if isinstance(value, dict) else value
        if not host or "\0" in host or not valid_level(level) or abs(level - default) <= 0.001:
            continue
        entry = {"zoom_level": float(level)}
        if isinstance(value, dict):
            timestamp = chromium_timestamp(value.get("last_modified"))
            if timestamp is not None:
                entry["last_modified"] = timestamp
        converted[host] = entry
    result = {}
    if "x" in defaults:
        result["default_zoom_level"] = float(default)
    if converted:
        result["per_host_zoom_levels"] = converted
    if result:
        contents = (json.dumps({"partition": result}) + "\n").encode("utf-8")
        if len(contents) > 4 * 1024 * 1024:
            raise ValueError("Legacy page zoom preferences are too large")
        (destination / PAGE_ZOOM_PREFS).write_bytes(contents)


def is_local_download_path(path, windows):
    if not isinstance(path, str) or any(ord(char) < 32 for char in path):
        return False
    parsed = (PureWindowsPath if windows else PurePosixPath)(path)
    if (not parsed.is_absolute() or not parsed.name or ".." in parsed.parts
            or path.endswith(("/", "\\"))):
        return False
    if windows:
        return (bool(re.fullmatch(r"[a-zA-Z]:", parsed.drive))
                and all(":" not in part for part in parsed.parts[1:]))
    return not path.startswith("//")


def retain_download_history(db, windows):
    columns = {row[1] for row in db.execute("PRAGMA table_info(downloads)")}
    required = {"id", "guid", "state", "transient", "danger_type", "interrupt_reason",
                "current_path", "target_path", "received_bytes", "total_bytes",
                "embedder_download_data", "hash", "http_method", "etag", "last_modified"}
    chain_columns = {row[1] for row in db.execute("PRAGMA table_info(downloads_url_chains)")}
    db.execute("DELETE FROM downloads_slices")
    db.execute("CREATE TEMP TABLE retained_downloads(id INTEGER PRIMARY KEY)")
    if required <= columns and {"id", "chain_index", "url"} <= chain_columns:
        candidates = db.execute("""
            SELECT id, guid, state, interrupt_reason, current_path, target_path,
                   received_bytes, total_bytes
            FROM downloads
            WHERE state IN (1, 2, 4) AND transient = 0 AND danger_type IN (0, 15)
            ORDER BY id
        """)
        guids = set()
        for row_id, guid, state, reason, current, target, received, total in candidates:
            try:
                identity = UUID(guid)
            except (AttributeError, TypeError, ValueError):
                continue
            if (type(row_id) is not int or not 0 < row_id < 0xffff_ffff
                    or identity.int == 0 or identity in guids
                    or str(identity) != guid.lower() or not is_local_download_path(target, windows)
                    or type(received) is not int or received < 0
                    or type(total) is not int or (total != -1 and total < received)
                    or type(reason) is not int or not 0 <= reason <= 0x7fff_ffff):
                continue
            if state == 1 and (reason != 0 or current not in ("", target)
                               or total not in (-1, received)):
                continue
            if state == 4 and reason == 0:
                continue
            chain = db.execute("""
                SELECT chain_index, url FROM downloads_url_chains
                WHERE id = ? ORDER BY chain_index LIMIT 33
            """, (row_id,)).fetchall()
            if not 0 < len(chain) <= 32 or any(
                index != offset or not isinstance(url, str) or not url
                or not can_restore_session_url(url) or url == "zitter://rss/"
                for offset, (index, url) in enumerate(chain)
            ):
                continue
            guids.add(identity)
            db.execute("INSERT INTO retained_downloads VALUES (?)", (row_id,))
        db.execute("""
            UPDATE downloads SET embedder_download_data = ?, hash = X'',
                http_method = '', etag = '', last_modified = '',
                current_path = CASE WHEN state = 1 THEN current_path ELSE '' END
            WHERE id IN (SELECT id FROM retained_downloads)
        """, (IMPORTED_DOWNLOAD_MARKER,))
    for table in ("downloads_url_chains", "downloads"):
        db.execute(f"DELETE FROM {table} WHERE id NOT IN (SELECT id FROM retained_downloads)")


def write_history(candidate, profile, entries, destination, *, windows):
    if not copy_sqlite_store(candidate, profile, entries, destination, HISTORY_FILES):
        return

    # Restore eligible terminal entries as history only. Discard transfer state.
    with closing(sqlite3.connect((destination / "History").as_uri() + "?mode=rw", uri=True)) as db:
        db.execute("PRAGMA trusted_schema=OFF")
        version = db.execute("SELECT value FROM meta WHERE key = 'version'").fetchone()
        if version not in ((HISTORY_SCHEMA_VERSION,), (str(HISTORY_SCHEMA_VERSION),)):
            raise ValueError("Unsupported legacy History schema")
        if db.execute("PRAGMA quick_check").fetchall() != [("ok",)]:
            raise ValueError("Legacy History database failed its integrity check")
        download_tables = {"downloads", "downloads_url_chains", "downloads_slices"}
        tables = dict(db.execute("SELECT name, type FROM sqlite_schema"))
        if (any(tables.get(name) != "table" for name in download_tables | {"urls", "visits"})
                or db.execute("SELECT 1 FROM sqlite_schema WHERE type = 'trigger'").fetchone()):
            raise ValueError("Unsupported legacy History tables or triggers")
        if db.execute("PRAGMA journal_mode=DELETE").fetchone() != ("delete",):
            raise ValueError("Could not checkpoint the copied History database")
        db.execute("PRAGMA secure_delete=ON")
        with db:
            retain_download_history(db, windows)


def write_favicons(candidate, profile, entries, destination):
    if not copy_sqlite_store(candidate, profile, entries, destination, FAVICON_FILES):
        return
    with closing(sqlite3.connect((destination / "Favicons").as_uri() + "?mode=rw", uri=True)) as db:
        db.execute("PRAGMA trusted_schema=OFF")
        version = db.execute("SELECT value FROM meta WHERE key = 'version'").fetchone()
        if version not in ((FAVICON_SCHEMA_VERSION,), (str(FAVICON_SCHEMA_VERSION),)):
            raise ValueError("Unsupported legacy Favicons schema")
        if db.execute("PRAGMA quick_check").fetchall() != [("ok",)]:
            raise ValueError("Legacy Favicons database failed its integrity check")
        tables = dict(db.execute("SELECT name, type FROM sqlite_schema"))
        if (any(tables.get(name) != "table" for name in ("icon_mapping", "favicons", "favicon_bitmaps"))
                or db.execute("SELECT 1 FROM sqlite_schema WHERE type = 'trigger'").fetchone()):
            raise ValueError("Unsupported legacy Favicons tables or triggers")
        if db.execute("PRAGMA journal_mode=DELETE").fetchone() != ("delete",):
            raise ValueError("Could not checkpoint the copied Favicons database")
        db.execute("PRAGMA secure_delete=ON")
        with db:
            db.execute("""
                DELETE FROM favicon_bitmaps
                WHERE typeof(image_data) != 'blob' OR length(image_data) NOT BETWEEN 1 AND 1048576
                   OR typeof(width) != 'integer' OR typeof(height) != 'integer'
                   OR width NOT BETWEEN 1 AND 1024 OR height NOT BETWEEN 1 AND 1024
            """)


def can_restore_session_url(url):
    if not url or url == "zitter://rss/":
        return True
    if re.search(r"[^!-~]|\\", url):
        return False
    try:
        parts = urlsplit(url)
        if parts.scheme not in ("http", "https") or not parts.hostname or "@" in parts.netloc:
            return False
        # urlsplit checks port syntax and range when this property is read.
        if parts.port is not None and not 0 <= parts.port <= 65535:
            return False
        if parts.netloc.startswith("["):
            return (bool(re.fullmatch(r"\[[a-fA-F0-9:.]+\](?::[0-9]+)?", parts.netloc))
                    and ipaddress.ip_address(parts.hostname).version == 6)
        return bool(re.fullmatch(r"[a-zA-Z0-9._-]+(?::[0-9]+)?", parts.netloc))
    except ValueError:
        return False


def convert_session_split(workspace, entry, tab_count):
    members = entry.get("split", [])
    weights = entry.get("weights", [])
    right = entry.get("right", -1)
    vertical = entry.get("vertical", False)
    if type(right) is not int or type(vertical) is not bool:
        raise ValueError(f"Unsupported legacy split for {workspace}")
    if "split" not in entry and right >= 0:
        ratio = entry.get("ratio", 0.5)
        if type(ratio) not in (int, float) or not 0 < ratio < 1:
            raise ValueError(f"Unsupported legacy split ratio for {workspace}")
        members = [entry.get("left", 0), right]
        weights = [ratio, 1 - ratio]
    elif right != -1:
        raise ValueError(f"Unsupported legacy split for {workspace}")
    if members == [] and weights == [] and not vertical:
        return None
    if (not isinstance(members, list) or not 2 <= len(members) <= MAX_SPLIT_PANES
            or any(type(index) is not int or not 0 <= index < tab_count for index in members)
            or len(set(members)) != len(members) or not isinstance(weights, list)):
        raise ValueError(f"Unsupported legacy split for {workspace}")
    if not weights:
        weights = [1 / len(members)] * len(members)
    if (len(weights) != len(members)
            or any(type(weight) not in (int, float) or not 0 < weight <= 1e100 for weight in weights)
            or not math.isfinite(sum(weights))):
        raise ValueError(f"Unsupported legacy split weights for {workspace}")
    return {"tabs": members, "weights": weights, "vertical": vertical}


def convert_workspace_pins(workspace, profile_pins, workspace_pins):
    if (not isinstance(profile_pins, list) or not isinstance(workspace_pins, list)
            or len(profile_pins) + len(workspace_pins) > MAX_SESSION_TABS):
        raise ValueError(f"Invalid legacy pins for {workspace}")
    converted, seen, folders = [], set(), set()
    tagged_pins = [(pin, True) for pin in profile_pins] + [
        (pin, False) for pin in workspace_pins
    ]
    for pin, profile_pin in tagged_pins:
        keys = {"id", "title", "url"} if profile_pin else {
            "id", "title", "url", "parent_id", "folder"
        }
        if not isinstance(pin, dict) or pin.keys() != keys:
            raise ValueError(f"Invalid legacy pin for {workspace}")
        identifier, title, url = pin["id"], pin["title"], pin["url"]
        parent, folder = pin.get("parent_id", ""), pin.get("folder", False)
        if (not isinstance(identifier, str) or not re.fullmatch(r"[!-~]{1,256}", identifier)
                or identifier in seen or not isinstance(title, str)
                or len(title.encode("utf-16-le")) > MAX_TAB_TITLE_UNITS * 2
                or not isinstance(parent, str) or (parent and parent not in folders)
                or type(folder) is not bool or not isinstance(url, str)
                or len(url.encode("utf-8")) > MAX_TAB_URL_BYTES
                or (url != "" if folder else not url or not can_restore_session_url(url))):
            raise ValueError(f"Invalid legacy pin for {workspace}")
        seen.add(identifier)
        if folder:
            folders.add(identifier)
        converted.append({"id": identifier, "title": title, "url": url,
                          "parent_id": parent, "folder": folder, "profile_pin": profile_pin})
    return converted


def convert_session_tabs(workspace, entry, profile_id, pins):
    supported = {"profile_id", "active", "tabs", "split", "weights", "vertical",
                 "left", "right", "ratio", "sleeping"}
    if not isinstance(entry, dict) or entry.get("profile_id") != profile_id:
        raise ValueError(f"Invalid legacy session profile for {workspace}")
    sleeping = entry.get("sleeping", False)
    if entry.keys() - supported or type(sleeping) is not bool:
        raise ValueError(f"Unsupported legacy workspace layout for {workspace}")
    tabs = entry.get("tabs")
    active = entry.get("active", 0)
    if (not isinstance(tabs, list) or not 1 <= len(tabs) <= MAX_SESSION_TABS
            or type(active) is not int or not 0 <= active < len(tabs)):
        raise ValueError(f"Invalid legacy session tabs or selection for {workspace}")
    open_pins = set()
    pin_ids = {pin["id"] for pin in pins if not pin["folder"]}
    for tab in tabs:
        if not isinstance(tab, dict) or tab.keys() not in ({"url", "title"}, {"url", "title", "pin"}):
            raise ValueError(f"Unsupported legacy session tab for {workspace}")
        if "pin" in tab:
            pin = tab["pin"]
            if not isinstance(pin, str) or pin not in pin_ids or pin in open_pins:
                raise ValueError(f"Invalid legacy open pin for {workspace}")
            open_pins.add(pin)
        url, title = tab["url"], tab["title"]
        if (not isinstance(url, str) or len(url.encode("utf-8")) > MAX_TAB_URL_BYTES
                or not can_restore_session_url(url) or not isinstance(title, str)
                or len(title.encode("utf-16-le")) > MAX_TAB_TITLE_UNITS * 2):
            raise ValueError(f"Unsupported legacy session tab for {workspace}")
    result = {"profile_id": workspace, "active": active, "tabs": tabs}
    if sleeping:
        result["sleeping"] = True
    if split := convert_session_split(workspace, entry, len(tabs)):
        result["split"] = split
    return result


def assistant_permissions(value):
    if not isinstance(value, dict):
        return {}
    return {
        key: value[key]
        for key in ("tabs", "pages", "actions", "history", "web", "confirm_clicks")
        if type(value.get(key)) is bool
    }


def convert_assistant_settings(settings):
    if not isinstance(settings, dict):
        return {}
    converted = {
        key: settings[key]
        for key in ("enabled", "ollama_configured", "background", "confirm_background")
        if type(settings.get(key)) is bool
    }
    for key in ("catalog_version", "concurrency"):
        if type(settings.get(key)) is int and -(2**31) <= settings[key] < 2**31:
            converted[key] = settings[key]
    for key, limit in (("codex_path", 4096), ("claude_path", 4096), ("default_model", 128)):
        value = settings.get(key)
        if isinstance(value, str) and len(value.encode("utf-8")) <= limit:
            converted[key] = value
    backends = settings.get("backends")
    if isinstance(backends, list) and len(backends) == 4 and all(type(item) is bool for item in backends):
        converted["backends"] = backends
    permissions = settings.get("permissions")
    if isinstance(permissions, dict):
        converted["permissions"] = assistant_permissions(permissions)
    models = settings.get("models")
    if isinstance(models, list):
        selected = []
        for entry in models:
            if not isinstance(entry, dict):
                continue
            provider, model = entry.get("provider"), entry.get("id")
            if (provider not in ("openai", "codex", "claude", "ollama")
                    or not isinstance(model, str) or not 0 < len(model.encode("utf-8")) <= 120):
                continue
            visible = entry.get("visible")
            selected.append({"provider": provider, "id": model,
                             "visible": visible if type(visible) is bool else True})
            if len(selected) == 32:
                break
        if selected:
            converted["models"] = selected
    key = settings.get("encrypted_api_key")
    if isinstance(key, str) and 0 < len(key) <= 8192:
        try:
            ciphertext = base64.b64decode(key, validate=True)
        except (ValueError, TypeError):
            ciphertext = b""
        # The pinned macOS Keychain and Windows DPAPI providers use v10.
        # Windows App-Bound Encryption uses v20. Preserve it for a later
        # authorized migration without trying to unwrap it in the importer.
        if len(ciphertext) >= 19 and ciphertext.startswith((b"v10", b"v20")):
            converted["encrypted_api_key"] = key
    return converted


def assistant_message(value):
    if not isinstance(value, dict) or not isinstance(value.get("text"), str):
        return None
    kind = value.get("kind")
    if type(kind) is not int or not 0 <= kind <= 5:
        return None
    message = {"kind": kind, "text": value["text"]}
    state = value.get("state")
    if type(state) is int and -(2**31) <= state < 2**31:
        message["state"] = state
    options = value.get("options")
    if isinstance(options, list):
        message["options"] = [option for option in options if isinstance(option, str)]
    answer = value.get("answer")
    if isinstance(answer, str) and answer:
        message["answer"] = answer
    elif kind == 5:
        # Questions left open by the old runtime cannot be resumed.
        message["answer"] = "—"
    chips = value.get("chips")
    if isinstance(chips, list):
        display = []
        for chip in chips:
            if not isinstance(chip, dict) or not isinstance(chip.get("kind"), str):
                continue
            title, color = chip.get("title"), chip.get("color")
            # Legacy tab and pin references do not resolve in the new session.
            display.append({"kind": chip["kind"], "ref": "",
                            "title": title if isinstance(title, str) else "",
                            "color": color if type(color) in (int, float)
                            and 0 <= color <= 0xffffffff else 0})
        message["chips"] = display
    return message


def read_assistant_sessions(candidate, entries, profile_ids):
    data = read_verified_file(
        candidate, candidate / ASSISTANT_SESSIONS_FILE, entries,
        "Assistant sessions", MAX_ASSISTANT_SESSIONS_BYTES,
    )
    if data is None:
        return None
    document = json.loads(data, object_pairs_hook=unique_preferences_object)
    if (not isinstance(document, dict) or type(document.get("version")) is not int
            or document["version"] != 1
            or any(not isinstance(document.get(key, []), list) for key in ("sessions", "tasks"))):
        raise ValueError("Unsupported legacy Assistant session format")

    result = {"version": 1, "sessions": [], "tasks": []}
    for section, key in (("sessions", "key"), ("tasks", "id")):
        seen = set()
        for entry in document.get(section, []):
            if not isinstance(entry, dict):
                continue
            profile, identifier = entry.get("profile"), entry.get(key)
            if (not isinstance(profile, str) or profile not in profile_ids
                    or not isinstance(identifier, str) or not identifier):
                continue
            permissions = assistant_permissions(entry.get("permissions"))
            permissions["confirm_clicks"] = True
            converted = {key: identifier, "profile": profile_ids[profile],
                         "permissions": permissions}
            for field in ("provider", "model"):
                if isinstance(entry.get(field), str):
                    converted[field] = entry[field]
            if section == "sessions":
                messages = entry.get("messages")
                if not isinstance(messages, list):
                    continue
                converted["messages"] = [message for value in messages
                                         if (message := assistant_message(value)) is not None]
                if not any(message["kind"] == 1 for message in converted["messages"]):
                    continue
                if isinstance(entry.get("title"), str):
                    converted["title"] = entry["title"]
                # Imported conversations require a new confirmation.
                converted["confirm_background"] = True
                timestamps = ("updated",)
            else:
                instruction = entry.get("instruction")
                if not isinstance(instruction, str) or not instruction:
                    continue
                converted.update(instruction=instruction, active=False, next_run=0)
                if isinstance(entry.get("name"), str):
                    converted["name"] = entry["name"]
                for field in ("kind", "days", "minute", "last_state"):
                    value = entry.get(field)
                    if type(value) is int and -(2**31) <= value < 2**31:
                        converted[field] = value
                timestamps = ("last_run",)
            for field in timestamps:
                value = entry.get(field)
                if type(value) in (int, float) and 0 <= value <= 8.64e15:
                    converted[field] = value
            if identifier in seen:
                raise ValueError(f"Duplicate legacy Assistant {section} identifier")
            seen.add(identifier)
            result[section].append(converted)
    if len(result["sessions"]) > MAX_ASSISTANT_CONVERSATIONS:
        raise ValueError("The legacy Assistant history exceeds the Content conversation limit")
    encoded = json.dumps(result, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
    if len(encoded.encode("utf-8")) > MAX_ASSISTANT_SESSIONS_BYTES:
        raise ValueError("The converted Assistant history exceeds the Content size limit")
    return encoded


def content_catalogue_ids(profiles, associations):
    defaults = {"ZitterWork": "work", "ZitterPersonal": "personal"}
    reserved_profiles = len(defaults.keys() - set(profiles.values()))
    if len(profiles) + reserved_profiles > 32 or len(associations) > 64:
        raise ValueError("The legacy catalogue exceeds Content profile or workspace limits")
    profile_ids = {
        profile: defaults.get(directory) or f"profile-{directory.removeprefix('ZitterProfile-')}"
        for profile, directory in profiles.items()
    }
    builtin_owners = {role: associations[role] for role in WORKSPACE_PROFILES if role in associations}
    if {profile_ids[owner] for owner in builtin_owners.values()} == set(WORKSPACE_PROFILES):
        profile_ids.update({owner: role for role, owner in builtin_owners.items()})
    workspace_ids = {}
    for workspace in associations:
        if workspace in WORKSPACE_PROFILES:
            workspace_ids[workspace] = workspace
        else:
            try:
                workspace_ids[workspace] = f"space-{UUID(workspace)}"
            except ValueError as error:
                raise ValueError("Unsupported legacy workspace identifier") from error
    if len(set(workspace_ids.values())) != len(workspace_ids):
        raise ValueError("Duplicate converted workspace identifier")
    return profile_ids, workspace_ids


def content_catalogue_name(name):
    if not name or len(name.encode("utf-8")) > 256 or any(ch in name for ch in "\r\n\t\0"):
        raise ValueError("Unsupported legacy catalogue name")
    return name


def read_workspace_session(candidate, entries, associations, profile_ids, workspace_ids):
    session = read_verified_json(candidate, candidate / "ZitterSession.json", entries, "workspace session")
    config = read_verified_json(candidate, candidate / "ZitterWorkspaces.json", entries, "workspace configuration")
    if not config or (session is not None and session.keys() - associations.keys()):
        raise ValueError("Invalid legacy workspace session or configuration")
    workspace_entries = {entry["id"]: entry for entry in config["workspaces"]}
    profile_entries = {entry["id"]: entry for entry in config.get("profiles", [])}
    active = config.get("active")
    if not isinstance(active, str) or active not in workspace_ids:
        active = next(iter(workspace_ids))
    converted = {"active_workspace": workspace_ids[active]}
    defaults = {"work": "Work", "personal": "Personal"}
    removed = {role: role for role in WORKSPACE_PROFILES if role not in associations}
    if removed:
        converted["removed_workspaces"] = removed
    assigned_profiles = set(associations.values())
    for profile, mapped in profile_ids.items():
        metadata = profile_entries.get(profile, {})
        name = content_catalogue_name(metadata.get("name", defaults.get(mapped)))
        if mapped not in defaults:
            converted.setdefault("extra_profiles", []).append({"id": mapped, "name": name})
        elif name != defaults[mapped]:
            converted.setdefault("profile_names", {})[mapped] = name
        if profile not in assigned_profiles and metadata.get("pins"):
            favorites = convert_workspace_pins(mapped, metadata["pins"], [])
            converted.setdefault("profile_favorites", {})[mapped] = favorites
    for workspace, profile_id in associations.items():
        pins = convert_workspace_pins(workspace, profile_entries.get(profile_id, {}).get("pins", []),
                                      workspace_entries[workspace].get("pins", []))
        mapped = workspace_ids[workspace]
        owner = profile_ids[profile_id]
        name = content_catalogue_name(workspace_entries[workspace]["name"])
        if mapped not in defaults:
            converted.setdefault("extra_workspaces", []).append({"id": mapped, "name": name})
        else:
            if name != defaults[mapped]:
                converted.setdefault("workspace_names", {})[mapped] = name
            if owner != mapped:
                epoch = uuid5(NAMESPACE_URL, f"dev.zitter.browser/{mapped}/{owner}")
                converted.setdefault("timeline_epochs", {})[mapped] = str(epoch)
        if session is not None and workspace in session:
            entry = convert_session_tabs(workspace, session[workspace], profile_id, pins)
        else:
            entry = {"active": 0, "tabs": [{"url": "", "title": ""}]}
        entry["profile_id"] = owner
        if mapped == converted["active_workspace"]:
            entry.pop("sleeping", None)
        if pins:
            entry["pins"] = pins
        converted[mapped] = entry
    rules = {}
    legacy_rules = config.get("link_rules")
    if isinstance(legacy_rules, list):
        for rule in legacy_rules:
            if not isinstance(rule, dict):
                continue
            host, target = rule.get("host"), rule.get("workspace_id")
            if (not isinstance(host, str) or not isinstance(target, str)
                    or target not in associations or not host or host != host.lower()
                    or not can_restore_session_url(f"https://{host}/")):
                continue
            parts = urlsplit(f"https://{host}/")
            if parts.netloc != host or parts.port is not None or parts.path != "/":
                continue
            if host.startswith("["):
                if f"[{ipaddress.IPv6Address(parts.hostname).compressed}]" != host:
                    continue
            elif re.search(r"(?:^|\.)(?:[0-9]+|0x[0-9a-f]+)\.?$", host):
                try:
                    if str(ipaddress.IPv4Address(host.rstrip("."))) != host:
                        continue
                except ValueError:
                    continue
            rules.setdefault(host, workspace_ids[target])
    if rules:
        converted["link_rules"] = rules
    if config.get("shift_click_peek") is False:
        converted["shift_click_peek"] = False
    if config.get("clean_address_copies") is False:
        converted["clean_address_copies"] = False
    if assistant := convert_assistant_settings(config.get("assistant")):
        converted["assistant"] = assistant
    if (session is None and not rules and "assistant" not in converted
            and "shift_click_peek" not in converted
            and "clean_address_copies" not in converted
            and not any(key in converted for key in (
                "extra_profiles", "extra_workspaces", "profile_names", "workspace_names",
                "profile_favorites", "timeline_epochs", "removed_workspaces",
            ))
            and not any("pins" in converted[name] for name in workspace_ids.values())):
        return None
    result = json.dumps(converted, ensure_ascii=False, separators=(",", ":"))
    if len(result.encode("utf-8")) > MAX_SESSION_BYTES:
        raise ValueError("The converted workspace session exceeds the Content size limit")
    return result


def read_workspace_timeline(candidate, entries, associations, profile_ids, workspace_ids):
    data = read_verified_file(
        candidate, candidate / "ZitterTimeline.jsonl", entries,
        "workspace timeline", MAX_TIMELINE_BYTES,
    )
    if data is None:
        return None
    output = []
    frame_owners = {}
    replaceable = set()
    for raw in BytesIO(data):
        if not raw.strip():
            continue
        line = json.loads(raw, object_pairs_hook=unique_preferences_object)
        if not isinstance(line, dict):
            raise ValueError("Invalid legacy workspace timeline record")
        timestamp, workspace = line.get("t"), line.get("w")
        if (type(timestamp) not in (int, float) or not 0 <= timestamp <= 32503680000000
                or timestamp != int(timestamp) or not isinstance(workspace, str)):
            raise ValueError("Invalid legacy workspace timeline timestamp or workspace")
        if workspace not in associations:
            continue
        if "m" in line:
            name = line["m"]
            if (line.keys() != {"t", "w", "m"} or not isinstance(name, str)
                    or not 1 <= len(name.encode("utf-8")) <= 160):
                raise ValueError("Invalid legacy workspace timeline mark")
            output.append({"t": int(timestamp), "w": workspace, "m": name})
            continue
        profile, active, tabs = line.get("p"), line.get("a"), line.get("tabs")
        if (line.keys() not in ({"t", "w", "p", "a", "tabs"},
                               {"t", "w", "p", "a", "tabs", "r"})
                or not isinstance(profile, str) or type(active) is not int
                or not isinstance(tabs, list) or len(tabs) > MAX_SESSION_TABS
                or not -1 <= active < len(tabs) or type(line.get("r", False)) is not bool):
            raise ValueError("Invalid legacy workspace timeline frame")
        owners = frame_owners.setdefault(workspace, [])
        if owners and timestamp < owners[-1][0]:
            raise ValueError("Legacy workspace timeline frames are out of order")
        owners.append((timestamp, profile))
        if profile != associations[workspace]:
            replaceable.discard(workspace)
            continue
        filtered = []
        selected = -1
        for index, pair in enumerate(tabs):
            if (not isinstance(pair, list) or len(pair) != 2
                    or not all(isinstance(value, str) for value in pair)):
                raise ValueError("Invalid legacy workspace timeline tab")
            url, title = pair
            if (not url or len(url.encode("utf-8")) > MAX_TAB_URL_BYTES
                    or not can_restore_session_url(url)):
                continue
            if index == active:
                selected = len(filtered)
            filtered.append([url, title[:MAX_TAB_TITLE_UNITS]])
        frame = {"t": int(timestamp), "w": workspace, "p": profile_ids[profile],
                 "a": selected, "tabs": filtered}
        if line.get("r") and workspace in replaceable:
            frame["r"] = True
        output.append(frame)
        replaceable.add(workspace)

    def belongs_to_current_profile(mark):
        owners = frame_owners.get(mark["w"], [])
        end = bisect_left(owners, (mark["t"] + 1, ""))
        return end > 0 and owners[end - 1][1] == associations[mark["w"]]

    result = "".join(
        json.dumps({**line, "w": workspace_ids[line["w"]]}, ensure_ascii=False, separators=(",", ":")) + "\n"
        for line in output if "m" not in line or belongs_to_current_profile(line)
    ).encode("utf-8")
    if len(result) > MAX_TIMELINE_BYTES:
        raise ValueError("The converted workspace timeline exceeds the Content size limit")
    return result


def write_adblock_preferences(destination, preferences):
    group = preferences.get("zitter")
    if not isinstance(group, dict):
        return
    result = {key: group[key] for key in (
        "ad_blocking", "ad_blocking_allowed_sites", "ad_blocking_rules", "ad_blocking_lists",
    ) if key in group}
    if not result:
        return
    enabled = result.get("ad_blocking", True)
    sites = result.get("ad_blocking_allowed_sites", [])
    rules = result.get("ad_blocking_rules", [])
    lists = result.get("ad_blocking_lists", {})
    if (type(enabled) is not bool or not isinstance(sites, list) or len(sites) > 512
            or not isinstance(rules, list) or len(rules) > 1000
            or not isinstance(lists, dict) or len(lists) > 8):
        raise ValueError("Invalid legacy ad blocking preferences")
    if any(not isinstance(site, str) or not 1 <= len(site.encode("utf-8")) <= 253
           or re.search(r"[^!-~]|[/?#@\\]", site)
           or (":" in site and not (site.startswith("[") and site.endswith("]")))
           or not can_restore_session_url(f"https://{site}/") for site in sites):
        raise ValueError("Invalid legacy ad blocking exception")
    if any(not isinstance(rule, str) or not 1 <= len(rule.encode("utf-8")) <= 1000
           or "\n" in rule or "\r" in rule for rule in rules):
        raise ValueError("Invalid legacy ad blocking rule")
    if any(len(key.encode("utf-8")) > 64 or type(value) is not bool
           for key, value in lists.items()):
        raise ValueError("Invalid legacy ad blocking list switch")
    encoded = json.dumps({"zitter": result}, ensure_ascii=False).encode("utf-8")
    if len(encoded) > 8 * 1024 * 1024:
        raise ValueError("Legacy ad blocking preferences exceed the Content limit")
    (destination / ADBLOCK_PREFS).write_bytes(encoded)


def copy_adblock_filter_cache(candidate, entries, destination):
    directory = "Zitter Filter Lists"
    source = candidate / directory
    if is_linked_path(source):
        raise ValueError("Invalid legacy filter-list cache path")
    target = destination / directory
    for name in ("easylist.txt", "easyprivacy.txt", "antiadblock.txt", "ublock.txt"):
        data = read_verified_file(candidate, source / name, entries,
                                  "filter-list cache", 16 * 1024 * 1024)
        if data is None:
            continue
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        if not data.startswith((b"[Adblock", b"! Title:")):
            continue
        target.mkdir(mode=0o700, exist_ok=True)
        path = target / name
        path.write_bytes(data)
        # Preserve age so an old cache refreshes after migration.
        shutil.copystat(source / name, path, follow_symlinks=False)


def read_windows_encryption_settings(candidate, entries):
    local_state = read_verified_json(candidate, candidate / "Local State", entries, "Local State")
    encryption = local_state.get("os_crypt") if local_state else None
    if encryption is not None and not isinstance(encryption, dict):
        raise ValueError("Missing legacy Windows OSCrypt settings")
    return encryption


def has_dpapi_envelope(wrapped_key):
    if not isinstance(wrapped_key, str):
        return False
    try:
        decoded = base64.b64decode(wrapped_key, validate=True)
    except ValueError:
        return False
    return len(decoded) > len(b"DPAPI") and decoded.startswith(b"DPAPI")


def read_dpapi_local_state(candidate, entries):
    encryption = read_windows_encryption_settings(candidate, entries)
    if encryption is None:
        raise ValueError("Missing legacy Windows OSCrypt settings")
    if encryption.get("app_bound_encrypted_key", "") != "":
        raise ValueError("Legacy Windows App-Bound Encryption requires a separate migration")
    wrapped_key = encryption.get("encrypted_key")
    if not has_dpapi_envelope(wrapped_key):
        raise ValueError("Malformed legacy Windows DPAPI key")
    payload = json.dumps({"os_crypt": {"encrypted_key": wrapped_key}})
    if len(payload) > 64 * 1024:
        raise ValueError("Legacy Windows DPAPI key exceeds the Content limit")
    return payload


def inspect_windows_store(path, table, column):
    if not path.is_file():
        return None
    with closing(sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)) as db:
        db.execute("PRAGMA trusted_schema=OFF")
        return dict(db.execute(f"""
            SELECT CASE
                     WHEN {column} IS NULL OR length({column}) = 0 THEN 'empty'
                     WHEN substr({column}, 1, 3) = ? THEN 'v10'
                     WHEN substr({column}, 1, 3) = ? THEN 'v20'
                     ELSE 'other_nonempty'
                   END, count(*)
            FROM {table}
            GROUP BY 1
        """, (b"v10", b"v20")))


def inspect_windows_encryption(trial):
    """Report key envelope status and ciphertext counts from verified scratch copies."""
    if is_linked_path(trial):
        raise ValueError("The trial root must not be a link or reparse point")
    trial = trial.resolve()
    manifest = read_trial_manifest(trial)
    candidate = trial / "candidate"
    if is_linked_path(candidate):
        raise ValueError("The candidate root must not be a link or reparse point")
    entries = manifest["entries"]
    encryption = read_windows_encryption_settings(candidate, entries) or {}
    wrapped_key = encryption.get("encrypted_key")
    key_status = "valid-envelope" if has_dpapi_envelope(wrapped_key) else "malformed"
    if "encrypted_key" not in encryption or wrapped_key == "":
        key_status = "missing"
    profiles = []
    for profile_id in manifest["layout"]["profiles"]:
        profile = candidate_profile(trial, profile_id, manifest)
        roles = [role for role in WORKSPACE_PROFILES
                 if manifest["layout"]["workspaces"].get(role) == profile_id]
        with TemporaryDirectory(prefix="zitter-encryption-inspection-") as temporary:
            scratch = Path(temporary).resolve()
            copy_cookie_store(candidate, profile, entries, scratch)
            copy_sqlite_store(candidate, profile, entries, scratch, LOGIN_FILES)
            profiles.append({
                "workspace_roles": roles,
                "cookies": inspect_windows_store(scratch / "Network/Cookies", "cookies",
                                                "encrypted_value"),
                "logins": inspect_windows_store(scratch / "Login Data", "logins",
                                               "password_value"),
            })
    return {
        "dpapi_key": key_status,
        "app_bound_key_present": encryption.get("app_bound_encrypted_key", "") != "",
        "profiles": profiles,
    }


def reject_app_bound_rows(profile):
    for path, statement in (
        (profile / "Network/Cookies",
         "SELECT 1 FROM cookies WHERE substr(encrypted_value, 1, 3) = ? LIMIT 1"),
        (profile / "Login Data",
         "SELECT 1 FROM logins WHERE substr(password_value, 1, 3) = ? LIMIT 1"),
    ):
        if not path.is_file():
            continue
        with closing(sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)) as db:
            db.execute("PRAGMA trusted_schema=OFF")
            if db.execute(statement, (b"v20",)).fetchone():
                raise ValueError("Legacy Windows App-Bound Encryption requires a separate migration")


def import_workspace_profiles(trial, destination_root, layout, *, selected_extensions=None,
                              alarm_importer=None, require_session=False):
    """Prepare the Content catalogue and profile stores together from the verified trial."""
    if is_linked_path(trial) or layout not in WORKSPACE_SESSION_FILES:
        raise ValueError("Expected a real trial and a supported Content Workspace layout")
    extension_selections = {}
    for workspace, selection in (selected_extensions or {}).items():
        ids = (selection,) if isinstance(selection, str) else selection
        if (workspace not in WORKSPACE_PROFILES or not isinstance(ids, (tuple, list))
                or not ids or any(not isinstance(value, str) or not EXTENSION_ID.fullmatch(value)
                                  for value in ids)
                or len(ids) != len(set(ids))):
            raise ValueError("Expected distinct Chromium extension IDs per selected workspace")
        extension_selections[workspace] = ids
    trial = trial.resolve()
    destination = separate_destination(trial, destination_root)
    manifest = read_trial_manifest(trial)
    associations = manifest["layout"]["workspaces"]
    profiles = manifest["layout"]["profiles"]
    if extension_selections.keys() - associations.keys():
        raise ValueError("Cannot select an extension for a missing legacy workspace")
    profile_ids, workspace_ids = content_catalogue_ids(profiles, associations)

    candidate = trial / "candidate"
    if is_linked_path(candidate):
        raise ValueError("The candidate has a linked root")
    entries = manifest["entries"]
    session = read_workspace_session(candidate, entries, associations, profile_ids, workspace_ids)
    if require_session and (session is None or "ZitterSession.json" not in entries):
        raise ValueError("The offline source has no Workspace session for startup")
    timeline = read_workspace_timeline(candidate, entries, associations, profile_ids, workspace_ids)
    assistant_sessions = read_assistant_sessions(candidate, entries, profile_ids)
    appearance = read_verified_json(
        candidate, candidate / "ZitterAppearance.json", entries, "workspace appearance",
    ) or {}
    appearance_keys = (
        "theme", "search_engine", "sidebar_pinned", "sidebar_width", "workspace_animations",
        "workspace_names", "pinned_folders_collapsed", "time_machine", "time_machine_days",
        "auto_update_check", "auto_update_download",
    )
    settings = {key: appearance[key] for key in appearance_keys if key in appearance}
    for legacy, mapped in workspace_ids.items():
        if isinstance(appearance.get(legacy), str):
            settings[mapped] = appearance[legacy]
    emoji = appearance.get("workspace_emoji")
    if isinstance(emoji, dict):
        settings["workspace_emoji"] = {
            mapped: emoji[legacy] for legacy, mapped in workspace_ids.items()
            if isinstance(emoji.get(legacy), str)
        }
    browser_tools = read_verified_json(
        candidate, candidate / "ZitterTools.json", entries, "browser tools",
    )
    windows = layout == "windows-dpapi"
    local_state = read_dpapi_local_state(candidate, entries) if windows else None
    profile_prefix = "workspace" if windows else layout
    with TemporaryDirectory(prefix=".zitter-import-", dir=destination.parent) as temporary:
        staged = Path(temporary) / "workspace"
        staged.mkdir(mode=0o700)
        for legacy_id, mapped in profile_ids.items():
            profile = candidate_profile(trial, legacy_id, manifest)
            target = staged / (f"{profile_prefix}-{mapped}" if mapped in WORKSPACE_PROFILES else mapped)
            target.mkdir(mode=0o700)
            preferences = read_verified_json(
                candidate, profile / "Preferences", entries, "profile preferences",
            ) or {}
            clipboard_times, popup_times = {}, {}
            rules = clipboard_rules_from_preferences(preferences, clipboard_times)
            popups_allowed, popups = popup_settings_from_preferences(preferences, popup_times)
            write_browser_storage(candidate, profile, entries, target)
            write_history(candidate, profile, entries, target, windows=windows or os.name == "nt")
            write_favicons(candidate, profile, entries, target)
            rss = read_verified_file(candidate, profile / "ZitterRSS.json", entries,
                                     "RSS state", 64 * 1024 * 1024)
            if rss is not None:
                document = json.loads(rss, object_pairs_hook=unique_preferences_object)
                if not isinstance(document, dict) or not isinstance(document.get("feeds"), list):
                    raise ValueError("Invalid legacy RSS state")
                (target / "ZitterRSS.json").write_bytes(rss)
            copy_sqlite_store(candidate, profile, entries, target, LOGIN_FILES)
            if windows:
                reject_app_bound_rows(target)
            write_password_saving_preference(target, preferences)
            write_page_zoom_preference(target, preferences)
            write_site_permission_rules(target, rules, popups, popups_allowed,
                                        clipboard_times=clipboard_times, popup_times=popup_times)
            write_adblock_preferences(target, preferences)
            if mapped not in WORKSPACE_PROFILES:
                (target / "Network").mkdir(mode=0o700, exist_ok=True)
                if windows:
                    (target / "NetworkBootstrap").mkdir(mode=0o700)
            ids = tuple(dict.fromkeys(
                extension for workspace, selected in extension_selections.items()
                if associations[workspace] == legacy_id for extension in selected
            ))
            alarm_ids = []
            for extension_id in ids:
                record = write_extension_storage(
                    candidate, profile, entries, target, extension_id,
                    include_install_state=True, indexed_install_state=len(ids) > 1,
                    allow_alarm_only=True,
                )
                if "alarms" in record["active_permissions"]["api"]:
                    alarm_ids.append(extension_id)
            if alarm_ids:
                write_extension_alarm_state(
                    candidate, profile, entries, target, alarm_ids, alarm_importer,
                )
        for role in set(WORKSPACE_PROFILES) - set(profile_ids.values()):
            # Content initializes the two built-in profile slots even when
            # the legacy catalogue only used one of them.
            (staged / f"{profile_prefix}-{role}").mkdir(mode=0o700)
        copy_adblock_filter_cache(candidate, entries, staged)
        if session is not None:
            (staged / WORKSPACE_SESSION_FILES[layout]).write_text(session, encoding="utf-8")
        if assistant_sessions is not None:
            (staged / ASSISTANT_SESSIONS_FILE).write_text(assistant_sessions, encoding="utf-8")
        if timeline is not None:
            name = Path(WORKSPACE_SESSION_FILES[layout]).with_suffix(".timeline.jsonl")
            (staged / name).write_bytes(timeline)
        if settings:
            (staged / WORKSPACE_SETTINGS_FILES[layout]).write_text(
                json.dumps(settings, ensure_ascii=False), encoding="utf-8",
            )
        if browser_tools is not None:
            (staged / "ZitterTools.json").write_text(
                json.dumps(browser_tools, ensure_ascii=False, separators=(",", ":")), encoding="utf-8",
            )
        if local_state is not None:
            (staged / WINDOWS_LOCAL_STATE).write_text(local_state, encoding="utf-8")
        (staged / PREPARED_WORKSPACE_MARKER).write_bytes(
            f"zitter-content-workspace-v1 {layout}\n".encode("ascii")
        )
        if os.name == "nt":
            staged.rename(destination)
        else:
            # POSIX rename can replace an empty directory. Reserve this name
            # exclusively so only our own empty directory can be replaced.
            destination.mkdir(mode=0o700)
            try:
                staged.rename(destination)
            except BaseException:
                destination.rmdir()
                raise


def prepare_macos_upgrade(source, trial, destination_root, *, selected_extensions=None,
                          alarm_importer=None, mock_keychain=False):
    """Prepare a rollback snapshot and a separate macOS startup workspace."""
    if is_linked_path(source):
        raise ValueError("The source root must not be a link or reparse point")
    source = source.resolve()
    trial = separate_destination(source, trial)
    destination = separate_destination(source, destination_root)
    separate_destination(trial, destination)
    for directory in destination.parents:
        for name in ("ZitterWorkspaces.json", "Local State"):
            path = directory / name
            if path.exists() or is_linked_path(path):
                raise ValueError("The Content destination has a legacy profile ancestor")
    profile_count = prepare(source, trial)
    # Keep the verified baseline available even if the conversion fails.
    layout = "mock-keychain" if mock_keychain else "keychain"
    import_workspace_profiles(trial, destination, layout,
                              selected_extensions=selected_extensions,
                              alarm_importer=alarm_importer, require_session=True)
    return profile_count


def write_extension_alarm_state(candidate, profile, entries, destination, ids, importer):
    relative = Path(profile.name, EXTENSION_STATE)
    source = candidate / relative
    prefix = relative.as_posix() + "/"
    expected = {
        name[len(prefix):]: entry for name, entry in entries.items()
        if name.startswith(prefix)
    }
    if not expected and not source.exists() and not is_linked_path(source):
        return
    if (is_linked_path(source) or source.resolve() != source
            or any(entry.get("kind") != "file" or not re.fullmatch(
                r"(?:CURRENT|LOCK|LOG(?:\.old)?|MANIFEST-[0-9]+|[0-9]+\.(?:ldb|sst|log))",
                name,
            ) for name, entry in expected.items())
            or inventory(source) != expected):
        raise ValueError("Invalid or changed legacy extension state database")
    current = expected.get("CURRENT", {})
    if not 0 < current.get("size", 0) <= 64:
        raise ValueError("Invalid legacy extension state CURRENT file")
    active_manifest = (source / "CURRENT").read_bytes()
    if not re.fullmatch(rb"MANIFEST-[0-9]+\n", active_manifest) or (
        active_manifest[:-1].decode("ascii") not in expected
    ):
        raise ValueError("Invalid legacy extension state manifest")
    if importer is None:
        raise ValueError("An extension alarm importer is required for the selected profile")
    importer = importer.resolve()
    if not importer.is_file():
        raise ValueError("The extension alarm importer does not exist")

    with TemporaryDirectory(prefix=".zitter-extension-state-", dir=destination.parent) as scratch:
        copy = Path(scratch) / "state"
        copy_verified(source, copy, expected)
        try:
            subprocess.run(
                [importer, copy, destination / EXTENSION_STATE, *ids],
                check=True, capture_output=True, timeout=60,
            )
        except (subprocess.SubprocessError, OSError) as error:
            raise ValueError("Could not import verified extension alarm state") from error


def write_extension_storage(candidate, profile, entries, destination, extension_id,
                            *, include_install_state, indexed_install_state=False,
                            allow_alarm_only=False):
    relative_store = Path(profile.name, EXTENSION_STORAGE, extension_id)
    source = candidate / relative_store
    if is_linked_path(source) or source.resolve() != source:
        raise ValueError("The candidate extension store contains a linked path")
    prefix = relative_store.as_posix() + "/"
    expected = {
        name[len(prefix):]: entry for name, entry in entries.items()
        if name.startswith(prefix)
    }
    record = (
        legacy_extension_record(candidate, profile.name, extension_id, entries)
        if include_install_state else None
    )
    alarm_only = (
        allow_alarm_only and record is not None
        and "alarms" in record["active_permissions"]["api"]
        and not expected and relative_store.as_posix() not in entries
        and not source.exists()
    )
    if not alarm_only:
        current = expected.get("CURRENT", {})
        if current.get("kind") != "file" or not 0 < current.get("size", 0) <= 64:
            raise ValueError("The candidate has no recognized extension LevelDB")
        if inventory(source) != expected:
            raise ValueError("The candidate extension store changed after preparation")
        active_manifest = (source / "CURRENT").read_bytes()
        if not re.fullmatch(rb"MANIFEST-[0-9]+\n", active_manifest) or expected.get(
            active_manifest[:-1].decode("ascii"), {}
        ).get("kind") != "file":
            raise ValueError("The candidate LevelDB manifest is invalid")
        destination_store = destination / EXTENSION_STORAGE / extension_id
        destination_store.parent.mkdir(mode=0o700, exist_ok=True)
        copy_verified(source, destination_store, expected)

    receipt = json.dumps(record, sort_keys=True) if record is not None else None
    if receipt is not None and len(receipt) > MAX_LEGACY_EXTENSION_RECORD_BYTES:
        raise ValueError("The legacy extension record exceeds the Content size limit")
    if receipt is not None:
        # Keep the record directory as a guard when the local store is absent.
        if indexed_install_state or alarm_only:
            records = destination / LEGACY_EXTENSION_RECORDS
            records.mkdir(mode=0o700, exist_ok=True)
            record_path = records / f"{extension_id}.json"
        else:
            record_path = destination / LEGACY_EXTENSION_RECORD
        record_path.write_text(receipt, encoding="utf-8")
    return record


def copy_extension_storage(
    trial, profile_id, extension_id, destination_profile, *, include_install_state=False
):
    """Copy a verified candidate store into a new Content profile directory."""
    if is_linked_path(trial) or not EXTENSION_ID.fullmatch(extension_id):
        raise ValueError("Expected a real trial and a Chromium extension ID")
    trial = trial.resolve()
    destination = separate_destination(trial, destination_profile)
    manifest = read_trial_manifest(trial)
    profile = candidate_profile(trial, profile_id, manifest)
    destination.mkdir(mode=0o700)
    try:
        write_extension_storage(trial / "candidate", profile, manifest["entries"],
                                destination, extension_id,
                                include_install_state=include_install_state)
    except BaseException:
        shutil.rmtree(destination)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prepare_parser = commands.add_parser("prepare")
    prepare_parser.add_argument("source", type=Path, help="Offline copy or synthetic fixture")
    prepare_parser.add_argument("trial", type=Path, help="New trial directory")
    rollback_parser = commands.add_parser("rollback")
    rollback_parser.add_argument("trial", type=Path)
    rollback_parser.add_argument("destination", type=Path, help="New directory for the old backend")
    inspection_parser = commands.add_parser("inspect-windows-encryption")
    inspection_parser.add_argument("trial", type=Path, help="Trial prepared from an offline copy")
    extension_parser = commands.add_parser("copy-extension-storage")
    extension_parser.add_argument("trial", type=Path)
    extension_parser.add_argument("profile_id")
    extension_parser.add_argument("extension_id")
    extension_parser.add_argument("destination_profile", type=Path)
    extension_parser.add_argument("--include-install-state", action="store_true")
    clipboard_parser = commands.add_parser("copy-clipboard-permissions")
    clipboard_parser.add_argument("trial", type=Path)
    clipboard_parser.add_argument("profile_id")
    clipboard_parser.add_argument("destination_profile", type=Path)
    storage_parser = commands.add_parser("copy-browser-storage")
    storage_parser.add_argument("trial", type=Path)
    storage_parser.add_argument("profile_id")
    storage_parser.add_argument("destination_profile", type=Path)
    workspace_parser = commands.add_parser("import-workspace-profiles")
    workspace_parser.add_argument("trial", type=Path)
    workspace_parser.add_argument("destination_root", type=Path)
    workspace_parser.add_argument("--layout", choices=WORKSPACE_SESSION_FILES, required=True)
    upgrade_parser = commands.add_parser("prepare-macos-upgrade")
    upgrade_parser.add_argument("source", type=Path, help="Offline copy or synthetic fixture")
    upgrade_parser.add_argument("trial", type=Path, help="New rollback snapshot directory")
    upgrade_parser.add_argument("destination_root", type=Path, help="New Content startup directory")
    for command_parser in (workspace_parser, upgrade_parser):
        command_parser.add_argument("--work-extension", metavar="ID", action="append")
        command_parser.add_argument("--personal-extension", metavar="ID", action="append")
        command_parser.add_argument("--alarm-importer", type=Path,
                                    help="Local import_extension_alarms executable")
    options = parser.parse_args()
    try:
        if options.command == "prepare":
            count = prepare(options.source, options.trial)
            print(f"Prepared baseline and candidate for {count} profiles")
        elif options.command == "rollback":
            rollback(options.trial, options.destination)
            print("Verified baseline and created a fresh rollback copy")
        elif options.command == "inspect-windows-encryption":
            print(json.dumps(inspect_windows_encryption(options.trial), indent=2))
        elif options.command == "copy-clipboard-permissions":
            count = copy_clipboard_permissions(
                options.trial, options.profile_id, options.destination_profile)
            print(f"Copied {count} Clipboard rules into a new Content profile")
        elif options.command == "copy-browser-storage":
            copy_browser_storage(options.trial, options.profile_id, options.destination_profile)
            print("Copied verified browser storage into a new Content profile")
        elif options.command in ("import-workspace-profiles", "prepare-macos-upgrade"):
            selected = {name: value for name, value in (
                ("work", options.work_extension), ("personal", options.personal_extension),
            ) if value is not None}
            if options.command == "prepare-macos-upgrade":
                prepare_macos_upgrade(options.source, options.trial, options.destination_root,
                                      selected_extensions=selected, alarm_importer=options.alarm_importer)
                print("Prepared the macOS startup workspace and rollback baseline")
            else:
                import_workspace_profiles(options.trial, options.destination_root, options.layout,
                                          selected_extensions=selected,
                                          alarm_importer=options.alarm_importer)
                print("Prepared Content Workspace profiles")
        else:
            copy_extension_storage(
                options.trial, options.profile_id, options.extension_id,
                options.destination_profile, include_install_state=options.include_install_state,
            )
            print("Copied the verified extension store into a new Content profile")
    except (OSError, ValueError, TypeError, KeyError, sqlite3.Error) as error:
        print(f"Migration trial failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
