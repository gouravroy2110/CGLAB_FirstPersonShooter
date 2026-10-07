// =============================================================================
//  gunbuild.h  -  Model registry for every weapon
//
//  The original four guns are hand-authored in gunmodels.h. The rest of the
//  arsenal is kit-bashed at start-up from boxes and polygons in the same
//  side-profile format, with hands borrowed from the matching archetype.
// =============================================================================
#pragma once
#include "gunmodels.h"
#include "weapons.h"

// Where attachments mount on a model (gun units)
struct GunMounts {
    float railU0, railU1, railH;     // top rail span + height (optics)
    float underU, underH;            // under-barrel mount (foregrip / laser)
};

void buildGunModels();                       // once at start-up
const GunModel  &gunModel(int w);
const GunGeom3D &gunGeom(int w);
const GunMounts &gunMounts(int w);
