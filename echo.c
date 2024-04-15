/*
 * Copyright (C) 2009 - 2019 Xilinx, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <string.h>

#include "lwip/err.h"
#include "lwip/tcp.h"
#if defined (__arm__) || defined (__aarch64__)
#include "xil_printf.h"
#endif

// global variables used to pass data between callbacks, tcp_arg
// could potentially be used instead

u16_t bytes_to_send;
u8_t test_buf[65536];
u8_t *write_head;



int transfer_data() {
	return 0;
}

void print_app_header()
{
#if (LWIP_IPV6==0)
	xil_printf("\n\r\n\r-----lwIP TCP echo server ------\n\r");
#else
	xil_printf("\n\r\n\r-----lwIPv6 TCP echo server ------\n\r");
#endif
	xil_printf("TCP packets sent to port 6001 will be echoed back\n\r");
}

err_t push_data(struct tcp_pcb *tpcb)
{
    // add up to remaining bytes_to_send to the transmit buffer, or
    // fill the remaining space in the transmit buffer
    u16_t packet_size = bytes_to_send;
    u16_t max_bytes = tcp_sndbuf(tpcb);
    err_t status;

    // if there's nothing left to send, exit early
    if (bytes_to_send == 0) {
        return ERR_OK;
    }

    // if adding all bytes to the buffer would make it overflow,
    // only fill the available space
    if (packet_size > max_bytes) {
        packet_size = max_bytes;
    }

    // write to the LWIP library's buffer
    status = tcp_write(tpcb, (void*)write_head, packet_size, 1);

    xil_printf("push_data: Asked to add %d bytes to the send buffer, adding %d bytes\r\n",
               bytes_to_send, packet_size);

    // keep track of how many bytes have been pushed to the buffer
    if (packet_size > bytes_to_send) {
        bytes_to_send = 0;
    } else {
        bytes_to_send -= packet_size;
    }

    // move our transmit buffer head forward
    write_head += packet_size;

    return status;
}


err_t recv_callback(void *arg, struct tcp_pcb *tpcb,
                               struct pbuf *p, err_t err)
{
	/* do not read the packet if we are not in ESTABLISHED state */
	if (!p) {
		tcp_close(tpcb);
		tcp_recv(tpcb, NULL);
		return ERR_OK;
	}

	xil_printf("entered recv_callback\r\n");

	/* indicate that the packet has been received */
	tcp_recved(tpcb, p->len);

	// convert the first two bytes of the received packet's payload
	// to a 16-bit unsigned integer

	bytes_to_send = *((u16_t*)(p->payload));

	xil_printf("Client asked for %d bytes\r\n", bytes_to_send);

	// load the buffer
	for (int i = 0; i < bytes_to_send; i++) {
		test_buf[i]= (u8_t)(i*3 & 0xFF);
	}

	write_head = test_buf;

	if (bytes_to_send > 0) {
	      err = push_data(tpcb);
	} // else, nothing to send


	/* echo back the payload */
	/* in this case, we assume that the payload is < TCP_SND_BUF */
	if (tcp_sndbuf(tpcb) > p->len) {
		err = tcp_write(tpcb, p->payload, p->len, 1);
	} else
		xil_printf("no space in tcp_sndbuf\n\r");

	/* free the received pbuf */
	pbuf_free(p);
	return err;
	//return ERR_OK;
}

// added

err_t sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len) {
	// log to USBUART for debugging purposes
    xil_printf("entered sent_callback\r\n");
    xil_printf("    bytes_to_send = %d\r\n", bytes_to_send);
    xil_printf("    len = %d\r\n", len);
    xil_printf("    free space = %d\r\n", tcp_sndbuf(tpcb));

    // if all bytes have been sent, we're done
    if (bytes_to_send <= 0)
    	return ERR_OK;

    return push_data(tpcb);
}



err_t accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	static int connection = 1;

	/* set the receive callback for this connection */
	tcp_recv(newpcb, recv_callback);

	/* set the sent callback for this connection */
	tcp_sent(newpcb, sent_callback); // <- add this

	/* just use an integer number indicating the connection id as the
	   callback argument */
	tcp_arg(newpcb, (void*)(UINTPTR)connection);

	/* increment for subsequent accepted connections */
	connection++;

	return ERR_OK;
}


int start_application()
{
	struct tcp_pcb *pcb;
	err_t err;
	unsigned port = 7;

	/* create new TCP PCB structure */
	pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
	if (!pcb) {
		xil_printf("Error creating PCB. Out of Memory\n\r");
		return -1;
	}

	/* bind to specified @port */
	err = tcp_bind(pcb, IP_ANY_TYPE, port);
	if (err != ERR_OK) {
		xil_printf("Unable to bind to port %d: err = %d\n\r", port, err);
		return -2;
	}

	/* we do not need any arguments to callback functions */
	tcp_arg(pcb, NULL);

	/* listen for connections */
	pcb = tcp_listen(pcb);
	if (!pcb) {
		xil_printf("Out of memory while tcp_listen\n\r");
		return -3;
	}

	/* specify callback to use for incoming connections */
	tcp_accept(pcb, accept_callback);

	xil_printf("TCP echo server started @ port %d\n\r", port);

	return 0;
}






