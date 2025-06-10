#ifndef __srtp_packet_h__
#define __srtp_packet_h__

/*
  CS3102 Coursework P2 : Simple, Reliable Transport Protocol (SRTP)
  saleem, Jan2024, Feb2023
  checked March 2025 (sjm55)

  These are suggested definitions only.
  Please modify as required.
*/

#include "srtp-common.h"
#include <stdint.h>

/* packet type values : bit field, but can be used as required */

#define SRTP_TYPE_req ((uint8_t)0x01)
#define SRTP_TYPE_ack ((uint8_t)0x02)

#define SRTP_TYPE_open ((uint8_t)0x10)
#define SRTP_TYPE_open_req (SRTP_TYPE_open | SRTP_TYPE_req)
#define SRTP_TYPE_open_ack (SRTP_TYPE_open | SRTP_TYPE_ack)

#define SRTP_TYPE_close ((uint8_t)0x20)
#define SRTP_TYPE_close_req (SRTP_TYPE_close | SRTP_TYPE_req)
#define SRTP_TYPE_close_ack (SRTP_TYPE_close | SRTP_TYPE_ack)

#define SRTP_TYPE_data ((uint8_t)0x40)
#define SRTP_TYPE_data_req (SRTP_TYPE_data | SRTP_TYPE_req)
#define SRTP_TYPE_data_ack (SRTP_TYPE_data | SRTP_TYPE_ack)

/* This header should not have a memory image greater than 100 bytes */
typedef struct Srtp_Header_s {
  uint32_t timestamp_sec;
  uint32_t timestamp_usec;
  uint32_t sequence_number;
  uint16_t type;
  uint16_t padding;
  /* CS3102 : define your header here */
} Srtp_Header_t;

#define SRTP_HDR_SIZE sizeof(Srtp_Header_t)
#define SRTP_MAX_PAYLOAD_SIZE SRTP_MAX_DATA_SIZE

typedef struct Srtp_Packet_s {
  Srtp_Header_t *hdr;    /* pointer to header */
  void *payload;         /* pointer to payload */
  uint16_t payload_size; /* size of payload */
} Srtp_Packet_t;

/* CS3012 : put in here whatever else you need */

#endif /* __srtp_packet_h__ */
