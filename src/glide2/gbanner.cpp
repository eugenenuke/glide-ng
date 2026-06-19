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
#include <glidesys.h>
#include <glide.h>
#include <core/BackendManager.h>
#include <core/EmbeddedAssets.h>

/* display the translucent 3Dfx powerfield logo */
extern "C" void
grShamelessPlug( void )
{
  GrState state;
  GrLfbInfo_t info;

  grGlideGetState( &state );
  grDisableAllEffects();

  grAlphaCombine( GR_COMBINE_FUNCTION_SCALE_OTHER,
                 GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_CONSTANT,
                 GR_COMBINE_OTHER_TEXTURE, FXFALSE );
  grColorCombine( GR_COMBINE_FUNCTION_SCALE_OTHER,
                 GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE,
                 GR_COMBINE_OTHER_TEXTURE,
                 FXFALSE );
  grAlphaBlendFunction( GR_BLEND_SRC_ALPHA,
                       GR_BLEND_ONE_MINUS_SRC_ALPHA,
                       GR_BLEND_ZERO, GR_BLEND_ZERO );

  uint32_t scrWidth = 640;
  uint32_t scrHeight = 480;
  if (auto* b = GlideWrapper::BackendManager::GetInstance().GetBackend()) {
      scrWidth = b->GetWidth();
      scrHeight = b->GetHeight();
  }

  constexpr int banner_width = 180;
  constexpr int banner_height = 90;
  const auto* banner_data = reinterpret_cast<const unsigned short*>(GlideWrapper::Assets::banner_raw);

  grClipWindow( 0, 0,
               static_cast<FxI32>(scrWidth - 1),
               static_cast<FxI32>(scrHeight - 1) ); 
  grDepthMask(FXFALSE);
  grDepthBufferFunction(GR_CMP_ALWAYS);
  grDepthBufferMode( GR_DEPTHBUFFER_DISABLE );
  
  grChromakeyValue( 0x0000 );
  grChromakeyMode( GR_CHROMAKEY_ENABLE );
  grLfbConstantAlpha( static_cast<FxU8>(90) );
  grLfbWriteColorFormat(GR_COLORFORMAT_ARGB);

  /* Attempt to lock with linear frame buffer pixel pipeline enabled */
  info.size = sizeof( info );
  if ( grLfbLock( GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                  GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, 
                  FXTRUE, &info ) ) {
      /* Draw Banner in lower right of screen */
      if ( scrWidth  < static_cast<FxU32>(banner_width)  ) return;
      if ( scrHeight < static_cast<FxU32>(banner_height) ) return;

      auto* dstData = static_cast<FxU32*>(info.lfbPtr);
      
      // Calculate destination address with proper C++ pointer arithmetic
      dstData = reinterpret_cast<FxU32*>(
          reinterpret_cast<char*>(dstData) + 
          (info.strideInBytes * ((scrHeight - 1) - banner_height)) +
          ((scrWidth - banner_width) << 1)
      );
      
      FxU32 dstJump = ((info.strideInBytes >> 1) - banner_width) >> 1;
      const auto* srcData = reinterpret_cast<const FxU32*>(&banner_data[banner_width * (banner_height - 1)]);
      FxU32 srcScanlineLength = banner_width >> 1;
      FxI32 srcJump = -banner_width;

      for( FxU32 scanline = 0; scanline < static_cast<FxU32>(banner_height); scanline++ ) {
          const FxU32* end = srcData + srcScanlineLength;
          while( srcData < end ) {
              *dstData++ = *srcData++;
          }
          dstData += dstJump;
          srcData += srcJump;
      }

      grLfbUnlock( GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER );
  }
  grGlideSetState( &state );
}
