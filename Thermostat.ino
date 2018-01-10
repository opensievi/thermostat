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

#include "LiquidCrystal_I2C/LiquidCrystal_I2C.h"
#include "PinChangeInt/PinChangeInt.h"
#include <EEPROM.h>
#include <DHT.h>

// Software version and writing time
#define SWVERSION "1.5"
#define SWDATE "12-17"

#define CONFIG_VERSION "OSTH1"
#define CONFIG_START 32

// Behaviour
#define BTN_DELAY 30 // 50ms delay for button to stop jitter
#define MENU_DELAY 10000 // 10seconds delay for menu to exit automatically
#define TIMERS 4 // Number of timers
#define BACKLIGHT_DELAY 1200000 // Backlight delay, 20minutes

// Pin layout
#define RELAY_PIN 12
#define DHT_PIN 5
#define TOGGLESW_PIN 6
#define ROTARYBTN_PIN 7
#define ROTARYFWD_PIN 8
#define ROTARYRWD_PIN 9
#define LED1_PIN 11
#define LED2_PIN 10
#define BUTTONS 1


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
DHT dht;


// There's quite a few phase tracking variables here,
// maybe we could take some of them off, maybe?
int btn_queue[BUTTONS];
int rotary_count=0;

int textState = 0;
int bglState = 0;
int tempSensorState = 0;

int cur_menu_phase=0; // current menu phase(page)
int cur_menu_state=0; // 0 = display, 1 = edit..

int cur_phase=0; // current working phase
int cur_subphase=0; // some phases require some refining...

char cur_temp[20] = "wait...";
int f_cur_humidity;
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
int dhtSamplingPeriod;

// Interrupt function to track down buttons
void trigger_button() {

        int cur_button = PCintPort::arduinoPin;

        switch (cur_button) {

                case ROTARYBTN_PIN:
                        btn_queue[0]++;
                        break;

                default:
                        // We don't have this button?
                        break;
        }

}

// Interrupt trigger for rotary button, we only
// track the last direction it was turned
void rotary_trigger() {

        int pinA = digitalRead(ROTARYFWD_PIN);
        int pinB = digitalRead(ROTARYRWD_PIN);

	if(rotary_count != 0) {
		return;
	}

	btn_queue[0]=0;

        if(pinA != pinB) {
                rotary_count=-1;
        } else {
                rotary_count=1;
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


void menu() {

	char buf[25];
	char buf2[25];

	if(cur_menu_state == 0) {

		switch(rotary_count) {
	
			case 1:
				cur_menu_phase++;
				timers[3]=MENU_DELAY;
				break;

			case -1:
				cur_menu_phase--;
				timers[3]=MENU_DELAY;
				break;
		}
		rotary_count = 0;
	} 

	// Return if there's nothing to do
	if(cur_menu_phase == 0) {
		return;
	}

	if(rotary_count != 0) {
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
				switch(rotary_count) {
					
					case 1:
						low_temp+=10;
						if(low_temp > 600) {
							low_temp = 600;
						}
						break;
					case -1:
						low_temp-=10;
						if(low_temp < 0) {
							low_temp = 0;
						}
						break;
				}
				rotary_count = 0;
			}
			sprintf(buf,"%-16s","Low temp limit");
			sprintf(buf2,"%-2d\337%-11s",low_temp/10," ");
                        break;

                case 2:
			if(cur_menu_state) {
				switch(rotary_count) {
					case 1:
						high_temp+=10;
						if(high_temp > 600) {
							high_temp = 600;
						}
						break;
					case -1:
						high_temp-=10;
						if(high_temp < 0) {
							high_temp = 0;
						}
						break;
				}
				rotary_count = 0;
			}

			sprintf(buf,"%-16s","High temp limit");
			sprintf(buf2,"%-2d\337%11s",high_temp/10," ");
                        break;

                case 3:
			if(cur_menu_state) {
				switch(rotary_count) {
					case 1:
						warmup_time += 60000;
						if(warmup_time > 3600000) {
							warmup_time = 3600000;
						}
						break;
					case -1:
						if(warmup_time > 60000) {
							warmup_time -= 60000;
						} else {
							warmup_time = 0;
						}
						break;
				}
				rotary_count = 0;
			}
			sprintf(buf,"%-16s","Warmup burn time");
			sprintf(buf2,"%-2lumin%9s",warmup_time/1000/60," ");
                        break;

                case 4:
			if(cur_menu_state) {
				switch(rotary_count) {
					case 1:
						cooldown_time += 60000;
						// Millisecond times. 3.6e6ms == 1 hour
						if(cooldown_time > 3600000) {
							cooldown_time = 3600000;
						}
						break;
					case -1:
						if(cooldown_time > 60000) {
							cooldown_time -= 60000;
						} else {
							cooldown_time = 0;
						}
						break;
				}
				rotary_count = 0;
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
void ReadDHT() {

	if(timers[1] == 0 && tempSensorState == 0) {

		Serial.println("Requesting temperatures from 1wire bus");
		tempSensorState = 1;
		timers[1] = dhtSamplingPeriod;
	}

	if(timers[1] == 0 && tempSensorState == 1) {

		float cTemp;
		int Whole, Fract, tFract;
		int positive=0;
		tempSensorState = 0;
		timers[1] = 2500;

		Serial.print("Reading data from 1wire...");
		int t_led=0;
		while(strcmp(dht.getStatusString(),"OK") != 0) {
			digitalWrite(RELAY_PIN, LOW);	
			digitalWrite(LED2_PIN, LOW);
			if(t_led==0) {
				digitalWrite(LED1_PIN, HIGH);
				t_led = 1;
			} else {
				digitalWrite(LED1_PIN, LOW);
				t_led = 0;
			}
			Serial.print("Sensor status: ");
			Serial.println(dht.getStatusString());
			lcd.clear();
			lcd.home();
			lcd.print("DHT sensor error:");
			lcd.setCursor(0,1);
			lcd.print(dht.getStatusString());
			delay(500);
		}
		cTemp = dht.getTemperature();
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
		f_cur_humidity = dht.getHumidity();
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
				cur_subphase++;
			}
			// Switch to warmup
			if(togglestate == LOW) {
				timers[2] = BACKLIGHT_DELAY; 
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
				digitalWrite(RELAY_PIN, HIGH); 
				digitalWrite(LED2_PIN, HIGH);
				cur_subphase = 1;
			}

			if(wait_timer == 0 && cur_subphase == 1) {
				cur_subphase=0;
				if(togglestate == LOW) {
					cur_phase=2;
				} else {
					// If we turned heating off during
					// warmup phase move to cooldown
					wait_timer = cooldown_time;
					cur_phase=3;
					digitalWrite(RELAY_PIN, LOW);
					digitalWrite(LED2_PIN, LOW);
				}
			}
			break;
		case 2: // Warming phase

			// If switch is turned off then switch instantly to cooldown mode
			// or switch to cooldown until temperature has risen enough
			if(togglestate != LOW || f_cur_temp >= high_temp) {
				wait_timer = cooldown_time;
				cur_phase = 3;

				// Turn heater off
				digitalWrite(RELAY_PIN, LOW);
				digitalWrite(LED2_PIN, LOW);
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

			if(togglestate != LOW) {
				cur_phase = 0;
				cur_subphase = 0;
			}
			break;
		default:
			// Something goes BADLY wrong if this
			// ever happens. We go to infinite loop
			// and shut everything down.
			char buf[20];
			digitalWrite(RELAY_PIN,LOW); // Shut off burner
			digitalWrite(LED2_PIN, LOW);
			int l_tmp=0;
			while(1) {
				if(l_tmp==0) {
					digitalWrite(LED1_PIN, HIGH);
					digitalWrite(LED2_PIN, LOW);
					l_tmp=1;
				} else {
					digitalWrite(LED1_PIN, LOW);
					digitalWrite(LED2_PIN, HIGH);
					l_tmp=0;
				}

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

	pinMode(ROTARYBTN_PIN, INPUT_PULLUP);
	pinMode(ROTARYFWD_PIN, INPUT_PULLUP);
	pinMode(ROTARYRWD_PIN, INPUT_PULLUP);
	pinMode(TOGGLESW_PIN, INPUT_PULLUP);
	pinMode(RELAY_PIN, OUTPUT);
	pinMode(LED1_PIN, OUTPUT);
	pinMode(LED2_PIN, OUTPUT);

	// Initialize timers
	for(int i=0;i < TIMERS;i++) {
		timers[i] = 0;
	}

	// Initialize time calculators
	for(int i=0; i <= 2; i++) {
		uptime[i]=0;
		burntime[i]=0;
	}

	digitalWrite(RELAY_PIN, LOW);

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


        // Initialize interrupts
        attachPinChangeInterrupt(ROTARYBTN_PIN, trigger_button, FALLING);
        attachPinChangeInterrupt(ROTARYFWD_PIN, rotary_trigger, CHANGE);

        // Initialize button counters
        for(int i=0; i < BUTTONS; i++) {
                btn_queue[i] = 0;
        }

	lcd.begin(LCD_WIDTH, LCD_HEIGHT);               // initialize the lcd 
	lcd.home ();                   // go home

	// Output some version information on startup
	lcd.print("O-S Thermostat");  
	lcd.setCursor ( 0, 1 );        // go to the next line
	sprintf(version, "ver %s %s", SWVERSION, SWDATE);
	lcd.print (version);

	dht.setup(DHT_PIN);
	dhtSamplingPeriod = dht.getMinimumSamplingPeriod() * 2;

	Serial.print("DHT sampling: ");
	Serial.println(dhtSamplingPeriod);
	delay ( 500 );

	int t_led = 0;
	while(strcmp(cur_temp, "wait...") == 0) {
		if(t_led == 0) {
			digitalWrite(LED1_PIN, HIGH);
			t_led = 1;
		} else {
			digitalWrite(LED1_PIN, LOW);
			t_led = 0;
		}
		count_timers();
		ReadDHT();
		delay(100);
	}
	digitalWrite(LED1_PIN, HIGH);

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
	ReadDHT(); // Read temperature sensor

	// Run phase changes if necessary, this is
	// EXTREMELY important, since if phases won't
	// change as expected the whole thing is going to 
	// break.
	changePhase(); 

	// Update backlight delay on every button press
	if(btn_queue[0] > 0 || rotary_count != 0) {
		timers[2] = BACKLIGHT_DELAY;
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
	if(cur_menu_phase > 0 && btn_queue[0] > 0) {
		if(cur_menu_state == 1) {
			cur_menu_state = 0;
		} else {
			cur_menu_state = 1;
		}
		btn_queue[0]=0;
		return;
	}

	menu();
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
		sprintf(buf,"Active: %2d:%02d:%02d  ",burntime[0],burntime[1],burntime[2]);
	} else if(textState == 3) {
		sprintf(buf,"Humidity: %-3d%%   ", f_cur_humidity);
	}

	if( textTime == 0) {
		textState++;
		textTime = 5000;
		if(textState > 3) {
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




