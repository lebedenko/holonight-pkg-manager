# Updates page manual acceptance checklist

REQ-NF-008: visual correctness is checked by the user, not by the assistant or by screenshot tooling. Automated
tests (`updates_model_test`, `updates_view_test`, `alpm_update_source_test`) cover state and data only.

Status: **not yet run.**

## Setup

Run each section once per theme. Pick the theme with a scratch appearance file:

```sh
printf 'version = 1\n\n[theme]\nscheme = "holonight-dark"\n'  > /tmp/hn-dark.toml
printf 'version = 1\n\n[theme]\nscheme = "holonight-light"\n' > /tmp/hn-light.toml

HOLONIGHT_APPEARANCE_FILE=/tmp/hn-dark.toml task run    # then again with /tmp/hn-light.toml
```

`task run` reads the live `/var/lib/pacman` and `/etc/pacman.conf` and does not modify them. For the states the
live system cannot produce on demand (stale data, no databases, reload failure), section C runs the application
against a **scratch copy** of the databases. The original databases are only read. This recipe was not run during
implementation; adjust it if your session needs different display variables.

```sh
mkdir -p /tmp/hn-updates
cp -a /var/lib/pacman/local /var/lib/pacman/sync /tmp/hn-updates/
touch -d '10 days ago' /tmp/hn-updates/sync/*.db
# Private mount namespace: /var/lib/pacman is replaced by the scratch copy for this process only.
sudo --preserve-env=WAYLAND_DISPLAY,XDG_RUNTIME_DIR,DISPLAY,HOLONIGHT_APPEARANCE_FILE \
  unshare --mount sh -c 'mount --bind /tmp/hn-updates /var/lib/pacman &&
    exec sudo -u "$SUDO_USER" --preserve-env=WAYLAND_DISPLAY,XDG_RUNTIME_DIR,DISPLAY,HOLONIGHT_APPEARANCE_FILE \
      env LD_LIBRARY_PATH="$PWD/build/dependencies/prefix/lib" "$PWD/build/holonight-packages"'
```

Remove `/tmp/hn-updates` afterwards.

## A. Navigation (live system)

| # | Check | Dark | Light |
|---|---|---|---|
| A1 | The app opens on **Installed**; the sidebar highlights Installed | ☐ | ☐ |
| A2 | Clicking **Updates** in the sidebar shows the Updates page and moves the highlight; clicking Installed switches back with the Installed page state (tab, selection) unchanged | ☐ | ☐ |

## B. Updates page (live system)

| # | Check | Dark | Light |
|---|---|---|---|
| B1 | While the first check runs: centered loading state, indeterminate progress bar, button reads "Loading…" and is disabled | ☐ | ☐ |
| B2 | With pending updates: headline card shows "N updates", total download size and "Data as of …" in local time, plus the "AUR and foreign packages are not covered." note | ☐ | ☐ |
| B3 | List layout: columns Package, Version (`old → new`), Repository, Download, Size change; header aligned with rows; download and size change right-aligned; negative deltas start with "-" | ☐ | ☐ |
| B4 | Scrolling: the list scrolls vertically with its own scroll bar; at the 720×480 minimum window, header and rows scroll horizontally together and no column is clipped out of reach | ☐ | ☐ |
| B5 | Ignored badge: temporarily add a pending package to `IgnorePkg` in `/etc/pacman.conf` (or use a group via `IgnoreGroup`), press Reload. The row stays listed with muted text and an "Ignored" badge (tooltip mentions IgnorePkg/IgnoreGroup). The headline count and total drop accordingly and show "1 ignored, not counted". Revert the edit | ☐ | ☐ |
| B6 | Reload busy state: pressing **Reload** keeps the list visible, shows the progress bar and disables the button until done | ☐ | ☐ |
| B7 | Up-to-date state (on a fully upgraded system): "All official-repository packages are up to date." with the official-only note; headline shows "0 updates" | ☐ | ☐ |
| B8 | No text is truncated or overlapping at 1360×890 and at 720×480 | ☐ | ☐ |

## C. Edge states (scratch copy, see Setup)

| # | Check | Dark | Light |
|---|---|---|---|
| C1 | Stale hint: with the 10-day-old copy, a warning notice reads "Your package databases are out of date. Sync them with your package manager, then press Reload." It names no command | ☐ | ☐ |
| C2 | Picking up a manual sync: `touch /tmp/hn-updates/sync/*.db`, press Reload. "Data as of" moves to now and the stale hint disappears | ☐ | ☐ |
| C3 | Reload banner: `chmod 000 /tmp/hn-updates/sync`, press Reload. A red-bordered notice reads "Reload failed: … Showing data from …" and the previous list stays visible and scrollable. `chmod 755` it and press Reload: the banner disappears | ☐ | ☐ |
| C4 | No-databases state: `mv /tmp/hn-updates/sync /tmp/hn-updates/sync.off`, press Reload. A centered "No package databases found." message with the pacman.conf explanation, visibly different from the up-to-date state. Move it back afterwards | ☐ | ☐ |

## Result

| Theme | Tester | Date | Result / notes |
|---|---|---|---|
| Dark | | | |
| Light | | | |
