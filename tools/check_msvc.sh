#!/bin/sh
# Catches what the owner's Visual Studio build (SDL checks on) rejects but a
# MinGW/GCC build doesn't: the CRT functions MSVC deprecates (C4996, an
# error under /sdl). Scans every source the project compiles, plus headers.
# Usage: tools/check_msvc.sh   (from anywhere; exit 1 on a finding)
cd "$(dirname "$0")/.."
SRCS=$(grep -o 'ClCompile Include="[^"]*"' Cacophony.vcxproj | sed 's/.*="//;s/"//')
BANNED='getenv|_wgetenv|fopen|_wfopen|freopen|_wfreopen|sprintf|vsprintf|strcpy|wcscpy|strcat|wcscat|strncpy|strncat|sscanf|swscanf|fscanf|scanf|strtok|wcstok|localtime|gmtime|ctime|asctime|mbstowcs|wcstombs|_itoa|itoa|_snprintf|_open|_wopen|strerror|tmpnam|strdup|fileno|unlink|getcwd|chdir|stricmp|strnicmp'
HITS=$(grep -nE "(^|[^_A-Za-z0-9])($BANNED)[[:space:]]*\(" $SRCS *.h 2>/dev/null | grep -vE "(snprintf|vsnprintf)[[:space:]]*\(" )
if [ -n "$HITS" ]; then
    echo "MSVC /sdl would reject these (C4996 deprecated CRT calls):"
    echo "$HITS"
    exit 1
fi
echo "ok   no MSVC-deprecated CRT calls"
