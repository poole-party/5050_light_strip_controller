#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

// LED vars
const uint8_t BLUE = 3;
const uint8_t RED = 5;
const uint8_t GREEN = 6;
uint8_t blueLevel = 0;
uint8_t redLevel = 255;
uint8_t greenLevel = 0;
uint16_t outputLevel = 0;
uint16_t incrementValue = 10;

// lookup table for sine wave color transition
const uint8_t sine8[] PROGMEM = {
	0,   0,   0,   0,   0,   1,   1,   2,   2,   3,   4,   5,   6,   7,   8,   9, 
	11,  12,  13,  15,  17,  18,  20,  22,  24,  26,  28,  30,  32,  35,  37,  39, 
	42,  44,  47,  49,  52,  55,  58,  60,  63,  66,  69,  72,  75,  78,  81,  85, 
	88,  91,  94,  97, 101, 104, 107, 111,  114, 117, 121, 124, 127, 131, 134, 137, 
	141, 144, 147, 150, 154, 157, 160, 163, 167, 170, 173, 176, 179, 182, 185, 188, 
	191, 194, 197, 200, 202, 205, 208, 210, 213, 215, 217, 220, 222, 224, 226, 229, 
	231, 232, 234, 236, 238, 239, 241, 242, 244, 245, 246, 248, 249, 250, 251, 251, 
	252, 253, 253, 254, 254, 255, 255, 255, 255, 255, 255, 255, 254, 254, 253, 253, 
	252, 251, 251, 250, 249, 248, 246, 245, 244, 242, 241, 239, 238, 236, 234, 232, 
	231, 229, 226, 224, 222, 220, 217, 215, 213, 210, 208, 205, 202, 200, 197, 194, 
	191, 188, 185, 182, 179, 176, 173, 170, 167, 163, 160, 157, 154, 150, 147, 144, 
	141, 137, 134, 131, 127, 124, 121, 117, 114, 111, 107, 104, 101,  97,  94,  91, 
	88,  85,  81,  78,  75,  72,  69,  66,  63,  60,  58,  55,  52,  49,  47,  44, 
	42,  39,  37,  35,  32,  30,  28,  26,  24,  22,  20,  18,  17,  15,  13,  12, 
	11,  9,   8,   7,   6,   5,   4,   3,   2,   2,   1,   1,   0,   0,   0,   0, 
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 
	0,   0,   0,   0,   0,   0,   0,   0 
};

// encoder vars
const uint8_t CLK = 7;
const uint8_t DATA = 8;
const uint8_t SWITCH = 9;
uint8_t prevNextCode = 0;
uint16_t store = 0;

// function prototypes
void static_color();
void cycle_color();

// SD related vars
File paramFile;
uint16_t params[5];
const uint8_t CS = 10;

// Support vars
unsigned long lastRun = 0;
uint8_t fadeDelay = 50;
double newParamsSet = 0;
uint8_t totalModes = 2;
uint8_t mode = 0;
typedef void (*OptionMenu[])();
OptionMenu modeList = {
	static_color,
	cycle_color, 
};

// Button timing variables
volatile uint8_t debounce = 20;		// ms debounce period to prevent flickering when pressing or releasing the button
uint16_t DCgap = 250;				// max ms between clicks for a double click event
uint16_t holdTime = 1000;			// ms hold period: how long to wait for press+hold event
uint16_t longHoldTime = 3000;	 	// ms long hold period: how long to wait for press+hold event

// Button variables
boolean buttonVal = HIGH;			// value read from button
boolean buttonLast = HIGH;			// buffered value of the button's previous state
boolean DCwaiting = false;			// whether we're waiting for a double click (down)
boolean DConUp = false;				// whether to register a double click on next release, or whether to wait and click
boolean singleOK = true;			// whether it's OK to do a single click
long downTime = -1;					// time the button was pressed down
long upTime = -1;					// time the button was released
boolean ignoreUp = false;			// whether to ignore the button release because the click+hold was triggered
boolean waitForUp = false;			// when held, whether to wait for the up event
boolean holdEventPast = false;		// whether or not the hold event happened already
boolean longHoldEventPast = false;	// whether or not the long hold event happened already

void clickEvent() {
	mode = (mode + 1) % totalModes;
	switch (mode) {
		case 0:
			incrementValue = 10;
			break;
		case 1:
			incrementValue = 1;
			break;
	}
	Serial.println(incrementValue);
}

void doubleClickEvent() {
	incrementValue = incrementValue < 10 ? 10 : 1;
}

void longHoldEvent() {
	mode = 0;
	outputLevel = 0;
	incrementValue = 10;
	fadeDelay = 50;
}

void setChannelColors(uint16_t redOut, uint16_t greenOut, uint16_t blueOut) {
	analogWrite(RED, pgm_read_byte(&sine8[redOut]));
	analogWrite(GREEN, pgm_read_byte(&sine8[greenOut]));
	analogWrite(BLUE, pgm_read_byte(&sine8[blueOut]));
}

void increment_color() {
	setChannelColors((outputLevel + 120) % 360, outputLevel % 360, (outputLevel + 240) % 360);
	outputLevel = (outputLevel + incrementValue) % 360;
}

void decrement_color() {
	setChannelColors((outputLevel + 120) % 360, outputLevel % 360, (outputLevel + 240) % 360);
	outputLevel = (outputLevel + (360 - incrementValue)) % 360;
}

void static_color() {
	// placeholder function to allow user selection of color
}

void cycle_color() {
	increment_color();
}

// Debounce the rotary encoder
// A vald CW or CCW move returns 1, invalid returns 0.
int8_t read_rotary() {
	static int8_t rot_enc_table[] = {0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0};

	prevNextCode <<= 2;
	if (digitalRead(DATA)) 
		prevNextCode |= 0x02;
	if (digitalRead(CLK)) 
		prevNextCode |= 0x01;
	prevNextCode &= 0x0f;

	// If valid then store as 16 bit data.
	if (rot_enc_table[prevNextCode] ) {
		store <<= 4;
		store |= prevNextCode;
		// CCW
		if ((store&0xff)==0x2b) return -1;
		// CW
		if ((store&0xff)==0x17) return 1;
	}
	return 0;
}

int checkButton() {
	int event = 0;
	buttonVal = digitalRead(SWITCH);
	// Button pressed down
	if (buttonVal == LOW && buttonLast == HIGH && (millis() - upTime) > debounce)
	{
		downTime = millis();
		ignoreUp = false;
		waitForUp = false;
		singleOK = true;
		holdEventPast = false;
		longHoldEventPast = false;
		if ((millis() - upTime) < DCgap && DConUp == false && DCwaiting == true) {
			DConUp = true;
		}
		else {
			DConUp = false;
		}
		DCwaiting = false;
	}
	// Button released
	else if (buttonVal == HIGH && buttonLast == LOW && (millis() - downTime) > debounce)
	{
		if (not ignoreUp)
		{
			upTime = millis();
			if (DConUp == false) 
				DCwaiting = true;
			else {
				event = 2;
				DConUp = false;
				DCwaiting = false;
				singleOK = false;
			}
		}
	}
	// Test for normal click event: DCgap expired
	if ( buttonVal == HIGH && (millis() - upTime) >= DCgap && DCwaiting == true && DConUp == false && singleOK == true && event != 2)
	{
		event = 1;
		DCwaiting = false;
	}
	// Test for hold
	if (buttonVal == LOW && (millis() - downTime) >= holdTime) {
	// Trigger "normal" hold
		if (not holdEventPast)
		{
			event = 3;
			waitForUp = true;
			ignoreUp = true;
			DConUp = false;
			DCwaiting = false;
			//downTime = millis();
			holdEventPast = true;
		}
		// Trigger "long" hold
		if ((millis() - downTime) >= longHoldTime)
		{
			if (not longHoldEventPast)
			{
				event = 4;
				longHoldEventPast = true;
			}
		}
	}
	buttonLast = buttonVal;
	return event;
}

void readParams() {
	char digits[10];
	bool receiving = false;
	uint8_t index = 0;
	uint8_t next_param = 0;
	char *ptr;
	while (paramFile.available()) {
		char next_char = paramFile.read();
		Serial.print(next_char);
		if (receiving == true) {
			if (next_char == '>') {
				digits[index] = '\0';
				params[next_param] = strtod(digits, &ptr);
				Serial.println();
				Serial.print("new param: ");
				Serial.println(params[next_param]);
				next_param++;
				index = 0;
				receiving = false;
			} else {
				digits[index] = next_char;
				index++;
			}
		} else if (next_char == '<') {
			receiving = true;
		}
	}
	params[next_param] = '\0';
}

boolean writeParams(char newSettings[]) {
	SD.remove("params.txt");
	paramFile = SD.open("params.txt", FILE_WRITE); // filename must be 8 characters or less
	if (paramFile) {
		for (uint8_t i = 0; i < strlen(newSettings); i++) {
			paramFile.print(newSettings[i]);
		}
	} else {
		return false;
	}
	paramFile.close();
	return true;
}

void initSD() {
	Serial.print("Initializing card... ");
	if (!SD.begin(CS)) {
		Serial.println("Card failed or not present!");
	} else {
		Serial.println("Card initialized!");
		// SD.remove("params.txt");
		paramFile = SD.open("params.txt");
		if (!paramFile) {
			Serial.print("No parameter file exists. Creating now... ");
			char defaultSettings[80] = 
			"Mode: <0>\n"
			"outputLevel: <0>\n"
			"incrementValue: <10>\n"
			"fadeDelay: <50>\n";
			if (writeParams(defaultSettings)) {
				Serial.println("File created successfully!");
			} else {
				Serial.println("File creation failed!");
			}
		} else {
			Serial.println("Parameter file found. Loading... ");
			readParams();
			paramFile.close();
			for (uint8_t i = 0; i < (sizeof(params) / sizeof(params[0])); i++) {
				Serial.print("Parameter ");
				Serial.print(i + 1);
				Serial.print(": ");
				Serial.println(params[i]);
			}
			mode = (int) params[0];
			outputLevel = (int) params[1];
			incrementValue = (int) params[2];
		}
	}
}

void setup() {
	delay(2000);
	pinMode(BLUE, OUTPUT);
	pinMode(RED, OUTPUT);
	pinMode(GREEN, OUTPUT);

	pinMode(CLK, INPUT);
	pinMode(CLK, INPUT_PULLUP);
	pinMode(DATA, INPUT);
	pinMode(DATA, INPUT_PULLUP);
	pinMode(CS, OUTPUT);
	Serial.begin(115200);

	initSD();
	setChannelColors((outputLevel + 120) % 360, outputLevel % 360, (outputLevel + 240) % 360);
}

void loop() {
	static int8_t c,val;
	long currentTime = millis();

	// set the frame/sec speed to run the current mode at
	if (mode > 0 && (currentTime - lastRun >= fadeDelay)) {
		modeList[mode]();
		lastRun = currentTime;
	}

	int b = checkButton();
	switch (b) {
		case 1:
			// advance to the next mode
			clickEvent();
			newParamsSet = millis();
			break;
		case 2:
			// cycle between coarse and fine adjustment
			doubleClickEvent();
			newParamsSet = millis();
			break;
		case 3:
			// holdEvent();
			break;
		case 4:
			// reset all settings
			longHoldEvent();
			break;
		default:
			break;
	}

	if (val=read_rotary()) {
		c +=val;
		// Serial.print(c);Serial.print(" ");

		// CCW
		if (prevNextCode==0x0b) {
			if (mode == 0) 
				decrement_color();
			if (mode == 1) 
				fadeDelay += 1;
			// Serial.print("eleven ");
			// Serial.println(store,HEX);
		}

		// CW
		if (prevNextCode==0x07) {
			if (mode == 0) 
				increment_color();
			if (mode == 1) 
				fadeDelay -= 1;
			// Serial.print("seven ");
			// Serial.println(store,HEX);
		}

		newParamsSet = millis();
	}

	// Check if there are new params to be written and if we have waited sufficiently long between actions to write them
	if (newParamsSet > 0 && (currentTime - newParamsSet >= 250)) {
		String settingStr = 
			"Mode: <" + String(mode) + ">\n"
			"outputLevel: <" + String(outputLevel) + ">\n"
			"incrementValue: <" + String(incrementValue) + ">\n"
			"fadeDelay: <" + String(fadeDelay) + ">\n";
		char newSettings[80];
		settingStr.toCharArray(newSettings, 80);
		writeParams(newSettings);
		newParamsSet = 0;
	}
}