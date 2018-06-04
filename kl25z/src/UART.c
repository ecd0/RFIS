#include "UART.h"
#include <stdio.h>

Q_T TxQ, RxQ;

/* BEGIN - UART0 Device Driver

	Code created by Shannon Strutz
	Date : 5/7/2014
	Licensed under CC BY-NC-SA 3.0
	http://creativecommons.org/licenses/by-nc-sa/3.0/

	Modified by Alex Dean 9/13/2016
	
*/


#if (BLOCKING_STDIO_U0 || QUEUED_STDIO_U0)
struct __FILE
{
  int handle;
};

FILE __stdout;  //Use with printf
FILE __stdin;		//use with fget/sscanf, or scanf
#endif


#if BLOCKING_STDIO_U0 // Original blocking version
//Retarget the fputc method to use the UART0
int fputc(int ch, FILE *f){
	while(!(UART0->S1 & UART_S1_TDRE_MASK) && !(UART0->S1 & UART_S1_TC_MASK));
	UART0->D = ch;
	return ch;
}

//Retarget the fgetc method to use the UART0
int fgetc(FILE *f){
	while(!(UART0->S1 & UART_S1_RDRF_MASK));
	return UART0->D;
}

#elif QUEUED_STDIO_U0 // queued serial I/O

//Retarget the fputc method to use the UART0
int fputc(int ch, FILE *f){
	Q_Enqueue(&TxQ, ch);
	// start transmitter if it isn't already running
	if (!(UART0->C2 & UART0_C2_TIE_MASK)) {
		while(!(UART0->S1 & UART_S1_TDRE_MASK) && !(UART0->S1 & UART_S1_TC_MASK))
			;
		UART0->D = Q_Dequeue(&TxQ); 
		UART0->C2 |= UART0_C2_TIE(1);
	}
	return ch;
}

//Retarget the fgetc method to use the UART0
int fgetc(FILE *f){
	if (Q_Size(&RxQ)>0)
		return Q_Dequeue(&RxQ);
	else
		return -1;
}
#endif

void Init_UART0(uint32_t baud_rate) {
	uint16_t sbr;
	uint8_t temp;
	
	// Enable clock gating for UART0 and Port A
	SIM->SCGC4 |= SIM_SCGC4_UART0_MASK; 										
	SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK;											
	
	// Make sure transmitter and receiver are disabled before init
	UART0->C2 &= ~UART0_C2_TE_MASK & ~UART0_C2_RE_MASK; 		
	
	// Set UART clock to 48 MHz clock 
	SIM->SOPT2 |= SIM_SOPT2_UART0SRC(1);
	SIM->SOPT2 |= SIM_SOPT2_PLLFLLSEL_MASK;

	// Set pins to UART0 Rx and Tx
	PORTA->PCR[1] = PORT_PCR_ISF_MASK | PORT_PCR_MUX(2); // Rx
	PORTA->PCR[2] = PORT_PCR_ISF_MASK | PORT_PCR_MUX(2); // Tx
	
	// Set baud rate and oversampling ratio
	sbr = (uint16_t)((SYS_CLOCK)/(baud_rate * UART_OVERSAMPLE_RATE)); 			
	UART0->BDH &= ~UART0_BDH_SBR_MASK;
	UART0->BDH |= UART0_BDH_SBR(sbr>>8);
	UART0->BDL = UART0_BDL_SBR(sbr);
	UART0->C4 |= UART0_C4_OSR(UART_OVERSAMPLE_RATE-1);				

	// Disable interrupts for RX active edge and LIN break detect, select one stop bit
	UART0->BDH |= UART0_BDH_RXEDGIE(0) | UART0_BDH_SBNS(0) | UART0_BDH_LBKDIE(0);
	
	// Don't enable loopback mode, use 8 data bit mode, don't use parity
	UART0->C1 = UART0_C1_LOOPS(0) | UART0_C1_M(0) | UART0_C1_PE(0); 
	// Don't invert transmit data, don't enable interrupts for errors
	UART0->C3 = UART0_C3_TXINV(0) | UART0_C3_ORIE(0)| UART0_C3_NEIE(0) 
			| UART0_C3_FEIE(0) | UART0_C3_PEIE(0);

	// Clear error flags
	UART0->S1 = UART0_S1_OR(1) | UART0_S1_NF(1) | UART0_S1_FE(1) | UART0_S1_PF(1);

	// Try it a different way
	UART0->S1 |= UART0_S1_OR_MASK | UART0_S1_NF_MASK | 
									UART0_S1_FE_MASK | UART0_S1_PF_MASK;
	
	// Send LSB first, do not invert received data
	UART0->S2 = UART0_S2_MSBF(0) | UART0_S2_RXINV(0); 

	
// Enable interrupts
#if USE_UART_INTERRUPTS
	Q_Init(&TxQ);
	Q_Init(&RxQ);

	NVIC_SetPriority(UART0_IRQn, 2); // 0, 1, 2, or 3
	NVIC_ClearPendingIRQ(UART0_IRQn); 
	NVIC_EnableIRQ(UART0_IRQn);

	// Enable receive interrupts but not transmit interrupts yet
	UART0->C2 |= UART_C2_RIE(1);
#endif

	// Enable UART receiver and transmitter
	UART0->C2 |= UART0_C2_RE(1) | UART0_C2_TE(1);	
	
	// Clear the UART RDRF flag
	temp = UART0->D;
	UART0->S1 &= ~UART0_S1_RDRF_MASK;

}

/* END - UART0 Device Driver 
	Code created by Shannon Strutz
	Date : 5/7/2014
	Licensed under CC BY-NC-SA 3.0
	http://creativecommons.org/licenses/by-nc-sa/3.0/

	Modified by Alex Dean 9/13/2016
	
*/

void UART0_IRQHandler(void) {
	uint8_t ch;
	if (UART0->S1 & (UART_S1_OR_MASK |UART_S1_NF_MASK | 
		UART_S1_FE_MASK | UART_S1_PF_MASK)) {
			// clear the error flags
			UART0->S1 |= UART0_S1_OR_MASK | UART0_S1_NF_MASK | 
									UART0_S1_FE_MASK | UART0_S1_PF_MASK;	
			// read the data register to clear RDRF
			ch = UART0->D;
	}
	if (UART0->S1 & UART0_S1_RDRF_MASK) {
		// received a character
		ch = UART0->D;
		if (!Q_Full(&RxQ)) {
			Q_Enqueue(&RxQ, ch);
		} else {
			// error - queue full.
			// discard character
		}
	}
	if ( (UART0->C2 & UART0_C2_TIE_MASK) && // transmitter interrupt enabled
			(UART0->S1 & UART0_S1_TDRE_MASK) ) { // tx buffer empty
		// can send another character
		if (!Q_Empty(&TxQ)) {
			UART0->D = Q_Dequeue(&TxQ);
		} else {
			// queue is empty so disable transmitter interrupt
			UART0->C2 &= ~UART0_C2_TIE_MASK;
		}
	}
}

// UART0 Interrupt Echo
void UART0_Echo_Interrupt_Test(void) {	
	uint8_t buffer[80], c, * bp;
	
	while (1) {
			// Blocking receive
			while (Q_Size(&RxQ) == 0)
				; // wait for character to arrive
			c = Q_Dequeue(&RxQ);
			
			// Blocking transmit
			sprintf((char *) buffer, "You pressed %c\n\r", c);
			// enqueue string
			bp = buffer;
			while (*bp != '\0') { 
				// copy characters up to null terminator
				while (Q_Full(&TxQ))
					; // wait for space to open up
				Q_Enqueue(&TxQ, *bp);
				bp++;
			}
			// start transmitter if it isn't already running
			if (!(UART0->C2 & UART0_C2_TIE_MASK)) {
				UART0->C2 |= UART0_C2_TIE(1);
			}
	}
}

void Send_String(uint8_t * str) {
	// enqueue string
	while (*str != '\0') { // copy characters up to null terminator
		while (Q_Full(&TxQ))
			; // wait for space to open up
		Q_Enqueue(&TxQ, *str);
		str++;
	}
	// start transmitter if it isn't already running
#if QUEUED_U0
	if (!(UART0->C2 & UART0_C2_TIE_MASK)) {
		UART0->D = Q_Dequeue(&TxQ); 
		UART0->C2 |= UART0_C2_TIE(1);
	}
#elif QUEUED_U2
	if (!(UART2->C2 & UART_C2_TIE_MASK)) {
		UART2->D = Q_Dequeue(&TxQ); 
		UART2->C2 |= UART_C2_TIE(1);
	}
#endif
}

void Init_UART2(uint32_t baud_rate) {
	uint32_t divisor;
	
	// enable clock to UART and Port E
	SIM->SCGC4 |= SIM_SCGC4_UART2_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTE_MASK;


	// select UART pins
	PORTE->PCR[22] = PORT_PCR_MUX(4); // Tx
	PORTE->PCR[23] = PORT_PCR_MUX(4); // Rx
	
	UART2->C2 &=  ~(UARTLP_C2_TE_MASK | UARTLP_C2_RE_MASK);
		
	// Set baud rate to 4800 baud
	divisor = BUS_CLOCK/(baud_rate*16);
	UART2->BDH = UART_BDH_SBR(divisor>>8);
	UART2->BDL = UART_BDL_SBR(divisor);
	
	// No parity, 8 bits, two stop bits, other settings;
	UART2->C1 = 0; 
	UART2->S2 = 0;
	UART2->C3 = 0;
	
// Enable transmitter and receiver but not interrupts
	UART2->C2 = UART_C2_TE_MASK | UART_C2_RE_MASK;
	
#if USE_UART_INTERRUPTS
	NVIC_SetPriority(UART2_IRQn, 2); // 0, 1, 2, or 3
	NVIC_ClearPendingIRQ(UART2_IRQn); 
	NVIC_EnableIRQ(UART2_IRQn);

	UART2->C2 |= UART_C2_RIE_MASK;
	Q_Init(&TxQ);
	Q_Init(&RxQ);
#endif

}

void UART0_Transmit_Poll(uint8_t data) {
		while (!(UART0->S1 & UART0_S1_TDRE_MASK))
			;
		UART0->D = data;
}	

uint8_t UART0_Receive_Poll(void) {
		while (!(UART0->S1 & UART0_S1_RDRF_MASK))
			;
		return UART0->D;
}	

void UART2_Transmit_Poll(uint8_t data) {
		while (!(UART2->S1 & UART_S1_TDRE_MASK))
			;
		UART2->D = data;
}	

uint8_t UART2_Receive_Poll(void) {
		while (!(UART2->S1 & UART_S1_RDRF_MASK))
			;
		return UART2->D;
}	

void UART2_IRQHandler(void) {
	uint8_t ch;

	if (UART2->S1 & (UART_S1_OR_MASK |UART_S1_NF_MASK | 
		UART_S1_FE_MASK | UART_S1_PF_MASK)) {
			// handle the error
			// clear the flag
			// UART2->S1 |= UART_S1_OR_MASK | UART_S1_NF_MASK | UART_S1_FE_MASK | UART_S1_PF_MASK;
			ch = UART2->D;
		}
	if (UART2->S1 & UART_S1_RDRF_MASK) {
		// received a character
		if (!Q_Full(&RxQ)) {
			Q_Enqueue(&RxQ, UART2->D);
		} else {
			// error - queue full.
			while (1)
				;
		}
	}	
	if ( (UART2->C2 & UART_C2_TIE_MASK) && // transmitter interrupt enabled
			(UART2->S1 & UART_S1_TDRE_MASK) ) { // tx buffer empty
		// can send another character
		if (!Q_Empty(&TxQ)) {
			UART2->D = Q_Dequeue(&TxQ);
		} else {
			// queue is empty so disable transmitter interrupt
			UART2->C2 &= ~UART_C2_TIE_MASK;
		}
	}

}

uint32_t Rx_Chars_Available(void) {
	return Q_Size(&RxQ);
}

uint8_t	Get_Rx_Char(void) {
	return Q_Dequeue(&RxQ);
}


// *******************************ARM University Program Copyright © ARM Ltd 2013*************************************   
