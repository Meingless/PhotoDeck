# Recovery snapshots

PhotoDeck periodically saves an unsaved working copy into its application-local
`recovery` directory. It is separate from the user's project file. The existing
schedule is unchanged: five seconds after a completed edit, immediately after a
previously deferred gesture finishes, and a 45-second periodic fallback. Unfinished model edits (such as brush gestures) are deferred. Canvas transform
previews are separate from committed model state: a recovery may capture the last
committed state during a preview, but never the uncommitted preview itself. Selection-only changes to a
clean project do not create a recovery copy.

## Responsiveness and memory

Only snapshot capture and final pathname publication run on the GUI thread.
Capture copies the small layer/selection metadata containers and clears their
mutable render/boundary caches. Layer pixels, layer masks, and immutable
selection coverage remain implicitly shared QImages; capture does not copy their
pixel buffers. Later edits detach buffers according to Qt's normal copy-on-write
rules. The worker receives a State value, never a Document, Canvas, or widget.

There is one process-wide admission slot, acquired **before** snapshot capture.
Across all MainWindows in that process, at most one full recovery snapshot and
encoder exist. Each live document has only a logical requested flag, stable ID,
and lifecycle epoch. Repeated edits coalesce to its latest state. A stable-ID
round-robin prevents a frequently edited tab from starving other requested tabs
within a window. Waiting windows retain no snapshots. Different PhotoDeck
processes have independent workers and session ownership.

An active snapshot can retain up to the existing 256 MiB layer and 64 MiB
selection limits, plus one PNG encode buffer and codec allocations. Those limits
are not a process-RSS limit: live documents, undo history, render caches, and later
copy-on-write edits also use memory. Snapshot image references are released on
the worker before completion is made visible and before another snapshot is
admitted. There is no per-tab queue of full State/image copies.

## Two-phase publication

The worker validates and serializes the immutable State into a uniquely named,
managed `.pending` file in the recovery directory. A worker-local QSaveFile has
direct-write fallback disabled; all image encoding, large writes, flush, file
synchronization, staging commit, and close occur on that worker.

The GUI observes a completed value using release/acquire synchronization and
joins only an already-completed worker. It then verifies the document still
exists, its stable ID and epoch match, and it still has unsaved content. Without
processing nested events, it atomically replaces the final `.pdeck` pathname with
the closed staging file. It records the captured revision, never a newer live
revision. Newer edits remain requested and are captured when their turn comes.

Publication uses one same-directory native rename: std::filesystem::rename on
POSIX, and MoveFileExW with MOVEFILE_REPLACE_EXISTING on Windows. Windows paths
remain UTF-16 and are converted to extended-length absolute/UNC paths. There is
no remove-old-then-rename window, cross-volume copy fallback, GUI QSaveFile commit,
or copying of project bytes on the GUI thread. Symlinks, directories, identical
files, and different canonical parent directories are rejected. A failed prepare
or publication preserves the previous final recovery file. Filesystem metadata
operations can themselves block on pathological storage; this design removes
PNG encoding, bulk writes, old-file hashing, and synchronization from the GUI,
not every possible storage delay. Atomic pathname replacement is distinct from
full power-loss durability; directory synchronization is not claimed here.

## Lifecycle and errors

Successful Save/Save As, a transition to clean state, actual tab deletion, and an
accepted whole-window close invalidate the epoch before attempting to remove
published recovery. An old worker result therefore cannot recreate a copy after Save or
Discard. A failed or canceled Save keeps recovery. Normal project Save retains
its existing synchronous behavior, source identity/external-modification checks,
copy/format-upgrade guards, and mark-clean policy. Its shared byte encoder is
protected by exact-output regressions against the pre-extraction encoder.

Close prompts run nested GUI event loops. Dispatch and publication are paused
for the complete confirmation sequence. If one tab is discarded and a later tab
cancels whole-window close, every still-live tab keeps its recovery and work
resumes. Discard becomes final only when the actual close is accepted.

Current-revision errors are shown in the status bar. Obsolete failures cannot
overwrite the status of a saved, closed, or newer document. A failed revision is
not immediately retried in a completion loop; a later ordinary schedule may
retry it.

The destructor stops dispatch, rejects publication, cooperatively cancels work,
and joins directly. Shutdown does not depend on GUI callbacks or event delivery,
and never terminates a thread. Cancellation is checked between layers, image
encodes, 1 MiB encoded-data writes, and selection rows. A current PNG encode,
flush/fsync, or stalled filesystem operation may delay shutdown; there is no hard
instant-cancellation guarantee. Direct destruction, used by tests as crash
simulation, preserves the last published recovery. Accepted normal close removes
its own published copies when the filesystem permits deletion. If cleanup is
denied or a path is unexpectedly replaced by a directory, the app logs the exact
path and error; a stale published copy may remain for manual cleanup. This does
not turn a successful project Save into failure, and the invalidated worker still
cannot recreate or update that copy.

## Session ownership and interrupted staging

New final names include a random session UUID and document ID. Each session
holds a QLockFile for its lifetime, with age-only stale detection disabled.
Recovery excludes files with a live, foreign-machine, malformed, unreadable,
or otherwise unverifiable matching owner lock. An absent lock denotes a released
unique session. A present lock needs a parseable PID and nonempty application
name, local identity evidence, native proof that the recorded PID is gone, and
unchanged metadata before it can be treated as inactive. PID reuse and process
inspection failures err toward preservation; lock age alone is never sufficient.

An exact nonempty machine-ID match establishes local identity even if the
hostname changed, differs in case, or is empty. Hostnames are not authoritative:
on Windows, [QLockFile reads COMPUTERNAME](https://github.com/qt/qtbase/blob/v6.8.3/src/corelib/io/qlockfile.cpp#L29-L36),
while [QSysInfo reads the DNS hostname](https://github.com/qt/qtbase/blob/v6.8.3/src/corelib/global/qsysinfo.cpp#L879-L904).
These different sources also appear in Qt 6.5.0's
[QLockFile](https://github.com/qt/qtbase/blob/v6.5.0/src/corelib/io/qlockfile.cpp#L32-L40)
and [QSysInfo](https://github.com/qt/qtbase/blob/v6.5.0/src/corelib/global/qsysinfo.cpp#L925-L950).
An explicit difference between two available machine IDs always rejects the
owner, even when the boot ID matches. If either machine ID is unavailable, the
stricter fallback requires both an exact nonempty boot-ID match and an exact
nonempty current hostname match. Missing identity evidence or a hostname alone
cannot authorize recovery or staging cleanup. These metadata checks attribute
local ownership; they do not authenticate files against an untrusted writer or
distinguish machines whose IDs were cloned.

On startup, cleanup is restricted to exact PhotoDeck UUID-owned pending and
QSaveFile temporary names whose session is provably inactive. Work per cleanup
pass is bounded. It never removes final `.pdeck` files, active foreign-session
files, symlinks, arbitrary hidden files, or unrecognized names. An inactive
recovered final is atomically claimed into the new session's own pathname before
later Save/Discard may remove it. Competing claimants fail safely.

Legacy UUID-only recovery filenames remain readable for compatibility. They
contain no session ownership information, so a concurrently running older
PhotoDeck version cannot be distinguished from a closed older session.

## Format and validation

Recovery uses the same bounded native codec as ordinary Save. Normal projects
continue to prefer v5 unless persistent named selections require v6. Recovery
additionally uses v6 when it needs live/previous selection editing context. It
preserves source pixels/masks, layer metadata, geometry, named selections, and
coverage; it does not replace them with a flattened preview or alter the live
document's path, title, dirty flag, source version, revision, or Save status.

Regression suites cover frozen-encoder byte equivalence, immutable capture,
blocked-worker GUI heartbeat, bounded admission/coalescing/fairness, lifecycle and
nested-close races, failure preservation/no retry spin, native Unicode atomic
replacement, session ownership/orphan cleanup, cooperative shutdown, and the
existing timer/gesture behavior. Identity-policy tests use synthetic values so
matching/conflicting machine IDs remain covered when the test host exposes no
machine ID. Subprocess tests exercise real lock metadata and PID checks; cases
that need an unavailable platform identifier are explicitly skipped. Linux
offscreen validation cannot substitute for native Windows/macOS runtime testing.
