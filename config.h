#pragma once
#include <stddef.h>
#include <stdint.h>

/* Configuration */

static const char udp_ip_remote[] = "10.11.19.1";
static const char udp_ip_local[] = "10.11.19.2";
static const uint16_t udp_port = 51461;
static const char udp_ip_mask[] = "255.255.255.252";

static const char tun_if_name[] = "qbkit-bridge";
static const size_t tun_if_mtu = 4000;
static const char tun_ip_remote[] = "10.11.19.5";
static const char tun_ip_local[] = "10.11.19.6";
static const char tun_ip_mask[] = "255.255.255.252";

static const char route_addr[] = "0.0.0.0";
static const char route_mask[] = "0.0.0.0";
static const int route_metric = 10000;
