/**
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
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
 * This file is part of and a contribution to the lwIP TCP/IP stack.
 *
 * Credits go to Adam Dunkels (and the current maintainers) of this software.
 *
 * Christiaan Simons rewrote this file to get a more stable echo application.
 *
 **/

 /* This file was modified by ST */

#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"
#include "main.h"
#include <string.h>
#include "lwip/ip_addr.h"
#include <stdio.h>
#include "app_ethernet.h"


extern struct netif gnetif;
extern uint8_t use_dhcp;


void Ethernet_EnableDHCP(void);

void Ethernet_EnableStatic(ip_addr_t *ip,
                           ip_addr_t *mask,
                           ip_addr_t *gw);

#if LWIP_TCP

static struct tcp_pcb *tcp_echoserver_pcb;

/* ECHO protocol states */
enum tcp_echoserver_states
{
  ES_NONE = 0,
  ES_WAIT_USERNAME,
  ES_WAIT_PASSWORD,
  ES_AUTHENTICATED,
  ES_ACCEPTED,
  ES_RECEIVED,
  ES_CLOSING
};

/* structure for maintaining connection infos to be passed as argument
   to LwIP callbacks*/
struct tcp_echoserver_struct
{
  u8_t state;             /* current connection state */
  u8_t retries;
  struct tcp_pcb *pcb;    /* pointer on the current tcp_pcb */
  struct pbuf *p;         /* pointer on the received/to be transmitted pbuf */

  char username[20];
};


static err_t tcp_echoserver_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_echoserver_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void tcp_echoserver_error(void *arg, err_t err);
static err_t tcp_echoserver_poll(void *arg, struct tcp_pcb *tpcb);
static err_t tcp_echoserver_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
static void tcp_echoserver_send(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es);
static void tcp_echoserver_connection_close(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es);


/**
  * @brief  Initializes the tcp echo server
  * @param  None
  * @retval None
  */
void tcp_echoserver_init(void)
{
  /* create new tcp pcb */
  tcp_echoserver_pcb = tcp_new();

  if (tcp_echoserver_pcb != NULL)
  {
    err_t err;

    /* bind echo_pcb to port 23 (ECHO protocol) */
    err = tcp_bind(tcp_echoserver_pcb, IP_ADDR_ANY, 23);

    if (err == ERR_OK)
    {
      /* start tcp listening for echo_pcb */
      tcp_echoserver_pcb = tcp_listen(tcp_echoserver_pcb);

      /* initialize LwIP tcp_accept callback function */
      tcp_accept(tcp_echoserver_pcb, tcp_echoserver_accept);
    }
    else
    {
      /* deallocate the pcb */
      memp_free(MEMP_TCP_PCB, tcp_echoserver_pcb);
    }
  }
}

/**
  * @brief  This function is the implementation of tcp_accept LwIP callback
  * @param  arg: not used
  * @param  newpcb: pointer on tcp_pcb struct for the newly created tcp connection
  * @param  err: not used
  * @retval err_t: error status
  */
static err_t tcp_echoserver_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  err_t ret_err;
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(err);

  /* set priority for the newly accepted tcp connection newpcb */
  tcp_setprio(newpcb, TCP_PRIO_MIN);

  /* allocate structure es to maintain tcp connection information */
  es = (struct tcp_echoserver_struct *)mem_malloc(sizeof(struct tcp_echoserver_struct));
  if (es != NULL)
  {
	es->state = ES_WAIT_USERNAME;
    es->pcb = newpcb;
    es->retries = 0;
    es->p = NULL;

    /* pass newly allocated es structure as argument to newpcb */
    tcp_arg(newpcb, es);

    /* initialize lwip tcp_recv callback function for newpcb  */
    tcp_recv(newpcb, tcp_echoserver_recv);

    /* initialize lwip tcp_err callback function for newpcb  */
    tcp_err(newpcb, tcp_echoserver_error);

    /* initialize lwip tcp_poll callback function for newpcb */
    tcp_poll(newpcb, tcp_echoserver_poll, 0);

    char msg[] = "Username: ";
    tcp_write(newpcb, msg, strlen(msg), 1);

    ret_err = ERR_OK;
  }
  else
  {
    /*  close tcp connection */
    tcp_echoserver_connection_close(newpcb, es);
    /* return memory error */
    ret_err = ERR_MEM;
  }
  return ret_err;
}


/**
  * @brief  This function is the implementation for tcp_recv LwIP callback
  * @param  arg: pointer on a argument for the tcp_pcb connection
  * @param  tpcb: pointer on the tcp_pcb connection
  * @param  pbuf: pointer on the received pbuf
  * @param  err: error information regarding the reveived pbuf
  * @retval err_t: error code
  */
static err_t tcp_echoserver_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  struct tcp_echoserver_struct *es;
  err_t ret_err;

  LWIP_ASSERT("arg != NULL",arg != NULL);

  es = (struct tcp_echoserver_struct *)arg;

  /* if we receive an empty tcp frame from client => close connection */
  if (p == NULL)
  {
    /* remote host closed connection */
    es->state = ES_CLOSING;
    if(es->p == NULL)
    {
       /* we're done sending, close connection */
       tcp_echoserver_connection_close(tpcb, es);
    }
    else
    {
      /* we're not done yet */
      /* acknowledge received packet */
      tcp_sent(tpcb, tcp_echoserver_sent);

      /* send remaining data*/
      tcp_echoserver_send(tpcb, es);
    }
    ret_err = ERR_OK;
  }
  /* else : a non empty frame was received from client but for some reason err != ERR_OK */
  else if(err != ERR_OK)
  {
    /* free received pbuf*/
    if (p != NULL)
    {
      es->p = NULL;
      pbuf_free(p);
    }
    ret_err = err;
  }
  else if((es->state == ES_ACCEPTED) ||
          (es->state == ES_WAIT_USERNAME) ||
          (es->state == ES_WAIT_PASSWORD) ||
          (es->state == ES_AUTHENTICATED))
  {
    /* first data chunk in p->payload */

    /* store reference to incoming pbuf (chain) */
    es->p = p;

    /* initialize LwIP tcp_sent callback function */
    tcp_sent(tpcb, tcp_echoserver_sent);

    /* send back the received data (echo) */
    tcp_echoserver_send(tpcb, es);

    ret_err = ERR_OK;
  }
  else if (es->state == ES_RECEIVED)
  {
    /* more data received from client and previous data has been already sent*/
    if(es->p == NULL)
    {
      es->p = p;

      /* send back received data */
      tcp_echoserver_send(tpcb, es);
    }
    else
    {
      struct pbuf *ptr;

      /* chain pbufs to the end of what we recv'ed previously  */
      ptr = es->p;
      pbuf_chain(ptr,p);
    }
    ret_err = ERR_OK;
  }
  else if(es->state == ES_CLOSING)
  {
    /* odd case, remote side closing twice, trash data */
    tcp_recved(tpcb, p->tot_len);
    es->p = NULL;
    pbuf_free(p);
    ret_err = ERR_OK;
  }
  else
  {
    /* unknown es->state, trash data  */
    tcp_recved(tpcb, p->tot_len);
    es->p = NULL;
    pbuf_free(p);
    ret_err = ERR_OK;
  }
  return ret_err;
}

/**
  * @brief  This function implements the tcp_err callback function (called
  *         when a fatal tcp_connection error occurs.
  * @param  arg: pointer on argument parameter
  * @param  err: not used
  * @retval None
  */
static void tcp_echoserver_error(void *arg, err_t err)
{
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(err);

  es = (struct tcp_echoserver_struct *)arg;
  if (es != NULL)
  {
    /*  free es structure */
    mem_free(es);
  }
}

/**
  * @brief  This function implements the tcp_poll LwIP callback function
  * @param  arg: pointer on argument passed to callback
  * @param  tpcb: pointer on the tcp_pcb for the current tcp connection
  * @retval err_t: error code
  */
static err_t tcp_echoserver_poll(void *arg, struct tcp_pcb *tpcb)
{
  err_t ret_err;
  struct tcp_echoserver_struct *es;

  es = (struct tcp_echoserver_struct *)arg;
  if (es != NULL)
  {
    if (es->p != NULL)
    {
      tcp_sent(tpcb, tcp_echoserver_sent);
      /* there is a remaining pbuf (chain) , try to send data */
      tcp_echoserver_send(tpcb, es);
    }
    else
    {
      /* no remaining pbuf (chain)  */
      if(es->state == ES_CLOSING)
      {
        /*  close tcp connection */
        tcp_echoserver_connection_close(tpcb, es);
      }
    }
    ret_err = ERR_OK;
  }
  else
  {
    /* nothing to be done */
    tcp_abort(tpcb);
    ret_err = ERR_ABRT;
  }
  return ret_err;
}

/**
  * @brief  This function implements the tcp_sent LwIP callback (called when ACK
  *         is received from remote host for sent data)
  * @param  None
  * @retval None
  */
static err_t tcp_echoserver_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(len);

  es = (struct tcp_echoserver_struct *)arg;
  es->retries = 0;

  if(es->p != NULL)
  {
    /* still got pbufs to send */
    tcp_sent(tpcb, tcp_echoserver_sent);
    tcp_echoserver_send(tpcb, es);
  }
  else
  {
    /* if no more data to send and client closed connection*/
    if(es->state == ES_CLOSING)
      tcp_echoserver_connection_close(tpcb, es);
  }
  return ERR_OK;
}


/**
  * @brief  This function is used to send data for tcp connection
  * @param  tpcb: pointer on the tcp_pcb connection
  * @param  es: pointer on echo_state structure
  * @retval None
  */
static void tcp_echoserver_send(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es)
{
  struct pbuf *ptr;
  err_t wr_err = ERR_OK;

  char prompt[] = "\r\nCoral> ";

  while ((wr_err == ERR_OK) &&
         (es->p != NULL) &&
         (es->p->len <= tcp_sndbuf(tpcb)))
  {

    /* get pointer on pbuf from es structure */
    ptr = es->p;

    /* enqueue data for transmission */
    char cmd[50];

    memcpy(cmd, ptr->payload, ptr->len);
    cmd[ptr->len] = '\0';


    uint8_t *data = (uint8_t *)ptr->payload;

    /* Ignore Telnet negotiation packets */
    if (ptr->len > 0 && data[0] == 0xFF)
    {
        wr_err = ERR_OK;
    }
    else
    {
    	cmd[strcspn(cmd, "\r\n")] = '\0';

    	    if(strlen(cmd) != 0)
    	    {
    	    	if(es->state == ES_WAIT_USERNAME)
    	    	    {
    	    	        strcpy(es->username, cmd);

    	    	        es->state = ES_WAIT_PASSWORD;

    	    	        char msg[] = "\rPassword: ";

    	    	        wr_err = tcp_write(tpcb, msg, strlen(msg), 1);

    	    	    }
    	    	    else if(es->state == ES_WAIT_PASSWORD)
    	    	    {
    	    	        if(strcmp(es->username, "sudeepk") == 0 &&
    	    	           strcmp(cmd, "pass@123") == 0)
    	    	        {
    	    	            es->state = ES_AUTHENTICATED;

    	    	            char msg[] =
    	    	            "\r\nLogin Successful\r\n"
    	    	            "\r\nCoral> ";

    	    	            wr_err = tcp_write(tpcb, msg, strlen(msg), 1);

    	    	        }
    	    	        else
    	    	        {
    	    	            es->state = ES_WAIT_USERNAME;

    	    	            char msg[] =
    	    	            "\r\nInvalid Login\r\nUsername: ";

    	    	            wr_err = tcp_write(tpcb, msg, strlen(msg), 1);

    	    	        }
    	    	    }
    	    	    else if(es->state == ES_AUTHENTICATED)
    	    	    {
    	    	    	if(strcmp(cmd, "logout") == 0)
    	    	    	{
    	    	    	        es->state = ES_WAIT_USERNAME;


    	    	    	        char msg[] =
    	    	    	                "\r\nLogged Out\r\n"
    	    	    	                "Username: ";

    	    	    	        wr_err = tcp_write(tpcb,
    	    	    	                           msg,
    	    	    	                           strlen(msg),
    	    	    	                           1);
    	    	    	 }
    	    	    	else if(strcmp(cmd, "dhcp") == 0)
    	    	    	{
    	    	    	    Ethernet_EnableDHCP();

    	    	    	    char msg[] =
    	    	    	    "\r\nSwitched to DHCP mode\r\n"
    	    	    	    "\r\nCoral> ";

    	    	    	    wr_err = tcp_write(tpcb, msg, strlen(msg), 1);
    	    	    	}
    	    	    	else if(strncmp(cmd, "static ", 7) == 0)
    	    	    	{
    	    	    	    char ip[20];
    	    	    	    char mask[20];
    	    	    	    char gw[20];

    	    	    	    if(sscanf(cmd,
    	    	    	              "static %19s %19s %19s",
    	    	    	              ip,
    	    	    	              mask,
    	    	    	              gw) == 3)
    	    	    	    {
    	    	    	    	ip_addr_t ipaddr;
    	    	    	    	ip_addr_t netmask;
    	    	    	    	ip_addr_t gateway;

    	    	    	    	ipaddr_aton(ip, &ipaddr);
    	    	    	    	ipaddr_aton(mask, &netmask);
    	    	    	    	ipaddr_aton(gw, &gateway);

    	    	    	    	Ethernet_EnableStatic(&ipaddr,
    	    	    	    	                      &netmask,
    	    	    	    	                      &gateway);

    	    	    	    	char msg[] =
    	    	    	    	"Static IP configured\r\n"
    	    	    	    	"\r\nCoral> ";

    	    	    	        wr_err = tcp_write(tpcb,
    	    	    	                           msg,
    	    	    	                           strlen(msg),
    	    	    	                           1);
    	    	    	    }
    	    	    	    else
    	    	    	    {
    	    	    	        char msg[] =
    	    	    	        "\r\nUsage:\r\n"
    	    	    	        "static <ip> <mask> <gateway>\r\n";

    	    	    	        wr_err = tcp_write(tpcb,
    	    	    	                           msg,
    	    	    	                           strlen(msg),
    	    	    	                           1);
    	    	    	    }
    	    	    	}
    	    	    	else if(strcmp(cmd, "show ip") == 0)
    	    	    	{
    	    	    		char msg[50];

    	    	    		sprintf(msg,
    	    	    		        "\r\nIP : %s\r\n",
    	    	    		        ip4addr_ntoa(netif_ip4_addr(&gnetif)));

    	    	    		wr_err = tcp_write(tpcb,
    	    	    		                   msg,
    	    	    		                   strlen(msg),
    	    	    		                   1);

    	    	    		sprintf(msg,
    	    	    		        "Mask : %s\r\n",
    	    	    		        ip4addr_ntoa(netif_ip4_netmask(&gnetif)));

    	    	    		wr_err = tcp_write(tpcb,
    	    	    		                   msg,
    	    	    		                   strlen(msg),
    	    	    		                   1);

    	    	    		sprintf(msg,
    	    	    		        "Gateway : %s\r\n",
    	    	    		        ip4addr_ntoa(netif_ip4_gw(&gnetif)));

    	    	    		wr_err = tcp_write(tpcb,
    	    	    		                   msg,
    	    	    		                   strlen(msg),
    	    	    		                   1);

    	    	    		wr_err = tcp_write(tpcb,
    	    	    		                   prompt,
    	    	    		                   strlen(prompt),
    	    	    		                   1);
    	    	    	}
    	    	    	else if(strcmp(cmd, "status") == 0)
    	    	    	{
    	    	    		char msg[300];

    	    	    		char ip[20];
    	    	    		char mask[20];
    	    	    		char gateway[20];

    	    	    		ip4addr_ntoa_r(netif_ip4_addr(&gnetif), ip, sizeof(ip));
    	    	    		ip4addr_ntoa_r(netif_ip4_netmask(&gnetif), mask, sizeof(mask));
    	    	    		ip4addr_ntoa_r(netif_ip4_gw(&gnetif), gateway, sizeof(gateway));

    	    	    		sprintf(msg,
    	    	    		        "\r\n"
    	    	    		        "IP      : %s\r\n"
    	    	    		        "Mask    : %s\r\n"
    	    	    		        "Gateway : %s\r\n"
    	    	    		        "Mode    : %s\r\n"
    	    	    		        "Green   : %s\r\n"
    	    	    		        "Red     : %s\r\n"
    	    	    		        "\r\nCoral> ",
    	    	    		        ip,
    	    	    		        mask,
    	    	    		        gateway,
    	    	    		        use_dhcp ? "DHCP" : "STATIC",
    	    	    		        HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) ? "ON" : "OFF",
    	    	    		        HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14) ? "ON" : "OFF");

    	    	    		wr_err = tcp_write(tpcb,
    	    	    		                   msg,
    	    	    		                   strlen(msg),
    	    	    		                   1);

    	    	    	}
    	    	    	else if(strcmp(cmd, "uptime") == 0)
    	    	    	{
    	    	    	    char msg[100];

    	    	    	    uint32_t ms = HAL_GetTick();

    	    	    	    uint32_t seconds = ms / 1000;
    	    	    	    uint32_t minutes = seconds / 60;
    	    	    	    uint32_t hours   = minutes / 60;

    	    	    	    seconds %= 60;
    	    	    	    minutes %= 60;

    	    	    	    sprintf(msg,
    	    	    	            "\r\n"
    	    	    	            "Uptime : %luh %lum %lus\r\n"
    	    	    	            "\r\nCoral> ",
    	    	    	            hours,
    	    	    	            minutes,
    	    	    	            seconds);

    	    	    	    wr_err = tcp_write(tpcb,
    	    	    	                       msg,
    	    	    	                       strlen(msg),
    	    	    	                       1);
    	    	    	}
    	    	    	else if(strcmp(cmd, "help") == 0)
    	    	    	{
    	    	    	    char msg[] =
    	    	    	            "\r\n"
    	    	    	            "Available Commands\r\n"
    	    	    	            "------------------\r\n"
    	    	    	            "help\r\n"
    	    	    	            "status\r\n"
    	    	    	            "show ip\r\n"
    	    	    	            "uptime\r\n"
    	    	    	            "dhcp\r\n"
    	    	    	            "static <ip> <mask> <gateway>\r\n"
    	    	    	            "Green On\r\n"
    	    	    	            "Green Off\r\n"
    	    	    	            "Red On\r\n"
    	    	    	            "Red Off\r\n"
    	    	    	            "logout\r\n"
    	    	    	            "\r\nCoral> ";

    	    	    	    wr_err = tcp_write(tpcb,
    	    	    	                       msg,
    	    	    	                       strlen(msg),
    	    	    	                       1);
    	    	    	}
    	    	    	else if (strcmp(cmd, "Green On") == 0)
    	    	    	{
    	    	    	        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);

    	    	    	        char msg[] =
    	    	    	        "\r\nPB0 HIGH\r\n"
    	    	    	        "\r\nCoral> ";
    	    	    	        wr_err = tcp_write(tpcb, msg, strlen(msg), 1);
    	    	    	}
    	    	    	else if (strcmp(cmd, "Green Off") == 0)
    	    	    	{
    	    	    	        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

    	    	    	        char msg[] =
    	    	    	        "\r\nPB0 LOW\r\n"
    	    	    	        "\r\nCoral> ";
    	    	    	        wr_err = tcp_write(tpcb, msg, strlen(msg), 1);
    	    	    	}
    	    	    	else if (strcmp(cmd, "Red On") == 0)
    	    	    	{
    	    	    	        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);

    	    	    	        char msg[] =
    	    	    	        "\r\nPB14 HIGH\r\n"
    	    	    	        "\r\nCoral> ";
    	    	    	        wr_err = tcp_write(tpcb, msg, strlen(msg), 1);
    	    	    	}
    	    	    	else if (strcmp(cmd, "Red Off") == 0)
    	    	    	{
    	    	    	        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);

    	    	    	        char msg[] =
    	    	    	        "\r\nPB14 LOW\r\n"
    	    	    	        "\r\nCoral> ";
    	    	    	        wr_err = tcp_write(tpcb, msg, strlen(msg), 1);
    	    	    	}
    	    	    	else
    	    	    	{
    	    	    		char msg[] =
    	    	    		"\r\nERROR: Unknown command!\r\n"
    	    	    		"\r\nCoral> ";
    	    	    	        wr_err = tcp_write(tpcb, msg, strlen(msg), 1);
    	    	    	}
    	    	    }
    	    }
    }



    if (wr_err == ERR_OK)
    {
      u16_t plen;
      u8_t freed;

      plen = ptr->len;

      /* continue with next pbuf in chain (if any) */
      es->p = ptr->next;

      if(es->p != NULL)
      {
        /* increment reference count for es->p */
        pbuf_ref(es->p);
      }

     /* chop first pbuf from chain */
      do
      {
        /* try hard to free pbuf */
        freed = pbuf_free(ptr);
      }
      while(freed == 0);
     /* we can read more data now */
     tcp_recved(tpcb, plen);
   }
   else if(wr_err == ERR_MEM)
   {
      /* we are low on memory, try later / harder, defer to poll */
     es->p = ptr;
   }
   else
   {
     /* other problem ?? */
   }
  }
}

/**
  * @brief  This functions closes the tcp connection
  * @param  tcp_pcb: pointer on the tcp connection
  * @param  es: pointer on echo_state structure
  * @retval None
  */
static void tcp_echoserver_connection_close(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es)
{

  /* remove all callbacks */
  tcp_arg(tpcb, NULL);
  tcp_sent(tpcb, NULL);
  tcp_recv(tpcb, NULL);
  tcp_err(tpcb, NULL);
  tcp_poll(tpcb, NULL, 0);

  /* delete es structure */
  if (es != NULL)
  {
    mem_free(es);
  }

  /* close tcp connection */
  tcp_close(tpcb);
}

#endif /* LWIP_TCP */
