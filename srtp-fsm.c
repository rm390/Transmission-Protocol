
/*
  CS3102 Coursework P2 : Simple, Reliable Transport Protocol (SRTP)

  saleem, Jan2024, Feb2023
  checked March 2025 (sjm55)
*/

#include <stdio.h>

#include "srtp-fsm.h"

const char *SRTP_fsm_strings_G[] = {
    "CLOSED",    "LISTENING",         "OPENING",
    "CONNECTED", "CLOSING_INITIATOR", "CLOSING_RESPONDER"};

void SRTP_fsm(SRTP_state_t *state, SRTP_event_t event) {
  if (*state < 0 || *state >= SRTP_state_closing_r) {
    printf("** Invalid state %d\n", *state);
    *state = SRTP_state_error;
    return;
  }

  if (event < 0 || event >= close_ack) {
    printf("** Invalid event %d\n", event);
    *state = SRTP_state_error;
    return;
  }

  printf("** Current state: %s\n", SRTP_fsm_strings_G[*state]);

  switch (*state) {
  // CLOSED
  case SRTP_state_closed:
    switch (event) {
    case event_start:
      *state = SRTP_state_listening;
      break;
    case event_open:
      *state = SRTP_state_opening;
      break;
    default:
      printf("** Invalid event in state %s\n", SRTP_fsm_strings_G[*state]);
      *state = SRTP_state_error;
    }
    break;

  // LISTENING
  case SRTP_state_listening:
    switch (event) {
    case open_req:
      *state = SRTP_state_connected;
      break;
    case close_req_r:
      *state = SRTP_state_closing_r;
      break;
    default:
      printf("** Invalid event in state %s\n", SRTP_fsm_strings_G[*state]);
      *state = SRTP_state_error;
    }
    break;

  // OPENING
  case SRTP_state_opening:
    switch (event) {
    case open_ack:
      *state = SRTP_state_connected;
      break;
    default:
      printf("** Invalid event in state %s\n", SRTP_fsm_strings_G[*state]);
      *state = SRTP_state_error;
    }
    break;

  // CONNECTED
  case SRTP_state_connected:
    switch (event) {
    case close_req_r:
      *state = SRTP_state_closing_r;
      break;
    case close_req_i:
      *state = SRTP_state_closing_i;
      break;
    case close_ack:
      *state = SRTP_state_closed;
      break;

    default:
      printf("** Invalid event in state %s\n", SRTP_fsm_strings_G[*state]);
      *state = SRTP_state_error;
    }
    break;

  // CLOSING_INITIATOR
  case SRTP_state_closing_i:
    switch (event) {
    case close_ack:
      *state = SRTP_state_closed;
      break;
    default:
      printf("** Invalid event in state %s\n", SRTP_fsm_strings_G[*state]);
      *state = SRTP_state_error;
    }
    break;

  // CLOSING_RESPONDER
  case SRTP_state_closing_r:
    switch (event) {
    case close_ack:
      *state = SRTP_state_closed;
      break;
    default:
      printf("** Invalid event in state %s\n", SRTP_fsm_strings_G[*state]);
      *state = SRTP_state_error;
    }
    break;

  default:
    // shouldn't happen!
    printf("** Invalid state %s\n", SRTP_fsm_strings_G[*state]);
    *state = SRTP_state_error;
  }
  printf("** New state: %s\n", SRTP_fsm_strings_G[*state]);
}
