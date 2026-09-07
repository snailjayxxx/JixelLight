"""Read-only CI experiments for CPU/GPU numerical parity.

Each candidate is applied ONLY in the ephemeral runner worktree, built against
exactly the same expanded tests, and restored in finally. No pushes, release
artifacts, tolerance changes or tests removed. Results are hypotheses until the
selected source change is separately committed and the normal build passes.
"""
from pathlib import Path
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'core/pipeline/ImagePipeline.cpp'
SHADER = ROOT / 'shaders/pipeline.comp'
CMAKE = ROOT / 'CMakeLists.txt'
ORIGINAL = {p: p.read_text(encoding='utf-8') for p in (SOURCE, SHADER, CMAKE)}
REPORTS = ROOT / 'numeric-lab'
REPORTS.mkdir(exist_ok=True)


def once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise RuntimeError('Expected one source anchor: ' + repr(old[:100]))
    return text.replace(old, new, 1)


def restore() -> None:
    for path, content in ORIGINAL.items():
        path.write_text(content, encoding='utf-8')


def configure_candidate(strict: bool, cartesian: bool, width: float | None) -> None:
    source, shader, cmake = ORIGINAL[SOURCE], ORIGINAL[SHADER], ORIGINAL[CMAKE]
    if strict:
        cmake += '\nif(MSVC)\n  set_source_files_properties(core/pipeline/ImagePipeline.cpp PROPERTIES COMPILE_OPTIONS "/fp:strict")\nelse()\n  set_source_files_properties(core/pipeline/ImagePipeline.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")\nendif()\n'
    if cartesian:
        source = once(source,
            '    Oklab lab = linearSrgbToOklab(linearSrgb);\n    float chroma',
            '''    Oklab lab = linearSrgbToOklab(linearSrgb);
    bool noBands = true;
    for (int i=0;i<8;++i) {
        const auto band=plan.data[ProcessingPlan::Bands+i];
        noBands &= band.x==0 && band.y==0 && band.z==0;
    }
    if (state.hue==0 && noBands) {
        const float c=std::hypot(lab.a,lab.b);
        const float gain=std::max(0.0f,1.0f+float(state.saturation/100))
            *std::max(0.0f,1.0f+float(state.vibrance/100)*(1.0f-clamp01(c/.30f))*.85f);
        lab.L=std::clamp(lab.L,0.0f,1.5f);lab.a*=gain;lab.b*=gain;
        return oklabToLinearSrgb(lab);
    }
    float chroma''')
        shader = once(shader,
            '    vec3 lab=toLab(c);\n    float chroma',
            '''    vec3 lab=toLab(c);
    bool noBands=true;
    for(int i=0;i<8;++i) noBands=noBands && all(equal(p[11+i].xyz,vec3(0.0)));
    if(p[5].x==0.0 && noBands) {
        float c=length(lab.yz);
        float gain=max(0.0,1.0+p[4].z)*max(0.0,1.0+p[4].w*(1.0-clamp(c/.30,0.0,1.0))*.85);
        lab.x=clamp(lab.x,0.0,1.5);lab.yz*=gain;
        return fromLab(lab);
    }
    float chroma''')
    if width is not None:
        literal = format(width, '.6f')
        source = once(source, 'smooth(-minChannel / 0.001f)',
                      f'smooth(-minChannel / ({literal}f * std::max(1.0f,y)))')
        shader = once(shader, 'smooth01(-smallest/.001)',
                      f'smooth01(-smallest/({literal}*max(1.0,y)))')
    SOURCE.write_text(source, encoding='utf-8')
    SHADER.write_text(shader, encoding='utf-8')
    CMAKE.write_text(cmake, encoding='utf-8')


def run(command: list[str], log: Path, env: dict[str,str] | None = None) -> int:
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=240, check=False)
    return result.returncode


def main() -> int:
    candidates = [
        ('baseline', False, False, None),
        ('strict-fp', True, False, None),
        ('cartesian-chroma', True, True, None),
        ('relative-gamut-001', True, False, .001),
        ('cartesian-relative-001', True, True, .001),
        ('relative-gamut-005', True, False, .005),
        ('cartesian-relative-005', True, True, .005),
    ]
    outcomes = []
    env = os.environ.copy()
    env['JIXELLIGHT_REQUIRE_GPU'] = '1'
    exe = ROOT / ('build/bin/Release/JixelLightGpuTests.exe' if os.name == 'nt' else 'build/bin/JixelLightGpuTests')
    try:
        for name, strict, cartesian, width in candidates:
            print('\n=== NUMERIC CANDIDATE ' + name + ' ===', flush=True)
            configure_candidate(strict, cartesian, width)
            build_log=REPORTS / (name+'-build.txt')
            code=run(['cmake','--build','build','--config','Release','--target','JixelLightGpuTests','--parallel','4'], build_log)
            if code:
                print(build_log.read_text(encoding='utf-8',errors='replace')[-12000:], flush=True)
                outcomes.append({'candidate':name,'build':code})
                continue
            test_log=REPORTS / (name+'-tests.txt')
            console=REPORTS / (name+'-console.txt')
            code=run([str(exe),'-o',str(test_log)+',txt'],console,env)
            report=test_log.read_text(encoding='utf-8-sig',errors='replace') if test_log.exists() else console.read_text(encoding='utf-8',errors='replace')
            print(report[-32000:], flush=True)
            outcomes.append({'candidate':name,'build':0,'tests':code})
            (REPORTS/'summary.json').write_text(json.dumps(outcomes,indent=2),encoding='utf-8')
    finally:
        restore()
        (REPORTS/'summary.json').write_text(json.dumps(outcomes,indent=2),encoding='utf-8')
    print('\n=== NUMERIC LAB SUMMARY ===\n'+json.dumps(outcomes,indent=2),flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())
