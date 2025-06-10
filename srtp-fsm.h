#ifndef __srtp_fsm_h__
#define __srtp_fsm_h__

/*
  CS3102 Coursework P2 : Simple, Reliable Transport Protocol (SRTP)

  saleem, Jan2024, Feb2023
  checked March 2025 (sjm55)
*/

/* CS3102: you can use the states defined below, or define your own */

typedef enum SRTP_state_e {
  SRTP_state_error = -1, // -1 problem with state
  SRTP_state_closed,     //  0 connection closed
  SRTP_state_listening,  //  1 @server : listening for incoming connections
  SRTP_state_opening,    //  2 @client : sent request to start connection
  SRTP_state_connected,  //  3 connected : connection established
  SRTP_state_closing_i,  //  4 closing initiated (@server or @client)
  SRTP_state_closing_r   //  5 closing responder (@server or @client)
} SRTP_state_t;

typedef enum SRTP_event_e {
  event_error = -1, // -1 problem with event
  event_start,      // 0 @server start event
  event_open,       // 1 @client open event
  open_req,         // 1 open request from client
  open_ack,         // 2 open ack from server
  data_req,         // 3 data request
  data_ack,         // 4 data ack
  close_req_i,      // 5 close request from initiator
  close_req_r,      // 6 close request from responder
  close_ack,        // 7 close ack
} SRTP_event_t;

void SRTP_fsm(SRTP_state_t *state, SRTP_event_t event);

#endif /* __srtp_fsm_h__ */
