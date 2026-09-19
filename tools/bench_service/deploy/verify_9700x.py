#!/usr/bin/python3
"""Fail-closed installed-boundary and live-cgroup verifier."""

from __future__ import annotations
import datetime, fcntl, grp, hashlib, os, platform, pwd, re, socket, stat, subprocess, sys, tempfile
from pathlib import Path

U,G,CG="buster-bench","buster-bench","buster-bench-candidate"
CPU,MEM,SWAP,TASKS=2,8*1024*1024*1024,0,256
S=Path("/var/lib/buster-bench"); Q,L,W,V=S/"queue",S/"lease",S/"workspaces",S/"verification"
LOCK=L/"host.lock"; EVID=V/"latest.txt"
O=Path("/opt/buster-bench"); I,R=O/"installed",O/"installed/recipes"
E=Path("/etc/buster-bench"); MAN,LEASE_ID,HOST_ID=E/"installed.sha256",E/"lease.identity",E/"host.identity"
SB=Path("/usr/local/libexec/buster-bench-service"); BB=Path("/usr/local/libexec/buster-bench-build")
TB=Path("/usr/local/libexec/buster-bench-throughput"); VB=Path("/usr/local/libexec/buster-bench-verify")
P=R/"validate-buster-v1.recipe"
UNIT=Path("/etc/systemd/system/buster-bench.service"); SLICE=Path("/etc/systemd/system/buster-bench.slice")
SYSU=Path("/usr/lib/sysusers.d/buster-bench.conf"); TMP=Path("/usr/lib/tmpfiles.d/buster-bench.conf")
SUDO=Path("/etc/sudoers.d/buster-bench-gateway"); POLKIT=Path("/etc/polkit-1/rules.d/60-buster-bench.rules")
ENV={"PATH":"/usr/sbin:/usr/bin:/sbin:/bin","LC_ALL":"C","LANG":"C"}
DIRS=((S,0o710,"root",CG),(Q,0o700,U,G),(L,0o710,"root",G),(W,0o2710,U,CG),
      (V,0o700,U,G),(O,0o755,"root","root"),(I,0o550,"root",G),(R,0o550,"root",G),
      (E,0o755,"root","root"))
FILES={SB:(0o555,"root","root"),BB:(0o555,"root","root"),TB:(0o555,"root","root"),
       VB:(0o555,"root","root"),P:(0o440,"root",G),UNIT:(0o644,"root","root"),
       SLICE:(0o644,"root","root"),SYSU:(0o644,"root","root"),TMP:(0o644,"root","root"),
       SUDO:(0o440,"root","root"),POLKIT:(0o644,"root","root"),
       LEASE_ID:(0o444,"root","root"),HOST_ID:(0o444,"root","root"),MAN:(0o444,"root","root")}
MAN_FILES=frozenset(FILES)-{MAN}
JOB=re.compile(r"^buster-bench-[1-9][0-9]*-[1-9][0-9]*(?:-(?:base-generate|base-build|candidate-generate|candidate-build|throughput))?\.service$")

class Error(RuntimeError): pass
def fail(s): raise Error(s)
def run(a,check=True):
    r=subprocess.run(a,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=ENV)
    if check and r.returncode: fail(f"{' '.join(a)}: {r.stderr.strip() or r.stdout.strip() or r.returncode}")
    return r
def ids(u,g): return pwd.getpwnam(u).pw_uid,grp.getgrnam(g).gr_gid
def no_symlink(p):
    cur=Path("/")
    for x in p.parts[1:]:
        cur/=x
        st=cur.lstat()
        if stat.S_ISLNK(st.st_mode): fail(f"symlinked path component: {cur}")
def meta(p,mode,u,g,is_dir=False):
    no_symlink(p); st=p.lstat(); uid,gid=ids(u,g)
    kind=stat.S_ISDIR(st.st_mode) if is_dir else stat.S_ISREG(st.st_mode) and st.st_nlink==1
    if not kind or (st.st_uid,st.st_gid,stat.S_IMODE(st.st_mode))!=(uid,gid,mode):
        fail(f"metadata drift: {p}")
def read(p,cap=4<<20):
    fd=os.open(p,os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW)
    try:
        a=os.fstat(fd)
        if not stat.S_ISREG(a.st_mode) or a.st_nlink!=1 or a.st_size>cap: fail(f"invalid file: {p}")
        out=[];used=0
        while True:
            b=os.read(fd,min(1<<20,cap+1-used))
            if not b:break
            out.append(b);used+=len(b)
            if used>cap:fail(f"oversized file: {p}")
        z=os.fstat(fd)
        if (a.st_dev,a.st_ino,a.st_size)!=(z.st_dev,z.st_ino,z.st_size):fail(f"changed while reading: {p}")
        return b"".join(out)
    finally:os.close(fd)
def digest(p):return hashlib.sha256(read(p,1<<30)).hexdigest()
def optional(p):
    try:return read(p,2<<20).decode().strip()
    except (FileNotFoundError,PermissionError):return "-"
def kv(data,name):
    out={}
    for line in data.decode("ascii").splitlines():
        if "=" not in line:fail(f"bad {name}")
        k,v=line.split("=",1)
        if not k or k in out:fail(f"duplicate {name} key")
        out[k]=v
    return out

def manifest():
    meta(MAN,0o444,"root","root")
    rows={}
    for line in read(MAN,64<<10).decode("ascii").splitlines():
        if not re.fullmatch(r"[0-9a-f]{64}  /[^ \n]+",line):fail("bad identity manifest")
        h,n=line.split("  ",1); p=Path(n)
        if p in rows:fail("duplicate manifest path")
        rows[p]=h
    if frozenset(rows)!=MAN_FILES:fail("identity manifest closure mismatch")
    for p,h in rows.items():
        if digest(p)!=h:fail(f"identity digest drift: {p}")
    return rows

def cpuinfo(key):
    for line in read(Path("/proc/cpuinfo"),2<<20).decode().splitlines():
        if line.startswith(key+"\t") or line.startswith(key+" "):
            if ":" in line:return line.split(":",1)[1].strip()
    return "-"

def host_identity():
    f={"schema":"1","arch":platform.machine(),"kernel-release":platform.release(),
       "hostname":socket.gethostname(),"machine-id":optional(Path("/etc/machine-id")),
       "product-uuid":optional(Path("/sys/class/dmi/id/product_uuid")).lower(),
       "cpu-vendor":cpuinfo("vendor_id"),"cpu-family":cpuinfo("cpu family"),
       "cpu-model":cpuinfo("model"),"cpu-model-name":cpuinfo("model name"),
       "microcode":cpuinfo("microcode"),"cpu-online":optional(Path("/sys/devices/system/cpu/online")),
       "cpu-present":optional(Path("/sys/devices/system/cpu/present")),
       "smt-active":optional(Path("/sys/devices/system/cpu/smt/active")),
       "boost":optional(Path("/sys/devices/system/cpu/cpufreq/boost")),
       "no-turbo":optional(Path("/sys/devices/system/cpu/intel_pstate/no_turbo"))}
    for n in ("sched_autogroup_enabled","sched_rt_period_us","sched_rt_runtime_us","sched_rr_timeslice_ms",
              "sched_util_clamp_min","sched_util_clamp_max","sched_energy_aware","numa_balancing",
              "perf_event_paranoid","nmi_watchdog"):
        f["sysctl-"+n]=optional(Path("/proc/sys/kernel")/n)
    root=Path("/sys/devices/system/cpu")
    cpus=sorted((p for p in root.iterdir() if re.fullmatch(r"cpu[0-9]+",p.name)),key=lambda p:int(p.name[3:]))
    entries=(("online","online"),("core","topology/core_id"),("package","topology/physical_package_id"),
             ("die","topology/die_id"),("thread-siblings","topology/thread_siblings_list"),
             ("core-cpus","topology/core_cpus_list"),("governor","cpufreq/scaling_governor"),
             ("driver","cpufreq/scaling_driver"),("epp","cpufreq/energy_performance_preference"),
             ("min-freq","cpufreq/scaling_min_freq"),("max-freq","cpufreq/scaling_max_freq"))
    for p in cpus:
        n=int(p.name[3:])
        for k,r in entries:f[f"cpu-{n}-{k}"]=optional(p/r)
        if f[f"cpu-{n}-online"]=="-":f[f"cpu-{n}-online"]="1"
        f[f"cpu-{n}-nodes"]=",".join(sorted(x.name for x in p.iterdir() if re.fullmatch(r"node[0-9]+",x.name))) or "-"
    return "".join(f"{k}={f[k]}\n" for k in sorted(f)).encode()

def accounts():
    u=pwd.getpwnam(U); g=grp.getgrnam(G); c=grp.getgrnam(CG)
    if u.pw_gid!=g.gr_gid or c.gr_gid not in os.getgrouplist(U,u.pw_gid):fail("service group drift")
    grp.getgrnam("buster-bench-dispatch");grp.getgrnam("buster-bench-admin")

def lease(free=True):
    meta(LOCK,0o600,U,G)
    x=kv(read(LEASE_ID,4096),"lease identity")
    if x.get("schema")!="1" or x.get("path")!=str(LOCK):fail("lease identity header drift")
    fd=os.open(LOCK,os.O_RDWR|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK)
    try:
        st=os.fstat(fd); s=LOCK.lstat()
        if (str(st.st_dev),str(st.st_ino),str(st.st_uid),str(st.st_gid),f"{stat.S_IMODE(st.st_mode):04o}") != \
           (x.get("device"),x.get("inode"),x.get("uid"),x.get("gid"),x.get("mode")) or \
           (s.st_dev,s.st_ino)!=(st.st_dev,st.st_ino):fail("stable lease drift")
        if free:
            try:fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:fail("active lease")
        return st.st_dev,st.st_ino
    finally:os.close(fd)

def show(unit,props):
    a=["/usr/bin/systemctl","show","--no-pager"]+[f"--property={p}" for p in props]+[unit]
    out={}
    for line in run(a).stdout.splitlines():
        if "=" not in line:fail("malformed systemctl output")
        k,v=line.split("=",1);out[k]=v
    if any(p not in out for p in props):fail(f"missing systemd property: {unit}")
    return out

def own_cgroup():
    for line in read(Path("/proc/self/cgroup"),64<<10).decode().splitlines():
        if line.startswith("0::"):return line[3:]
    fail("not cgroup v2")

def competing(allow_main=False):
    r=run(["/usr/bin/systemctl","list-units","--all","--plain","--no-legend","--type=service","buster-bench*.service"],False)
    if r.returncode not in (0,1):fail("cannot list benchmark units")
    inside=own_cgroup()=="/buster-bench.slice/buster-bench.service"; out=[]
    for line in r.stdout.splitlines():
        x=line.split(None,4)
        if len(x)<4 or x[2]=="inactive":continue
        if x[0]=="buster-bench.service" and (inside or allow_main) and x[2] in {"active","activating"}:continue
        if x[0]=="buster-bench.service" or JOB.fullmatch(x[0]):out.append(f"{x[0]}:{x[2]}:{x[3]}")
    return out

def definitions():
    s=show("buster-bench.service",("FragmentPath","User","Group","SupplementaryGroups","Slice","ExecStart",
                                   "ExecStartPre","NoNewPrivileges","ProtectSystem","PrivateNetwork","RestrictAddressFamilies"))
    expected={"FragmentPath":str(UNIT),"User":U,"Group":G,"Slice":"buster-bench.slice",
              "NoNewPrivileges":"yes","ProtectSystem":"strict","PrivateNetwork":"yes","RestrictAddressFamilies":"AF_UNIX"}
    for k,v in expected.items():
        if s[k]!=v:fail(f"service property drift: {k}={s[k]}")
    if CG not in s["SupplementaryGroups"].split() or str(SB) not in s["ExecStart"] or " serve " not in s["ExecStart"]:
        fail("fixed service command/group drift")
    if str(VB) not in s["ExecStartPre"] or " preflight" not in s["ExecStartPre"]:fail("missing preflight")
    z=show("buster-bench.slice",("FragmentPath","AllowedCPUs","MemoryMax","MemorySwapMax","TasksMax"))
    e={"FragmentPath":str(SLICE),"AllowedCPUs":str(CPU),"MemoryMax":str(MEM),
       "MemorySwapMax":str(SWAP),"TasksMax":str(TASKS)}
    for k,v in e.items():
        if z[k]!=v:fail(f"slice property drift: {k}={z[k]}")

def preflight(allow_main=False):
    accounts()
    for p,m,u,g in DIRS:meta(p,m,u,g,True)
    for p,(m,u,g) in FILES.items():meta(p,m,u,g)
    rows=manifest()
    if host_identity()!=read(HOST_ID,2<<20):fail("host/kernel/topology/scheduler drift")
    lease(True)
    c=competing(allow_main)
    if c:fail("competing/stale units: "+", ".join(c))
    run(["/usr/sbin/visudo","-cf",str(SUDO)])
    definitions()
    return rows

def dfile(fd,name):
    x=os.open(name,os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW,dir_fd=fd)
    try:return os.read(x,4096).decode("ascii").strip()
    finally:os.close(x)
def cpus(s):
    out=set()
    for x in s.split(","):
        if "-" in x:
            a,b=map(int,x.split("-",1));out.update(range(a,b+1))
        else:out.add(int(x))
    return out
def limit(s):return None if s=="max" else int(s)

def cgroup(path):
    if path!="/buster-bench.slice/buster-bench.service":fail(f"cgroup drift: {path}")
    mi=read(Path("/proc/self/mountinfo"),2<<20).decode()
    if not any(" /sys/fs/cgroup " in x and " - cgroup2 " in x for x in mi.splitlines()):fail("not cgroup2")
    ds=[os.open("/sys/fs/cgroup",os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW)]
    labels=["/sys/fs/cgroup"]
    try:
        for n in path.lstrip("/").split("/"):
            if not re.fullmatch(r"[A-Za-z0-9_.-]+",n) or n in {".",".."}:fail("unsafe cgroup path")
            ds.append(os.open(n,os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW,dir_fd=ds[-1]))
            labels.append(labels[-1]+"/"+n)
        ev=[];ms=[];ss=[];ps=[]
        for i,fd in enumerate(ds):
            c,m,s,p,e=(dfile(fd,n) for n in ("cpuset.cpus.effective","memory.max","memory.swap.max","pids.max","cgroup.events"))
            if CPU not in cpus(c):fail("installed CPU excluded")
            ms.append(limit(m));ss.append(limit(s));ps.append(limit(p))
            ev += [f"cgroup-{i}-path={labels[i]}",f"cgroup-{i}-cpus={c}",f"cgroup-{i}-memory-max={m}",
                   f"cgroup-{i}-swap-max={s}",f"cgroup-{i}-pids-max={p}",f"cgroup-{i}-events={e.replace(chr(10),';')}"]
        if cpus(dfile(ds[1],"cpuset.cpus.effective"))!={CPU} or cpus(dfile(ds[2],"cpuset.cpus.effective"))!={CPU}:fail("CPU isolation drift")
        eff=lambda a:min(x for x in a if x is not None) if any(x is not None for x in a) else None
        if ms[1]!=MEM or eff(ms)!=MEM or ss[1]!=SWAP or eff(ss)!=SWAP or ps[1]!=TASKS or eff(ps)!=TASKS:
            fail("effective cgroup limit drift")
        if "populated 1" not in dfile(ds[2],"cgroup.events").splitlines():fail("service cgroup empty")
        return ev
    finally:
        for fd in reversed(ds):os.close(fd)

def process(pid,path):
    if pid<=1:fail("bad MainPID")
    if f"0::{path}" not in read(Path(f"/proc/{pid}/cgroup"),64<<10).decode().splitlines():fail("MainPID cgroup mismatch")
    fields={}
    for line in read(Path(f"/proc/{pid}/status"),256<<10).decode().splitlines():
        if ":" in line:
            k,v=line.split(":",1);fields[k]=v.strip()
    uid,gid=ids(U,G)
    if fields.get("Uid","").split()!=[str(uid)]*4 or fields.get("Gid","").split()!=[str(gid)]*4:fail("MainPID credentials drift")
    return [f"main-pid={pid}",f"main-pid-cgroup={path}",f"main-pid-uid={uid}",f"main-pid-gid={gid}"]

def publish_evidence(data):
    uid,gid=ids(U,G);fd,name=tempfile.mkstemp(prefix=".verification.",dir=V);tmp=Path(name)
    try:
        os.fchmod(fd,0o400);os.fchown(fd,uid,gid);view=memoryview(data)
        while view:
            n=os.write(fd,view)
            if n<=0:fail("short evidence write")
            view=view[n:]
        os.fsync(fd);os.close(fd);fd=-1
        os.replace(tmp,EVID)
        d=os.open(V,os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC)
        try:os.fsync(d)
        finally:os.close(d)
    finally:
        if fd>=0:os.close(fd)
        try:tmp.unlink()
        except FileNotFoundError:pass

def live():
    rows=preflight(True)
    s=show("buster-bench.service",("ActiveState","SubState","MainPID","ControlGroup","InvocationID","Slice","User","Group"))
    if (s["ActiveState"],s["SubState"],s["Slice"],s["User"],s["Group"])!=("active","running","buster-bench.slice",U,G):
        fail("service not active with installed identity")
    if not s["MainPID"].isdigit() or not re.fullmatch(r"[0-9a-f]{32}",s["InvocationID"]):fail("bad live identity")
    boot=optional(Path("/proc/sys/kernel/random/boot_id"))
    if not re.fullmatch(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}",boot):fail("bad boot ID")
    lines=["schema=1","qualification=installed-boundary-live-verification","status=passed",
           "recorded-at="+datetime.datetime.now(datetime.timezone.utc).isoformat(),
           "boot-id="+boot,"service-invocation-id="+s["InvocationID"]]
    lines += process(int(s["MainPID"]),s["ControlGroup"]) + cgroup(s["ControlGroup"])
    dev,ino=lease(True);lines += [f"lease-device={dev}",f"lease-inode={ino}"]
    lines += ["host-"+x for x in read(HOST_ID,2<<20).decode().splitlines()]
    lines += [f"sha256={h}  {p}" for p,h in sorted(rows.items(),key=lambda x:str(x[0]))]
    body=("\n".join(lines)+"\n").encode(); h=hashlib.sha256(body).hexdigest()
    publish_evidence(body+f"evidence-sha256={h}\n".encode())
    print(f"VERIFY_LIVE_OK evidence={EVID} sha256={h}")

def main():
    if len(sys.argv)!=2 or sys.argv[1] not in {"capture-host-identity","preflight","live"}:
        fail("usage: buster-bench-verify capture-host-identity|preflight|live")
    if sys.argv[1]=="capture-host-identity":
        if os.geteuid()!=0:fail("identity capture requires root")
        sys.stdout.buffer.write(host_identity())
    elif sys.argv[1]=="preflight":
        preflight();print("VERIFY_PREFLIGHT_OK")
    else:live()
    return 0

if __name__=="__main__":
    try:raise SystemExit(main())
    except (Error,KeyError,OSError,ValueError) as e:
        print(f"VERIFY_FAIL {e}",file=sys.stderr);raise SystemExit(1)
