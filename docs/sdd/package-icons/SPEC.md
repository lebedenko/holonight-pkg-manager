# Package icons

## Intent and boundaries

Show original-color 24px application icons in Installed, Updates and Explore without GUI network access. Preserve row
heights, horizontal table scrolling, independent vertical scrolling and detail panels. Missing metadata, unsupported
images and non-application packages retain initial-letter tiles. No suffix guessing, downloads or dependency installation.

## Design

The ALPM installed adapter adds absolute package-owned `usr/share/applications/*.desktop` paths to `Package` using
standard C++ strings, relative to its configured root. No changes to snapshots, D-Bus, the session service or transactions.
A GUI-only static library owns one `PackageIconResolver`, shared by all presentation models. Worker jobs parse visible
Application desktop entries and the local AppStream OS catalog, validating local images with Qt image readers. Installed
refresh replaces desktop associations; overlapping loads coalesce and obsolete results are discarded. Destruction is safe
because workers capture values, never model/resolver pointers. Results remain cached between refreshes.

Visible desktop entries matching the package basename take precedence, followed by lexical path order. Catalog desktop
components are ordered by ID and associated only through AppStream package names. Catalog use in Explore is restricted
to Arch official repository names. Foreign packages can use installed desktop metadata but never guessed catalog matches.
Remote AppStream icons are ignored. Available themed icons take precedence over readable local images. Theme pixmaps
are rendered once on the GUI thread into bounded original-color sources; role reads only access cached values.

All three models expose `iconName` and `iconSource`; cache publication emits role-specific data changes without resetting
filtering or selection. `PackageIcon.qml` renders the source or the letter tile, including when asynchronous image loading
fails. Header spacers align the new Updates/Explore slots without increasing header height. The Updates table minimum
width grows by one icon slot (32px including spacing), preserving its name-column minimum and horizontal scrolling. AppStreamQt is a required GUI build dependency; the Arch catalog
and Qt image decoder packages remain optional runtime dependencies.

## Acceptance

Isolated fixtures cover exact associations, desktop priority/visibility, stable component order, invalid images, absent
catalogs, model consistency and refresh. Existing model/QML suites, policy fixtures, dual-style compiled runtime acceptance
and installation checks cover unchanged layout and runtime discovery. Installed JXL acceptance is conditional on a Qt JXL
plugin: environments without it must retain fallback tiles. No production transaction or pointer/focus automation is used.
