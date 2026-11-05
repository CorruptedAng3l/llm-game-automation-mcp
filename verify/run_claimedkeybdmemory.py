"""One actual source version, both arches; memory callbacks only, never desktop input."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess

CANONICAL_TESTS={'vi_claimed_keybd_memory.cpp': '0365BBAF054E431D41AE430C39E0135511C72779CF1CD44E55F0E08BB112E252', 'vi_keybd_protocol_memory.cpp': 'E81D3E5A1053E2EDAE4D190B37FB2A5F228BC033C412DFE1611228459170E568', 'vi_keybd_abi.cpp': '97647AAAD5E96CE54DA1BAAC623524D48443FB80F613EDB7520155D3A6E58C68', 'vi_keybd_inject_report_memory.cpp': '50C7D34F6F5A9689069FCCF9DAADB620EFC58518C0A858B96063A3D579CBC4A3'}

parser=argparse.ArgumentParser()
parser.add_argument('--repo-root',type=Path,required=True)
parser.add_argument('--source-root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--test-root',type=Path)
args=parser.parse_args();repo=args.repo_root.resolve();source=args.source_root.resolve();out=args.out.resolve()
tests=(args.test_root or repo/'test').resolve()
if not out.is_relative_to(repo):raise ValueError('Out must remain in private RepoRoot')
out.mkdir(parents=True,exist_ok=True);code=out/'code';code.mkdir(exist_ok=True)
pins=[]
for name in ('vi_cursor_guard.h','vi_cursor_install.inl','vi_cli_cursor_guard.inl','vi_claim_native_fence.h','vi_claim_native_fence.inl','vi_ready_json.h'):
    p=source/'src'/name;data=p.read_bytes();(code/name).write_bytes(data);pins.append({'path':str(p),'sha256':hashlib.sha256(data).hexdigest().upper()})
shutil.copyfile(repo/'src/vi_launch_injection_failure.h',code/'vi_launch_injection_failure.h')
text=(source/'src/vi_cli_readiness.inl').read_text(encoding='utf-8');a=text.index('static int ReportInputReady(');(code/'inject_report.inl').write_text(text[a:],encoding='utf-8',newline='\n')
text=(repo/'src/vi_cli_inject_reply.inl').read_text(encoding='utf-8');(code/'inject_emitter.inl').write_text(text[:text.index('struct InjectChildHandle')],encoding='utf-8',newline='\n')
text=(repo/'src/vinput_cli.cpp').read_text(encoding='utf-8');a=text.index('static std::string JEsc(');(code/'json_escape.inl').write_text(text[a:text.index('static std::wstring ExeDir(',a)],encoding='utf-8',newline='\n')
text=(source/'src/vi_cursor_guard.inl').read_text(encoding='utf-8');start=text.index('bool CursorProtectionReady(){');end=text.index('vi_cursor_guard::Owner CursorOwnerSnapshot(){',start)
(code/'readiness.inl').write_text(text[start:end],encoding='utf-8',newline='\n')
added=(source/'src/vi_keybd_event_guard.inl').exists()
if added:
    data=(source/'src/vi_keybd_event_guard.inl').read_bytes();(code/'vi_keybd_event_guard.inl').write_bytes(data)
    signature=data.decode('utf-8').replace('\r\n','\n').splitlines()[1]
    assert signature.startswith('using PFN_KeybdEvent=')
    pins.append({'path':str(source/'src/vi_keybd_event_guard.inl'),'sha256':hashlib.sha256(data).hexdigest().upper()})
else:signature='using PFN_KeybdEvent=void(WINAPI*)(BYTE,BYTE,DWORD,ULONG_PTR);' # official unguarded API shape, not an installed hook
(code/'keybd_signature.inl').write_text(signature+'\n',encoding='utf-8',newline='\n')
shutil.copyfile(tests/'vi_keybd_legacy8_contract.inl',code/'legacy_guard.inl')
(out/'source_pins.json').write_text(json.dumps({'has_keybd_source':added,'files':pins},indent=2)+'\n',encoding='utf-8')
vc=Path(__import__('os').environ.get('VINPUT_VCVARS_DIR','E:/MSVC/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build'));rows=[]
for arch,setup,machine in [('x64','vcvars64.bat',0x8664),('x86','vcvars32.bat',0x14c)]:
    for kind,name in [('guard','vi_claimed_keybd_memory.cpp'),('protocol','vi_keybd_protocol_memory.cpp'),('abi','vi_keybd_abi.cpp'),('report','vi_keybd_inject_report_memory.cpp')]:
        cpp=tests/name;data=cpp.read_bytes();canonical=data.replace(b'\r\n',b'\n');expected=CANONICAL_TESTS[name]
        if hashlib.sha256(canonical).hexdigest().upper()!=expected:raise ValueError('Reviewed same-test canonical source mismatch: '+name)
        build=out/arch/kind;build.mkdir(parents=True,exist_ok=True);exe=build/'model.exe';log=build/'build.log';imports=build/'imports.log';batch=build/'build.cmd'
        temp=build/'temp';temp.mkdir(exist_ok=True)
        batch.write_text('@echo off\nset "TEMP='+str(temp)+'"\nset "TMP='+str(temp)+'"\ncall "'+str(vc/setup)+'" >nul || exit /b 90\ncl /nologo /std:c++20 /O2 /MT /EHsc /W4 /WX /DHAS_KEYBD='+str(int(added))+' /I"'+str(code)+'" "'+str(cpp)+'" /Fo:"'+str(build/'model.obj')+'" /Fe:"'+str(exe)+'" /link /INCREMENTAL:NO >"'+str(log)+'" 2>&1\nif errorlevel 1 exit /b %errorlevel%\ndumpbin /nologo /imports "'+str(exe)+'" >"'+str(imports)+'"\nexit /b %errorlevel%\n',encoding='ascii')
        built=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=build,capture_output=True,text=True,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
        if built.returncode:raise RuntimeError(log.read_text(encoding='utf-8',errors='replace'))
        assert not re.search(r'\bwarning\s+[A-Z]+\d+',log.read_text(encoding='utf-8',errors='replace'),re.I)
        imported=imports.read_text(encoding='utf-8',errors='replace').lower();assert not any(n in imported for n in ('user32.dll','dinput','xinput','winmm.dll','gameinput.dll','shell32.dll'))
        pe=exe.read_bytes();at=struct.unpack_from('<I',pe,0x3c)[0];assert struct.unpack_from('<H',pe,at+4)[0]==machine
        ran=subprocess.run([str(exe)],cwd=build,capture_output=True,text=True,timeout=10,creationflags=subprocess.CREATE_NO_WINDOW)
        (build/'stdout.log').write_text(ran.stdout,encoding='utf-8');(build/'stderr.log').write_text(ran.stderr,encoding='utf-8')
        match=re.search(r'^RESULT (\d+) PASS (\d+) FAIL$',ran.stdout,re.M);assert match,ran.stdout
        rows.append({'arch':arch,'kind':kind,'compile_exit':0,'exit':ran.returncode,'passed':int(match[1]),'failed':int(match[2]),'cpp_raw_sha256':hashlib.sha256(data).hexdigest().upper(),'cpp_canonical_sha256':expected,
                     'checks':[{'pass':s.startswith('PASS '),'name':s[5:]} for s in ran.stdout.splitlines() if s.startswith(('PASS ','FAIL '))]})
result={'completed':True,'has_keybd_source':added,'scope':'pure loader/ownership/fence/original callbacks; no installed hook, real input or escape proof','passed':sum(r['passed'] for r in rows),'failed':sum(r['failed'] for r in rows),'rows':rows}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='rows'}));raise SystemExit(1 if result['failed'] or any(r['exit'] for r in rows) else 0)
