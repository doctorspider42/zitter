#!/usr/bin/env python3
"""Stage the Content Lab and shared UI into a dedicated Chromium checkout."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import content_blocking_patch as content_blocking
from dnr_cache_patch import patch_rules_monitor
import download_publish_patch as download_publish
import editor_service_patch as editor_services
import existing_keychain_patch as existing_keychain
import fingerprint_protection_patch as fingerprint_protection
from keychain_identity_patch import patch_keychain


ROOT = Path(__file__).resolve().parents[1]
LAB = ROOT / "chromium/content_embedder"
MARKER = ".zitter-content-presentation"
CHROME_METADATA_INPUTS = {"chrome/VERSION", "chrome/app/theme/chromium/BRANDING"}


def copy(source, destination):
    if destination.is_file() and source.read_bytes() == destination.read_bytes():
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    # A changed staged file must be newer than objects from the prior snapshot.
    shutil.copy(source, destination)


def verify_content_dependencies(checkout, output):
    target = "//zitter_content:zitter_content"
    graph = json.loads(subprocess.check_output(
        [str(checkout / "buildtools/mac/gn"), "desc", str(output), target, "deps",
         "--all", "--format=json", f"--root-target={target}"],
        cwd=checkout, text=True,
    ))
    labels = sorted(set(graph.get(target, {}).get("deps", [])))
    if not labels:
        raise ValueError("GN did not report the Content dependency graph")
    chrome = [label for label in labels if label.startswith(("//chrome/", "//chrome:"))]
    if chrome:
        raise ValueError(f"Content graph includes {len(chrome)} //chrome dependencies: "
                         + ", ".join(chrome[:10]))
    # A GN action can consume a source file without depending on a GN target.
    inputs = subprocess.check_output(
        [str(checkout / "third_party/ninja/ninja"), "-C", str(output),
         "-t", "inputs", "zitter_content"], cwd=checkout, text=True,
    )
    chrome_root = str(checkout / "chrome") + os.sep
    chrome_inputs = set()
    for entry in inputs.splitlines():
        path = os.path.normpath(output / entry)
        if path.startswith(chrome_root):
            chrome_inputs.add(os.path.relpath(path, checkout))
    unexpected = sorted(chrome_inputs - CHROME_METADATA_INPUTS)
    if unexpected:
        raise ValueError("Content build reads unexpected //chrome inputs: "
                         + ", ".join(unexpected[:10]))
    return {
        "gn_dependency_count": len(labels),
        "gn_dependencies_sha256": hashlib.sha256("\n".join(labels).encode()).hexdigest(),
        "chrome_metadata_inputs": sorted(chrome_inputs),
    }


def preview_build(checkout, output, ninja):
    # Ask Ninja to update just the GN graph using this output's regeneration
    # rule. A dry run alone can stop at GN regeneration and hide compilation.
    command = [str(ninja), "-C", str(output)]
    subprocess.run([*command, "build.ninja.stamp"], cwd=checkout, check=True)
    status = "ZITTER_ACTION "
    env = dict(os.environ, NINJA_STATUS=status)
    dry_run = subprocess.run(
        [*command, "-n", "zitter_content"], cwd=checkout, env=env,
        stdout=subprocess.PIPE, text=True, check=True,
    )
    actions = [line.removeprefix(status) for line in dry_run.stdout.splitlines()
               if line.startswith(status)]
    for action in actions[:15]:
        print(f"  {action}")
    if len(actions) > 15:
        print(f"  … {len(actions) - 15:,} more actions")
    print(f"Ninja scheduled {len(actions):,} actions for zitter_content in {output}.")
    if (actions and (output / ".ninja_deps").is_file()
            and not (output / ".ninja_log").is_file()):
        print("This output has dependency records but no Ninja command log. "
              "Ninja may rebuild existing outputs even when their inputs are unchanged.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path, help="Dedicated Chromium src checkout")
    parser.add_argument("--preview-build", type=Path, metavar="OUT",
                        help="After staging, preview Ninja actions in a configured Content output")
    args = parser.parse_args()
    checkout = args.checkout.resolve()
    if not (checkout / ".gn").is_file():
        parser.error(f"Chromium source root not found at {checkout}")
    revision = json.loads((ROOT / "chromium/revision.json").read_text())["revision"]
    head = subprocess.check_output(["git", "-C", checkout, "rev-parse", "HEAD"], text=True).strip()
    if head != revision:
        parser.error(f"Expected Chromium {revision}, found {head}")

    if args.preview_build is not None:
        output = (checkout / args.preview_build).resolve()
        manifest = output / "build.ninja"
        if (not output.is_relative_to(checkout / "out") or not manifest.is_file()
                or not any(line.startswith("build zitter_content: phony ")
                           for line in manifest.read_text(encoding="utf-8").splitlines())):
            parser.error("--preview-build requires an existing Content GN output in checkout/out")
        ninja = checkout / "third_party/ninja" / ("ninja.exe" if os.name == "nt" else "ninja")
        if not ninja.is_file():
            parser.error(f"Ninja not found at {ninja}")

    presentation = checkout / "zitter"
    build = presentation / "BUILD.gn"
    lab_root = checkout / "zitter_content"
    if presentation.is_symlink() or lab_root.is_symlink():
        parser.error("Choose a checkout with local staging directories")
    if (presentation / ".zitter-overlay.json").exists():
        parser.error("This checkout owns a Chrome overlay. Use a separate checkout.")
    if (build.exists() and not (presentation / MARKER).exists()
            and build.read_bytes() != (LAB / "presentation_root.gn").read_bytes()):
        parser.error(f"Refusing to replace an existing {build}")

    try:
        fingerprint_patch = fingerprint_protection.prepare(
            checkout, register_chrome_pref=False
        )
        plan = content_blocking.prepare(
            checkout, include_chrome=False, previous_plan=fingerprint_patch
        )
        dnr_monitor = (checkout / "extensions/browser/api/declarative_net_request"
                       / "rules_monitor_service.cc")
        if dnr_monitor.is_symlink() or not dnr_monitor.resolve().is_relative_to(checkout):
            raise ValueError("Extension rules monitor path escapes the checkout")
        original = dnr_monitor.read_bytes()
        plan[dnr_monitor] = (
            original, patch_rules_monitor(original.decode("utf-8")).encode("utf-8"),
        )
        runtime_api = checkout / "extensions/browser/api/runtime/runtime_api.cc"
        if runtime_api.is_symlink() or not runtime_api.resolve().is_relative_to(checkout):
            raise ValueError("Extension runtime API path escapes the checkout")
        original = runtime_api.read_bytes()
        old = b"""    context.tab_id = GetTabId(*web_contents);
    context.frame_id = GetFrameId(*host);
    context.window_id = GetWindowId(*web_contents);
"""
        new = b"""    ExtensionsBrowserClient::Get()->GetTabAndWindowIdForWebContents(
        web_contents, &context.tab_id, &context.window_id);
    context.frame_id = GetFrameId(*host);
"""
        updated = original
        if original.count(old) == 1 and new not in original:
            updated = updated.replace(old, new)
        elif original.count(new) != 1 or old in original:
            raise ValueError("Unexpected extension runtime context metadata")
        frame_context = b"    auto context_type = get_context_type(web_contents);"
        guard = b"""    // Content can observe an extension frame before its sandbox origin commits.
    if (host->GetLastCommittedOrigin() != extension()->origin() ||
        GetViewType(web_contents) == mojom::ViewType::kInvalid) {
      continue;
    }

"""
        if guard not in updated:
            if updated.count(frame_context) != 1:
                raise ValueError("Unexpected extension runtime frame context loop")
            updated = updated.replace(frame_context, guard + frame_context)
        if updated != original:
            plan[runtime_api] = (original, updated)
        for relative, before, after in (
            ('extensions/browser/api/offscreen/offscreen_document_manager.h',
             b'  // Closes the current offscreen document.',
             b'  bool AllowsClipboard(const Extension& extension, const OffscreenDocumentHost& host) const;\n\n  // Closes the current offscreen document.'),
            ('extensions/browser/api/offscreen/offscreen_document_manager.h',
             b'    std::unique_ptr<OffscreenDocumentHost> host;',
             b'    std::unique_ptr<OffscreenDocumentHost> host;\n    bool allows_clipboard = false;'),
            ('extensions/browser/api/offscreen/offscreen_document_manager.cc',
             b'  OffscreenDocumentHost* host = data.host.get();',
             b'  OffscreenDocumentHost* host = data.host.get();\n  data.allows_clipboard = reasons.contains(api::offscreen::Reason::kClipboard);'),
            ('extensions/browser/api/offscreen/offscreen_document_manager.cc',
             b'void OffscreenDocumentManager::CloseOffscreenDocumentForExtension(\n    const Extension& extension) {',
             b'bool OffscreenDocumentManager::AllowsClipboard(\n    const Extension& extension, const OffscreenDocumentHost& host) const {\n  auto entry = offscreen_documents_.find(extension.id());\n  return entry != offscreen_documents_.end() && entry->second.host.get() == &host &&\n         entry->second.allows_clipboard;\n}\n\nvoid OffscreenDocumentManager::CloseOffscreenDocumentForExtension(\n    const Extension& extension) {'),
            ('content/public/browser/clipboard_types.h',
             b'class WebContents;',
             b'class WebContents;\n\n// Optional browser-process policy checked at each frame clipboard write.\nusing FrameClipboardWritePolicy = bool (*)(RenderFrameHost*);\nCONTENT_EXPORT void SetFrameClipboardWritePolicy(FrameClipboardWritePolicy policy);'),
            ('content/browser/renderer_host/clipboard_host_impl.cc',
             b'class FrameClipboardContext : public ClipboardHostImpl::Context {',
             b'FrameClipboardWritePolicy frame_clipboard_write_policy = nullptr;\n\nclass FrameClipboardContext : public ClipboardHostImpl::Context {'),
            ('content/browser/renderer_host/clipboard_host_impl.cc',
             b'  bool CanWrite() override { return true; }',
             b'  bool CanWrite() override {\n    return !frame_clipboard_write_policy || frame_clipboard_write_policy(&*render_frame_host_);\n  }'),
            ('content/browser/renderer_host/clipboard_host_impl.cc',
             b'bool ClipboardHostImpl::IsWriteAllowed() {',
             b'void SetFrameClipboardWritePolicy(FrameClipboardWritePolicy policy) {\n  frame_clipboard_write_policy = policy;\n}\n\nbool ClipboardHostImpl::IsWriteAllowed() {'),
        ):
            source = checkout / relative
            if source.is_symlink() or not source.resolve().is_relative_to(checkout):
                raise ValueError(f"Clipboard overlay path escapes the checkout: {relative}")
            original = source.read_bytes()
            _, current = plan.get(source, (original, original))
            if current.count(after) == 1:
                continue
            if current.count(before) != 1:
                raise ValueError(f"Unexpected clipboard overlay source: {relative}")
            plan[source] = (original, current.replace(before, after))
        keychain = checkout / "components/os_crypt/common/keychain_password_mac.mm"
        if keychain.is_symlink() or not keychain.resolve().is_relative_to(checkout):
            raise ValueError("Keychain identity path escapes the checkout")
        keychain_original = keychain.read_bytes()
        plan[keychain] = (
            keychain_original,
            patch_keychain(keychain_original.decode("utf-8"), "Zitter Content Lab").encode("utf-8"),
        )
        renderer_build = checkout / "extensions/renderer/BUILD.gn"
        if renderer_build.is_symlink() or not renderer_build.resolve().is_relative_to(checkout):
            raise ValueError("Extension renderer build path escapes the checkout")
        renderer_original = renderer_build.read_bytes()
        chrome_resources = b'    "//chrome:resources",\n'
        content_comment = b"    # Zitter Content uses the extension resources below.\n"
        if renderer_original.count(chrome_resources) == 1:
            renderer_staged = renderer_original.replace(chrome_resources, content_comment)
        elif renderer_original.count(content_comment) == 1 and chrome_resources not in renderer_original:
            renderer_staged = renderer_original
        else:
            raise ValueError("Unexpected extension renderer resources dependency")
        plan[renderer_build] = (renderer_original, renderer_staged)
        messaging = checkout / "extensions/browser/api/messaging/message_service.cc"
        if messaging.is_symlink() or not messaging.resolve().is_relative_to(checkout):
            raise ValueError("Extension message service path escapes the checkout")
        original = messaging.read_bytes()
        old = b"""  if (!receiver_render_frame_host) {
    return nullptr;
  }

  return ExtensionMessagePort::CreateForTab("""
        new = b"""  if (!receiver_render_frame_host ||
      WebContents::FromRenderFrameHost(receiver_render_frame_host) !=
          receiver_contents) {
    return nullptr;
  }

  return ExtensionMessagePort::CreateForTab("""
        if original.count(old) == 1 and new not in original:
            messaging_staged = original.replace(old, new)
        elif original.count(new) != 1 or old in original:
            raise ValueError("Unexpected extension message document routing")
        else:
            messaging_staged = original
        old = b"messaging_delegate_->MaybeGetTabInfo(source_contents);"
        new = (b"messaging_delegate_->MaybeGetTabInfoForRecipient(source_contents,\n"
               b"                                                   *target_extension);")
        if messaging_staged.count(old) == 1 and new not in messaging_staged:
            messaging_staged = messaging_staged.replace(old, new)
        elif messaging_staged.count(new) != 1 or old in messaging_staged:
            raise ValueError("Unexpected extension message sender tab lookup")
        plan[messaging] = (original, messaging_staged)
        messaging_delegate = messaging.with_name("messaging_delegate.h")
        if messaging_delegate.is_symlink() or not messaging_delegate.resolve().is_relative_to(checkout):
            raise ValueError("Extension messaging delegate path escapes the checkout")
        original = messaging_delegate.read_bytes()
        anchor = b"  // Returns the WebContents for the given tab ID, if found.\n"
        method = b"""  virtual std::optional<base::DictValue> MaybeGetTabInfoForRecipient(
      content::WebContents* web_contents,
      const Extension& recipient);

"""
        if original.count(anchor) != 1 or original.count(method) > 1:
            raise ValueError("Unexpected extension messaging delegate declaration")
        staged = original if method in original else original.replace(anchor, method + anchor)
        plan[messaging_delegate] = (original, staged)
        delegate_source = messaging.with_name("messaging_delegate.cc")
        if delegate_source.is_symlink() or not delegate_source.resolve().is_relative_to(checkout):
            raise ValueError("Extension messaging delegate source escapes the checkout")
        original = delegate_source.read_bytes()
        anchor = b"content::WebContents* MessagingDelegate::GetWebContentsByTabId("
        method = b"""std::optional<base::DictValue> MessagingDelegate::MaybeGetTabInfoForRecipient(
    content::WebContents* web_contents,
    const Extension& recipient) {
  return MaybeGetTabInfo(web_contents);
}

"""
        if original.count(anchor) != 1 or original.count(method) > 1:
            raise ValueError("Unexpected extension messaging delegate definition")
        staged = original if method in original else original.replace(anchor, method + anchor)
        plan[delegate_source] = (original, staged)
        history = checkout / "content/browser/renderer_host/navigation_controller_impl.cc"
        if history.is_symlink() or not history.resolve().is_relative_to(checkout):
            raise ValueError("Navigation controller path escapes the checkout")
        original = history.read_bytes()
        anchor = b"bool NavigationControllerImpl::CanGoBack() {\n"
        history_hook = b"""// Zitter checks the actual target chosen by Chromium's history interventions.
CONTENT_EXPORT bool ZitterHistoryEntryHasPost(const NavigationEntry& entry) {
  auto has_post = [](auto&& self, const NavigationEntryImpl::TreeNode& node) -> bool {
    return (node.frame_entry && node.frame_entry->method() == "POST") ||
           std::ranges::any_of(node.children, [&](const auto& child) {
             return self(self, *child);
           });
  };
  return has_post(has_post, *static_cast<const NavigationEntryImpl&>(entry).root_node());
}

CONTENT_EXPORT bool ZitterGoThroughHistory(NavigationController& controller,
                                          bool forward) {
  auto& impl = static_cast<NavigationControllerImpl&>(controller);
  const auto target = forward ? impl.GetIndexForGoForward(true)
                              : impl.GetIndexForGoBack(true);
  if (!target || ZitterHistoryEntryHasPost(*impl.GetEntryAtIndex(*target))) {
    return false;
  }
  impl.GoToIndex(*target);
  return true;
}

"""
        if original.count(anchor) != 1:
            raise ValueError("Unexpected navigation history entry point")
        if history_hook not in original:
            if b"ZitterGoThroughHistory" in original:
                raise ValueError("Unexpected existing Zitter navigation history hook")
            plan[history] = (original, original.replace(anchor, history_hook + anchor))
        navigation = checkout / "content/browser/renderer_host/navigation_request.cc"
        if navigation.is_symlink() or not navigation.resolve().is_relative_to(checkout):
            raise ValueError("Navigation request path escapes the checkout")
        original = navigation.read_bytes()
        declaration_anchor = b"const blink::DocumentToken& NavigationRequest::GetDocumentToken() const {"
        commit_anchor = b"""    PopulateDocumentTokenForCrossDocumentNavigation();

    // Ensure the renderer does not reuse the document sequence number"""
        commit_callback = b"""namespace {

struct ZitterCommitCallback : base::SupportsUserData::Data {
  base::OnceCallback<void(const blink::DocumentToken&)> callback;
};

const char kZitterCommitCallbackKey = 0;

}  // namespace

CONTENT_EXPORT void ZitterBeforeDocumentCommit(
    NavigationHandle& navigation,
    base::OnceCallback<void(const blink::DocumentToken&)> callback) {
  auto data = std::make_unique<ZitterCommitCallback>();
  data->callback = std::move(callback);
  navigation.SetUserData(&kZitterCommitCallbackKey, std::move(data));
}

"""
        commit_dispatch = commit_anchor.replace(
            b"\n\n",
            b"""
    if (auto* data = static_cast<ZitterCommitCallback*>(
            GetUserData(&kZitterCommitCallbackKey))) {
      std::move(data->callback).Run(GetDocumentToken());
      RemoveUserData(&kZitterCommitCallbackKey);
    }

""", 1)
        if (original.count(declaration_anchor) != 1 or
                original.count(commit_anchor) + original.count(commit_dispatch) != 1):
            raise ValueError("Unexpected document commit logic")
        if commit_callback + declaration_anchor not in original:
            if b"ZitterBeforeDocumentCommit" in original or commit_dispatch in original:
                raise ValueError("Unexpected existing document commit hook")
            staged = original.replace(declaration_anchor, commit_callback + declaration_anchor)
            plan[navigation] = (original, staged.replace(commit_anchor, commit_dispatch))
        elif commit_dispatch not in original:
            raise ValueError("Missing document commit hook dispatch")
        tiling = checkout / "cc/tiles/picture_layer_tiling.cc"
        if tiling.is_symlink() or not tiling.resolve().is_relative_to(checkout):
            raise ValueError("Picture tiling path escapes the checkout")
        original = tiling.read_bytes()
        anchor = b"  // We can't use SetPriorityRect(EVENTUALLY_RECT)"
        reset_offscreen_tiles = b"""  // A clipped, scrolled layer can shrink beyond its old priority rect
  // without changing the tile size. The final partial tile can still have
  // a valid index even though its new bounds are outside the priority rect.
  if (has_eventually_rect_tiles_ &&
      !tiling_rect.Intersects(current_eventually_rect_)) {
    SetTilingRect(tiling_rect);
    Reset();
    return true;
  }

"""
        if original.count(anchor) != 1:
            raise ValueError("Unexpected picture tiling resize logic")
        if reset_offscreen_tiles + anchor not in original:
            if reset_offscreen_tiles in original:
                raise ValueError("Unexpected existing picture tiling resize hook")
            plan[tiling] = (original, original.replace(anchor, reset_offscreen_tiles + anchor))
        partition = checkout / "content/browser/storage_partition_impl.cc"
        if partition.is_symlink() or not partition.resolve().is_relative_to(checkout):
            raise ValueError("Storage partition source escapes the checkout")
        original = partition.read_bytes()
        dictionary_path = b"""      context_params->file_paths->shared_dictionary_directory =
          partition_path_.Append(FILE_PATH_LITERAL("Shared Dictionary"));"""
        respect_embedder_path = b"""      if (!context_params->file_paths->shared_dictionary_directory) {
        context_params->file_paths->shared_dictionary_directory =
            partition_path_.Append(FILE_PATH_LITERAL("Shared Dictionary"));
      }"""
        if original.count(dictionary_path) + original.count(respect_embedder_path) != 1:
            raise ValueError("Unexpected shared dictionary storage configuration")
        if dictionary_path in original:
            plan[partition] = (original, original.replace(dictionary_path, respect_embedder_path))
        plan.update(existing_keychain.prepare(checkout))
        plan.update(editor_services.prepare(checkout))
        plan.update(download_publish.prepare(checkout))
        content_blocking.apply(plan)
    except (OSError, ValueError) as error:
        parser.error(f"Content browser hooks staging failed: {error}")

    files = {
        path.relative_to(ROOT): path
        for directory in ("chromium/assistant", "chromium/night_shift", "chromium/prototype",
                          "chromium/renderer", "zitter")
        for path in (ROOT / directory).rglob("*")
        if path.is_file()
    }
    for path in (
        "chromium/app/compile_ui_lab_metal.py",
        "chromium/app/zitter_version.h",
        "chromium/app/mac_release_metadata.cc",
        "chromium/app/mac_release_metadata.h",
        "chromium/app/mac_release_metadata_probe.cc",
        "chromium/app/network_policy.cc",
        "chromium/app/network_policy.h",
        "chromium/app/network_policy_components.inc",
        "chromium/app/network_policy_features.inc",
        "chromium/app/updater_mac.cc",
        "chromium/app/updater_mac.h",
        "tests/fixtures/assistant_cli.cc",
        "third_party/brave_clean_urls/clean-urls.json",
    ):
        files[Path(path)] = ROOT / path
    files[Path("BUILD.gn")] = LAB / "presentation_root.gn"
    for path, source in files.items():
        copy(source, presentation / path)
    (presentation / MARKER).touch(exist_ok=True)
    for source in LAB.iterdir():
        if source.is_file() and source.name != "presentation_root.gn":
            copy(source, lab_root / source.name)
    copy(ROOT / "assets/icon/Zitter.icns", lab_root / "Zitter.icns")
    # Reuse Chromium's renderer hooks and function signatures without linking
    # the Chrome renderer target or exposing the rest of the tabs namespace.
    def pinned_source(relative_path):
        return subprocess.check_output(
            ["git", "-C", str(checkout), "show", f"{revision}:{relative_path}"]
        )

    for name in ("tabs_hooks_delegate.h", "tabs_hooks_delegate.cc"):
        source = pinned_source(f"chrome/renderer/extensions/api/{name}").replace(
            b'"chrome/renderer/extensions/api/tabs_hooks_delegate.h"',
            b'"tabs_hooks_delegate.h"',
        )
        destination = lab_root / name
        if not destination.exists() or destination.read_bytes() != source:
            destination.write_bytes(source)
    parsed = json.loads(subprocess.check_output(
        [sys.executable, str(checkout / "tools/json_comment_eater/json_comment_eater.py")],
        input=pinned_source("chrome/common/extensions/api/tabs.json"),
    ))
    function_names = {
        "sendMessage", "connect", "query", "create", "update", "remove", "move",
        "reload", "duplicate", "goBack", "goForward", "get", "getCurrent", "discard",
    }
    tab_functions = [item for item in parsed[0]["functions"]
                     if item["name"] in function_names]
    if {item["name"] for item in tab_functions} != function_names:
        parser.error("Unexpected Chromium tabs function schema")
    query = next(item for item in tab_functions if item["name"] == "query")
    query_fields = {
        "active", "pinned", "audible", "muted", "highlighted", "currentWindow",
        "lastFocusedWindow", "status", "title", "url", "windowId", "windowType", "index",
        "discarded",
    }
    query_info = query["parameters"][0]
    query_info["properties"] = {name: spec for name, spec in query_info["properties"].items()
                                if name in query_fields}
    mutation_fields = {"create": {"active", "url", "windowId"},
                       "update": {"active", "url", "muted"}}
    for function in tab_functions:
        if function["name"] in mutation_fields:
            options = function["parameters"][-1]
            options["properties"] = {name: spec for name, spec in options["properties"].items()
                                     if name in mutation_fields[function["name"]]}
    tab_fields = {
        "id", "index", "windowId", "incognito", "active", "highlighted", "selected",
        "pinned", "audible", "discarded", "mutedInfo", "status", "url",
        "pendingUrl", "title",
    }
    type_names = {"Tab", "TabStatus", "WindowType", "MutedInfo", "MutedInfoReason"}
    tab_types = [item for item in parsed[0]["types"] if item["id"] in type_names]
    tab_type = next(item for item in tab_types if item["id"] == "Tab")
    tab_type["properties"] = {name: spec for name, spec in tab_type["properties"].items()
                              if name in tab_fields}

    def qualify_api_types(value, namespace, local_types):
        if isinstance(value, dict):
            if value.get("$ref") in local_types:
                value["$ref"] = f'{namespace}.{value["$ref"]}'
            for child in value.values():
                qualify_api_types(child, namespace, local_types)
        elif isinstance(value, list):
            for child in value:
                qualify_api_types(child, namespace, local_types)

    for tab_type in tab_types:
        tab_type["id"] = f'tabs.{tab_type["id"]}'
    event_names = {"onCreated", "onActivated", "onRemoved", "onUpdated", "onMoved"}
    tab_events = [item for item in parsed[0]["events"] if item["name"] in event_names]
    if {item["name"] for item in tab_events} != event_names:
        parser.error("Unexpected Chromium tabs event schema")
    updated = next(item for item in tab_events if item["name"] == "onUpdated")
    changes = updated["parameters"][1]
    update_fields = {"status", "url", "title", "pinned", "audible", "discarded", "mutedInfo"}
    changes["properties"] = {name: spec for name, spec in changes["properties"].items()
                             if name in update_fields}
    schema = {"namespace": "tabs", "types": tab_types, "functions": tab_functions,
              "events": tab_events}
    qualify_api_types(schema, "tabs", type_names)
    schema = json.dumps(schema, separators=(",", ":"))
    schema_file = lab_root / "tabs_messaging_schema.inc"
    schema_source = 'R"zitter(' + schema + ')zitter"\n'
    if not schema_file.exists() or schema_file.read_text() != schema_source:
        schema_file.write_text(schema_source, encoding="utf-8")
    navigation_source = checkout / "chrome/common/extensions/api/web_navigation.json"
    navigation = json.loads("\n".join(line for line in navigation_source.read_text().splitlines()
                                      if not line.lstrip().startswith("//")))[0]
    navigation_events = {"onBeforeNavigate", "onCommitted", "onDOMContentLoaded",
                         "onCompleted", "onErrorOccurred", "onHistoryStateUpdated",
                         "onReferenceFragmentUpdated"}
    navigation["events"] = [event for event in navigation["events"]
                            if event["name"] in navigation_events]
    if {event["name"] for event in navigation["events"]} != navigation_events:
        parser.error("Unexpected Chromium webNavigation event schema")
    navigation_types = {item["id"] for item in navigation["types"]}
    for item in navigation["types"]:
        item["id"] = f'webNavigation.{item["id"]}'
    qualify_api_types(navigation, "webNavigation", navigation_types)
    navigation_file = lab_root / "web_navigation_schema.inc"
    navigation_schema = 'R"zitter(' + json.dumps(navigation, separators=(",", ":")) + ')zitter"\n'
    if not navigation_file.exists() or navigation_file.read_text() != navigation_schema:
        navigation_file.write_text(navigation_schema, encoding="utf-8")
    panel_source = checkout / "chrome/common/extensions/api/side_panel.idl"
    idl_compiler = checkout / "tools/json_schema_compiler/idl_schema.py"
    panel = json.loads(subprocess.check_output(
        [sys.executable, str(idl_compiler), str(panel_source)], cwd=checkout, text=True))[0]
    panel.pop("manifest_keys", None)
    panel_types = {item["id"] for item in panel["types"]}
    for item in panel["types"]:
        item["id"] = f'sidePanel.{item["id"]}'
    qualify_api_types(panel, "sidePanel", panel_types)
    panel_file = lab_root / "side_panel_schema.inc"
    panel_schema = 'R"zitter(' + json.dumps(panel, separators=(",", ":")) + ')zitter"\n'
    if not panel_file.exists() or panel_file.read_text() != panel_schema:
        panel_file.write_text(panel_schema, encoding="utf-8")
    notifications_source = checkout / "chrome/common/extensions/api/notifications.webidl"
    web_idl_compiler = checkout / "tools/json_schema_compiler/web_idl_schema.py"
    notifications = json.loads(subprocess.check_output(
        [sys.executable, str(web_idl_compiler), str(notifications_source)],
        cwd=checkout, text=True,
    ))[0]
    notification_types = {item["id"] for item in notifications["types"]}
    for item in notifications["types"]:
        item["id"] = f'notifications.{item["id"]}'
    qualify_api_types(notifications, "notifications", notification_types)
    notifications_file = lab_root / "notifications_schema.inc"
    notifications_schema = 'R"zitter(' + json.dumps(notifications, separators=(",", ":")) + ')zitter"\n'
    if not notifications_file.exists() or notifications_file.read_text() != notifications_schema:
        notifications_file.write_text(notifications_schema, encoding="utf-8")
    privacy = json.loads(subprocess.check_output(
        [sys.executable, str(checkout / "tools/json_comment_eater/json_comment_eater.py")],
        input=pinned_source("chrome/common/extensions/api/privacy.json"),
    ))[0]
    services = privacy["properties"]["services"]
    services["properties"] = {
        name: services["properties"][name]
        for name in ("passwordSavingEnabled", "autofillAddressEnabled", "autofillCreditCardEnabled")
    }
    privacy["properties"] = {"services": services}
    privacy.pop("types", None)
    privacy_file = lab_root / "privacy_schema.inc"
    privacy_schema = 'R"zitter(' + json.dumps(privacy, separators=(",", ":")) + ')zitter"\n'
    if not privacy_file.exists() or privacy_file.read_text() != privacy_schema:
        privacy_file.write_text(privacy_schema, encoding="utf-8")
    messaging = "chrome/browser/extensions/api/messaging/"
    for name in ("native_message_process_host.h", "native_message_process_host.cc",
                 "native_process_launcher.h", "chrome_native_message_port_dispatcher.h",
                 "chrome_native_message_port_dispatcher.cc"):
        source = pinned_source(messaging + name).decode()
        source = source.replace(messaging + "native_message_process_host.h",
                                "native_message_process_host.h")
        source = source.replace(messaging + "native_process_launcher.h",
                                "native_process_launcher.h")
        if name.startswith("chrome_native_message_port_dispatcher"):
            source = source.replace(messaging + "chrome_native_message_port_dispatcher.h",
                                    "workspace_native_message_port_dispatcher.h")
            source = source.replace("ChromeNativeMessagePortDispatcher",
                                    "WorkspaceNativeMessagePortDispatcher")
            name = name.replace("chrome_", "workspace_", 1)
        if name == "native_message_process_host.cc":
            source = "\n".join(line for line in source.splitlines()
                               if not line.startswith('#include "chrome/')) + "\n"
            start = source.index("base::FilePath GetProfilePathIfEnabled(")
            stop = source.index("}  // namespace", start)
            source = source[:start] + source[stop:]
            start = source.index("// static\nstd::unique_ptr<NativeMessageHost> NativeMessageHost::Create(")
            stop = source.index("// static", start + len("// static"))
            source = source[:start] + source[stop:]
            source = source.replace("NM_LOG(ERROR)", "LOG(ERROR)")
        destination = lab_root / name
        if not destination.exists() or destination.read_text() != source:
            destination.write_text(source, encoding="utf-8")
    print(f"Staged Content Lab in {checkout}", flush=True)
    if args.preview_build is not None:
        try:
            preview_build(checkout, output, ninja)
        except (OSError, subprocess.CalledProcessError) as error:
            parser.exit(1, f"Content build preview failed: {error}\n")


if __name__ == "__main__":
    main()
