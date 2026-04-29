/* **************************************************
   MSXlib - C programming library for MSX1
   ( port of SGlib from devkitSMS to MSX )
   based on: na_th_an, sverx
   ************************************************** */

/* library initialization. you don't need to call this if you use crt0_msx.rel */
void MSX_init (void);

/* VDP operative mode handling functions */
void MSX_VDPturnOnFeature (unsigned int feature);
void MSX_VDPturnOffFeature (unsigned int feature);
/* feature can be one of the following: */
#define MSX_VDPFEATURE_SHOWDISPLAY      0x0140
#define MSX_VDPFEATURE_FRAMEIRQ         0x0120
#define MSX_VDPFEATURE_USELARGESPRITES  0x0102
#define MSX_VDPFEATURE_ZOOMSPRITES      0x0101

/* handy macros */
#define MSX_displayOn()   MSX_VDPturnOnFeature(MSX_VDPFEATURE_SHOWDISPLAY)
#define MSX_displayOff()  MSX_VDPturnOffFeature(MSX_VDPFEATURE_SHOWDISPLAY)

void MSX_setSpriteMode (unsigned char mode);
#define MSX_SPRITEMODE_NORMAL          0x00
#define MSX_SPRITEMODE_LARGE           0x01
#define MSX_SPRITEMODE_ZOOMED          0x02
#define MSX_SPRITEMODE_LARGE_ZOOMED    0x03

/* TMS9918 palette - identical between SG-1000 and MSX1 */
void MSX_setBackdropColor (unsigned char entry);
#define MSX_COLOR_TRANSPARENT           0x00
#define MSX_COLOR_BLACK                 0x01
#define MSX_COLOR_MEDIUM_GREEN          0x02
#define MSX_COLOR_LIGHT_GREEN           0x03
#define MSX_COLOR_DARK_BLUE             0x04
#define MSX_COLOR_LIGHT_BLUE            0x05
#define MSX_COLOR_DARK_RED              0x06
#define MSX_COLOR_CYAN                  0x07
#define MSX_COLOR_MEDIUM_RED            0x08
#define MSX_COLOR_LIGHT_RED             0x09
#define MSX_COLOR_DARK_YELLOW           0x0A
#define MSX_COLOR_LIGHT_YELLOW          0x0B
#define MSX_COLOR_DARK_GREEN            0x0C
#define MSX_COLOR_MAGENTA               0x0D
#define MSX_COLOR_GRAY                  0x0E
#define MSX_COLOR_WHITE                 0x0F

/* wait until next VBlank starts */
void MSX_waitForVBlank (void);

/* functions to load tiles into VRAM */
void MSX_loadTilePatterns (void *src, unsigned int tilefrom, unsigned int size);
void MSX_loadTileColours (void *src, unsigned int tilefrom, unsigned int size);
void MSX_loadSpritePatterns (void *src, unsigned int tilefrom, unsigned int size);

/* functions for the tilemap */
void MSX_loadTileMapArea (unsigned char x, unsigned char y, void *src, unsigned char width, unsigned char height);
void MSX_setNextTileatXY (unsigned char x, unsigned char y);
void MSX_setTileatXY (unsigned char x, unsigned char y, unsigned char tile);
void MSX_setTile (unsigned char tile);
void MSX_fillTile (unsigned char tile, unsigned int count);

/* functions for sprites handling */
void MSX_initSprites (void);
void MSX_addSprite (unsigned char x, unsigned char y, unsigned char tile, unsigned char attr);
void MSX_addMetaSprite1x1 (unsigned char x, unsigned char y, const unsigned char *mt);
void MSX_addMetaSprite (unsigned char x, unsigned char y, const unsigned char *mt);
void MSX_finalizeSprites (void);
void MSX_copySpritestoSAT (void);
unsigned char *MSX_getStp (void);
void MSX_setStp (unsigned char *s);

/* functions to read joypad(s) - MSX uses PSG GPIO + PPI selector */
unsigned char MSX_getKeysStatus (void);

/* handy defines for joypad(s) handling - same bitfield as SG so engine reads it identically */
#ifndef CONTROLLER_PORTS
#define CONTROLLER_PORTS
#define PORT_A_KEY_UP		0x0001
#define PORT_A_KEY_DOWN 	0x0002
#define PORT_A_KEY_LEFT 	0x0004
#define PORT_A_KEY_RIGHT	0x0008
#define PORT_A_KEY_1		0x0010
#define PORT_A_KEY_2		0x0020
#define PORT_A_KEY_START	PORT_A_KEY_1

#define PORT_B_KEY_UP		0x0040
#define PORT_B_KEY_DOWN 	0x0080
#define PORT_B_KEY_LEFT 	0x0100
#define PORT_B_KEY_RIGHT	0x0200
#define PORT_B_KEY_1		0x0400
#define PORT_B_KEY_2		0x0800
#define PORT_B_KEY_START	PORT_B_KEY_1

#define RESET_KEY_NOT		0x1000        /* unused on MSX */
#define CARTRIDGE_SLOT		0x2000        /* unused on MSX */
#define PORT_A_TH			0x4000        /* unused on MSX */
#define PORT_B_TH			0x8000        /* unused on MSX */
#endif

_Bool MSX_queryPauseRequested (void);          /* true if F5 has been pressed */
void MSX_resetPauseRequest (void);

/* low level functions */
void MSX_VRAMmemset (unsigned int dst, unsigned char value, unsigned int size);

/* VRAM unsafe functions. Fast, but dangerous! */
void MSX_copySpritestoSAT (void);
void MSX_VRAMmemcpy128 (unsigned int dst, void *src);

/* the Interrupt Service Routine (do not modify) */
void MSX_isr (void) __interrupt;
/* MSX has no NMI button - kept for API parity, no-op */
void MSX_nmi_isr (void) __critical __interrupt;

/* Handy when loading patterns */
#define PATTERN_DATA_BANK_A 0x0000
#define PATTERN_DATA_BANK_B 0x0800
#define PATTERN_DATA_BANK_C 0x1000
#define PGT_BASE			0x0000
#define CGT_BASE 			0x2000
#define SGT_BASE 			0x3800

/* MSX VDP I/O ports (TMS9918 with MSX wiring) */
__sfr __at 0x99 VDPControlPort;
__sfr __at 0x99 VDPStatusPort;
__sfr __at 0x98 VDPDataPort;

/* MSX AY-3-8910 PSG ports */
__sfr __at 0xA0 PSGAddrPort;
__sfr __at 0xA1 PSGDataPort;
__sfr __at 0xA2 PSGReadPort;

/* MSX PPI 8255 (slots, keyboard, etc.) */
__sfr __at 0xA8 PrimarySlotPort;
__sfr __at 0xA9 PPIPortB;     /* keyboard row read */
__sfr __at 0xAA PPIPortC;     /* keyboard row select + cassette + caps */
__sfr __at 0xAB PPIControl;

/* VRAM map (identical to SG-1000) */
#define PNTADDRESS			0x1800
#define SATADDRESS 			0x1B00
#define PGTADDRESS 			0x0000
#define CGTADDRESS			0x2000
#define SGTADDRESS			0x3800

#define HI(x)				((x)>>8)
#define LO(x)				((x)&0xFF)

void MSX_setUpdateList (unsigned char *ul);
void MSX_doUpdateList (void);

void music_pause (unsigned char p);
