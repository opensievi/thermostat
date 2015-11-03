// OpenSievi Thermostat
// https://github.com/opensievi/thermostat
//
// Copyright (c) 2015 Tapio Salonsaari <take@nerd.fi>
// 
// This arudino project is to control diesel powered heaters. Temperature is read with 
// DS18B20 (or similar) sensor, heater is controlled via relay and use interface is 
// done with 16x2 LCD display and couple of buttons.
//
// See README.md and LICENSE.txt for more info
//

#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

// Software version and writing time
#define SWVERSION "1.1b"
#define SWDATE "10-15"

#define CONFIG_VERSION "OSTH1"
#define CONFIG_START 32

// Behaviour
#define BTN_DELAY 30 // 50ms delay for button to stop jitter
#define MENU_DELAY 10000 // 10seconds delay for menu to exit automatically
#define TIMERS 4 // Number of timers
#define BACKLIGHT_DELAY 1200000 // Backlight delay, 20minutes

// Pin layout
#define RELAY_PIN 2
#define DS_PIN 3
#define UPBTN_PIN 4
#define ENTBTN_PIN 5
#define DOWNBTN_PIN 6
#define TOGGLESW_PIN 7
#define LED1_PIN 13

// LCD used
#define LCD_WIDTH 16
#define LCD_HEIGHT 2
#define LCD_ADDRESS 0x27

// http://playground.arduino.cc/Code/EEPROMLoadAndSaveSettings
struct StoredConf {
	char confversion[6];
	unsigned long c_warmup_time;
	unsigned long c_cooldown_time;
	int c_low_temp;
	int c_high_temp;
} storage = {
	// Default values
	CONFIG_VERSION,
	900000,
	1200000,
	100,
	200
};

// Init subsystems
LiquidCrystal_I2C lcd(LCD_ADDRESS, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);
OneWire ds(DS_PIN);
DallasTemperature sensors(&ds);
DeviceAddress insideThermometer;

// There's quite a few phase tracking variables here,
// maybe we could take some of them off, maybe?
int btnPressed = 0;
int btnState = 0;
int btnLock = 0;
int textState = 0;
int bglState = 0;
int oneWireState = 0;

byte ow_addr[8];
int cur_menu_phase=0; // current menu phase(page)
int cur_menu_state=0; // 0 = display, 1 = edit..

int cur_phase=0; // current working phase
int cur_subphase=0; // some phases require some refining...

char cur_temp[20] = "wait...";
int f_cur_temp;

unsigned long timers[TIMERS]; // Countdown timers
unsigned long timer_millis; // Internal timer

int uptime[3]; // uptime calculator
int burntime[3]; // burntime calculator on current uptime
int timecalc; // Used to calculate spent time
unsigned long textTime = 2000;
unsigned long wait_timer; // Used to track warmup/cooldown times

// Warmup & cooldown timers are unsigned, so MAKE SURE that they
// won't loop over. If they go to negative the burntime goes to
// about 49 days instead of few minutes.

// Wait warmup_timer milliseconds for burner to warm up
// 900 000 = 15 minutes
//unsigned long warmup_time = 900000;
unsigned long warmup_time;

// Wait cooldown_timer milliseconds for burner to cool down
// 1 200 000 milliseconds = 20 minutes
//unsigned long cooldown_time = 1200000;
unsigned long cooldown_time;

// Start / stop temperatures
// We multiple temps by 10, so 25 degrees becomes
// 250 and so on, so we don't need to use floats
//int low_temp = 100;
//int high_temp = 200;
int low_temp;
int high_temp;

// Check that we actually have temperature sensor,
// and verify that we don't do anything unless
// we have one.
int findOneWire() {

	sensors.begin();
	
	while (!sensors.getAddress(insideThermometer, 0)) { 
		Serial.println("Unable to find address for Device 0");
		lcd.clear();
		lcd.home();
		lcd.print("No sensor address!");
		delay(1000);
	}

	sensors.setResolution(insideThermometer, 12);
	return 0;
}

// Read buttons. Current schema requires only that single button
// functions at a time so this will do.
int readButtons() {

	int bState = 0;
	int t_btnPressed = 0;
	int t_pressed = 0;
	
	bState = digitalRead(UPBTN_PIN);
	if(bState == HIGH) {
		t_btnPressed = 1;
		t_pressed = 1;
	}

	bState = digitalRead(ENTBTN_PIN);
	if(bState == HIGH) {
		t_btnPressed = 2;
		t_pressed = 1;
	}

	bState = digitalRead(DOWNBTN_PIN);
	if(bState == HIGH) {
		t_btnPressed = 3;
		t_pressed = 1;
	}

	if(t_pressed != btnState) {
		timers[0]=BTN_DELAY;
		btnState = t_pressed;
	}

	if(btnPressed != t_btnPressed) {
		
		if(timers[0] == 0) {
			btnPressed = t_btnPressed;
		} 
	}

	if(btnLock == btnPressed) {
		return 0;
	} else {
		btnLock = 0;
		return btnPressed;
	}
}

// Do timer calculations
void count_timers() {

	unsigned long diffMillis = millis() - timer_millis;
	timer_millis = millis();

	for(int i=0;i < TIMERS; i++) {
		
		if(timers[i] >= diffMillis) {
			timers[i] -= diffMillis;
		} else {
			timers[i] = 0;
		}
	}

	if(wait_timer >= diffMillis) {
		wait_timer -= diffMillis;
	} else {
		wait_timer = 0;
	}

	if(textTime >= diffMillis) {
		textTime -= diffMillis;
	} else {
		textTime = 0;
	}

	// Do time tracking calculations. This isn't millisecond
	// accurate, but should be close enough for this purpose.
	timecalc += diffMillis;
	if(timecalc >= 1000) {
		timecalc -= 1000;

		// If burner is on then update burntime as well
		if(cur_phase > 0 && cur_phase < 3) {
			burntime[2]++;

			if(burntime[2]>=60) {
				burntime[1]++;
				burntime[2]-=60;
			}

			if(burntime[1]>=60) {
				burntime[0]++;
				burntime[1]-=60;
			}
		}

		uptime[2]++;
		if(uptime[2]>=60) {
			uptime[1]++;
			uptime[2]-=60;
		}

		if(uptime[1]>=60) {
			uptime[0]++;
			uptime[1]-=60;
		}
	}
	
	return;
}	


void menu(int btn) {

	char buf[25];
	char buf2[25];

	if(cur_menu_state == 0) {

		switch(btn) {
	
			case 1:
				cur_menu_phase++;
				timers[3]=MENU_DELAY;
				break;

			case 2:
				cur_menu_phase--;
				timers[3]=MENU_DELAY;
				break;
		}
	} 

	// Return if there's nothing to do
	if(cur_menu_phase == 0) {
		return;
	}

	if(btn > 0) {
		timers[3] = MENU_DELAY;
	}

	// When timer reaches zero return to main menu
	if(timers[3] == 0) {
		cur_menu_state=0;
		cur_menu_phase=0;
	}

	// Keep menu phase in valid range (0..4) and
	// loop it over if necessary.
	if(cur_menu_phase < 0) {
		cur_menu_phase=5;
	}

	if(cur_menu_phase > 5) {
		cur_menu_phase=0;
	}

        // Manage menu. This code both shows the menu items and handles
	// changing of values.
        switch(cur_menu_phase) {

                case 1:
			if(cur_menu_state) {
				switch(btn) {
					
					case 1:
						low_temp+=10;
						if(low_temp > 600) {
							low_temp = 600;
						}
						break;
					case 2:
						low_temp-=10;
						if(low_temp < 0) {
							low_temp = 0;
						}
						break;
				}
			}
			sprintf(buf,"%-16s","Low temp limit");
			sprintf(buf2,"%-2d\337%-11s",low_temp/10," ");
                        break;

                case 2:
			if(cur_menu_state) {
				switch(btn) {
					case 1:
						high_temp+=10;
						if(high_temp > 600) {
							high_temp = 600;
						}
						break;
					case 2:
						high_temp-=10;
						if(high_temp < 0) {
							high_temp = 0;
						}
						break;
				}
			}

			sprintf(buf,"%-16s","High temp limit");
			sprintf(buf2,"%-2d\337%11s",high_temp/10," ");
                        break;

                case 3:
			if(cur_menu_state) {
				switch(btn) {
					case 1:
						warmup_time += 60000;
						if(warmup_time > 3600000) {
							warmup_time = 3600000;
						}
						break;
					case 2:
						if(warmup_time > 60000) {
							warmup_time -= 60000;
						} else {
							warmup_time = 0;
						}
						break;
				}
			}
			sprintf(buf,"%-16s","Warmup burn time");
			sprintf(buf2,"%-2lumin%9s",warmup_time/1000/60," ");
                        break;

                case 4:
			if(cur_menu_state) {
				switch(btn) {
					case 1:
						cooldown_time += 60000;
						// Millisecond times. 3.6e6ms == 1 hour
						if(cooldown_time > 3600000) {
							cooldown_time = 3600000;
						}
						break;
					case 2:
						if(cooldown_time > 60000) {
							cooldown_time -= 60000;
						} else {
							cooldown_time = 0;
						}
						break;
				}
			}
			sprintf(buf,"%-16s","Cooldown time");
			sprintf(buf2,"%-2lumin%9s",cooldown_time/1000/60," ");
                        break;

		case 5:
			sprintf(buf,"%-16s", "Save values");
			if(cur_menu_state) {
				// Save config here, we just halt menu
				// for at this point. This causes MENU_DELAY 
				// (10seconds) lock to operations, but
				// we don't need to save values often anyways
				sprintf(buf2,"%-16s","Writing EEPROM");
				
				// We need to update variables on storage struct at
				// this point. Might be more convinient to actually
				// use conf variables directly, but this will do for
				// now.
				storage.c_warmup_time = warmup_time;
				storage.c_cooldown_time = cooldown_time;
				storage.c_low_temp = low_temp;
				storage.c_high_temp = high_temp;
				for (unsigned int t=0; t<sizeof(storage); t++) {
					EEPROM.write(CONFIG_START + t, *((char*)&storage + t));
				}
			} else {
				sprintf(buf2,"%-16s", "Press enter.");
			}
			break;

        }

	// Pad second line
	if(cur_menu_state) {
		sprintf(buf2,"%s<-",buf2);
	} else {
		sprintf(buf2,"%s  ",buf2);
	}

	lcd.setCursor(0,0);
	lcd.print(buf);
	lcd.setCursor(0,1);
	lcd.print(buf2);
        return;
}


// Read sensor data and store it in string to cur_temp char[]
// Uses timers[1] to create non-blocking environment
void ReadOneWire() {

	if(timers[1] == 0 && oneWireState == 0) {

		Serial.println("Requesting temperatures from 1wire bus");
		sensors.requestTemperatures();
		oneWireState = 1;
		timers[1] = 1000;
	}

	if(timers[1] == 0 && oneWireState == 1) {

		float cTemp;
		int temp100;
		int Whole, Fract, tFract;
		int positive=0;
		oneWireState = 0;
		timers[1] = 2500;

		Serial.print("Reading data from 1wire...");
		cTemp = sensors.getTempCByIndex(0);
		Serial.println(cTemp);

		if(cTemp > 0) {
			positive=1;
		}

		//sprintf(cur_temp, "%.1f\337", cTemp);
		Whole = int(cTemp);
		Fract = int((cTemp - Whole) * 100);
		tFract = Fract - (Fract/10)*10;
		Fract = Fract/10;
		if(tFract > 4)
			Fract++;

		f_cur_temp = Whole*10 + Fract;
		sprintf(cur_temp, "%c%2d.%1d\337 ",positive ? '+' : '-', Whole, Fract);
	}

	return;
}

void changePhase() {
	int togglestate;
	togglestate = digitalRead(TOGGLESW_PIN);
	
	switch(cur_phase) {
		case 0: // Standby
			if(cur_subphase == 0) {
				digitalWrite(LED1_PIN, LOW);
				cur_subphase++;
			}
			// Switch to warmup
			if(togglestate == HIGH) {
				timers[2] = BACKLIGHT_DELAY; 
				digitalWrite(LED1_PIN, HIGH);
				cur_phase=4;
				cur_subphase=0;
			}
			break;
		case 1: // Warmup
			// Change state ONLY if we aren't
			// in warmup state

			// Start burner	and reset timer
			if(cur_subphase == 0) {
				wait_timer = warmup_time;
				digitalWrite(RELAY_PIN, LOW); 
				cur_subphase = 1;
			}

			if(wait_timer == 0 && cur_subphase == 1) {
				cur_subphase=0;
				if(togglestate == HIGH) {
					cur_phase=2;
				} else {
					// If we turned heating off during
					// warmup phase move to cooldown
					wait_timer = cooldown_time;
					cur_phase=3;
					digitalWrite(RELAY_PIN, HIGH);
				}
			}
			break;
		case 2: // Warming phase

			// If switch is turned off then switch instantly to cooldown mode
			// or switch to cooldown until temperature has risen enough
			if(togglestate != HIGH || f_cur_temp >= high_temp) {
				wait_timer = cooldown_time;
				cur_phase = 3;

				// Turn heater off
				digitalWrite(RELAY_PIN, HIGH);
			}

			break;
		case 3: // Cooldown phase

			// We stand by no matter what's the temperature
			if(wait_timer == 0) {
				cur_phase = 4;
			}
			break;
		case 4: // Wait for temperature to drop

			if(f_cur_temp <= low_temp) {
				// Go to warm up cycle
				cur_phase = 1;
			}

			if(togglestate != HIGH) {
				cur_phase = 0;
				cur_subphase = 0;
			}
			break;
		default:
			// Something goes BADLY wrong if this
			// ever happens. We go to infinite loop
			// and shut everything down.
			char buf[20];
			digitalWrite(RELAY_PIN,HIGH); // Shut off burner
			while(1) {
				lcd.home();
				lcd.print("PHASE ERROR            ");
				lcd.setCursor(0,1);
				sprintf(buf,"Phase: %d              ",cur_phase);
				lcd.print(buf);
				delay(10000);
			}
			break;
	}
}

// Function: Setup
//
// Initialize subsystems and start main loop
//
void setup()
{
	char version[20];	

	Serial.begin(9600);

	pinMode(UPBTN_PIN, INPUT);
	pinMode(ENTBTN_PIN, INPUT);
	pinMode(DOWNBTN_PIN, INPUT);
	pinMode(TOGGLESW_PIN, INPUT);
	pinMode(RELAY_PIN, OUTPUT);
	pinMode(LED1_PIN, OUTPUT);

	// Initialize timers
	for(int i=0;i < TIMERS;i++) {
		timers[i] = 0;
	}

	// Initialize time calculators
	for(int i=0; i <= 2; i++) {
		uptime[i]=0;
		burntime[i]=0;
	}

	// Our relay pulls when RELAY_PIN is LOW, this is somewhat
	// inconvinient, but it should work out just fine
	digitalWrite(RELAY_PIN, HIGH);
	digitalWrite(LED1_PIN, LOW);

	// Read configuration from EEPROM, or if they're
	// not found use default values
	if (	EEPROM.read(CONFIG_START + 0) == CONFIG_VERSION[0] &&
		EEPROM.read(CONFIG_START + 1) == CONFIG_VERSION[1] &&
		EEPROM.read(CONFIG_START + 2) == CONFIG_VERSION[2]) {

		Serial.println("Reading EEPROM");
		for (unsigned int t=0; t<sizeof(storage); t++) {
		      *((char*)&storage + t) = EEPROM.read(CONFIG_START + t);
		      Serial.println(*((char*)&storage + t));
		}
	}
	// Move values from configuration to actual variables used.
	// This is a bit useless step, since we could use variables
	// directly from configuration, but at this point I won't fix
	// this
	warmup_time = storage.c_warmup_time;
	cooldown_time = storage.c_cooldown_time;
	low_temp = storage.c_low_temp;
	high_temp = storage.c_high_temp;

	lcd.begin(LCD_WIDTH, LCD_HEIGHT);               // initialize the lcd 
	lcd.home ();                   // go home

	// Output some version information on startup
	lcd.print("O-S Thermostat");  
	lcd.setCursor ( 0, 1 );        // go to the next line
	sprintf(version, "ver %s %s", SWVERSION, SWDATE);
	lcd.print (version);
	findOneWire();
	ReadOneWire();
	delay ( 500 );

	lcd.clear();

	timers[2] = BACKLIGHT_DELAY;
	bglState = 1;
	return;
}




void loop() 
{
	char buf[20];
	char buf2[17];
	char phasetxt[10]="WAIT";

	count_timers(); // Manage time calculations
	ReadOneWire(); // Read temperature sensor

	// Run phase changes if necessary, this is
	// EXTREMELY important, since if phases won't
	// change as expected the whole thing is going to 
	// break.
	changePhase(); 

	int btn = readButtons();
	// Set button lock so we have to release button 
	// before taking another action. This is somewhat
	// inconvinient, but will do for now.
	if(btn > 0) {
		// Update backlight delay on every button press
		timers[2] = BACKLIGHT_DELAY;
		btnLock = btn;
	}

	// Shut down backlight if necessary
	if(bglState == 1 && timers[2] == 0) {
		lcd.noBacklight();
		bglState = 0;
	}

	if(bglState == 0 && timers[2] > 0) {
		// If backlight is off then do nothing, just 
		// start backlight.
		lcd.backlight();
		bglState = 1;
		return;
	}

	// Enter edit mode
	if(cur_menu_phase > 0 && btn == 3) {
		if(cur_menu_state == 1) {
			cur_menu_state = 0;
		} else {
			cur_menu_state = 1;
		}
		return;
	}

	menu(btn);
	// Don't display running data over menu
	if(cur_menu_phase != 0) {
		return;
	}

	// Display phase texts
	int tminus = (wait_timer / 1000) / 60;
	int whole;
	int fract;

	if(textState == 1) {
		sprintf(buf,"Up: %2d:%02d:%02d    ",uptime[0],uptime[1],uptime[2]);
	} else if(textState == 2) {
		sprintf(buf,"Burn: %2d:%02d:%02d  ",burntime[0],burntime[1],burntime[2]);
	} 
	if( textTime == 0) {
		textState++;
		textTime = 5000;
		if(textState > 2) {
			textState = 0;
		}
	}
	switch(cur_phase) {
		case 0:
			sprintf(phasetxt, "%s", "STDBY");
			if(textState == 0) {
				sprintf(buf,"Standby...      ");
			}
			break;
		case 1:
			sprintf(phasetxt,"%s", "WARMUP");
			if(textState == 0) {
				sprintf(buf,"Warming, T-%dm     ", tminus);
			}
			break;
		case 2:
			sprintf(phasetxt, "%s", "WARMING");
			whole = high_temp / 10;
			fract = high_temp % 10;
			if(textState == 0) {
				sprintf(buf,"Heating to %2d.%d\337",whole,fract);
			}
			break;
		case 3:
			sprintf(phasetxt,"%s", "COOLDOWN");
			if(textState == 0) {
				sprintf(buf,"Cooldown, T-%dm     ", tminus);
			}
			break;
		case 4:
			sprintf(phasetxt, "%s", "COOLING");
			whole = low_temp / 10;
			fract = low_temp % 10;
			if(textState == 0) {
				sprintf(buf,"Cooling to %2d.%d\337",whole,fract);
			}
			break;
	}

	sprintf(buf2, "%-6s%9s", cur_temp, phasetxt);
	lcd.setCursor(0,0);
	lcd.print(buf2);

	lcd.setCursor(0,1);
	lcd.print(buf);


}




