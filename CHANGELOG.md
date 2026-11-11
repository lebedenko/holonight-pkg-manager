# Changelog

All notable changes to HoloNight Packages will be documented in this file.

## [Unreleased]

### Added

- Install and Remove from the GUI. The Explore details panel has an Install control and the Installed details panel a
  Remove control, one package at a time. Each asks the privileged `holonight-package-helper` for a plan and shows a
  confirmation dialog with the packages to install or remove (dependencies, orphans, hooks, expiry); after confirming,
  polkit authorizes in the helper and a modal progress dialog shows the phase and current item, with Cancel until the
  transaction starts changing the system. Refusals (for example a required system update) and failures have fixed,
  translatable messages; a transaction that cannot be cancelled keeps the window open. The Installed and Explore lists
  are re-read after every outcome. Updates, system upgrade, multi-select and AUR are not part of this change.
- Initial Qt 6/QML application skeleton and development infrastructure.
- Read-only Updates page, reachable from the sidebar (Installed stays the landing page). It compares installed
  packages with the sync databases already on disk via libalpm and lists strictly newer official-repository
  versions with repository, download size and installed-size change. A Reload button re-runs the comparison to
  pick up databases synced outside the application. Packages matched by `IgnorePkg`/`IgnoreGroup` in
  `/etc/pacman.conf` are listed with an "Ignored" badge and excluded from the headline count and download total.
  The page shows when the data was current and hints when the databases are more than 7 days old. It distinguishes
  up-to-date, no-databases and error states, and keeps the previous list with a banner when a reload fails. The
  application never synchronises databases and makes no network access; AUR and foreign packages are not covered.
- Read-only Explore page, reachable from the sidebar. It searches every package in the sync databases already on disk
  (name and description, case-insensitive, ranked exact > prefix > contains > description-only) and shows repository,
  version, download size and an "Installed" badge that reflects the local database. Results are capped at 500 with a
  footer giving the real match count. A details panel shows description, URL, licenses, dependencies and optional
  dependencies, with no actions. The package index is built off the GUI thread, a Reload button re-reads the databases
  to pick up syncs made outside the application, and the same stale-database hint and reload-failure banner as the
  Updates page apply. The application never synchronises databases and makes no network access; AUR and local-only
  packages are not covered, and there is no in-app sync. Configured third-party sync repositories are included.
