import sys; sys.path.insert(0,'/home/claude/tfevr/tools/translate')
from lfdlib import *; from brief_seg import *; from brief_ocr import *
def learn_file(lib, r, name, path, kind='text'):
    h,rows,bl=segment(r[name+'.DELT'])
    tl={b[1]:b for b in bl if b[0]=='text'}
    bad=[]
    for line in open(path, encoding='utf-8'):
        line=line.rstrip('\n')
        if not line or line.startswith('#'): continue
        y,t=line.split('|',1)
        y=int(y)
        if y in tl: ya,yb=tl[y][1],tl[y][2]
        else:
            ya=y; yb=y
            while rows[yb+1] if yb+1<h else False: yb+=1
        base,bs=line_blobs(rows,ya,yb)
        chars=t.replace(' ','')
        if len(bs)==len(chars):
            for (x0,x1,k),c in zip(bs,chars): lib.learn(k,c)
        else: bad.append((y,t,len(bs),len(chars)))
    return bad
if __name__=='__main__':
    r={n+'.'+t:b for t,n,b in read_lfd('/home/claude/lfd/DFBRIEF.LFD')}
    lib=Lib()
    import glob,os
    for p in sorted(glob.glob('/home/claude/tfevr/tools/translate/brief/*_en.txt')):
        n=os.path.basename(p)[:-7]
        print(n, learn_file(lib,r,n,p))
    lib.save('/home/claude/tfevr/tools/translate/brief/body_glyphs.json')
    print(len(lib.g), ''.join(sorted(set(lib.g.values()))))
