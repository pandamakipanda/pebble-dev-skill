import sys, zlib, struct
def read_ppm(p):
    d=open(p,'rb').read()
    # parse P6 with whitespace/comments
    fields=[]; i=0
    while len(fields)<4:
        while i<len(d) and d[i:i+1].isspace(): i+=1
        if d[i:i+1]==b'#':
            while d[i:i+1] not in (b'\n',b''): i+=1
            continue
        j=i
        while j<len(d) and not d[j:j+1].isspace(): j+=1
        fields.append(d[i:j]); i=j
    i+=1
    w,h=int(fields[1]),int(fields[2])
    return w,h,d[i:i+w*h*3]
def write_png(path,w,h,rgb,scale=1):
    if scale!=1:
        rows=[]
        for y in range(h):
            r=rgb[y*w*3:(y+1)*w*3]
            r=b''.join(r[x*3:x*3+3]*scale for x in range(w))
            rows.append(r*scale)
        rgb=b''.join(rows); w*=scale; h*=scale
    raw=b''.join(b'\x00'+rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t,data):
        c=t+data
        return struct.pack('>I',len(data))+c+struct.pack('>I',zlib.crc32(c)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'
    png+=chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))
    png+=chunk(b'IDAT',zlib.compress(raw,9))
    png+=chunk(b'IEND',b'')
    open(path,'wb').write(png)
if __name__=='__main__':
    src,dst=sys.argv[1],sys.argv[2]
    sc=int(sys.argv[3]) if len(sys.argv)>3 else 1
    w,h,rgb=read_ppm(src); write_png(dst,w,h,rgb,sc)
    print(f"{src} -> {dst} ({w}x{h} scale={sc})")
