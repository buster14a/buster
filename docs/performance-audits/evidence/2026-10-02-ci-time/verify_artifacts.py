"""Verify retained GitHub ZIPs against published digests; no builds or tests run."""
from pathlib import Path
import hashlib, json, csv
ROOT=Path(__file__).resolve().parent / 'raw'
C4='''linux-x86_64-release 11222316504 7c1bd5a1ed7699830647e1ee305f85d6731e518bc8cf9eeb9fd2abd69e2b06db
linux-x86_64-sanitized-debug 11222655693 8f1863c56b0f2eea4940e4bfab489cb8826e1ed19975e5fc650db0297e5f750b
linux-x86_64-sanitized-release 11222156061 3ad95a3768db5b66c62d1ed9575779a25b3950d22069f4f00092cf0ce44158f8
linux-x86_64-portability 11221905927 6feced5b5d9fbc561d517d2c4c82eeae74113765dc07d7cc1cf889a395931972
linux-aarch64-release 11221454900 7ef48a877c53252169484621441b0eb468af8a66aa19fbffe4314a512131d160
linux-aarch64-sanitized-debug 11221604683 ac700f942ac38c0fbecdf53efc08791c9689dd1919a03d127ed76a3c8181a119
linux-aarch64-sanitized-release 11221349528 effa3d4cbcad58498c53b9a1c3449c7a58108e946cb6cd349a0836bc28bc4c35
linux-aarch64-portability 11221631557 b4c544df15c843fba4d4786c6278147fbb95ebf0a44c19c4a1453fc172b45800
macos-aarch64-checks 11222700913 eb6c5acb25ca88d07d02f7656c68e61c02705d9d9e795c0c81390fba4f6b0e53
macos-aarch64-release 11222187067 46548229794ba5d03c06df2b76a0637c7b5d6128461cc854f10b6a03f0849597
windows-x86_64-sanitized-debug 11222651454 64202b4dab44783072df464f1702737a01236f0bd4f30dbc784abd0d6170f712
windows-x86_64-sanitized-release 11221414878 2b2640e7dfcc4621fbd14ec3cbf3f259906e783c02162510a98fa763a01be4ad
windows-x86_64-release 11221664191 ca23350995c12fe3d6f49e1c73a08ec23aac18b62a5c6ed721215b9b0bdcc3ec
windows-x86_64-portability 11222072051 5a633c9abffb88b38c2842441c7746b5ddd42445e3c3ac4be7c8135befba8846
windows-aarch64-release 11222690825 3d773c08749548e28332931efc779f20effa93e0597d1634ec017be901c7be1a
windows-aarch64-checks 11221418935 e662a8da7bf6f6cc60be1b0fd2d788edbe0b701851b94bb3a1cc6b279ac80baa'''
A3='''windows-x86_64-checks 11221155025 53a0cac7adb34afb457b6844235e4148f0636bb9ee13621ccdc1ca998022dae3
windows-aarch64-release 11220765495 16049cf07ce3bed1f2b63154bce916fbf284e8e742c184a88a1c8394f5b54bc9
linux-aarch64-checks 11220536143 b3274844ec427e4dfc769e30c7177d97230d86fcf7b7b4edd5ad7c4ccb515a93
linux-x86_64-checks 11220525894 22ca3728694afab06b8684f308ebfac7ee0ef3181f28ec69aa4ea11dde794c78
macos-aarch64-checks 11220411223 3c8ec9a37b86757d1e8bb62d05f7233ffbe752ebf46934eba04dbb58a53fa807
windows-x86_64-release 11220081429 b5d2a60060f2e004f64d1b990980c741091b598f5497758aa2e123c90d9b7eff
linux-aarch64-release 11219863013 b9ae5d1992f0bb8e7da17328f2d00b0de58ee2db84ae93ac4e29b709fe3b93ae
windows-aarch64-checks 11219777291 7f660d4369e3e8afe60939bd07f671ca3bcc81430282b0cfe9f93c35bd8b4680
linux-x86_64-release 11219763396 aecb9d2b6cf37accc6057f326b2f417f75689c062ceae78499444b713a477bb7
macos-aarch64-release 11219723692 5405222fc269f8eaf2b50cd116e9cac1d10f11ec017a2651f15a2d01cdb925ff'''
rows=[]
for run, data in [('36996168725',C4),('36991068435',A3)]:
 for line in data.splitlines():
  lane,aid,digest=line.split()
  rows.append(dict(run_id=run,attempt=1,artifact_id=int(aid),filename=f'desktop-{lane}-{run}-1.zip',published_sha256=digest,source_revision='e424b387fcb51b00c1d19e87e2d369ae8a212792'))
rows.append(dict(run_id='36996168725',attempt=1,artifact_id=11222445904,filename='clang-analyzer-36996168725-1.zip',published_sha256='a19e677994802c67eeb66f9c4b7a12a7d21d49ce3c40ad79831fd4175144601b',source_revision='e424b387fcb51b00c1d19e87e2d369ae8a212792'))
for r in rows:
 p=ROOT/r['filename'];r['actual_sha256']=hashlib.sha256(p.read_bytes()).hexdigest();r['bytes']=p.stat().st_size
 if r['actual_sha256'] != r['published_sha256']: raise ValueError(f"Digest mismatch {p.name}")
 r['sha256_verified']=True;r['url']=f"https://github.com/buster14a/buster/actions/runs/{r['run_id']}/artifacts/{r['artifact_id']}"
OUT=Path(__file__).resolve().parent / 'derived'; OUT.mkdir(exist_ok=True);(OUT/'verified_artifacts.json').write_text(json.dumps(rows,indent=2))
with (OUT/'verified_artifacts.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
print('Verified',len(rows),'raw ZIP SHA-256 digests,',sum(r['bytes'] for r in rows),'bytes.')
