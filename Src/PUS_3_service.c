/*
 * PUS_3_service.c
 *
 *  Created on: 2024. gada 12. jūn.
 *      Author: Rūdolfs Arvīds Kalniņš <rakal@kth.se>
 */
#include "Space_Packet_Protocol.h"

#define MAX_PAR_COUNT       16
#define MAX_STRUCT_COUNT    16
#define MAX_TM_DATA_LEN     (MAX_PAR_COUNT * 4) + 2 // Each parameter is potentialy 4 bytes and struct id is 2 bytes.
#define DEF_COL_INTV        500
#define DEF_UC_N1           3
#define DEF_UC_PS           false

#define DEF_FPGA_N1         2
#define DEF_FPGA_PS         false

#define HK_SPP_APP_ID        61  // Just some random numbers.
#define HK_PUS_SOURCE_ID     14

extern uint16_t temperature_i;
extern uint16_t uc3v_i;
extern uint16_t fpga3v_i;
extern uint16_t fpga1p5v_i;
extern uint16_t vbat_i;

typedef struct {
    uint16_t SID;
    uint16_t collection_interval;
    uint16_t N1;
    uint32_t parameters[MAX_PAR_COUNT]; // 32 bit here for future proofing. All params currently are 16-bit.
    bool     periodic_send;
    uint32_t last_collect_tick;
    uint32_t seq_count;
} HK_par_report_structure_t;

typedef enum {
    UC_SID            = 0xAAAA,
    FPGA_SID          = 0x5555,
} HK_SID;


HK_par_report_structure_t HKPRS_uc = {
    .SID                    = UC_SID,
    .collection_interval    = DEF_COL_INTV,
    .N1                     = DEF_UC_N1,
    .parameters             = {0},
    .periodic_send          = DEF_UC_PS,
    .last_collect_tick      = 0,
    .seq_count              = 0,
};

HK_par_report_structure_t HKPRS_fpga = {
    .SID                    = FPGA_SID,
    .collection_interval    = DEF_COL_INTV,
    .N1                     = DEF_FPGA_N1,
    .parameters             = {0},
    .periodic_send          = DEF_FPGA_PS,
    .last_collect_tick      = 0,
    .seq_count              = 0,
};

HK_par_report_structure_t HKPRS_err = {
    .SID                    = 0,
    .collection_interval    = 0,
    .N1                     = 0,
    .parameters             = {0},
    .periodic_send          = 0,
    .last_collect_tick      = 0,
    .seq_count              = 0,
};

static HK_par_report_structure_t* get_HKPRS(uint16_t SID) {
    HK_par_report_structure_t* selected_HKPRS;
    if (SID == UC_SID) {
        selected_HKPRS = &HKPRS_uc;
    } else if (SID == FPGA_SID) {
        selected_HKPRS = &HKPRS_fpga;
    } else {
        selected_HKPRS = &HKPRS_err; // This is kind of bad, probably should return an error code.
    }
    return selected_HKPRS;
}



static void fill_report_struct(uint16_t SID) {
    uint16_t s_vbat = vbat_i;
    uint16_t s_temp = temperature_i;
    uint16_t s_fpga3v = fpga3v_i;
    uint16_t s_fpga1p5v = fpga1p5v_i;
    uint16_t s_uc3v = uc3v_i;

    uint32_t uc_pars[DEF_UC_N1] = {s_vbat, s_temp, s_uc3v};
    uint32_t fpga_pars[DEF_FPGA_N1] = {s_fpga1p5v, s_fpga3v};

    HK_par_report_structure_t* HKPRS = get_HKPRS(SID);
    switch(SID) {
        case UC_SID:
            for(int i = 0; i < HKPRS->N1; i++) {
                HKPRS->parameters[i] = uc_pars[i];
            }
            break;
        case FPGA_SID:
            for(int i = 0; i < HKPRS->N1; i++) {
                HKPRS->parameters[i] = fpga_pars[i];
            }
            break;
    }  
}


static uint16_t encode_HK_struct(HK_par_report_structure_t* HKPRS, uint8_t* out_buffer) {
    uint8_t* orig_pointer = out_buffer;
    memcpy(out_buffer, &(HKPRS->SID), sizeof(HKPRS->SID));
    out_buffer += sizeof(HKPRS->SID);

    for (int i = 0; i < HKPRS->N1; i++) {
        memcpy(out_buffer, &(HKPRS->parameters[i]), sizeof(HKPRS->parameters[i]));
        out_buffer += sizeof(HKPRS->parameters[i]);
    }
    return out_buffer - orig_pointer;
}



static void send_HK_struct(SPP_header_t* req_p_header , PUS_TC_header_t* req_s_header, uint8_t* data, uint16_t SID) {
    HK_par_report_structure_t* HKPRS = get_HKPRS(SID);
    uint8_t TM_data[MAX_TM_DATA_LEN];
    uint16_t HK_data_len = encode_HK_struct(HKPRS, TM_data);
    SPP_header_t TM_SPP_header = SPP_make_header(
        SPP_VERSION,
        SPP_PACKET_TYPE_TM,
        1,
        req_p_header->application_process_id,
        SPP_SEQUENCE_SEG_UNSEG,
        HKPRS->seq_count,
        SPP_PUS_TM_HEADER_LEN_WO_SPARE + HK_data_len + CRC_BYTE_LEN - 1
    );
    PUS_TM_header_t TM_PUS_header  = PUS_make_TM_header(
        PUS_VERSION,
        0,
        HOUSEKEEPING_SERVICE_ID,
        HK_PARAMETER_REPORT,
        0,
        req_s_header->source_id,
        0
    );
    SPP_send_TM(&TM_SPP_header, &TM_PUS_header, TM_data, HK_data_len);
    HKPRS->seq_count++;
}

static void send_one_shot(SPP_header_t* req_p_header , PUS_TC_header_t* req_s_header, uint8_t* data) {
    uint16_t nof_structs = 0;
    memcpy(&nof_structs, data, sizeof(nof_structs));
    data += sizeof(nof_structs);
    
    uint16_t SIDs[MAX_STRUCT_COUNT];
    for (int i = 0; i < nof_structs; i++) {
        memcpy(&(SIDs[i]), data, sizeof(SIDs[i]));
        data += sizeof(SIDs[i]);
    }
    for (int i = 0; i < nof_structs; i++) {
        uint16_t SID = SIDs[i];
        send_HK_struct(req_p_header, req_s_header, data, SID);
    }
}

static void set_periodic_report(uint8_t* data, bool state) {
    uint16_t nof_SIDs = 0;
    memcpy(&nof_SIDs, data, sizeof(nof_SIDs));
    data += sizeof(nof_SIDs);

    for(int i = 0; i < nof_SIDs; i++) {
        uint16_t SID = 0;
        memcpy(&SID, data, sizeof(SID));
        data += sizeof(SID);

        switch(SID) {
            case UC_SID:
                HKPRS_uc.periodic_send = state;
                break;
            case FPGA_SID:
                HKPRS_fpga.periodic_send = state;
                break;
        }
    }
}


void SPP_collect_HK_data(uint32_t current_ticks) {
    if ((current_ticks - HKPRS_uc.last_collect_tick) > HKPRS_uc.collection_interval) {
        fill_report_struct(HKPRS_uc.SID);
        HKPRS_uc.last_collect_tick = current_ticks;
    }
    if ((current_ticks - HKPRS_fpga.last_collect_tick) > HKPRS_fpga.collection_interval) {
        fill_report_struct(HKPRS_fpga.SID);
        HKPRS_fpga.last_collect_tick = current_ticks;
    }
}


static void HK_make_headers_send(uint16_t sel_SID) {
        HK_par_report_structure_t* HKPRS = get_HKPRS(sel_SID);
        uint8_t TM_data[MAX_TM_DATA_LEN];
        uint16_t HK_data_len = encode_HK_struct(HKPRS, TM_data);
        SPP_header_t TM_SPP_header = SPP_make_header(
            SPP_VERSION,
            SPP_PACKET_TYPE_TM,
            1,
            HK_SPP_APP_ID,
            SPP_SEQUENCE_SEG_UNSEG,
            HKPRS->seq_count,
            SPP_PUS_TM_HEADER_LEN_WO_SPARE + HK_data_len + CRC_BYTE_LEN - 1
        );
        PUS_TM_header_t TM_PUS_header  = PUS_make_TM_header(
            PUS_VERSION,
            0,
            HOUSEKEEPING_SERVICE_ID,
            HK_PARAMETER_REPORT,
            0,
            HK_PUS_SOURCE_ID,
            0
        );
        SPP_send_TM(&TM_SPP_header, &TM_PUS_header, TM_data, HK_data_len);
        HKPRS->seq_count++;
}


void SPP_periodic_HK_send() {
    if (HKPRS_uc.periodic_send) {
        HK_make_headers_send(UC_SID);
    }
    if (HKPRS_fpga.periodic_send) {
        HK_make_headers_send(FPGA_SID);
    }
}

// HK - Housekeeping PUS service 3
SPP_error SPP_handle_HK_TC(SPP_header_t* SPP_header , PUS_TC_header_t* secondary_header, uint8_t* data) {
    if (Current_Global_Device_State != NORMAL_MODE) {
        return UNDEFINED_ERROR;
    }
    if (secondary_header == NULL) {
        return UNDEFINED_ERROR;
    }

   if (secondary_header->message_subtype_id == HK_EN_PERIODIC_REPORTS) {
        set_periodic_report(data, true);
    } else if (secondary_header->message_subtype_id == HK_DIS_PERIODIC_REPORTS) {
        set_periodic_report(data, false);
    } else if (secondary_header->message_subtype_id == HK_ONE_SHOT) {
        send_one_shot(SPP_header, secondary_header, data);
    }
    return SPP_OK;
}
