# Open Sievi Thermostat

This arudino project is to control diesel powered
heaters. Temperature is read with DS18B20 (or similar) sensor, 
heater is controlled via relay and use interface is done
with 16x2 LCD display and couple of buttons.

# External libraries

- OneWire
- LiquidCrystal_I2C

# Basic program loop

Basically program runs on infinite loop trough couple of
different stages. Some exceptions will be made via UI, but
this is the basic structure:

1. Wait for start temperature
  - When temperature drops to start temperature pull the relay
2. Keep relay on until warmup time is gone
  - Diesel heaters don't like if you turn them continously on/off
3. Wait for target temperature
  - Keep heater on until target temperature is reached
4. Keep relay off until cooldown time is gone
  - Again, heaters don't like if you turn them continously on/off
5. Go to step 1

# Phases

Current setup on working phases

0. Idle, thermostat off, no burn (obviously)
1. Warmup timer, burn on
2. Wait for target temp, burn on
3. Cooldown timer, burn off
4. Wait for target temp, burn off

# Timers

Program runs various timers, stored on timers -array in milliseconds.
There's quite a few of them, so some documentation is necessary.

0. Button delay
  - Used to remove jittering from switches
1. OneWire read delay
  - 1Wire sensors require 750-1000ms delay from read command to process
    read. This is non-blocking counter for that
2. Backlight delay
  - When this hits 0 disable backlight from display
3. Menu delay
  - Counts down couple of seconds and returns UI from menu if user doesn't
    do anything
   

# ToDo

- Error handling when 1Wire sensor doesn't work
- User interface (menu, current phase, etc)
- Relay control
- LCD backlight control
- Different phases
- Lots of other things...

# Version history

(We'll need some kind of roadmap tool)

## 0.1

- LCD control works
- 1Wire reading works

## 0.5

- Basic thermostat functionality works

