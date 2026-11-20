/*
 * glide-ng Clean-Room Open-Source DOS/32 Glide 2.x Overlay (GLIDE2X.OVL)
 *
 * Drop-in GPL-3.0-or-later replacement for the DOSBox-SVN / DOSBox-X Glide
 * pass-through overlay. Exports all 130 DOSBox Glide 2.x bridge symbols
 * (including _GUENCODERLE16@16, _GUENDIANSWAPBYTES@4, _GUENDIANSWAPWORDS@4,
 * and _GUTEXCREATECOLORMIPMAP@0 omitted by legacy third-party overlays).
 */

#include <stdint.h>

#define GLIDE_PORT_DEFAULT 0x600u
#define GLIDE_ACTIVATE_CMD 0xFFu
#define GLIDE_LFB_MAGIC    0xFFFFF1FBu

/* --- Freestanding x86 port I/O & DPMI helpers (zero CRT dependencies) --- */

static void ovl_outb(uint16_t port, uint8_t val);
#pragma aux ovl_outb = \
    "out dx, al"       \
    parm [dx] [al]     \
    modify exact [];

static uint8_t ovl_inb(uint16_t port);
#pragma aux ovl_inb = \
    "in al, dx"       \
    parm [dx]         \
    value [al]        \
    modify exact [al];

/* DPMI 0.9+ INT 31h AX=0800h: Physical Address Mapping -> linear address. */
static uint32_t ovl_dpmi_map_phys(uint32_t phys_addr, uint32_t byte_size);
#pragma aux ovl_dpmi_map_phys = \
    "mov eax, 0800h"            \
    "mov ebx, edx"              \
    "shr ebx, 16"               \
    "mov cx, dx"                \
    "mov esi, edi"              \
    "shr esi, 16"               \
    "int 31h"                   \
    "jc fail"                   \
    "shl ebx, 16"               \
    "mov bx, cx"                \
    "mov eax, ebx"              \
    "jmp done"                  \
    "fail:"                     \
    "xor eax, eax"              \
    "done:"                     \
    parm [edx] [edi]            \
    value [eax]                 \
    modify exact [eax ebx ecx esi];

/* DPMI 1.0 INT 31h AX=0801h: Free Physical Address Mapping (no-op on 0.9). */
static void ovl_dpmi_unmap_phys(uint32_t lin_addr);
#pragma aux ovl_dpmi_unmap_phys = \
    "mov eax, 0801h"              \
    "mov ebx, edx"                \
    "shr ebx, 16"                 \
    "mov cx, dx"                  \
    "int 31h"                     \
    parm [edx]                    \
    modify exact [eax ebx ecx];

/* --- Shared DOSBox bridge state (80-byte real-mode parameter block) --- */

static uint16_t g_port = GLIDE_PORT_DEFAULT;
static volatile uint32_t *g_shared = (volatile uint32_t *)0;
static uint32_t g_lfb_linear = 0u;

static void ovl_ensure_init(void) {
  uint8_t lo, hi;
  uint16_t seg;
  if (g_shared) return;
  ovl_outb(g_port, GLIDE_ACTIVATE_CMD);
  lo = ovl_inb(g_port);
  hi = ovl_inb(g_port);
  seg = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
  if (seg != 0u) {
    g_shared = (volatile uint32_t *)(uintptr_t)((uint32_t)seg << 4);
  }
}

/* --- Generic DOSBox I/O bridge dispatchers (0..16 uint32_t arguments) --- */

static void ovl_void0(uint8_t id) {
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  ovl_outb(g_port, id);
}

static void ovl_void1(uint8_t id, uint32_t a1) {
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  g_shared[1] = a1;
  ovl_outb(g_port, id);
}

static void ovl_void2(uint8_t id, uint32_t a1, uint32_t a2) {
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  g_shared[1] = a1;
  g_shared[2] = a2;
  ovl_outb(g_port, id);
}

static void ovl_void3(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3) {
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  ovl_outb(g_port, id);
}

static void ovl_void4(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                      uint32_t a4) {
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  ovl_outb(g_port, id);
}

static void ovl_void5(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                      uint32_t a4, uint32_t a5) {
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  g_shared[5] = a5;
  ovl_outb(g_port, id);
}

static void ovl_void6(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                      uint32_t a4, uint32_t a5, uint32_t a6) {
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  g_shared[5] = a5;
  g_shared[6] = a6;
  ovl_outb(g_port, id);
}

static void ovl_void7(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                      uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  g_shared[5] = a5;
  g_shared[6] = a6;
  g_shared[7] = a7;
  ovl_outb(g_port, id);
}

static void ovl_void8(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                      uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                      uint32_t a8) {
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  g_shared[5] = a5;
  g_shared[6] = a6;
  g_shared[7] = a7;
  g_shared[8] = a8;
  ovl_outb(g_port, id);
}

static void ovl_void10(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                       uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                       uint32_t a8, uint32_t a9, uint32_t a10) {
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  g_shared[5] = a5;
  g_shared[6] = a6;
  g_shared[7] = a7;
  g_shared[8] = a8;
  g_shared[9] = a9;
  g_shared[10] = a10;
  ovl_outb(g_port, id);
}

static uint32_t ovl_ret0(uint8_t id) {
  volatile uint32_t ret = 0u;
  ovl_ensure_init();
  if (!g_shared) return 0u;
  g_shared[0] = (uint32_t)(uintptr_t)&ret;
  ovl_outb(g_port, id);
  (void)ovl_inb(g_port);
  return ret;
}

static uint32_t ovl_ret1(uint8_t id, uint32_t a1) {
  volatile uint32_t ret = 0u;
  ovl_ensure_init();
  if (!g_shared) return 0u;
  g_shared[0] = (uint32_t)(uintptr_t)&ret;
  g_shared[1] = a1;
  ovl_outb(g_port, id);
  (void)ovl_inb(g_port);
  return ret;
}

static uint32_t ovl_ret2(uint8_t id, uint32_t a1, uint32_t a2) {
  volatile uint32_t ret = 0u;
  ovl_ensure_init();
  if (!g_shared) return 0u;
  g_shared[0] = (uint32_t)(uintptr_t)&ret;
  g_shared[1] = a1;
  g_shared[2] = a2;
  ovl_outb(g_port, id);
  (void)ovl_inb(g_port);
  return ret;
}

static uint32_t ovl_ret4(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                         uint32_t a4) {
  volatile uint32_t ret = 0u;
  ovl_ensure_init();
  if (!g_shared) return 0u;
  g_shared[0] = (uint32_t)(uintptr_t)&ret;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  ovl_outb(g_port, id);
  (void)ovl_inb(g_port);
  return ret;
}

static uint32_t ovl_ret6(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                         uint32_t a4, uint32_t a5, uint32_t a6) {
  volatile uint32_t ret = 0u;
  ovl_ensure_init();
  if (!g_shared) return 0u;
  g_shared[0] = (uint32_t)(uintptr_t)&ret;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  g_shared[5] = a5;
  g_shared[6] = a6;
  ovl_outb(g_port, id);
  (void)ovl_inb(g_port);
  return ret;
}

static uint32_t ovl_ret7(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                         uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
  volatile uint32_t ret = 0u;
  ovl_ensure_init();
  if (!g_shared) return 0u;
  g_shared[0] = (uint32_t)(uintptr_t)&ret;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  g_shared[5] = a5;
  g_shared[6] = a6;
  g_shared[7] = a7;
  ovl_outb(g_port, id);
  (void)ovl_inb(g_port);
  return ret;
}

static uint32_t ovl_ret8(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                         uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                         uint32_t a8) {
  volatile uint32_t ret = 0u;
  ovl_ensure_init();
  if (!g_shared) return 0u;
  g_shared[0] = (uint32_t)(uintptr_t)&ret;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  g_shared[5] = a5;
  g_shared[6] = a6;
  g_shared[7] = a7;
  g_shared[8] = a8;
  ovl_outb(g_port, id);
  (void)ovl_inb(g_port);
  return ret;
}

static uint32_t ovl_ret12(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                          uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                          uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11,
                          uint32_t a12) {
  volatile uint32_t ret = 0u;
  ovl_ensure_init();
  if (!g_shared) return 0u;
  g_shared[0] = (uint32_t)(uintptr_t)&ret;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  g_shared[5] = a5;
  g_shared[6] = a6;
  g_shared[7] = a7;
  g_shared[8] = a8;
  g_shared[9] = a9;
  g_shared[10] = a10;
  g_shared[11] = a11;
  g_shared[12] = a12;
  ovl_outb(g_port, id);
  (void)ovl_inb(g_port);
  return ret;
}

static uint32_t ovl_ret15(uint8_t id, uint32_t a1, uint32_t a2, uint32_t a3,
                          uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                          uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11,
                          uint32_t a12, uint32_t a13, uint32_t a14,
                          uint32_t a15) {
  volatile uint32_t ret = 0u;
  ovl_ensure_init();
  if (!g_shared) return 0u;
  g_shared[0] = (uint32_t)(uintptr_t)&ret;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  g_shared[5] = a5;
  g_shared[6] = a6;
  g_shared[7] = a7;
  g_shared[8] = a8;
  g_shared[9] = a9;
  g_shared[10] = a10;
  g_shared[11] = a11;
  g_shared[12] = a12;
  g_shared[13] = a13;
  g_shared[14] = a14;
  g_shared[15] = a15;
  ovl_outb(g_port, id);
  (void)ovl_inb(g_port);
  return ret;
}

static void ovl_res_dims(uint32_t res, uint32_t *w, uint32_t *h) {
  static const uint16_t kWidths[16] = {
      320, 320, 400, 512, 640, 640, 640, 640,
      800, 960, 856, 512, 1024, 1280, 1600, 400};
  static const uint16_t kHeights[16] = {
      200, 240, 256, 384, 200, 350, 400, 480,
      600, 720, 480, 256, 768, 1024, 1200, 300};
  if (res < 16u) {
    *w = kWidths[res];
    *h = kHeights[res];
  } else {
    *w = 640u;
    *h = 480u;
  }
}

/* ========================================================================= */
/* Exported Glide 2.x Functions (DOSBox I/O Bridge IDs 0..129)               */
/* ========================================================================= */

#define OVL_API __stdcall

/* Minimal freestanding LE DLL entry point (far return EAX=0 for CauseWay). */
#pragma aux ovl_dll_start "__DLLstart_"
uint32_t __far ovl_dll_start(void) { return 0u; }

void OVL_API grAADrawLine(uint32_t a1, uint32_t a2) { ovl_void2(0u, a1, a2); }
void OVL_API grAADrawPoint(uint32_t a1) { ovl_void1(1u, a1); }
void OVL_API grAADrawPolygon(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(2u, a1, a2, a3); }
void OVL_API grAADrawPolygonVertexList(uint32_t a1, uint32_t a2) { ovl_void2(3u, a1, a2); }
void OVL_API grAADrawTriangle(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) { ovl_void6(4u, a1, a2, a3, a4, a5, a6); }
void OVL_API grAlphaBlendFunction(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) { ovl_void4(5u, a1, a2, a3, a4); }
void OVL_API grAlphaCombine(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) { ovl_void5(6u, a1, a2, a3, a4, a5); }
void OVL_API grAlphaControlsITRGBLighting(uint32_t a1) { ovl_void1(7u, a1); }
void OVL_API grAlphaTestFunction(uint32_t a1) { ovl_void1(8u, a1); }
void OVL_API grAlphaTestReferenceValue(uint32_t a1) { ovl_void1(9u, a1); }
void OVL_API grBufferClear(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(10u, a1, a2, a3); }
uint32_t OVL_API grBufferNumPending(void) { return ovl_ret0(11u); }
void OVL_API grBufferSwap(uint32_t a1) { ovl_void1(12u, a1); }
void OVL_API grCheckForRoom(uint32_t a1) { ovl_void1(13u, a1); }
void OVL_API grChromakeyMode(uint32_t a1) { ovl_void1(14u, a1); }
void OVL_API grChromakeyValue(uint32_t a1) { ovl_void1(15u, a1); }
void OVL_API grClipWindow(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) { ovl_void4(16u, a1, a2, a3, a4); }
void OVL_API grColorCombine(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) { ovl_void5(17u, a1, a2, a3, a4, a5); }
void OVL_API grColorMask(uint32_t a1, uint32_t a2) { ovl_void2(18u, a1, a2); }
void OVL_API grConstantColorValue4(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) { ovl_void4(19u, a1, a2, a3, a4); }
void OVL_API grConstantColorValue(uint32_t a1) { ovl_void1(20u, a1); }
void OVL_API grCullMode(uint32_t a1) { ovl_void1(21u, a1); }
void OVL_API grDepthBiasLevel(uint32_t a1) { ovl_void1(22u, a1); }
void OVL_API grDepthBufferFunction(uint32_t a1) { ovl_void1(23u, a1); }
void OVL_API grDepthBufferMode(uint32_t a1) { ovl_void1(24u, a1); }
void OVL_API grDepthMask(uint32_t a1) { ovl_void1(25u, a1); }
void OVL_API grDisableAllEffects(void) { ovl_void0(26u); }
void OVL_API grDitherMode(uint32_t a1) { ovl_void1(27u, a1); }
void OVL_API grDrawLine(uint32_t a1, uint32_t a2) { ovl_void2(28u, a1, a2); }
void OVL_API grDrawPlanarPolygon(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(29u, a1, a2, a3); }
void OVL_API grDrawPlanarPolygonVertexList(uint32_t a1, uint32_t a2) { ovl_void2(30u, a1, a2); }
void OVL_API grDrawPoint(uint32_t a1) { ovl_void1(31u, a1); }
void OVL_API grDrawPolygon(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(32u, a1, a2, a3); }
void OVL_API grDrawPolygonVertexList(uint32_t a1, uint32_t a2) { ovl_void2(33u, a1, a2); }
void OVL_API grDrawTriangle(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(34u, a1, a2, a3); }
void OVL_API grErrorSetCallback(uint32_t a1) { (void)a1; }
void OVL_API grFogColorValue(uint32_t a1) { ovl_void1(36u, a1); }
void OVL_API grFogMode(uint32_t a1) { ovl_void1(37u, a1); }
void OVL_API grFogTable(uint32_t a1) { ovl_void1(38u, a1); }
void OVL_API grGammaCorrectionValue(uint32_t a1) { ovl_void1(39u, a1); }
void OVL_API grGlideGetState(uint32_t a1) { ovl_void1(40u, a1); }
void OVL_API grGlideGetVersion(uint32_t a1) { ovl_void1(41u, a1); }

/* ID 42: grGlideInit negotiates physical LFB address & maps it via DPMI. */
void OVL_API grGlideInit(void) {
  volatile uint32_t lfb_phys = GLIDE_LFB_MAGIC;
  volatile uint32_t lfb_pages = 0u;
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  g_shared[1] = (uint32_t)(uintptr_t)&lfb_phys;
  g_shared[2] = (uint32_t)(uintptr_t)&lfb_pages;
  ovl_outb(g_port, 42u);
  if (lfb_phys != GLIDE_LFB_MAGIC && lfb_pages != 0u && g_lfb_linear == 0u) {
    g_lfb_linear = ovl_dpmi_map_phys(lfb_phys, lfb_pages << 12);
  }
}

void OVL_API grGlideSetState(uint32_t a1) { ovl_void1(43u, a1); }
void OVL_API grGlideShamelessPlug(uint32_t a1) { ovl_void1(44u, a1); }

/* ID 45: grGlideShutdown unmaps DPMI LFB if active. */
void OVL_API grGlideShutdown(void) {
  ovl_void0(45u);
  if (g_lfb_linear != 0u) {
    ovl_dpmi_unmap_phys(g_lfb_linear);
    g_lfb_linear = 0u;
  }
}

void OVL_API grHints(uint32_t a1, uint32_t a2) { ovl_void2(46u, a1, a2); }
void OVL_API grLfbConstantAlpha(uint32_t a1) { ovl_void1(47u, a1); }
void OVL_API grLfbConstantDepth(uint32_t a1) { ovl_void1(48u, a1); }
uint32_t OVL_API grLfbLock(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) { return ovl_ret6(49u, a1, a2, a3, a4, a5, a6); }
uint32_t OVL_API grLfbReadRegion(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) { return ovl_ret7(50u, a1, a2, a3, a4, a5, a6, a7); }
uint32_t OVL_API grLfbUnlock(uint32_t a1, uint32_t a2) { return ovl_ret2(51u, a1, a2); }
void OVL_API grLfbWriteColorFormat(uint32_t a1) { ovl_void1(52u, a1); }
void OVL_API grLfbWriteColorSwizzle(uint32_t a1, uint32_t a2) { ovl_void2(53u, a1, a2); }
uint32_t OVL_API grLfbWriteRegion(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8) { return ovl_ret8(54u, a1, a2, a3, a4, a5, a6, a7, a8); }
void OVL_API grRenderBuffer(uint32_t a1) { ovl_void1(55u, a1); }
void OVL_API grResetTriStats(void) { ovl_void0(56u); }
void OVL_API grSplash(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) { ovl_void5(57u, a1, a2, a3, a4, a5); }
void OVL_API grSstConfigPipeline(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(58u, a1, a2, a3); }
uint32_t OVL_API grSstControl(uint32_t a1) { return ovl_ret1(59u, a1); }
void OVL_API grSstIdle(void) { ovl_void0(60u); }
uint32_t OVL_API grSstIsBusy(void) { return ovl_ret0(61u); }
void OVL_API grSstOrigin(uint32_t a1) { ovl_void1(62u, a1); }
void OVL_API grSstPerfStats(uint32_t a1) { ovl_void1(63u, a1); }
uint32_t OVL_API grSstQueryBoards(uint32_t a1) { return ovl_ret1(64u, a1); }
uint32_t OVL_API grSstQueryHardware(uint32_t a1) { return ovl_ret1(65u, a1); }
void OVL_API grSstResetPerfStats(void) { ovl_void0(66u); }
uint32_t OVL_API grSstScreenHeight(void) { return ovl_ret0(67u); }
uint32_t OVL_API grSstScreenWidth(void) { return ovl_ret0(68u); }
void OVL_API grSstSelect(uint32_t a1) { ovl_void1(69u, a1); }
uint32_t OVL_API grSstStatus(void) { return ovl_ret0(70u); }
uint32_t OVL_API grSstVRetraceOn(void) { return ovl_ret0(71u); }
void OVL_API grSstVidMode(uint32_t a1, uint32_t a2) { ovl_void2(72u, a1, a2); }
uint32_t OVL_API grSstVideoLine(void) { return ovl_ret0(73u); }
void OVL_API grSstWinClose(void) { ovl_void0(74u); }

/* ID 75: grSstWinOpen passes resolved width, height, and &g_lfb_linear. */
uint32_t OVL_API grSstWinOpen(uint32_t hwnd, uint32_t res, uint32_t refresh,
                              uint32_t cformat, uint32_t origin, uint32_t nbuf,
                              uint32_t naux) {
  volatile uint32_t ret = 0u;
  uint32_t w = 640u, h = 480u;
  ovl_ensure_init();
  if (!g_shared) return 0u;
  ovl_res_dims(res, &w, &h);
  g_shared[0] = (uint32_t)(uintptr_t)&ret;
  g_shared[1] = hwnd;
  g_shared[2] = res;
  g_shared[3] = refresh;
  g_shared[4] = cformat;
  g_shared[5] = origin;
  g_shared[6] = nbuf;
  g_shared[7] = naux;
  g_shared[8] = w;
  g_shared[9] = h;
  g_shared[10] = (uint32_t)(uintptr_t)&g_lfb_linear;
  ovl_outb(g_port, 75u);
  (void)ovl_inb(g_port);
  return ret;
}

uint32_t OVL_API grTexCalcMemRequired(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) { return ovl_ret4(76u, a1, a2, a3, a4); }
void OVL_API grTexClampMode(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(77u, a1, a2, a3); }
void OVL_API grTexCombine(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) { ovl_void7(78u, a1, a2, a3, a4, a5, a6, a7); }
void OVL_API grTexCombineFunction(uint32_t a1, uint32_t a2) { ovl_void2(79u, a1, a2); }
void OVL_API grTexDetailControl(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) { ovl_void4(80u, a1, a2, a3, a4); }
void OVL_API grTexDownloadMipMap(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) { ovl_void4(81u, a1, a2, a3, a4); }
void OVL_API grTexDownloadMipMapLevel(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8) { ovl_void8(82u, a1, a2, a3, a4, a5, a6, a7, a8); }
void OVL_API grTexDownloadMipMapLevelPartial(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10) { ovl_void10(83u, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10); }
void OVL_API grTexDownloadTable(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(84u, a1, a2, a3); }
void OVL_API grTexDownloadTablePartial(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) { ovl_void5(85u, a1, a2, a3, a4, a5); }
void OVL_API grTexFilterMode(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(86u, a1, a2, a3); }
void OVL_API grTexLodBiasValue(uint32_t a1, uint32_t a2) { ovl_void2(87u, a1, a2); }
uint32_t OVL_API grTexMaxAddress(uint32_t a1) { return ovl_ret1(88u, a1); }
uint32_t OVL_API grTexMinAddress(uint32_t a1) { return ovl_ret1(89u, a1); }
void OVL_API grTexMipMapMode(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(90u, a1, a2, a3); }
void OVL_API grTexMultibase(uint32_t a1, uint32_t a2) { ovl_void2(91u, a1, a2); }
void OVL_API grTexMultibaseAddress(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) { ovl_void5(92u, a1, a2, a3, a4, a5); }
void OVL_API grTexNCCTable(uint32_t a1, uint32_t a2) { ovl_void2(93u, a1, a2); }
void OVL_API grTexSource(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) { ovl_void4(94u, a1, a2, a3, a4); }
uint32_t OVL_API grTexTextureMemRequired(uint32_t a1, uint32_t a2) { return ovl_ret2(95u, a1, a2); }
void OVL_API grTriStats(uint32_t a1, uint32_t a2) { ovl_void2(96u, a1, a2); }
uint32_t OVL_API gu3dfGetInfo(uint32_t a1, uint32_t a2) { return ovl_ret2(97u, a1, a2); }
uint32_t OVL_API gu3dfLoad(uint32_t a1, uint32_t a2) { return ovl_ret2(98u, a1, a2); }
void OVL_API guAADrawTriangleWithClip(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(99u, a1, a2, a3); }
void OVL_API guAlphaSource(uint32_t a1) { ovl_void1(100u, a1); }
void OVL_API guColorCombineFunction(uint32_t a1) { ovl_void1(101u, a1); }
void OVL_API guDrawPolygonVertexListWithClip(uint32_t a1, uint32_t a2) { ovl_void2(102u, a1, a2); }
void OVL_API guDrawTriangleWithClip(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(103u, a1, a2, a3); }

/* ID 104: Pure-CPU 16-bit RLE encoder (missing from legacy DOSBox OVL). */
int32_t OVL_API guEncodeRLE16(void *dst, const void *src, uint32_t width,
                              uint32_t height) {
  const uint16_t *sp = (const uint16_t *)src;
  uint32_t *dp = (uint32_t *)dst;
  uint32_t remaining = width * height;
  int32_t bytes = 0;
  if (!sp || remaining == 0u) return 0;
  while (remaining > 0u) {
    uint16_t color = *sp;
    uint16_t run = 1u;
    while ((uint32_t)run < remaining && run < 0xFFFFu && sp[run] == color) {
      ++run;
    }
    if (dp) {
      *dp++ = ((uint32_t)run << 16) | (uint32_t)color;
    }
    bytes += 4;
    sp += run;
    remaining -= (uint32_t)run;
  }
  return bytes;
}

/* ID 105: Pure-CPU 32-bit byte swap (missing from legacy DOSBox OVL). */
uint32_t OVL_API guEndianSwapBytes(uint32_t v) {
  return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
         ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

/* ID 106: Pure-CPU 16-bit word swap (missing from legacy DOSBox OVL). */
uint32_t OVL_API guEndianSwapWords(uint32_t v) {
  return (v << 16) | (v >> 16);
}

void OVL_API guFogGenerateExp2(uint32_t a1, uint32_t a2) { ovl_void2(107u, a1, a2); }
void OVL_API guFogGenerateExp(uint32_t a1, uint32_t a2) { ovl_void2(108u, a1, a2); }
void OVL_API guFogGenerateLinear(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(109u, a1, a2, a3); }

/* ID 110: Returns IEEE-754 float in x87 ST(0). */
float OVL_API guFogTableIndexToW(uint32_t a1) {
  union {
    uint32_t u;
    float f;
  } conv;
  conv.u = ovl_ret1(110u, a1);
  return conv.f;
}

void OVL_API guMPDrawTriangle(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(111u, a1, a2, a3); }
void OVL_API guMPInit(void) { ovl_void0(112u); }
void OVL_API guMPTexCombineFunction(uint32_t a1) { ovl_void1(113u, a1); }
void OVL_API guMPTexSource(uint32_t a1, uint32_t a2) { ovl_void2(114u, a1, a2); }
void OVL_API guMovieSetName(uint32_t a1) { ovl_void1(115u, a1); }
void OVL_API guMovieStart(void) { ovl_void0(116u); }
void OVL_API guMovieStop(void) { ovl_void0(117u); }

uint32_t OVL_API guTexAllocateMemory(uint32_t a1, uint32_t a2, uint32_t a3,
                                     uint32_t a4, uint32_t a5, uint32_t a6,
                                     uint32_t a7, uint32_t a8, uint32_t a9,
                                     uint32_t a10, uint32_t a11, uint32_t a12,
                                     uint32_t a13, uint32_t a14, uint32_t a15) {
  return ovl_ret15(118u, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12,
                   a13, a14, a15);
}

uint32_t OVL_API guTexChangeAttributes(uint32_t a1, uint32_t a2, uint32_t a3,
                                       uint32_t a4, uint32_t a5, uint32_t a6,
                                       uint32_t a7, uint32_t a8, uint32_t a9,
                                       uint32_t a10, uint32_t a11,
                                       uint32_t a12) {
  return ovl_ret12(119u, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
}

void OVL_API guTexCombineFunction(uint32_t a1, uint32_t a2) { ovl_void2(120u, a1, a2); }

/* ID 121: Missing from legacy DOSBox OVL; returns NULL mipmap handle. */
uint32_t OVL_API guTexCreateColorMipMap(void) { return 0u; }

void OVL_API guTexDownloadMipMap(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(122u, a1, a2, a3); }
void OVL_API guTexDownloadMipMapLevel(uint32_t a1, uint32_t a2, uint32_t a3) { ovl_void3(123u, a1, a2, a3); }
uint32_t OVL_API guTexGetCurrentMipMap(uint32_t a1) { return ovl_ret1(124u, a1); }
uint32_t OVL_API guTexGetMipMapInfo(uint32_t a1) { return ovl_ret1(125u, a1); }
uint32_t OVL_API guTexMemQueryAvail(uint32_t a1) { return ovl_ret1(126u, a1); }
void OVL_API guTexMemReset(void) { ovl_void0(127u); }
void OVL_API guTexSource(uint32_t a1) { ovl_void1(128u, a1); }

/* Compatibility wrappers for obsolete Glide 2.11 utility calls. */
void OVL_API guFbReadRegion(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            uint32_t dst, uint32_t stride) {
  (void)grLfbReadRegion(0u, x, y, w, h, stride, dst);
}

void OVL_API guFbWriteRegion(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t src, uint32_t stride) {
  (void)grLfbWriteRegion(1u, x, y, 0u, w, h, stride, src);
}

/* ID 129: ConvertAndDownloadRle (16 uint32_t arguments). */
void OVL_API ConvertAndDownloadRle(uint32_t a1, uint32_t a2, uint32_t a3,
                                   uint32_t a4, uint32_t a5, uint32_t a6,
                                   uint32_t a7, uint32_t a8, uint32_t a9,
                                   uint32_t a10, uint32_t a11, uint32_t a12,
                                   uint32_t a13, uint32_t a14, uint32_t a15,
                                   uint32_t a16) {
  ovl_ensure_init();
  if (!g_shared) return;
  g_shared[0] = 0u;
  g_shared[1] = a1;
  g_shared[2] = a2;
  g_shared[3] = a3;
  g_shared[4] = a4;
  g_shared[5] = a5;
  g_shared[6] = a6;
  g_shared[7] = a7;
  g_shared[8] = a8;
  g_shared[9] = a9;
  g_shared[10] = a10;
  g_shared[11] = a11;
  g_shared[12] = a12;
  g_shared[13] = a13;
  g_shared[14] = a14;
  g_shared[15] = a15;
  g_shared[16] = a16;
  ovl_outb(g_port, 129u);
}
