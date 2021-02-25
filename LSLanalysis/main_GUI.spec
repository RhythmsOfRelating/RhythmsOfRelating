# -*- mode: python ; coding: utf-8 -*-

block_cipher = None

<<<<<<< HEAD

a = Analysis(['main_GUI.py'],
             pathex=['D:\\PycharmProjects\\hyperscanning_BCI\\LSLanalysis'],
             binaries=[],
             datas=[],
=======
a = Analysis(['main_GUI.py'],
             pathex=['/Users/inspireadmin/Downloads/RhythmsOfRelating/LSLanalysis'],
             binaries=[('/Users/inspireadmin/miniconda3/lib/python3.7/site-packages/pylsl/liblsl64.dylib','.')],  
             datas=[('/Users/inspireadmin/Downloads/RhythmsOfRelating/LSLanalysis/logging.conf','log'),
('/Users/inspireadmin/Downloads/RhythmsOfRelating/LSLanalysis/log/development.log','log')],
>>>>>>> da3155348a0d509f11b9e00396e0f605ce977405
             hiddenimports=[],
             hookspath=[],
             runtime_hooks=[],
             excludes=[],
             win_no_prefer_redirects=False,
             win_private_assemblies=False,
             cipher=block_cipher,
             noarchive=False)
pyz = PYZ(a.pure, a.zipped_data,
             cipher=block_cipher)
exe = EXE(pyz,
          a.scripts,
          [],
<<<<<<< HEAD
          exclude_binaries=True,
=======
          exclude_binaries=False,
>>>>>>> da3155348a0d509f11b9e00396e0f605ce977405
          name='main_GUI',
          debug=False,
          bootloader_ignore_signals=False,
          strip=False,
          upx=True,
          console=True )
coll = COLLECT(exe,
               a.binaries,
               a.zipfiles,
               a.datas,
               strip=False,
               upx=True,
               upx_exclude=[],
               name='main_GUI')
<<<<<<< HEAD
=======
app = BUNDLE(exe,
         name='myscript.app',
         icon=None,
         bundle_identifier=None)
>>>>>>> da3155348a0d509f11b9e00396e0f605ce977405
