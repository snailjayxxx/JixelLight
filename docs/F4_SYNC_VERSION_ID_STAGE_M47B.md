# F4 M4.7B: stable version targets in sync/reopen verification

Current outcome: all three standalone suites pass, but Windows package
validation is cancelled at the60-minute total-job limit. B remains failed.

The selective-sync regression reopened a project and assumed that row1 still
identified photoB. Catalog loading orders saved import times and source keys;
those positions can differ from the controller's earlier in-memory positions.
The test now remembers each original/virtual version's persistent library `id`,
putsBfirst in its owned SQLite fixture, verifies that ordering and selects the
intended saved identities for every post-reopen assertion.

All original source exclusion, unselected-version, parameter-group, Undo/Redo,
geometry, curation, keywords, repeat-sync and history checks remain. Additional
assertions confirmB's saved undo state and redo branch before the controlled
order change. Production sync, database writes, rendering, masks, UI and codecs
are unchanged. GUI library `id` and CLI `catalog_key` are separate API fields.

## Original failure and controlled verification

[N a53d201 / CI40493231189](https://github.com/snailjayxxx/JixelLight/actions/runs/40493231189)
remains failed. Linux61/62 in869.85s reports Core214/1/0: the unchanged sync/reopen
method expected−1 at row1 and observed1.25. The other61 suites, all17 eraser
shards and three new Core methods pass; laterLinuxGUI steps do not run.
Windows62/62 in1216.39s/macOS62/62 in1361.58s and both actual SDK-stripped
15-mode packages pass. Desktop successes do not override Linux's failure.

The unmodified small local method passes12 times, so natural CI reproduction
is not claimed. A private controlled catalog reorder produces the exact
1.25/−1 failure while confirming thatB's stored−1 and redo branch are intact.
On that same fixture, stable-ID selection passes all original state/history
assertions (Qt3/0/0,25ms). The formal regression now includes the controlled
reorder. No delays, removed assertions or looser numerical gates are added.

N's five original ZIP size/SHA/CRC/safe-extraction receipts, three full logs
and all-three actual checkout ebabc2db15c6901197586d369b7737fdf8e4c350/tree
f820cbab9b8672bc12e325fd09f084c45e70c411 equalNHEAD are verified. Original bytes
are preserved under the physical build parent `evidence/ci-40493231189/` and
[N's runbook](F4_BRUSH_ERASE_STAGE_M47A.md); the failed outcome is unchanged.

## Original local candidate and publication gate

A fresh physical parallel1 Core build and complete Core215/0/0 pass in23.41s
under the original120s limit, with realDNG/Sony fixtures and unchanged source,
daily preferences and original fixture hashes. A patch-added blank line was
trimmed after the first build, then the changed test unit was rebuilt before
this sole fullCore run. Original build logs are retained.

Only this existing test method changes. Twenty-two other tested non-document
source hashes remain identical toN; unaffected local native/numerical evidence
is reused with original stamps and exact source links. It is not relabeled as
a fresh GUI run. Local fullCore reports retain their precommit a53d201 stamp.
A new committed own62/all3/both actual15-mode package CI and its original
artifacts/full job-source proofs are required. M1f4b26e/CI40477576605 remains
last fully accepted until that succeeds.

Source32Mi/shared3GiB/research2Mi/tile8Mi/work96Mi/GPU40/65535 and owned45s/
RAW-Sony60s/performance120s remain unchanged. Local evidence is in the physical
build parent's `sync-version-identity-core/` and
`sync-version-identity-reuse-proof.json`; private controlledRED/GREEN evidence
and the original failure are preserved in the handoff.

The separately prepared selected-stroke locale and scope-layout UI repairs
remain unapplied and are outside this test repair. Native GPU brush, native
FP32 RAW and remaining F0–F6 work stay unfinished. PR16 staysDraft;
main and PR15 are untouched.

## Committed B outcome: failed Windows aggregate budget

[dc3a94f / CI40503630435](https://github.com/snailjayxxx/JixelLight/actions/runs/40503630435)
passes all62 suites on Linux1,019.28s, Windows1,810.50s and macOS1,542.06s.
Each reports Core215/0/0 and retains every prior numerical/native/realSony
export, full-source statistics,3GiB and individual45/60/120s gate. The identity
repair is verified on all three platforms; this does not accept the whole run.

macOS completes its actual15-mode SDK-stripped package. Windows reaches11
verified modes, then its configured60-minute total budget cancels the job at
60m13s during packaging. GitHub's check annotation explicitly reports
`The job has exceeded the maximum execution time of 1h0m0s`. The Windows
package has no final manifest and has not completed Sony eraseCPU,
source-relink, Sony nativeCLI or developCLI. These are not counted as passes.
The original partial package and cancellation log remain failed evidence.

All three full logs prove actual checkout05053ca48a95ea5e8b9eb2bf4c046cac027df3f7,
whose tree d08a21cc71a79f01a966f73246fcfc195304b9b4 equals BHEAD. The five original
ZIPs have verified byte sizes/SHA/CRC/safe extraction; all three full logs
preserve original newlines and hashes. They are archived in the physical build
parent's `evidence/ci-40503630435/`. B stays unaccepted and M1f4b26e remains the
last fully accepted checkpoint. C increases only the aggregate desktop job
budget to75 minutes while retaining every individual gate.

| Original B artifact | ID | Bytes | SHA256 |
| --- | --- | ---: | --- |
| validation-Linux | 12734768704 | 35247582 | 4d859da85754dfea903a45466ea7fbb1a71f161e90ca6c88e68b2415b3bc61dc |
| validation-macOS | 12737700234 | 34264223 | b7c9e312c805990481bbf99733217ea91420361ff7684549c82fb51f875f013b |
| validation-Windows | 12736504312 | 31334144 | d5cde10f22ea60a31365da2168bc34763f22b8d8801631959ddc87d23b3e1a75 |
| package-validation-macOS | 12737990510 | 24254812 | bd45e9e1e830f380165d0e489b6babbc9125f46a026ebfed6186156d7d96a918 |
| package-validation-Windows | 12738285573 | 17483005 | 8796e2082364eb8ad76fe0bbf4e3d9294c9aaaa44366f9a543ac19ec10a8a701 |
