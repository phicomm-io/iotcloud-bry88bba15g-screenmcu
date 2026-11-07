#!/usr/bin/env python3
import hashlib,json,re,struct,sys,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];b=Path(sys.argv[1]);image=b/'screen_firmware.bin';data=image.read_bytes();sp,reset=struct.unpack_from('<II',data)
assert len(data)==0x67000 and sp==0x20010000 and reset&1 and 0x14000<=reset<0x7b000
assert b'iotcloud-screen/BRY88BBA15G' in data
files=[b/'screen_firmware.elf',image,b/'screen_firmware.map']
manifest={'source':json.loads((Path(sys.argv[1])/'version.json').read_text()),'target':'BRY88BBA15G-screen','chip':'SWM34SVET6-50','application_base':0x14000,'application_limit':0x67000,'size':len(data),'sha256':hashlib.sha256(data).hexdigest(),'md5':hashlib.md5(data).hexdigest(),'installable':False,'install_gate':'verified application and NOR staging, erased copy tail','boot_backup_required':False,'boot_digest_required':False,'factory_copy_bytes':0x7e000,'required_blank_nor_tail':[0x7e7000,0x7fe000],'retained_sram_range':[0x2000bfc0,0x2000c000],'maintenance_diagnostics':3,'recovery_only':b'install=factory-uart;mode=recovery;' in data,'uart_maintenance_version':3,'startup_window_ms':30000,'boot_export_bytes':81920,'activation':'verified NOR staging, A1 flag, factory bootloader','toolchain':subprocess.check_output([str(Path.home()/'.cache/phicomm/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gcc'),'--version'],text=True).splitlines()[0],'source_files':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file() and p.suffix in ('.c','.h','.S','.ld','.cmake') and not any(x in p.parts for x in ['build','build-sanitized','artifacts','.git'])}}
manifest['uart2_quiet']=b'uart2=quiet;' in data
backlight=re.search(rb';backlight_max=(\d+);',data)
manifest['backlight_max_percent']=int(backlight.group(1)) if backlight else 100
(b/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');files.append(b/'manifest.json');(b/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in files))
