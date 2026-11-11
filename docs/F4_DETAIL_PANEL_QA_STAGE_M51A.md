# F4 M6A compact controls and picker readiness

The complete local sharpening run at `deaf9f1` reports77/78 CTests in725.90s.
Core217, Performance39, GPU39, film-grain12 and independent-sharpen14 each
pass with zero failures/skips. The sole failure is the existing unanswered
XMP picker check: its150ms clock expires before the dialog becomes visible.
An unchanged isolated20-observation XMP rerun passes; neither that rerun nor
the77 original successes are relabeled as an original all-green78-test run.

The test helper now gives dialog construction its existing10s readiness
allowance, then requires the full150ms unanswered interval after actual
visibility. A dialog that never appears still fails. It verifies expiry,
zero selections/acceptance, actual closure, unchanged catalog/original/XMP
bytes and no merge activity. Product dialogs/import behavior and the120s
CTest deadline are unchanged. The corrected XMP component suite passes20
observations locally without failures/skips.

Visual review of the new feature panels exposed older English headers and
Reset buttons extending beyond a300px window. The tool scroller now keeps
its available width, header rows and titles may shrink, and redundant header
spacers are removed. Clearer Color Grading and Curves titles fit beside their
actions. Steady-pen options use a separate row; layer-management buttons wrap.
The primary Local EV slider follows layer/mode selection, keeping both in
view when the waveform reduces the compact tool viewport to160px. Existing
object identities, actions, pressure settings and edit semantics are retained.

The first normal-App CPU probe genuinely fails after the initial reflow:
additional rows put Local EV below the compact waveform viewport. Moving the
slider beside the primary controls fixes that product layout issue. The final
normal CPU and Metal/native-brush Apps pass their entire35-phase Paint/Erase,
Undo/Redo, reopen, language, exact-scope and manual/automatic-scroll workflows
in8,464/9,504ms respectively. Both retain2,659,021 full-scope pixels and place
Local EV at71..84 within the160px tool viewport. Their actual English control
captures and six bilingual300/320/350px feature-panel PNGs are visually
reviewed. These prepublication scoped observations do not establish shipping
or physical tablet acceptance. The initial duplicate-QML-property refusal and
driver-log/report-name collision are retained; neither is a passing App run.

CI364 retains Linux dependency DNS failure, Windows76/77 with the subsequently
corrected FL backend expectation, and macOS compilation cancelled when CI365
supersedes it. The original Mac log reaches96%, with no completed tests or
measured OOM claim. CI365 runs the published grain/sharpening changes and the
four-job desktop build cap; its results do not accept this later follow-up.

The Linux setup now replaces only the runner mirror list's failing Azure
hostname with Ubuntu's primary archive, preserving suites, signing keys and
other mirror entries. Network retries/timeouts follow the runner project's
bounded1-retry/15s policy. Bash syntax and exact substitutions are checked
locally; real Linux installation remains a CI requirement. See the primary
[runner configuration](https://github.com/actions/runner-images/blob/main/images/ubuntu/scripts/build/configure-apt.sh)
and [Ubuntu archive](https://archive.ubuntu.com/ubuntu/). No insecure APT flags,
dependency substitutions, product precision/memory limits or test skips are
introduced. Complete current-commit78-test and three-platform/package gates
remain required.
