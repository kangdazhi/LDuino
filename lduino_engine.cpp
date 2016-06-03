// Copyright 2016 Frederic Rible
//
// This file is part of LDuino, an Arduino based PLC software compatible with LDmicro.
//
// LDuino is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// LDuino is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with LDmicro.  If not, see <http://www.gnu.org/licenses/>.

#include "lduino_engine.h"

#define INT_SET_BIT                              1
#define INT_CLEAR_BIT                            2
#define INT_COPY_BIT_TO_BIT                      3
#define INT_SET_VARIABLE_TO_LITERAL              4
#define INT_SET_VARIABLE_TO_VARIABLE             5
#define INT_INCREMENT_VARIABLE                   6
#define INT_SET_VARIABLE_ADD                     7
#define INT_SET_VARIABLE_SUBTRACT                8
#define INT_SET_VARIABLE_MULTIPLY                9
#define INT_SET_VARIABLE_DIVIDE                 10
#define INT_READ_ADC                            11
#define INT_SET_PWM                             12

#define INT_IF_BIT_SET                          50
#define INT_IF_BIT_CLEAR                        51
#define INT_IF_VARIABLE_LES_LITERAL             52
#define INT_IF_VARIABLE_EQUALS_VARIABLE         53
#define INT_IF_VARIABLE_GRT_VARIABLE            54 // obsolete

#define INT_ELSE                                60 + 100
#define INT_END_IF                              61 + 100

#define INT_END_OF_PROGRAM                     255

LDuino_engine::LDuino_engine()
{
	mb = NULL;
	Program = NULL;
	IO = NULL;
	ClearProgram();
}

void LDuino_engine::ClearProgram(void)
{
	ProgramReady = false;
	LoaderState = st_init;
	pc = 0;
	line_length = 0;
	time = 0;
	nbProgram = 0;
	nbIO = 0;
	delete[] Program;
	delete[] IO;
	Program = NULL;
	IO = NULL;
}

int LDuino_engine::HexDigit(int c)
{
	if ((c >= '0') && (c <= '9')) {
		return c - '0';
	}
	else if ((c >= 'a') && (c <= 'f')) {
		return 10 + (c - 'a');
	}
	else if ((c >= 'A') && (c <= 'F')) {
		return 10 + (c - 'A');
	}
	return -1;
}

LDuino_engine::state LDuino_engine::ChangeState(char * line)
{
	if (strstr(line, "$$LDcode")) LoaderState = st_LDcode;
	else if (strstr(line, "$$IO")) LoaderState = st_IO;
	else if (strstr(line, "$$cycle")) LoaderState = st_cycle;
}

void LDuino_engine::LoadProgramLine(char *line)
{
	line = strtok(line, "\r\n");
	ChangeState(line);

	switch (LoaderState) {
		case st_LDcode:
		{
			if (line[0] == '$') {
				// $$LDcode program_size
				strtok(line, " ");
				char *p = strtok(NULL, " ");
				if (!p) break;
				nbProgram = atoi(p);
				Program = new BYTE[nbProgram];
				D(Serial << "nbProgram=" << nbProgram << "\n");
				break;
			}
			
			for (char *t = line; t[0] >= 32 && t[1] >= 32; t += 2) {
				Program[pc++] = HexDigit(t[1]) | (HexDigit(t[0]) << 4);
				D(Serial << "New Opcode[" << pc - 1 << "]=" << Program[pc - 1] << '\n');
			}
			break;
		}
		case st_IO:
		{
			if (line[0] == '$') {
				// $$IO nb_named_IO total_nb_IO
				strtok(line, " ");
				strtok(NULL, " ");
				char *p = strtok(NULL, " ");
				if (!p) break;
				nbIO = atoi(p);
				IO = new IO_t[nbIO];
				D(Serial << "nbIO=" << nbIO << "\n");
				break;
			}

			// 0 Xin 7 6 0 0
			char *p;
			p = strtok(line, " ");	// Addr
			if (!p) break;
			int addr = atoi(p);
			if (addr < 0 || addr >= nbIO) break;
			strtok(NULL, " ");	// Skip name
			p = strtok(NULL, "");
			if (!p) break;
			sscanf(p, "%d %d %d %d", 
					&IO[addr].Map.type, 
					&IO[addr].Map.pin, 
					&IO[addr].Map.ModbusSlave, 
					&IO[addr].Map.ModbusOffset);
			D(Serial << "New IO[" << addr << "]=" << p << '\n');
			break;
		}
		case st_cycle:
		{	
			// $$cycle 10000 us
			cycle_ms = atoi(line + 7) / 1000;
			ConfigureModbus();
			ProgramReady = true;
			D(Serial.println("Program Ready"));
			D(Serial.print("cycle time (ms): "));
			D(Serial.println(cycle_ms));
			break;
		}
	}
}

void LDuino_engine::LoadProgramLine(char c)
{
	if (line_length < sizeof(line)) line[line_length++] = c;
	if (c == '\n' && line_length > 0) {
		line[line_length] = 0;
		LoadProgramLine(line);
		line_length = 0;
	}
}

void LDuino_engine::ConfigureModbus(void)
{
	if (!mb) return;

	for (int addr = 0; addr < nbIO; addr++) {
		switch (IO[addr].Map.type) {
		case IO_TYPE_MODBUS_COIL:
			mb->addCoil(IO[addr].Map.ModbusOffset);
			break;
		case IO_TYPE_MODBUS_CONTACT:
			mb->addIsts(IO[addr].Map.ModbusOffset);
			break;
		case IO_TYPE_MODBUS_HREG:
			mb->addHreg(IO[addr].Map.ModbusOffset);
			break;
		}
	}
}

void LDuino_engine::InterpretOneCycle(void)
{
	if (!ProgramReady) return;

	for (int pc = 0;;) {
		D(Serial << "opcode[" << String(pc, HEX) << "]=" << String(Program[pc], HEX) << "\n");
		switch (Program[pc]) {
		case INT_SET_BIT:
			WRITE_BIT(Program[pc+1], 1);
			pc += 2;
			break;

		case INT_CLEAR_BIT:
			WRITE_BIT(Program[pc + 1], 0);
			pc += 2;
			break;

		case INT_COPY_BIT_TO_BIT:
			WRITE_BIT(Program[pc+1], READ_BIT(Program[pc + 2]));
			pc += 3;
			break;

		case INT_SET_VARIABLE_TO_LITERAL:
			WRITE_INT(Program[pc+1], Program[pc + 2] + (Program[pc + 3] << 8));
			pc += 4;
			break;

		case INT_SET_VARIABLE_TO_VARIABLE:
			WRITE_INT(Program[pc+1], READ_INT(Program[pc + 2]));
			pc += 3;
			break;

		case INT_INCREMENT_VARIABLE:
			WRITE_INT(Program[pc+1], READ_INT(Program[pc+1]) + 1);
			pc += 2;
			break;

		case INT_SET_VARIABLE_ADD:
			WRITE_INT(Program[pc+1], READ_INT(Program[pc + 2]) + READ_INT(Program[pc + 3]));
			pc += 4;
			break;

		case INT_SET_VARIABLE_SUBTRACT:
			WRITE_INT(Program[pc + 1], READ_INT(Program[pc + 2]) - READ_INT(Program[pc + 3]));
			pc += 4;
			break;

		case INT_SET_VARIABLE_MULTIPLY:
			WRITE_INT(Program[pc + 1], READ_INT(Program[pc + 2]) * READ_INT(Program[pc + 3]));
			pc += 4;
			break;

		case INT_SET_VARIABLE_DIVIDE:
			if (READ_INT(Program[pc + 3]) != 0) {
				WRITE_INT(Program[pc + 1], READ_INT(Program[pc + 2]) / READ_INT(Program[pc + 3]));
			}
			pc += 4;
			break;

		case INT_SET_PWM:
			WRITE_PWM(Program[pc + 1]);	 // PWM frequency is ignored
			pc += 4;
			break;

		case INT_READ_ADC:
			READ_ADC(Program[pc + 1]);
			pc += 2;
			break;

		case INT_IF_BIT_SET:
			if (!READ_BIT(Program[pc+1])) pc += Program[pc + 2];
			pc += 3;
			break;

		case INT_IF_BIT_CLEAR:
			if (READ_BIT(Program[pc + 1])) pc += Program[pc + 2];
			pc += 3;
			break;

		case INT_IF_VARIABLE_LES_LITERAL:
			if (!(READ_INT(Program[pc + 1]) < (Program[pc + 2] + (Program[pc + 3] << 8)))) pc += Program[pc + 4];
			pc += 5;
			break;

		case INT_IF_VARIABLE_EQUALS_VARIABLE:
			if (!(READ_INT(Program[pc + 1]) == READ_INT(Program[pc + 2]))) pc += Program[pc + 3];
			pc += 4;
			break;

		case INT_IF_VARIABLE_GRT_VARIABLE:
			if (!(READ_INT(Program[pc + 1]) > READ_INT(Program[pc + 2]))) pc += Program[pc + 3];
			pc += 4;
			break;

		case INT_ELSE:
			pc += Program[pc+1];
			pc += 2;
			break;

		case INT_END_OF_PROGRAM:
			return;

		default:
			Serial.print("Unknown opcode: 0x");
			Serial.print(Program[pc], HEX);
			Serial.println("");
			Serial.print("PC: 0x");
			Serial.print(pc, HEX);
			Serial.println("");
			ProgramReady = false;
			return;
		}
	}
}

void LDuino_engine::Engine(void)
{
	int count = 10;

	if (time == 0) {
		time = millis();
		return;
	}

	while (ProgramReady && time + cycle_ms < millis() && count-- > 0) {
		unsigned long ts = micros();
		InterpretOneCycle();
		processing_time = micros() - ts;
		time = time + cycle_ms;
#if DEBUG
		while (!Serial.available());
		Serial.read();
#endif
	}
}

void LDuino_engine::WRITE_BIT(BYTE addr, boolean value)
{
	if (IO[addr].Value == value) return;

	switch (IO[addr].Map.type) {
	case IO_TYPE_DIG_OUTPUT:
		digitalWrite(IO[addr].Map.pin, value);
		break;
	case IO_TYPE_MODBUS_COIL:
		if (mb) mb->Coil(IO[addr].Map.ModbusOffset, value);
		break;
	}

	IO[addr].Value = value;
	D(Serial << "write bit[" << addr << "] " << value << "\n");
}

boolean LDuino_engine::READ_BIT(BYTE  addr)
{
	switch (IO[addr].Map.type) {
	case IO_TYPE_DIG_INPUT:
		IO[addr].Value = digitalRead(IO[addr].Map.pin);
		break;
	case IO_TYPE_MODBUS_COIL:
		if (mb) IO[addr].Value = mb->Coil(IO[addr].Map.ModbusOffset);
		break;
	}

	D(Serial << "read  bit[" << addr << "] " << IO[addr].Value << '\n');
	return IO[addr].Value;
}

void LDuino_engine::WRITE_INT(BYTE  addr, SWORD value)
{
	if (IO[addr].Value == value) return;

	switch (IO[addr].Map.type) {
	case IO_TYPE_MODBUS_HREG:
		if (mb) mb->Hreg(IO[addr].Map.ModbusOffset, value);
		break;
	}

	IO[addr].Value = value;
	D(Serial << "write int[" << addr << "] " << value << '\n');
}

LDuino_engine::SWORD LDuino_engine::READ_INT(BYTE  addr)
{
	switch (IO[addr].Map.type) {
	case IO_TYPE_MODBUS_HREG:
		if (mb) IO[addr].Value = mb->Hreg(IO[addr].Map.ModbusOffset);
		break;
	}

	D(Serial << "read  int[" << addr << "] " << IO[addr].Value << '\n');
	return IO[addr].Value;
}

void LDuino_engine::WRITE_PWM(BYTE addr)
{
	analogWrite(IO[addr].Map.pin, IO[addr].Value);
}

void LDuino_engine::READ_ADC(BYTE addr)
{
	IO[addr].Value = analogRead(IO[addr].Map.pin);
}