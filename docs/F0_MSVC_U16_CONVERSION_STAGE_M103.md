# MSVC bounded U16 conversion candidate — M103

The independent M100 probe passes2,293,766 exact comparisons on both MSVC194435229 and Apple M2. MSVC alternating-pair medians are12.579100ms original/5.595000ms candidate for2097152 values; M2 medians2.239292/3.062042ms favor the original. These are arithmetic-only observations, with visible host variation and no whole-render or deadline benefit established. Original samples/logs and compile-tree receipts remain retained.

Only Windows native MSVC color-output RGB quantization now uses the bounded expression. Clang, clang-cl and other compilers retain the literal original path. The original clamped float product is stored before widening; positive truncation after the double half-unit addition matches lround without the below-half rounding error of a float addition. Nonfinite scaled values retain the original lround fallback. Alpha, every preceding color/RAW operation, transfer curves, source identity, engine version, GPU layout/math and budgets remain unchanged. The independent original brush pipeline retains its original quantizer.

Core tests compile the actual helper on every platform and compare all65535 half-unit thresholds/adjacent float inputs, clamped extremes and2097152 seeded inputs against the original expression, requiring2,293,766 exact matches. Existing complete-render/oracle/GPU/native/RAW regressions remain mandatory. Compilation and actual renderer qualification are pending; expected Core244/session17 and full102/all96 groups, all29 native contexts, all seven binary invariants, normal three-platform/88-context installed delivery and unchanged45/60/120-second/aggregate limits remain. No Windows pressure-deadline repair or overall completion is claimed.

Primary rule: [C++ floating-integral conversion](https://eel.is/c++draft/conv.fpint). The stage's pure unit cases and full original-pipeline comparisons, rather than a change in numeric tolerance, must verify the equivalence in actual compilers.

All269 Python methods pass in1.044s. No heavy compilation has run alongside the preceding full/native/RAW qualification.
