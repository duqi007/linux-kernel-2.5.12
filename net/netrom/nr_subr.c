/*
 *	NET/ROM release 007
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	NET/ROM 001	Jonathan(G4KLX)	Cloned from ax25_subr.c
 *	NET/ROM	003	Jonathan(G4KLX)	Added G8BPQ NET/ROM extensions.
 *	NET/ROM 007	Jonathan(G4KLX)	New timer architecture.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/netrom.h>

/*
 *	This routine purges all of the queues of frames.
 */
void nr_clear_queues(struct sock *sk)
{
	nr_cb *nr = nr_sk(sk);

	skb_queue_purge(&sk->write_queue);
	skb_queue_purge(&nr->ack_queue);
	skb_queue_purge(&nr->reseq_queue);
	skb_queue_purge(&nr->frag_queue);
}

/*
 * This routine purges the input queue of those frames that have been
 * acknowledged. This replaces the boxes labelled "V(a) <- N(r)" on the
 * SDL diagram.
 */
void nr_frames_acked(struct sock *sk, unsigned short nr)
{
	nr_cb *nrom = nr_sk(sk);
	struct sk_buff *skb;

	/*
	 * Remove all the ack-ed frames from the ack queue.
	 */
	if (nrom->va != nr) {
		while (skb_peek(&nrom->ack_queue) != NULL && nrom->va != nr) {
		        skb = skb_dequeue(&nrom->ack_queue);
			kfree_skb(skb);
			nrom->va = (nrom->va + 1) % NR_MODULUS;
		}
	}
}

/*
 * Requeue all the un-ack-ed frames on the output queue to be picked
 * up by nr_kick called from the timer. This arrangement handles the
 * possibility of an empty output queue.
 */
void nr_requeue_frames(struct sock *sk)
{
	struct sk_buff *skb, *skb_prev = NULL;

	while ((skb = skb_dequeue(&nr_sk(sk)->ack_queue)) != NULL) {
		if (skb_prev == NULL)
			skb_queue_head(&sk->write_queue, skb);
		else
			skb_append(skb_prev, skb);
		skb_prev = skb;
	}
}

/*
 *	Validate that the value of nr is between va and vs. Return true or
 *	false for testing.
 */
int nr_validate_nr(struct sock *sk, unsigned short nr)
{
	nr_cb *nrom = nr_sk(sk);
	unsigned short vc = nrom->va;

	while (vc != nrom->vs) {
		if (nr == vc) return 1;
		vc = (vc + 1) % NR_MODULUS;
	}

	return nr == nrom->vs;
}

/*
 *	Check that ns is within the receive window.
 */
int nr_in_rx_window(struct sock *sk, unsigned short ns)
{
	nr_cb *nr = nr_sk(sk);
	unsigned short vc = nr->vr;
	unsigned short vt = (nr->vl + nr->window) % NR_MODULUS;

	while (vc != vt) {
		if (ns == vc) return 1;
		vc = (vc + 1) % NR_MODULUS;
	}

	return 0;
}

/* 
 *  This routine is called when the HDLC layer internally generates a
 *  control frame.
 */
void nr_write_internal(struct sock *sk, int frametype)
{
	nr_cb *nr = nr_sk(sk);
	struct sk_buff *skb;
	unsigned char  *dptr;
	int len, timeout;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + NR_NETWORK_LEN + NR_TRANSPORT_LEN;

	switch (frametype & 0x0F) {
		case NR_CONNREQ:
			len += 17;
			break;
		case NR_CONNACK:
			len += (nr->bpqext) ? 2 : 1;
			break;
		case NR_DISCREQ:
		case NR_DISCACK:
		case NR_INFOACK:
			break;
		default:
			printk(KERN_ERR "NET/ROM: nr_write_internal - invalid frame type %d\n", frametype);
			return;
	}

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	/*
	 *	Space for AX.25 and NET/ROM network header
	 */
	skb_reserve(skb, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + NR_NETWORK_LEN);
	
	dptr = skb_put(skb, skb_tailroom(skb));

	switch (frametype & 0x0F) {

		case NR_CONNREQ:
			timeout  = nr->t1 / HZ;
			*dptr++  = nr->my_index;
			*dptr++  = nr->my_id;
			*dptr++  = 0;
			*dptr++  = 0;
			*dptr++  = frametype;
			*dptr++  = nr->window;
			memcpy(dptr, &nr->user_addr, AX25_ADDR_LEN);
			dptr[6] &= ~AX25_CBIT;
			dptr[6] &= ~AX25_EBIT;
			dptr[6] |= AX25_SSSID_SPARE;
			dptr    += AX25_ADDR_LEN;
			memcpy(dptr, &nr->source_addr, AX25_ADDR_LEN);
			dptr[6] &= ~AX25_CBIT;
			dptr[6] &= ~AX25_EBIT;
			dptr[6] |= AX25_SSSID_SPARE;
			dptr    += AX25_ADDR_LEN;
			*dptr++  = timeout % 256;
			*dptr++  = timeout / 256;
			break;

		case NR_CONNACK:
			*dptr++ = nr->your_index;
			*dptr++ = nr->your_id;
			*dptr++ = nr->my_index;
			*dptr++ = nr->my_id;
			*dptr++ = frametype;
			*dptr++ = nr->window;
			if (nr->bpqext) *dptr++ = sysctl_netrom_network_ttl_initialiser;
			break;

		case NR_DISCREQ:
		case NR_DISCACK:
			*dptr++ = nr->your_index;
			*dptr++ = nr->your_id;
			*dptr++ = 0;
			*dptr++ = 0;
			*dptr++ = frametype;
			break;

		case NR_INFOACK:
			*dptr++ = nr->your_index;
			*dptr++ = nr->your_id;
			*dptr++ = 0;
			*dptr++ = nr->vr;
			*dptr++ = frametype;
			break;
	}

	nr_transmit_buffer(sk, skb);
}

/*
 * This routine is called when a Connect Acknowledge with the Choke Flag
 * set is needed to refuse a connection.
 */
void nr_transmit_refusal(struct sk_buff *skb, int mine)
{
	struct sk_buff *skbn;
	unsigned char *dptr;
	int len;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + NR_NETWORK_LEN + NR_TRANSPORT_LEN + 1;

	if ((skbn = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skbn, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN);

	dptr = skb_put(skbn, NR_NETWORK_LEN + NR_TRANSPORT_LEN);

	memcpy(dptr, skb->data + 7, AX25_ADDR_LEN);
	dptr[6] &= ~AX25_CBIT;
	dptr[6] &= ~AX25_EBIT;
	dptr[6] |= AX25_SSSID_SPARE;
	dptr += AX25_ADDR_LEN;
	
	memcpy(dptr, skb->data + 0, AX25_ADDR_LEN);
	dptr[6] &= ~AX25_CBIT;
	dptr[6] |= AX25_EBIT;
	dptr[6] |= AX25_SSSID_SPARE;
	dptr += AX25_ADDR_LEN;

	*dptr++ = sysctl_netrom_network_ttl_initialiser;

	if (mine) {
		*dptr++ = 0;
		*dptr++ = 0;
		*dptr++ = skb->data[15];
		*dptr++ = skb->data[16];
	} else {
		*dptr++ = skb->data[15];
		*dptr++ = skb->data[16];
		*dptr++ = 0;
		*dptr++ = 0;
	}

	*dptr++ = NR_CONNACK | NR_CHOKE_FLAG;
	*dptr++ = 0;

	if (!nr_route_frame(skbn, NULL))
		kfree_skb(skbn);
}

void nr_disconnect(struct sock *sk, int reason)
{
	nr_stop_t1timer(sk);
	nr_stop_t2timer(sk);
	nr_stop_t4timer(sk);
	nr_stop_idletimer(sk);

	nr_clear_queues(sk);

	nr_sk(sk)->state = NR_STATE_0;

	sk->state     = TCP_CLOSE;
	sk->err       = reason;
	sk->shutdown |= SEND_SHUTDOWN;

	if (!sk->dead)
		sk->state_change(sk);

	sk->dead = 1;
}
