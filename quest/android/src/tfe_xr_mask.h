#pragma once
// Gas mask seen from inside (tools/gasmask/gasmask.py -> files/gasmask.bin): drawn last in each eye,
// locked to the head, while the player wears the mask. vp / model are column major; light 0..1.25;
// fade 0..1 (putting it on / taking it off); breath 0..1 (exhale fog on the lens).
void mask_draw(const float vp[16], const float model[16], float light, float fade, float breath, int eyeW, int eyeH);
