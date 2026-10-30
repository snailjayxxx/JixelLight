# F4: zero-pressure gestures and stationary pressure peaks

A brush gesture containing only zero-pressure dabs has no coverage. It now finishes without creating an empty layer, advancing the render revision or adding an Undo entry; after an Undo, it also leaves the Redo branch available. The same rule applies to Paint and Erase on an existing layer.

Pressure can rise and fall while a pen remains at the same point or moves less than a sampling interval. The sampler now retains a positive local peak before pressure falls, so a `0 → 0.8 → 0` gesture can produce visible coverage instead of being mistaken for an empty gesture. Existing dab and work limits still reject an oversized stroke atomically.

## Formal Root validation

The serial build passed, along with targeted Core and controller tests and the eight existing failure-evidence tests. An independent controlled-QPA canvas check exercised the empty new layer, stationary positive pulse, visible pixels, Undo, zero-pressure preservation of Redo, Redo, and zero-pressure Paint and Erase on an existing layer. CPU/Metal, 1×/2× and opaque/transparent images all passed on Cocoa; an additional offscreen CPU case passed. Each new case retained six verified screenshots. The existing pressure workflow also passed all eight CPU/Metal 1×/2× opaque/transparent combinations with its original 22 screenshots, exact export, reference-plan checks and 45-second limit.

The public Sony ARW pressure regression used the unchanged existing scenario. CPU/Metal GUI completed in 10,277/10,723 ms; export plus independent reference took 34,336.020/34,590.095 ms. Both retained 32,947,264 source, histogram and waveform pixels, the shared 3 GiB reservation and exact PNG16/ICC under the original 60/120-second gates. The native Sony reports retain their original `passed=false` and external-verification-pending state; both full drivers and independent verifiers exited 0. This is not a Sony-specific test of the new zero-pressure behavior.

Independent local audit verified the 224 source files, four built binaries and 252 PNG screenshots from the new, existing and Sony runs. Local evidence is recorded at `evidence/F4-pressure-edge-root-local-v1/receipt.json`. The new canvas checks are separate from the existing 45-second pressure path and are included in the next native and deployed-package validation. Cross-platform CI for this change is pending. Controlled QPA events do not establish physical tablet behavior; native GPU brush computation and native FP32 RAW decoding remain unverified.
