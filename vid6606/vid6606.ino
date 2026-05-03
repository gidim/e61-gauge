/*
  VID6606 stepper driver sweep demo using AccelStepper library
  Adapted from STI6606z example code for VID6606 driver
  Sweeps the needle back and forth with smooth acceleration
*/

#include <AccelStepper.h>
#include <elapsedMillis.h>

// Motor Connections for VID6606 driver
const int enabPin = 2;  // RESET/ENABLE pin
const int dirPin = 3;   // DIR pin
const int stepPin = 4;  // STEP pin

// Create stepper instance (step/direction driver)
AccelStepper myStepper(AccelStepper::DRIVER, stepPin, dirPin);

elapsedMillis sweepTime;
const int sweepInterval = 2000;   // sweep interval in milliseconds

const long STEPS_PER_REVOLUTION = 315 * 12;   // for 315 degrees with twelve step microstepping
const long SWEEP_RANGE = 3000;    // sweep range in steps

bool sweepingUp = true;

void setup() {
  pinMode(enabPin, OUTPUT);
  digitalWrite(enabPin, LOW);
  delay(100);
  digitalWrite(enabPin, HIGH);    // enable the controller

  myStepper.setMaxSpeed(2000);
  myStepper.setAcceleration(2400);

  // Zero the motor
  myStepper.runToNewPosition(-4000);   // run backwards to find zero
  myStepper.setCurrentPosition(0);

  // Initial position
  myStepper.runToNewPosition(100);
  delay(500);

  sweepTime = 0;
}

void loop() {
  // Check if it's time to change sweep direction
  if (sweepTime >= sweepInterval) {
    sweepTime = 0;

    if (sweepingUp) {
      myStepper.moveTo(SWEEP_RANGE);
      sweepingUp = false;
    } else {
      myStepper.moveTo(100);
      sweepingUp = true;
    }
  }

  // Always run the stepper (handles acceleration automatically)
  myStepper.run();
}
