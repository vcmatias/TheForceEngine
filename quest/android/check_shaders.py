import os, re, subprocess, sys, itertools
SH = sys.argv[1]
HDR = open(sys.argv[2]).read() if len(sys.argv) > 2 else ""
def expand(name, seen=None):
    txt = open(os.path.join(SH, name), encoding="latin1").read()
    out = []
    for line in txt.split("\n"):
        m = re.match(r'\s*#include\s*[<"]([^>"]+)[>"]', line, re.I)
        if m:
            inc = m.group(1)
            inc = os.path.basename(inc)
            out.append(expand(inc))
        else:
            out.append(line)
    return "\n".join(out)
opts = ["OPT_BILINEAR_DITHER","OPT_BLOOM","OPT_COLORMAP_INTERP","OPT_SMOOTH_LIGHTRAMP","OPT_TRUE_COLOR","SECTOR_TRANSPARENT_PASS","SKYMODE_VANILLA","MODEL_TRANSPARENT_PASS","OPT_CURVE","OPT_DASHED_LINE"]
combos = [[]] + [[o] for o in opts] + [opts[:5]]
fails = {}
for f in sorted(os.listdir(SH)):
    if not (f.endswith('.vert') or f.endswith('.frag')): continue
    src = expand(f)
    for c in combos:
        defs = "".join(f"#define {d} 1\n" for d in c)
        src2 = "".join(ch if ord(ch)<128 else " " for ch in src)
        src2 = re.sub(r"out\s+float\s+gl_ClipDistance\s*\[\s*\d+\s*\]\s*;", "", src2)
        if f.endswith('.vert'):
            src2 = src2.replace("gl_ClipDistance","tfe_ClipDist")
            if "tfe_ClipDist" in src2: src2 = "out float tfe_ClipDist[8];\n" + src2
        src2 = re.sub(r"\bnoperspective\b", "", src2)
        full = HDR + defs + src2
        p = f"/tmp/claude-0/sh.{f.split('.')[-1]}"
        open(p,"w").write(full)
        r = subprocess.run(["glslangValidator", p], capture_output=True, text=True)
        if r.returncode:
            if os.environ.get('DUMP'): open('/tmp/claude-0/dump_'+f+'_'+'_'.join(c)+'.glsl','w').write(full)
            errs = [l for l in r.stdout.splitlines() if 'ERROR' in l and 'compilation terminated' not in l and 'compilation errors' not in l]
            fails.setdefault(f, set()).update(errs)
for f, e in fails.items():
    print("==", f); [print("  ", x) for x in sorted(e)[:40]]
print("falhas:", len(fails))
