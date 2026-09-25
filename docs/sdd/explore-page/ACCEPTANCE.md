# Explore page manual acceptance checklist

REQ-NF-011: visual correctness is checked by the user, not by the assistant or by screenshot tooling. Automated
tests (`explore_model_test`, `explore_view_test`, `explore_search_test`, `alpm_explore_source_test`) cover state,
ranking and data only.

Status: **Passed in light and dark themes, including the follow-up layout and copy changes; reported by the user on 2026-09-25.**

## Setup

Run each section once per theme. Pick the theme with a scratch appearance file:

```sh
printf 'version = 1\n\n[theme]\nscheme = "holonight-dark"\n'  > /tmp/hn-dark.toml
printf 'version = 1\n\n[theme]\nscheme = "holonight-light"\n' > /tmp/hn-light.toml

HOLONIGHT_APPEARANCE_FILE=/tmp/hn-dark.toml task run    # then again with /tmp/hn-light.toml
```

`task run` reads the live `/var/lib/pacman` and does not modify it. For the states the live system cannot produce on
demand (stale data, no databases, reload failure), section D runs the application against a **scratch copy** of the
databases, using the same recipe as the Updates checklist (`docs/sdd/pending-updates/ACCEPTANCE.md`, Setup):

```sh
mkdir -p /tmp/hn-explore
cp -a /var/lib/pacman/local /var/lib/pacman/sync /tmp/hn-explore/
touch -d '10 days ago' /tmp/hn-explore/sync/*.db
# Private mount namespace: /var/lib/pacman is replaced by the scratch copy for this process only.
sudo --preserve-env=WAYLAND_DISPLAY,XDG_RUNTIME_DIR,DISPLAY,HOLONIGHT_APPEARANCE_FILE \
  unshare --mount sh -c 'mount --bind /tmp/hn-explore /var/lib/pacman &&
    exec sudo -u "$SUDO_USER" --preserve-env=WAYLAND_DISPLAY,XDG_RUNTIME_DIR,DISPLAY,HOLONIGHT_APPEARANCE_FILE \
      env LD_LIBRARY_PATH="$PWD/build/dependencies/prefix/lib" "$PWD/build/holonight-packages"'
```

Remove `/tmp/hn-explore` afterwards. This recipe was not run during implementation; adjust it to your session.

## A. Navigation and layout (live system)

| # | Check | Dark | Light |
|---|---|---|---|
| A1 | The app still opens on **Installed**. Clicking **Explore** in the sidebar shows the Explore page and moves the highlight; Updates and Installed still switch correctly | ✓ | ✓ |
| A2 | Header shows "Explore", "Data as of …" in local time, the "Configured sync repositories are searched; AUR and local-only packages are not covered." note and a **Reload** button on the right | ✓ | ✓ |
| A3 | The search field spans the page width, shows the placeholder "Search configured repositories" and is enabled once loading finishes. Before any input the page shows "Enter a search term to find packages" | ✓ | ✓ |
| A4 | While the first load runs: centered "Loading package index…", indeterminate progress bar, "Loading…" on a disabled Reload button, and a disabled search field with the placeholder "Loading package index…" | ✓ | ✓ |

## B. Search results (live system)

| # | Check | Dark | Light |
|---|---|---|---|
| B1 | Type `vim`. Results appear shortly after the last keystroke, not on every key. Table columns: Package (name over a one-line description), Version, Repository badge, Download, and an installed badge column; header aligned with rows; sizes right-aligned | ✓ | ✓ |
| B2 | Ranking looks right: exact name first (`vim`), then names starting with the query (`vim-runtime`, `vimb`, …), then names containing it (`gvim`, `neovim`), then description-only matches. Within a group names are alphabetical | ✓ | ✓ |
| B3 | Installed badge: an installed package with the same version shows "Installed" (green); one whose installed version differs shows "Installed <version>" in a subdued style; a package that is not installed shows no badge | ✓ | ✓ |
| B4 | The results list scrolls vertically with its own scroll bar. At the 720×480 minimum window, header and rows scroll horizontally together and no column is clipped out of reach; with a window ≥ 1092 px wide the details panel sits beside the table, below it otherwise | ✓ | ✓ |
| B5 | Query `a` (very broad): the list shows 500 rows and a caption at its end reading "Showing first 500 of N matches, refine your search to find what you're looking for." | ✓ | ✓ |
| B6 | Query `zzzzzz`: a centered "No packages match 'zzzzzz'. Note: Configured sync repositories are searched; AUR and local-only packages are not covered." and no table | ✓ | ✓ |
| B7 | Clearing the field returns to the "Enter a search term…" hint; a query of only spaces does too | ✓ | ✓ |

## C. Details panel (live system)

| # | Check | Dark | Light |
|---|---|---|---|
| C1 | With no row selected the panel reads "Select a package to view details" | ✓ | ✓ |
| C2 | Clicking a row highlights it (accent edge) and fills the panel: name, repository badge, then rows Version, Download size, Installed size, License, URL, the description and chips for Dependencies and Optional dependencies. Up/Down arrows move the selection | ✓ | ✓ |
| C3 | The panel is read-only: there is **no** Remove/Install button, no more-options button, no footer links, and no "Installed", "Reason", "Required by" or "Local state" sections, even for an installed package | ✓ | ✓ |
| C4 | A package with more than five optional dependencies shows a "+N more" chip; activating it reveals the rest | ✓ | ✓ |
| C5 | Rows with empty fields (no URL, no licenses) omit those rows instead of showing blanks | ✓ | ✓ |
| C6 | No text is truncated or overlapping at 1360×890 and at 720×480; long names/descriptions elide with a tooltip on hover | ✓ | ✓ |

## D. Reload, freshness and edge states (scratch copy, see Setup)

| # | Check | Dark | Light |
|---|---|---|---|
| D1 | Reload busy state: pressing **Reload** keeps the results visible, shows the progress bar and disables the button until done; the query and selection are unchanged afterwards | ✓ | ✓ |
| D2 | Stale hint: with the 10-day-old copy, a warning notice reads "Your package databases are out of date. Sync them with your package manager, then press Reload." (same wording as on the Updates page) | ✓ | ✓ |
| D3 | Picking up a manual sync: `touch /tmp/hn-explore/sync/*.db`, press Reload. "Data as of" moves to now and the stale hint disappears | ✓ | ✓ |
| D4 | Reload banner: `chmod 000 /tmp/hn-explore/sync`, press Reload. A red-bordered notice reads "Reload failed: … Showing data from …" and the previous results stay visible and searchable. `chmod 755` it and press Reload: the banner disappears | ✓ | ✓ |
| D5 | No-databases state: `mv /tmp/hn-explore/sync /tmp/hn-explore/sync.off`, press Reload. A centered "No package databases found." message with the pacman.conf explanation and a disabled search field. Move it back afterwards | ✓ | ✓ |
| D6 | Error state: start the application against a scratch copy without `local/ALPM_DB_VERSION`. The page shows "Couldn't load packages" with the reason, a disabled search field and an enabled Reload button | ✓ | ✓ |

## E. State across navigation

| # | Check | Dark | Light |
|---|---|---|---|
| E1 | Search `vim`, select a row, go to Installed and back to Explore: the query, results and selection are unchanged | ✓ | ✓ |
| E2 | Restart the application: Explore starts empty (no remembered query or selection) | ✓ | ✓ |

## Result

| Theme | Tester | Date | Result / notes |
|---|---|---|---|
| Dark | User-reported | 2026-09-25 | Passed, including follow-up changes |
| Light | User-reported | 2026-09-25 | Passed, including follow-up changes |
