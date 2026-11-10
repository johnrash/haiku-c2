# Draft: a bug report for Haiku (not sent)

> **Status:** Not filed. The fix in the fork (patch 0207) was written with AI assistance: don't attach it or derive a patch from it for Haiku.

A draft for Haiku's bug tracker (https://dev.haiku-os.org), about the data loss found in M46. Nothing has been sent. Haiku doesn't take AI-written code, so the report describes the fix in words and has no patch. It was drafted with an AI assistant, which you may want to say, or put it in your own words.

---

**Summary:** Since hrev59865, file data smaller than one page-writer batch is not written back until sync, unmount or memory pressure; a power loss loses it while the journal keeps the metadata.

**Component:** System/Kernel (VM)

**Version:** found on hrev60299 (arm64); the code is architecture-independent. Introduced by 3bfaa91290 "kernel/vm: Per-KDiskDevice modified queues and page writers" (2026-04-14, part of hrev59865).

**Description:**

In `ModifiedPageQueue::_PageWriter()` (src/system/kernel/vm/vm_page_writer.cpp), when fewer than `kNumPages` (256) pages are queued, the writer waits on `fPageWriterCondition` for `PAGES_FLUSH_DURATION_LOCAL_QUOTA` (3 s). Since 3bfaa91290, a wait that times out goes straight back to waiting (`continue`). Before that commit, the writer went on to write whatever was queued after the 3-second wait.

As a result, as long as a device's modified queue holds fewer than 256 pages (1 MiB with 4 KiB pages), nothing writes them. That lasts until something wakes the writer: a sync, an unmount, the over-quota check, or low memory. On an otherwise idle system that can be indefinitely.

BFS's journal still writes the metadata (inode, size, block allocation) within seconds. After a power loss or a reset, the files therefore come back with the right size but with whatever their blocks held before: zeros, or data of files deleted earlier. `checkfs` finds nothing wrong, since the metadata is consistent. A clean shutdown is unaffected, because unmounting writes everything.

**How it was found:** test programs copied onto an eMMC with scp were later found to contain pages of package files that had been deleted shortly before. The machine had been power-cycled hours after the copy, without a shutdown. The syslog's own last pages of that session were zeros, for the same reason.

**Steps to reproduce:**

1. On an otherwise idle machine, copy a few files totalling less than 1 MiB to a BFS volume (for example four files of 20 to 300 KB).
2. Wait 30 seconds or more, and don't sync.
3. Cut the power, or reset the machine.
4. After booting, compare the files with the originals.

**Expected:** the files are intact. Before the change they were written within about 3 seconds.

**Actual:** the files have the right sizes, but their contents are stale (zeros, or the contents of previously freed blocks). On the reporter's ARM64 board all four files of the test were wrong after a 30-second wait.

**Suggested fix (in words):** after the idle wait, whether it was woken or timed out, continue to the code that collects and writes the queued pages, as the page writer did before 3bfaa91290. In other words, drop the `continue` on a timed-out wait. The `modifiedPages == 0` check right after already handles an empty queue.

The loop condition `fWriterThread >= 0` still ends the thread at teardown, and the queue is then written every 3 seconds when idle, as before. If waking an idle disk every 3 seconds is a concern, a longer idle interval would still bound the data at risk. Never writing a small queue doesn't.

**Verification of that change** (the reporter's ARM64 board, eMMC, BFS): the same test, with the power cut 5, 10, 30 and 30 seconds after the copy, found every file intact each time. Several of those boots replayed the journal, so the metadata had been in flight too.

---

**Notes, not part of the report:**
- The fork's fix is `haiku-patches/0207` (M46); it was written with AI assistance, so don't attach it or derive a patch from it. The test script is `tools/stress/powercut.py`.
- Before filing, it's worth checking upstream's current master: the code may have changed since hrev60299.
