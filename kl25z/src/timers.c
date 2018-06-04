#include "timers.h"
#include "MKL25Z4.h"

volatile unsigned PIT_interrupt_counter = 0;
volatile unsigned LCD_update_requested = 0;

void Init_PIT(unsigned period) {
	// Enable clock to PIT module
	SIM->SCGC6 |= SIM_SCGC6_PIT_MASK;
	
	// Enable module, freeze timers in debug mode
	PIT->MCR &= ~PIT_MCR_MDIS_MASK;
	PIT->MCR |= PIT_MCR_FRZ_MASK;
	
	// Initialize PIT0 to count down from argument 
	PIT->CHANNEL[0].LDVAL = PIT_LDVAL_TSV(period);

	// No chaining
	PIT->CHANNEL[0].TCTRL &= PIT_TCTRL_CHN_MASK;
	
	// Generate interrupts
	PIT->CHANNEL[0].TCTRL |= PIT_TCTRL_TIE_MASK;

	/* Enable Interrupts */
	NVIC_SetPriority(PIT_IRQn, 128); // 0, 64, 128 or 192
	NVIC_ClearPendingIRQ(PIT_IRQn); 
	NVIC_EnableIRQ(PIT_IRQn);	
}


void Start_PIT(void) {
// Enable counter
	PIT->CHANNEL[0].TCTRL |= PIT_TCTRL_TEN_MASK;
}

void Stop_PIT(void) {
// Enable counter
	PIT->CHANNEL[0].TCTRL &= ~PIT_TCTRL_TEN_MASK;
}


void PIT_IRQHandler() {
	static unsigned LCD_update_delay = LCD_UPDATE_PERIOD;
	//clear pending IRQ
	NVIC_ClearPendingIRQ(PIT_IRQn);
	
	// check to see which channel triggered interrupt 
	if (PIT->CHANNEL[0].TFLG & PIT_TFLG_TIF_MASK) {
		// clear status flag for timer channel 0
		PIT->CHANNEL[0].TFLG &= PIT_TFLG_TIF_MASK;
		
		// Do ISR work
		PIT_interrupt_counter++;
		LCD_update_delay--;
		if (LCD_update_delay == 0) {
			LCD_update_requested = 1;
			LCD_update_delay = LCD_UPDATE_PERIOD;
		}
	} else if (PIT->CHANNEL[1].TFLG & PIT_TFLG_TIF_MASK) {
		// clear status flag for timer channel 1
		PIT->CHANNEL[1].TFLG &= PIT_TFLG_TIF_MASK;
	} 
}

void Init_PWM()
{
	//turn on clock to TPM 
	SIM->SCGC6 |= SIM_SCGC6_TPM0_MASK;
	
	//set multiplexer to connect TPM0 Ch 2 to PTA5
	PORTA->PCR[5] |= PORT_PCR_MUX(3); 

	// TPM0 Ch 4 to PTC8
	PORTC->PCR[8] |= PORT_PCR_MUX(3);
	
	//set clock source for tpm
	SIM->SOPT2 |= (SIM_SOPT2_TPMSRC(1) | SIM_SOPT2_PLLFLLSEL_MASK);

	//load the counter and mod
	TPM0->MOD = 60124;
		
	//set channels to center-aligned high-true PWM
	//	TPM0->CONTROLS[1].CnSC = TPM_CnSC_MSB_MASK | TPM_CnSC_ELSB_MASK;
	TPM0->CONTROLS[2].CnSC = TPM_CnSC_MSB_MASK | TPM_CnSC_ELSB_MASK;
	TPM0->CONTROLS[4].CnSC = TPM_CnSC_MSB_MASK | TPM_CnSC_ELSB_MASK;

	//set TPM to up-down and divide by 8 prescaler and clock mode
	TPM0->SC = (TPM_SC_CPWMS_MASK | TPM_SC_CMOD(1) | TPM_SC_PS(3));
	
	//set trigger mode
	TPM0->CONF |= TPM_CONF_TRGSEL(0xA);
	
	TPM0->CONTROLS[2].CnV = 0x3000;
	TPM0->CONTROLS[4].CnV = 0x2000;
	
}

void Set_PWM_Values(uint16_t perc1, uint16_t perc2) {
	uint16_t n1, n2;
	
	n1 = perc1*55 + 1500;
	n2 = perc2*55 + 1500;
	
	TPM0->CONTROLS[2].CnV = n1;
	TPM0->CONTROLS[4].CnV = n2;
	
}
// *******************************ARM University Program Copyright © ARM Ltd 2013*************************************   
