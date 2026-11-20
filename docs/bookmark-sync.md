# Bookmark sync

In first-run setup, choose a folder or connect Dropbox. Continue to the next
step to keep bookmarks local. Both options are also available in
**Settings → Passwords and profiles → Bookmark sync**. Turning off sync keeps
the bookmarks on the device and the files at the destination.

## Folder

Select the same shared folder on each device. A cloud desktop client, Syncthing,
or a network share can carry the files. Zitter creates a **Zitter Bookmarks**
subfolder. Each device writes its own JSON file. These files contain bookmark
URLs, titles, and folder names in readable form. Choose a location accessible
only to the people who should see them.

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
folders and ordering. A new device can create the workspace and profile needed
for an incoming bookmark. Open tabs, browsing history, cookies, passwords and
live-folder credentials stay local.

While running, Zitter reconciles changes on startup and every 30 seconds.
Independent edits to different bookmarks merge. Concurrent updates to the same
bookmark resolve by version, with a stable device ID as the tiebreaker.
Deletion records survive offline periods. Dropbox uploads use the file's
revision so concurrent writers can retry against the newest version.
