import numpy as np
from PIL import Image
from scipy import ndimage as nd
def shift_eye(a, box, seed_iris, dx):
    x0,y0,x1,y1=box
    sub=a[y0:y1,x0:x1]
    dark=sub<128
    lab,_=nd.label(dark)
    iris=lab==lab[seed_iris[1]-y0,seed_iris[0]-x0]
    white=~dark
    wl,_=nd.label(white)
    # white components touching iris (eye white) but not touching box border
    border=set(np.unique(np.concatenate([wl[0],wl[-1],wl[:,0],wl[:,-1]])))
    dil=nd.binary_dilation(iris,iterations=2)
    ids=set(np.unique(wl[dil&white]))-border-{0}
    eyewhite=np.isin(wl,list(ids))
    interior=nd.binary_closing(eyewhite|iris,iterations=2)
    interior=nd.binary_fill_holes(interior)
    # keep iris only inside interior-ish
    new=np.where(interior,255,sub).astype(np.uint8)
    sh=np.roll(iris,dx,axis=1)
    new[sh&interior]=sub[iris].min() if iris.any() else 0
    new[sh&interior]=0
    a[y0:y1,x0:x1]=new
    return interior.sum(),iris.sum()
def make(src,dst,dx):
    a=np.array(Image.open(src).convert('L'))
    print(shift_eye(a,(326,440,440,500),(370,470),dx))
    Image.fromarray(a).save(dst)
if __name__=='__main__':
    make('keys/neutral.png','keys/look_left.png',-13)
    Image.open('keys/look_left.png').crop((180,420,460,540)).resize((560,240)).save('/tmp/ll.png')
