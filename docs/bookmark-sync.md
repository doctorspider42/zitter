# Workspace sync

In first-run setup, choose a folder or connect Dropbox. Continue to the next
step to keep bookmarks local. Both options are also available in
**Settings → Passwords and profiles → Workspace sync**. Turning off sync keeps
your local state and the files at the destination.

## Import from another device

When you connect to a destination that contains synced data, the preview shows
the number of incoming profiles, spaces, tabs, and bookmarks, along with the
space names and their tab counts. You can merge this copy with your local
bookmarks and tabs or replace the local sync data with the downloaded copy.
Merging keeps both sets, with synced items first.
Replacing requires a separate confirmation. When the download contains spaces,
it removes local-only spaces and can restore spaces previously removed on this
device. Spaces containing live folders are kept. If the download contains only
profile favorites, the local space list is kept. Passwords and live folders stay
local.

The first import can also install missing Chrome Web Store extensions for the
corresponding profiles. This option is off by default. Zitter downloads the
signed packages from the store and shows each extension's permissions for
approval. Subsequent syncs do not install extensions.

## Folder

Select the same shared folder on each device. A cloud desktop client, Syncthing,
or a network share can carry the files. Zitter creates a **Zitter Bookmarks**
subfolder. Under `sync-v2`, each device writes its own small index. The indexes
refer to verified files shared by content hash. These files contain tab and
bookmark URLs, titles, and folder names in readable form. Choose a location
accessible only to the people who should see them.

## Dropbox

Zitter opens the Dropbox authorization page in a foreground tab in the current
workspace profile. The tab handles the registered redirect before a network
request is sent. Zitter uses PKCE and stores an encrypted refresh token in the
local profile. The Dropbox desktop application is optional.

The official build uses the public client ID for Zitter Sync. To distribute a fork with a separate Dropbox app, register a scoped **App folder** application in
the [Dropbox App Console](https://www.dropbox.com/developers/apps). Enable
public clients and select `files.content.read`, `files.content.write`, and
`files.metadata.read` in the app's permissions. Zitter uses the permissions
configured for that app when authorizing Dropbox. Register the exact redirect
URI `http://127.0.0.1:53682/dropbox/callback`. Set the public client ID in the existing
Chromium output directory's GN arguments before building:

```gn
zitter_dropbox_app_key = "<public app key>"
```

Use the same key for macOS and Windows. For local testing, set the environment
variable `ZITTER_DROPBOX_APP_KEY` when launching the application. Request
production approval in the Dropbox App Console for public distribution.

## What syncs

Zitter synchronizes profile favorites, pinned pages, their custom titles,
folders and ordering. It also carries workspace names, colors, icons and order,
and the IDs of Chrome Web Store extensions available for the first import.
A pinned page or profile favorite appears in its pinned section on each device.
Its open page and navigation stay in the local session. Renaming an open pin
renames the pin, so the name syncs. Sessions before 0.26.4 kept that name on
the open tab, which never synced. The next sync moves those names to their
pins, unless the pin already has a name.
Zitter fetches page titles and favicons for incoming tabs and favicons for
incoming bookmarks without sending site cookies or opening the tabs.
A new device can create the matching workspaces and profiles and reopen web tabs
in their saved order. Sync transfers their HTTP and HTTPS addresses. Private
windows, internal pages, form submissions, browsing history, cookies, passwords,
and live-folder credentials stay local. Tabs that need confirmation before
closing stay open until the workspace can safely apply the remote layout.

While running, Zitter reconciles changes on startup. Local changes are sent
about 1.5 seconds after the last edit. For Dropbox, Zitter keeps one long
poll open and checks the shared copy when the sync index changes. The
periodic check runs every five minutes while Dropbox notifications work,
or every 30 seconds without them or when using a shared folder. Existing
Dropbox connections without `files.metadata.read` can be reconnected to
authorize notifications once that permission is enabled for the Dropbox app.

Choose **Sync now** under **Settings → Passwords and profiles → Workspace sync**
to save the current session and then check for changes. If the session fails
to save, Zitter skips that sync attempt.
Independent edits to different bookmarks merge. Concurrent updates to the same
bookmark resolve by version. A deletion wins over an update at the same
version. Removing a bookmark does not rewrite the positions of the remaining
bookmarks. Workspace names, colors, icons, positions and profile assignments
reconcile separately.
Deletion records survive offline periods. Dropbox uploads use the file's
revision so concurrent writers can retry against the newest version. Open tabs
have stable IDs. Two devices can add, update, or close different tabs in the
same space and keep both changes. If two devices edit the same tab, the version
and device ID determine which edit applies.

### Remove a space or profile from sync

When deleting a synced space or an unused browsing profile, select **Also
remove from the cloud** to remove it from the shared copy. The deletion is
queued while offline and survives a restart. Turning off sync cancels any
deletion that has yet to be published. Update every connected device so it
recognizes shared deletions.

Other devices close a removed space when its tabs can safely close. Spaces
with live folders and the last local space can remain available locally.
Deleting a profile from sync removes its synced favorites and extension list
from the shared copy. Other devices keep their local browsing profile and its
credentials. Spaces still associated with the deleted profile are omitted from
sync until assigned to another synced profile.

## Storage and scaling

The index maps each workspace to a file with its tabs and pinned pages, and
each profile to a file with its favorites and extension IDs. Devices reuse
verified files when their hashes match, including data retained from a
previous run. Writers publish changed files before
updating the index. Dropbox checks the current index revision on write and
retries a conflicting update against the latest index. Folder indexes are
written atomically. A missing or corrupt referenced file pauses sync until a
complete index can be read.

Each data file is limited to 5 MiB. The limit applies per workspace or profile.
Deletion records and previously published immutable file versions are retained.
If concurrent changes exceed the local tab or bookmark limit, the shared copy
keeps the overflow and the device opens the items that fit. Other spaces can
continue syncing.

### Upgrade from the original format

Zitter can read the original shared-folder JSON files and the original Dropbox
ledger. The first successful v2 write leaves the legacy files as a backup.
Upgrade every device before relying on changes flowing between them in both
directions. Older versions cannot read the new indexes.
Local sessions saved by this version may contain stable tab IDs. Use a build
that understands those IDs to open the updated local session.
