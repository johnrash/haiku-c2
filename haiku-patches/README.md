# The Haiku fork, as patches

These 343 patches are the port's changes to Haiku. They are the commits of
the fork's `odroid-c2` branch on top of upstream Haiku a12d4cd1a5
(hrev60299+6, committed 2026-10-26), in order, made with `git format-patch`.
Each file keeps its author and date. For publication, 34 commit messages
were reworded (personal details and apps not published here taken out), and
two code comments in 0327 and 0333 were too.

- **To get the tree:** `tools/haiku-setup.sh` clones upstream Haiku, checks
  out the base on a new branch `odroid-c2` and applies them all with
  `git am`. The final tree is `ec03ed53d7fe66eb70dd88e16dcf9585257edd37`
  and its version is hrev60299+349 (the hrev tags come from Haiku's own
  server: GitHub's mirror has none). The private fork is at +352: it also
  has three commits of internal artwork, which aren't published, so images
  built from these patches have Haiku's stock unofficial-build artwork
  instead.
- **To take one fix:** in your own Haiku checkout, run
  `git am haiku-patches/<file>`, or read the diff. Many of the bug fixes
  stand alone. Others build on earlier patches in the series.
- **To find one:** [`docs/haiku-fixes.md`](../docs/haiku-fixes.md) lists them
  by area. "Mxx" in a message is a milestone of the port
  ([`docs/milestones.md`](../docs/milestones.md)). "+N" is the fork's
  version: up to +66 it's patch N-6, from +68 to +79 patch N-7, and from
  +82 on patch N-9. "The test board" is the bench C2 on a crossover
  cable.

**AI-assisted code.** These changes were written with Claude, Anthropic's AI
model, working for the port's owner, and the commits say so in a
`Co-Authored-By: Claude` line. The exception is 0075, Axel Dörfler's
upstream fix, cherry-picked into the fork: only its added "Fork:" note is
the port's. **Haiku does not accept AI-generated
contributions**, so please don't submit these patches, or code derived from
them, to Haiku as your own work. If you think a fix belongs upstream, report
the bug to Haiku with what the commit message says about the cause, and let
Haiku's developers fix it their way.

Haiku is under the MIT licence (some parts under others; see Haiku's own
licence files). These patches are under the licence of the code they change.
Two kinds of files come in under their own terms: the RTL8822B tables and
headers from Linux's rtw88, under its BSD-3-Clause option with Realtek's
notice (0256, 0257), and Arm's optimized-routines (MIT OR Apache-2.0 WITH
LLVM-exception, 0041).
