# F0: keep the complete Linux validation job within its aggregate budget

CI331 (`41908124548`, head `b71bff9`) was cancelled because the Linux job exceeded GitHub's configured 45-minute whole-job limit. Its dependency step took 18m05s; all 69 registered tests passed in a further 16m02s. The later pressure/Sony step was interrupted and the eight actual-canvas recovery cases, RAW matrix, full-scope/readiness and final GUI checks were skipped. The partial 69-test success does not establish Linux or whole-round acceptance.

Windows and macOS completed successfully. Their independent native and actual-package audits retained 69 tests, 37 package modes each, original recovery Qt16 and live-controller Qt8 reports, every old gate and all new recovery cases. Windows native CPU pressure took 44,382/44,400 ms and deployed CPU pressure 44,452/44,365 ms for opaque/transparent images, under the original 45,000 ms limit. These individual samples do not establish a timing guarantee.

The canonical failed-round receipt is `evidence/ci-41908124548/terminal-failed-v1/receipt.json`, SHA-256 `b28b1507c1e76741f0dfeb04cb5e34bb20ce3ebb2ce55d9813629c55c5fa0a9a`. Its 227 files retain three original FULL logs, five original ZIPs, the exact 45m0s timeout annotation, the Linux partial and missing work, desktop strict audits, 32 desktop recovery cases with 320 PNGs, and actual checkout/tree proof. Whole-round and Linux acceptance flags remain false; the sealed Linux failure was adopted by hash without downloading or auditing it again.

## Change and local check

Only `linux-gpu.timeout-minutes` changes from 45 to 60 in `.github/workflows/build.yml`. A structural YAML comparison requires every other workflow node to remain identical, including desktop 90-minute budgets, all commands, 69 native tests, 37 package modes and all original 45/60/120-second per-case deadlines. No color tolerance, real RAW requirement, 32 Mi-pixel coverage, shared 3 GiB reservation, exact export/ICC or complete scope gate changes.

The 227 other pinned source files match the formally validated M48J source bytes. Application code, shaders, QML, brush sampling and all validators are unchanged. Completed heavy local application tests are retained rather than repeated for this aggregate workflow setting. The bounded Root change and original evidence are recorded separately at `evidence/F0-linux-job-budget-root-local-v1/verified-v1/receipt.json`; this is workflow validation, not a new local app build or three-platform acceptance.

A fresh commit must still pass all three native jobs and both actual 37-mode packages under every original per-case gate. Until that complete run is independently accepted, M48J's Linux recovery checks and the new commit's cross-platform acceptance remain pending. Full unique-position pressure jitter, physical tablet input and recovery-specific Sony behavior remain unverified.
