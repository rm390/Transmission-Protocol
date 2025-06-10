/*
  CS3201 Coursework P2 : Simpler Reliable Transport Protocol (SRTP).
  saleem, Jan2024, Feb2023.
  checked March 2025 (sjm55)

  API for SRTP.

  Underneath, it MUST use UDP.
*/

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include "srtp-common.h"
#include "srtp-fsm.h"
#include "srtp-packet.h"
#include "srtp-pcb.h"
#include "srtp.h"

extern Srtp_Pcb_t G_pcb; /* in srtp-pcb.c */

#define MAX_RE_TX 10
#define SRTP_RTO_MIN ((uint32_t)10000)
#define SRTP_RTO_MAX ((uint32_t)500000)
uint32_t t_n, s_n, v_n;

void *srtp_create_packet(uint8_t type, void *data, uint16_t data_size,
                         uint32_t seq_num);

void srtp_rto_adjust(uint32_t r_n);

/*
  Must be called before any other srtp_zzz() API calls.

  For use by client and server process.
*/
void srtp_initialise() {
  reset_SrtpPcb();
  G_pcb.rto = G_pcb.rtt = SRTP_RTO_MIN;
  t_n = s_n = v_n = 0;
}

/*
  port : local port number to be used for socket
  return : error - srtp-common.h
           success - valid socket descriptor

  For use by server process.
*/
int srtp_start(uint16_t port) {
  // Check if SRTP is already started
  if (G_pcb.state != SRTP_state_closed) {
    fprintf(stderr, "[SRTP] Error: SRTP already started\n");
    return SRTP_ERROR;
  }

  // Local port
  G_pcb.port = port;
  G_pcb.local.sin_addr.s_addr = htonl(INADDR_ANY);
  G_pcb.local.sin_port = htons(port);

  // Create socket
  if ((G_pcb.sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    fprintf(stderr, "srtp_start(): socket()");
    return SRTP_ERROR;
  }

  // Bind the socket
  if (bind(G_pcb.sd, (struct sockaddr *)&G_pcb.local, sizeof(G_pcb.local)) <
      0) {
    fprintf(stderr, "srtp_start(): bind()");
    close(G_pcb.sd);
    return SRTP_ERROR;
  }

  // Set up listening state
  SRTP_fsm(&G_pcb.state, event_start);
  if (G_pcb.state != SRTP_state_listening) {
    fprintf(stderr, "[SRTP] Error: SRTP not listening\n");
    close(G_pcb.sd);
    return SRTP_ERROR;
  }

  G_pcb.start_time = srtp_timestamp();

  return G_pcb.sd;
}

/*
  sd : socket descriptor as previously provided by srtp_start()
  return : error - srtp-common.h
           success - sd, to indicate sd now also is "connected"

  For use by server process.
*/
int srtp_accept(int sd) {
  // Check if SRTP is already started
  if (G_pcb.state != SRTP_state_listening) {
    fprintf(stderr, "[SRTP] Error: SRTP not listening\n");
    close(sd);
    return SRTP_ERROR;
  }

  // Check if SRTP is already connected
  if (G_pcb.state == SRTP_state_connected) {
    fprintf(stderr, "[SRTP] Error: SRTP already connected\n");
    close(sd);
    return SRTP_ERROR;
  }

  // Create buffer for open_req packet
  void *open_req_pkt = malloc(SRTP_HDR_SIZE);
  if (!open_req_pkt) {
    fprintf(stderr, "[SRTP] Error: malloc() problem");
    close(sd);
    return SRTP_ERROR;
  }

  printf("[SRTP] Waiting for open_req...\n");

  // Wait for open_req
  socklen_t l = sizeof(G_pcb.remote);
  if (recvfrom(sd, open_req_pkt, SRTP_HDR_SIZE, 0,
               (struct sockaddr *)&G_pcb.remote, &l) < 0) {
    fprintf(stderr, "srtp_accept(): recvfrom()");
    free(open_req_pkt);
    close(sd);
    return SRTP_ERROR;
  }

  // Check packet for open_req
  Srtp_Header_t *hdr = (Srtp_Header_t *)open_req_pkt;
  if (hdr->type != SRTP_TYPE_open_req) {
    fprintf(stderr, "[SRTP] Error: packet type mismatch\n");
    free(open_req_pkt);
    close(sd);
    return SRTP_ERROR;
  }

  // Free the open_req packet as we don't need it anymore
  free(open_req_pkt);
  G_pcb.open_req_rx++;
  printf("[SRTP] Sending open_ack...\n");

  // Send open_ack
  void *open_ack_pkt = srtp_create_packet(SRTP_TYPE_open_ack, NULL, 0, 0);
  if (!open_ack_pkt) {
    fprintf(stderr, "[SRTP] Error: malloc() problem");
    close(sd);
    return SRTP_ERROR;
  }

  if (sendto(sd, open_ack_pkt, SRTP_HDR_SIZE, 0,
             (struct sockaddr *)&G_pcb.remote, sizeof(G_pcb.remote)) < 0) {
    fprintf(stderr, "[SRTP] Error: sendto()");
    free(open_ack_pkt);
    close(sd);
    G_pcb.sd = -1;
    return SRTP_ERROR;
  }
  G_pcb.open_ack_tx++;

  // Free the ack packet as we don't need it anymore
  free(open_ack_pkt);

  SRTP_fsm(&G_pcb.state, open_req);
  if (G_pcb.state != SRTP_state_connected) {
    fprintf(stderr, "[SRTP] Error: SRTP not connected\n");
    close(sd);
    return SRTP_ERROR;
  }
  return sd;
}

/*
  port : local and remote port number to be used for socket
  return : error - SRTP_ERROR
           success - valid socket descriptor

  For use by client process.
*/
int srtp_open(const char *fqdn, uint16_t port) {
  // Check if SRTP is already started
  if (G_pcb.state != SRTP_state_closed) {
    fprintf(stderr, "[SRTP] Error: SRTP already started\n");
    return SRTP_ERROR;
  }

  // Set pcb variables
  G_pcb.port = port;
  G_pcb.local.sin_family = AF_INET;
  G_pcb.local.sin_addr.s_addr = htonl(INADDR_ANY);
  G_pcb.local.sin_port = htons(port);

  // Resolve fully qualified domain name
  struct in_addr addr;
  addr.s_addr = (in_addr_t)0;

  if (inet_aton(fqdn, &addr) == 0) {
    struct hostent *hp = gethostbyname(fqdn);
    if (hp == (struct hostent *)0) {
      fprintf(stderr, "[SRTP] Error: failed to resolve hostname\n");
      return SRTP_ERROR;
    } else {
      memcpy((void *)&addr.s_addr, (void *)*hp->h_addr_list,
             sizeof(addr.s_addr));
    }
  }

  if (addr.s_addr != (in_addr_t)0) {
    G_pcb.remote.sin_family = AF_INET;
    G_pcb.remote.sin_addr.s_addr = addr.s_addr;
    G_pcb.remote.sin_port = htons(port);
  } else {
    return SRTP_ERROR;
  }

  printf("[SRTP] Resolved %s to %s\n", fqdn, inet_ntoa(addr));

  SRTP_fsm(&G_pcb.state, event_open);
  if (G_pcb.state != SRTP_state_opening) {
    fprintf(stderr, "[SRTP] Error: SRTP failed to open\n");
    return SRTP_ERROR;
  }

  // Create socket
  if ((G_pcb.sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    fprintf(stderr, "srtp_start(): socket()");
    return SRTP_ERROR;
  }

  if (bind(G_pcb.sd, (struct sockaddr *)&G_pcb.local, sizeof(G_pcb.local)) <
      0) {
    fprintf(stderr, "srtp_start(): bind()");
    close(G_pcb.sd);
    return SRTP_ERROR;
  }

  // Create open_req packet
  void *open_req_pkt = srtp_create_packet(SRTP_TYPE_open_req, NULL, 0, 0);
  if (!open_req_pkt) {
    fprintf(stderr, "[SRTP] Error: malloc() problem");
    close(G_pcb.sd);
    return SRTP_ERROR;
  }

  // Create buffer for open_ack packet
  uint8_t *ack_pkt = malloc(SRTP_HDR_SIZE);
  if (!ack_pkt) {
    fprintf(stderr, "[SRTP] Error: malloc() problem");
    free(open_req_pkt);
    close(G_pcb.sd);
    return SRTP_ERROR;
  }

  // Set timeout for receiving open_ack
  struct timeval tv;
  tv.tv_sec = 3;
  tv.tv_usec = 0;
  setsockopt(G_pcb.sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  socklen_t l = sizeof(G_pcb.remote);

  int retries;
  for (retries = 0; retries < SRTP_MAX_RE_TX; retries++) {
    // Send open_req
    if (sendto(G_pcb.sd, open_req_pkt, SRTP_HDR_SIZE, 0,
               (struct sockaddr *)&G_pcb.remote, sizeof(G_pcb.remote)) < 0) {
      fprintf(stderr, "[SRTP] Error: sendto()\n");
      close(G_pcb.sd);
      free(open_req_pkt);
      free(ack_pkt);
      return SRTP_ERROR;
    }
    G_pcb.open_req_tx++;

    printf("[SRTP] Sent open_req...\n");
    printf("[SRTP] Waiting for open_ack...\n");

    // Wait for open_ack
    ssize_t n = recvfrom(G_pcb.sd, ack_pkt, SRTP_HDR_SIZE, 0,
                         (struct sockaddr *)&G_pcb.remote, &l);

    // If we receive a packet check if it is an open_ack
    if (n > 0) {
      Srtp_Header_t *hdr = (Srtp_Header_t *)ack_pkt;
      if (hdr->type == SRTP_TYPE_open_ack) {
        printf("[SRTP] Received open_ack...\n");
        G_pcb.open_ack_rx++;
        break;
      } else {
        fprintf(stderr, "[SRTP] Error: packet type mismatch\n");
        close(G_pcb.sd);
        free(open_req_pkt);
        free(ack_pkt);
        return SRTP_ERROR;
      }
    }

    // If we don't receive a packet, check if it is a timeout or an error
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (retries != SRTP_MAX_RE_TX - 1) {
        printf("[SRTP] open_ack timeout. Retrying...\n");
        G_pcb.open_req_re_tx++;
      }
    } else {
      fprintf(stderr, "[SRTP] Error: recvfrom()\n");
      close(G_pcb.sd);
      free(open_req_pkt);
      free(ack_pkt);
      return SRTP_ERROR;
    }
    G_pcb.open_ack_re_tx++;
  }

  // Free both packets as we don't need them anymore
  free(open_req_pkt);
  free(ack_pkt);

  if (retries == SRTP_MAX_RE_TX) {
    fprintf(stderr, "[SRTP] Error: open_ack not received\n");
    close(G_pcb.sd);
    return SRTP_ERROR;
  }

  // Change state to connected
  SRTP_fsm(&G_pcb.state, open_ack);
  if (G_pcb.state != SRTP_state_connected) {
    fprintf(stderr, "[SRTP] Error: SRTP not connected\n");
    close(G_pcb.sd);
    return SRTP_ERROR;
  }

  printf("[SRTP] Connected to %s:%d\n", fqdn, port);

  G_pcb.start_time = srtp_timestamp();

  return G_pcb.sd;
}

/*
  port : local and remote port number to be used for socket
  return : error - SRTP_ERROR
           success - SRTP_SUCCESS

  For use by client process.
*/
int srtp_close(int sd) {
  // Check if SRTP is already closed
  if (G_pcb.state == SRTP_state_closed) {
    fprintf(stderr, "[SRTP] SRTP already closed...\n");
    return SRTP_ERROR;
  }

  // Check if SRTP is already Connected
  if (G_pcb.state != SRTP_state_connected) {
    fprintf(stderr, "[SRTP] Error: SRTP not connected\n");
    return SRTP_ERROR;
  }

  // Check socket descriptor is valid
  if (sd < 0) {
    fprintf(stderr, "[SRTP] Error: socket descriptor invalid\n");
    return SRTP_ERROR;
  }

  // Create close_req packet
  void *close_req_pkt = srtp_create_packet(SRTP_TYPE_close_req, NULL, 0, 0);
  if (!close_req_pkt) {
    fprintf(stderr, "[SRTP] Error: malloc() problem");
    return SRTP_ERROR;
  }

  // Create buffer for packet
  void *close_ack_pkt = malloc(SRTP_HDR_SIZE);
  if (!close_ack_pkt) {
    fprintf(stderr, "[SRTP] Error: malloc() problem");
    free(close_req_pkt);
    return SRTP_ERROR;
  }

  // Set timeout for receiving close_ack
  struct timeval tv = {.tv_sec = 3, .tv_usec = 0};
  setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // Send close_req, wait for close_ack, and retransmit if not received.
  if (sendto(sd, close_req_pkt, SRTP_HDR_SIZE, 0,
             (struct sockaddr *)&G_pcb.remote, sizeof(G_pcb.remote)) < 0) {
    fprintf(stderr, "[SRTP] Error: sendto()\n");
    free(close_req_pkt);
    free(close_ack_pkt);
    return SRTP_ERROR;
  }
  G_pcb.close_req_tx++;
  free(close_req_pkt);

  printf("[SRTP] Sent close_req...\n");
  printf("[SRTP] Waiting for close_ack...\n");

  // Wait for close_ack
  socklen_t l = sizeof(G_pcb.remote);
  ssize_t n = recvfrom(sd, close_ack_pkt, SRTP_HDR_SIZE, 0,
                       (struct sockaddr *)&G_pcb.remote, &l);

  if (n > 0) {
    Srtp_Header_t *hdr = (Srtp_Header_t *)close_ack_pkt;
    switch (hdr->type) {
    case SRTP_TYPE_close_ack:
      G_pcb.close_ack_rx++;
      printf("[SRTP] Received close_ack...\n");
      close(sd);
      return SRTP_SUCCESS;
    case SRTP_TYPE_close_req:
      printf("[SRTP] Received close_req...\n");
    }
  } else if (n < 0) {
    // If we don't receive a packet, check if it is a timeout or an error
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      fprintf(stderr, "[SRTP] Error: close_ack timeout\n");
    }
  } else if (n == 0) {
    fprintf(stderr, "[SRTP] Error: recvfrom() returned 0 bytes\n");
  }

  free(close_ack_pkt);
  fprintf(stderr, "[SRTP] Force closing connection...\n");
  close(sd);

  return SRTP_SUCCESS;
}
/*
  sd : socket descriptor
  data : buffer with bytestream to transmit
  data_size : number of bytes to transmit
  return : error - SRTP_ERROR
           success - number of bytes transmitted
*/
int srtp_tx(int sd, void *data, uint16_t data_size) {
  if (G_pcb.state != SRTP_state_connected) {
    fprintf(stderr, "[SRTP] Error: SRTP not connected\n");
    return SRTP_ERROR;
  }

  // Check if data_size is valid
  if (data_size > SRTP_MAX_DATA_SIZE || data_size <= 0) {
    fprintf(stderr, "[SRTP] Error: data_size invalid\n");
    return SRTP_ERROR;
  }

  // Check socket descriptor is valid
  if (sd < 0) {
    fprintf(stderr, "[SRTP] Error: socket descriptor invalid\n");
    return SRTP_ERROR;
  }

  // Create packet for data_req
  void *data_req_pkt =
      srtp_create_packet(SRTP_TYPE_data_req, data, data_size, G_pcb.seq_tx);
  if (!data_req_pkt) {
    fprintf(stderr, "[SRTP] Error: malloc() problem");
    return SRTP_ERROR;
  }

  // Create buffer for data_ack packet
  void *data_ack_pkt = malloc(SRTP_HDR_SIZE);
  if (!data_ack_pkt) {
    fprintf(stderr, "[SRTP] Error: malloc() problem");
    free(data_req_pkt);
    return SRTP_ERROR;
  }

  // Set timeout for receiving data_ack
  struct timeval tv = {.tv_sec = 1, .tv_usec = G_pcb.rto};
  setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // Send data req, wait for data_ack, and retransmit if not received.
  int retries = 0;
  while (retries < MAX_RE_TX) {

    if (sendto(sd, data_req_pkt, SRTP_HDR_SIZE + data_size, 0,
               (struct sockaddr *)&G_pcb.remote, sizeof(G_pcb.remote)) < 0) {
      fprintf(stderr, "[SRTP] Error: sendto()\n");
      free(data_req_pkt);
      return SRTP_ERROR;
    }
    if (retries > 0) {
      G_pcb.data_req_re_tx++;
    } else {
      G_pcb.data_req_tx++;
    }

    printf("[SRTP] Sent data_req...\n");
    printf("[SRTP] Waiting for data_ack...\n");

    // Wait for data_ack
    socklen_t l = sizeof(G_pcb.remote);
    ssize_t n = recvfrom(sd, data_ack_pkt, SRTP_HDR_SIZE, 0,
                         (struct sockaddr *)&G_pcb.remote, &l);

    // If we receive a packet check if it is a data_ack and matches the
    // sequence_number
    if (n > 0) {
      Srtp_Header_t *hdr = (Srtp_Header_t *)data_ack_pkt;

      if (hdr == NULL) {
        fprintf(stderr, "[SRTP] Error: packet type mismatch\n");
        free(data_req_pkt);
        free(data_ack_pkt);
        return SRTP_ERROR;
      }

      // Check packet for data_ack
      if (hdr->type == SRTP_TYPE_data_ack &&
          hdr->sequence_number == G_pcb.seq_tx) {
        printf("[SRTP] Received data_ack...\n");

        // Get the rtt
        struct timeval recv_time;
        gettimeofday(&recv_time, (struct timezone *)0);

        G_pcb.rtt = (recv_time.tv_sec - hdr->timestamp_sec) * 1000000 +
                    (recv_time.tv_usec - hdr->timestamp_usec);

        G_pcb.seq_tx++;
        G_pcb.data_ack_rx++;
        G_pcb.data_req_bytes_tx += data_size;
        srtp_rto_adjust(G_pcb.rtt);
        return data_size;

      } else if (hdr->type == SRTP_TYPE_data_req &&
                 hdr->sequence_number < G_pcb.seq_rx) {
        void *data_ack_pkt = srtp_create_packet(SRTP_TYPE_data_ack, NULL, 0,
                                                hdr->sequence_number);
        if (!data_ack_pkt) {
          fprintf(stderr, "[SRTP] Error: malloc() problem");
          free(data_req_pkt);
          return SRTP_ERROR;
        }
        if (sendto(sd, data_ack_pkt, SRTP_HDR_SIZE, 0,
                   (struct sockaddr *)&G_pcb.remote,
                   sizeof(G_pcb.remote)) < 0) {
          fprintf(stderr, "[SRTP] Error: sendto()\n");
        }
        continue;
      } else if (hdr->type == SRTP_TYPE_close_req) {
        SRTP_fsm(&G_pcb.state, close_req_r);
        printf("[SRTP] Received close_req...\n");
        G_pcb.close_req_rx++;
        return data_size;
      }
      continue;
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        printf("[SRTP] data_ack timeout. Retrying...\n");
        G_pcb.data_req_re_tx++;
        retries++;
        srtp_rto_adjust(G_pcb.rtt);
      } else {
        fprintf(stderr, "[SRTP] Error: recvfrom()\n");
        break;
      }
    }

    srtp_rto_adjust(G_pcb.rtt);
    printf("[SRTP] RTO: %d\n", G_pcb.rto);

    if (retries == MAX_RE_TX) {
      fprintf(stderr, "[SRTP] Error: data_ack not received\n");
      break;
    }
  }

  free(data_req_pkt);
  free(data_ack_pkt);
  return SRTP_ERROR;
}

/*
  sd : socket descriptor
  data : buffer to store bytestream received
  data_size : size of buffer
  return : error - SRTP_ERROR
           success - number of bytes received
*/
int srtp_rx(int sd, void *data, uint16_t data_size) {
  if (G_pcb.state != SRTP_state_connected) {
    fprintf(stderr, "[SRTP] Error: SRTP not connected\n");
    return SRTP_ERROR;
  }

  // Check if data_size is valid
  if (data_size > SRTP_MAX_DATA_SIZE || data_size <= 0) {
    fprintf(stderr, "[SRTP] Error: data_size invalid\n");
    return SRTP_ERROR;
  }

  // Check socket descriptor is valid
  if (sd < 0) {
    fprintf(stderr, "[SRTP] Error: socket descriptor invalid\n");
    return SRTP_ERROR;
  }

  // Set timeout for receiving data_req
  struct timeval tv = {.tv_sec = 0, .tv_usec = 0};
  setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // Create buffer for data_req packet
  uint8_t *data_req_pkt = malloc(SRTP_HDR_SIZE + data_size);
  if (!data_req_pkt) {
    fprintf(stderr, "[SRTP] Error: malloc() problem");
    return SRTP_ERROR;
  }

  socklen_t l = sizeof(sd);

  while (1) {
    printf("[SRTP] Waiting for data_req...\n");
    ssize_t bytes_recv = recvfrom(sd, data_req_pkt, SRTP_HDR_SIZE + data_size,
                                  0, (struct sockaddr *)&G_pcb.remote, &l);

    // Check number of bytes received is not negative or zero
    if (bytes_recv < 0) {
      fprintf(stderr, "[SRTP] Error: recvfrom()\n");
      break;
    }

    // Check packet for data_req
    Srtp_Header_t *hdr = (Srtp_Header_t *)data_req_pkt;
    if (hdr->type != SRTP_TYPE_data_req) {
      // Print packet type mismatch error
      fprintf(stderr, "[SRTP] Error: packet type mismatch\n");
      break;
    }
    G_pcb.data_req_rx++;

    printf("[SRTP] Received data_req...\n");
    uint32_t seq_num = hdr->sequence_number;

    // Create data_ack packet
    void *data_ack_pkt =
        srtp_create_packet(SRTP_TYPE_data_ack, NULL, 0, seq_num);
    if (!data_ack_pkt) {
      fprintf(stderr, "[SRTP] Error: malloc() problem");
      break;
    }

    // Send data_ack packet
    if (sendto(sd, data_ack_pkt, SRTP_HDR_SIZE, 0,
               (struct sockaddr *)&G_pcb.remote, sizeof(G_pcb.remote)) < 0) {
      fprintf(stderr, "[SRTP] Error: sendto()\n");
      free(data_ack_pkt);
      break;
    }
    G_pcb.data_ack_tx++;

    // If the sequence number is the same as the one we are expecting,
    // continue the loop
    if (seq_num == G_pcb.seq_rx) {
      // Copy data from data_req packet to data buffer and free the packet
      memcpy(data, data_req_pkt + SRTP_HDR_SIZE, data_size);
      free(data_req_pkt);
      free(data_ack_pkt);
      G_pcb.seq_rx++;
      G_pcb.data_req_bytes_rx += bytes_recv - SRTP_HDR_SIZE;
      return bytes_recv - SRTP_HDR_SIZE;
    }
  }

  free(data_req_pkt);
  return SRTP_ERROR;
}

// Adjust the RTO based on the RTT. Algorithm taken from example code provided
// in week 5
void srtp_rto_adjust(uint32_t r_n) {
  if (G_pcb.rto == SRTP_RTO_MIN) {
    s_n = r_n;
    v_n = r_n >> 1;
  } else {
    v_n = (v_n >> 1) + (v_n >> 2) + ((s_n > r_n ? s_n - r_n : r_n - s_n) >> 2);
    s_n = (s_n >> 1) + (s_n >> 2) + (s_n >> 3) + (r_n >> 3);
  }
  t_n = s_n + (v_n << 2);
  G_pcb.rto = t_n < SRTP_RTO_MIN ? SRTP_RTO_MIN : t_n;
  G_pcb.rto = G_pcb.rto > SRTP_RTO_MAX ? SRTP_RTO_MAX : G_pcb.rto;
}

// Create a packet for SRTP
void *srtp_create_packet(uint8_t type, void *data, uint16_t data_size,
                         uint32_t seq_num) {
  // Create packet for data_req
  void *pkt = malloc(SRTP_HDR_SIZE + data_size);
  if (!pkt) {
    fprintf(stderr, "[SRTP] Error: malloc() problem");
    return NULL;
  }

  Srtp_Packet_t p;
  p.hdr = (Srtp_Header_t *)pkt;
  p.payload = (void *)(uintptr_t *)pkt + SRTP_HDR_SIZE;
  p.payload_size = data_size;
  memset(p.payload, 0, data_size);

  // Get timestamp for probe packet
  struct timeval send_time;
  gettimeofday(&send_time, (struct timezone *)0);

  // Fill probe packet header
  p.hdr->type = type;
  p.hdr->timestamp_sec = send_time.tv_sec;
  p.hdr->timestamp_usec = send_time.tv_usec;
  p.hdr->sequence_number = seq_num;
  p.hdr->padding = 0;

  // Fill data into packet
  if (data != NULL || data_size == 0)
    memcpy(p.payload, data, data_size);

  return pkt;
}
