# Sidebar tabs

Right-click a tab to duplicate it, move it to another workspace, or pin it to the current profile. Duplicate tab also uses **Command+Shift+D** on macOS and **Ctrl+Shift+D** on Windows; the shortcut can be changed in settings. A duplicate is an independent tab with its own copy of the navigation history.

Moving within a profile preserves the live page and navigation history. Moving to a workspace in another profile reloads the URL with that profile’s storage and login session. Moving the final tab leaves an empty page in the source workspace. Sleeping destinations wake before receiving a tab.

Drag a tab to the top of the sidebar (the address and New Tab area), or onto an existing profile favorite, to pin it to the profile. Profile favorites appear as compact icon tiles in every workspace assigned to that profile. Workspace pins and folders keep their existing scope. Unpinning a profile favorite removes it across the profile and keeps already opened pages as ordinary tabs.

Favicons come from Chromium’s page favicon driver and the profile’s local favicon database. The compositor and Metal use the same clipped bitmap recipe. Pages without an available favicon show a letter from the site name; RSS keeps its RSS icon. Favorites retain their titles in tooltips and accessibility labels.

Right-click a workspace in the footer and choose **Reload workspace icons** to fetch its tab and pinned-page icons again, bypassing the image cache. This updates the profile’s favicon database without reloading the open pages. Missing or failed icons fall back to letters.
