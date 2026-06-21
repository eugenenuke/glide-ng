/*
** THIS SOFTWARE IS SUBJECT TO COPYRIGHT PROTECTION AND IS OFFERED ONLY
** PURSUANT TO THE 3DFX GLIDE GENERAL PUBLIC LICENSE. THERE IS NO RIGHT
** TO USE THE GLIDE TRADEMARK WITHOUT PRIOR WRITTEN PERMISSION OF 3DFX
** INTERACTIVE, INC. A COPY OF THIS LICENSE MAY BE OBTAINED FROM THE 
** DISTRIBUTOR OR BY CONTACTING 3DFX INTERACTIVE INC(info@3dfx.com). 
** THIS PROGRAM IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND, EITHER 
** EXPRESSED OR IMPLIED. SEE THE 3DFX GLIDE GENERAL PUBLIC LICENSE FOR A
** FULL TEXT OF THE NON-WARRANTY PROVISIONS.  
** 
** USE, DUPLICATION OR DISCLOSURE BY THE GOVERNMENT IS SUBJECT TO
** RESTRICTIONS AS SET FORTH IN SUBDIVISION (C)(1)(II) OF THE RIGHTS IN
** TECHNICAL DATA AND COMPUTER SOFTWARE CLAUSE AT DFARS 252.227-7013,
** AND/OR IN SIMILAR OR SUCCESSOR CLAUSES IN THE FAR, DOD OR NASA FAR
** SUPPLEMENT. UNPUBLISHED RIGHTS RESERVED UNDER THE COPYRIGHT LAWS OF
** THE UNITED STATES.  
** 
** COPYRIGHT 3DFX INTERACTIVE, INC. 1999, ALL RIGHTS RESERVED
*/

#include <3dfx.h>
#include <glide.h>
#include <core/BackendManager.h>
#include <core/GlideSplashAnimator.h>
#include <core/WrapperConfig.h>
#include <core/Logger.h>

/*
** Banshee/Voodoo3 Callback-based Splash Animation Entry Point
*/
extern "C" void FX_CALL
grSplashCb(float x, float y, float w, float h, FxU32 frame, void (*callback)(int frame))
{
    if (!GlideWrapper::EmulationRegistry::GetInstance().IsFunctionAvailable("grSplashCb")) {
        GLIDE_LOG(WARN, "API", "GATED CALL: grSplashCb is not available under the active version/device profile.");
        return;
    }

    auto* animator = GlideWrapper::BackendManager::GetInstance().GetSplashAnimator();
    if (animator) {
        animator->Render(x, y, w, h, frame, callback);
    }
}

/*
** Modernized grSplash Entry Point (Baseline Glide 2.1 / 2.40)
*/
extern "C" void FX_CALL
grSplash(float x, float y, float w, float h, FxU32 frame)
{
    auto* animator = GlideWrapper::BackendManager::GetInstance().GetSplashAnimator();
    if (animator) {
        animator->Render(x, y, w, h, frame, nullptr);
    }
}
