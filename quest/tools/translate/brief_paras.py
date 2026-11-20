import sys; sys.path.insert(0,'/home/claude/tfevr/tools/translate')
from lfdlib import *; from brief_seg import *; from brief_ocr import *; from brief_slots import *
r={n+'.'+t:b for t,n,b in read_lfd('/home/claude/lfd/DFBRIEF.LFD')}
lib=Lib().load('/home/claude/tfevr/tools/translate/brief/body_glyphs.json')
def slot_text(rows,s):
    if s['inpic']:
        rr=[dict() for _ in rows]
        for (x,y) in s['text']: rr[y][x]=rows[y][x]
        return ocr_line(lib,rr,s['ya'],s['yb'])[0]
    return ocr_line(lib,rows,s['ya'],s['yb'])[0]
names=sys.argv[1:] or ['secbase','talay','sewers','testbase','gromas','dtention','ramshed','robotics','narshada','jabship','impcity','fuelstat','executor','arc']
for n in names:
    h,rows,blocks,slots,paras,pm=find_slots(r[n+'.DELT'],lib)
    print('==',n)
    for i,p in enumerate(paras):
        print(f'[{i}] c{p[0]["color"]} ' + ' '.join(f'{s["ya"]}{"p" if s["inpic"] else ""}({s["xs"]}-{s["xe"]})' for s in p))
        print('    '+' / '.join(slot_text(rows,s) for s in p))
