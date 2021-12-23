/*
* BSD 3-Clause License
* 
* Copyright (c) 2019, Alef Berg da Silva
* All rights reserved.
* 
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
* 
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
* 
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
* 
* 3. Neither the name of the copyright holder nor the names of its
*    contributors may be used to endorse or promote products derived from
*    this software without specific prior written permission.
* 
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
* 
* 
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/rpl/rpl.h"

#include "net/netstack.h"
#include "dev/slip.h"

#define DEBUG DEBUG_PRINT
#include "net/uip-debug.h"

#include "simple-udp.h"

uint16_t dag_id[] = {0x1111, 0x1100, 0, 0, 0, 0, 0, 0x0011};

static uip_ipaddr_t prefix;
static uint8_t prefix_set;
static struct uip_udp_conn *server_conn;
static struct simple_udp_connection broadcast_connection;
static uip_ipaddr_t rest_server_ipaddr;

#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	5678
#define REPAIR_TIMER 60
#define REMOTE_PORT     UIP_HTONS(COAP_DEFAULT_PORT)

#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

PROCESS(border_router_process, "Border router process");
PROCESS(broadcast_router_process, "Broadcast process");
AUTOSTART_PROCESSES(&broadcast_router_process);
/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  //PRINTA("Server IPv6 addresses:\n");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      PRINTA(" ");
      uip_debug_ipaddr_print(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTA("\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
void
set_prefix_64(uip_ipaddr_t *prefix_64)
{
  uip_ipaddr_t ipaddr;
  memcpy(&prefix, prefix_64, 16);
  memcpy(&ipaddr, prefix_64, 16);
  prefix_set = 1;
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);
}
/*---------------------------------------------------------------------------*/
void
request_prefix(void)
{
  /* mess up uip_buf with a dirty request... */
  uip_buf[0] = '?';
  uip_buf[1] = 'P';
  uip_len = 2;
  slip_send();
  uip_len = 0;
}
/*---------------------------------------------------------------------------*/
static void
send_wake()
{
  uip_ipaddr_t addr;
  PRINTF("Sending broadcast\n");
  uip_create_linklocal_allnodes_mcast(&addr);
  simple_udp_sendto(&broadcast_connection, "Wake!", 5, &addr);
}
/*---------------------------------------------------------------------------*/
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  PRINTF("Data received from ");
  uip_debug_ipaddr_print(sender_addr);
  PRINTF(" on port %d from port %d with length %d: '%s'\n",
         receiver_port, sender_port, datalen, data);
}
/* This function is will be passed to COAP_BLOCKING_REQUEST() to handle responses. */
void
client_chunk_handler(void *response)
{
  const uint8_t *chunk;

  int len = coap_get_payload(response, &chunk);
  PRINTF("|%.*s", len, (char *)chunk);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(border_router_process, ev, data)
{
  static struct etimer et;
  static uint8_t databuffer[UIP_BUFSIZE];

  PROCESS_BEGIN();

  PRINTF("Sink node process started\n");

  server_conn = udp_new(NULL, UIP_HTONS(UDP_CLIENT_PORT), NULL);
  if(server_conn == NULL) {
    PRINTF("No UDP connection available, exiting the process!\n");
    PROCESS_EXIT();
  }
  udp_bind(server_conn, UIP_HTONS(UDP_SERVER_PORT));

  PRINTF("Created a server connection with remote address ");
  uip_debug_ipaddr_print(&server_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n", UIP_HTONS(server_conn->lport),
         UIP_HTONS(server_conn->rport));
  
  etimer_set(&et, CLOCK_SECOND * REPAIR_TIMER);
  while(1) {
    PROCESS_WAIT_EVENT();
    if(ev == tcpip_event) {
      if(uip_newdata()) {
        uip_ipaddr_t addr = UIP_IP_BUF->srcipaddr;
        PROCESS_CONTEXT_BEGIN(&border_router_process);
        memcpy(databuffer, uip_appdata, uip_datalen());
/*
        float seq_id, temperature, humidity, luminosity, parent_id, rate;
        char t[3];
        strncpy(t, databuffer, 2);
        t[3] = '\0';
        seq_id = strtol(t, NULL, 16);
	strncpy(t, databuffer[1], 4);//temperature
        temperature = strtol(t, NULL, 16);
        strncpy(t, databuffer[5], 4);//humidity
        humidity = strtol(t, NULL, 16);
        strncpy(t, databuffer[9], 3);//luminosity
        luminosity = strtol(t, NULL, 16);
        strncpy(t, databuffer[12], 1);//parent_id
        parent_id = strtol(t, NULL, 16);
        strncpy(t, databuffer[13], 1);//rate
        rate = strtol(t, NULL, 16);
	//converting
        temperature = ((0.01*temperature) - 39.60);
        humidity = (((0.0405*humidity) - 4) + ((-2.8 * 0.000001)*(pow(humidity,2))));
        luminosity = (luminosity * 0.4071);
        float i = humidity/20 + (27 - temperature)/10;
        if(2.5 <= i || i <= 2) { uip_udp_packet_sendto(server_conn, "5", 1, &addr, UDP_CLIENT_PORT); }*/
       
	PRINTF("Forwarding to server %s\n", databuffer);
        uip_udp_packet_send(server_conn, databuffer, uip_datalen());

        PROCESS_CONTEXT_END();
      }
    }
    if(etimer_expired(&et)) {
      etimer_reset(&et);  
      PRINTF("Initiating global repair\n");
      rpl_repair_root(RPL_DEFAULT_INSTANCE);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(broadcast_router_process, ev, data)
{
  static struct etimer et;
  rpl_dag_t *dag;

  PROCESS_BEGIN();

/* While waiting for the prefix to be sent through the SLIP connection, the future
 * border router can join an existing DAG as a parent or child, or acquire a default 
 * router that will later take precedence over the SLIP fallback interface.
 * Prevent that by turning the radio off until we are initialized as a DAG root.
 */
  prefix_set = 0;
  NETSTACK_MAC.off(0);

  PROCESS_PAUSE();

  PRINTF("RPL-Border router started\n");

#if 0
   /* The border router runs with a 100% duty cycle in order to ensure high
     packet reception rates.
     Note if the MAC RDC is not turned off now, aggressive power management of the
     cpu will interfere with establishing the SLIP connection */
  NETSTACK_MAC.off(1);
#endif
 
  /* Request prefix until it has been received */
  while(!prefix_set) {
    etimer_set(&et, CLOCK_SECOND);
    request_prefix();
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
  }

  dag = rpl_set_root(RPL_DEFAULT_INSTANCE,(uip_ip6addr_t *)dag_id);
  if(dag != NULL) {
    rpl_set_prefix(dag, &prefix, 64);
    PRINTF("created a new RPL dag\n");
  }

  /* Now turn the radio on, but disable radio duty cycling.
   * Since we are the DAG root, reception delays would constrain mesh throughbut.
   */
  NETSTACK_MAC.off(1);
  
#if DEBUG || 1
  print_local_addresses();
#endif

  simple_udp_register(&broadcast_connection, 1234,
                      NULL, 1234,
                      receiver);

  send_wake();
  process_start(&border_router_process, NULL);
  etimer_set(&et, CLOCK_SECOND * 60);
  while(1) {
    PROCESS_YIELD();
    if(etimer_expired(&et)) {
      etimer_reset(&et);
      send_wake();
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
