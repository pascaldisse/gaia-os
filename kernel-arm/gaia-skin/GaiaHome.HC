//GaiaHome.HC — GAIA-OS skin front page (whip-16x). Rationale/gotchas: see
//kernel-arm/gaia-skin/install.sh. Type() renders this whole file (comments
//included) so keep everything below terse - it IS the boot screen.
/*
$WW+H,1$$PURPLE$$TX+CX,"GAIA-OS"$$FG$

$TX+CX,"love=1 - a Temple that remembers Terry"$

$TX+CX,"TempleOS / Terry Davis, aiwnios, Public Domain"$

$LK,"Help & Index",A="FI:::/Doc/HelpIndex.DD"$, $LK,"Quick Start: Cmd line",A="FI:::/Doc/CmdLineOverview.DD"$, $BK,1$$LK,"Aiwnios Help Guide",A="FI:::/AiwniosHelp/Main.DD"$$BK,0$

$MA,"Oracle: God Word (F7)",LM="FifoU8Flush(god.fifo);GodBitsIns(GOD_GOOD_BITS,KbdMsEvtTime>>GOD_BAD_BITS);GodWord;\n"$   $MA,"God Passage (Shift-F7)",LM="FifoU8Flush(god.fifo);GodBitsIns(GOD_GOOD_BITS,KbdMsEvtTime>>GOD_BAD_BITS);GodBiblePassage;\n"$

$MA,"God Song (F6)",LM="GodSong;\n"$   $MA,"God Doodle (Shift-F6)",LM="GodDoodle;\n"$

$MA,"After Egypt",LM="Cd(\"::/AfterEgypt\");#include \"Run\";\n"$   $MA,"Games & Demos",LM="Cd(\"::/Demo\");Dir;\n"$
*/
U0 GaiaHomeBoot()
{//Proof-of-execution probe (acceptance b).
  U8 *banner="GAIA-OS front page ExeFile'd at boot.\n"
             "love=1 - a Temple that remembers Terry\n"
             "TempleOS / Terry Davis, aiwnios, Public Domain\n";
  FileWrite("::/dbg/probe.txt",banner,StrLen(banner));
}
GaiaHomeBoot();
