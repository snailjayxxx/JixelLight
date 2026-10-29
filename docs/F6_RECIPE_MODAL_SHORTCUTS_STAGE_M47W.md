# F6 M4.7W: keep recipe dialog keyboard focus

While the Apply edit recipe dialog is open, the application-wide `G` and `D` shortcuts no longer change the background workspace when Paste, Apply or Cancel has focus. Once the dialog closes, those shortcuts again select Library and Develop. The recipe preview, clipboard snapshot, command application and one-step Undo behavior remain covered by the existing checks.

The baseline was reproduced with the actual Main QML, PhotoController and controlled Qt QPA input: both offscreen and Cocoa runs changed the background workspace when a dialog button had focus. An earlier probe omitted a shortcut-label context and produced a QML ReferenceError; that failed probe is retained separately and is not the valid reproduction. The fix disables the two existing application shortcuts while the dialog is visible and restores them on close.

## Local validation and limits

The actual Root application, input probe and export verifier were built serially in 179,216 ms, with all 221 tracked source files checked before and after. The build and reports use the pre-commit source stamp `6f66955`; they do not represent a build stamped with the later commit.

Four actual Root application runs exercised controlled input at 1× and 2×. Offscreen QClipboard runs took 3,659/3,873 ms; Cocoa runs using a fake clipboard took 4,009/4,150 ms. Each retained the original 34 recipe cases and the 60,000 ms limit, checked `G` and `D` at all three button focuses without changing photo/history, and checked both shortcuts after closing the dialog. The existing offscreen shortcut-help check passed in 3,501 ms. Source and tested binary hashes and the user's preferences remained unchanged. The screenshots show the English and Chinese compact dialogs, invalid-command feedback and truncated preview within the window.

The offscreen QClipboard checks are isolated Qt tests, and the Cocoa runs use a fake clipboard. Neither proves native operating-system clipboard behavior or physical keyboard input. Linux, Windows and macOS native clipboard runs, actual deployed packages and the original cross-platform gates are required for the new commit; they are not yet accepted. CI317's prior acceptance applies to `6f66955` and does not accept this change.

Private reproduction, the earlier failed probe, candidate tests and validator negative cases are sealed at `evidence/F6-recipe-modal-shortcuts-private-v1/receipt.json`. Actual Root source, binaries, reports and local logs are collected separately at `evidence/F6-recipe-modal-shortcuts-root-local-v1/receipt.json`. These local results do not establish completion of all F6 workflows or the full project.
