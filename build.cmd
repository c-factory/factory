if exist a.exe erase a.exe
gcc *.c ..\collections\src\*.c ..\strings\strings.c ..\numbers\numbers.c ..\files\*.c ..\json\json.c -I.\..\collections\include -I.\.. -g -Werror 
if exist a.exe a.exe