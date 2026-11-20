#pragma once
struct XrQuaternionf{float x,y,z,w;}; struct XrVector3f{float x,y,z;}; struct XrPosef{XrQuaternionf orientation; XrVector3f position;};
struct XrFovf{float angleLeft,angleRight,angleUp,angleDown;};
