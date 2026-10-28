import numpy as np
from PIL import Image
def load(p): return Image.open(p).convert('L').resize((1024,1024),Image.LANCZOS)
def shift_of(ref,img):
    a=1-np.asarray(ref.resize((256,256)),float)/255; b=1-np.asarray(img.resize((256,256)),float)/255
    a-=a.mean(); b-=b.mean()
    c=np.fft.ifft2(np.fft.fft2(a)*np.conj(np.fft.fft2(b))).real
    y,x=np.unravel_index(c.argmax(),c.shape)
    if y>128:y-=256
    if x>128:x-=256
    return x*4,y*4
ref=load('keys/neutral.png')
for k in ['blink','happy','sad','thinking','sleeping','pet','shake','speaking_open','listening','look_right']:
    print(k,shift_of(ref,load(f'keys/{k}.jpg')))
