#pragma once
#include <stdint.h>
typedef struct { uint8_t ip_addr0, ip_addr1, ip_addr2, ip_addr3; } ipAddress;
typedef struct { uint32_t addr; } ip_addr_t;
ip_addr_t set_pc_ipAddr(ipAddress a);
void UDP_Send(const char *buf, ip_addr_t *ip, uint16_t port, uint16_t len);
