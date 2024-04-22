#include "GS_Telemetry.h"

uint8_t TelemetryTX[77];
uint8_t uC_GS_ID;

void SendTelemetry() {
	uint8_t StatusPacket[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	StatusPacket[0] = 64;
	StatusPacket[1] = 0;
	StatusPacket[2] = 0;
	StatusPacket[3] = 0;
	StatusPacket[4] = 0;
	StatusPacket[5] = 0;
	StatusPacket[6] = 0;
	StatusPacket[7] ^= (-HAL_GPIO_ReadPin(SD_SW_B_GPIO_Port, SD_SW_B_Pin) ^ StatusPacket[7]) & (1 << 2);
	StatusPacket[7] ^= (-uCFileOpen ^ StatusPacket[7]) & (1 << 1);
	StatusPacket[7] ^= (-FPGAFileOpen ^ StatusPacket[7]) & (1 << 0);


	uint16_t ch_0 = ((uint16_t) (temperature * 10)) & 0x0FFF;
	uint16_t ch_1 = ((uint16_t) (vbat * 100)) & 0x0FFF;
	uint16_t ch_2 = ((uint16_t) (fpga3v * 100)) & 0x0FFF;
	uint16_t ch_3 = ((uint16_t) (fpga1p5v * 100)) & 0x0FFF;
	uint16_t ch_4 = ((uint16_t) (uc3v * 100)) & 0x0FFF;
	uint16_t ch_5 = currentDataRate & 0x0FFF;
	uint16_t ch_6 = writeQueueSize;
	uint16_t ch_7 = 0;
	uint16_t ch_8 = 0;
	uint16_t ch_9 = 0;
	uint16_t ch_10 = 0;
	uint16_t ch_11 = 0;

	uint8_t channels0_3[6];
	uint8_t channels4_7[6];
	uint8_t channels8_11[6];
	uint8_t channels0_3_ASCII[12];
	uint8_t channels4_7_ASCII[12];
	uint8_t channels8_11_ASCII[12];


	// Converts to big endian and puts 3 12 bit values into 6 byte memory
	channels0_3[0] = (*((uint8_t*) &ch_0 + 1) << 4) | (*((uint8_t*) &ch_0) >> 4);
	channels0_3[1] = (*((uint8_t*) &ch_0) << 4) | *((uint8_t*) &ch_1 + 1);
	channels0_3[2] = *((uint8_t*) &ch_1);
	channels0_3[3] = (*((uint8_t*) &ch_2 + 1) << 4) | (*((uint8_t*) &ch_2) >> 4);
	channels0_3[4] = (*((uint8_t*) &ch_2) << 4) | *((uint8_t*) &ch_3 + 1);
	channels0_3[5] = *((uint8_t*) &ch_3);

	channels4_7[0] = (*((uint8_t*) &ch_4 + 1) << 4) | (*((uint8_t*) &ch_4) >> 4);
	channels4_7[1] = (*((uint8_t*) &ch_4) << 4) | *((uint8_t*) &ch_5 + 1);
	channels4_7[2] = *((uint8_t*) &ch_5);
	channels4_7[3] = (*((uint8_t*) &ch_6 + 1) << 4) | (*((uint8_t*) &ch_6) >> 4);
	channels4_7[4] = (*((uint8_t*) &ch_6) << 4) | *((uint8_t*) &ch_7 + 1);
	channels4_7[5] = *((uint8_t*) &ch_7);

	channels8_11[0] = (*((uint8_t*) &ch_8 + 1) << 4) | (*((uint8_t*) &ch_8) >> 4);
	channels8_11[1] = (*((uint8_t*) &ch_8) << 4) | *((uint8_t*) &ch_9 + 1);
	channels8_11[2] = *((uint8_t*) &ch_9);
	channels8_11[3] = (*((uint8_t*) &ch_10 + 1) << 4) | (*((uint8_t*) &ch_10) >> 4);
	channels8_11[4] = (*((uint8_t*) &ch_10) << 4) | *((uint8_t*) &ch_11 + 1);
	channels8_11[5] = *((uint8_t*) &ch_11);

	bytesToASCII(channels0_3, channels0_3_ASCII, 6);
	bytesToASCII(channels4_7, channels4_7_ASCII, 6);
	bytesToASCII(channels8_11, channels8_11_ASCII, 6);

	uint8_t StatusPacketASCII[16];
	bytesToASCII(StatusPacket, StatusPacketASCII, 8);

	TelemetryTX[0] = '#';
	TelemetryTX[1] = uC_GS_ID;
	memcpy(&TelemetryTX[2], StatusPacketASCII, 16);
	memcpy(&TelemetryTX[18], "01AD", 4);
	memcpy(&TelemetryTX[22], channels0_3_ASCII, 12);
	memcpy(&TelemetryTX[34], "02AD", 4);
	memcpy(&TelemetryTX[38], channels4_7_ASCII, 12);
	memcpy(&TelemetryTX[50], "03AD", 4);
	memcpy(&TelemetryTX[54], channels8_11_ASCII, 12);
	TelemetryTX[66] = 0x0A;

	HAL_GPIO_WritePin(UC_CONSOLE_EN_GPIO_Port, UC_CONSOLE_EN_Pin, GPIO_PIN_SET);
	FPGA_Transmit_Binary(TelemetryTX, 67);
	HAL_GPIO_WritePin(UC_CONSOLE_EN_GPIO_Port, UC_CONSOLE_EN_Pin, GPIO_PIN_RESET);
}

void bytesToASCII(uint8_t* data, uint8_t* ascii_data, size_t data_length) {
	uint8_t ascii_idx = 0;

	for (int i = 0; i < data_length; i++) {
		uint8_t ascii1 = data[i] >> 4;
		uint8_t ascii2 = data[i] & 0x0F;

		switch (ascii1) {
		case 15:
			ascii_data[ascii_idx] = 'F';
			break;
		case 14:
			ascii_data[ascii_idx] = 'E';
			break;
		case 13:
			ascii_data[ascii_idx] = 'D';
			break;
		case 12:
			ascii_data[ascii_idx] = 'C';
			break;
		case 11:
			ascii_data[ascii_idx] = 'B';
			break;
		case 10:
			ascii_data[ascii_idx] = 'A';
			break;
		default:
			ascii_data[ascii_idx] = ascii1 + 48;
		}

		ascii_idx++;

		switch (ascii2) {
		case 15:
			ascii_data[ascii_idx] = 'F';
			break;
		case 14:
			ascii_data[ascii_idx] = 'E';
			break;
		case 13:
			ascii_data[ascii_idx] = 'D';
			break;
		case 12:
			ascii_data[ascii_idx] = 'C';
			break;
		case 11:
			ascii_data[ascii_idx] = 'B';
			break;
		case 10:
			ascii_data[ascii_idx] = 'A';
			break;
		default:
			ascii_data[ascii_idx] = ascii2 + 48;
		}

		ascii_idx++;
	}
}
