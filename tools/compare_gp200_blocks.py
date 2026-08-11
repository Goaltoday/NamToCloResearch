#!/usr/bin/env python3
import argparse, math, struct
from pathlib import Path

def u32(b,o): return struct.unpack_from('<I',b,o)[0]
def floats(b,o,n): return struct.unpack_from('<%df'%n,b,o)
def stats(a,b):
    n=min(len(a),len(b)); a=a[:n]; b=b[:n]
    ma=sum(abs(x-y) for x,y in zip(a,b))/n
    mean_a=sum(a)/n; mean_b=sum(b)/n
    num=sum((x-mean_a)*(y-mean_b) for x,y in zip(a,b))
    da=sum((x-mean_a)**2 for x in a); db=sum((y-mean_b)**2 for y in b)
    corr=num/math.sqrt(da*db) if da and db else float('nan')
    exact=sum(x==y for x,y in zip(a,b))
    return corr,ma,exact

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('candidate'); ap.add_argument('reference')
    args=ap.parse_args()
    a=Path(args.candidate).read_bytes(); b=Path(args.reference).read_bytes()
    for label,x in [('candidate',a),('reference',b)]:
        print(f'{label}: size={len(x)} magic={x[:4].decode(errors="replace")} declared=0x{u32(x,4):X} payload=0x{u32(x,0x14):X} model=0x{u32(x,0x84):X}')
    a1=floats(a,0x88,128); b1=floats(b,0x88,128)
    n=min(u32(a,0x84),u32(b,0x84),1024)
    a2=floats(a,0x288,n); b2=floats(b,0x288,n)
    for label,x,y in [('blockA[128]',a1,b1),(f'blockB[{n}]',a2,b2)]:
        c,m,e=stats(x,y); print(f'{label}: corr={c:.9f} MAE={m:.9g} exact={e}/{len(x)}')
if __name__=='__main__': main()
