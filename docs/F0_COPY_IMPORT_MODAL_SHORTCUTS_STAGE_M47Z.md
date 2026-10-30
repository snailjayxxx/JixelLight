# F0 M4.7Z: keep Copy Import focused

With Custom names, Choose…, Start copy or Cancel focused in the Copy Import dialog, application-wide G and D could switch the workspace behind it. Workspace navigation shortcuts are now disabled while the dialog is visible and restored after it closes.

A private pre-fix Root-QML reproduction observed all eight control/key combinations; the original 17-case validator failed. The private candidate retained all 17 guide cases on offscreen 1×/2× and Cocoa 1×/2×, including editable naming text, unchanged library/history, Cancel and restored navigation. The original failure, candidate reports and independent checks are sealed at `evidence/F0-copy-import-modal-shortcuts-private-v1/receipt.json`.

Separate private small-image tests clicked the actual Start copy control. The successful copy retained the source bytes and editing history, while the existing-name conflict retained the existing file, imported zero items and displayed an error. Reports are sealed at `evidence/F0-copy-import-functional-private-v1/seal.json` and `evidence/F0-copy-import-conflict-private-v1/seal.json`. The successful-copy offscreen outer exit code was not captured; its subprocess result is retained. Both file and folder picker results were simulated. These controlled QPA runs are not physical input, system picker or real Sony acceptance.

## Formal Root and committed CI

After CI320 was accepted and sealed, the three-file candidate was applied with a guard. The app and two probes were built serially using pre-commit stamp `c2775c6`. Formal Root offscreen 1×/2× and Cocoa 1×/2× guide runs passed all 17 cases in 7,069/7,604/7,151/7,917 ms. They test G/D on four controls, text entry, unchanged state, Cancel and navigation after closing. Two recipe regressions retained 34 cases in 3,593/3,708 ms. These are controlled QPA runs and do not establish physical input or native-system clipboard behavior.

On the public Sony RAW, actual Root CPU/Metal pressure GUI runs completed in 9,983/10,059 ms; export plus independent reference took 34,751.217/33,929.463 ms. Source, histogram and waveform each covered 32,947,264 pixels, with exact PNG16 pixels and ICC. Peak shared reservations were 1,716,323,360/1,720,467,584 bytes under 3 GiB, within the unchanged 60,000 ms GUI and 120,000 ms export-reference gates. Reservation is not process RSS. The 221 tested source files, three binaries and user preferences remained unchanged during the tests.

Root local evidence is recorded at `evidence/F0-copy-import-modal-shortcuts-root-local-v1/receipt.json`. The source stamp names the pre-commit parent, not the later commit. The new committed three-platform CI and both deployed packages remain pending. This slice does not establish physical tablet or keyboard input, native GPU brush, native FP32 RAW, or overall F0–F6 completion.
