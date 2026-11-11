# F0 M10 — bounded artifact delivery after mandatory package checks

CI370 macOS passes81/81 and all original native/compute/display/benchmark steps,
then its validation-report upload spends10m29s failing five CreateArtifact
requests. The final response is an HTML page rather than JSON. Because that
upload preceded deployment, the original macOS package step is skipped and the
job fails. It is not a platform/package pass.

CI368 Windows separately passes80/80 in1965.02s and its deployed-validation step
reports success, then the packaging-report upload remains active until the
original job is cancelled. The retrieved first log ends part-way through the
printed package JSON. No complete70-mode manifest or shipping-artifact success
is inferred from that partial original log or from the successful step alone.

## Correction

All existing product commands, validation conditions, reports, source bytes,
checks and their order remain. Desktop deployment/validation now runs before
artifact transport. A failed evidence upload cannot prevent that mandatory
product gate from executing.

Each upload attempt is bounded to2 minutes and gets at most one separately named
`-transport-retry1` attempt, with the same paths and no overwrite. The original
name remains on the first attempt. Literal validation path lists move unchanged
to one job environment value used by both attempts. Failed original actions,
IDs, digests and outcomes remain distinguishable; product tests are not rerun by
this recovery. The60/120/180-minute job caps and all native/per-operation limits
remain unchanged.

Temporary `continue-on-error` applies only to the upload attempts. A mandatory
final gate inspects their pre-override `outcome`, not their adjusted conclusion.
It requires the preceding job/product status to be successful, successful
platform packaging on desktop, and a completed validation/package/shipping
artifact with positive ID and SHA256 digest. Linux requires its validation
artifact. Missing/both-failed uploads, skipped packaging, unknown outcomes or
unexpected repeated success fail the gate. The complete audit is printed into
original job logs and the job summary; a recovered transport is explicitly
identified. It is not relabeled as an original successful upload.

This uses GitHub's documented [step timeout and failure handling](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#jobsjob_idstepstimeout-minutes),
[pre-override step outcomes](https://docs.github.com/en/actions/reference/workflows-and-actions/contexts#steps-context)
and [v4 artifact identity/digest outputs](https://github.com/actions/upload-artifact/blob/v4/README.md).
The change retains the existing v4 action and artifact formats; it does not
bypass an unavailable service or allow acceptance without delivered evidence.

## Local verification and original scope

Seven stdlib Python methods pass. They cover all three platform modes, retained
failed/cancelled originals followed by successful transport, missing/failed
artifacts, failed product/package gates, invalid IDs/digests, unexpected retries,
and CLI failure-audit preservation/overwrite refusal. Ruby parses the workflow;
a structured comparison confirms every old non-upload product command,
condition/environment, literal path glob and job deadline is unchanged. The
Desktop upload ordering and final strict gates are checked independently.
No product C++/QML/shader/test threshold changes are made in this stage. Its
actual cloud transport recovery and current platform/package results remain
pending; an outage can still correctly fail the job.

Original evidence remains outside Git:

- `denoise-cpu-cache/ci370-macos-failed-original.log`,3,236,148B,
  SHA256`a39de4a2a618ac75b27c380cfbb24832c0e0c1793b02d000c9607f68d6a5d07e`;
  its original job is130757741412 and run43088063224 is associated with head093f5c5.
- `denoise-gpu-stage/ci368-windows-cancelled-original.log`, first retrieved
  **partial**4,317,602B log,
  SHA256`db3d1270b8cc74ddab821815203b68a5012620eb0b47657d8722afa3dbbdbf1d`;
  original job130733081188, run43080756900 associated with head125c83a.
- Each corresponding `*-job-original.json` preserves API step conclusions and
  timestamps. These observations do not constitute a new independent whole
  artifact acceptance receipt. GitHub PR checkout merge stamps are distinct from
  associated PR head SHAs.

Local tool evidence is under
`/Users/jaysnail/.cache/jixellight/artifact-transport-stage/`, including
`gate-tests-first.log`, `original-paths.json` and `workflow-parsed.json`.
Earlier failures/cancellations are retained and not rerun or promoted here.
