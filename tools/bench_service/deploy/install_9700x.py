#!/usr/bin/python3
"""Idempotent, fail-closed installer for the dedicated Ryzen 7 9700X worker."""

from __future__ import annotations
import fcntl, grp, hashlib, os, pwd, stat, subprocess, sys, tempfile
from pathlib import Path

U, G, CG = "buster-bench", "buster-bench", "buster-bench-candidate"
S = Path("/var/lib/buster-bench")
Q, L, W, V = S/"queue", S/"lease", S/"workspaces", S/"verification"
LOCK = L/"host.lock"
O = Path("/opt/buster-bench")
I, R, T = O/"installed", O/"installed/recipes", O/"staging"
E = Path("/etc/buster-bench")
MAN, LEASE_ID, HOST_ID = E/"installed.sha256", E/"lease.identity", E/"host.identity"
SB = Path("/usr/local/libexec/buster-bench-service")
BB = Path("/usr/local/libexec/buster-bench-build")
TB = Path("/usr/local/libexec/buster-bench-throughput")
VB = Path("/usr/local/libexec/buster-bench-verify")
P = R/"validate-buster-v1.recipe"
UNIT = Path("/etc/systemd/system/buster-bench.service")
SLICE = Path("/etc/systemd/system/buster-bench.slice")
SYSU = Path("/usr/lib/sysusers.d/buster-bench.conf")
TMP = Path("/usr/lib/tmpfiles.d/buster-bench.conf")
SUDO = Path("/etc/sudoers.d/buster-bench-gateway")
POLKIT = Path("/etc/polkit-1/rules.d/60-buster-bench.rules")
ENV = {"PATH":"/usr/sbin:/usr/bin:/sbin:/bin", "LC_ALL":"C", "LANG":"C"}
DIRS = (
    (S,0o710,"root",CG),(Q,0o700,U,G),(L,0o710,"root",G),
    (W,0o2710,U,CG),(V,0o700,U,G),(O,0o755,"root","root"),
    (I,0o550,"root",G),(R,0o550,"root",G),(E,0o755,"root","root"),
)

class Error(RuntimeError): pass
def fail(s): raise Error(s)
def run(a, check=True):
    r=subprocess.run(a,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=ENV)
    if check and r.returncode:
        fail(f"{' '.join(a)}: {r.stderr.strip() or r.stdout.strip() or r.returncode}")
    return r
def ids(u,g): return pwd.getpwnam(u).pw_uid, grp.getgrnam(g).gr_gid

def no_symlink(p, missing=False):
    cur=Path("/")
    for x in p.parts[1:]:
        cur/=x
        try: st=cur.lstat()
        except FileNotFoundError:
            if missing: return
            fail(f"missing path component: {cur}")
        if stat.S_ISLNK(st.st_mode): fail(f"symlinked path component: {cur}")

def source(p, executable=False):
    no_symlink(p)
    st=p.lstat()
    if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1 or st.st_uid or st.st_mode&0o022:
        fail(f"untrusted reviewed source: {p}")
    if executable and not st.st_mode&0o100: fail(f"source is not executable: {p}")

def directory(p,mode,u,g,missing):
    no_symlink(p,missing=True)
    try: st=p.lstat()
    except FileNotFoundError:
        if missing:return
        fail(f"missing directory: {p}")
    uid,gid=ids(u,g)
    if not stat.S_ISDIR(st.st_mode) or (st.st_uid,st.st_gid,stat.S_IMODE(st.st_mode))!=(uid,gid,mode):
        fail(f"directory drift: {p}")

def read(p):
    fd=os.open(p,os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW)
    try:
        a=os.fstat(fd)
        if not stat.S_ISREG(a.st_mode) or a.st_nlink!=1: fail(f"not a regular file: {p}")
        out=[]
        while True:
            b=os.read(fd,1<<20)
            if not b: break
            out.append(b)
        z=os.fstat(fd)
        if (a.st_dev,a.st_ino,a.st_size)!=(z.st_dev,z.st_ino,z.st_size): fail(f"changed while reading: {p}")
        return b"".join(out)
    finally: os.close(fd)

def fsync_dir(p):
    fd=os.open(p,os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC)
    try: os.fsync(fd)
    finally: os.close(fd)

def publish(dst,data,mode,u="root",g="root"):
    no_symlink(dst.parent)
    if dst.exists() or dst.is_symlink():
        st=dst.lstat()
        if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1: fail(f"refusing to replace: {dst}")
    uid,gid=ids(u,g)
    fd,name=tempfile.mkstemp(prefix=f".{dst.name}.",dir=dst.parent)
    tmp=Path(name)
    try:
        os.fchmod(fd,mode); os.fchown(fd,uid,gid)
        view=memoryview(data)
        while view:
            n=os.write(fd,view)
            if n<=0: fail(f"short write: {dst}")
            view=view[n:]
        os.fsync(fd); os.close(fd); fd=-1
        os.replace(tmp,dst); fsync_dir(dst.parent)
    finally:
        if fd>=0: os.close(fd)
        try: tmp.unlink()
        except FileNotFoundError: pass

def install(src,dst,mode,u="root",g="root"): publish(dst,read(src),mode,u,g)
def sha(p): return hashlib.sha256(read(p)).hexdigest()

def active_units():
    r=run(["/usr/bin/systemctl","list-units","--all","--plain","--no-legend",
           "--type=service","buster-bench*.service"],False)
    if r.returncode not in (0,1): fail("cannot inspect benchmark units")
    out=[]
    for line in r.stdout.splitlines():
        x=line.split(None,4)
        if len(x)>=4 and x[2]!="inactive": out.append(f"{x[0]}:{x[2]}:{x[3]}")
    return out

def open_lease(uid,gid):
    flags=os.O_RDWR|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK
    try: fd=os.open(LOCK,flags)
    except FileNotFoundError:
        try: fd=os.open(LOCK,flags|os.O_CREAT|os.O_EXCL,0o600)
        except FileExistsError: fail("lease appeared during installation")
        os.fchmod(fd,0o600); os.fchown(fd,uid,gid); os.fsync(fd); fsync_dir(L)
    st=os.fstat(fd)
    if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1 or \
       (st.st_uid,st.st_gid,stat.S_IMODE(st.st_mode))!=(uid,gid,0o600):
        os.close(fd); fail("lease metadata drift; refusing repair")
    selected=LOCK.lstat()
    if (selected.st_dev,selected.st_ino)!=(st.st_dev,st.st_ino):
        os.close(fd); fail("lease path/inode race")
    try: fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except BlockingIOError:
        os.close(fd); fail("active lease; installation forbidden")
    return fd,st

def main():
    if len(sys.argv)!=1 or os.geteuid()!=0: fail("run as root with no arguments")
    for c in ("/usr/bin/systemctl","/usr/bin/systemd-sysusers","/usr/bin/systemd-tmpfiles",
              "/usr/sbin/visudo","/usr/bin/python3"):
        if not Path(c).is_file() or not os.access(c,os.X_OK): fail(f"missing command: {c}")
    d=Path(__file__).resolve().parent
    repo=d.parents[2]
    src={
      T/"buster-bench-service":SB,T/"buster-bench-build":BB,T/"buster-bench-throughput":TB,
      d/"verify_9700x.py":VB,
      repo/"tools/bench_service/profiles/validate-buster-v1.recipe":P,
      d/"buster-bench.service":UNIT,d/"buster-bench.slice":SLICE,
      d/"buster-bench.sysusers.conf":SYSU,d/"buster-bench.tmpfiles.conf":TMP,
      d/"buster-bench-gateway.sudoers":SUDO,d/"60-buster-bench.rules":POLKIT,
    }
    source(Path(__file__).resolve())
    no_symlink(T); st=T.lstat()
    if not stat.S_ISDIR(st.st_mode) or st.st_uid or stat.S_IMODE(st.st_mode)!=0o700:
        fail("staging must be root-owned mode 0700")
    for p in src: source(p,p in {T/"buster-bench-service",T/"buster-bench-build",T/"buster-bench-throughput"})
    prior=(SB,BB,TB,VB,P,UNIT,SLICE,SYSU,TMP,SUDO,POLKIT,LEASE_ID,HOST_ID)
    present=[p for p in prior if p.exists() or p.is_symlink()]
    if MAN.exists() or MAN.is_symlink():
        r=run([str(VB),"preflight"],False)
        if r.returncode: fail("installed boundary drift: "+(r.stderr.strip() or r.stdout.strip()))
    elif present: fail("unmanaged prior installation: "+", ".join(map(str,present)))
    live=active_units()
    if live: fail("benchmark units not inactive: "+", ".join(live))
    install(d/"buster-bench.sysusers.conf",SYSU,0o644)
    run(["/usr/bin/systemd-sysusers",str(SYSU)])
    uid,gid=ids(U,G)
    if grp.getgrnam(CG).gr_gid not in os.getgrouplist(U,gid): fail("missing candidate group membership")
    grp.getgrnam("buster-bench-dispatch"); grp.getgrnam("buster-bench-admin")
    for x,m,u,g in DIRS: directory(x,m,u,g,True)
    install(d/"buster-bench.tmpfiles.conf",TMP,0o644)
    run(["/usr/bin/systemd-tmpfiles","--create",str(TMP)])
    for x,m,u,g in DIRS: directory(x,m,u,g,False)
    fd,initial=open_lease(uid,gid)
    try:
        record=(f"schema=1\npath={LOCK}\ndevice={initial.st_dev}\ninode={initial.st_ino}\n"
                f"uid={initial.st_uid}\ngid={initial.st_gid}\nmode={stat.S_IMODE(initial.st_mode):04o}\n").encode()
        if LEASE_ID.exists():
            if read(LEASE_ID)!=record: fail("recorded lease inode drift")
        else: publish(LEASE_ID,record,0o444)
        modes={SB:(0o555,"root","root"),BB:(0o555,"root","root"),TB:(0o555,"root","root"),
               VB:(0o555,"root","root"),P:(0o440,"root",G),UNIT:(0o644,"root","root"),
               SLICE:(0o644,"root","root"),SYSU:(0o644,"root","root"),TMP:(0o644,"root","root"),
               SUDO:(0o440,"root","root"),POLKIT:(0o644,"root","root")}
        for a,b in src.items(): install(a,b,*modes[b])
        run(["/usr/sbin/visudo","-cf",str(SUDO)])
        run(["/usr/bin/systemctl","daemon-reload"])
        host=run([str(VB),"capture-host-identity"]).stdout.encode()
        if not host.endswith(b"\n"): fail("unterminated host identity")
        publish(HOST_ID,host,0o444)
        files=(SB,BB,TB,VB,P,UNIT,SLICE,SYSU,TMP,SUDO,POLKIT,LEASE_ID,HOST_ID)
        publish(MAN,"".join(f"{sha(p)}  {p}\n" for p in files).encode(),0o444)
        run([str(VB),"preflight"])
        final=os.fstat(fd); selected=LOCK.lstat()
        if (final.st_dev,final.st_ino)!=(initial.st_dev,initial.st_ino) or \
           (selected.st_dev,selected.st_ino)!=(initial.st_dev,initial.st_ino):
            fail("lease inode changed during installation")
    finally: os.close(fd)
    print(f"INSTALL_OK lease={initial.st_dev}:{initial.st_ino}")
    print("service stopped; dispatch disabled")
    return 0

if __name__=="__main__":
    try: raise SystemExit(main())
    except (Error,KeyError,OSError) as e:
        print(f"INSTALL_FAIL {e}",file=sys.stderr); raise SystemExit(1)
