# Qt responsiveness and service ownership

## Update I/O

The GUI reads persisted snapshots on a single worker at a time. The worker runs the existing snapshot store, including the catalog lease and provenance validation. A request that arrives during a read supersedes its result and schedules one follow-up. The GUI thread applies a changed snapshot or emits one invalidation when a valid file becomes unreadable. Historical rows continue to follow the shared snapshot selection policy. Check completion, a new snapshot timestamp, and filesystem changes can request a read.

The Updates model and session daemon share `UpdatePathWatcher`. Raw events invalidate any in-flight evaluation and restart the debounce. Worker discovery resolves configuration and catalog paths and traverses watched directories. The owning thread registers new paths in batches, checks once again after registering directories, and drops obsolete watches. Existing ancestors provide a route back to directories or files deleted or replaced by rename. Manual reload can evaluate while filesystem discovery is pending once the GUI has initialized.

## Service ownership

The session status and check clients share an owner generation. They install the watcher before the initial asynchronous lookup, read properties from the current unique name with activation disabled, and merge signals buffered during the read. Owner changes invalidate old replies and clear live state while preserving check history. The explicit `CheckNow` call still allows activation and reports its acknowledgement independently of completion.

The Updates page starts passive helper observation on first activation. The transaction adapter establishes its lazy system-bus connection on a worker and invalidates the displayed compatibility when the owner changes. An explicit upgrade, retry, or partial-upgrade action resolves the owner and reads its protocol version asynchronously. An explicit request can activate an absent helper. Preparation consumes the successful probe once, checks the name again, and sends `PrepareUpgrade` to the version reply's unique owner. The transaction adapter also routes the resulting upgrade's execute and cancel calls to that owner. A replacement helper needs a new probe. All D-Bus calls and owner callbacks are delivered on the adapter's owning thread.

## Presentation

History caches resolved and unresolved foreign UIDs for the GUI session. The numeric label is available immediately and the model emits only origin-role updates for affected rows. The history loader prepares deduplicated package names on a worker. The search field updates immediately and the proxy applies the last term after 150 ms, using case-insensitive matching in source order. Selection follows the original row identity through filtering and incremental reads.

The transaction controller sets preview and hook rows when a plan is accepted and builds result rows on terminal outcomes. A separate progress signal updates status and cancellation bindings while execution continues. Authorization denial returns to the same prepared preview. QML uses typed presentation objects, the controller's page enum, and the C++ badge text.

## Acceptance

The tests exercise stale snapshot ordering, locked reads, filesystem recovery, owner handoff, version replacement and prepare dispatch, deferred UID labels, and large-preview progress. The compiled QML suite runs under Holonight and Fusion with fakes at the application ports. The history fixture measures search from submission through the filtered count and measures detail selection through processing queued UI events.

In a native Debug offscreen run with 40,000 transactions and 20,000 matches, the search took 180 ms from the last edit through the list layout, including the 150 ms debounce. Selecting a transaction with 250 package actions took 35 ms through the queued UI update. The search rebuilds the proxy mapping with one layout change. Detail selection still constructs the selected action and hook rows on the GUI thread.
