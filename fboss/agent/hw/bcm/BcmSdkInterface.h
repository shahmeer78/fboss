/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

extern "C" {
//
// BCM
//
#include <bcm/cosq.h>
#include <bcm/error.h>
#include <bcm/field.h>
#include <bcm/init.h>
#include <bcm/l2.h>
#include <bcm/l3.h>
#include <bcm/mirror.h>
#include <bcm/port.h>
#include <bcm/qos.h>
#include <bcm/rx.h>
#include <bcm/stat.h>
#include <bcm/switch.h>
#include <bcm/trunk.h>

// Opennsl
#include <bcm/types.h>
//
// Shared
//
#include <shared/error.h>
}

namespace facebook::fboss {

class BcmSdkInterface {
 public:
  virtual ~BcmSdkInterface() = default;

  //
  // BCM
  //

  virtual int bcm_switch_pkt_trace_info_get(
      int unit,
      uint32 options,
      uint8 port,
      int len,
      uint8* data,
      bcm_switch_pkt_trace_info_t* pkt_trace_info) = 0;

  virtual int bcm_cosq_bst_stat_clear(
      int unit,
      bcm_gport_t gport,
      bcm_cos_queue_t cosq,
      bcm_bst_stat_id_t bid) = 0;

  virtual int bcm_field_range_get(
      int unit,
      bcm_field_range_t range,
      uint32* flags,
      bcm_l4_port_t* min,
      bcm_l4_port_t* max) = 0;

  virtual int bcm_port_phy_control_get(
      int unit,
      bcm_port_t port,
      bcm_port_phy_control_t type,
      uint32* value) = 0;

  virtual int bcm_cosq_gport_traverse(
      int unit,
      bcm_cosq_gport_traverse_cb cb,
      void* user_data) = 0;

  virtual int bcm_cosq_gport_sched_set(
      int unit,
      bcm_gport_t gport,
      bcm_cos_queue_t cosq,
      int mode,
      int weight) = 0;

  virtual int bcm_cosq_gport_sched_get(
      int unit,
      bcm_gport_t gport,
      bcm_cos_queue_t cosq,
      int* mode,
      int* weight) = 0;

  virtual int bcm_field_range_create(
      int unit,
      bcm_field_range_t* range,
      uint32 flags,
      bcm_l4_port_t min,
      bcm_l4_port_t max) = 0;

  virtual int bcm_cosq_init(int unit) = 0;

  virtual int
  bcm_field_entry_prio_set(int unit, bcm_field_entry_t entry, int prio) = 0;

  virtual int bcm_rx_cosq_mapping_size_get(int unit, int* size) = 0;

  virtual int bcm_field_action_add(
      int unit,
      bcm_field_entry_t entry,
      bcm_field_action_t action,
      uint32 param0,
      uint32 param1) = 0;

  virtual int bcm_field_action_delete(
      int unit,
      bcm_field_entry_t entry,
      bcm_field_action_t action,
      uint32 param0,
      uint32 param1) = 0;

  virtual int bcm_cosq_bst_profile_set(
      int unit,
      bcm_gport_t gport,
      bcm_cos_queue_t cosq,
      bcm_bst_stat_id_t bid,
      bcm_cosq_bst_profile_t* profile) = 0;

  virtual int bcm_field_qualify_DstIp6(
      int unit,
      bcm_field_entry_t entry,
      bcm_ip6_t data,
      bcm_ip6_t mask) = 0;

  virtual int bcm_field_qualify_InPorts(
      int unit,
      bcm_field_entry_t entry,
      bcm_pbmp_t data,
      bcm_pbmp_t mask) = 0;

  virtual int bcm_field_range_destroy(int unit, bcm_field_range_t range) = 0;

  virtual int bcm_rx_active(int unit) = 0;

  virtual int bcm_field_qualify_L4SrcPort(
      int unit,
      bcm_field_entry_t entry,
      bcm_l4_port_t data,
      bcm_l4_port_t mask) = 0;

  virtual int bcm_field_qualify_IcmpTypeCode(
      int unit,
      bcm_field_entry_t entry,
      uint16 data,
      uint16 mask) = 0;

  virtual int bcm_field_entry_create(
      int unit,
      bcm_field_group_t group,
      bcm_field_entry_t* entry) = 0;

  virtual int bcm_port_autoneg_set(int unit, bcm_port_t port, int autoneg) = 0;

  virtual int bcm_field_action_get(
      int unit,
      bcm_field_entry_t entry,
      bcm_field_action_t action,
      uint32* param0,
      uint32* param1) = 0;

  virtual int bcm_field_group_traverse(
      int unit,
      bcm_field_group_traverse_cb callback,
      void* user_data) = 0;

  virtual int
  bcm_switch_control_set(int unit, bcm_switch_control_t type, int arg) = 0;

  virtual int
  bcm_switch_control_get(int unit, bcm_switch_control_t type, int* arg) = 0;

  virtual int bcm_field_entry_create_id(
      int unit,
      bcm_field_group_t group,
      bcm_field_entry_t entry) = 0;

  virtual int bcm_port_ability_advert_set(
      int unit,
      bcm_port_t port,
      bcm_port_ability_t* ability_mask) = 0;

  virtual int bcm_field_qualify_SrcIp6(
      int unit,
      bcm_field_entry_t entry,
      bcm_ip6_t data,
      bcm_ip6_t mask) = 0;

  virtual int
  bcm_l2_traverse(int unit, bcm_l2_traverse_cb trav_fn, void* user_data) = 0;

  virtual int bcm_port_subsidiary_ports_get(
      int unit,
      bcm_port_t port,
      bcm_pbmp_t* pbmp) = 0;

  virtual int bcm_field_group_enable_get(
      int unit,
      bcm_field_group_t group,
      int* enable) = 0;

  virtual int bcm_l3_info(int unit, bcm_l3_info_t* l3info) = 0;

  virtual int bcm_field_qualify_SrcPort(
      int unit,
      bcm_field_entry_t entry,
      bcm_module_t data_modid,
      bcm_module_t mask_modid,
      bcm_port_t data_port,
      bcm_port_t mask_port) = 0;

  virtual int bcm_l3_egress_ecmp_add(
      int unit,
      bcm_l3_egress_ecmp_t* ecmp,
      bcm_if_t intf) = 0;

  virtual int bcm_field_qualify_RangeCheck(
      int unit,
      bcm_field_entry_t entry,
      bcm_field_range_t range,
      int invert) = 0;

  virtual int bcm_field_init(int unit) = 0;

  virtual int bcm_cosq_control_set(
      int unit,
      bcm_gport_t port,
      bcm_cos_queue_t cosq,
      bcm_cosq_control_t type,
      int arg) = 0;

  virtual void bcm_cosq_gport_discard_t_init(
      bcm_cosq_gport_discard_t* discard) = 0;

  virtual int bcm_cosq_gport_discard_set(
      int unit,
      bcm_gport_t gport,
      bcm_cos_queue_t cosq,
      bcm_cosq_gport_discard_t* discard) = 0;

  virtual int bcm_cosq_gport_mapping_set(
      int unit,
      bcm_gport_t ing_port,
      bcm_cos_t priority,
      uint32 flags,
      bcm_gport_t gport,
      bcm_cos_queue_t cosq) = 0;

  virtual int bcm_cosq_gport_mapping_get(
      int unit,
      bcm_gport_t ing_port,
      bcm_cos_t priority,
      uint32 flags,
      bcm_gport_t* gport,
      bcm_cos_queue_t* cosq) = 0;

  virtual int bcm_cosq_gport_discard_get(
      int unit,
      bcm_gport_t gport,
      bcm_cos_queue_t cosq,
      bcm_cosq_gport_discard_t* discard) = 0;

  virtual int bcm_qos_map_create(int unit, uint32 flags, int* map_id) = 0;

  virtual int bcm_qos_map_destroy(int unit, int map_id) = 0;

  virtual int
  bcm_qos_map_add(int unit, uint32 flags, bcm_qos_map_t* map, int map_id) = 0;

  virtual int bcm_qos_map_delete(
      int unit,
      uint32 flags,
      bcm_qos_map_t* map,
      int map_id) = 0;

  virtual int bcm_qos_map_multi_get(
      int unit,
      uint32 flags,
      int map_id,
      int array_size,
      bcm_qos_map_t* array,
      int* array_count) = 0;

  virtual int bcm_qos_port_map_set(
      int unit,
      bcm_gport_t gport,
      int ing_map,
      int egr_map) = 0;

  virtual int bcm_qos_port_map_get(
      int unit,
      bcm_gport_t gport,
      int* ing_map,
      int* egr_map) = 0;

  virtual int bcm_qos_port_map_type_get(
      int unit,
      bcm_gport_t gport,
      uint32 flags,
      int* map_id) = 0;

  virtual int
  bcm_port_dscp_map_mode_get(int unit, bcm_port_t port, int* mode) = 0;

  virtual int
  bcm_port_dscp_map_mode_set(int unit, bcm_port_t port, int mode) = 0;

  virtual int bcm_qos_multi_get(
      int unit,
      int array_size,
      int* map_ids_array,
      int* flags_array,
      int* array_count) = 0;

  virtual int bcm_field_qualify_DstPort(
      int unit,
      bcm_field_entry_t entry,
      bcm_module_t data_modid,
      bcm_module_t mask_modid,
      bcm_port_t data_port,
      bcm_port_t mask_port) = 0;

  virtual int bcm_field_qualify_DstMac(
      int unit,
      bcm_field_entry_t entry,
      bcm_mac_t mac,
      bcm_mac_t macMask) = 0;

  virtual int bcm_field_qualify_SrcMac(
      int unit,
      bcm_field_entry_t entry,
      bcm_mac_t mac,
      bcm_mac_t macMask) = 0;

  virtual void bcm_l3_egress_ecmp_t_init(bcm_l3_egress_ecmp_t* ecmp) = 0;

  virtual int bcm_port_phy_control_set(
      int unit,
      bcm_port_t port,
      bcm_port_phy_control_t type,
      uint32 value) = 0;

  virtual int bcm_field_qualify_L4DstPort(
      int unit,
      bcm_field_entry_t entry,
      bcm_l4_port_t data,
      bcm_l4_port_t mask) = 0;

  virtual int bcm_field_qualify_TcpControl(
      int unit,
      bcm_field_entry_t entry,
      uint8 data,
      uint8 mask) = 0;

  virtual int bcm_cosq_gport_bandwidth_set(
      int unit,
      bcm_gport_t gport,
      bcm_cos_queue_t cosq,
      uint32 kbits_sec_min,
      uint32 kbits_sec_max,
      uint32 flags) = 0;
  virtual int bcm_cosq_gport_bandwidth_get(
      int unit,
      bcm_gport_t gport,
      bcm_cos_queue_t cosq,
      uint32* kbits_sec_min,
      uint32* kbits_sec_max,
      uint32* flags) = 0;

  virtual int bcm_cosq_bst_profile_get(
      int unit,
      bcm_gport_t gport,
      bcm_cos_queue_t cosq,
      bcm_bst_stat_id_t bid,
      bcm_cosq_bst_profile_t* profile) = 0;

  virtual int bcm_field_entry_destroy(int unit, bcm_field_entry_t entry) = 0;

  virtual int bcm_field_group_create_id(
      int unit,
      bcm_field_qset_t qset,
      int pri,
      bcm_field_group_t group) = 0;

  virtual int bcm_port_pause_sym_set(int unit, bcm_port_t port, int pause) = 0;

  virtual int bcm_switch_object_count_multi_get(
      int unit,
      int object_size,
      bcm_switch_object_t* object_array,
      int* entries) = 0;

  virtual int bcm_field_entry_multi_get(
      int unit,
      bcm_field_group_t group,
      int entry_size,
      bcm_field_entry_t* entry_array,
      int* entry_count) = 0;

  virtual int bcm_rx_cosq_mapping_set(
      int unit,
      int index,
      bcm_rx_reasons_t reasons,
      bcm_rx_reasons_t reasons_mask,
      uint8 int_prio,
      uint8 int_prio_mask,
      uint32 packet_type,
      uint32 packet_type_mask,
      bcm_cos_queue_t cosq) = 0;

  virtual int bcm_rx_cosq_mapping_get(
      int unit,
      int index,
      bcm_rx_reasons_t* reasons,
      bcm_rx_reasons_t* reasons_mask,
      uint8* int_prio,
      uint8* int_prio_mask,
      uint32* packet_type,
      uint32* packet_type_mask,
      bcm_cos_queue_t* cosq) = 0;

  virtual int bcm_rx_cosq_mapping_delete(int unit, int index) = 0;

  virtual int bcm_cosq_bst_stat_get(
      int unit,
      bcm_gport_t gport,
      bcm_cos_queue_t cosq,
      bcm_bst_stat_id_t bid,
      uint32 options,
      uint64* value) = 0;

  virtual int bcm_cosq_bst_stat_sync(int unit, bcm_bst_stat_id_t bid) = 0;
  virtual int bcm_stat_custom_add(
      int unit,
      bcm_port_t port,
      bcm_stat_val_t type,
      bcm_custom_stat_trigger_t trigger) = 0;

  virtual int bcm_field_qualify_IpFrag(
      int unit,
      bcm_field_entry_t entry,
      bcm_field_IpFrag_t frag_info) = 0;

  virtual int bcm_field_qualify_IpProtocol(
      int unit,
      bcm_field_entry_t entry,
      uint8 data,
      uint8 mask) = 0;

  virtual int bcm_rx_queue_max_get(int unit, bcm_cos_queue_t* cosq) = 0;

  virtual int bcm_field_group_get(
      int unit,
      bcm_field_group_t group,
      bcm_field_qset_t* qset) = 0;

  virtual int bcm_field_group_status_get(
      int unit,
      bcm_field_group_t group,
      bcm_field_group_status_t* status) = 0;

  virtual int bcm_field_stat_create(
      int unit,
      bcm_field_group_t group,
      int nstat,
      bcm_field_stat_t* stat_arr,
      int* stat_id) = 0;

  virtual int bcm_field_entry_stat_attach(
      int unit,
      bcm_field_entry_t entry,
      int stat_id) = 0;

  virtual int bcm_field_entry_stat_detach(
      int unit,
      bcm_field_entry_t entry,
      int stat_id) = 0;

  virtual int
  bcm_field_entry_stat_get(int unit, bcm_field_entry_t entry, int* stat_id) = 0;

  virtual int bcm_field_stat_destroy(int unit, int stat_id) = 0;

  virtual int bcm_field_stat_get(
      int unit,
      int stat_id,
      bcm_field_stat_t stat,
      uint64* value) = 0;

  virtual int bcm_field_stat_size(int unit, int stat_id, int* stat_size) = 0;

  virtual int bcm_field_stat_config_get(
      int unit,
      int stat_id,
      int nstat,
      bcm_field_stat_t* stat_arr) = 0;

  virtual int bcm_field_entry_reinstall(int unit, bcm_field_entry_t entry) = 0;

  virtual int bcm_field_qualify_RangeCheck_get(
      int unit,
      bcm_field_entry_t entry,
      int max_count,
      bcm_field_range_t* range,
      int* invert,
      int* count) = 0;

  virtual int
  bcm_field_entry_prio_get(int unit, bcm_field_entry_t entry, int* prio) = 0;

  virtual int bcm_field_qualify_SrcIp6_get(
      int unit,
      bcm_field_entry_t entry,
      bcm_ip6_t* data,
      bcm_ip6_t* mask) = 0;

  virtual int bcm_field_qualify_DstIp6_get(
      int unit,
      bcm_field_entry_t entry,
      bcm_ip6_t* data,
      bcm_ip6_t* mask) = 0;

  virtual int bcm_field_qualify_L4SrcPort_get(
      int unit,
      bcm_field_entry_t entry,
      bcm_l4_port_t* data,
      bcm_l4_port_t* mask) = 0;

  virtual int bcm_field_qualify_L4DstPort_get(
      int unit,
      bcm_field_entry_t entry,
      bcm_l4_port_t* data,
      bcm_l4_port_t* mask) = 0;

  virtual int bcm_field_qualify_TcpControl_get(
      int unit,
      bcm_field_entry_t entry,
      uint8* data,
      uint8* mask) = 0;

  virtual int bcm_field_qualify_SrcPort_get(
      int unit,
      bcm_field_entry_t entry,
      bcm_module_t* data_modid,
      bcm_module_t* mask_modid,
      bcm_port_t* data_port,
      bcm_port_t* mask_port) = 0;

  virtual int bcm_field_qualify_DstPort_get(
      int unit,
      bcm_field_entry_t entry,
      bcm_module_t* data_modid,
      bcm_module_t* mask_modid,
      bcm_port_t* data_port,
      bcm_port_t* mask_port) = 0;

  virtual int bcm_field_qualify_IpFrag_get(
      int unit,
      bcm_field_entry_t entry,
      bcm_field_IpFrag_t* frag_info) = 0;

  virtual int bcm_field_qualify_DSCP_get(
      int unit,
      bcm_field_entry_t entry,
      uint8* data,
      uint8* mask) = 0;

  virtual int bcm_field_qualify_IpProtocol_get(
      int unit,
      bcm_field_entry_t entry,
      uint8* data,
      uint8* mask) = 0;

  virtual int bcm_field_qualify_IpType_get(
      int unit,
      bcm_field_entry_t entry,
      bcm_field_IpType_t* type) = 0;

  virtual int bcm_field_qualify_Ttl_get(
      int unit,
      bcm_field_entry_t entry,
      uint8* data,
      uint8* mask) = 0;

  virtual int bcm_field_entry_enable_get(
      int unit,
      bcm_field_entry_t entry,
      int* enable_flag) = 0;

  virtual int bcm_field_qualify_DstMac_get(
      int unit,
      bcm_field_entry_t entry,
      bcm_mac_t* data,
      bcm_mac_t* mask) = 0;

  virtual int bcm_field_qualify_SrcMac_get(
      int unit,
      bcm_field_entry_t entry,
      bcm_mac_t* data,
      bcm_mac_t* mask) = 0;

  virtual int bcm_field_qualify_Ttl(
      int unit,
      bcm_field_entry_t entry,
      uint8 data,
      uint8 mask) = 0;

  virtual int bcm_field_qualify_IpType(
      int unit,
      bcm_field_entry_t entry,
      bcm_field_IpType_t type) = 0;

  virtual int bcm_field_qualify_DSCP(
      int unit,
      bcm_field_entry_t entry,
      uint8 data,
      uint8 mask) = 0;

  virtual int bcm_field_qualify_DstClassL2_get(
      int unit,
      bcm_field_entry_t entry,
      uint32* data,
      uint32* mask) = 0;

  virtual int bcm_field_qualify_DstClassL2(
      int unit,
      bcm_field_entry_t entry,
      uint32 data,
      uint32 mask) = 0;

  virtual int bcm_field_qualify_DstClassL3_get(
      int unit,
      bcm_field_entry_t entry,
      uint32* data,
      uint32* mask) = 0;

  virtual int bcm_field_qualify_DstClassL3(
      int unit,
      bcm_field_entry_t entry,
      uint32 data,
      uint32 mask) = 0;

  virtual int bcm_field_group_destroy(int unit, bcm_field_group_t) = 0;

  virtual int bcm_cosq_control_get(
      int unit,
      bcm_gport_t port,
      bcm_cos_queue_t cosq,
      bcm_cosq_control_t type,
      int* arg) = 0;

  virtual int
  bcm_l3_egress_get(int unit, bcm_if_t intf, bcm_l3_egress_t* egr) = 0;

  virtual int bcm_l3_egress_create(
      int unit,
      uint32 flags,
      bcm_l3_egress_t* egr,
      bcm_if_t* if_id) = 0;

  virtual int
  bcm_l3_egress_find(int unit, bcm_l3_egress_t* egr, bcm_if_t* intf) = 0;

  virtual int bcm_l3_egress_traverse(
      int unit,
      bcm_l3_egress_traverse_cb trav_fn,
      void* user_data) = 0;

  virtual int bcm_l3_egress_ecmp_delete(
      int unit,
      bcm_l3_egress_ecmp_t* ecmp,
      bcm_if_t intf) = 0;

  virtual int bcm_l3_egress_ecmp_get(
      int unit,
      bcm_l3_egress_ecmp_t* ecmp,
      int intf_size,
      bcm_if_t* intf_array,
      int* intf_count) = 0;

  virtual int bcm_field_entry_install(int unit, bcm_field_entry_t entry) = 0;

  virtual int bcm_field_qualify_DstIp(
      int unit,
      bcm_field_entry_t entry,
      bcm_ip_t data,
      bcm_ip_t mask) = 0;

  virtual int bcm_port_pause_get(
      int unit,
      bcm_port_t port,
      int* pause_tx,
      int* pause_rx) = 0;

  virtual int
  bcm_port_pause_set(int unit, bcm_port_t port, int pause_tx, int pause_rx) = 0;

  virtual int bcm_port_sample_rate_get(
      int unit,
      bcm_port_t port,
      int* ingress_rate,
      int* egress_rate) = 0;

  virtual int bcm_port_sample_rate_set(
      int unit,
      bcm_port_t port,
      int ingress_rate,
      int egress_rate) = 0;

  virtual int bcm_port_control_set(
      int unit,
      bcm_port_t port,
      bcm_port_control_t type,
      int value) = 0;

  virtual int bcm_port_control_get(
      int unit,
      bcm_port_t port,
      bcm_port_control_t type,
      int* value) = 0;

  virtual int bcm_info_get(int unit, bcm_info_t* info) = 0;

  virtual int bcm_linkscan_update(int unit, bcm_pbmp_t pbmp) = 0;

  virtual int bcm_trunk_bitmap_expand(int unit, bcm_pbmp_t* pbmp_ptr) = 0;

  virtual int
  bcm_port_loopback_get(int unit, bcm_port_t port, uint32* value) = 0;

  virtual int
  bcm_port_loopback_set(int unit, bcm_port_t port, uint32 value) = 0;

  virtual int bcm_mirror_init(int unit) = 0;

  virtual int bcm_mirror_mode_set(int unit, int mode) = 0;

  virtual int bcm_mirror_destination_create(
      int unit,
      bcm_mirror_destination_t* mirror_dest) = 0;

  virtual int bcm_mirror_destination_get(
      int unit,
      bcm_gport_t mirror_dest_id,
      bcm_mirror_destination_t* mirror_dest) = 0;

  virtual int bcm_mirror_destination_destroy(
      int unit,
      bcm_gport_t mirror_dest_id) = 0;

  virtual int bcm_mirror_port_dest_add(
      int unit,
      bcm_port_t port,
      uint32 flags,
      bcm_gport_t mirror_dest_id) = 0;

  virtual int bcm_mirror_port_dest_delete(
      int unit,
      bcm_port_t port,
      uint32 flags,
      bcm_gport_t mirror_dest_id) = 0;

  virtual int
  bcm_mirror_port_dest_delete_all(int unit, bcm_port_t port, uint32 flags) = 0;

  virtual int bcm_mirror_port_dest_get(
      int unit,
      bcm_port_t port,
      uint32 flags,
      int mirror_dest_size,
      bcm_gport_t* mirror_dest,
      int* mirror_dest_count) = 0;

  virtual int bcm_mirror_destination_traverse(
      int unit,
      bcm_mirror_destination_traverse_cb cb,
      void* user_data) = 0;

  virtual int sh_process_command(int unit, char* cmd) = 0;

  // mpls
  virtual int bcm_mpls_init(int unit) = 0;

  virtual int bcm_mpls_tunnel_switch_add(
      int unit,
      bcm_mpls_tunnel_switch_t* info) = 0;

  virtual int bcm_mpls_tunnel_switch_delete(
      int unit,
      bcm_mpls_tunnel_switch_t* info) = 0;

  virtual int bcm_mpls_tunnel_switch_get(
      int unit,
      bcm_mpls_tunnel_switch_t* info) = 0;

  virtual int bcm_mpls_tunnel_switch_traverse(
      int unit,
      bcm_mpls_tunnel_switch_traverse_cb cb,
      void* user_data) = 0;

  virtual int bcm_mpls_tunnel_initiator_set(
      int unit,
      bcm_if_t intf,
      int num_labels,
      bcm_mpls_egress_label_t* label_array) = 0;

  virtual int bcm_mpls_tunnel_initiator_clear(int unit, bcm_if_t intf) = 0;

  virtual int bcm_mpls_tunnel_initiator_get(
      int unit,
      bcm_if_t intf,
      int label_max,
      bcm_mpls_egress_label_t* label_array,
      int* label_count) = 0;

  // port resource APIs
  virtual int bcm_port_resource_speed_set(
      int unit,
      bcm_gport_t port,
      bcm_port_resource_t* resource) = 0;

  virtual int bcm_port_resource_speed_get(
      int unit,
      bcm_gport_t port,
      bcm_port_resource_t* resource) = 0;

  virtual int bcm_port_resource_multi_set(
      int unit,
      int nport,
      bcm_port_resource_t* resource) = 0;

  virtual int bcm_l2_addr_delete_by_port(
      int unit,
      bcm_module_t mod,
      bcm_port_t port,
      uint32 flags) = 0;
};

} // namespace facebook::fboss