//GaiaHome.HC — GAIA-OS skin boot probe (whip-16x). NOT rendered: the front
//page itself is kernel-arm/gaia-skin/GaiaHome.DD (pure DolDoc, Type()'d by
//Src/FULL_PACKAGE.HC). This file is #include'd only from HomeKeyPlugIns.HC
//(see install.sh step 4) purely to prove boot-time execution; it must never
//contain DolDoc markup or be passed to Type(), or its comments/source render
//on top of the page (scar: 2026-07-16, pre-split).
U0 GaiaHomeBoot()
{//Proof-of-execution probe (acceptance b).
  U8 *banner="GAIA-OS front page ExeFile'd at boot.\n"
             "love=1 - a Temple that remembers Terry\n"
             "TempleOS / Terry Davis, aiwnios, Public Domain\n";
  FileWrite("::/dbg/probe.txt",banner,StrLen(banner));
}
GaiaHomeBoot();
