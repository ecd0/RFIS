#include <MKL25Z4.H>
#include <stdio.h>
#include "gpio_defs.h"
#include "UART.h"
#include "LEDs.h"
#include "timers.h"		
#include "delay.h"
#include "math.h"
#include "neopixel.h"
#include "motors.h"

motor_t motors[5];

extern uint8_t	Get_Rx_Char(void);
extern uint32_t Rx_Chars_Available(void);
void init_GPIO(void);
void init_interrupts(void);
uint8_t ch1, ch2, ch3, ch4, inst2, inst1;
int enc_pos_1, enc_pos_2, enc_pos_3, enc_pos_4, enc_pos_5;
int m1_calibrated = 0, m2_calibrated = 0, m3_calibrated = 0, m4_calibrated = 0, m5_calibrated = 0;
int all_calibrated = 0;
int m1_steps = 0, m2_steps = 0, m3_steps = 0, m4_steps = 0, m5_steps = 0;
int m1_expected = 0, m2_expected = 0, m3_expected = 0, m4_expected = 0, m5_expected = 0;
int m1_direction = 0, m2_direction = 0, m3_direction = 0, m4_direction = 0, m5_direction = 0;
int m1_finished = 0, m2_finished = 0, m3_finished = 0, m4_finished = 0, m5_finished = 0;
int m1_running = 0, m2_running = 0, m3_running = 0, m4_running = 0, m5_running = 0;
int total_running = 0;
int i;
int motor_delay_1 = 20000, motor_delay_2 = 2500, motor_delay_3 = 2500, motor_delay_4 = 10000, motor_delay_5 = 10000; //Control the motor speed here
int delay = 0;
const int m1_thresh = 100, m2_thresh = 100, m3_thresh = 100, m4_thresh = 100, m5_thresh = 100;
uint8_t in, red, grn, blu, temp;
int remaining = 0, remaining_m5 = 0;
int fixing_m1 = 0, fixing_m2 = 0, fixing_m3 = 0, fixing_m4 = 0, fixing_m5 = 0;


int main (void) { 
	
	//Init the indicator LED on board
	Init_RGB_LEDs();
	
	//Init GPIO for motors and pumps
	init_GPIO();
	
	//Init Interrupts for Encoders
	init_interrupts();
	
	//Init NeoPixel Ring
	NP_init();
	
	//Clear NeoPixel Ring
	for(i = 0; i < 16; i++){
			NP_set_pixel(i,0,0,0);
		}
	NP_update(0);
	
	//Starting up uncalibrated (YELLOW)
	Control_RGB_LEDs(1,1,0);
	Delay(50);
	Init_UART0(115200);
	

	
	
	//Send_String("\r\nHello World!\r\n"); //Tell me you're alive
	Delay(200);
	
	while (1) {
	//Make sure the step and direction pins are cleared before starting. (may be uneccessary)
	PTB->PCOR = MASK(0);
	PTB->PCOR = MASK(1);
		
	//If 3 characters are received, store them now. Otherwise continue running throught the program.
	if(Rx_Chars_Available() == 4){
		ch1 = Get_Rx_Char();
		ch2 = Get_Rx_Char();
		ch3 = Get_Rx_Char();
		ch4 = Get_Rx_Char();
		
		if(ch1 != 'S' &&  //Stop
			 ch1 != 'C' &&  //Calibrate
		   ch1 != 'M' &&  //Move
		   ch1 != 'P' &&  //Pin
		   ch1 != 'L' &&  //Light
		   ch1 != 'D' &&  //Delay
		   ch1 != 'R'){   //Reset
			Send_String("E010");
		}
	}
	/////////////////////////////////////////////////////////////
	// If an 'S' (0x53) is sent we need to stop all motors and //
	// send the positions arrived at for each motor.           //
	/////////////////////////////////////////////////////////////
	if(ch1 == 'S'){
		
		//Shut down motors
		m1_steps = 0;
		m2_steps = 0;
		m3_steps = 0;
		m4_steps = 0;
		m5_steps = 0;
		
		if(m1_running){
			m1_running = 0;
			Send_String("A100"); //done
		}
		if(m2_running){
			m2_running = 0;
			Send_String("A200"); //done
		}
		if(m3_running){
			m3_running = 0;
			Send_String("A300"); //done
		}
		if(m4_running){
			m4_running = 0;
			Send_String("A400"); //done
		}
		if(m5_running){
			m5_running = 0;
			Send_String("A500"); //done
		}
		
	}
	
	/////////////////////////////////////////////////////////////////////
  // If the character is 'C' (0x43), we need to calibrate the motor. //
  /////////////////////////////////////////////////////////////////////
  if(ch1 == 'C'){
    
		if(ch2 > 0 && ch2 < 6){
			motors[ch2].pos = 0;
			motors[ch2].calibrated = 1;
		}
		else{
			Send_String("E010"); //Invalid Message
		}
		
		//Check if everything is calibrated set indicator LED to GREEN
		if(motors[0].calibrated &
			 motors[1].calibrated &
		   motors[2].calibrated &
		   motors[3].calibrated &
		   motors[4].calibrated){
			all_calibrated = 1;
			Control_RGB_LEDs(0,1,0);
		}
		
  }
	
	//////////////////////////////////////////////////////////////////////////////////
  // If an 'M' is sent, we are receiving motor instructions:                      //
  //                                                                              //
  // AT FIRST THE BYTE 0x4D ('M') IS SENT WITH THE MOTOR NUMBER (0X31 - 0X35) AND //
	// 2 ADDITIONAL BYTES CONTAINING STEP AND DIRECTION INFORMATION.                //
  // THEN THIS INFORMATION IS STORED AND THE CODE TO RUN THE MOTOR WILL BE        //
	// EXECUTED EACH TIME THROUGH THE 'while(1)' LOOP UNTIL THE NUMBER OF STEPS     //
	// HAS BEEN DECREMENTED TO ZERO.                                                //
  //////////////////////////////////////////////////////////////////////////////////
  if(ch1 == 'M' || m1_steps > 0 || m2_steps > 0 || m3_steps > 0 || m4_steps > 0 || m5_steps > 0){

		//Store the number of steps being requested
		inst1 = ch4;
		inst2 = ch3;
		
		//New instructions being received
		if(ch1 == 'M'){
			//Tell us you hear us.
			//Send_String("\r\nReceived Motor Instruction\r\n"); //delete me to save processor time
			
			if(ch2 == '1'){
				//Get the direction bit
				m1_direction = inst2 >> 7;
			
				//Calculate the number of steps we're moving
				inst2 = inst2 << 1; // Get rid of direction bit
				inst2 = inst2 >> 1;
				m1_steps = inst2 * 256 + inst1; // Concatenate
				
				/*
				Calculate expected final position
				*/
				
				m1_running = 1;
			}
			else if(ch2 == '2'){
				//Get the direction bit
				m2_direction = inst2 >> 7;
			
				//Calculate the number of steps we're moving
				inst2 = inst2 << 1; // Get rid of direction bit
				inst2 = inst2 >> 1;
				m2_steps = inst2 * 256 + inst1; // Concatenate
				
				/*
				Calculate expected final position
				*/
				
				m2_running = 1;
			}
			else if(ch2 == '3'){
				//Get the direction bit
				m3_direction = inst2 >> 7;
			
				//Calculate the number of steps we're moving
				inst2 = inst2 << 1; // Get rid of direction bit
				inst2 = inst2 >> 1;
				m3_steps = inst2 * 256 + inst1; // Concatenate
				
				/*
				Calculate expected final position
				*/
				
				m3_running = 1;
			}
			else if(ch2 == '4'){
				//Get the direction bit
				m4_direction = inst2 >> 7;
			
				//Calculate the number of steps we're moving
				inst2 = inst2 << 1; // Get rid of direction bit
				inst2 = inst2 >> 1;
				m4_steps = inst2 * 256 + inst1; // Concatenate
				
				/*
				Calculate expected final position
				*/
				
				m4_running = 1;
			}
			else if(ch2 == '5'){
				//Get the direction bit
				m5_direction = inst2 >> 7;
			
				//Calculate the number of steps we're moving
				inst2 = inst2 << 1; // Get rid of direction bit
				inst2 = inst2 >> 1;
				m5_steps = inst2 * 256 + inst1; // Concatenate
				
				/*
				Calculate expected final position
				*/
				
				m5_running = 1;
			}
			//Invalid Message Error
			else{
				Send_String("E010");
			}
		}
			
		total_running = m1_running + m2_running + m3_running + m4_running + m5_running;
			
			// Code for a single step followed by the decrement of the number of steps
      if(m1_steps > 0){
				
				//Set m1_direction pin
				if(m1_direction){
					PTB->PCOR = MASK(0); // Move forward
					if(!fixing_m1)
						m1_expected+=16;
				}
				else{
					PTB->PSOR = MASK(0); // Move backward
					if(!fixing_m1)
						m1_expected-=16;
				}
				
				//Pulse m1_step pin
        PTB->PSOR = MASK(1);
        Delay(motor_delay_1 / total_running); 
        PTB->PCOR = MASK(1);
        Delay(motor_delay_1 / total_running); 
				
				m1_steps--; //Decrement
				
				if(m1_steps == 0){
					remaining = abs(m1_expected - enc_pos_1);
				  if(remaining >= 16){
						m1_direction = (m1_expected > enc_pos_1);
						m1_steps = remaining/16;
						fixing_m1 = 1;
					}
					else{
						m1_finished = 1;
					  fixing_m1 = 0;
					}
				}
      }
			if(m2_steps > 0){
				
				//Set m2_direction pin
				if(m2_direction){
					PTB->PCOR = MASK(2); // Move forward
					if(!fixing_m2)
						m2_expected+=1;
				}
				else{
					PTB->PSOR = MASK(2); // Move backward
					if(!fixing_m2)
						m2_expected-=1;
				}
				
				//Pulse m2_step pin
        PTB->PSOR = MASK(3);
        Delay(motor_delay_2 / total_running); 
        PTB->PCOR = MASK(3);
        Delay(motor_delay_2 / total_running); 
				
				m2_steps--; //Decrement
				
				if(m2_steps == 0){
					remaining = abs(m2_expected - enc_pos_2);
				  if(remaining >= 1){
						m2_direction = (m2_expected > enc_pos_2);
						m2_steps = remaining;
						fixing_m2 = 1;
					}
					else{
						m2_finished = 1;
					  fixing_m2 = 0;
					}
				}
      }
			if(m3_steps > 0){
				
				//Set m3_direction pin
				if(m3_direction){
					PTB->PCOR = MASK(8); // Move forward
					if(!fixing_m3)
						m3_expected+=1;
				}
				else{
					PTB->PSOR = MASK(8); // Move backward
					if(!fixing_m3)
						m3_expected-=1;
				}
				
				//Pulse m3_step pin
        PTB->PSOR = MASK(9);
        Delay(motor_delay_3 / total_running); 
        PTB->PCOR = MASK(9);
        Delay(motor_delay_3 / total_running); 
				
				m3_steps--; //Decrement
				
				if(m3_steps == 0){
					remaining = abs(m3_expected - enc_pos_3);
				  if(remaining >= 1){
						m3_direction = (m3_expected > enc_pos_3);
						m3_steps = remaining/16;
						fixing_m3 = 1;
					}
					else{
						m3_finished = 1;
					  fixing_m3 = 0;
					}
				}
      }
			if(m4_steps > 0){
				
				//Set m4_direction pin
				if(m4_direction){
					PTB->PCOR = MASK(10); // Move forward
					if(!fixing_m4)
						m4_expected+=16;
				}
				else{
					PTB->PSOR = MASK(10); // Move backward
					if(!fixing_m4)
						m4_expected-=16;
				}
				
				//Pulse m4_step pin
        PTB->PSOR = MASK(11);
        Delay(motor_delay_4 / total_running); 
        PTB->PCOR = MASK(11);
        Delay(motor_delay_4 / total_running); 
				
				m4_steps--; //Decrement
				
				
				if(m4_steps == 0){
					remaining = abs(m4_expected - enc_pos_4);
				  if(remaining >= 16){
						m4_direction = (m4_expected > enc_pos_4);
						m4_steps = remaining/16;
						fixing_m4 = 1;
					}
					else{
						m4_finished = 1;
					  fixing_m4 = 0;
					}
				}
      }
			if(m5_steps > 0){
				
				//Set m5_direction pin
				if(m5_direction){
					PTC->PCOR = MASK(4); // Move forward
					if(!fixing_m5)
						m5_expected+=16;
				}
				else{
					PTC->PSOR = MASK(4); // Move backward
					if(!fixing_m5)
						m5_expected-=16;
				}
				
				//Pulse m5_step pin
        PTC->PSOR = MASK(5);
        Delay(motor_delay_5 / total_running); 
        PTC->PCOR = MASK(5);
        Delay(motor_delay_5 / total_running); 
				
				m5_steps--; //Decrement
				
				if(m5_steps == 0){
					remaining = abs(m5_expected - enc_pos_5);
				  if(remaining >= 16){
						m5_direction = (m5_expected > enc_pos_5);
						m5_steps = remaining/16;
						fixing_m5 = 1;
					}
					else{
						m5_finished = 1;
					  fixing_m5 = 0;
					}
				}
			}
			
		/*	
		TODO: ADD CONTROL SYSTEM HERE 
	  */
			
    
		//Finished controlling any motor yet?

		if(m1_finished){
			m1_running = 0;
			m1_finished = 0;
			Send_String("A100"); //done
		}
		if(m2_finished){
			m2_running = 0;
			m2_finished = 0;
			Send_String("A200"); //done
		}
		if(m3_finished){
			m3_running = 0;
			m3_finished = 0;
			Send_String("A300"); //done
		}
		if(m4_finished){
			m4_running = 0;
			m4_finished = 0;
			Send_String("A400"); //done
		}
		if(m5_finished){
			m5_running = 0;
			m5_finished = 0;
			Send_String("A500"); //done
		}
		
		/*
		TODO: ADD ERROR CHECKING HERE
		*/
		
  }
	
	
	/////////////////////////////////////////////////////////////
	// If a 'P' (0x50) is sent, set the pin indicated by ch2   //
	// on port ? to the value indicated by ch3.                //
	/////////////////////////////////////////////////////////////
	if(ch1 == 'P'){
		
		//Blowing Pump
		if(ch2){
			
			//Turned on
			if(ch3){
				PTC->PCOR = MASK(11); //Turn off other pump for protection
				PTC->PSOR = MASK(10);
				//Send_String("\r\Blow On\r\n");
			}
			
			//Turned off
			else{
				PTC->PCOR = MASK(10);
				//Send_String("\r\nBlow Off\r\n");
			}
			
		}
		
		//Suction Pump
		else{
			
			//Turned on
			if(ch3){
				PTC->PCOR = MASK(10); //Turn off other pump for protection
				PTC->PSOR = MASK(11);
				//Send_String("\r\nSuction On\r\n");
			}
			
			//Turned off
			else{
				PTC->PCOR = MASK(11);
				//Send_String("\r\nSuction Off\r\n");
			}
			
		}
		
	}
	
	
	/////////////////////////////////////////////////////////////
	// If an 'L' (0x4C) is sent: if the next 6 bits are 0 and  //
	// the next 2 are 0 then set indicator LED to YELLOW       //
	// (clear error - uncalibtrated)  if the system is still   //
	// uncalibrated or GREEN (clear error - calibrated) if the //
  // system has already been calibrated. If the next 2 were  //
	// non-zero instead then set the indicator LED to PURPLE   //
	// (done).                                                 //
	/////////////////////////////////////////////////////////////
	if(ch1 == 'L'){
		
		//ch2[7:2] == 0 (INDICATOR LED)
		if(ch2 >> 2 == 0){
			//ch2[1:0] == 0
			if(ch2 << 6 == 0x00){
				if(all_calibrated){
					Control_RGB_LEDs(0,1,0); //GREEN (clear error - calibrated
					//Send_String("\r\nCalibrated State\r\n");
				}
				else{
					Control_RGB_LEDs(1,1,0); //YELLOW (clear error - uncalibrated)
					//Send_String("\r\nUncalibrated State\r\n");
				}
			}
			//ch2[1:0] == 1
			else{
				Control_RGB_LEDs(1,0,1); //PURPLE (done)
				//Send_String("\r\nDone State\r\n");
			}
		}
		
		//ch2[7:2] != 0 (NEOPIXEL RING)
		else{
		  in = ch2 >> 2;
			red = ch2 << 6;
		  red = red >> 2;
			temp = ch3 >> 4;
			red |= temp;
		  grn = ch3 << 4;
			grn = grn >> 2; 
			temp = ch4 >> 6;
			grn |= temp;
		  blu = ch4 << 2;
			blu = blu >> 2;
			
			NP_set_pixel(in, red, grn, blu);
			NP_update(0);
		}
		
	}
	/*
	if(abs(m1_expected-enc_pos_1)>m1_thresh){
		Control_RGB_LEDs(1,0,0);
		
		//Shut down motors
		m1_steps = 0;
		m2_steps = 0;
		m3_steps = 0;
		m4_steps = 0;
		m5_steps = 0;
		
		if(m1_running){
			m1_running = 0;
			Send_String("A100"); //done
		}
		if(m2_running){
			m2_running = 0;
			Send_String("A200"); //done
		}
		if(m3_running){
			m3_running = 0;
			Send_String("A300"); //done
		}
		if(m4_running){
			m4_running = 0;
			Send_String("A400"); //done
		}
		if(m5_running){
			m5_running = 0;
			Send_String("A500"); //done
		}
		
	}
	
	if(abs(m2_expected-enc_pos_2)>m2_thresh){
		Control_RGB_LEDs(1,0,0);
		
		//Shut down motors
		m1_steps = 0;
		m2_steps = 0;
		m3_steps = 0;
		m4_steps = 0;
		m5_steps = 0;
		
		if(m1_running){
			m1_running = 0;
			Send_String("A100"); //done
		}
		if(m2_running){
			m2_running = 0;
			Send_String("A200"); //done
		}
		if(m3_running){
			m3_running = 0;
			Send_String("A300"); //done
		}
		if(m4_running){
			m4_running = 0;
			Send_String("A400"); //done
		}
		if(m5_running){
			m5_running = 0;
			Send_String("A500"); //done
		}
		
	}
	
	if(abs(m3_expected-enc_pos_3)>m3_thresh){
		Control_RGB_LEDs(1,0,0);
		
		//Shut down motors
		m1_steps = 0;
		m2_steps = 0;
		m3_steps = 0;
		m4_steps = 0;
		m5_steps = 0;
		
		if(m1_running){
			m1_running = 0;
			Send_String("A100"); //done
		}
		if(m2_running){
			m2_running = 0;
			Send_String("A200"); //done
		}
		if(m3_running){
			m3_running = 0;
			Send_String("A300"); //done
		}
		if(m4_running){
			m4_running = 0;
			Send_String("A400"); //done
		}
		if(m5_running){
			m5_running = 0;
			Send_String("A500"); //done
		}
		
	}
	
	if(abs(m4_expected-enc_pos_4)>m4_thresh){
		Control_RGB_LEDs(1,0,0);
		
		//Shut down motors
		m1_steps = 0;
		m2_steps = 0;
		m3_steps = 0;
		m4_steps = 0;
		m5_steps = 0;
		
		if(m1_running){
			m1_running = 0;
			Send_String("A100"); //done
		}
		if(m2_running){
			m2_running = 0;
			Send_String("A200"); //done
		}
		if(m3_running){
			m3_running = 0;
			Send_String("A300"); //done
		}
		if(m4_running){
			m4_running = 0;
			Send_String("A400"); //done
		}
		if(m5_running){
			m5_running = 0;
			Send_String("A500"); //done
		}
		
	}
	
	if(abs(m5_expected-enc_pos_5)>m5_thresh){
		Control_RGB_LEDs(1,0,0);
		
		//Shut down motors
		m1_steps = 0;
		m2_steps = 0;
		m3_steps = 0;
		m4_steps = 0;
		m5_steps = 0;
		
		if(m1_running){
			m1_running = 0;
			Send_String("A100"); //done
		}
		if(m2_running){
			m2_running = 0;
			Send_String("A200"); //done
		}
		if(m3_running){
			m3_running = 0;
			Send_String("A300"); //done
		}
		if(m4_running){
			m4_running = 0;
			Send_String("A400"); //done
		}
		if(m5_running){
			m5_running = 0;
			Send_String("A500"); //done
		}
		
	}
	*/
	if(ch1 == 'D'){
		if(ch2 > 0 && ch2 < 6){
  		motors[ch2].period_ticks = ch3 << 8 + ch4; // Concatenate
		}
		else{
			Send_String("E010"); //invalid message
		}
	}
	
	
	if(ch1 == 'R'){
		Control_RGB_LEDs(1,1,0); //YELLOW (clear error - uncalibrated)
		for(i = 0; i < 16; i++){
			NP_set_pixel(i,0,0,0);
		}
		NP_update();
		
		for(i = 0; i < 5; i++){
		  motors[i].pos = 0;
			motors[i].calibrated = 0;
			motors[i].target = 0;
	  }
	}

	//Clear all characters for next through
	ch1 = NULL;
	ch2 = NULL;
	ch3 = NULL;
	ch4 = NULL;
	
	}
}

////////////////////////////////////////////////
//             INIT GPIO PINS                 //
//                                            //
// Motor       DIRECTION PIN        STEP PIN  //
//                                            //
// Motor 1:    PTB0                 PTB1      //
// Motor 2:    PTB2                 PTB3      //
// Motor 3:    PTB8                 PTB9      //
// Motor 4:    PTB10                PTB11     //
// Motor 5:    PTC4                 PTC5      //
//                                            //
//                                            //
// Suction Motor: PTC11                       //
// Blowing Motor: PTC10                       //
////////////////////////////////////////////////
void init_GPIO(void){
	
	//Enable clock for ports A, B, C, D, and E
	SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK |SIM_SCGC5_PORTB_MASK |SIM_SCGC5_PORTC_MASK | SIM_SCGC5_PORTD_MASK | SIM_SCGC5_PORTE_MASK;
	
	//Init Motor 1 (PTB0, PTB1)
	PORTB->PCR[0] &= ~PORT_PCR_MUX_MASK;
	PORTB->PCR[0] |= PORT_PCR_MUX(1); 
	PORTB->PCR[1] &= ~PORT_PCR_MUX_MASK;
	PORTB->PCR[1] |= PORT_PCR_MUX(1); 
	
	//Init Motor 2 (PTB2, PTB3)
	PORTB->PCR[2] &= ~PORT_PCR_MUX_MASK;
	PORTB->PCR[2] |= PORT_PCR_MUX(1); 
	PORTB->PCR[3] &= ~PORT_PCR_MUX_MASK;
	PORTB->PCR[3] |= PORT_PCR_MUX(1); 
	
	//Init Motor 3  (PTB8, PTB9)
	PORTB->PCR[8] &= ~PORT_PCR_MUX_MASK;
	PORTB->PCR[8] |= PORT_PCR_MUX(1); 
	PORTB->PCR[9] &= ~PORT_PCR_MUX_MASK;
	PORTB->PCR[9] |= PORT_PCR_MUX(1); 
	
	//Init Motor 4 (PTB10, PTB11)
	PORTB->PCR[10] &= ~PORT_PCR_MUX_MASK;
	PORTB->PCR[10] |= PORT_PCR_MUX(1); 
	PORTB->PCR[11] &= ~PORT_PCR_MUX_MASK;
	PORTB->PCR[11] |= PORT_PCR_MUX(1); 
	
	//Init Motor 5 (PTA16, PTA17)
	PORTC->PCR[4] &= ~PORT_PCR_MUX_MASK;
	PORTC->PCR[4] |= PORT_PCR_MUX(1); 
	PORTC->PCR[5] &= ~PORT_PCR_MUX_MASK;
	PORTC->PCR[5] |= PORT_PCR_MUX(1); 
	
	//Init Blowing Pin
	PORTC->PCR[10] &= ~PORT_PCR_MUX_MASK;
	PORTC->PCR[10] |= PORT_PCR_MUX(1); 
	
	//Init Suction Pin
	PORTC->PCR[11] &= ~PORT_PCR_MUX_MASK;
	PORTC->PCR[11] |= PORT_PCR_MUX(1); 


	PTB->PDDR |= MASK(0) | MASK (1) | MASK (2) | MASK (3) | MASK(8) | MASK (9) | MASK (10) | MASK (11);
	PTC->PDDR |= MASK(4) | MASK(5) | MASK(10) | MASK(11);
}

//////////////////////////////////
//      INIT INTERRUPTS         //
//                              //
// Enc#       CHA    CHB        //
//                              //
//Enocder 1 = PTA16  PTA17      //
//Enocder 2 = PTD2   PTD3       //
//Enocder 3 = PTD4   PTD5       //
//Enocder 4 = PTD6   PTD7       //
//Enocder 5 = PTA12  PTA13      //
//                              //
//////////////////////////////////
void init_interrupts(void) {
/* enable clock for port D */
SIM->SCGC5 |= SIM_SCGC5_PORTD_MASK;
	
/* enable clock for port A */
SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK;
	
/* Select GPIO and enable pull-up resistors and
interrupts on falling edges for pin
connected to switch */
	
PORTD->PCR[2] |= PORT_PCR_MUX(1) |
PORT_PCR_PS_MASK | PORT_PCR_PE_MASK |
PORT_PCR_IRQC(0x0b);
PORTD->PCR[3] |= PORT_PCR_MUX(1) |
PORT_PCR_PS_MASK | PORT_PCR_PE_MASK |
PORT_PCR_IRQC(0x0b);
	
PORTD->PCR[4] |= PORT_PCR_MUX(1) |
PORT_PCR_PS_MASK | PORT_PCR_PE_MASK |
PORT_PCR_IRQC(0x0b);
PORTD->PCR[5] |= PORT_PCR_MUX(1) |
PORT_PCR_PS_MASK | PORT_PCR_PE_MASK |
PORT_PCR_IRQC(0x0b);

PORTD->PCR[6] |= PORT_PCR_MUX(1) |
PORT_PCR_PS_MASK | PORT_PCR_PE_MASK |
PORT_PCR_IRQC(0x0b);
PORTD->PCR[7] |= PORT_PCR_MUX(1) |
PORT_PCR_PS_MASK | PORT_PCR_PE_MASK |
PORT_PCR_IRQC(0x0b);

PORTA->PCR[12] |= PORT_PCR_MUX(1) |
PORT_PCR_PS_MASK | PORT_PCR_PE_MASK |
PORT_PCR_IRQC(0x0b);
PORTA->PCR[13] |= PORT_PCR_MUX(1) |
PORT_PCR_PS_MASK | PORT_PCR_PE_MASK |
PORT_PCR_IRQC(0x0b);

PORTA->PCR[16] |= PORT_PCR_MUX(1) |
PORT_PCR_PS_MASK | PORT_PCR_PE_MASK |
PORT_PCR_IRQC(0x0b);
PORTA->PCR[17] |= PORT_PCR_MUX(1) |
PORT_PCR_PS_MASK | PORT_PCR_PE_MASK |
PORT_PCR_IRQC(0x0b);

/* Set port D switch bit to inputs */
PTD->PDDR &= ~MASK(2);
PTD->PDDR &= ~MASK(3);

PTD->PDDR &= ~MASK(4);
PTD->PDDR &= ~MASK(5);

PTD->PDDR &= ~MASK(6);
PTD->PDDR &= ~MASK(7);

PTA->PDDR &= ~MASK(12);
PTA->PDDR &= ~MASK(13);

PTA->PDDR &= ~MASK(16);
PTA->PDDR &= ~MASK(17);

/* Enable Interrupts */
NVIC_SetPriority(PORTD_IRQn, 128);
NVIC_ClearPendingIRQ(PORTD_IRQn);
NVIC_EnableIRQ(PORTD_IRQn);

NVIC_SetPriority(PORTA_IRQn, 0);
NVIC_ClearPendingIRQ(PORTA_IRQn);
NVIC_EnableIRQ(PORTA_IRQn);
}

void PORTD_IRQHandler(void) {
	
if ((PORTD->ISFR & MASK(2))) {
	
	//encoder1pinA high
	if(PTD->PDIR & MASK(2)){
		//encoder1pinB high
		if(PTD->PDIR & MASK(3)){
			enc_pos_2-=4;
		}
		//encoder1pinB low
		else{
			enc_pos_2+=4;
		}
	}
	
	//encoder1pinA low
	else{
		//encoder1pinB high
		if(PTD->PDIR & MASK(3)){
			enc_pos_2+=4;
		}
		//encoder1pinB low
		else{
			enc_pos_2-=4;
		}
	}
}


if ((PORTD->ISFR & MASK(3))) {
	//encoder1pinB high
	if(PTD->PDIR & MASK(3)){
		//encoder1pinA high
		if(PTD->PDIR & MASK(2)){
			enc_pos_2+=4;
		}
		//encoder1pinA low
		else{
			enc_pos_2-=4;
		}
	}
	
	//encoder1pinB low
	else{
		//encoder1pinA high
		if(PTD->PDIR & MASK(2)){
			enc_pos_2-=4;
		}
		//encoder1pinA low
		else{
			enc_pos_2+=4;
		}
	}
}

if ((PORTD->ISFR & MASK(4))) {
	
	//encoder1pinA high
	if(PTD->PDIR & MASK(4)){
		//encoder1pinB high
		if(PTD->PDIR & MASK(5)){
			enc_pos_3-=4;
		}
		//encoder1pinB low
		else{
			enc_pos_3+=4;
		}
	}
	
	//encoder1pinA low
	else{
		//encoder1pinB high
		if(PTD->PDIR & MASK(5)){
			enc_pos_3+=4;
		}
		//encoder1pinB low
		else{
			enc_pos_3-=4;
		}
	}
}


if ((PORTD->ISFR & MASK(5))) {
	
	//encoder1pinB high
	if(PTD->PDIR & MASK(5)){
		//encoder1pinA high
		if(PTD->PDIR & MASK(4)){
			enc_pos_3+=4;
		}
		//encoder1pinA low
		else{
			enc_pos_3-=4;
		}
	}
	
	//encoder1pinB low
	else{
		//encoder1pinA high
		if(PTD->PDIR & MASK(4)){
			enc_pos_3-=4;
		}
		//encoder1pinA low
		else{
			enc_pos_3+=4;
		}
	}
}





if ((PORTD->ISFR & MASK(6))) {
	
	//encoder1pinA high
	if(PTD->PDIR & MASK(6)){
		//encoder1pinB high
		if(PTD->PDIR & MASK(7)){
			enc_pos_4-=4;
		}
		//encoder1pinB low
		else{
			enc_pos_4+=4;
		}
	}
	
	//encoder1pinA low
	else{
		//encoder1pinB high
		if(PTD->PDIR & MASK(7)){
			enc_pos_4+=4;
		}
		//encoder1pinB low
		else{
			enc_pos_4-=4;
		}
	}
}


if ((PORTD->ISFR & MASK(7))) {
	
	//encoder1pinB high
	if(PTD->PDIR & MASK(7)){
		//encoder1pinA high
		if(PTD->PDIR & MASK(6)){
			enc_pos_4+=4;
		}
		//encoder1pinA low
		else{
			enc_pos_4-=4;
		}
	}
	
	//encoder1pinB low
	else{
		//encoder1pinA high
		if(PTD->PDIR & MASK(6)){
			enc_pos_4-=4;
		}
		//encoder1pinA low
		else{
			enc_pos_4+=4;
		}
	}
}

// clear status flags
PORTD->ISFR = 0xffffffff;

}

void PORTA_IRQHandler(void) {

if ((PORTA->ISFR & MASK(12))) {
	
	//encoder1pinA high
	if(PTA->PDIR & MASK(12)){
		//encoder1pinB high
		if(PTA->PDIR & MASK(13)){
			enc_pos_5-=4;
		}
		//encoder1pinB low
		else{
			enc_pos_5+=4;
		}
	}
	
	//encoder1pinA low
	else{
		//encoder1pinB high
		if(PTA->PDIR & MASK(13)){
			enc_pos_5+=4;
		}
		//encoder1pinB low
		else{
			enc_pos_5-=4;
		}
	}
}


if ((PORTA->ISFR & MASK(13))) {
	
	//encoder1pinB high
	if(PTA->PDIR & MASK(13)){
		//encoder1pinA high
		if(PTA->PDIR & MASK(12)){
			enc_pos_5+=4;
		}
		//encoder1pinA low
		else{
			enc_pos_5-=4;
		}
	}
	
	//encoder1pinB low
	else{
		//encoder1pinA high
		if(PTA->PDIR & MASK(12)){
			enc_pos_5-=4;
		}
		//encoder1pinA low
		else{
			enc_pos_5+=4;
		}
	}
}



if ((PORTA->ISFR & MASK(16))) {
	
	//encoder1pinA high
	if(PTA->PDIR & MASK(16)){
		//encoder1pinB high
		if(PTA->PDIR & MASK(17)){
			enc_pos_1-=4;
		}
		//encoder1pinB low
		else{
			enc_pos_1+=4;
		}
	}
	
	//encoder1pinA low
	else{
		//encoder1pinB high
		if(PTA->PDIR & MASK(17)){
			enc_pos_1+=4;
		}
		//encoder1pinB low
		else{
			enc_pos_1-=4;
		}
	}
}


if ((PORTA->ISFR & MASK(17))) {
	
	//encoder1pinB high
	if(PTA->PDIR & MASK(17)){
		//encoder1pinA high
		if(PTA->PDIR & MASK(16)){
			enc_pos_1+=4;
		}
		//encoder1pinA low
		else{
			enc_pos_1-=4;
		}
	}
	
	//encoder1pinB low
	else{
		//encoder1pinA high
		if(PTA->PDIR & MASK(12)){
			enc_pos_1-=4;
		}
		//encoder1pinA low
		else{
			enc_pos_1+=4;
		}
	}
}

	PORTA->ISFR = 0xffffffff;
}
