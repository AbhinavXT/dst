"""Schema-driven Kavach packet decoder (reference implementation).
Reads kavach.xml and decodes a frame into (name, value) rows. The C++ engine
mirrors this logic exactly."""
import xml.etree.ElementTree as ET

# ---------- bit cursors ----------
class MsbCursor:
    def __init__(s, b, start=0, limit=None):
        s.b=b; s.pos=start; s.limit=limit if limit is not None else len(b)*8
    def take(s, n):
        v=0
        for _ in range(n):
            if s.pos > s.limit-1: s.pos+=1; continue
            v=(v<<1)|((s.b[s.pos>>3]>>(7-(s.pos&7)))&1); s.pos+=1
        return v
class LsbCursor:                       # for Annexure-D RFID (not exercised here)
    def __init__(s,b,start=0,limit=None):
        s.b=b; s.pos=start; s.limit=limit if limit is not None else len(b)*8
    def take(s,n):
        v=0
        for i in range(n):
            if s.pos> s.limit-1: s.pos+=1; continue
            bit=(s.b[s.pos>>3]>>(s.pos&7))&1; v|=bit<<i; s.pos+=1
        return v

def to_signed(v,n):
    return v-(1<<n) if (v & (1<<(n-1))) else v

# ---------- schema model ----------
class Schema:
    def __init__(s, path):
        s.root=ET.parse(path).getroot()
        s.enums={e.get('name'):e for e in s.root.find('enums').findall('enum')}
        s.structs={st.get('name'):st for st in s.root.findall('struct')}
        s.packets=s.root.findall('packet')
    def enum_label(s, name, v):
        e=s.enums.get(name)
        if e is None: return str(v)
        for m in e.findall('map'):
            if m.get('v') is not None and int(m.get('v'))==v:
                return m.get('label', str(v))
            if m.get('from') is not None and int(m.get('from'))<=v<=int(m.get('to')):
                if m.get('formula'):
                    val=eval(m.get('formula'), {}, {'v':v})
                    return f"{val} {m.get('unit','')}".strip()
                return m.get('label', str(v))
        return str(v)

def cond_ok(when, ctx):
    if not when: return True
    # supports id==N, id!=N, id>N, id<N
    for op in ('==','!=','>=','<=','>','<'):
        if op in when:
            a,b=when.split(op); a=a.strip(); b=int(b.strip())
            x=ctx.get(a,0)
            return {'==':x==b,'!=':x!=b,'>':x>b,'<':x<b,'>=':x>=b,'<=':x<=b}[op]
    return True

# ---------- field walker ----------
def read_field(cur, fld, sch, ctx, rows, prefix=""):
    if fld.tag=='pad':
        cur.take(int(fld.get('bits'))); return
    if fld.tag=='field':
        if not cond_ok(fld.get('when'), ctx): return
        n=int(fld.get('bits')); raw=cur.take(n)
        val=to_signed(raw,n) if fld.get('signed')=='true' else raw
        if fld.get('id'): ctx[fld.get('id')]=val
        disp=str(val)
        if fld.get('enum'): disp=sch.enum_label(fld.get('enum'), val)
        elif fld.get('unit'): disp=f"{val} {fld.get('unit')}"
        rows.append((prefix+fld.get('name'), disp)); return
    if fld.tag=='repeat':
        cnt=ctx.get(fld.get('count'),0)
        nm=fld.get('name')
        for i in range(cnt):
            if cur.pos>=cur.limit: break
            local=dict(ctx)
            parts=[]
            for sub in fld:
                tmp=[]
                read_field(cur, sub, sch, local, tmp, prefix="")
                parts+=tmp
            label=f"  {nm}[{i+1}]"
            rows.append((label, "  ".join(f"{k.strip()}={v}" for k,v in parts)))
        return

def decode_struct(cur, st, sch, ctx, rows, prefix="  "):
    for fld in st:
        read_field(cur, fld, sch, ctx, rows, prefix=prefix)

def decode(frame, sch):
    rows=[]
    # pick packet by match (pkt_type==N on top nibble, msb-first)
    top=(frame[0]>>4)&0xF
    pkt=None
    for p in sch.packets:
        m=p.get('match','')
        if m.startswith('pkt_type=='):
            if int(m.split('==')[1])==top: pkt=p; break
    if pkt is None: return [("(no schema)", f"pkt_type={top}")]
    n=len(frame)
    cur=MsbCursor(frame, 0, (n-4)*8)   # exclude trailing CRC bytes
    ctx={}
    for fld in pkt:
        if fld.tag=='subpackets':
            tb=int(fld.get('type_bits')); lb=int(fld.get('len_bits'))
            reserve=int(fld.get('reserve_tail','8'))
            cases={c.get('type'):c.get('struct') for c in fld.findall('case')}
            idx=1
            while cur.pos//8 < n-reserve:
                sb=cur.pos//8
                t=cur.take(tb); L=cur.take(lb)
                if L==0 or sb+L> n-4: break
                stname=cases.get(str(t))
                rows.append((f"subpkt[{idx}] {stname or 'reserved(%d)'%t} @byte {sb}", f"{L} B"))
                if stname and stname in sch.structs:
                    sc=MsbCursor(frame, sb*8+tb+lb, sb*8+L*8)
                    decode_struct(sc, sch.structs[stname], sch, dict(ctx), rows)
                cur.pos=sb*8+L*8; idx+=1
        else:
            read_field(cur, fld, sch, ctx, rows, prefix="")
    return rows

# ---------- run against a real packet ----------
if __name__=="__main__":
    sch=Schema("kavach.xml")
    def parse(ln): return bytes(int(x,16) for x in ln.split()[3:])
    import sys
    cap = sys.argv[1] if len(sys.argv)>1 else "/home/claude/dl/dl_abh/replay/loco_1_1_18062026_140921.cap"
    target=None
    for ln in open(cap):
        if not ln.startswith("@slrp"): continue
        b=parse(ln)
        if len(b)<12: continue
        if ((b[0]>>4)&0xF)==9:
            rows=decode(b, sch)
            # find the screenshot one (ref 989)
            d=dict(rows)
            if d.get("LAST_REF_RFID")=="989" and any("989" in v for k,v in rows if k.strip().startswith("tag")):
                target=rows; break
    for k,v in target:
        print(f"{k:<34} {v}")
