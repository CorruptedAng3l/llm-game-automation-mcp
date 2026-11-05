"""Compile/run one frozen source version, both arches, no UI/DI API imports.

The SAME test naturally exits1 on a failing old version and0 on a passing version.
There is no expected-red inversion. Run under the parent's compiler job boundary.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys

parser=argparse.ArgumentParser()
parser.add_argument('--repo-root',type=Path,required=True)
parser.add_argument('--source-root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--fence-dir',type=Path)
args=parser.parse_args()
repo=args.repo_root.resolve();source=args.source_root.resolve();out=args.out.resolve()
if not out.is_relative_to(repo):raise ValueError('Out must remain below private RepoRoot')
out.mkdir(parents=True,exist_ok=True)
code=out/'code'
extract=[sys.executable,str(repo/'verify/prepare_claimed_cursor_memory.py'),'--source-root',str(source),'--out',str(code)]
if args.fence_dir:extract+=['--fence-dir',str(args.fence_dir.resolve())]
subprocess.run(extract,check=True,timeout=15)
cpp=repo/'test/vi_claimed_cursor_memory.cpp';cpp_bytes=cpp.read_bytes()
cpp_hash=hashlib.sha256(cpp_bytes).hexdigest().upper()
cpp_canonical_hash=hashlib.sha256(cpp_bytes.replace(b'\r\n',b'\n')).hexdigest().upper()
if cpp_canonical_hash!='AF6636B6CF1E4496FA6CF6E67D15E5E0CDC29B20C889C30C99ABAE44311E2A85':raise ValueError('SAME reviewed pure test canonical-LF source hash mismatch')
vc=Path(__import__('os').environ.get('VINPUT_VCVARS_DIR','E:/MSVC/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build'))
rows=[]
for arch,setup,machine in [('x64','vcvars64.bat',0x8664),('x86','vcvars32.bat',0x14c)]:
 folder=out/arch;folder.mkdir(exist_ok=True);exe=folder/'vi_claimed_cursor_memory.exe';log=folder/'build.log';imports=folder/'imports.log';batch=folder/'build.cmd'
 batch.write_text('@echo off\ncall "'+str(vc/setup)+'" >nul || exit /b 90\n'+
  'cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I"'+str(code)+'" "'+str(cpp)+'" /Fo:"'+str(folder/'test.obj')+'" /Fe:"'+str(exe)+'" /link /INCREMENTAL:NO >"'+str(log)+'" 2>&1\n'+
  'if errorlevel 1 exit /b %errorlevel%\ndumpbin /nologo /imports "'+str(exe)+'" >"'+str(imports)+'"\nexit /b %errorlevel%\n',encoding='ascii')
 built=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=folder,capture_output=True,text=True,timeout=120)
 row={'arch':arch,'source_sha256':cpp_hash,'source_canonical_lf_sha256':cpp_canonical_hash,'compile_exit':built.returncode,'build_log':str(log),'imports':str(imports)};rows.append(row)
 if built.returncode:
  (out/'result.json').write_text(json.dumps({'completed':False,'rows':rows},indent=2)+'\n',encoding='utf-8');raise RuntimeError(log.read_text(encoding='utf-8',errors='replace'))
 if re.search(r'\bwarning\s+[A-Z]+\d+',log.read_text(encoding='utf-8',errors='replace'),re.I):raise RuntimeError('Compiler warning')
 imported=imports.read_text(encoding='utf-8',errors='replace').lower()
 forbidden=[name for name in ['user32.dll','dinput8.dll','dinput.dll','winmm.dll','gdi32.dll'] if name in imported]
 if forbidden:raise RuntimeError('Forbidden UI/DI import: '+repr(forbidden))
 data=exe.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
 if struct.unpack_from('<H',data,pe+4)[0]!=machine:raise RuntimeError('Wrong architecture')
 ran=subprocess.run([str(exe)],cwd=folder,capture_output=True,text=True,timeout=20)
 (folder/'stdout.log').write_text(ran.stdout,encoding='utf-8');(folder/'stderr.log').write_text(ran.stderr,encoding='utf-8')
 match=re.search(r'^RESULT (\d+) PASS (\d+) FAIL$',ran.stdout,re.M)
 if not match:raise RuntimeError('Complete model result unavailable')
 row.update({'exit':ran.returncode,'passed':int(match[1]),'failed':int(match[2]),'forbidden_imports':forbidden,'scope':'memory callbacks only; no installed hook/desktop/DI escape proof','checks':[{'pass':line.startswith('PASS '),'name':line[5:]} for line in ran.stdout.splitlines() if line.startswith(('PASS ','FAIL '))]})
record={'schema':1,'completed':True,'source_sha256':cpp_hash,'source_canonical_lf_sha256':cpp_canonical_hash,'source_hash_policy':'exact AF6636 canonical source; CRLF-to-LF only','passed':sum(r['passed'] for r in rows),'failed':sum(r['failed'] for r in rows),'rows':rows}
(out/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'completed':True,'passed':record['passed'],'failed':record['failed'],'out':str(out)}))
sys.exit(1 if record['failed'] or any(row['exit']!=0 for row in rows) else 0)
