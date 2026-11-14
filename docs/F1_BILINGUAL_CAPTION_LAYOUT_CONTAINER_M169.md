# M169: own the bilingual layout height on an Item

Accepted b91de16 remains unchanged. M168/fd44a33 is held: Python302 and its
fresh full Release build succeed, then the first DPR1 capture fixture fails
6/1 before any pressure/App/full-Ctest/CI/private/shipping run. The new caption
cannot instantiate because Label inherits Text's read-only implicitHeight.
All original four capture methods pass; the new QML instantiation assertion
retains the real error. The source,38files/60,884,580bytes,19 runtime copies,
five graphs and all1404 original build-file hashes remain preserved. Manifest:
e66f8ffdc5a7a45978a5efec0fb5891667d1751f53a5fbee719a6c750d3a87e6.

Use a plain Item to own the writable implicit width/height, with the normal
Label as its visible child. Font/color aliases and read-only text-height
observations keep the existing caller/test interface. Both invisible measures
use the actual Label font, padding, width and line-height behavior. The child
Label still inherits fonts through the original controls/ApplicationWindow
chain; the plain Item does not introduce an interactive control or a fixed
font/height. Caption text, Main.qml usage and all18 font/width checks remain.

Primary Qt6.8.3 references:
https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quicktemplates/qquicklabel.cpp
https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quicktemplates/qquickcontrol.cpp
(`Label` extends Text; parentFont walks recognized controls/labels before the
ApplicationWindow, preserving normal font inheritance through a plain Item.)

The actual original Linux failure remains independently held at cd99e71:
canvas882×653→882×649/revision13→15 during translation, while complete Develop,
brush/history/source/input-owner state stays identical. Its original rejection
and ca77d043... job-log hash remain. The successor changes only the caption
container, with the M168 regression unchanged. Use a fresh NAS source/head and
physical build. First require Python302 and an actual-component QML preflight,
then capture DPR1/2 each7/0/five cases, four pressure variants and the complete
development/full107/all101/all33/nineteen-runtime/private/platform/package
gates. All original state/pixel/ICC/numeric/resource/deadline checks remain.
Full F0–F6 and photographic/native-float/physical-tablet acceptance remain open.
