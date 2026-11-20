#pragma once
//////////////////////////////////////////////////////////////////////
// Quest port: in-game configuration in the original Dark Forces
// "CONFIGURATION" art (MENU.LFD), with game, VR and controls pages.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>

namespace TFE_DarkForces
{
	void  vrMenu_open();
	JBool vrMenu_isOpen();
	JBool vrMenu_isFrameless();
	// Runs one frame (input + draw). Returns JTRUE when the TFE system menu was requested on close.
	JBool vrMenu_update();
	// VR: loads the menu room textures from the game files (once).
	void  vrMenu_roomTextures();
	void  vrMenu_pdaBezel();
}
