// MSX MK1 v0.1 - port of SG-1000 hw layer to MSX
// Copyleft Mojon Twins 2013, 2015, 2017, 2018 / MSX port 2026

// Maps the HW_* abstraction layer used by the engine onto the MSX implementation
// (MSXlib). Mirrors hw_sg1000.h verbatim but redirects to MSX_* primitives.

#define HW_displayOn 						MSX_displayOn
#define HW_displayOff 						MSX_displayOff
#define HW_setSpriteMode 					MSX_setSpriteMode
#define HW_loadTilePatterns					MSX_loadTilePatterns
#define HW_loadTileColours					MSX_loadTileColours
#define HW_loadSpritePatterns				MSX_loadSpritePatterns
#define HW_setTileatXY						MSX_setTileatXY
#define HW_setNextTileatXY					MSX_setNextTileatXY
#define HW_setTile 							MSX_setTile
#define HW_fillTile 						MSX_fillTile
#define HW_loadTileMapArea 					MSX_loadTileMapArea
#define HW_initSprites 						MSX_initSprites
#define HW_addSprite 						MSX_addSprite
#define HW_addMetaSprite1x1 				MSX_addMetaSprite1x1
#define HW_addMetaSprite 					MSX_addMetaSprite
#define HW_finalizeSprites 					MSX_finalizeSprites
#define HW_getStp 							MSX_getStp
#define HW_setStp 							MSX_setStp
#define HW_waitForVBlank 					MSX_waitForVBlank
#define HW_getKeysStatus  					MSX_getKeysStatus
#define HW_queryPauseRequested 				MSX_queryPauseRequested
#define HW_resetPauseRequest 				MSX_resetPauseRequest
#define HW_VRAMmemset 						MSX_VRAMmemset
#define HW_copySpritestoSAT 				MSX_copySpritestoSAT
#define HW_VRAMmemcpy128 					MSX_VRAMmemcpy128
#define HW_setUpdateList 					MSX_setUpdateList
#define HW_doUpdateList						MSX_doUpdateList
#define HW_isr 								MSX_isr
#define HW_nmi_isr 							MSX_nmi_isr

// ---------------------------------------------------------------
// SG_* -> MSX_* aliases for game code that calls into the lib
// directly (constants and a handful of helpers the engine and
// the per-game *.h files reference without going through HW_*).
// TMS9918 palette and VDP features are bit-identical between
// SG-1000 and MSX1, so this is just a name swap.
// ---------------------------------------------------------------

// Sprite mode
#define SG_SPRITEMODE_NORMAL				MSX_SPRITEMODE_NORMAL
#define SG_SPRITEMODE_LARGE					MSX_SPRITEMODE_LARGE
#define SG_SPRITEMODE_ZOOMED				MSX_SPRITEMODE_ZOOMED
#define SG_SPRITEMODE_LARGE_ZOOMED			MSX_SPRITEMODE_LARGE_ZOOMED

// VDP features
#define SG_VDPFEATURE_SHOWDISPLAY			MSX_VDPFEATURE_SHOWDISPLAY
#define SG_VDPFEATURE_FRAMEIRQ				MSX_VDPFEATURE_FRAMEIRQ
#define SG_VDPFEATURE_USELARGESPRITES		MSX_VDPFEATURE_USELARGESPRITES
#define SG_VDPFEATURE_ZOOMSPRITES			MSX_VDPFEATURE_ZOOMSPRITES

// VDP feature helpers
#define SG_VDPturnOnFeature					MSX_VDPturnOnFeature
#define SG_VDPturnOffFeature				MSX_VDPturnOffFeature

// Palette (TMS9918 - identical 16 colours)
#define SG_COLOR_TRANSPARENT				MSX_COLOR_TRANSPARENT
#define SG_COLOR_BLACK						MSX_COLOR_BLACK
#define SG_COLOR_MEDIUM_GREEN				MSX_COLOR_MEDIUM_GREEN
#define SG_COLOR_LIGHT_GREEN				MSX_COLOR_LIGHT_GREEN
#define SG_COLOR_DARK_BLUE					MSX_COLOR_DARK_BLUE
#define SG_COLOR_LIGHT_BLUE					MSX_COLOR_LIGHT_BLUE
#define SG_COLOR_DARK_RED					MSX_COLOR_DARK_RED
#define SG_COLOR_CYAN						MSX_COLOR_CYAN
#define SG_COLOR_MEDIUM_RED					MSX_COLOR_MEDIUM_RED
#define SG_COLOR_LIGHT_RED					MSX_COLOR_LIGHT_RED
#define SG_COLOR_DARK_YELLOW				MSX_COLOR_DARK_YELLOW
#define SG_COLOR_LIGHT_YELLOW				MSX_COLOR_LIGHT_YELLOW
#define SG_COLOR_DARK_GREEN					MSX_COLOR_DARK_GREEN
#define SG_COLOR_MAGENTA					MSX_COLOR_MAGENTA
#define SG_COLOR_GRAY						MSX_COLOR_GRAY
#define SG_COLOR_WHITE						MSX_COLOR_WHITE

// Direct VRAM helpers (used by game code without HW_*)
#define SG_VRAMmemset						MSX_VRAMmemset
#define SG_VRAMmemcpy128					MSX_VRAMmemcpy128

// Some per-game .h files call these full names directly:
#define SG_addSprite						MSX_addSprite
#define SG_addMetaSprite					MSX_addMetaSprite
#define SG_addMetaSprite1x1					MSX_addMetaSprite1x1
#define SG_initSprites						MSX_initSprites
#define SG_finalizeSprites					MSX_finalizeSprites
#define SG_copySpritestoSAT					MSX_copySpritestoSAT
#define SG_getStp							MSX_getStp
#define SG_setStp							MSX_setStp
#define SG_setTile							MSX_setTile
#define SG_setTileatXY						MSX_setTileatXY
#define SG_setNextTileatXY					MSX_setNextTileatXY
#define SG_fillTile							MSX_fillTile
#define SG_loadTilePatterns					MSX_loadTilePatterns
#define SG_loadTileColours					MSX_loadTileColours
#define SG_loadSpritePatterns				MSX_loadSpritePatterns
#define SG_loadTileMapArea					MSX_loadTileMapArea
#define SG_setBackdropColor					MSX_setBackdropColor
#define SG_setSpriteMode					MSX_setSpriteMode
#define SG_displayOn						MSX_displayOn
#define SG_displayOff						MSX_displayOff
#define SG_waitForVBlank					MSX_waitForVBlank
#define SG_getKeysStatus					MSX_getKeysStatus
#define SG_queryPauseRequested				MSX_queryPauseRequested
#define SG_resetPauseRequest				MSX_resetPauseRequest
#define SG_setUpdateList					MSX_setUpdateList
#define SG_doUpdateList						MSX_doUpdateList
