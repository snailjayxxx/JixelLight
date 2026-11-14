# F6 camera-reference regression selection

M170 candidate `8afa5f5` completed Python305, a fresh Release build, all five new
C++ methods in focused probes, capture7 at DPR1/2, four pressure contexts,
GPU85/native19 and both native source diagnostic workflows. Its 15 selected
CTest entries passed. The subsequent count gate correctly stopped development:
`brush-scopes-scheduling` reported91 rather than the required94 Qt results.

The executable contains the three new controller tests, but that CTest entry
uses an explicit method list and did not select them. The original91/0 and
expected94 gate stay retained. All252 evidence files/144,542,199 bytes, complete
source, nineteen runtimes, five build graphs and hashes of all1454 original build
files are independently held. That candidate ran no formal full107, platform,
private or shipping qualification and has no acceptance index.

This successor adds those exact three existing method names to the existing
scheduling test entry. It preserves every previous selection and keeps107 CTest
entries and101 Qt result groups. Runtime implementation and all test bodies are
byte-identical to8afa5f5. Expected counts stay Python305, Core278,
command-session24, scheduling94, window capture7, ordinary GPU85 and native GPU19.
The first new-build check must run the actual CTest selection and observe94/0.

Accepted root remains b91de16 at this source checkpoint. Parent candidate6845833
has closed local107/101/all33/all19/private3 and scoped capture proof; LinuxCI457
succeeds while desktop package qualification remains running. Independent base
acceptance and this successor's complete fresh qualification remain required.
Overall F0–F6 development is incomplete.
