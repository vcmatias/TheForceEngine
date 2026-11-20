import sys; sys.path.insert(0,'/home/claude/tfevr/tools/translate')
from lfdlib import *; from goals_lib import *
r={n+'.'+t:b for t,n,b in read_lfd('/home/claude/lfd/DFBRIEF.LFD')}
F={k:ShadedFont() for k in 'gyn'}
H=ShadedFont()
bad=[]
for line in open('/home/claude/tfevr/tools/translate/brief/goals_en.txt'):
    parts=line.rstrip('\n').split('|'); n=parts[0]; texts=parts[1:]
    fr=read_anim(r[n+'.ANIM'])
    pix=decode_delt(fr[0])[4]
    lg=line_groups(pix,62,200)
    # header
    hb=glyph_blobs(pix,47,54,MAIN['h'],xmin=60,bg=(166,167))
    if len(hb)==len('MISSIONOBJECTIVES'):
        for (a,b,g),c in zip(hb,'MISSIONOBJECTIVES'): H.learn(c,g,MAIN['h'])
    items=[]
    for i,((ya,yb),t) in enumerate(zip(lg,texts)):
        if t=='*': continue
        font='n' if i==0 else 'g'
        bs=glyph_blobs(pix,ya,yb,MAIN['g'])
        ch=t.replace(' ','')
        if len(bs)==len(ch):
            for (a,b,g),c in zip(bs,ch): F[font].learn(c,g,MAIN['g'])
        else: bad.append((n,ya,t,len(bs),len(ch)))
    # yellow frames: item k
    for k,f in enumerate(fr[1:]):
        p=decode_delt(f)[4]
        ylg=line_groups(p,0,300)
        # transcripts of item k: map frame lines to frame0 lines by y
        for (ya,yb) in ylg:
            j=[i for i,(a,b) in enumerate(lg) if abs(a-ya)<=1]
            if not j or j[0]>=len(texts) or texts[j[0]]=='*': continue
            t=texts[j[0]]; ch=t.replace(' ','')
            bs=glyph_blobs(p,ya,yb,MAIN['y'])
            if len(bs)==len(ch):
                for (a,b,g),c in zip(bs,ch): F['y'].learn(c,g,MAIN['y'])
            else: bad.append((n,'y',ya,t,len(bs),len(ch)))
for k,f in F.items(): f.save(f'/home/claude/tfevr/tools/translate/brief/goals_{k}.json'); print(k,''.join(sorted(f.g)))
H.save('/home/claude/tfevr/tools/translate/brief/goals_h.json'); print('h',''.join(sorted(H.g)))
print(bad)
