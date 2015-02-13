#include <LiquidCrystal_I2C.h>
#include <OneWire.h>

// Software version and date
#define SWVERSION "0.5"
#define SWDATE "01-15"

// Behaviour
#define BTN_DELAY 50
#define MENU_DELAY 10000
#define TIMERS 4

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

// Init subsystems
LiquidCrystal_I2C lcd(LCD_ADDRESS, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);
OneWire ds(DS_PIN);

// There's quite a few phase tracking variables here,
// maybe we could take some of them off, maybe?
int btnPressed = 0;
int btnState = 0;
int btnLock = 0;
int textState = 0;

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

// Wait warmup_timer milliseconds for burner to warm up
// 600 000 = 10 minutes
//unsigned long warmup_time = 600000;
unsigned long warmup_time = 65000;

// Wait cooldown_timer milliseconds for burner to cool down
// 1 200 000 milliseconds = 20 minutes
//unsigned long cooldown_time = 1200000;
unsigned long cooldown_time = 12000;

// Start / stop temperatures
// We multiple temps by 10, so 25 degrees becomes
// 250 and so on, so we don't need to use floats
int low_temp = 240;
int high_temp = 285;

// Check that we actually have temperature sensor,
// and verify that we don't do anything unless
// we have one.
int findOneWire() {

        ds.reset_search();
        if ( !ds.search(ow_addr)) {
                ds.reset_search();
                return 0;
        }

        if ( OneWire::crc8( ow_addr, 7) != ow_addr[7]) {
                Serial.print("CRC is not valid!\n");
                return 2;
        }

        if (ow_addr[0] == 0x10 || ow_addr[0] == 0x28) {
		Serial.println("Sensor found");
                return 1;
        }
        else { 
                Serial.print("Device family is not recognized: 0x");
                Serial.println(ow_addr[0],HEX);
                return 3;
        }

	return 4;
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

	if(cur_menu_phase < 0) {
		cur_menu_phase=4;
	}

	if(cur_menu_phase > 4) {
		cur_menu_phase=0;
	}

        // If there's a need we'll change the menu
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
	//lcd.print("0123456789012345");
        return;
}


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
  
	lcd.begin(LCD_WIDTH, LCD_HEIGHT);               // initialize the lcd 
	lcd.home ();                   // go home

	// Output some version information on startup
	lcd.print("O-S Thermostat");  
	lcd.setCursor ( 0, 1 );        // go to the next line
	sprintf(version, "ver %s %s", SWVERSION, SWDATE);
	lcd.print (version);
	delay ( 1000 );

	lcd.clear();
	int t_ow = findOneWire();

	while(t_ow != 1) {

		lcd.setCursor(0,0);
		lcd.print("Sensor error:");
		lcd.setCursor(0,1);

		switch (t_ow) {
			case 0:
				lcd.print("NOT FOUND");
				break;
			case 2:	
				lcd.print("CRC ERROR");
				break;
			case 3:
				lcd.print("WRONG FAMILY");
				break;
			case 4:
				lcd.print("FATAL");
				break;
			default:
				lcd.print("!!!!!");
				break;	
		}		

	}

	return;
}

// Read sensor data and store it in string to cur_temp char[]
// Uses timers[1] to create non-blocking environment
void ReadOneWire() {

	int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract, tFract;
	byte data[12];
	byte present;

	if(timers[1] == 0) {

		findOneWire(); // No idea why this is needed EVERY time
		ds.reset();
		ds.select(ow_addr);
		ds.write(0x44,1);         // start conversion, with parasite power on at the end
		Serial.println("Preparing 1wire sensor...");
		timers[1] = 2500;
	}

	if(timers[1] < 1000) {

		findOneWire();
		Serial.println("Reading data...");
		present = ds.reset();
		ds.select(ow_addr);
		ds.write(0xBE);         // Read Scratchpad
		delay(1);

		for (int i = 0; i < 9; i++) {           // we need 9 bytes
			data[i] = ds.read();
		}

		LowByte = data[0];
		HighByte = data[1];
		TReading = (HighByte << 8) + LowByte;
		SignBit = TReading & 0x8000;  // test most sig bit
		if (SignBit) { // negative
			TReading = (TReading ^ 0xffff) + 1; // 2's comp
		}
		Tc_100 = (6 * TReading) + TReading / 4;    // multiply by (100 * 0.0625) or 6.25
		Whole = Tc_100 / 100;  // separate off the whole and fractional portions
		Fract = Tc_100 % 100;
		tFract = Fract % 10;
		Fract = Fract / 10;

		f_cur_temp = Whole*10 + Fract;

		// Use only 1 digit fractions, round up if necessary
		if(tFract > 4) {
			Fract ++;
		}

		sprintf(cur_temp, "%c%2d.%1d\337 ",SignBit ? '-' : '+', Whole, Fract);
		timers[1] = 0;
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




void loop() 
{
	char buf[20];

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
		btnLock = btn;
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

	lcd.setCursor(0,0);
	sprintf(buf, "%-7s [%d][%d]", cur_temp, btn,btnLock);
	lcd.print(buf);

	// Display phase texts
	int tminus = (wait_timer / 1000) / 60;
	int whole;
	int fract;

	switch(cur_phase) {
		case 0:
			if(textState == 1) {
				sprintf(buf,"Up: %2d:%02d:%02d  ",uptime[0],uptime[1],uptime[2]);
			} else if(textState == 2) {
				sprintf(buf,"Burn: %2d:%02d:%02d  ",burntime[0],burntime[1],burntime[2]);
			} else {
				sprintf(buf,"Standby...      ");
			}
			if( textTime == 0) {
				textState++;
				textTime = 5000;
				if(textState > 2) {
					textState = 0;
				}
			}
			break;
		case 1:
			sprintf(buf,"Warming, T-%dm     ", tminus);
			break;
		case 2:
			whole = high_temp / 10;
			fract = high_temp % 10;
			sprintf(buf,"Heating to %2d.%d\337",whole,fract);
			break;
		case 3:
			sprintf(buf,"Cooldown, T-%dm     ", tminus);
			break;
		case 4:
			whole = low_temp / 10;
			fract = low_temp % 10;
			sprintf(buf,"Cooling to %2d.%d\337",whole,fract);
			break;
	}

	lcd.setCursor(0,1);
	lcd.print(buf);


}




