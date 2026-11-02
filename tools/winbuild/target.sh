# Target platform of the single-file plug-in builds (sourced by build2.sh; issue #112).
#   NK_TARGET=windows (default): MinGW-w64, CLAP + VST2 in one PE file (.dll, copied to .clap), Win32 editor,
#                                version resource.
#   NK_TARGET=linux:             native g++, CLAP only (ELF shared object .so; the builder names it .clap),
#                                editor on X11 (the Win32 editor code through src/gui/win32_x11.cpp, needs
#                                libX11 at run time), no VST2 entry point.
#   NK_TARGET=macos:             Apple clang on a Mac, CLAP only (Mach-O of the machine's architecture, .dylib;
#                                the builder puts it into a .clap bundle), editor as an NSView (the Win32 editor
#                                code through src/gui/win32_cocoa.mm), no VST2 entry point.
# Sets: NK_EXT (file extension of the linker output), NK_PLAT_SRC (platform sources), NK_LINK (link options
# before the libraries), NK_STRIP (unused-section removal and stripping), NK_LIBS / NK_LIBS88 (system libraries of the SC-55 and the 88emu builds).
# Functions: nk_res <gen/ver_tag> (compiles gen/ver_tag.rc on Windows, prints the object or nothing),
#            nk_finish <outdir> <file> (Windows: .dll -> .clap copy).
case "${NK_TARGET:-windows}" in
  windows)
    NK_EXT=dll
    NK_PLAT_SRC="$S/src/vst2/vst2_entry.cpp $S/src/gui/editor_win32.cpp"
    NK_LINK="-static -static-libgcc -static-libstdc++"
    NK_LIBS="-lgdi32 -luser32 -lmsimg32"
    NK_LIBS88="-lgdi32 -luser32 -lmsimg32 -lshlwapi -lole32 -luuid -lshell32 -lwinmm"
    NK_STRIP="-Wl,--gc-sections -s" ;;
  linux)
    NK_EXT=so
    # Export only clap_entry; libstdc++/libgcc are linked in, so the plug-in needs nothing but glibc (2.35 or newer,
    # see glibc_compat.c; the object is built without LTO).
    mkdir -p gen && printf '{ global: clap_entry; local: *; };\n' > gen/clap_linux.map
    gcc -O2 -fPIC -c "$(dirname "${BASH_SOURCE[0]}")/glibc_compat.c" -o gen/glibc_compat.o || return 1
    NK_PLAT_SRC="gen/glibc_compat.o $S/src/gui/editor_win32.cpp $S/src/gui/win32_gdi.cpp $S/src/gui/win32_x11.cpp"
    NK_LINK="-DNUKED_SC55_X11_EDITOR -DNK_FONT_DIR=\"$S/third_party/fonts\" -fPIC -pthread -static-libgcc -static-libstdc++ -Wl,--version-script=gen/clap_linux.map -Wl,--no-undefined -Wl,-z,noexecstack"
    NK_LIBS="-lX11 -ldl"
    NK_LIBS88="-lX11 -ldl"
    NK_STRIP="-Wl,--gc-sections -s" ;;
  macos)
    # Export only clap_entry; libc++ is part of macOS. NK_MACOS_MIN: oldest macOS that loads the plug-in.
    NK_EXT=dylib
    NK_PLAT_SRC="$S/src/gui/editor_win32.cpp $S/src/gui/win32_gdi.cpp $S/src/gui/win32_cocoa.mm"
    NK_LINK="-DNUKED_SC55_COCOA_EDITOR -DNK_FONT_DIR=\"$S/third_party/fonts\" -fobjc-arc -mmacosx-version-min=${NK_MACOS_MIN:-11.0} -fPIC -pthread -Wl,-exported_symbol,_clap_entry"
    NK_LIBS="-framework Cocoa"
    NK_LIBS88="-framework Cocoa"
    NK_STRIP="-Wl,-dead_strip -Wl,-x" ;;
  *) echo "unknown NK_TARGET '$NK_TARGET' (windows, linux or macos)" >&2; return 1 ;;
esac
nk_res() {
  [ "${NK_TARGET:-windows}" = windows ] || return 0
  x86_64-w64-mingw32-windres "$1.rc" -O coff -o "$1.o" && echo "$1.o"
}
nk_finish() {
  [ "${NK_TARGET:-windows}" = windows ] && cp "$1/$2.dll" "$1/$2.clap"
  return 0
}
