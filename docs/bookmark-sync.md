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
Replacing requires a separate confirmation and can remove local bookmarks or
close local tabs. Passwords and live folders stay local.

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
public clients and the `files.content.read` and `files.content.write`
permissions. Register the exact redirect URI `http://127.0.0.1:53682/dropbox/callback`. Set the public client ID in the existing
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
Zitter fetches favicons for incoming bookmarks without sending site cookies.
A new device can create the matching workspaces and profiles and reopen web tabs
in their saved order. Sync transfers their HTTP and HTTPS addresses. Private
windows, internal pages, form submissions, browsing history, cookies, passwords,
and live-folder credentials stay local. Tabs that need confirmation before
closing stay open until the workspace can safely apply the remote layout.

While running, Zitter reconciles changes on startup and every 30 seconds.
Choose **Sync now** under **Settings → Passwords and profiles → Workspace sync**
to save the current session and then check for changes. If the session fails
to save, Zitter skips that sync attempt.
Independent edits to different bookmarks merge. Concurrent updates to the same
bookmark resolve by version, with a stable device ID as the tiebreaker.
Deletion records survive offline periods. Dropbox uploads use the file's
revision so concurrent writers can retry against the newest version. Open tabs
have stable IDs. Two devices can add, update, or close different tabs in the
same space and keep both changes. If two devices edit the same tab, the version
and device ID determine which edit applies.

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

### Upgrade from the original format

Zitter can read the original shared-folder JSON files and the original Dropbox
ledger. The first successful v2 write leaves the legacy files as a backup.
Upgrade every device before relying on changes flowing between them in both
directions. Older versions cannot read the new indexes.
Local sessions saved by this version may contain stable tab IDs. Use a build
that understands those IDs to open the updated local session.
