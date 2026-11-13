# F0 crop and gesture transition ownership — M130 regression

Status: test-only preparation, before compilation. Parent585a56b has fresh Release build, mandatory GPU82/real-Sony preflight, targeted12, scheduling90, Python283, default-display PNG16/ICC and actual normal-App CPU/Metal/DPR1/DPR2 development closure. Its independent full102/native33/fifteen-runtime qualification is running. Stable root37c2e4f remains all-three CI438/installed88/strict accepted and clean.

Source review finds that `beginCrop()` calls refinement and then continues changing draft/viewport state even if a refinement notification accepts terminal close. Other uncommitted crop/brush transitions also emit externally observable notifications before continuing. These are source findings; no new product failure has yet been reproduced.

The new focused test uses a real imported photograph and saved project. Its normal refinement library notification accepts `beginClose()` while entering crop. The crop action must stop, keep the post-close draft/revision frozen, and retain the already saved+0.75EV edit. Production code is unchanged. The original full102 graph is retained and the additional case is registered in the existing scheduling group.

No new build may overlap the parent's full/native qualification. The focused baseline will run only after that local closure, with its own new physical build directory; any actual failure will be retained before product changes. No render, RAW precision, original-photo writes, gesture sampling/capacity, worker, GPU tolerance or deadline changes. Main/protected PR15 and camera/physical-device acceptance remain separate.
