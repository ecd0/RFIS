#ifndef MOTORS_H
#define MOTORS_H

typedef struct __motor__t
{
  int16_t pos; //last known position
  int16_t target; //goal position in steps
  uint16_t used_steps; //num steps taken toward target
  uint16_t error; //num missed steps (as reported by encoder)
  uint16_t max_error; //trigger level for error condition
  uint16_t extra_steps; //extra steps available to correct error (should always be < max_error)
} motor_t;

#endif