#pragma once
// Imperial ship room around the 2D screen (menus, cutscenes). Matrices column major.
// model: room frame -> app space (origin on the floor under the screen, +z toward the player).
void room_draw(const float viewProj[16], const float model[16], const float eye[3], float screenW, float screenH, float screenY, float time);
// Room surfaces use the game's own textures (RGBA rows bottom-up), given by the game once its files
// are mounted. kind: 0 floor, 1 wall, 2 ceiling, 3 light strip, 5 console, 6 button panel, 8 trim, 10 computer bank.
bool room_needsTextures();
void room_setTexture(int kind, const unsigned char* rgba, int w, int h, float metersPerTexel, float offU, float offV);
