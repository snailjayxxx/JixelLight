# F6 M4.7U: apply an edit recipe in the desktop UI

The Project menu opens **Apply edit recipe** for the current available version. Paste the same JSON edit-command array used by the offline CLI, for example:

```json
[{"command":"develop.set","parameter":"exposure","value":0.5}]
```

The dialog takes an exact clipboard snapshot and shows a read-only preview. It escapes carriage returns, non-breaking spaces and Unicode line/paragraph separators in the preview; text beyond 4,096 UTF-16 units is truncated for display, without changing the snapshot being parsed. The user can inspect the complete recipe at its source. Snapshots above 1,048,576 UTF-16 units and arrays outside 1–10,000 commands are refused. Obtaining text from the platform clipboard may itself allocate memory before the length check; the limit is not a bound on that external allocation.

Commands are validated by the existing shared command registry against a staged copy. A rejected command leaves the image and its history untouched; a successful array is applied as one history step and can be undone or redone together. If the current photo or virtual version changes while the dialog is open, the user must reopen it. The action is unavailable during crop or brush input. No new RAW decoder, pixel kernel or GPU brush is introduced.

## Local validation and limits

The actual Root application was built with CMake `-j1` in 169,250 ms, with its source unchanged throughout, before this commit; its embedded source stamp is `ef7785c`, not the later commit. The build completed with linker warnings about duplicate Core linkage and local dependencies built for a newer macOS SDK; the full log is retained. Existing diff and workflow YAML checks passed.

The new executable passed four 34-case GUI runs: offscreen QClipboard at 1×/2× in 3,495/3,740 ms, and Cocoa with a fake in-memory clipboard at 1×/2× in 3,807/3,876 ms. These cover exact Unicode and carriage-return snapshots, rejection of malformed and oversized text, preservation of the earlier snapshot, target-version changes, atomic apply, one-step Undo/Redo and a 10,000-command array. The existing shortcut guide passed in 3,493 ms. The GUI deadline remains 60,000 ms; the settings file and binary hashes stayed unchanged during the runs. English and Chinese compact captures were inspected. Inputs were controlled Qt events, not physical input; the offscreen clipboard is not the system clipboard, and the Cocoa runs used a fake. The locally built executable, 11 changed source files, 56 UI artifacts, commands, logs and receipts are sealed at `evidence/F6-edit-recipe-root-local-v1/receipt.json` (89 files, collector and outer command exited 0).

A separately built private candidate also exercised the genuine public Sony image and full histogram/waveform using a fake clipboard. That earlier binary retained an older build stamp and is not evidence that this Root binary or deployed package passed that combination. The original failed editable-preview and carriage-return preview cases remain in the private evidence; the locally validated candidate preserves the original snapshot and displays the carriage return explicitly. See `evidence/F6-recipe-cr-preview-private-v1/receipt.json` and `evidence/F6-real-sony-cr-preview-private-v1/receipt.json`.

## Three-platform acceptance

[CI316](https://github.com/snailjayxxx/JixelLight/actions/runs/40941352392) passed all 69 native tests with no failures or skips on Linux, Windows and macOS. Actual first checkout `1bce58974b5a0cfb0c023f97fa081ea8cce624c4` on each platform has the same source tree as committed `a2fe32e`. The new real isolated native-clipboard check passed all 34 GUI cases on xcb, Windows and Cocoa in 3,513/4,582/6,369 ms. Both actual SDK-stripped Windows/macOS packages passed all 25 modes; their recipe case uses the fake clipboard. The GUI events are controlled Qt inputs, not physical input.

The deployed Windows Sony pressure workflow passed CPU/GPU GUI in 54,745/29,998 ms and export plus independent reference in 79,950.096/79,686.539 ms. Both retain the complete 32,947,264 source/histogram/waveform pixels, exact PNG16 pixels and ICC, and the shared 3 GiB reservation. The reservation is not a process-RSS limit. The existing 45/60-second GUI, 120-second export/reference and performance, colour and real-Sony requirements remain unchanged.

Five original validation ZIPs, three complete job logs, all source files and independent audits are sealed at `evidence/ci-40941352392/terminal-accepted-v2/receipt.json` (2,902 files; collector and sealer exited 0). CI313 remains evidence-incomplete because qsb files were omitted; CI314 remains unaccepted after its Windows aggregate timeout. Physical input and full F6/MCP completion remain outstanding.
