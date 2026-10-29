# F0 M4.7S: preserve the actual qsb bundle in validation artifacts

CI313 completed successfully on GitHub, but its strict evidence review remains
incomplete. The scratch shader bundle was produced under the hidden `.qsb`
directory and was omitted from the three uploaded native validation ZIPs. The
69 tests themselves passed on Linux, Windows and macOS; the missing bytes prevent
an independent check of the final bundle and must not be represented as full
acceptance of M4.7R.

The regression test now copies its actual final bundle into the non-hidden
attempt directory as `fixture-final.qsb`. Its report records the filename, byte
count and SHA-256, and the test rejects a missing, empty or mismatched copy. The
copy is compared with the digest from the last successful build; it is not a
replacement shader or a new rendering path. Shader sources, pipeline, RAW,
colour, GPU, UI and production application code are unchanged.

## CI313: completed jobs, incomplete evidence

[CI313](https://github.com/snailjayxxx/JixelLight/actions/runs/40901013769)
finished successfully. Its original XML contains exactly the old 68 test names
plus `qsb-depfile-spaces`, with zero failures or skips on each platform. Linux,
Windows and macOS recorded 1,101.83, 1,774.04 and 1,521.87 seconds respectively.
The actual Windows and macOS SDK-stripped packages each passed their independent
24-mode audit. All other native checks passed a separately labelled partial audit;
that audit explicitly records the missing qsb bundle as failed and is not an
acceptance result.

All five original validation ZIPs, their verified extracted files, three complete
job logs, the source pins, original failed observer and partial audit records are
sealed at `evidence/ci-40901013769/terminal-incomplete-v2/receipt.json` (2,667
files). The original ZIPs passed size, SHA-256, CRC and safe-extraction checks;
the logs confirm the actual checkout tree matches the published source. The
collector exited zero before sealing. The receipt deliberately remains
`accepted=false`.

The first qsb observer incorrectly expected Ninja on Linux; its failure is
preserved. The real jobs used Unix Makefiles on Linux/macOS and Visual Studio 17
2022 on Windows. Windows checked out the two helper files with CRLF line endings:
the original artifact hashes match the reports, and their exact CRLF-to-LF
projection matches the committed source. This distinction is retained rather than
claiming identical raw bytes across line-ending conversions.

## Validation of this candidate

The normal Root `qsb-depfile-spaces` CTest passed in 4,495 ms using Qt 6.8.3 and
Ninja, with all 22 actual commands and 12 malformed-input controls. The new
847-byte visible file is byte-identical to the actual final `.qsb` output;
its SHA-256 is `a4e8618bddd1928565a592fc7fe459f756d447ffdfa308e74aa94656dd7ce79b`.
Existing reports and the results-file prefix were retained. The local receipt is
`M47S-qsb-proof-root-v1/receipt.json`.

Independent private macOS runs exercised Ninja, Unix Makefiles and Ninja
Multi-Config. A visible-files-only archive retained the verified final bundle in
each case; copy-error and tampered-copy controls failed as intended. The updated
observer also rejects 12 malformed evidence cases and validates the actually
observed Visual Studio log and line endings against a clearly labelled synthetic
bundle. That synthetic control is not an actual Windows bundle or native
Windows acceptance. Original observer failures and all control reports remain
preserved in `qsb-evidence-private-20261030-v1`.

No new image-rendering or GUI run is claimed for this test/evidence-only change;
the previously recorded local App and genuine Sony results retain their original
source stamps. At this point the candidate still required a committed CI with all 69 tests
on all three platforms, both actual 24-mode packages, five original validation
ZIPs including the visible qsb bundle, three complete checkout logs, source pins
and an independently sealed strict audit. Existing 45/60/120-second GUI/export,
32 Mi-pixel, 3 GiB reservation and colour-accuracy limits remain unchanged.

## CI314: qsb evidence retained, Windows aggregate timeout

[CI314](https://github.com/snailjayxxx/JixelLight/actions/runs/40914418539) completed all 69 tests and strict native audits on Linux, Windows and macOS; all three uploaded actual qsb bundles are present and their SHA-256 matches their reports. The actual macOS SDK-stripped package passed all 24 modes. Windows reached its **75-minute total job limit** during the deployed CPU Sony export/reference check. Its GUI and export write completed under their original GUI limit, but the independent reference was interrupted and the final package manifest was not produced. The original Windows package audit fails on that missing manifest. Five original validation ZIPs, three full checkout logs, the GitHub timeout annotation and the failed audit are sealed at `evidence/ci-40914418539/terminal-failed-v1/receipt.json` (2,650 files; `accepted=false`). This is not full acceptance. The aggregate-only follow-up is documented in [M4.7T](F0_WINDOWS_CI_BUDGET_STAGE_M47T.md).

## CI315 follow-up

The later aggregate-timeout follow-up passed its complete three-platform and two-package audit at `ef7785c`; its original artifacts and source were sealed. CI313 remains evidence-incomplete and CI314 remains failed. See [M4.7T CI315 acceptance](F0_WINDOWS_CI_BUDGET_STAGE_M47T.md#ci315-acceptance).
