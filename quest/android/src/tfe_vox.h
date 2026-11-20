#pragma once
// Voxel replacements for sprites (Dark Forces Voxel Pack), drawn by the GPU model renderer.
#include <TFE_System/types.h>
#include <vector>
struct JediModel;
struct SecObject;
struct RWall;
#define MFLAG_VOXEL_MODELS 1
void vox_prepareLevel();                                   // map the loaded sprites to voxel models (before the GPU model upload)
const std::vector<JediModel*>& vox_models();               // every voxel model built so far
JediModel* vox_objectModel(SecObject* obj, s32 transform[9]);   // model for this sprite object now, or null
JediModel* vox_wallModel(RWall* wall, s32 transform[9], f32 pos[3], JBool* fullbright);   // voxel wall piece for this sign, or null
