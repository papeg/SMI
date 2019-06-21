#define BUFFER_SIZE 1024

#include "smi/channel_helpers.h"


// the maximum number of consecutive reads that each CKs/CKr can do from the same channel
#define READS_LIMIT 8
// maximum number of ranks in the cluster
#define MAX_RANKS 8


// QSFP channels
#ifndef SMI_EMULATION_RANK
channel SMI_Network_message io_out_0 __attribute__((depth(16))) __attribute__((io("kernel_output_ch0")));
channel SMI_Network_message io_in_0 __attribute__((depth(16))) __attribute__((io("kernel_input_ch0")));
channel SMI_Network_message io_out_1 __attribute__((depth(16))) __attribute__((io("kernel_output_ch1")));
channel SMI_Network_message io_in_1 __attribute__((depth(16))) __attribute__((io("kernel_input_ch1")));
channel SMI_Network_message io_out_2 __attribute__((depth(16))) __attribute__((io("kernel_output_ch2")));
channel SMI_Network_message io_in_2 __attribute__((depth(16))) __attribute__((io("kernel_input_ch2")));
channel SMI_Network_message io_out_3 __attribute__((depth(16))) __attribute__((io("kernel_output_ch3")));
channel SMI_Network_message io_in_3 __attribute__((depth(16))) __attribute__((io("kernel_input_ch3")));
#else
#if SMI_EMULATION_RANK == 0
channel SMI_Network_message io_out_0 __attribute__((depth(16))) __attribute__((io("emulated_channel_r0c0_r6c1")));
channel SMI_Network_message io_in_0 __attribute__((depth(16))) __attribute__((io("emulated_channel_r6c1_r0c0")));
channel SMI_Network_message io_out_1 __attribute__((depth(16))) __attribute__((io("emulated_channel_r0c1_r2c0")));
channel SMI_Network_message io_in_1 __attribute__((depth(16))) __attribute__((io("emulated_channel_r2c0_r0c1")));
channel SMI_Network_message io_out_2 __attribute__((depth(16))) __attribute__((io("emulated_channel_r0c2_r1c3")));
channel SMI_Network_message io_in_2 __attribute__((depth(16))) __attribute__((io("emulated_channel_r1c3_r0c2")));
channel SMI_Network_message io_out_3 __attribute__((depth(16))) __attribute__((io("emulated_channel_r0c3_r1c2")));
channel SMI_Network_message io_in_3 __attribute__((depth(16))) __attribute__((io("emulated_channel_r1c2_r0c3")));
#endif
#endif

/**
  These four tables, defined at compile time, maps application endpoints (Port) to CKs/CKr and are
  used by the compiler to lay down the circuitry. The data routing table is used by push (and collectives)
  to send the actual communication data, while the control is used by push (and collective) to receive
  control information (e.g. rendezvous data) from the pairs
*/
// logical port -> index in channels_to_ck_s -> ck_s channel
__constant char internal_to_cks_data_rt[2] = { 0, 1 };
__constant char internal_to_cks_control_rt[2] = { -1, -1 };

// logical port -> index in channels_to_ck_r -> ck_r channel
__constant char internal_from_ckr_data_rt[2] = { -1, -1 };
__constant char internal_from_ckr_control_rt[2] = { 0, 1 };

channel SMI_Network_message channels_cks_data[2] __attribute__((depth(16)));
channel SMI_Network_message channels_cks_control[0] __attribute__((depth(16)));
channel SMI_Network_message channels_ckr_data[0] __attribute__((depth(BUFFER_SIZE)));
channel SMI_Network_message channels_ckr_control[2] __attribute__((depth(BUFFER_SIZE)));

__constant char QSFP_COUNT = 4;

// connect all CK_S together
channel SMI_Network_message channels_interconnect_ck_s[QSFP_COUNT*(QSFP_COUNT-1)] __attribute__((depth(16)));

// connect all CK_R together
channel SMI_Network_message channels_interconnect_ck_r[QSFP_COUNT*(QSFP_COUNT-1)] __attribute__((depth(16)));

// connect corresponding CK_S/CK_R pairs
channel SMI_Network_message channels_interconnect_ck_s_to_ck_r[QSFP_COUNT] __attribute__((depth(16)));

// connect corresponding CK_R/CK_S pairs
channel SMI_Network_message channels_interconnect_ck_r_to_ck_s[QSFP_COUNT] __attribute__((depth(16)));

#include "smi/pop.h"
#include "smi/push.h"

__kernel void CK_S_0(__global volatile char *restrict rt, const int num_ranks)
{
    char external_routing_table[MAX_RANKS];
    for (int i = 0; i < MAX_RANKS; i++)
    {
        if (i < num_ranks)
        {
            external_routing_table[i] = rt[i];
        }
    }

    // number of CK_S - 1 + CK_R + 1 CKS hardware ports
    const char num_sender = 5;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;

    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // receive from CK_S_1
                message = read_channel_nb_intel(channels_interconnect_ck_s[0], &valid);
                break;
            case 1:
                // receive from CK_S_2
                message = read_channel_nb_intel(channels_interconnect_ck_s[1], &valid);
                break;
            case 2:
                // receive from CK_S_3
                message = read_channel_nb_intel(channels_interconnect_ck_s[2], &valid);
                break;
            case 3:
                // receive from CK_R_0
                message = read_channel_nb_intel(channels_interconnect_ck_r_to_ck_s[0], &valid);
                break;
            case 4:
                // receive from app channel on hardware port 0/data
                message = read_channel_nb_intel(channels_cks_data[0], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char idx = external_routing_table[GET_HEADER_DST(message.header)];
            switch (idx)
            {
                case 0:
                    // send to QSFP
                    write_channel_intel(io_out_0, message);
                    break;
                case 1:
                    // send to CK_R_0
                    write_channel_intel(channels_interconnect_ck_s_to_ck_r[0], message);
                    break;
                case 2:
                    // send to CK_S_1
                    write_channel_intel(channels_interconnect_ck_s[3], message);
                    break;
                case 3:
                    // send to CK_S_2
                    write_channel_intel(channels_interconnect_ck_s[6], message);
                    break;
                case 4:
                    // send to CK_S_3
                    write_channel_intel(channels_interconnect_ck_s[9], message);
                    break;
            }
        }
        if (!valid || contiguous_reads == READS_LIMIT)
        {
            contiguous_reads = 0;
            sender_id++;
            if (sender_id == num_sender)
            {
                sender_id = 0;
            }
        }
    }
}
__kernel void CK_R_0(__global volatile char *restrict rt, const char rank)
{
    // rt contains intertwined (dp0, cp0, dp1, cp1, ...)
    char external_routing_table[2 /* logical port count */][2];
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            external_routing_table[i][j] = rt[i * 2 + j];
        }
    }

    // QSFP + number of CK_Rs - 1 + CK_S
    const char num_sender = 5;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;
    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // QSFP
                message = read_channel_nb_intel(io_in_0, &valid);
                break;
            case 1:
                // receive from CK_R_1
                message = read_channel_nb_intel(channels_interconnect_ck_r[0], &valid);
                break;
            case 2:
                // receive from CK_R_2
                message = read_channel_nb_intel(channels_interconnect_ck_r[1], &valid);
                break;
            case 3:
                // receive from CK_R_3
                message = read_channel_nb_intel(channels_interconnect_ck_r[2], &valid);
                break;
            case 4:
                // receive from CK_S_0
                message = read_channel_nb_intel(channels_interconnect_ck_s_to_ck_r[0], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char dest;
            if (GET_HEADER_DST(message.header) != rank)
            {
                dest = 0;
            }
            else dest = external_routing_table[GET_HEADER_PORT(message.header)][GET_HEADER_OP(message.header) == SMI_REQUEST];

            switch (dest)
            {
                case 0:
                    // send to CK_S_0
                    write_channel_intel(channels_interconnect_ck_r_to_ck_s[0], message);
                    break;
                case 1:
                    // send to CK_R_1
                    write_channel_intel(channels_interconnect_ck_r[3], message);
                    break;
                case 2:
                    // send to CK_R_2
                    write_channel_intel(channels_interconnect_ck_r[6], message);
                    break;
                case 3:
                    // send to CK_R_3
                    write_channel_intel(channels_interconnect_ck_r[9], message);
                    break;
                case 4:
                    // send to app channel with hardware port 0/control
                    write_channel_intel(channels_ckr_control[0], message);
                    break;
            }
        }

        if (!valid || contiguous_reads == READS_LIMIT)
        {
            contiguous_reads = 0;
            sender_id++;
            if (sender_id == num_sender)
            {
                sender_id = 0;
            }
        }
    }
}
__kernel void CK_S_1(__global volatile char *restrict rt, const int num_ranks)
{
    char external_routing_table[MAX_RANKS];
    for (int i = 0; i < MAX_RANKS; i++)
    {
        if (i < num_ranks)
        {
            external_routing_table[i] = rt[i];
        }
    }

    // number of CK_S - 1 + CK_R + 1 CKS hardware ports
    const char num_sender = 5;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;

    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // receive from CK_S_0
                message = read_channel_nb_intel(channels_interconnect_ck_s[3], &valid);
                break;
            case 1:
                // receive from CK_S_2
                message = read_channel_nb_intel(channels_interconnect_ck_s[4], &valid);
                break;
            case 2:
                // receive from CK_S_3
                message = read_channel_nb_intel(channels_interconnect_ck_s[5], &valid);
                break;
            case 3:
                // receive from CK_R_1
                message = read_channel_nb_intel(channels_interconnect_ck_r_to_ck_s[1], &valid);
                break;
            case 4:
                // receive from app channel on hardware port 1/data
                message = read_channel_nb_intel(channels_cks_data[1], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char idx = external_routing_table[GET_HEADER_DST(message.header)];
            switch (idx)
            {
                case 0:
                    // send to QSFP
                    write_channel_intel(io_out_1, message);
                    break;
                case 1:
                    // send to CK_R_1
                    write_channel_intel(channels_interconnect_ck_s_to_ck_r[1], message);
                    break;
                case 2:
                    // send to CK_S_0
                    write_channel_intel(channels_interconnect_ck_s[0], message);
                    break;
                case 3:
                    // send to CK_S_2
                    write_channel_intel(channels_interconnect_ck_s[7], message);
                    break;
                case 4:
                    // send to CK_S_3
                    write_channel_intel(channels_interconnect_ck_s[10], message);
                    break;
            }
        }
        if (!valid || contiguous_reads == READS_LIMIT)
        {
            contiguous_reads = 0;
            sender_id++;
            if (sender_id == num_sender)
            {
                sender_id = 0;
            }
        }
    }
}
__kernel void CK_R_1(__global volatile char *restrict rt, const char rank)
{
    // rt contains intertwined (dp0, cp0, dp1, cp1, ...)
    char external_routing_table[2 /* logical port count */][2];
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            external_routing_table[i][j] = rt[i * 2 + j];
        }
    }

    // QSFP + number of CK_Rs - 1 + CK_S
    const char num_sender = 5;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;
    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // QSFP
                message = read_channel_nb_intel(io_in_1, &valid);
                break;
            case 1:
                // receive from CK_R_0
                message = read_channel_nb_intel(channels_interconnect_ck_r[3], &valid);
                break;
            case 2:
                // receive from CK_R_2
                message = read_channel_nb_intel(channels_interconnect_ck_r[4], &valid);
                break;
            case 3:
                // receive from CK_R_3
                message = read_channel_nb_intel(channels_interconnect_ck_r[5], &valid);
                break;
            case 4:
                // receive from CK_S_1
                message = read_channel_nb_intel(channels_interconnect_ck_s_to_ck_r[1], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char dest;
            if (GET_HEADER_DST(message.header) != rank)
            {
                dest = 0;
            }
            else dest = external_routing_table[GET_HEADER_PORT(message.header)][GET_HEADER_OP(message.header) == SMI_REQUEST];

            switch (dest)
            {
                case 0:
                    // send to CK_S_1
                    write_channel_intel(channels_interconnect_ck_r_to_ck_s[1], message);
                    break;
                case 1:
                    // send to CK_R_0
                    write_channel_intel(channels_interconnect_ck_r[0], message);
                    break;
                case 2:
                    // send to CK_R_2
                    write_channel_intel(channels_interconnect_ck_r[7], message);
                    break;
                case 3:
                    // send to CK_R_3
                    write_channel_intel(channels_interconnect_ck_r[10], message);
                    break;
                case 4:
                    // send to app channel with hardware port 1/control
                    write_channel_intel(channels_ckr_control[1], message);
                    break;
            }
        }

        if (!valid || contiguous_reads == READS_LIMIT)
        {
            contiguous_reads = 0;
            sender_id++;
            if (sender_id == num_sender)
            {
                sender_id = 0;
            }
        }
    }
}
__kernel void CK_S_2(__global volatile char *restrict rt, const int num_ranks)
{
    char external_routing_table[MAX_RANKS];
    for (int i = 0; i < MAX_RANKS; i++)
    {
        if (i < num_ranks)
        {
            external_routing_table[i] = rt[i];
        }
    }

    // number of CK_S - 1 + CK_R + 0 CKS hardware ports
    const char num_sender = 4;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;

    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // receive from CK_S_0
                message = read_channel_nb_intel(channels_interconnect_ck_s[6], &valid);
                break;
            case 1:
                // receive from CK_S_1
                message = read_channel_nb_intel(channels_interconnect_ck_s[7], &valid);
                break;
            case 2:
                // receive from CK_S_3
                message = read_channel_nb_intel(channels_interconnect_ck_s[8], &valid);
                break;
            case 3:
                // receive from CK_R_2
                message = read_channel_nb_intel(channels_interconnect_ck_r_to_ck_s[2], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char idx = external_routing_table[GET_HEADER_DST(message.header)];
            switch (idx)
            {
                case 0:
                    // send to QSFP
                    write_channel_intel(io_out_2, message);
                    break;
                case 1:
                    // send to CK_R_2
                    write_channel_intel(channels_interconnect_ck_s_to_ck_r[2], message);
                    break;
                case 2:
                    // send to CK_S_0
                    write_channel_intel(channels_interconnect_ck_s[1], message);
                    break;
                case 3:
                    // send to CK_S_1
                    write_channel_intel(channels_interconnect_ck_s[4], message);
                    break;
                case 4:
                    // send to CK_S_3
                    write_channel_intel(channels_interconnect_ck_s[11], message);
                    break;
            }
        }
        if (!valid || contiguous_reads == READS_LIMIT)
        {
            contiguous_reads = 0;
            sender_id++;
            if (sender_id == num_sender)
            {
                sender_id = 0;
            }
        }
    }
}
__kernel void CK_R_2(__global volatile char *restrict rt, const char rank)
{
    // rt contains intertwined (dp0, cp0, dp1, cp1, ...)
    char external_routing_table[2 /* logical port count */][2];
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            external_routing_table[i][j] = rt[i * 2 + j];
        }
    }

    // QSFP + number of CK_Rs - 1 + CK_S
    const char num_sender = 5;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;
    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // QSFP
                message = read_channel_nb_intel(io_in_2, &valid);
                break;
            case 1:
                // receive from CK_R_0
                message = read_channel_nb_intel(channels_interconnect_ck_r[6], &valid);
                break;
            case 2:
                // receive from CK_R_1
                message = read_channel_nb_intel(channels_interconnect_ck_r[7], &valid);
                break;
            case 3:
                // receive from CK_R_3
                message = read_channel_nb_intel(channels_interconnect_ck_r[8], &valid);
                break;
            case 4:
                // receive from CK_S_2
                message = read_channel_nb_intel(channels_interconnect_ck_s_to_ck_r[2], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char dest;
            if (GET_HEADER_DST(message.header) != rank)
            {
                dest = 0;
            }
            else dest = external_routing_table[GET_HEADER_PORT(message.header)][GET_HEADER_OP(message.header) == SMI_REQUEST];

            switch (dest)
            {
                case 0:
                    // send to CK_S_2
                    write_channel_intel(channels_interconnect_ck_r_to_ck_s[2], message);
                    break;
                case 1:
                    // send to CK_R_0
                    write_channel_intel(channels_interconnect_ck_r[1], message);
                    break;
                case 2:
                    // send to CK_R_1
                    write_channel_intel(channels_interconnect_ck_r[4], message);
                    break;
                case 3:
                    // send to CK_R_3
                    write_channel_intel(channels_interconnect_ck_r[11], message);
                    break;
            }
        }

        if (!valid || contiguous_reads == READS_LIMIT)
        {
            contiguous_reads = 0;
            sender_id++;
            if (sender_id == num_sender)
            {
                sender_id = 0;
            }
        }
    }
}
__kernel void CK_S_3(__global volatile char *restrict rt, const int num_ranks)
{
    char external_routing_table[MAX_RANKS];
    for (int i = 0; i < MAX_RANKS; i++)
    {
        if (i < num_ranks)
        {
            external_routing_table[i] = rt[i];
        }
    }

    // number of CK_S - 1 + CK_R + 0 CKS hardware ports
    const char num_sender = 4;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;

    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // receive from CK_S_0
                message = read_channel_nb_intel(channels_interconnect_ck_s[9], &valid);
                break;
            case 1:
                // receive from CK_S_1
                message = read_channel_nb_intel(channels_interconnect_ck_s[10], &valid);
                break;
            case 2:
                // receive from CK_S_2
                message = read_channel_nb_intel(channels_interconnect_ck_s[11], &valid);
                break;
            case 3:
                // receive from CK_R_3
                message = read_channel_nb_intel(channels_interconnect_ck_r_to_ck_s[3], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char idx = external_routing_table[GET_HEADER_DST(message.header)];
            switch (idx)
            {
                case 0:
                    // send to QSFP
                    write_channel_intel(io_out_3, message);
                    break;
                case 1:
                    // send to CK_R_3
                    write_channel_intel(channels_interconnect_ck_s_to_ck_r[3], message);
                    break;
                case 2:
                    // send to CK_S_0
                    write_channel_intel(channels_interconnect_ck_s[2], message);
                    break;
                case 3:
                    // send to CK_S_1
                    write_channel_intel(channels_interconnect_ck_s[5], message);
                    break;
                case 4:
                    // send to CK_S_2
                    write_channel_intel(channels_interconnect_ck_s[8], message);
                    break;
            }
        }
        if (!valid || contiguous_reads == READS_LIMIT)
        {
            contiguous_reads = 0;
            sender_id++;
            if (sender_id == num_sender)
            {
                sender_id = 0;
            }
        }
    }
}
__kernel void CK_R_3(__global volatile char *restrict rt, const char rank)
{
    // rt contains intertwined (dp0, cp0, dp1, cp1, ...)
    char external_routing_table[2 /* logical port count */][2];
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            external_routing_table[i][j] = rt[i * 2 + j];
        }
    }

    // QSFP + number of CK_Rs - 1 + CK_S
    const char num_sender = 5;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;
    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // QSFP
                message = read_channel_nb_intel(io_in_3, &valid);
                break;
            case 1:
                // receive from CK_R_0
                message = read_channel_nb_intel(channels_interconnect_ck_r[9], &valid);
                break;
            case 2:
                // receive from CK_R_1
                message = read_channel_nb_intel(channels_interconnect_ck_r[10], &valid);
                break;
            case 3:
                // receive from CK_R_2
                message = read_channel_nb_intel(channels_interconnect_ck_r[11], &valid);
                break;
            case 4:
                // receive from CK_S_3
                message = read_channel_nb_intel(channels_interconnect_ck_s_to_ck_r[3], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char dest;
            if (GET_HEADER_DST(message.header) != rank)
            {
                dest = 0;
            }
            else dest = external_routing_table[GET_HEADER_PORT(message.header)][GET_HEADER_OP(message.header) == SMI_REQUEST];

            switch (dest)
            {
                case 0:
                    // send to CK_S_3
                    write_channel_intel(channels_interconnect_ck_r_to_ck_s[3], message);
                    break;
                case 1:
                    // send to CK_R_0
                    write_channel_intel(channels_interconnect_ck_r[2], message);
                    break;
                case 2:
                    // send to CK_R_1
                    write_channel_intel(channels_interconnect_ck_r[5], message);
                    break;
                case 3:
                    // send to CK_R_2
                    write_channel_intel(channels_interconnect_ck_r[8], message);
                    break;
            }
        }

        if (!valid || contiguous_reads == READS_LIMIT)
        {
            contiguous_reads = 0;
            sender_id++;
            if (sender_id == num_sender)
            {
                sender_id = 0;
            }
        }
    }
}
