import sys; sys.path.insert(0,'/home/claude/tfevr/tools/translate')
from lfdlib import *; from brief_seg import *; from brief_ocr import *
r={n+'.'+t:b for t,n,b in read_lfd('/home/claude/lfd/DFBRIEF.LFD')}
lib=Lib().load('/home/claude/tfevr/tools/translate/brief/body_glyphs.json')
names=sys.argv[1:] or ['secbase','talay','sewers','testbase','gromas','dtention','ramshed','robotics','narshada','jabship','impcity','fuelstat','executor','arc']
tot=0
for name in names:
    h,rows,bl=segment(r[name+'.DELT'])
    print('==',name)
    for b in bl:
        if b[0]=='text':
            t,unk,x=ocr_line(lib,rows,b[1],b[2])
            tot+=len(unk)
            print(f'{b[1]}|{t}   [x{x} c{b[3]}]')
        else: print('  ',b[0],b[1],b[2])
print('unknown',tot)
