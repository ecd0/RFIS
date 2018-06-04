#include "neopixel.h"

//waveform definitions
#define TICKS_PERIOD (59) // modulo-1 for pwm clock (config'd as up counter). neopixel clock: 800khz = 1.25us period ((1/48MHz)/60)
#define VAL0         (14) // logic 0 hi period in clock ticks
#define VAL1         (30) // logic 1 hi period in clock ticks
#define VAL00         (0) // low value for reset/latch (no high period)

//timing/dma configuration (in units of bit times (pwm periods))
#define NP_PRE       (2) // start by sending a reset
#define NP_BITS      (24) // data bits per pixel
#define NP_POST      (64) // closing reset (latch) time (40 bit times * 1.25 us per bit = 50us)
#define NP_DMA_BYTES (sizeof(np_data))

//for readability
#define FALSE (0)
#define TRUE (1)

volatile uint8_t v_np_dma_complete = FALSE; //set by DMA interrupt on successful transfer completion
volatile uint8_t v_np_data_changed = FALSE; //set by np_set_pixel to indicate changes in data buffer (auto-update if dma complete interrupt catches it)

uint16_t np_data[NP_PRE + (NP_PIXELS * NP_BITS) + NP_POST]; //timer is 16 bits, so each bit time is repr'd w/ 16 bits


static void NP_restart_DMA(void);


void NP_init(void)
{
	uint16_t bb = 0, bi; //bit base, index

  //clear all color values to start
	for(bi=0; bi < NP_PRE; bi++)
	{
		np_data[bi] = VAL00;
	}
	bb = bi;
	for(bi = 0; bi < NP_PIXELS * NP_BITS; bi++)
	{
		np_data[bb + bi] = VAL0;
	}
	bb += bi;
	for(bi = 0; bi < NP_POST; bi++)
	{
		np_data[bb + bi] = VAL00;
	}
	//--init pin E30
	SIM->SCGC5 |= SIM_SCGC5_PORTE_MASK; //open clock gate to port E (omitting this causes hard fault)
  PORTE->PCR[30] = PORT_PCR_MUX(3); //connect ptE30 to TPM0 ch 3 (alt func 3)
  PTE->PDDR |= 1<<30; //enable output on pin
	PORTD->PCR[3] |= PORT_PCR_MUX(4);
	PTD->PDDR |= 1<<3;
//	PORTD->PCR[3] &= ~PORT_PCR_MUX(4); //connect ptE30 to TPM0 ch 3 (alt func 3)
	//TODO: anything else?

	//--init timer 0, chan 3
  SIM->SCGC6 |= SIM_SCGC6_TPM0_MASK; //enable clock to TPM0
	SIM->SOPT2 |= SIM_SOPT2_TPMSRC(1) | SIM_SOPT2_PLLFLLSEL_MASK; //connect 48MHz MCGFLLCLK to TPM0 (copied from UART config, hopefully this is correct)
	//TPM0->CONF |= TPM_CONF_TRIGSEL(8); //trigger on overflow
	TPM0->SC = TPM_SC_DMA_MASK | TPM_SC_TOIE_MASK; //enable dma transfer on ovf, disable ovf interrupt, disable counter, prescale == 1
	TPM0->CONTROLS[3].CnSC =//clear event flag, enable chan interrupts, edge-aligned pwm mode, high on start, low when counter matches pwm period, enable DMA transfers
		TPM_CnSC_CHF_MASK | TPM_CnSC_CHIE_MASK | TPM_CnSC_MSB_MASK | /*TPM_CnSC_MSA_MASK | */TPM_CnSC_ELSB_MASK | /*~TPM_CnSC_ELSA_MASK |*/ TPM_CnSC_DMA_MASK;
	TPM0->CONTROLS[3].CnV = 0; //initially zero duty cycle
	TPM0->CNT = 0; //clear counter
  TPM0->MOD = TICKS_PERIOD; //set base period

	//--init dma mux
  SIM->SCGC6 |= SIM_SCGC6_DMAMUX_MASK; //enable DMA MUX clock
	DMAMUX0->CHCFG[0] = DMAMUX_CHCFG_ENBL_MASK | DMAMUX_CHCFG_SOURCE(27); //receive tpm0ch3 dma requests on dma ch 0
	
	//--init dma TPM0ch3 src = 27
	SIM->SCGC7 |= SIM_SCGC7_DMA_MASK; //enable DMA clock
	DMA0->DMA->DCR &= ~(DMA_DCR_SMOD_MASK|DMA_DCR_DMOD_MASK); //disable circular buffer for src and dst (SMOD/DMOD)
	DMA0->DMA->DCR &= ~DMA_DCR_DINC_MASK; //disable dest addr auto-incr
	DMA0->DMA->DCR |= DMA_DCR_SINC(1); //enable source addr auto-increment
	DMA0->DMA->DCR |= DMA_DCR_SSIZE(2) | DMA_DCR_DSIZE(2); //both src and dest transfer size is 16 bits
	DMA0->DMA->DCR |= DMA_DCR_EINT(1) | DMA_DCR_ERQ(1) | DMA_DCR_D_REQ(1); //enable completion interrupt, enable peripheral requests, auto-disable interrupt requests on completion
	DMA0->DMA->DCR |= DMA_DCR_EADREQ(1) | DMA_DCR_CS(1); //enable async DMA and enable cycle stealing
  DMA0->DMA->SAR = (uint32_t)np_data; //source address is neopixel data buffer
	DMA0->DMA->DAR = (uint32_t)&TPM0->CONTROLS[3].CnV; //dst addr is pwm ch 3 value reg
	DMA0->DMA->DSR_BCR |= NP_DMA_BYTES; //transfer the whole data buffer
  NVIC_SetPriority(DMA0_IRQn, 129); //enable completion interrupt TODO: priority?
  NVIC_ClearPendingIRQ(DMA0_IRQn);
  NVIC_EnableIRQ(DMA0_IRQn);

	TPM0->SC |= TPM_SC_CMOD(1); //count on clock (ENABLES TIMER)
}


void NP_set_pixel(uint8_t index, uint8_t red, uint8_t grn, uint8_t blu)
{
	uint8_t cbi; //color bit counter (w/in color byte)
	uint16_t pbi; //pixel bit index (w/in dma buffer)
	if(index < NP_PIXELS) //only update pixels we have
	{
		pbi = NP_PRE + (index * NP_BITS); //actual pixel offset in dma buffer
		for(cbi=0; cbi < 8; cbi++) //GREEN is first
		{
			if(grn & 0x80)
				np_data[pbi] = VAL1;
			else
				np_data[pbi] = VAL0;
			grn <<= 1; //next color bit
			pbi++; //next dma bit
		}
		for(cbi=0; cbi < 8; cbi++) //RED is next
		{
			if(red & 0x80)
				np_data[pbi] = VAL1;
			else
				np_data[pbi] = VAL0;
			red <<= 1; //next color bit
			pbi++; //next dma bit
		}
		for(cbi=0; cbi < 8; cbi++) //BLUE is last
		{
			if(blu & 0x80)
				np_data[pbi] = VAL1;
			else
				np_data[pbi] = VAL0;
			blu <<= 1; //next color bit
			pbi++; //next dma bit
		}
		v_np_data_changed = TRUE; //TODO: see if we're actually changing the pixel? (to maybe save an update cycle)
	}
}


NP_pixel_t NP_get_pixel(uint8_t index)
{
	NP_pixel_t pixel = {0};
	uint16_t pbi;
	if(index < NP_PIXELS)
	{
//	  pbi = NP_PRE + (index * NP_BITS); //actual pixel offset in dma buffer
//		pixel.red = np_data[
	}
	return pixel;
}


//interrupt handler for dma transfer completion TODO: is this called after every individual unit transfer, or completion of entire block?
void DMA0_IRQHandler(void)
{
	int x=0;
	//check for error conditions first
	if(DMA0->DMA->DSR_BCR & (DMA_DSR_BCR_CE_MASK | DMA_DSR_BCR_BES_MASK | DMA_DSR_BCR_BED_MASK))
	{
		x=1;
	}
	if((DMA0->DMA->DSR_BCR & DMA_DSR_BCR_BCR_MASK) == 0) //no bytes remaining
	{
	  v_np_dma_complete = TRUE;
   	DMA0->DMA->DSR_BCR |= DMA_DSR_BCR_DONE_MASK; //clear done bit to enable reprogramming
	  if(v_np_data_changed) //TODO also check bytes remaining
	  	NP_restart_DMA();//trigger another update if data changed during last dma transaction
	  else
  	{
	  	TPM0->SC &= ~TPM_SC_CMOD_MASK; //disable clock
  	}
	}
}


//minimum reinitialization to start another dma transfer
static void NP_restart_DMA(void)
{
	v_np_dma_complete = FALSE; //clear completion flag before we start again
  DMA0->DMA->SAR = (uint32_t)np_data; //re-init src addr
	DMA0->DMA->DSR_BCR |= NP_DMA_BYTES; //re-init byte count
	//DMA0->DMA->DSR_BCR |= DMA_DSR_BCR_DONE_MASK; //clear done bit to restart
	TPM0->CNT = 0; //reset counter
	DMA0->DMA->DCR |= DMA_DCR_ERQ_MASK; //re-enable peripheral requests
	TPM0->CONTROLS[3].CnSC |= TPM_CnSC_CHF_MASK; //clear the channel event flag
	TPM0->SC |= TPM_SC_CMOD(1); //restart the clock
	//TODO: maybe clear channel flag in dma? tpm?
}


void NP_update(uint8_t wait)
{
	v_np_data_changed = FALSE; //clear data change flag before starting a new update
	while(!v_np_dma_complete && wait); //wait indefinitely if asked (TODO: timeout?)
	if(v_np_dma_complete) //make sure it was the completion flag that broke the wait loop
		NP_restart_DMA();
}
