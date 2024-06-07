 /*
 * Space_Packet_Protocol.c
 *
 *  Created on: 2024. gada 23. apr.
 *      Author: Rūdolfs
 */

#include "Space_Packet_Protocol.h"
#include <stdio.h>

uint8_t DEBUG_Space_Packet_Data_Buffer[256];
uint8_t OBC_Space_Packet_Data_Buffer[1024];

uint8_t DEBUGRxBuffer[COBS_FRAME_LEN];
uint8_t DEBUGTxBuffer[COBS_FRAME_LEN];
uint16_t SPP_DEBUG_recv_count = 0;
uint8_t SPP_DEBUG_recv_char = 0xff;

uint8_t OBCRxBuffer[COBS_FRAME_LEN];
uint8_t OBCTxBuffer[COBS_FRAME_LEN];
uint16_t SPP_OBC_recv_count = 0;
uint8_t SPP_OBC_recv_char = 0xff;


#define HK_TEST_LEN 172
uint8_t HK_test_packet[HK_TEST_LEN] = { // Hard coded test response
	        	0x09, 0x41, 0xC0, 0x1B, 0x00, 0xA5, 0x10, 0x03, 0x19, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x0D, 0x59,
		        0xAA, 0x00, 0x00, 0x64, 0x80, 0x60, 0x01, 0x00, 0x00, 0x12, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		        0x12, 0x23, 0x34, 0x45, 0x56, 0x78, 0x67, 0x89, 0x9A, 0xBC, 0xAB, 0x03, 0x00, 0x00, 0x00, 0x00,
		        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		        0x19, 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x2B, 0x00, 0xF4, 0xA6, 0x00, 0x00, 0x01, 0x00, 0x1B,
		        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x22, 0x00, 0x00, 0x00, 0x05, 0x0D, 0xC5, 0x10, 0x00,
		        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x0B, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
		        0x18, 0x0C, 0x00, 0x02, 0x00, 0x00, 0x01, 0x70, 0x00, 0x00, 0x0F, 0xAF, 0x00, 0x00, 0x2E, 0xED,
		        0x53, 0xC2, 0x61, 0x40, 0x00, 0x01, 0x00, 0x20, 0x00, 0x20, 0xFF, 0xFF, 0x60, 0x14, 0xD4, 0xC0,
		        0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC9, 0x55}; 


static SPP_error SPP_UART_transmit_DMA(uint8_t* data, uint16_t data_len) {
	*(data + data_len) = 0x00; // Adding sentinel value.
	data_len++;
    HAL_UART_Transmit_DMA(&SPP_DEBUG_UART, data, data_len);
    HAL_UART_Transmit(&SPP_OBC_UART, data, data_len, 100);
    return SPP_OK;
}

static SPP_error SPP_reset_UART_recv_DMA() {
    HAL_UART_Receive_DMA(&SPP_DEBUG_UART, DEBUG_Space_Packet_Data_Buffer, 1);
    HAL_UART_Receive_DMA(&SPP_OBC_UART, OBC_Space_Packet_Data_Buffer, 1);
    return SPP_OK;
};


void SPP_send_HK_test_packet() {
	HK_test_packet[3]++;
    HAL_UART_Transmit_DMA(&SPP_DEBUG_UART, HK_test_packet, HK_TEST_LEN);
}

// CRC16-CCITT
static uint16_t SPP_CRC16_byte(uint16_t crcValue, uint8_t newByte) {
	uint8_t i;

	for (i = 0; i < 8; i++) {

		if (((crcValue & 0x8000) >> 8) ^ (newByte & 0x80)){
			crcValue = (crcValue << 1)  ^ 0x1021;
		}else{
			crcValue = (crcValue << 1);
		}

		newByte <<= 1;
	}
  
	return crcValue;
}

static uint16_t SPP_calc_CRC16(uint8_t* data, uint16_t length) {
    uint16_t CRCvalue = 0xFFFF;
    for(uint16_t i = 0; i < length; i++) {
        CRCvalue = SPP_CRC16_byte(CRCvalue, data[i]);
    }
   
    return CRCvalue;
}


// CRC is calculated over all of the packet. The last two bytes are the received CRC.
SPP_error SPP_validate_checksum(uint8_t* packet, uint16_t packet_length) {
    uint16_t received_CRC = 0x0000;
    received_CRC |= (packet[packet_length - 2] << 8) | packet[packet_length - 1];
    uint16_t calculated_CRC = SPP_calc_CRC16(packet, packet_length - 2);
    if (received_CRC != calculated_CRC) {
        return SPP_PACKET_CRC_MISMATCH;
    } else {
        return SPP_OK;
    }
}


// Takes pointer to SPP_PRIMARY_HEADER_LEN length buffer.
// Takes pointer to SPP_primary_header_t struct.
// Fills primary_header with corresponding data from raw_buffer
// Return negative error code if failed, or 0 if correct. (SPP_error)
// CHECK SIZE OF raw_header BEFORE PASSING TO THIS FUNCTION
SPP_error SPP_decode_primary_header(uint8_t* raw_header, SPP_primary_header_t* primary_header) {
	primary_header->packet_version_number	= (raw_header[0] & 0xE0) >> 5;
	primary_header->packet_type 			= (raw_header[0] & 0x10) >> 4;
	primary_header->secondary_header_flag	= (raw_header[0] & 0x08) >> 3;
	primary_header->application_process_id	=((raw_header[0] & 0x03) << 8) | (raw_header[1]);
	primary_header->sequence_flags			= (raw_header[2] & 0xC0) >> 6;
	primary_header->packet_sequence_count	=((raw_header[2] & 0x3F) << 8) | (raw_header[3]);
	primary_header->packet_data_length		= (raw_header[4] << 8) 		   | (raw_header[5]);
    return SPP_OK;
}


// Takes filled SPP primary header struct, encodes into SPP_PRIMARY_HEADER_LEN length buffer
// and copies results do given buffer pointer
// Return negative error code if failed, or 0 if correct. (SPP_error)
// CHECK SIZE OF result_buffer BEFORE PASSING TO THIS FUNCTION
SPP_error SPP_encode_primary_header(SPP_primary_header_t* primary_header, uint8_t* result_buffer) {
    for(int i = 0; i < SPP_PRIMARY_HEADER_LEN; i++) {
        result_buffer[i] ^= result_buffer[i];    // Clear result buffer.
    }

    result_buffer[0] |=  primary_header->packet_version_number  << 5;
    result_buffer[0] |=  primary_header->packet_type            << 4;
    result_buffer[0] |=  primary_header->secondary_header_flag  << 3;
    result_buffer[0] |= (primary_header->application_process_id & 0x300) >> 8;
    result_buffer[1] |=  primary_header->application_process_id & 0x0FF;
    result_buffer[2] |=  primary_header->sequence_flags         << 6;
    result_buffer[2] |= (primary_header->packet_sequence_count  & 0x3F00) >> 8;
    result_buffer[3] |=  primary_header->packet_sequence_count  & 0x00FF;
    result_buffer[4] |= (primary_header->packet_data_length     & 0xFF00) >> 8;
    result_buffer[5] |=  primary_header->packet_data_length     & 0x00FF;
    return SPP_OK;
}


SPP_error SPP_decode_PUS_TC_header(uint8_t* raw_header, SPP_PUS_TC_header_t* secondary_header) {
    secondary_header->PUS_version_number = (raw_header[0] & 0xF0) >> 4;
    secondary_header->ACK_flags          = (raw_header[0] & 0x0F);
    secondary_header->service_type_id    =  raw_header[1];
    secondary_header->message_subtype_id =  raw_header[2];
    secondary_header->source_id          = (raw_header[3] << 8) | raw_header[4];
    secondary_header->spare              = 0; // Based on PUS message type I guess? (Optional)
    return SPP_OK;
}

// Technically this is not needed since, Langmuir Probe Payload will not send TCs.
SPP_error SPP_encode_PUS_TC_header(SPP_PUS_TC_header_t* secondary_header, uint8_t* result_buffer) {
    for(int i = 0; i < SPP_PUS_TC_HEADER_LEN_WO_SPARE; i++) {
        result_buffer[i] ^= result_buffer[i];    // Clear result buffer.
    }

    result_buffer[0] |=  secondary_header->PUS_version_number << 4;
    result_buffer[0] |=  secondary_header->ACK_flags;
    result_buffer[1] |=  secondary_header->service_type_id;
    result_buffer[2] |=  secondary_header->message_subtype_id;
    result_buffer[3] |= (secondary_header->source_id & 0xFF00) >> 8;
    result_buffer[4] |= (secondary_header->source_id & 0x00FF) >> 8;

    return SPP_OK;
};

// Technically this is not needed since, Langmuir Probe Payload will only send TMs.
SPP_error SPP_decode_PUS_TM_header(uint8_t* raw_header, SPP_PUS_TM_header_t* secondary_header) {
    secondary_header->PUS_version_number    = (raw_header[0] & 0xF0) >> 4;
    secondary_header->sc_time_ref_status    = (raw_header[0] & 0x0F);
    secondary_header->service_type_id       =  raw_header[1];
    secondary_header->message_subtype_id    =  raw_header[2];
    secondary_header->message_type_counter  = (raw_header[3] << 8) | raw_header[4];
    secondary_header->destination_id        = (raw_header[5] << 8) | raw_header[6];
    secondary_header->time                  = (raw_header[7] << 8) | raw_header[8];
    secondary_header->spare                 = 0; // Based on PUS message type I guess? (Optional)
    return SPP_OK;
}

SPP_error SPP_encode_PUS_TM_header(SPP_PUS_TM_header_t* secondary_header, uint8_t* result_buffer) {
    for(int i = 0; i < SPP_PUS_TM_HEADER_LEN_WO_SPARE; i++) {
        result_buffer[i] ^= result_buffer[i];    // Clear result buffer.
    }

    result_buffer[0] |=  secondary_header->PUS_version_number << 4;
    result_buffer[0] |=  secondary_header->sc_time_ref_status;
    result_buffer[1] |=  secondary_header->service_type_id;
    result_buffer[2] |=  secondary_header->message_subtype_id;
    result_buffer[3] |= (secondary_header->message_type_counter & 0xFF00) >> 8;
    result_buffer[4] |= (secondary_header->message_type_counter & 0x00FF) >> 8;
    result_buffer[5] |= (secondary_header->destination_id & 0xFF00) >> 8;
    result_buffer[6] |= (secondary_header->destination_id & 0x00FF) >> 8;
    result_buffer[7] |= (secondary_header->time & 0xFF00) >> 8;
    result_buffer[8] |= (secondary_header->time & 0x00FF) >> 8;
    
    return SPP_OK;
};


static SPP_primary_header_t SPP_make_new_primary_header(uint8_t packet_version_number, uint8_t packet_type, uint8_t secondary_header_flag, uint16_t application_process_id, uint8_t sequence_flags, uint16_t packet_sequence_count, uint16_t packet_data_length) {
    SPP_primary_header_t primary_header;
    primary_header.packet_version_number    = packet_version_number;
    primary_header.packet_type              = packet_type;
    primary_header.secondary_header_flag    = secondary_header_flag;
    primary_header.application_process_id   = application_process_id;
    primary_header.sequence_flags           = sequence_flags;
    primary_header.packet_sequence_count    = packet_sequence_count;
    primary_header.packet_data_length       = packet_data_length;
    return primary_header;
}


static SPP_PUS_TM_header_t SPP_make_new_PUS_TM_header(uint8_t PUS_version_number, uint8_t sc_time_ref_status, uint8_t service_type_id,
                                uint8_t message_subtype_id, uint16_t message_type_counter, uint16_t destination_id, uint16_t time) {
    SPP_PUS_TM_header_t PUS_TM_header;
    PUS_TM_header.PUS_version_number      =  PUS_version_number;
    PUS_TM_header.sc_time_ref_status      =  sc_time_ref_status;
    PUS_TM_header.service_type_id         =  service_type_id;
    PUS_TM_header.message_subtype_id      =  message_subtype_id;
    PUS_TM_header.message_type_counter    =  message_type_counter;
    PUS_TM_header.destination_id          =  destination_id;
    PUS_TM_header.time                    =  time;
    return PUS_TM_header;
}

// Flags denoting if an ACK TM message is requested for
// Success of request acceptence, start , progress and completion of execution
static inline uint8_t succ_acceptence_req(SPP_PUS_TC_header_t* secondary_header) {
    return secondary_header->ACK_flags & 0x08;
}
static inline uint8_t succ_start_req     (SPP_PUS_TC_header_t* secondary_header) {
    return secondary_header->ACK_flags & 0x04;
}
static inline uint8_t succ_progress_req  (SPP_PUS_TC_header_t* secondary_header) {
    return secondary_header->ACK_flags & 0x02;
}
static inline uint8_t succ_completion_req(SPP_PUS_TC_header_t* secondary_header) {
    return secondary_header->ACK_flags & 0x01;
}

static inline uint16_t byte_swap16(uint16_t value) {
    return ((value << 8) | (value >> 8)) & 0xFFFF;
}


static SPP_error SPP_add_CRC_to_msg(uint8_t* packet, uint16_t length, uint8_t* output) {
    uint16_t calculated_CRC = SPP_calc_CRC16(packet, length);
    uint16_t bs_CRC = byte_swap16(calculated_CRC); // (byteswapped CRC) - memcpy copies starting from LSB thus we swap.
    memcpy(output, &bs_CRC, CRC_BYTE_LEN);
    return SPP_OK;
}

static SPP_error SPP_add_data_to_packet(uint8_t* data, uint16_t data_len, uint8_t* packet) {
    memcpy(packet, data, data_len);
    return SPP_OK;
}


// HK - Housekeeping PUS service 3
SPP_error SPP_handle_HK_TC(SPP_PUS_TC_header_t* secondary_header) {
    if (secondary_header == NULL) {
        return UNDEFINED_ERROR;
    }


    if (secondary_header->message_subtype_id == 128) {
        
    }
    SPP_reset_UART_recv_DMA();
    return SPP_OK;
}

static SPP_error SPP_send_TM(SPP_primary_header_t* response_primary_header, SPP_PUS_TM_header_t* response_secondary_header, uint8_t* data, uint16_t data_len) {
    uint8_t response_TM_packet[SPP_MAX_PACKET_LEN];
    uint8_t response_TM_packet_COBS[SPP_MAX_PACKET_LEN];
    for(int i = 0; i < SPP_MAX_PACKET_LEN; i++) {
        response_TM_packet[i] = 0x00;
        response_TM_packet_COBS[i] = 0x00;
    }

    uint8_t* current_pointer = response_TM_packet;
    uint16_t packet_total_len = current_pointer - response_TM_packet;

    SPP_encode_primary_header(response_primary_header, current_pointer);
    current_pointer += SPP_PRIMARY_HEADER_LEN;
    packet_total_len = current_pointer - response_TM_packet;


    SPP_encode_PUS_TM_header(response_secondary_header, current_pointer);
    current_pointer += SPP_PUS_TM_HEADER_LEN_WO_SPARE;
    packet_total_len = current_pointer - response_TM_packet;

    if (data != NULL) {
        SPP_add_data_to_packet(data, data_len, current_pointer);
        current_pointer += data_len;
        packet_total_len = current_pointer - response_TM_packet;
    }

    SPP_add_CRC_to_msg(response_TM_packet, packet_total_len, current_pointer);
    current_pointer += CRC_BYTE_LEN;
    packet_total_len = current_pointer - response_TM_packet;

    uint16_t cobs_packet_total_len = COBS_encode(response_TM_packet, packet_total_len, response_TM_packet_COBS);
 
    memcpy(OBCTxBuffer, response_TM_packet_COBS, cobs_packet_total_len);
    SPP_UART_transmit_DMA(OBCTxBuffer, cobs_packet_total_len);
    return SPP_OK;
}



static SPP_error SPP_send_request_verification(SPP_primary_header_t* request_primary_header, SPP_PUS_TC_header_t* request_secondary_header, PUS_RV_Subtype_ID requested_ACK) {

    SPP_primary_header_t response_primary_header;
    SPP_PUS_TM_header_t response_PUS_TM_header;
    uint8_t data[SPP_PRIMARY_HEADER_LEN];

    response_primary_header = SPP_make_new_primary_header(SPP_VERSION, SPP_PACKET_TYPE_TM, request_primary_header->secondary_header_flag,
        request_primary_header->application_process_id, SPP_SEQUENCE_SEG_UNSEG, request_primary_header->packet_sequence_count,
        SPP_PUS_TM_HEADER_LEN_WO_SPARE + SPP_PRIMARY_HEADER_LEN + CRC_BYTE_LEN - 1
    );
    // Create response PUS TM header with 1,requested_ACK
    response_PUS_TM_header = SPP_make_new_PUS_TM_header(PUS_VERSION, 0, REQUEST_VERIFICATION_SERVICE_ID, requested_ACK,
        0, request_secondary_header->source_id, 0
    );

    // Data sent in request verification is the request primary header itself.
    // Thus we need to copy it into the data field of the response.
    SPP_encode_primary_header(request_primary_header, data);

    SPP_send_TM(&response_primary_header, &response_PUS_TM_header, data, SPP_PRIMARY_HEADER_LEN);
    return SPP_OK;
}


static SPP_error SPP_handle_TEST_TC(SPP_primary_header_t* request_primary_header, SPP_PUS_TC_header_t* request_secondary_header) {
    if (request_primary_header == NULL || request_secondary_header == NULL) {
        return UNDEFINED_ERROR;
    }

    SPP_primary_header_t response_primary_header;
    SPP_PUS_TM_header_t response_PUS_TM_header;

    if (request_secondary_header->message_subtype_id == T_ARE_YOU_ALIVE_TEST_ID) {
        if (succ_acceptence_req(request_secondary_header)) {
            SPP_send_request_verification(request_primary_header, request_secondary_header, RV_SUCC_ACCEPTANCE_VERIFICATION_ID);
        }
        if (succ_start_req(request_secondary_header)) {
            SPP_send_request_verification(request_primary_header, request_secondary_header, RV_SUCC_START_OF_EXEC_VERIFICATION_ID);
        }

        response_primary_header = SPP_make_new_primary_header(SPP_VERSION, SPP_PACKET_TYPE_TM, request_primary_header->secondary_header_flag,
            request_primary_header->application_process_id, SPP_SEQUENCE_SEG_UNSEG, request_primary_header->packet_sequence_count,
            SPP_PUS_TM_HEADER_LEN_WO_SPARE + CRC_BYTE_LEN - 1
        );

        if (succ_progress_req(request_secondary_header)) {
            SPP_send_request_verification(request_primary_header, request_secondary_header, RV_SUCC_PROG_OF_EXEC_VERIFICATION_ID);
        }

        // Create response PUS TM header with 17,2
        response_PUS_TM_header = SPP_make_new_PUS_TM_header(PUS_VERSION, 0, TEST_SERVICE_ID, T_ARE_YOU_ALIVE_TEST_REPORT_ID,
            0, request_secondary_header->source_id, 0
        );
    }

    SPP_send_TM(&response_primary_header, &response_PUS_TM_header, NULL, 0);

    if (succ_completion_req(request_secondary_header)) {
        SPP_send_request_verification(request_primary_header, request_secondary_header, RV_SUCC_COMPL_OF_EXEC_VERIFICATION_ID);
    }


    SPP_reset_UART_recv_DMA();
    return SPP_OK;
}


// Test function to check if decodeing and encoding and data seperation works correctly.
SPP_error SPP_handle_incoming_TC(SPP_TC_source source) {
    uint8_t* recv_buffer;
    uint8_t* packet_buffer; 

    if (source == OBC_TC) {
        recv_buffer   = OBCRxBuffer;
        packet_buffer = OBC_Space_Packet_Data_Buffer;
    } else if (source == DEBUG_TC) {
        recv_buffer   = DEBUGRxBuffer;
        packet_buffer = DEBUG_Space_Packet_Data_Buffer;
    } else {
    	return UNDEFINED_ERROR;
    }

    HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin);

    COBS_decode(recv_buffer, COBS_FRAME_LEN, packet_buffer);

    SPP_primary_header_t primary_header;
    SPP_decode_primary_header(packet_buffer, &primary_header);
    uint16_t space_packet_length = primary_header.packet_data_length + SPP_PRIMARY_HEADER_LEN + 1;

    SPP_error CRC_er = SPP_validate_checksum(packet_buffer, space_packet_length);
    
    if (CRC_er != SPP_OK) {
        return SPP_PACKET_CRC_MISMATCH;
    }
    
    if (primary_header.secondary_header_flag) {
        uint8_t secondary_header_buffer[SPP_PUS_TC_HEADER_LEN_WO_SPARE];
        memcpy(secondary_header_buffer, packet_buffer + SPP_PRIMARY_HEADER_LEN, SPP_PUS_TC_HEADER_LEN_WO_SPARE);

        SPP_PUS_TC_header_t PUS_TC_header;
        SPP_decode_PUS_TC_header(secondary_header_buffer, &PUS_TC_header);
        
        if (PUS_TC_header.service_type_id == HOUSEKEEPING_SERVICE_ID) {
            SPP_handle_HK_TC(&PUS_TC_header);
        } 
        else if (PUS_TC_header.service_type_id == TEST_SERVICE_ID) {
            SPP_handle_TEST_TC(&primary_header, &PUS_TC_header);
        }
    }
    return SPP_OK;
}



