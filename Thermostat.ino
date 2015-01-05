#include <LiquidCrystal_I2C.h>
#include <OneWire.h>

// Software version and date
#define SWVERSION "0.1"
#define SWDATE "01-15"

// Behaviour
#define BTN_DELAY 50

#define TIMERS 2

// Pin layout
#define RELAY_PIN 2
#define DS_PIN 3
#define UPBTN_PIN 4
#define ENTBTN_PIN 5
#define DOWNBTN_PIN 6
#define TOGGLESW_PIN 7

// LCD used
#define LCD_WIDTH 16
#define LCD_HEIGHT 2
#define LCD_ADDRESS 0x27

// Init subsystems
LiquidCrystal_I2C lcd(LCD_ADDRESS, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);
OneWire ds(DS_PIN);

int btnPressed = 0;
int btnState = 0;
byte ow_addr[8];
int cur_phase=0; // current working phase
char cur_temp[20] = "wait...";
unsigned long timers[TIMERS]; // Countdown timers
unsigned long timer_millis; // Internal timer

int uptime[2]; // uptime calculator
int burntime[2]; // burntime calculator on current uptime
int timecalc; // Used to calculate spent time
unsigned long wait_timer; // Used to track warmup/cooldown times

// Wait warmup_timer milliseconds for burner to warm up
// 600 000 = 10 minutes
//unsigned long warmup_time = 600000;
unsigned long warmup_time = 6000;

// Wait cooldown_timer milliseconds for burner to cool down
// 1 200 000 milliseconds = 20 minutes
//unsigned long cooldown_time = 1200000;
unsigned long cooldown_time = 12000;

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

	return btnPressed;
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

	// Do time tracking calculations. This isn't millisecond
	// accurate, but should be close enough for this purpose.
	timecalc += diffMillis;
	if(timecalc >= 1000) {
		timecalc -= 1000;
		uptime[2]++;

		// If burner is on then update burntime as well
		if(cur_phase > 0 && cur_phase < 3) {
			burntime[2]++;

			if(burntime[2]>=60) {
				burntime[1]++;
				burntime[2]-=60;
			}

			if(burntime[2]>=60) {
				burntime[0]++;
				burntime[2]-=60;
			}
		}

		if(uptime[2]>=60) {
			uptime[1]++;
			uptime[2]-=60;
		}

		if(uptime[2]>=60) {
			uptime[0]++;
			uptime[2]-=60;
		}
	}
	
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

	// Initialize timers
	for(int i=0;i <= TIMERS;i++) {
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
  
	lcd.begin(LCD_WIDTH, LCD_HEIGHT);               // initialize the lcd 
	lcd.home ();                   // go home

	// Output some version information on startup
	lcd.print("SaHa Thermostat");  
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
	byte present = 0;

	if(timers[1] == 0) {

		findOneWire(); // No idea why this is needed EVERY time
		ds.reset();
		ds.select(ow_addr);
		ds.write(0x44,1);         // start conversion, with parasite power on at the end
		Serial.println("Preparing 1wire sensor...");
		timers[1] = 2000;
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

		// Use only 1 digit fractions, round up if necessary
		if(tFract > 4) {
			Fract ++;
		}

		sprintf(cur_temp, "%c%2d.%1d\337   ",SignBit ? '-' : '+', Whole, Fract);
		timers[1] = 0;
	}

	return;
}

void changePhase() {
	int togglestate;
	togglestate = digitalRead(TOGGLESW_PIN);
	
	switch(cur_phase) {
		case 0: // Standby
			// Switch to warmup
			if(togglestate == HIGH) {
				cur_phase=1;
				wait_timer = warmup_time;
			}
			break;
		case 1: // Warmup
			// Change state ONLY if we aren't
			// in warmup state
			if(wait_timer == 0) {
				if(togglestate == HIGH) {
					cur_phase=2;
				} else {
					// If we turned heating off during
					// warmup phase move to cooldown
					cur_phase=3;
				}
			}
			break;
		case 2: // Warming phase
			// We'd need target temp for this...
			if(togglestate != HIGH) {
				wait_timer = cooldown_time;
				cur_phase = 3;
			}
			break;
		case 3: // Cooldown phase
			if(wait_timer == 0) {
				cur_phase = 4;
			}
			break;
		case 4: // Wait for temperature to drop
			// We'd need target temp for this too...
			if(togglestate != HIGH) {
				cur_phase = 0;
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
	ReadOneWire();
	changePhase();

	lcd.setCursor(0,0);
	lcd.print(cur_temp);


	int btn = readButtons();
	sprintf(buf,"%d", btn);
	lcd.setCursor(0,1);
	lcd.print(buf);

	sprintf(buf,"%d", cur_phase);
	lcd.setCursor(2,1);
	lcd.print(buf);

	sprintf(buf,"%lu", wait_timer);
	lcd.setCursor(4,1);
	lcd.print(buf);

}




