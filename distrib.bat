@mkdir .build
@cd .build

@call setenv /x86

cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DFARAPI18=1 ..
nmake -nologo distrib
@if errorlevel 1 goto error
@copy *.7z ..
@rm -r *

@call setenv /x64

cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DFARAPI18=1 ..
nmake -nologo distrib
@if errorlevel 1 goto error
@copy *.7z ..
@rm -r *

@goto end

:error
@echo TERMINATED WITH ERRORS

:end

@cd ..
@rm -r .build