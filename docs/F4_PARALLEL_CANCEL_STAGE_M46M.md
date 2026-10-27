# F4 M4.6M — cancel the observed request before unrelated startup

Development branch design/lightcraft-fusion-20261008; PR16 remains Draft. **Accepted at 1f4b26e55ec7c3e4fe3dca2386eef924c31b354c / [CI40477576605](https://github.com/snailjayxxx/JixelLight/actions/runs/40477576605)** after strict all-three45/45, both actual11-mode packages and all original artifact/source proofs. J/K/L keep their original failed outcomes.

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

## Acceptance criteria — now met

New exact committed all45/all3/zero skips,212 Core/36 GPU/39 performance/21 Look/17 admission/all27 admittedGPU≤40/65535, owned24/nine/7 blocked+4 ready/full2,659,021/45s, realSony16/six/native32,947,264/full histogram+waveform/sourceSHA/shared3GiB/60s, originalRAW23/allUI/new pointer trace/full10,340,960/60s, both actual SDK-stripped11-mode packages and original ZIP/full-log/actual checkout-tree proof are verified by the committed result below. Actual Mac vignette-package validation is included.

Source32Mi/shared3GiB/research2Mi/tile8Mi/work96Mi, GUI60,000ms/owned45,000ms/performance120s remain unchanged. M contains no eraser product change; subsequent eraser development has separate acceptance. No nativeGPU brush/nativeFP32 RAW/F0–F6 completion is claimed.

## Exact committed acceptance

All45 CTest entries pass without skips on Linux438.70s/Windows976.98s/macOS998.90s. Each has212 Core/36 GPU/39 performance/21 Look/17 admission; all27 admitted GPU cases have max0/65535, retaining the40 limit. Mac's original cancelled-future test now passes with every old assertion.

| Actual SDK-stripped package CPU / GPU ms | Original RAW | Owned brush | Real Sony brush |
| --- | --- | --- | --- |
| Windows | 51,434 / 33,333 | 29,828 / 18,069 | 43,127 / 36,586 |
| macOS | 35,589 / 33,483 | 24,863 / 24,080 | 23,400 / 22,676 |

Both packages pass all11 modes. RAW retains23 successful screenshots/allUI/new target-ready/stable guards/current full10,340,960/QML0/original60,000ms. Both actual Mac reports record amount−1.904, second-pose wait1/not-ready0/polish0; CPU vignette capture inspected with visible−1.90. These are actual normal-path passes, not a claimed natural reproduction of K's intermittent first click. Owned24/nine/7 bilingual blocked+4 ready/full2,659,021/45s and realSony16/six/native7028×4688/full32,947,264 histogram+waveform/sourceSHA/shared3GiB/60s pass on all three and both packages. Real photographs remain private.

All three full logs identify actual checkout bafee1ae277180931948e5614e4b756f561d9042, tree de0ca55b757575c338ea805eccedeff9887ad399 equal M HEAD. Full original character counts Linux1,057,035/Windows966,399(original CRLF)/Mac953,142 are retained. Five original ZIPs pass expected size/SHA256/CRC/safe extraction:

| Artifact | ID | Bytes | SHA-256 |
| --- | --- | --- | --- |
| validation-Linux | 12720724786 | 32,807,568 | d8ec351db45323094f5792ce76cbbd0a990481b9e5f04ae19b038a22de847a1e |
| validation-Windows | 12723451672 | 29,057,089 | d90567b56519295b51a98611c7fe9b025793a6f1a74866bb95dd41c7d297153b |
| validation-macOS | 12722389157 | 31,998,115 | e20a17341109ee2ae6da5dbab946f43155f09dc83232e1300a3725672e02e5f6 |
| package-validation-Windows | 12723368252 | 15,846,112 | 48f9cd5a38c39f4d5c28f36ff63fd76f49d27eeb5fd7a05e6682b6f0df358fda |
| package-validation-macOS | 12723512892 | 21,727,608 | f19c711c7c2283b55b96a304a90f1152059912244af8066c20acd6a10b5da057 |

The strict finalizer accepts /tmp/jixellight-40477576605-summary.json and parent ci-40477576605-summary.json with actual source/tree, all five receipts and three original logs. L/K/J failed evidence is preserved, not retroactively accepted. This accepts the current guarded brush/capacity workflow and its QA repairs; remaining F4/F0–F6 work continues.
