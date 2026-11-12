# F0 M74 — complete reference at the Adobe RGB color-mask boundary

CI389's new early Windows WARP preflight correctly stops before the long full suite. Its actual GPU test finds maximum42/65535 at one dark Adobe RGB gamut-edge pixel, above the unchanged40 limit. The color selection is broad (H45/C.12/L.4), EV+3, not inverted, centerL.62/C.08/H359; source16 RGB31331/0/0 at(30,28) becomes CPU44898/78/0 versus GPU44899/36/18. It is not a narrow-selection failure.

The original858,871-byte failed log SHA-256 is4cff8de7e236a8a1af24cd65ac6f9679877f6f5be17b88731b681d028b8dc414. Both real-Sony/GPU CTest cases execute; one fails in206.31s. Full95/native/packages are not accepted at that head. No retry hides this result.

Active color masks targeting Adobe RGB now use the complete CPU reference on every backend, with accepted diagnostic policy `color-mask-adobe-boundary` when an earlier narrow/source/brush/grain policy does not already explain the reference. The sRGB/P3/ProPhoto paths, UI preview compute, CPU math, packed state, alpha, engine/profile compatibility and all limits stay unchanged. This follows the existing explicit Adobe-grain precision policy rather than weakening40.

The624-case matrix retains every case and now requires108 compute/516 exact uploads. Sony comparisons distinguish six compute/two complete references; bright-perceptual composition includes the new Adobe policy. All82 Metal GPU methods pass in29.889s in the development tree; original alpha/all1024-bin checks remain. The combined64 all-stage cases keep their44 compute/20 inherited-policy split. This local development build also contains the separately prepared ProPhoto metadata correction, which does not change pixel math; it is not an exact frozen-head acceptance claim.

Only GPU policy/tests and documentation are staged here. Exact-head frozen95, native workflows, new Windows/macOS/Linux preflight/full gates and both84-context packages remain required. The next isolated ICC metadata correction and soft-proof UI are separate work. No merge, release or whole-product completion.
