!include project.ini

CPPFLAGS = -nologo -Zi -W3 -Gy -GS -GR -EHsc -MP -c
DEFINES = -DWIN32_LEAN_AND_MEAN -D_WINDOWS -D_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1 -D_FAR_USE_FARFINDDATA -D_WIN32_WINNT=0x0500
LINKFLAGS = -nologo -debug -incremental:no -stack:10000000 -map -manifest:no -dynamicbase -nxcompat -largeaddressaware -dll
RCFLAGS = -nologo

!if "$(CPU)" == "AMD64" || "$(PLATFORM)" == "x64"
PLATFORM = x64
DEFINES = $(DEFINES) -DWIN64
!else
PLATFORM = x86
DEFINES = $(DEFINES) -DWIN32
LINKFLAGS = $(LINKFLAGS) -safeseh
!endif

!ifdef RELEASE
OUTDIR = Release
DEFINES = $(DEFINES) -DNDEBUG
CPPFLAGS = $(CPPFLAGS) -O1 -GL -MT
LINKFLAGS = $(LINKFLAGS) -opt:ref -opt:icf -LTCG
!else
OUTDIR = Debug
DEFINES = $(DEFINES) -DDEBUG -D_DEBUG
CPPFLAGS = $(CPPFLAGS) -Od -RTC1 -MTd
LINKFLAGS = $(LINKFLAGS) -fixed:no
!endif
OUTDIR = $(OUTDIR).$(PLATFORM)
INCLUDES = -Ifar -I$(OUTDIR)
CPPFLAGS = $(CPPFLAGS) -Fo$(OUTDIR)\ -Fd$(OUTDIR)\ $(INCLUDES) $(DEFINES)
RCFLAGS = $(RCFLAGS) $(INCLUDES) $(DEFINES)

!ifdef BUILD
!include $(OUTDIR)\far.ini
!endif

OBJS = $(OUTDIR)\farutils.obj $(OUTDIR)\inet.obj $(OUTDIR)\iniparse.obj $(OUTDIR)\options.obj $(OUTDIR)\pathutils.obj $(OUTDIR)\plugin.obj $(OUTDIR)\strutils.obj $(OUTDIR)\sysutils.obj $(OUTDIR)\transact.obj $(OUTDIR)\trayicon.obj $(OUTDIR)\ui.obj $(OUTDIR)\update.obj

LIBS = user32.lib advapi32.lib shell32.lib winhttp.lib msi.lib

project: depfile $(OUTDIR)\far.ini
  $(MAKE) -nologo -$(MAKEFLAGS) build_project BUILD=1

distrib: depfile $(OUTDIR)\far.ini
  $(MAKE) -nologo -$(MAKEFLAGS) build_distrib BUILD=1

installer: depfile $(OUTDIR)\far.ini
  $(MAKE) -nologo -$(MAKEFLAGS) build_installer BUILD=1

build_project: $(OUTDIR)\$(MODULE).dll $(OUTDIR)\en.lng $(OUTDIR)\ru.lng $(OUTDIR)\en.hlf $(OUTDIR)\ru.hlf

$(OUTDIR)\$(MODULE).dll: $(OUTDIR)\plugin.def $(OBJS) $(OUTDIR)\headers.pch $(OUTDIR)\version.res $(OUTDIR)\icon.res project.ini
  link $(LINKFLAGS) -def:$(OUTDIR)\plugin.def -out:$@ $(OBJS) $(OUTDIR)\headers.obj $(OUTDIR)\version.res $(OUTDIR)\icon.res $(LIBS)

$(OBJS): $(OUTDIR)\headers.pch

.cpp{$(OUTDIR)}.obj::
  $(CPP) $(CPPFLAGS) -Yuheaders.hpp -FIheaders.hpp -Fp$(OUTDIR)\headers.pch $<

$(OUTDIR)\headers.pch: headers.cpp headers.hpp
  $(CPP) $(CPPFLAGS) headers.cpp -Ycheaders.hpp -Fp$(OUTDIR)\headers.pch

depfile: $(OUTDIR) $(OUTDIR)\msg.h
  tools\gendep.exe $(INCLUDES) > $(OUTDIR)\dep.mak

$(OUTDIR)\msg.h $(OUTDIR)\en.lng $(OUTDIR)\ru.lng: $(OUTDIR) $(OUTDIR)\en.msg $(OUTDIR)\ru.msg
  tools\msgc -in $(OUTDIR)\en.msg $(OUTDIR)\ru.msg -out $(OUTDIR)\msg.h $(OUTDIR)\en.lng $(OUTDIR)\ru.lng

$(OUTDIR)\version.res: $(OUTDIR)\version.rc
  $(RC) $(RCFLAGS) -fo$@ $**

$(OUTDIR)\icon.res: icon.rc
  $(RC) $(RCFLAGS) -fo$@ $**

icon.rc: update.ico

PREPROC = tools\preproc $** $@

$(OUTDIR)\version.rc: project.ini version.rc
  $(PREPROC)

$(OUTDIR)\plugin.def: project.ini plugin.def
  $(PREPROC)

$(OUTDIR)\en.msg: project.ini en.msg
  $(PREPROC)

$(OUTDIR)\ru.msg: project.ini ru.msg
  $(PREPROC)

$(OUTDIR)\en.hlf: project.ini en.hlf
  $(PREPROC)

$(OUTDIR)\ru.hlf: project.ini ru.hlf
  $(PREPROC)

$(OUTDIR)\far.ini: far\plugin.hpp
  tools\farver $** $@

$(OUTDIR):
  if not exist $(OUTDIR) mkdir $(OUTDIR)

!ifdef BUILD
!include $(OUTDIR)\dep.mak
!endif


DISTRIB = $(OUTDIR)\$(MODULE)_$(VER_MAJOR).$(VER_MINOR).$(VER_PATCH)_uni
DISTRIB_FILES = .\$(OUTDIR)\$(MODULE).dll .\$(OUTDIR)\$(MODULE).map .\$(OUTDIR)\en.lng .\$(OUTDIR)\ru.lng .\$(OUTDIR)\en.hlf .\$(OUTDIR)\ru.hlf
!if "$(PLATFORM)" != "x86"
DISTRIB = $(DISTRIB)_$(PLATFORM)
!endif
!ifndef RELEASE
DISTRIB = $(DISTRIB)_dbg
DISTRIB_FILES = $(DISTRIB_FILES) .\$(OUTDIR)\$(MODULE).pdb
!endif

build_distrib: $(DISTRIB).7z

$(DISTRIB).7z: $(DISTRIB_FILES) project.ini
  7z a -mx=9 $@ $(DISTRIB_FILES)

build_installer: $(DISTRIB).msi

LIGHTFLAGS = -nologo -cultures:en-us -spdb -sval -b installer
CANDLEFLAGS = -nologo -Iinstaller -dSourceDir=$(OUTDIR) -dPlatform=$(PLATFORM) -dProduct=$(NAME) -dVersion=$(VER_MAJOR).$(VER_MINOR).$(VER_PATCH) -dMinFarVersion=$(FAR_VER_MAJOR).$(FAR_VER_MINOR).$(FAR_VER_BUILD)
!ifdef RELEASE
LIGHTFLAGS = $(LIGHTFLAGS) -dcl:high
!else
LIGHTFLAGS = $(LIGHTFLAGS) -sh -cc $(OUTDIR) -reusecab -O2
!endif
WIXOBJ = $(OUTDIR)\installer.wixobj $(OUTDIR)\ui.wixobj $(OUTDIR)\plugin.wixobj

$(DISTRIB).msi: $(DISTRIB_FILES) project.ini $(WIXOBJ) installer\ui_en-us.wxl installer\banner.jpg installer\dialog.jpg installer\exclam.ico installer\info.ico
  light $(LIGHTFLAGS) -loc installer\ui_en-us.wxl -out $@ $(WIXOBJ)

{installer}.wxs{$(OUTDIR)}.wixobj::
  candle $(CANDLEFLAGS) -out $(OUTDIR)\ $<

installer\installer.wxs installer\plugin.wxs: installer\installer.wxi installer\plugin.wxi


clean:
  if exist $(OUTDIR) rd /s /q $(OUTDIR)


.PHONY: project distrib build_project build_distrib build_installer depfile clean
.SUFFIXES: .wxs