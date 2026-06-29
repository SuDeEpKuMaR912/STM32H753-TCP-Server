/**
  ******************************************************************************
  * @file    LwIP/LwIP_HTTP_Server_Netconn_RTOS/Src/app_ethernet.c
  * @author  MCD Application Team
  * @brief   Ethernet specific module
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "lwip/opt.h"
#include "main.h"
#if LWIP_DHCP
#include "lwip/dhcp.h"
#endif
#include "app_ethernet.h"
#include "ethernetif.h"
#include "lwip/netifapi.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
#define MAX_DHCP_TRIES  4
__IO uint8_t DHCP_state = DHCP_OFF;

uint8_t use_dhcp = 0;

ip_addr_t user_ip = IPADDR4_INIT_BYTES(192,168,80,120);
ip_addr_t user_netmask = IPADDR4_INIT_BYTES(255,255,255,0);
ip_addr_t user_gateway = IPADDR4_INIT_BYTES(192,168,0,163);



/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Notify the User about the network interface config status
  * @param  netif: the network interface
  * @retval None
  */
void ethernet_link_status_updated(struct netif *netif)
{
  if (netif_is_up(netif))
 {
#if LWIP_DHCP
    /* Update DHCP state machine */
    DHCP_state = DHCP_START;
#else
    BSP_LED_On(LED2);
    BSP_LED_Off(LED3);
#endif /* LWIP_DHCP */
  }
  else
  {
#if LWIP_DHCP
    /* Update DHCP state machine */
    DHCP_state = DHCP_LINK_DOWN;
#else
    BSP_LED_Off(LED2);
    BSP_LED_On(LED3);
#endif /* LWIP_DHCP */
  }
}

#if LWIP_DHCP
/**
  * @brief  DHCP Process
  * @param  argument: network interface
  * @retval None
  */
void DHCP_Thread(void* argument)
{
  struct netif *netif = (struct netif *) argument;
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gw;
  struct dhcp *dhcp;

  for (;;)
  {
    switch (DHCP_state)
    {
    case DHCP_START:
      {

        BSP_LED_Off(LED2);

        if(use_dhcp)
        {
            ip_addr_set_zero_ip4(&netif->ip_addr);
            ip_addr_set_zero_ip4(&netif->netmask);
            ip_addr_set_zero_ip4(&netif->gw);

            DHCP_state = DHCP_WAIT_ADDRESS;

            netifapi_dhcp_start(netif);
        }
        else
        {
            netifapi_dhcp_stop(netif);

            netifapi_netif_set_addr(
                    netif,
                    ip_2_ip4(&user_ip),
                    ip_2_ip4(&user_netmask),
                    ip_2_ip4(&user_gateway));

            DHCP_state = DHCP_ADDRESS_ASSIGNED;
        }
      }
      break;
    case DHCP_WAIT_ADDRESS:
      {
    	if (dhcp_supplied_address(netif))
    	{
    	   DHCP_state = DHCP_ADDRESS_ASSIGNED;

    	   BSP_LED_On(LED2);

    	   volatile uint32_t ip = netif->ip_addr.addr;
    	}
        else
        {
          dhcp = (struct dhcp *)netif_get_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP);

          /* DHCP timeout */
          if (dhcp->tries > MAX_DHCP_TRIES)
          {
            DHCP_state = DHCP_TIMEOUT;

            /* Static address used */
            IP_ADDR4(&ipaddr, IP_ADDR0 ,IP_ADDR1 , IP_ADDR2 , IP_ADDR3 );
            IP_ADDR4(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
            IP_ADDR4(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
            netifapi_netif_set_addr(netif, ip_2_ip4(&ipaddr), ip_2_ip4(&netmask), ip_2_ip4(&gw));

            BSP_LED_On(LED2);
          }
        }
      }
      break;
  case DHCP_LINK_DOWN:
    {
      DHCP_state = DHCP_OFF;

      BSP_LED_Off(LED2);
    }
    break;
    default: break;
    }

    /* wait 500 ms */
    osDelay(500);
  }
}

void Ethernet_EnableDHCP(void)
{
    use_dhcp = 1;
    DHCP_state = DHCP_START;
}

void Ethernet_EnableStatic(ip_addr_t *ip,
                           ip_addr_t *mask,
                           ip_addr_t *gw)
{
    use_dhcp = 0;

    ip_addr_copy(user_ip, *ip);
    ip_addr_copy(user_netmask, *mask);
    ip_addr_copy(user_gateway, *gw);

    DHCP_state = DHCP_START;
}

#endif  /* LWIP_DHCP */
