#!/usr/bin/env python3
# The Force Engine VR for Meta Quest - installer of the DFMOD weapon style.
#
# The DFMOD weapon style uses the weapon models of the "Dark Forces Mod Remastered" for Jedi Academy
# (DFmod 2025 Final). Those models belong to the mod's authors and are not distributed with the port.
# This script builds the file the game needs (dfmod_combined.bin) on your own computer, from your own
# download of the mod, and copies it to the headset:
#
#   1. finds DFmod_2025_Final.zip (or opens the mod's official page so you can download it);
#   2. takes the weapon models out of it (models/weapons2 inside the mod's .pk3 files);
#   3. converts them (first-person model completed by the mod's own world model);
#   4. copies dfmod_combined.bin to the Quest with adb.
#
# Usage:  python install_dfmod.py [--zip FILE | --folder DIR] [--no-push] [--out FILE] [--adb FILE]
#                                 [--recompute] [--lang en|pt]
# Needs Python 3.9+ with numpy, scipy and pillow (install_dfmod.bat / install_dfmod.sh set that up).
import argparse, glob, hashlib, io, json, locale, os, shutil, subprocess, sys, tempfile, time, webbrowser, zipfile

VERSION = '1.0'
MOD_PAGE = 'https://www.moddb.com/mods/dark-forces-mod-remastered/downloads/dfmod-2025-final'
MOD_FILE = 'DFmod_2025_Final.zip'
PACKAGE = 'com.vcmatias.tfevr'
DEST = '/sdcard/Android/data/%s/files/dfmod_combined.bin' % PACKAGE
HERE = os.path.dirname(os.path.abspath(__file__))
FOLDERS = ('briar_pistol', 'blaster_r', 'heavy_repeater', 'demp2', 'golan_arms', 'concussion', 'merr_sonn')
EXTS = ('.md3', '.glm', '.jpg', '.jpeg', '.tga', '.png')

MSG = {
    'title': ('TFE VR for Meta Quest - DFMOD weapon style installer %s', 'TFE VR para Meta Quest - instalador do estilo de armas DFMOD %s'),
    'deps': ('Missing Python package: %s. Run install_dfmod.bat (Windows) or install_dfmod.sh, or: pip install numpy scipy pillow',
             'Falta o pacote Python: %s. Rode install_dfmod.bat (Windows) ou install_dfmod.sh, ou: pip install numpy scipy pillow'),
    'step1': ('[1/4] Looking for the mod archive (%s)', '[1/4] Procurando o arquivo do mod (%s)'),
    'found': ('      found: %s', '      encontrado: %s'),
    'open': ('      Not found. Opening the official download page:\n      %s\n      Download %s (about 590 MB) and leave it in your Downloads folder.\n      Waiting for the file... (Ctrl+C to stop; you can also run again with --zip FILE)',
             '      Arquivo ausente. Abrindo a pagina oficial de download:\n      %s\n      Baixe %s (cerca de 590 MB) e deixe na pasta Downloads.\n      Aguardando o arquivo... (Ctrl+C para parar; ou rode de novo com --zip ARQUIVO)'),
    'timeout': ('      The file did not show up. Run again when the download is done, or pass --zip FILE.',
                '      O arquivo nao apareceu. Rode de novo quando o download terminar, ou use --zip ARQUIVO.'),
    'step2': ('[2/4] Extracting the weapon models', '[2/4] Extraindo os modelos das armas'),
    'pk3': ('      %s: %d files', '      %s: %d arquivos'),
    'nomodels': ('      No models/weapons2 files found in %s. Is this the DFmod 2025 Final archive?',
                 '      Nenhum arquivo de models/weapons2 em %s. Este e o arquivo do DFmod 2025 Final?'),
    'missing': ('      Missing weapon file: %s', '      Falta o arquivo de arma: %s'),
    'same': ('      weapon files match DFmod 2025 Final', '      arquivos das armas conferem com o DFmod 2025 Final'),
    'differ': ('      %d weapon file(s) differ from DFmod 2025 Final: the fit of the models will be computed again (slower) and may need review',
               '      %d arquivo(s) de arma diferem do DFmod 2025 Final: o encaixe dos modelos sera recalculado (mais lento) e pode precisar de revisao'),
    'step3': ('[3/4] Converting (a few minutes)', '[3/4] Convertendo (alguns minutos)'),
    'reg': ('      fitting the world models (this is the slow part)', '      encaixando os modelos de mundo (esta e a parte lenta)'),
    'comb': ('      combining weapon %d of %d', '      combinando arma %d de %d'),
    'pack': ('      packing', '      empacotando'),
    'done3': ('      %s (%.1f MB)', '      %s (%.1f MB)'),
    'exact': ('      identical to the file of the reference build', '      identico ao arquivo do build de referencia'),
    'close': ('      built (bytes differ from the reference build, which is expected with other library versions)',
              '      gerado (os bytes diferem do build de referencia, o que e esperado com outras versoes das bibliotecas)'),
    'step4': ('[4/4] Copying to the Quest', '[4/4] Copiando para o Quest'),
    'skip': ('      skipped (--no-push). Copy the file yourself to:\n      %s', '      pulado (--no-push). Copie o arquivo para:\n      %s'),
    'noadb': ('      adb not found. Connect the Quest by USB and copy the file with SideQuest or Meta Quest Developer Hub to:\n      %s\n      or run again with --adb FILE',
              '      adb ausente. Conecte o Quest por USB e copie o arquivo pelo SideQuest ou Meta Quest Developer Hub para:\n      %s\n      ou rode de novo com --adb ARQUIVO'),
    'nodev': ('      No headset answered adb. Connect the Quest by USB, accept the debugging prompt inside the headset and run again.\n      The converted file stays at: %s',
              '      Nenhum headset respondeu ao adb. Conecte o Quest por USB, aceite o aviso de depuracao dentro do headset e rode de novo.\n      O arquivo convertido fica em: %s'),
    'noapp': ('      The game (%s) is not installed on the headset. Install the APK, open it once and run again.',
              '      O jogo (%s) nao esta instalado no headset. Instale o APK, abra uma vez e rode de novo.'),
    'pushfail': ('      Copy failed: %s', '      Falha na copia: %s'),
    'pushed': ('      copied (%d bytes on the headset)', '      copiado (%d bytes no headset)'),
    'end': ('Done. In the game options, set the weapon style (WEAPONS) to DFMOD. Close and open the game if it was running.',
            'Pronto. Nas opcoes do jogo, escolha o estilo de armas (ARMAS) DFMOD. Feche e abra o jogo se ele estava aberto.'),
}
LANG = 0
def say(key, *a):
    print(MSG[key][LANG] % a if a else MSG[key][LANG], flush=True)

def downloads_dirs():
    home = os.path.expanduser('~')
    return [d for d in (os.path.join(home, 'Downloads'), os.getcwd(), HERE, os.path.dirname(HERE)) if os.path.isdir(d)]

def find_archive():
    for d in downloads_dirs():
        hits = sorted(glob.glob(os.path.join(d, 'DFmod*2025*.zip')) + glob.glob(os.path.join(d, 'dfmod*2025*.zip')), key=os.path.getmtime, reverse=True)
        for h in hits:
            if zipfile.is_zipfile(h): return h
    return None

def wait_for_archive(minutes=60):
    say('open', MOD_PAGE, MOD_FILE)
    try: webbrowser.open(MOD_PAGE)
    except Exception: pass
    end = time.time() + minutes * 60
    last = None
    while time.time() < end:
        time.sleep(5)
        h = find_archive()
        if h:
            size = os.path.getsize(h)
            if last == (h, size): return h          # complete zip whose size stopped changing
            last = (h, size)
    return None

def wanted(name):
    n = name.replace('\\', '/').lower()
    i = n.find('models/weapons2/')
    if i < 0: return None
    rel = n[i + len('models/weapons2/'):]
    if not rel or rel.endswith('/') or not rel.endswith(EXTS): return None
    return rel if rel.split('/')[0] in FOLDERS else None

def extract_from_zip(zf, root, label, depth=0):
    """Copies models/weapons2 files (lower-case names) from a zip, descending into .pk3 and .zip members."""
    n = 0
    names = sorted(zf.namelist(), key=str.lower)        # later .pk3 files override earlier ones, as in the game
    for name in names:
        rel = wanted(name)
        if rel:
            dst = os.path.join(root, *rel.split('/'))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with zf.open(name) as src, open(dst, 'wb') as out: shutil.copyfileobj(src, out)
            n += 1
    if n: say('pk3', label, n)
    total = n
    if depth < 2:
        for name in names:
            if name.lower().endswith(('.pk3', '.zip')):
                with tempfile.TemporaryFile() as tmp:
                    with zf.open(name) as src: shutil.copyfileobj(src, tmp)
                    tmp.seek(0)
                    try:
                        with zipfile.ZipFile(tmp) as inner: total += extract_from_zip(inner, root, os.path.basename(name), depth + 1)
                    except zipfile.BadZipFile: pass
    return total

def extract_from_folder(folder, root):
    """An unpacked copy of the mod: loose models/weapons2 files and .pk3 files anywhere below the folder."""
    total = 0
    pk3s = []
    for base, _, files in os.walk(folder):
        for fn in sorted(files, key=str.lower):
            p = os.path.join(base, fn)
            if fn.lower().endswith('.pk3'): pk3s.append(p); continue
            r = os.path.relpath(p, folder).replace('\\', '/').lower()
            rel = wanted(r)
            if rel is None and r.split('/')[0] in FOLDERS and r.endswith(EXTS): rel = r     # the folder is models/weapons2 itself
            if rel:
                dst = os.path.join(root, *rel.split('/'))
                os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copyfile(p, dst); total += 1
    for p in sorted(pk3s, key=lambda x: os.path.basename(x).lower()):
        try:
            with zipfile.ZipFile(p) as zf: total += extract_from_zip(zf, root, os.path.basename(p), 1)
        except zipfile.BadZipFile: pass
    return total

def check_inputs(root, ref):
    bad = 0
    for rel, sha in ref['inputs'].items():
        p = os.path.join(root, *rel.split('/'))
        if not os.path.exists(p) or hashlib.sha256(open(p, 'rb').read()).hexdigest() != sha: bad += 1
    return bad

def find_adb(arg):
    if arg: return arg if os.path.exists(arg) else None
    exe = 'adb.exe' if os.name == 'nt' else 'adb'
    p = shutil.which('adb')
    if p: return p
    cands = [os.path.join(HERE, 'platform-tools', exe), os.path.join(os.path.dirname(HERE), 'platform-tools', exe),
             os.path.join(os.path.expanduser('~'), 'Downloads', 'platform-tools', exe)]
    la, pf = os.environ.get('LOCALAPPDATA', ''), os.environ.get('ProgramFiles', '')
    if la:
        cands += [os.path.join(la, 'Android', 'Sdk', 'platform-tools', exe),
                  os.path.join(la, 'Programs', 'SideQuest', 'resources', 'app.asar.unpacked', 'build', 'platform-tools', exe),
                  os.path.join(la, 'Programs', 'SideQuest', 'resources', 'platform-tools', exe)]
    if pf:
        cands += [os.path.join(pf, 'Meta Quest Developer Hub', 'resources', 'bin', exe),
                  os.path.join(pf, 'Oculus Developer Hub', 'resources', 'bin', exe)]
    cands += [os.path.expanduser('~/Library/Android/sdk/platform-tools/adb'), os.path.expanduser('~/Android/Sdk/platform-tools/adb')]
    for c in cands:
        if os.path.exists(c): return c
    return None

def adb(exe, *args, timeout=600):
    r = subprocess.run([exe] + list(args), capture_output=True, text=True, timeout=timeout)
    return r.returncode, (r.stdout or '') + (r.stderr or '')

def push(exe, out):
    code, txt = adb(exe, 'devices', timeout=60)
    devs = [l for l in txt.splitlines()[1:] if l.strip().endswith('device')]
    if not devs: say('nodev', out); return False
    code, txt = adb(exe, 'shell', 'pm', 'path', PACKAGE, timeout=60)
    if 'package:' not in txt: say('noapp', PACKAGE); return False
    adb(exe, 'shell', 'mkdir', '-p', os.path.dirname(DEST), timeout=60)
    code, txt = adb(exe, 'push', out, DEST)
    if code != 0: say('pushfail', txt.strip()[-300:]); return False
    code, txt = adb(exe, 'shell', 'stat', '-c', '%s', DEST, timeout=60)
    try: size = int(txt.strip().splitlines()[-1])
    except Exception: size = -1
    if size != os.path.getsize(out): say('pushfail', 'size on the headset: %s' % txt.strip()); return False
    say('pushed', size); return True

def main():
    global LANG
    ap = argparse.ArgumentParser(description='Builds dfmod_combined.bin from your own copy of the Dark Forces Mod Remastered and copies it to the Quest.')
    ap.add_argument('--zip', help='path of %s' % MOD_FILE)
    ap.add_argument('--folder', help='unpacked copy of the mod (or its models/weapons2 folder)')
    ap.add_argument('--out', default=os.path.join(HERE, 'dfmod_combined.bin'), help='where to write the converted file')
    ap.add_argument('--no-push', action='store_true', help='only convert; do not copy to the headset')
    ap.add_argument('--adb', help='path of the adb executable')
    ap.add_argument('--recompute', action='store_true', help='compute the fit of the world models again instead of using the stored one')
    ap.add_argument('--lang', choices=('en', 'pt'))
    a = ap.parse_args()
    lang = a.lang or ((locale.getlocale()[0] or os.environ.get('LANG', '') or '')[:2].lower())
    LANG = 1 if lang == 'pt' else 0
    say('title', VERSION)
    for mod in ('numpy', 'scipy', 'PIL'):
        try: __import__(mod)
        except ImportError: say('deps', 'pillow' if mod == 'PIL' else mod); return 2
    tools = os.path.join(HERE, 'dfmod') if os.path.isdir(os.path.join(HERE, 'dfmod')) else os.path.join(os.path.dirname(HERE), 'tools', 'dfmod')
    sys.path.insert(0, tools)
    import numpy as np
    import register, combine, pack_comb
    ref = json.load(open(os.path.join(HERE, 'dfmod_2025_final.json')))

    work = tempfile.mkdtemp(prefix='tfevr_dfmod_')
    try:
        root = os.path.join(work, 'weapons2'); os.makedirs(root)
        say('step1', MOD_FILE)
        if a.folder:
            src = a.folder; say('found', src); say('step2'); n = extract_from_folder(src, root)
        else:
            src = a.zip or find_archive() or wait_for_archive()
            if not src or not os.path.exists(src): say('timeout'); return 3
            say('found', src); say('step2')
            with zipfile.ZipFile(src) as zf: n = extract_from_zip(zf, root, os.path.basename(src))
        if not n: say('nomodels', src); return 4
        import assemble
        for wid, (folder, wname, bname) in assemble.WEAPONS.items():
            for fn in [wname + '_hand.md3', wname + '.md3'] + ([bname + '.md3'] if bname else []):
                if not os.path.exists(os.path.join(root, folder, fn)): say('missing', folder + '/' + fn); return 4
        for wid, rel in register.WORLD.items():
            if not os.path.exists(os.path.join(root, *rel.split('/'))): say('missing', rel); return 4
        bad = check_inputs(root, ref)
        say('same') if bad == 0 else say('differ', bad)

        say('step3')
        cdir = os.path.join(work, 'comb'); os.makedirs(cdir)
        if bad == 0 and not a.recompute:
            for wid, r in ref['registration'].items():
                np.savez(os.path.join(cdir, 'reg%s.npz' % wid), s=r['s'], R=np.array(r['R']), t=np.array(r['t']))
        else:
            say('reg'); register.run(root, cdir, verbose=False)
        ids = list(register.WORLD)
        for k, wid in enumerate(ids):
            say('comb', k + 1, len(ids))
            P, UV, TX, srcs = combine.combine(root, wid, cdir, verbose=False)
            np.savez(os.path.join(cdir, 'c%d.npz' % wid), P=P, UV=UV, TX=np.array([x or '' for x in TX]), src=srcs)
        say('pack')
        tmp_out = os.path.join(work, 'dfmod_combined.bin')
        devnull = io.StringIO(); stdout = sys.stdout; sys.stdout = devnull
        try: pack_comb.main(root, cdir, os.path.join(HERE, 'own_parts'), tmp_out)
        finally: sys.stdout = stdout
        out = os.path.abspath(a.out)
        os.makedirs(os.path.dirname(out), exist_ok=True); shutil.copyfile(tmp_out, out)
        say('done3', out, os.path.getsize(out) / 1048576.0)
        h = hashlib.sha256()
        with open(out, 'rb') as f:
            for chunk in iter(lambda: f.read(1 << 20), b''): h.update(chunk)
        say('exact') if h.hexdigest() == ref['output_sha256'] else say('close')
    finally:
        shutil.rmtree(work, ignore_errors=True)

    say('step4')
    if a.no_push: say('skip', DEST); return 0
    exe = find_adb(a.adb)
    if not exe: say('noadb', DEST); return 0
    if push(exe, out): say('end')
    return 0

if __name__ == '__main__':
    try: sys.exit(main())
    except KeyboardInterrupt: sys.exit(130)
