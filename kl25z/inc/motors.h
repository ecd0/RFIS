/*
  Robotic Foram Imaging System - ECE 485, Spring 2018

  Info:
    motors.h
      Motor data definition for stepper motors w/ encoder feedback

  Authors:
		Eric Davis - ecdavis@ncsu.edu

  Apr 27 2018 - Eric
	  v1
*/

#ifndef MOTORS_H
#define MOTORS_H

#define STEPS_PER_REV (200) //for all our steppers

typedef struct __motor__t
{
  int16_t  pos;          //last known position
  int16_t  target;       //goal position in steps
  uint16_t used_steps;   //num steps taken toward target
  uint16_t error;        //num missed steps (as reported by encoder)
  uint16_t max_error;    //trigger level for error condition
  uint16_t extra_steps;  //extra steps available to correct error (should always be < max_error)
	uint16_t period_ticks; //motor step period in number of timer ticks (i.e. modulo value)
	uint8_t  microsteps;   //divider (microstep setting, 16 for pin steppers)
} motor_t;

extern motor_t motors[5];

#endif