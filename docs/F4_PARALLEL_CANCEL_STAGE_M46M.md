# F4 M4.6M — cancel the observed request before unrelated startup

Development branch design/lightcraft-fusion-20261008; PR16 remains Draft. This test-only candidate has fresh local Core acceptance and awaits its own committed all-three/actual-package validation. Last fully accepted remains I8ed006a/[CI40445440071](https://github.com/snailjayxxx/JixelLight/actions/runs/40445440071). J/K/L failures remain retained.

## Problem and change

[L CI40470594391](https://github.com/snailjayxxx/JixelLight/actions/runs/40470594391) completes Linux and Windows45/45 plus actual Windows11-mode package. Mac44/45 fails only brushParallelRequestsIsolateRuntimeFailureAndInFlightCancellation: a pending request later returns an image instead of null. Mac native/package validation is skipped.

The test observes its own active helpers and pending future, starts three unrelated futures, then sets cancellation. Startup or scheduling can allow normal completion before cancellation. A token set after completion cannot invalidate an already returned result.

Only the existing cancellation store moves immediately after those observations and before the unrelated starts. All assertions remain: active helpers/pending, cancelled null result, independent runtime refusal without poisoning its token, exact healthy whole/panned outputs, only two successful frame counters, bounded helpers, recovery and unchanged source fingerprints. No retry, skip, accepted completed result, larger fixture, production hook or sleep is added.

Product C++, QML, schema, kernels, decoder, memory accounting, CMake, workflow and wrappers stay byte-identical to L. Its native vignette guard and Linux45/Windows60/Mac60 whole-job settings remain.

## Actual local evidence

The original local test passes12 attempts: this is not natural reproduction of intermittent CI scheduling. A private copy inserts500ms after the same pending observation to model delayed unrelated startup, reproducing the exact old null-result failure once. The reordered variant passes12 targeted attempts and6 with the same controlled gap after cancellation. Delays exist only in private diagnostic copies; original RED/logs/source/commands are retained.

After all L jobs finish, only this test-order patch is applied. Physical Qt6.8.3/SDK15.4 build, parallel1, succeeds; complete existing Core CTest passes **212/0/0 in23.54s**, within unchanged120s, with actual DNG/Sony fixtures configured. Tested CoreTests.cpp SHA is9496f017f972e88d0488c53d59da2d8e11aa604fee8de7d6e2eb40d7852182e4. Preferences and original DNG/Sony hashes stay unchanged. Existing SDK26-built dependency linker warnings are retained.

Parent evidence: cancel-observation-core-build.log, cancel-observation-core-ctest.log, cancel-observation-core-local-summary.json, cancel-observation-original-summary.json and cancel-observation-reuse-proof.json. Old Core/CTest outputs are copied before verification. Local reports retain c60ecf5 precommit base, not a new committed CI identity.

Private diagnostics remain in .git/info/jixellight-cancel-observation and physical build-local/cancel-observation-local. No eraser prototype header or changed Core ABI is used.

Unchanged source hashes reuse the passed local normal/2× RAW, brush/Sony/scopes/exports and other shards with original stamps. The full45 suite/native heavy matrices are not restarted for this QA-only ordering fix; fresh complete Core212 is the relevant local regression.

## Acceptance pending

New exact committed all45/all3/zero skips,212 Core/36 GPU/39 performance/21 Look/17 admission/all27 admittedGPU≤40/65535, owned24/nine/7 blocked+4 ready/full2,659,021/45s, realSony16/six/native32,947,264/full histogram+waveform/sourceSHA/shared3GiB/60s, originalRAW23/allUI/new pointer trace/full10,340,960/60s, both actual SDK-stripped11-mode packages and original ZIP/full-log/actual checkout-tree proof remain required. Actual Mac vignette-package validation is still pending.

Source32Mi/shared3GiB/research2Mi/tile8Mi/work96Mi, GUI60,000ms/owned45,000ms/performance120s remain unchanged. The private11-file eraser draft is unapplied; no production erasing/nativeUI/realRAW eraser export/nativeGPU brush/nativeFP32 RAW/F0–F6 completion is claimed.
