#include <LiquidCrystal_I2C.h>
#include <OneWire.h>

#define RELAY_PIN 2
#define DS_PIN 3

#define LCD_WIDTH 16
#define LCD_HEIGHT 2

LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);
OneWire ds(DS_PIN);

void setup()
{
	Serial.begin(9600);

	pinMode(RELAY_PIN, OUTPUT);
	digitalWrite(RELAY_PIN, HIGH);
  
	lcd.begin(LCD_WIDTH, LCD_HEIGHT);               // initialize the lcd 
	lcd.home ();                   // go home
	lcd.print("SaHa Thermostat");  
	lcd.setCursor ( 0, 1 );        // go to the next line
	lcd.print ("ver 0.1, 01-15");
	delay ( 1000 );
	lcd.clear();

	return;
}

void loop() 
{
	int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract, tFract;
	byte i;
	byte present = 0;
	byte data[12];
	byte addr[8];
	char buf[20];

	ds.reset_search();
	if ( !ds.search(addr)) {
		lcd.setCursor(0,0);
		lcd.print("No 1Wire sensors found!            ");
		Serial.print("No more addresses.\n");
		ds.reset_search();
		return;
	}

	if ( OneWire::crc8( addr, 7) != addr[7]) {
		lcd.setCursor(0,0);
		lcd.print("CRC error on 1Wire                ");
		Serial.print("CRC is not valid!\n");
		return;
	}

	if (addr[0] == 0x10 || addr[0] == 0x28) {
		Serial.print("Known device\n");
	}
	else {
		lcd.setCursor(0,0);
		lcd.print("Unknown sensor device!            ");
		Serial.print("Device family is not recognized: 0x");
		Serial.println(addr[0],HEX);
		return;
	}

	ds.reset();
	ds.select(addr);
	ds.write(0x44,1);         // start conversion, with parasite power on at the end

	delay(1000);     // maybe 750ms is enough, maybe not

	present = ds.reset();
	ds.select(addr);    
	ds.write(0xBE);         // Read Scratchpad

	for ( i = 0; i < 9; i++) {           // we need 9 bytes
		data[i] = ds.read();
		Serial.print(data[i], HEX);
		Serial.print(" ");
	}
	Serial.println();
	
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
	
	if(tFract > 4) {
		Fract ++;
	}

	sprintf(buf, "%c%2d.%1d\337",SignBit ? '-' : '+', Whole, Fract);

	lcd.setCursor(0,0);
	lcd.print(buf);
}
