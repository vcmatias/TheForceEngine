import sys; sys.path.insert(0,'/tmp/claude-0/lfd')
import lfd, numpy as np
from PIL import Image
P=lfd.pltt('menu'); a=np.frombuffer(open(sys.argv[1],'rb').read(),np.uint8).reshape(200,320)
Image.fromarray(P[a]).resize((1280,800),Image.NEAREST).save(sys.argv[2])
