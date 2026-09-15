# Task Manager sample

See [CONTRIBUTING](../../CONTRIBUTING.md#native-samples) for the executable.
Historical measurements are in [the maintainer archive](../llm/task-manager-history.md).

## Task Manager

The sample has Processes, Performance, and Details pages.
Every page uses the same live Windows snapshot.
There are no simulated GPU, network, service, or startup-app pages.
The sample uses the public controls. It has no application-specific window procedure, renderer, or UIA provider.
The Theme button cycles through dark, light, and high-contrast colors without a change to Windows settings.

| Input | Action |
| --- | --- |
| Ctrl+F | Process search by name or PID |
| Ctrl+1 / Ctrl+2 / Ctrl+3 | Processes / Performance / Details |
| F5 / Refresh | One new snapshot, including during pause |
| Ctrl+P / Pause / Resume | Suspend or resume periodic snapshots |
| Rate | Cycle through 1-, 2-, and 5-second intervals |
| Up / Down / Home / End / Page Up / Page Down | Row selection and reveal |
| F6 with grid focus | Switch between rows and column headers |
| Left / Right with header focus | Previous or next column header |
| Ctrl+Left / Ctrl+Right with header focus | Decrease or increase column width by 16 DIPs |
| Ctrl+Shift+Left / Ctrl+Shift+Right with header focus | Move the column one position left or right |
| Enter / Space with header focus | Sort the column |
| Enter or double-click on a row | Selected-process details |
| Drag a header boundary | Resize the column |
| Drag a header | Move the column to the insertion marker |
| Escape during a header drag | Cancel the move or restore the original width |
| Shift+wheel, horizontal wheel, or bottom scrollbar | Horizontal scroll |
| Right-click / Shift+F10 / context-menu key | Details, Copy PID, End task, pause, and refresh commands |

The Processes page shows name, PID, CPU, working set, thread count, total I/O rate, and counter availability.
An all-digit search matches an exact PID. Other searches match part of the process name, without case sensitivity.
Missing permissions and process exits are normal sampling conditions.
The row remains visible when possible. Unavailable values show an em dash, not zero.
The status area shows collection errors, snapshot count, and collection duration.
Details includes the selected image path, architecture, working set, and private commit.
The sampler requests image metadata only for the selected identity.
Open file location passes the exact parent directory to the Windows shell, without command-line concatenation.

**CAUTION:** End task can lose unsaved work.
The confirmation dialog names the process and PID. No is the default answer.
The worker opens the process again and compares its creation time with the selected identity.
It keeps that same handle through the critical-process check and termination.
PID 0, PID 4, the sample itself, and critical processes cannot be termination targets.
An unavailable identity or failed safety check also blocks termination.
The sample does not request debug privilege or elevation.

### Metric definitions and sampling

Process CPU uses the kernel-plus-user delta, elapsed monotonic time, and the active logical-processor count.
A new process, resume, or invalid counter delta has no CPU rate until the next valid interval.
The code rejects negative, overflowing, nonfinite, and implausible deltas.
It clamps only drift within 0.01 percentage points of 100 percent.
System CPU subtracts idle from the kernel-plus-user delta because `GetSystemTimes` includes idle in kernel time.
On systems with multiple processor groups, system CPU covers the calling group. The Performance page states this limitation.

Working set means all resident process pages, including shared pages.
Private commit means committed private virtual memory, not private resident RAM.
Physical RAM reports total, used, and available bytes from `GlobalMemoryStatusEx`.
System commit and its limit come from `GetPerformanceInfo`.
`GetProcessIoCounters` includes file, network, and other transfers. The sample does not label this counter as disk throughput.
MiB and GiB use powers of 1024.

One worker collects all process and system counters. It does not create a thread for each process.
The interval starts after each collection, so slow collections cannot accumulate scheduled work.
The worker retains one pending request and one latest result.
An undelivered collection error remains pending until UI-thread delivery.
Closing the window revokes callbacks, requests cancellation, and transfers worker disposal outside the UI thread.
Loaders must bound their work or obey the stop token. Arbitrary uninterruptible loaders cannot guarantee immediate process exit.

Pause stops periodic sampling. There is no animation timer or scheduled repaint during pause.
A manual refresh remains available. Resume resets rate baselines.
Minimized or hidden windows suspend the worker and reset baselines on return.
Charts retain 60 values each. Missing samples and cadence changes produce gaps.
The horizontal axis represents sample positions, not a uniform wall-clock timeline across rate changes.
The interval label describes the current cadence.
