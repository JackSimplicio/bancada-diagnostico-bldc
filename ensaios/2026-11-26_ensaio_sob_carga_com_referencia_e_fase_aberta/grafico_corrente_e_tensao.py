import csv, sys
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
plt.rcParams.update({'font.family':'serif','font.serif':['Liberation Serif'],'font.size':9,'axes.linewidth':0.7})
def load(f):
    rows=[r for r in csv.reader(open(f),delimiter=';') if r and not r[0].startswith('#') and r[0]!='t_ms']
    t=[int(r[0])/1000 for r in rows]; I=[float(r[1]) for r in rows]; V=[float(r[2]) for r in rows]
    al=[int(r[3]) for r in rows]; co=[int(r[5]) for r in rows]
    base=[l for l in open(f) if l.startswith('# linha_base')][0].split(';')[1]
    return t,I,V,al,co,float(base)
fs={'a':sys.argv[1],'b':sys.argv[2]}
fig,axs=plt.subplots(2,2,figsize=(16/2.54,8.6/2.54),dpi=300,sharex='col',gridspec_kw={'height_ratios':[2.3,1]})
fig.subplots_adjust(left=0.08,right=0.985,top=0.9,bottom=0.13,wspace=0.22,hspace=0.12)
tit={'a':'(a) Queda de corrente com a fonte ligada','b':'(b) Queda de corrente ao desligar a fonte'}
for col,k in enumerate(['a','b']):
    t,I,V,al,co,base=load(fs[k]); a1,a2=axs[0][col],axs[1][col]
    a1.plot(t,I,color='#222222',lw=0.6,label='Corrente')
    a1.axhline(base,color='#7a7a7a',ls='--',lw=0.8,label='Linha de base')
    a1.axhline(2*base,color='#b00000',ls=':',lw=0.9,label='Alerta (2 × base)')
    ic=[i for i,c in enumerate(co) if c]
    if ic: a1.plot([t[ic[0]]],[I[ic[0]]],'v',color='#b00000',ms=5,label='TRAVADO')
    a1.set_title(tit[k],fontsize=9); a1.set_ylabel('Corrente (A)')
    a2.plot(t,V,color='#1f4fbf',lw=0.7); a2.set_ylabel('Tensão (V)'); a2.set_xlabel('Tempo (s)')
    a2.set_ylim(15,25.5)
    for ax in (a1,a2):
        ax.spines[['top','right']].set_visible(False); ax.grid(True,color='#dddddd',lw=0.5); ax.tick_params(labelsize=8)
from matplotlib.ticker import FuncFormatter
for ax in axs.flat: ax.yaxis.set_major_formatter(FuncFormatter(lambda v,p: ('%g'%v).replace('.',',')))
axs[0][0].legend(fontsize=7,frameon=False,loc='upper left')
fig.savefig(sys.argv[3],dpi=300)
