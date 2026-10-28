/* Loco simulator stand-in for the VCC's enet_user.h: UDP_Send goes out on a
 * real socket (loco_sim.c). */
#pragma once
#include <stdint.h>
typedef struct { uint8_t ip_addr0, ip_addr1, ip_addr2, ip_addr3; } ipAddress;
typedef struct { uint32_t addr; } ip_addr_t;
ip_addr_t set_pc_ipAddr(ipAddress address);
void UDP_Send(const char *buffer, ip_addr_t *ip_address, uint16_t port, uint16_t length);
