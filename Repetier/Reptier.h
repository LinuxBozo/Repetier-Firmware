/*
    This file is part of Repetier-Firmware.

    Repetier-Firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Foobar is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Foobar.  If not, see <http://www.gnu.org/licenses/>.

    This firmware is a nearly complete rewrite of the sprinter firmware
    by kliment (https://github.com/kliment/Sprinter)
    which based on Tonokip RepRap firmware rewrite based off of Hydra-mmm firmware.
*/

#include <WProgram.h>
#include "gcode.h"
#include "fastio.h"
#ifdef SDSUPPORT
#include "Sd2Card.h"
#include "SdFat.h"
extern void initsd();
#endif

#define uint uint16_t
#define uint8 uint8_t
#define int8 int8_t
#define uint32 uint32_t
#define int32 int32_t

/** \brief Data to drive one extruder.

This structure contains all definitions for an extruder and all
current state variables, like current temperature, feeder position etc.
*/
typedef struct { // Size: 12*1 Byte+12*4 Byte+4*2Byte = 68 Byte
  byte id;
  long xOffset;
  long yOffset;
  float stepsPerMM; ///< Steps per mm.
  byte sensorType; ///< Type of temperature sensor.
  byte sensorPin; ///< Pin to read extruder temperature.
  byte heaterPin; ///< Pin to enable the heater.
  byte enablePin; ///< Pin to enable extruder stepper motor.
  byte directionPin; ///< Pin number to assign the direction.
  byte stepPin; ///< Pin number for a step.
  byte enableOn;
  byte invertDir; ///< 1 if the direction of the extruder should be inverted.
  float maxFeedrate;
  float maxAcceleration; ///< Maximum acceleration in mm/s^2.
  float maxStartFeedrate; ///< Maximum start feedrate in mm/s.
  long extrudePosition; ///< Current extruder position in steps.
  int currentTemperature; ///< Currenttemperature value read from sensor.
  int targetTemperature; ///< Target temperature value in units of sensor.
  int currentTemperatureC; ///< Current temperature in °C.
  int targetTemperatureC; ///< Target temperature in °C.
  long lastTemperatureUpdate; ///< Time in millis of the last temperature update.
  byte heatManager; ///< How is temperature controled. 0 = on/off, 1 = PID-Control
  int watchPeriod; ///< Time in seconds, a M109 command will wait to stabalize temperature
  float advanceK; ///< Koefficient for advance algorithm. 0 = off
#ifdef TEMP_PID
  long tempIState; ///< Temp. var. for PID computation.
  byte pidDriveMax; ///< Used for windup in PID calculation.
  long pidPGain; ///< Pgain (proportional gain) for PID temperature control [0,01 Units].
  long pidIGain; ///< Igain (integral) for PID temperature control [0,01 Units].
  long pidDGain;  ///< Dgain (damping) for PID temperature control [0,01 Units].
  byte pidMax; ///< Maximum PWM value, the heater should be set.
#endif
} Extruder;


#if HEATED_BED_SENSOR_TYPE!=0
extern int current_bed_raw;
extern int target_bed_raw;
#endif
#ifdef USE_ADVANCE
extern int maxadv;
extern float maxadvspeed;
#endif

extern Extruder *current_extruder;
extern Extruder extruder[];
// Initalize extruder and heated bed related pins
extern void initExtruder();
extern void extruder_select(byte ext_num);
// Set current extruder position
//extern void extruder_set_position(float pos,bool relative);
// set the temperature of current extruder
extern void extruder_set_temperature(int temp_celsius);
extern int extruder_get_temperature();
// Set temperature of heated bed
extern void heated_bed_set_temperature(int temp_celsius);
//extern long extruder_steps_to_position(float value,byte relative);
extern void extruder_set_direction(byte steps);
extern void extruder_disable();
/** \brief Sends the high-signal to the stepper for next extruder step. 

Call this function only, if interrupts are disabled.
*/
inline void extruder_step() {
#if NUM_EXTRUDER==1
  WRITE(EXT0_STEP_PIN,HIGH);
#else
  digitalWrite(current_extruder->stepPin,HIGH);
#endif
}
/** \brief Sets stepper signal to low for current extruder. 

Call this function only, if interrupts are disabled.
*/
inline void extruder_unstep() {
#if NUM_EXTRUDER==1
  WRITE(EXT0_STEP_PIN,LOW);
#else
  digitalWrite(current_extruder->stepPin,LOW);
#endif
}
/** \brief Activates the extruder stepper and sets the direction. */
inline void extruder_set_direction(byte dir) {  
#if NUM_EXTRUDER==1
  if(dir)
    WRITE(EXT0_DIR_PIN,!EXT0_INVERSE);
  else
    WRITE(EXT0_DIR_PIN,EXT0_INVERSE);
#else
  digitalWrite(current_extruder->directionPin,dir!=0 ? (!(current_extruder->invertDir)) : current_extruder->invertDir);
#endif
}
inline void extruder_enable() {
#if NUM_EXTRUDER==1
#if EXT0_ENABLE_PIN>-1
    WRITE(EXT0_ENABLE_PIN,EXT0_ENABLE_ON ); 
#endif
#else
  if(current_extruder->enablePin > -1) 
    digitalWrite(current_extruder->enablePin,current_extruder->enableOn); 
#endif
}
// Read a temperature and return its value in °C
// this high level method supports all known methods
extern int read_raw_temperature(byte type,byte pin);
extern int heated_bed_get_temperature();
// Convert a raw temperature value into °C
extern int conv_raw_temp(byte type,int raw_temp);
// Converts a temperture temp in °C into a raw value
// which can be compared with results of read_raw_temperature
extern int conv_temp_raw(byte type,int temp);
// Updates the temperature of all extruders and heated bed if it's time.
// Toggels the heater power if necessary.
extern void manage_temperatures(bool critical);

extern byte manage_monitor;

void process_command(GCode *code);

void manage_inactivity(byte debug);

extern void update_ramps_parameter();
extern void finishNextSegment();
extern byte get_coordinates(GCode *com);
extern void queue_move(byte check_endstops);
extern void linear_move(long steps_remaining[]);
extern inline void disable_x();
extern inline void disable_y();
extern inline void disable_z();
extern inline void enable_x();
extern inline void enable_y();
extern inline void enable_z();

extern void kill(byte only_steppers);

extern float axis_steps_per_unit[];
extern float inv_axis_steps_per_unit[];
extern float max_feedrate[];
extern float homing_feedrate[];
extern float max_start_speed_units_per_second[];
extern long max_acceleration_units_per_sq_second[];
extern long max_travel_acceleration_units_per_sq_second[];
extern unsigned long axis_steps_per_sqr_second[];
extern unsigned long axis_travel_steps_per_sqr_second[];
extern byte relative_mode;    ///< Determines absolute (false) or relative Coordinates (true).
extern byte relative_mode_e;  ///< Determines Absolute or Relative E Codes while in Absolute Coordinates mode. E is always relative in Relative Coordinates mode.

extern byte unit_inches;
extern unsigned long previous_millis_cmd;
extern unsigned long max_inactive_time;
extern unsigned long stepper_inactive_time;

extern void setupTimerInterrupt();

typedef struct { // RAM usage: 72 Byte
  byte timer0Interval;              ///< Update interval of timer0 compare
  long interval;                    ///< Last step duration in ticks.
#if USE_OPS==1
  bool filamentRetracted;           ///< Is the extruder filament retracted
#endif
  volatile int extruderStepsNeeded; ///< This many extruder steps are still needed, <0 = reverse steps needed.
  unsigned long timer;              ///< used for acceleration/deceleration timing
  unsigned long stepNumber;         ///< Step number in current move.
#ifdef USE_ADVANCE
  long advance_executed;             ///< Executed advance steps
  int advance_steps_set;
#endif
  long currentPositionSteps[4];     ///< Position in steps from origin.
  long destinationSteps[4];         ///< Target position in steps.
  float extruderSpeed;              ///< Extruder speed in mm/s.
#if USE_OPS==1
  int opsRetractSteps;              ///< Retract filament this much steps
  int opsPushbackSteps;             ///< Retract+extra distance for backslash
  float opsMinDistance;
  float opsRetractDistance;
  float opsRetractBackslash;
  byte opsMode;                     ///< OPS operation mode. 0 = Off, 1 = Classic, 2 = Fast
  float opsMoveAfter;               ///< Start move after opsModeAfter percent off full retract.
  int opsMoveAfterSteps;            ///< opsMoveAfter converted in steps (negative value!).
#endif
  long xMaxSteps;                   ///< For software endstops, limit of move in positive direction.
  long yMaxSteps;                   ///< For software endstops, limit of move in positive direction.
  long zMaxSteps;                   ///< For software endstops, limit of move in positive direction.
  float feedrate;                   ///< Last requested feedrate.
  float maxJerk;                    ///< Maximum allowed jerk in mm/s
  float maxZJerk;                   ///< Maximum allowed jerk in z direction in mm/s
  long offsetX;                     ///< X-offset for different extruder positions.
  long offsetY;                     ///< Y-offset for different extruder positions.
} PrinterState;
extern PrinterState printer_state;

/** Marks the first step of a new move */
#define FLAG_WARMUP 1
#define FLAG_ACCELERATING 2
#define FLAG_DECELERATING 4
#define FLAG_ACCELERATION_ENABLED 8
#define FLAG_CHECK_ENDSTOPS 16
#define FLAG_SKIP_ACCELERATING 32
#define FLAG_SKIP_DEACCELERATING 64
#define FLAG_BLOCKED 128

/** Are the step parameter computed */
#define FLAG_JOIN_STEPPARAMS_COMPUTED 1
/** The right speed is fixed. Don't check this block or any block to the left. */
#define FLAG_JOIN_END_FIXED 2
/** The left speed is fixed. Don't check left block. */
#define FLAG_JOIN_START_FIXED 4
/** Start filament retraction at move start */
#define FLAG_JOIN_START_RETRACT 8
/** Wait for filament pushback, before ending move */
#define FLAG_JOIN_END_RETRACT 16
/** Disable retract for this line */
#define FLAG_JOIN_NO_RETRACT 32
/** Wait for the extruder to finish it's up movement */
#define FLAG_JOIN_WAIT_EXTRUDER_UP 64
/** Wait for the extruder to finish it's down movement */
#define FLAG_JOIN_WAIT_EXTRUDER_DOWN 128
// Printing related data
typedef struct { // RAM usage: 24*4+15 = 111 Byte
  byte primaryAxis;
  byte flags;
  byte joinFlags;
  byte halfstep;                  ///< 0 = disabled, 1 = halfstep, 2 = fulstep
  byte dir;                       ///< Direction of movement. 1 = X+, 2 = Y+, 4= Z+, values can be combined.
  long delta[4];                  ///< Steps we want to move.
  long error[4];                  ///< Error calculation for Bresenham algorithm
  float speedX;                   ///< Speed in x direction at fullInterval in mm/s
  float speedY;                   ///< Speed in y direction at fullInterval in mm/s
  float speedZ;                   ///< Speed in z direction at fullInterval in mm/s
  float fullSpeed;                ///< Desired speed mm/s
  float acceleration;             ///< Real acceleration mm/s²
  float distance;
  float startFactor;
  float endFactor;
  unsigned long fullInterval;     ///< interval at full speed in ticks/step.
  unsigned long stepsRemaining;   ///< Remaining steps, until move is finished
  unsigned int accelSteps;        ///< How much steps does it take, to reach the plateau.
  unsigned int decelSteps;        ///< How much steps does it take, to reach the end speed.
  unsigned long accelerationPrim; ///< Acceleration along primary axis
  unsigned long facceleration;    ///< accelerationPrim*262144/F_CPU
  unsigned int vMax;              ///< Maximum reached speed in steps/s.
  unsigned int vStart;            ///< Starting speed in steps/s.
  unsigned int vEnd;              ///< End speed in steps/s
#ifdef USE_ADVANCE
  long advanceRate;               ///< Advance steps at full speed
  long advanceFull;               ///< Maximum advance at fullInterval [steps*65536]
  long advanceStart;
  long advanceEnd;
#endif
#if USE_OPS==1
  long opsReverseSteps;           ///< How many steps are needed to reverse retracted filament at full speed
#endif
} PrintLine;

extern PrintLine lines[];
extern byte lines_write_pos; // Position where we write the next cached line move
extern byte lines_pos; // Position for executing line movement
extern volatile byte lines_count; // Number of lines cached 0 = nothing to do
extern byte printmoveSeen;
extern long baudrate;
#if OS_ANALOG_INPUTS>0
// Get last result for pin x
extern volatile uint osAnalogInputValues[OS_ANALOG_INPUTS];
#endif
#define BEGIN_INTERRUPT_PROTECTED {byte sreg=SREG;__asm volatile( "cli" ::: "memory" );
#define END_INTERRUPT_PROTECTED SREG=sreg;}
#define ESCAPE_INTERRUPT_PROTECTED SREG=sreg;

#define SECONDS_TO_TICKS(s) (unsigned long)(s*(float)F_CPU)
extern long CPUDivU2(unsigned int divisor);

#ifdef SDSUPPORT
extern Sd2Card card; // ~14 Byte
extern SdVolume volume;
extern SdFile root;
extern SdFile file;
extern uint32_t filesize;
extern uint32_t sdpos;
extern bool sdmode;
extern bool sdactive;
extern bool savetosd;
extern int16_t n;

#endif

#if MOTHERBOARD==6 || MOTHERBOARD==62
#define EXTRUDER_TIMER_VECTOR TIMER2_COMPA_vect
#define EXTRUDER_OCR OCR2A
#define EXTRUDER_TCCR TCCR2A
#define EXTRUDER_TIMSK TIMSK2
#define EXTRUDER_OCIE OCIE2A
#else
#define EXTRUDER_TIMER_VECTOR TIMER0_COMPA_vect
#define EXTRUDER_OCR OCR0A
#define EXTRUDER_TCCR TCCR0A
#define EXTRUDER_TIMSK TIMSK0
#define EXTRUDER_OCIE OCIE0A
#endif

