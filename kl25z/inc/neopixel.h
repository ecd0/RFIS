/* Neopixel driver code, using PWM and DMA
   adapted from Erich Styger's example code (mcuoneclipse.com)

	 Mar 25 2018 - Eric Davis
	   v1
*/

#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <MKL25Z4.H>

//neopixel configuration
#define NP_PIXELS (16) // number of pixels

typedef struct _NP_pixel_t
{
	uint8_t red;
	uint8_t grn;
	uint8_t blu;
} NP_pixel_t;

//uses TPM ch 3, PTE30, and DMA channel 0
void NP_init(void);

//set indicated pixel (0-15) to the given rgb value. sets internal flag indicated state of data buffer has changed.
void NP_set_pixel(uint8_t index, uint8_t red, uint8_t grn, uint8_t blu);

//get indicated pixel color (returns all 0s if index invalid)
NP_pixel_t NP_get_pixel(uint8_t index);

//send neopixel data to ring
// wait (boolean) - if update in progress, block if wait == 1, else return immediately (no update)
void NP_update(uint8_t wait);

#endif
