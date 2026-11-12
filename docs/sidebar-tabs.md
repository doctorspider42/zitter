# Sidebar tabs

Choose **Rename Tab** from a tab's menu to edit its name in the sidebar.
Press Enter or click the checkmark to save, or press Escape to cancel.
**Restore Page Title** clears the custom name on both pinned and temporary tabs.
An open pin shows a return arrow when its current URL differs from its saved URL.
Click the arrow to load the saved URL in that tab.

Right-click a tab to duplicate it, move it to another workspace, or pin it to the current profile. Duplicate tab also uses **Command+Shift+D** on macOS and **Ctrl+Shift+D** on Windows; the shortcut can be changed in settings. A duplicate is an independent tab with its own copy of the navigation history.

Moving within a profile preserves the live page and navigation history. Moving to a workspace in another profile reloads the URL with that profile’s storage and login session. Moving the final tab leaves an empty page in the source workspace. Sleeping destinations wake before receiving a tab.

Drag a tab to the top of the sidebar (the address and New Tab area), or onto an existing profile favorite, to pin it to the profile. Profile favorites appear as compact icon tiles in every workspace assigned to that profile. Workspace pins and folders keep their existing scope. Unpinning a profile favorite removes it across the profile and keeps already opened pages as ordinary tabs.

Favicons come from Chromium’s page favicon driver and the profile’s local favicon database. The compositor and Metal use the same clipped bitmap recipe. Pages without an available favicon show a letter from the site name; RSS keeps its RSS icon. Favorites retain their titles in tooltips and accessibility labels.

In the Command Center, run `> Refresh all favicons`. Zitter fetches page markup and icons for tabs and pinned pages in the current workspace, bypassing the cache. It updates the profile’s favicon database without reloading the open pages. Missing icons fall back to letters.

When importing from Arc, Chrome, Edge, Brave, Firefox, Opera, or Vivaldi, Zitter reads cached PNG icons for pins and favorites from the source profile's favicon database. For uncached icons it tries the site's default favicon resource in the background. The imported links stay unopened and the current workspace stays selected. Reimporting also fills in icons for previously imported Spaces.

Right-click the empty sidebar area or the workspace icon and choose
**New Folder** to add a folder. You can also use **+** in the sidebar.

Live Folders are hidden and do not refresh by default. Existing folder
settings and cached items are kept in the workspace configuration.
