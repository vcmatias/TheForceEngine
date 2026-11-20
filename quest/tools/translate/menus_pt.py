"""Portuguese menu art: escape menu, confirmation dialogs (MENU.LFD), agent menu (AGENTMNU.LFD)."""
import sys; sys.path.insert(0,'/home/claude/tfevr/tools/translate')
from lfdlib import *; from relabel import *; from goals_render import load_fonts
F=load_fonts(); THIN=F['g']; BOLD=F['h']
log=[]
def do_anim(res, name, specs, pal):
    fr=read_anim(res[name+'.ANIM'])
    out=[]
    for i,f in enumerate(fr):
        if not f or not specs.get(i): out.append(f); continue
        x0,y0,x1,y1,p=decode_delt(f); p=dict(p)
        for sp in specs[i]:
            r,lines=sp[0],sp[1]; opts=sp[2] if len(sp)>2 else ''
            font=BOLD if 'b' in opts else THIN
            ts='notbg' if 'n' in opts else ('black' if 'k' in opts else None)
            if len(sp)>3: ts=sp[3]
            relabel(p,r,lines,font,pal,txtset=ts,log=log,tag=f'{name}[{i}]')
        out.append(encode_delt(x0,y0,x1,y1,p))
    return write_anim(out)
def patch(src, dst, specs, palname):
    res=read_lfd(src); rr={n+'.'+t:b for t,n,b in res}
    pal=read_pltt(rr[palname+'.PLTT'])
    out=[(t,n,do_anim(rr,n,specs[n],pal)) for t,n,b in res if t=='ANIM' and n in specs]
    write_lfd(dst,out)
# ---- MENU.LFD: escape menu + confirmations
ABORT=((59,37,150,50),['ABORTAR']); NEXT=((59,37,150,50),['AVANÇAR'])
CONF=((59,57,151,70),['CONFIGURAR']); QUIT=((59,77,150,90),['SAIR DO JOGO']); RET=((59,101,151,114),['CONTINUAR'])
esc={0:[ABORT,CONF,QUIT,RET],1:[ABORT],2:[ABORT],3:[NEXT],4:[NEXT],5:[RET],6:[RET],7:[CONF],8:[CONF],9:[QUIT],10:[QUIT]}
NO=((231,124,253,135),['NÃO'],'n'); YES=((270,124,291,135),['SIM'],'n')
NO2=((230,109,254,120),['NÃO'],'n'); YES2=((269,109,293,120),['SIM'],'n')
yesno={0:[NEXT,CONF,QUIT,RET,((152,72,292,90),['OBJETIVOS','CONCLUÍDOS'],'bk'),((152,94,292,112),['IR PARA A PRÓXIMA','MISSÃO?'],'k'),NO,YES],
       1:[ABORT,CONF,QUIT,RET,((152,72,292,92),['OBJETIVOS','INCOMPLETOS'],'bk'),((152,99,292,108),['ABORTAR A MISSÃO?'],'k'),NO,YES],
       2:[ABORT,CONF,QUIT,RET,((152,76,292,85),['SAIR DO JOGO'],'bk'),((152,86,292,95),['TEM CERTEZA?'],'k'),NO2,YES2],
       3:[NO],4:[NO],5:[YES],6:[YES],7:[NO2],8:[NO2],9:[YES2],10:[YES2]}
patch('/home/claude/lfd/MENU.LFD','/home/claude/lfd/out/MENU.LFD',{'escmenu':esc,'yesno':yesno},'menu')
# ---- AGENTMNU.LFD
B1=((23,166,78,186),['NOVO','AGENTE']); B2=((92,166,146,186),['REMOVER','AGENTE'])
B3=((166,166,225,186),['SAIR']); B4=((241,166,300,186),['INICIAR','MISSÃO'])
T0=((40,9,280,18),['MENU DO AGENTE'],'b',('bg',73,74,75))
T1=((21,22,146,31),['AGENTES'],'bn'); T2=((167,22,296,31),['MISSÕES'],'bn')
agm={0:[T0,T1,T2,B1,B2,B3,B4],1:[B1],2:[B1],3:[B2],4:[B2],5:[B3],6:[B3],7:[B4],8:[B4]}
DN=((168,108,194,121),['NÃO'],'n'); DY=((207,108,233,121),['SIM'],'n')
dlg={0:[((92,74,230,85),['REMOVER AGENTE'],'bk'),((92,86,230,96),['TEM CERTEZA?'],'k'),DN,DY],
     1:[((92,74,230,85),['SAIR DO JOGO'],'bk'),((92,86,230,96),['TEM CERTEZA?'],'k'),DN,DY],
     2:[((92,72,230,83),['DIGITE SEU NOME:'],'bk'),DN,DY],
     3:[DN],4:[DN],5:[DY],6:[DY],7:[DN],8:[DN],9:[DY],10:[DY]}
patch('/home/claude/lfd/AGENTMNU.LFD','/home/claude/lfd/out/AGENTMNU.LFD',{'agentmnu':agm,'agentdlg':dlg},'agentmnu')
print('\n'.join(log) or 'ok')
