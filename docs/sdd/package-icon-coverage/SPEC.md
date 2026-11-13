# Package icon coverage

## Intent and requirements

Select the first usable local application icon, without a numerical coverage target.
Metadata candidates alone do not establish successful rendering.

- REQ-F-001: If an icon candidate cannot produce an image, then the resolver shall try the next eligible candidate.
- REQ-F-002: When visible desktop candidates are exhausted, the resolver shall consider eligible NoDisplay entries.
- REQ-F-003: If all eligible candidates fail, then the package view shall display its initial-letter tile.

## Decisions and interfaces

Keep PackageIcon, resolver methods and iconName/iconSource model roles unchanged.
Installed desktop entries precede catalog entries. Visible Application entries precede
NoDisplay entries; Hidden entries are excluded. Within each group prefer a matching
package basename then lexical path order. Catalog components retain ID/data-ID order
and exact package associations. Exhaust stock icons then cached/local icons in metadata
order within each component before moving to the next. Foreign packages cannot use catalog icons.

Workers parse metadata and decode local files with installed HolonightImages::Images;
GUI code alone uses QIcon/QPixmap. Worker results preserve ordered candidate lists
until GUI resolution. Coalesced refreshes reject obsolete results; model role reads
remain cached lookups. Destruction joins outstanding worker work before Qt shutdown. QML receives PNG pixels, never original local paths.

Raster devices are caller-owned readable seekable files. Decode with applied orientation,
preserved colors/aspect ratio and a 64x64 bound. Explicit SVG uses the bounded SVG API;
unsupported resources fail into the next candidate. Limits: 16 MiB input, 16 million
source pixels, 8192 source extent, 64 MiB decoded bytes, 1 MiB metadata, 4 MiB TIFF
metadata and 1024 records. Set Qt allocation ceiling to 64 MiB before icon workers.
No provider API change or isolated decoder adoption. Domain, backend, daemon and
helper dependencies remain unchanged; GUI icon library links Images privately.

## Delivery and acceptance

See the umbrella [contract](../../../../docs/package-icon-contract.md).
The supported Arch profile and acceptance image require archlinux-appstream-data
and kimageformats. Runtime remains offline and graceful on incomplete installations.
Published provider revisions build before Packages in a disposable installed prefix.

Regressions cover candidate exhaustion, visibility ordering, exact associations,
local pixel size/color/aspect ratio, SVG/resource limits, persistent published pixels,
model consistency and refresh. JPEG XL succeeds in supported-installation acceptance;
codec-absent behavior remains a separate fallback check. Staged probes stay outside
the delivered application. Existing QML and dual-style runtime acceptance preserve
24px presentation and scrolling. Production inspection uses isolated HOME/XDG,
read-only ALPM and no pointer/focus automation. Compare resolver-success counts on
the same package/catalog snapshot, separating desktop, catalog and unresolved results.
