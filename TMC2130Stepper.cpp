#define TMC2130DEBUG

#include <SPI.h>
#include "TMC2130Stepper.h"

//#define REG_GCONF			0x00
#define REG_GSTAT			0x01
#define REG_IOIN			0x04
//#define REG_IHOLD_IRUN	0x10
#define REG_TPOWERDOWN		0x11
#define REG_TSTEP			0x12
#define REG_TPWMTHRS		0x13
#define REG_TCOOLTHRS		0x14
#define REG_THIGH			0x15
#define REG_XDIRECT			0x2D
#define REG_VDCMIN			0x33
#define REG_MSCNT			0x6A
#define REG_MSCURACT		0x6B
//#define REG_CHOPCONF		0x6C
//#define REG_COOLCONF		0X6D
#define REG_DCCTRL			0x6E
#define REG_DRVSTATUS		0x6F
//#define REG_PWMCONF		0X70
#define REG_PWMSCALE		0X71
#define REG_ENCMCTRL		0x72
#define REG_LOSTSTEPS		0x73

TMC2130Stepper::TMC2130Stepper(uint8_t pinEN, uint8_t pinDIR, uint8_t pinStep, uint8_t pinCS) {
	_started = false;

	this->_pinEN = pinEN;
	this->_pinDIR = pinDIR;
	this->_pinSTEP = pinStep;
	this->_pinCS = pinCS;
}

void TMC2130Stepper::begin() {
#ifdef TMC2130DEBUG
	Serial.println("TMC2130 Stepper driver library");
	Serial.print("Enable pin: ");
	Serial.println(_pinEN);
	Serial.print("Direction pin: ");
	Serial.println(_pinDIR);
	Serial.print("Step pin: ");
	Serial.println(_pinSTEP);
	Serial.print("Chip select pin: ");
	Serial.println(_pinCS);
#endif
	//set pins
	pinMode(_pinEN, OUTPUT);
	pinMode(_pinDIR, OUTPUT);
	pinMode(_pinSTEP, OUTPUT);
	pinMode(_pinCS, OUTPUT);
	digitalWrite(_pinEN, HIGH); //deactivate driver (LOW active)
	digitalWrite(_pinDIR, LOW); //LOW or HIGH
	digitalWrite(_pinSTEP, LOW);
	digitalWrite(_pinCS, HIGH);

	pinMode(12, OUTPUT);
	pinMode(11, INPUT);
	pinMode(13, OUTPUT);
	digitalWrite(12, LOW);
	digitalWrite(11, HIGH);
	digitalWrite(13, LOW);

	SPI.begin();
	_started = true;
}

uint32_t TMC2130Stepper::send2130(uint8_t addressByte, uint32_t *config, uint32_t value, uint32_t mask) {
	uint8_t s;

	SPI.beginTransaction(SPISettings(16000000/8, MSBFIRST, SPI_MODE3));
	digitalWrite(_pinCS, LOW);

	s = SPI.transfer(addressByte & 0xFF);
#ifdef TMC2130DEBUG
	Serial.print("Address byte: ");
	Serial.println(addressByte, HEX);
	Serial.print("Config: ");
	Serial.println(*config, HEX);
	Serial.print("Value: ");
	Serial.println(value, HEX);
	Serial.print("Mask: ");
	Serial.println(mask, HEX);
	Serial.print("s: ");
	Serial.println(s, HEX);
#endif

	if (addressByte << 7) { // Check if WRITE command
		*config &= ~mask; // Clear bits being set
		*config |= (value & mask); // Set new values
		SPI.transfer((*config >> 24) & 0xFF);
		SPI.transfer((*config >> 16) & 0xFF);
		SPI.transfer((*config >>  8) & 0xFF);
		SPI.transfer(*config & 0xFF);
#ifdef TMC2130DEBUG
		Serial.println("WRITE cmd");
		Serial.print("New config: ");
		Serial.println(*config, HEX);
#endif
	} else { // READ command
		*config  = SPI.transfer((*config >> 24) & 0xFF);
		*config <<= 8;
		*config |= SPI.transfer((*config >> 16) & 0xFF);
		*config <<= 8;
		*config |= SPI.transfer((*config >>  8) & 0xFF);
		*config <<= 8;
		*config |= SPI.transfer( *config 		& 0xFF);
#ifdef TMC2130DEBUG
		Serial.println("WRITE cmd");
		Serial.print("New config: ");
		Serial.println(*config, HEX);
#endif
	}

	digitalWrite(_pinCS, HIGH);
	SPI.endTransaction();

	return s;
}
//#ifdef TMC2130DEBUG
void TMC2130Stepper::checkStatus() {
	uint32_t data = 0x0;
	uint8_t s;

    //show REG_GSTAT
    s = send2130(READ|REG_GSTAT, &data, 0x0, 0x0);
    Serial.print("GSTAT:     0x0");
    Serial.print(data, HEX);
    Serial.print("\t - ");
    Serial.print("Status: 0x");
    Serial.print(s, HEX);
    if(s & 0x01) Serial.print(" reset");
    if(s & 0x02) Serial.print(" error");
    if(s & 0x04) Serial.print(" sg2");
    if(s & 0x08) Serial.print(" standstill");
    Serial.println(" ");

    //show REG_DRVSTATUS
    s = send2130(READ|REG_DRVSTATUS, &data, 0x0, 0x0);
    Serial.print("DRVSTATUS: 0x");
    Serial.print(data, HEX);
    Serial.print("\t - ");
    Serial.print("Status: 0x");
    Serial.print(s, HEX);
    if(s & 0x01) Serial.print(" reset");
    if(s & 0x02) Serial.print(" error");
    if(s & 0x04) Serial.print(" sg2");
    if(s & 0x08) Serial.print(" standstill");
    Serial.println(" ");
}
//#endif

void TMC2130Stepper::setSSSCurrent() {
	setCurrent(1000, 1);
}

/*	
	Requested current = mah = I_rms/1000
	Equation for current:
	I_rms = (CS+1)/32 * V_fs/(R_sense+0.02ohm) * 1/sqrt(2)
	Solve for CS ->
	CS = 32*sqrt(2)*I_rms*(R_sense+0.02)/V_fs - 1
	
	Example:
	vsense = 0b0 -> V_fs = 0.325V
	mah = 1640mAh = I_rms/1000 = 1.64A
	R_sense = 0.10 Ohm
	->
	CS = 32*sqrt(2)*1.64*(0.10+0.02)/0.325 - 1 = 27.4
	CS = 27
*/	
void TMC2130Stepper::setCurrent(uint16_t mah, float Rsense) {
	float V_fs;
	if (val_vsense)
		V_fs = 0.325;
	else
		V_fs = 0.180;
	uint8_t CS = 32.0*1.41421*mah/1000.0*(Rsense+0.02)/V_fs - 1;
	run_current(CS);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_TPOWERDOWN

uint8_t TMC2130Stepper::power_down_delay() {return val_tpowerdown;}

void TMC2130Stepper::power_down_delay(uint8_t value) {
	val_tpowerdown = value;
	send2130(WRITE|REG_TPOWERDOWN, &cur_TPOWERDOWN, value, 0xFF);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_TSTEP

uint32_t TMC2130Stepper::microstep_time() {
	uint32_t data = 0x0;
	send2130(READ|REG_TSTEP, &data, 0x0, 0x0);
	return data;
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_TPWMTHRS

uint32_t TMC2130Stepper::stealth_max_speed() {return val_tpwmthrs;}

void TMC2130Stepper::stealth_max_speed(uint32_t value) {
	if (value > 1048575) value = 1048575;
	val_tpwmthrs = value;
	send2130(WRITE|REG_TPWMTHRS, &cur_TPWMTHRS, value, 0xFFFFF);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_TCOOLTHRS

uint32_t TMC2130Stepper::coolstep_min_speed() {return val_tcoolthrs;}

void TMC2130Stepper::coolstep_min_speed(uint32_t value) {
	if (value > 1048575) value = 1048575;
	val_tcoolthrs = value;
	send2130(WRITE|REG_TCOOLTHRS, &cur_TCOOLTHRS, value, 0xFFFFF);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_THIGH

uint32_t TMC2130Stepper::mode_sw_speed() {return val_thigh;}

void TMC2130Stepper::mode_sw_speed(uint32_t value) {
	if (value > 1048575) value = 1048575;
	val_thigh = value;
	send2130(WRITE|REG_THIGH, &cur_THIGH, value, 0xFFFFF);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_XDRIRECT

int16_t TMC2130Stepper::coil_A_current() {return val_xdirect_a;}

void TMC2130Stepper::coil_A_current(int16_t value) {
	if (value < -255) value = -255;
	else if (value > 255) value = 255;
	val_xdirect_a = value;
	send2130(WRITE|REG_XDIRECT, &cur_XDIRECT, value, 0x1FF);
}

int16_t TMC2130Stepper::coil_B_current() {return val_xdirect_b;}

void TMC2130Stepper::coil_B_current(int16_t value) {
	if (value < -255) value = -255;
	else if (value > 255) value = 255;
	val_xdirect_b = value;
	send2130(WRITE|REG_XDIRECT, &cur_XDIRECT, (uint32_t)value << 16, 0x1FF0000);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_VDCMIN

uint32_t TMC2130Stepper::DCstep_min_speed() {return val_vdcmin;}

void TMC2130Stepper::DCstep_min_speed(uint32_t value) {
	if (value > 16777215) value = 16777215;
	val_vdcmin = value;
	send2130(WRITE|REG_VDCMIN, &cur_VDCMIN, value, 0xFFFFFF);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_DRVSTATUS

uint32_t TMC2130Stepper::DRVSTATUS() {
	uint32_t data = 0x0;
	send2130(READ|REG_DRVSTATUS, &data, 0x0, 0x0);
	return data;
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_PWM_SCALE

uint32_t TMC2130Stepper::PWM_SCALE() {
	uint32_t data = 0x0;
	send2130(READ|REG_PWMSCALE, &data, 0x0, 0x0);
	return data;
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_ENCM_CTRL

uint8_t TMC2130Stepper::invert_encoder() {return val_invert_encoder;}

void TMC2130Stepper::invert_encoder(uint8_t value) {
	val_invert_encoder = value;
	send2130(WRITE|REG_ENCMCTRL, &cur_PWMCONF, value, 0b1);
}

uint8_t TMC2130Stepper::maxspeed() {return val_maxspeed;}

void TMC2130Stepper::maxspeed(uint8_t value) {
	val_maxspeed = value;
	send2130(WRITE|REG_ENCMCTRL, &cur_PWMCONF, value, 0b1 << 1);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_LOST_STEPS

uint32_t TMC2130Stepper::LOST_STEPS() {
	uint32_t data = 0x0;
	send2130(READ|REG_LOSTSTEPS, &data, 0x0, 0x0);
	return data;
}