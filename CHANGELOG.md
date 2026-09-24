# Changelog

All notable changes to HoloNight Packages will be documented in this file.

## [Unreleased]

### Added

- Initial Qt 6/QML application skeleton and development infrastructure.
- Read-only Updates page, reachable from the sidebar (Installed stays the landing page). It compares installed
  packages with the sync databases already on disk via libalpm and lists strictly newer official-repository
  versions with repository, download size and installed-size change. A Reload button re-runs the comparison to
  pick up databases synced outside the application. Packages matched by `IgnorePkg`/`IgnoreGroup` in
  `/etc/pacman.conf` are listed with an "Ignored" badge and excluded from the headline count and download total.
  The page shows when the data was current and hints when the databases are more than 7 days old. It distinguishes
  up-to-date, no-databases and error states, and keeps the previous list with a banner when a reload fails. The
  application never synchronises databases and makes no network access; AUR and foreign packages are not covered.
