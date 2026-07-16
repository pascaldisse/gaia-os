//TempleDbg — file-driven debug channel for aiwnios GUI (whip-162 law:
//visual verification = in-app dump, never host screen capture).
//Boot: aiwnios -t <root> TempleDbg.HC  (arg_boot_files, c/main.c:75)
//Channel: drop ::/dbg/cmd.HC → ExeFile'd + deleted (poll 250mS).
//Screen:  DbgScrn(fn) → BMP at ::/dbg/scrn.bmp (FileBMP.HC BMPScrnCapture).
//
//GUI-MODE GOTCHA (proven 2026-07-16): `-t . TempleDbg.HC` on the argv is a
//no-op without `-c`/`--tui` — c/main.c arg_boot_files help text says so
//verbatim ("Files to run on boot in command line mode"), and
//Src/FULL_PACKAGE.HC only drains CmdLineBootFiles inside
//`if(IsCmdLineMode){ Spawn(&CmdLine); }`; the GUI branch never touches it.
//To actually load this file in a GUI instance, hook it into the one path
//FULL_PACKAGE.HC *does* run unconditionally in GUI mode: append to the
//boot dir's ~/HomeKeyPlugIns.HC —
//  if(FileFind("::/TempleDbg.HC")) #include "::/TempleDbg.HC";
//(HomeKeyPlugIns.HC is ExeFile'd every GUI boot regardless of argv.)

#include "::/DolNet/FileBMP"

I64 DbgScrn(U8 *fn="::/dbg/scrn.bmp")
{//Dump current scrn to BMP on mounted host dir.
  return BMPScrnCapture(fn);
}

U0 DbgDaemon()
{
  U8 *cmd="::/dbg/cmd.HC";
  if (!FileFind("::/dbg",,FUF_JUST_DIRS))
    DirMk("::/dbg");
  while (TRUE) {
    if (FileFind(cmd)) {
      ExeFile("::/dbg/cmd");
      Del(cmd);
    }
    Sleep(250);
  }
}

Spawn(&DbgDaemon,NULL,"DbgDaemon");
"TempleDbg: channel up\n";
