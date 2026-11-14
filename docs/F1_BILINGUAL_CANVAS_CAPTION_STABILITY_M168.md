# M168: keep the photo viewport stable across caption translations

Accepted root b91de16 remains unchanged. M167/cd99e71 is held with its original
CI456 Linux failure. Linux passes all107 CTest selections in1256.91s, then
controlled GPU pressure fails at3250ms/phase1 during its second preview-language
capture. The complete original log SHA256 is
ca77d0430be094520ef15c73523c826d675c098b0e37df20cefad4128c2f6e0f.
Original logs, the failed report, complete source tar, App/five graphs and all
669 original App-subset build-file hashes are retained. The held manifest is
f79a0e9ace777165b04fd881aa9d327501769977ef7ea7b023a7fa7f6b1afd1e.
Other original CI456 jobs continue independently; none was rerun or cancelled.

The new failure-only state evidence isolates the change: canvas882×653 becomes
882×649 and requested revision13 becomes15. Complete Develop state, brush
document, history0, source path and inactive input owner remain identical.
The original rejection is preserved; this is not an accepted state change.

An independent Qt6.8.3 Cocoa observation uses the exact original Main.qml title
Label. Its18 font/width combinations show the default macOS family keeping the
viewport stable, while Arial/Helvetica/Times/Courier fallback and wrapping can
change it. A Fusion/Arial configuration reproduces a4pixel caption-height
difference in that isolated layout. The normal App remains successful on this
Mac with that configuration; the original Linux state record is authoritative.

## Caption layout repair

The new BilingualCaption measures both existing strings with the same font,
wrapping and available width, reserving their larger height. The visible label
still switches between the same original Chinese and English strings. It keeps
the existing Main.qml layout, color and font inheritance, without a fixed font
or pixel height that could clip another platform. Only this photo title uses
the component. Normal image processing, viewport mapping, input, export and
statistics implementations remain unchanged.

The existing actual-window capture fixture adds one method. It loads the exact
frozen component source used by the App resources and exercises6 requested
font families at400/176/100logical-pixel widths. Each row renders both languages,
requires different actual images, unclipped text, unchanged caption/canvas
height and an exact independent Qt window control. Previous four methods stay
unchanged; capture Qt counts become7/0 with5evidence cases. No new CTest
selection or executable: original107/all101/nineteen runtime pins remain.
Python remains302; ordinary GPU85/native19 and all numeric/resource/deadline
limits remain unchanged.

Use the new NAS source/head/physical Release build. Require caption/capture
DPR1/2, all four pressure variants, original development, full107/all101/all33,
nineteen-runtime/five-graph guards, private3, own original three-platform CI
and both actual package checks. The earlier scoped6-result ASAN fixture is
not a full-App or extended-QML-fixture ASAN qualification. Current validation
is pending. Main/PR15 remain unchanged, and the full F0–F6 plan is incomplete.
