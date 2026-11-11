import pathlib,sys,hashlib,json,re
r=pathlib.Path(sys.argv[1]); rt=r/'runtime'
for name,digest in json.loads((rt/'checksums.json').read_text()).items():
 p=r/name
 if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=digest:raise SystemExit('Missing or damaged file: '+name)
for d in ['savedata/home/.WorldOfGoo','savedata/config','savedata/data','savedata/preferences','logs']:(r/d).mkdir(parents=True,exist_ok=True)
config='''; RGDS+ Alpha 1.2 performance defaults. Edit after the first launch if desired.
language = en
vsync = 1
use_2x_assets = 1
use_fbo = 0
fbo_width = 800
fbo_height = 600
screen_width = 1024
screen_height = 768
relative_mouse = 0
'''
for dest in ['savedata/home/.WorldOfGoo/config.ini','savedata/preferences/config.ini']:
 p=r/dest
 if not p.exists():
  p.write_text(config)
 else:
  previous=p.read_text()
  settings=re.sub(r'(?im)^[ \t]*use_fbo[ \t]*=.*$', 'use_fbo = 0', previous)
  if settings==previous and not re.search(r'(?im)^[ \t]*use_fbo[ \t]*=',previous):
   settings=previous.rstrip()+'\nuse_fbo = 0\n'
  if settings!=previous:
   backup=p.with_name('config.pre-alpha12.ini')
   if not backup.exists():backup.write_text(previous)
   staged=p.with_suffix('.ini.tmp')
   staged.write_text(settings)
   staged.replace(p)
print('Runtime and original game executable checksums OK',flush=True)
