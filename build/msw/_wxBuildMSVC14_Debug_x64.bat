call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\bin\amd64\vcvars64.bat"

nmake /nologo -f makefile.vc USE_XRC=0 BUILD=debug DEBUG_INFO=default UNICODE=1 WXUNIV=0 MONOLITHIC=1 USE_EXCEPTIONS=1 USE_ODBC=0 USE_OPENGL=0 USE_GUI=1 RUNTIME_LIBS=dynamic SHARED=1 VENDOR= wxUSE_WINSOCK2=1

pause
