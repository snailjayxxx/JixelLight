# Independent U16 quantization probe — M100

CI410 Windows fails its transparent-pressure CPU App deadline47631/45000ms; CI413 separately fails selected Sony CPU final GUI60225/60000ms. Original logs/reports and both successful full102 runs are retained. CI413 records full-scopes13814.6403ms and33 window readbacks13696.1614ms; those measured components do not isolate a root cause. No product or deadline repair is claimed.

This standalone probe compares the renderer's clamped float-product/lround expression with a bounded positive integer conversion after a wider half-unit addition. It keeps the original float multiplication and nonfinite fallback; the wider addition avoids prematurely rounding a just-below-half value across an integer boundary. All65535 half-unit boundaries, adjacent float inputs, clamped extremes and2097152 deterministic inputs compare to the original function before measurement. Two warmups each precede seven alternating-order timing pairs with actual output equality and a checksum.

A separate small Windows/MSVC strict-float probe workflow records original samples and compiler version. The existing three-platform product workflow is unchanged: no job, check,88-context package gate, numeric tolerance or deadline is moved, removed, split or enlarged. This probe is not product acceptance and changes no renderer, App, engine, RAW path or build target. Compilation, exactness and timing are pending. A faster isolated expression would still require actual full-render/oracle/source and platform validation before adoption; no Windows or whole-job speedup is assumed.

Primary conversion rule: [C++ working draft, floating-integral conversions](https://eel.is/c++draft/conv.fpint).
