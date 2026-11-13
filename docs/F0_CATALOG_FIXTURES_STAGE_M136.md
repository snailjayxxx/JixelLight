# F0 catalog acceptance fixture correction — M136

Status: test-only correction after the first grouped repair attempt. Production method/header bytes remain identical to71fa8a4. A fresh third physical build and all original full/native/platform gates remain required. Parent455ff12 is draftPR84/CI440; root37c2e4f is unchanged.

The second physical build passes Python283 and Release111.070467 seconds. Thirteen of fourteen targeted CTest groups pass, including Core277, scheduling91 and gesture44. Catalog results are131 passed/7 failed/0 skipped/0 blacklisted in13.471 seconds. The unchanged original clipboard-close regression now passes, including its independent saved exposure and original-source checks. No mandatory GPU/native/full qualification followed this targeted failure.

The seven failures concern the new fixtures: four keyword rows compare raw `one, two` against the accepted canonical `one,two`; two export rows repeat the already-current exported-preset status so no changed notification should fire; one nested-project case edits the first open catalog to1.25 before copying it and incorrectly expects the first to retain.75. The correction keeps the raw keyword input and explicitly checks its normalized command, uses an actual clipboard operation to establish a different pre-export status, and creates the second catalog before editing its value. Both catalog exposures are independently read before nested opening. Every original lifetime, trace, persistence, source and refusal assertion remains.

All98 files/141,988,113 bytes, exact Git-verified source copies, original runtimes/build graph/logs remain failed in `failed-development-71fa8a4-second`; the second build is never rebuilt. Original targeted log SHA256 `fb8b8da2b2ad3c533c3e07681f9c58ae341a10ba2afbf91f408372e7d9a104ed`; catalog results SHA256 `d3c40d4e8f9c5639e5827dac4213c2b066752385a4105f60fc99f4778f585e43`. The independent593399e clipboard failure remains separately failed with21 files.

Expected counts remain catalog138, full104/all98 Qt groups and sixteen runtime guards. No production behavior, image mathematics, source formats, resource/worker policies, precision bounds or deadlines change.
