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

#include "contiki.h"
#include "lib/random.h"
#include "sys/ctimer.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/uip-udp-packet.h"
#include "sys/ctimer.h"
#include "dev/sht11-sensor.h"
#include "dev/light-sensor.h"
#include "simple-udp.h"

#include <stdio.h>
#include <string.h>

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define DEBUG DEBUG_PRINT
#include "net/uip-debug.h"

#define START_INTERVAL		(15 * CLOCK_SECOND)
#define SEND_INTERVAL		(PERIOD * CLOCK_SECOND)
#define SEND_TIME		(random_rand() % (SEND_INTERVAL))
#define MAX_PAYLOAD_LEN		30

static struct uip_udp_conn *client_conn;
static struct simple_udp_connection broadcast_connection;
static uip_ipaddr_t server_ipaddr;
static int temperature, humidity, light;
static int rate = 10;
static int auth = 0;
/*---------------------------------------------------------------------------*/
PROCESS(client_node_process, "Client node process");
PROCESS(client_broadcast_process, "Client node broadcast process");
AUTOSTART_PROCESSES(&client_broadcast_process);
/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void)
{
  char *str;

  if(uip_newdata()) {
    str = uip_appdata;
    str[uip_datalen()] = '\0';
    printf("DATA recv '%s'\n", str);
  }
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
  printf("Data received from ");
  uip_debug_ipaddr_print(sender_addr);
  printf(" on port %d from port %d with length %d: '%s'\n",
         receiver_port, sender_port, datalen, data);
  
  uip_ipaddr_t addr;
  printf("Sending broadcast\n");
  uip_create_linklocal_allnodes_mcast(&addr);
  simple_udp_sendto(&broadcast_connection, data, datalen, &addr);
  if(0 ==  strcmp(data, "Wake!"))
  { 
    auth = 1;
    process_start(&client_node_process, NULL);
  } else {  auth = 0;  }
}
/*---------------------------------------------------------------------------*/
static void
send_packet(void)
{
  static int seq_id;
  static int parent_id = 0;
  char buf[MAX_PAYLOAD_LEN];
//seq_id | Temperature | Humidity | Luminosity | Parent_ID | Rate
//2 bytes|   4 bytes   |  4 bytes |   3 bytes  |   1 byte  | 1 byte
  seq_id++;
  PRINTF("%02X%04X%04X%03X%01X%01X send to ", seq_id, temperature, humidity, light, parent_id, rate);
  uip_debug_ipaddr_print(&server_ipaddr);
  PRINTF("\n");
  sprintf(buf, "%02X%04X%04X%03X%01X%01X", seq_id, temperature, humidity, light, parent_id, rate);
  uip_udp_packet_send(client_conn, buf, strlen(buf));
}
/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTF("Client IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTF("\n");
      /* hack to make address "final" */
      if (state == ADDR_TENTATIVE) {
	uip_ds6_if.addr_list[i].state = ADDR_PREFERRED;
      }
    }
  }
}
/*---------------------------------------------------------------------------*/
static void
set_global_address(void)
{
  uip_ipaddr_t ipaddr;

  uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

/* The choice of server address determines its 6LoPAN header compression.
 * (Our address will be compressed Mode 3 since it is derived from our link-local address)
 * Obviously the choice made here must also be selected in udp-server.c.
 *
 * For correct Wireshark decoding using a sniffer, add the /64 prefix to the 6LowPAN protocol preferences,
 * e.g. set Context 0 to aaaa::.  At present Wireshark copies Context/128 and then overwrites it.
 * (Setting Context 0 to aaaa::1111:2222:3333:4444 will report a 16 bit compressed address of aaaa::1111:22ff:fe33:xxxx)
 *
 * Note the IPCMV6 checksum verification depends on the correct uncompressed addresses.
 */
 
#if 0
/* Mode 1 - 64 bits inline */
   uip_ip6addr(&server_ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 1);
#elif 1
/* Mode 2 - 16 bits inline */
  uip_ip6addr(&server_ipaddr, 0xaaaa, 0, 0, 0, 0x0212, 0x7401, 0x1, 0x101);
#else
/* Mode 3 - derived from server link-local (MAC) address */
  uip_ip6addr(&server_ipaddr, 0xaaaa, 0, 0, 0, 0x0250, 0xc2ff, 0xfea8, 0xcd1a); //redbee-econotag
#endif
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(client_node_process, ev, data)
{
  static struct etimer periodic;

  PROCESS_BEGIN();

  PRINTF("Client node process started\n");

  /* new connection with remote host */
  client_conn = udp_new(&server_ipaddr, UIP_HTONS(UDP_SERVER_PORT), NULL); 
  if(client_conn == NULL) {
    PRINTF("No UDP connection available, exiting the process!\n");
    PROCESS_EXIT();
  }
  udp_bind(client_conn, UIP_HTONS(UDP_CLIENT_PORT)); 

  PRINTF("Created a connection with the server ");
  PRINT6ADDR(&client_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n",
	UIP_HTONS(client_conn->lport), UIP_HTONS(client_conn->rport));

  /*simple_udp_register(&client_conn, UDP_CLIENT_PORT,
                      &server_ipaddr, UDP_SERVER_PORT, receiver);*/

  etimer_set(&periodic, CLOCK_SECOND * rate);
  while(1) {

    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    }

    if(!auth) {
      PRINTF("Not auth\n");
      continue;
    }
    
    SENSORS_ACTIVATE(light_sensor);
    SENSORS_ACTIVATE(sht11_sensor);

    if(etimer_expired(&periodic)) {
      etimer_reset(&periodic);
      temperature = sht11_sensor.value(SHT11_SENSOR_TEMP);
      humidity = sht11_sensor.value(SHT11_SENSOR_HUMIDITY);
      light = light_sensor.value(LIGHT_SENSOR_TOTAL_SOLAR);
      send_packet();
    }
   
    SENSORS_DEACTIVATE(light_sensor);
    SENSORS_DEACTIVATE(sht11_sensor);

  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(client_broadcast_process, ev, data)
{
  PROCESS_BEGIN();
  
  PROCESS_PAUSE();

  set_global_address();
  
  PRINTF("Client node broadcast process started\n");

  print_local_addresses();

  simple_udp_register(&broadcast_connection, 1234,
                      NULL, 1234, receiver);

  PROCESS_END();
}
