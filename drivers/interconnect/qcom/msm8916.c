// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Linaro Ltd
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#include <dt-bindings/interconnect/qcom,msm8916.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "msm8916_ids.h"
#include "smd-rpm.h"

#define RPM_BUS_MASTER_REQ      0x73616d62
#define RPM_BUS_SLAVE_REQ       0x766c7362

#define to_qcom_provider(_provider) \
	container_of(_provider, struct qcom_icc_provider, provider)

enum qcom_qos_mode {
	QCOM_QOS_MODE_BYPASS = 0,
	QCOM_QOS_MODE_FIXED,
	QCOM_QOS_MODE_MAX,
};

struct qcom_icc_provider {
	struct icc_provider	provider;
	void __iomem		*base;
	struct clk		*bus_clk;
	struct clk		*bus_a_clk;
};

#define MSM8916_MAX_LINKS	8

/**
 * struct qcom_icc_node - Qualcomm specific interconnect nodes
 * @name: the node name used in debugfs
 * @id: a unique node identifier
 * @links: an array of nodes where we can go next while traversing
 * @num_links: the total number of @links
 * @port: the offset index into the masters QoS register space
 * @buswidth: width of the interconnect between a node and the bus (bytes)
 * @ap_owned: the AP CPU does the writing to QoS registers
 * @qos_mode: QoS mode for ap_owned resources
 * @mas_rpm_id:	RPM id for devices that are bus masters
 * @slv_rpm_id:	RPM id for devices that are bus slaves
 * @rate: current bus clock rate in Hz
 */
struct qcom_icc_node {
	unsigned char *name;
	u16 id;
	u16 links[MSM8916_MAX_LINKS];
	u16 num_links;
	u16 port;
	u16 buswidth;
	bool ap_owned;
	enum qcom_qos_mode qos_mode;
	int mas_rpm_id;
	int slv_rpm_id;
	u64 rate;
};

struct qcom_icc_desc {
	struct qcom_icc_node **nodes;
	size_t num_nodes;
};

#define DEFINE_QNODE(_name, _id, _port, _buswidth, _ap_owned,		\
			_mas_rpm_id, _slv_rpm_id, _qos_mode,		\
			_numlinks, ...)					\
		static struct qcom_icc_node _name = {			\
		.name = #_name,						\
		.id = _id,						\
		.port = _port,						\
		.buswidth = _buswidth,					\
		.ap_owned = _ap_owned,					\
		.mas_rpm_id = _mas_rpm_id,				\
		.slv_rpm_id = _slv_rpm_id,				\
		.qos_mode = _qos_mode,					\
		.num_links = _numlinks,					\
		.links = { __VA_ARGS__ },				\
	}

DEFINE_QNODE(bimc_snoc_mas, MSM8916_BIMC_SNOC_MAS, 0, 8, 1, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_BIMC_SNOC_SLV);
DEFINE_QNODE(bimc_snoc_slv, MSM8916_BIMC_SNOC_SLV, 0, 8, 1, -1, -1, QCOM_QOS_MODE_FIXED, 2, MSM8916_SNOC_INT_0, MSM8916_SNOC_INT_1);
DEFINE_QNODE(mas_apss, MSM8916_MASTER_AMPSS_M0, 0, 8, 1, -1, -1, QCOM_QOS_MODE_FIXED, 3, MSM8916_SLAVE_EBI_CH0, MSM8916_BIMC_SNOC_MAS, MSM8916_SLAVE_AMPSS_L2);
DEFINE_QNODE(mas_audio, MSM8916_MASTER_LPASS, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_MAS_0);
DEFINE_QNODE(mas_blsp_1, MSM8916_MASTER_BLSP_1, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_MAS_1);
DEFINE_QNODE(mas_dehr, MSM8916_MASTER_DEHR, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_MAS_0);
DEFINE_QNODE(mas_gfx, MSM8916_MASTER_GRAPHICS_3D, 2, 8, 1, -1, -1, QCOM_QOS_MODE_FIXED, 3, MSM8916_SLAVE_EBI_CH0, MSM8916_BIMC_SNOC_MAS, MSM8916_SLAVE_AMPSS_L2);
DEFINE_QNODE(mas_jpeg, MSM8916_MASTER_JPEG, 6, 16, 1, -1, -1, QCOM_QOS_MODE_BYPASS, 2, MSM8916_SNOC_MM_INT_0, MSM8916_SNOC_MM_INT_2);
DEFINE_QNODE(mas_mdp, MSM8916_MASTER_MDP_PORT0, 7, 16, 1, -1, -1, QCOM_QOS_MODE_BYPASS, 2, MSM8916_SNOC_MM_INT_0, MSM8916_SNOC_MM_INT_2);
DEFINE_QNODE(mas_pnoc_crypto_0, MSM8916_MASTER_CRYPTO_CORE0, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_INT_1);
DEFINE_QNODE(mas_pnoc_sdcc_1, MSM8916_MASTER_SDCC_1, 7, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_INT_1);
DEFINE_QNODE(mas_pnoc_sdcc_2, MSM8916_MASTER_SDCC_2, 8, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_INT_1);
DEFINE_QNODE(mas_qdss_bam, MSM8916_MASTER_QDSS_BAM, 11, 8, 1, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_SNOC_QDSS_INT);
DEFINE_QNODE(mas_qdss_etr, MSM8916_MASTER_QDSS_ETR, 10, 8, 1, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_SNOC_QDSS_INT);
DEFINE_QNODE(mas_snoc_cfg, MSM8916_MASTER_SNOC_CFG, 0, 4, 0, 20, -1, QCOM_QOS_MODE_BYPASS, 1, MSM8916_SNOC_QDSS_INT);
DEFINE_QNODE(mas_spdm, MSM8916_MASTER_SPDM, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_MAS_0);
DEFINE_QNODE(mas_tcu0, MSM8916_MASTER_TCU0, 5, 8, 1, -1, -1, QCOM_QOS_MODE_FIXED, 3, MSM8916_SLAVE_EBI_CH0, MSM8916_BIMC_SNOC_MAS, MSM8916_SLAVE_AMPSS_L2);
DEFINE_QNODE(mas_tcu1, MSM8916_MASTER_TCU1, 6, 8, 1, -1, -1, QCOM_QOS_MODE_FIXED, 3, MSM8916_SLAVE_EBI_CH0, MSM8916_BIMC_SNOC_MAS, MSM8916_SLAVE_AMPSS_L2);
DEFINE_QNODE(mas_usb_hs, MSM8916_MASTER_USB_HS, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_MAS_1);
DEFINE_QNODE(mas_vfe, MSM8916_MASTER_VFE, 9, 16, 1, -1, -1, QCOM_QOS_MODE_BYPASS, 2, MSM8916_SNOC_MM_INT_1, MSM8916_SNOC_MM_INT_2);
DEFINE_QNODE(mas_video, MSM8916_MASTER_VIDEO_P0, 8, 16, 1, -1, -1, QCOM_QOS_MODE_BYPASS, 2, MSM8916_SNOC_MM_INT_0, MSM8916_SNOC_MM_INT_2);
DEFINE_QNODE(mm_int_0, MSM8916_SNOC_MM_INT_0, 0, 16, 1, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_SNOC_MM_INT_BIMC);
DEFINE_QNODE(mm_int_1, MSM8916_SNOC_MM_INT_1, 0, 16, 1, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_SNOC_MM_INT_BIMC);
DEFINE_QNODE(mm_int_2, MSM8916_SNOC_MM_INT_2, 0, 16, 1, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_SNOC_INT_0);
DEFINE_QNODE(mm_int_bimc, MSM8916_SNOC_MM_INT_BIMC, 0, 16, 1, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_SNOC_BIMC_1_MAS);
DEFINE_QNODE(pnoc_int_0, MSM8916_PNOC_INT_0, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 8, MSM8916_PNOC_SNOC_MAS, MSM8916_PNOC_SLV_0, MSM8916_PNOC_SLV_1, MSM8916_PNOC_SLV_2, MSM8916_PNOC_SLV_3, MSM8916_PNOC_SLV_4, MSM8916_PNOC_SLV_8, MSM8916_PNOC_SLV_9);
DEFINE_QNODE(pnoc_int_1, MSM8916_PNOC_INT_1, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_SNOC_MAS);
DEFINE_QNODE(pnoc_m_0, MSM8916_PNOC_MAS_0, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_INT_0);
DEFINE_QNODE(pnoc_m_1, MSM8916_PNOC_MAS_1, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_SNOC_MAS);
DEFINE_QNODE(pnoc_s_0, MSM8916_PNOC_SLV_0, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 5, MSM8916_SLAVE_CLK_CTL, MSM8916_SLAVE_TLMM, MSM8916_SLAVE_TCSR, MSM8916_SLAVE_SECURITY, MSM8916_SLAVE_MSS);
DEFINE_QNODE(pnoc_s_1, MSM8916_PNOC_SLV_1, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 5, MSM8916_SLAVE_IMEM_CFG, MSM8916_SLAVE_CRYPTO_0_CFG, MSM8916_SLAVE_MSG_RAM, MSM8916_SLAVE_PDM, MSM8916_SLAVE_PRNG);
DEFINE_QNODE(pnoc_s_2, MSM8916_PNOC_SLV_2, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 5, MSM8916_SLAVE_SPDM, MSM8916_SLAVE_BOOT_ROM, MSM8916_SLAVE_BIMC_CFG, MSM8916_SLAVE_PNOC_CFG, MSM8916_SLAVE_PMIC_ARB);
DEFINE_QNODE(pnoc_s_3, MSM8916_PNOC_SLV_3, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 5, MSM8916_SLAVE_MPM, MSM8916_SLAVE_SNOC_CFG, MSM8916_SLAVE_RBCPR_CFG, MSM8916_SLAVE_QDSS_CFG, MSM8916_SLAVE_DEHR_CFG);
DEFINE_QNODE(pnoc_s_4, MSM8916_PNOC_SLV_4, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 3, MSM8916_SLAVE_VENUS_CFG, MSM8916_SLAVE_CAMERA_CFG, MSM8916_SLAVE_DISPLAY_CFG);
DEFINE_QNODE(pnoc_s_8, MSM8916_PNOC_SLV_8, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 3, MSM8916_SLAVE_USB_HS, MSM8916_SLAVE_SDCC_1, MSM8916_SLAVE_BLSP_1);
DEFINE_QNODE(pnoc_s_9, MSM8916_PNOC_SLV_9, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 3, MSM8916_SLAVE_SDCC_2, MSM8916_SLAVE_LPASS, MSM8916_SLAVE_GRAPHICS_3D_CFG);
DEFINE_QNODE(pnoc_snoc_mas, MSM8916_PNOC_SNOC_MAS, 0, 8, 0, 29, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_SNOC_SLV);
DEFINE_QNODE(pnoc_snoc_slv, MSM8916_PNOC_SNOC_SLV, 0, 8, 0, -1, 45, QCOM_QOS_MODE_FIXED, 3, MSM8916_SNOC_INT_0, MSM8916_SNOC_INT_BIMC, MSM8916_SNOC_INT_1);
DEFINE_QNODE(qdss_int, MSM8916_SNOC_QDSS_INT, 0, 8, 1, -1, -1, QCOM_QOS_MODE_FIXED, 2, MSM8916_SNOC_INT_0, MSM8916_SNOC_INT_BIMC);
DEFINE_QNODE(slv_apps_l2, MSM8916_SLAVE_AMPSS_L2, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_apss, MSM8916_SLAVE_APSS, 0, 4, 0, -1, 20, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_audio, MSM8916_SLAVE_LPASS, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_bimc_cfg, MSM8916_SLAVE_BIMC_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_blsp_1, MSM8916_SLAVE_BLSP_1, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_boot_rom, MSM8916_SLAVE_BOOT_ROM, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_camera_cfg, MSM8916_SLAVE_CAMERA_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_cats_0, MSM8916_SLAVE_CATS_128, 0, 16, 0, -1, 106, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_cats_1, MSM8916_SLAVE_OCMEM_64, 0, 8, 0, -1, 107, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_clk_ctl, MSM8916_SLAVE_CLK_CTL, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_crypto_0_cfg, MSM8916_SLAVE_CRYPTO_0_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_dehr_cfg, MSM8916_SLAVE_DEHR_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_display_cfg, MSM8916_SLAVE_DISPLAY_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_ebi_ch0, MSM8916_SLAVE_EBI_CH0, 0, 8, 0, -1, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_gfx_cfg, MSM8916_SLAVE_GRAPHICS_3D_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_imem_cfg, MSM8916_SLAVE_IMEM_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_imem, MSM8916_SLAVE_IMEM, 0, 8, 0, -1, 26, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_mpm, MSM8916_SLAVE_MPM, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_msg_ram, MSM8916_SLAVE_MSG_RAM, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_mss, MSM8916_SLAVE_MSS, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pdm, MSM8916_SLAVE_PDM, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pmic_arb, MSM8916_SLAVE_PMIC_ARB, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pnoc_cfg, MSM8916_SLAVE_PNOC_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_prng, MSM8916_SLAVE_PRNG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_qdss_cfg, MSM8916_SLAVE_QDSS_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_qdss_stm, MSM8916_SLAVE_QDSS_STM, 0, 4, 0, -1, 30, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_rbcpr_cfg, MSM8916_SLAVE_RBCPR_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_sdcc_1, MSM8916_SLAVE_SDCC_1, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_sdcc_2, MSM8916_SLAVE_SDCC_2, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_security, MSM8916_SLAVE_SECURITY, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_snoc_cfg, MSM8916_SLAVE_SNOC_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_spdm, MSM8916_SLAVE_SPDM, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_srvc_snoc, MSM8916_SLAVE_SRVC_SNOC, 0, 8, 0, -1, 29, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_tcsr, MSM8916_SLAVE_TCSR, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_tlmm, MSM8916_SLAVE_TLMM, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_usb_hs, MSM8916_SLAVE_USB_HS, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_venus_cfg, MSM8916_SLAVE_VENUS_CFG, 0, 4, 0, -1, -1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(snoc_bimc_0_mas, MSM8916_SNOC_BIMC_0_MAS, 0, 8, 0, 3, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_SNOC_BIMC_0_SLV);
DEFINE_QNODE(snoc_bimc_0_slv, MSM8916_SNOC_BIMC_0_SLV, 0, 8, 0, -1, 24, QCOM_QOS_MODE_FIXED, 1, MSM8916_SLAVE_EBI_CH0);
DEFINE_QNODE(snoc_bimc_1_mas, MSM8916_SNOC_BIMC_1_MAS, 0, 16, 1, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_SNOC_BIMC_1_SLV);
DEFINE_QNODE(snoc_bimc_1_slv, MSM8916_SNOC_BIMC_1_SLV, 0, 8, 1, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_SLAVE_EBI_CH0);
DEFINE_QNODE(snoc_int_0, MSM8916_SNOC_INT_0, 0, 8, 0, 99, 130, QCOM_QOS_MODE_FIXED, 3, MSM8916_SLAVE_QDSS_STM, MSM8916_SLAVE_IMEM, MSM8916_SNOC_PNOC_MAS);
DEFINE_QNODE(snoc_int_1, MSM8916_SNOC_INT_1, 0, 8, 0, 100, 131, QCOM_QOS_MODE_FIXED, 3, MSM8916_SLAVE_APSS, MSM8916_SLAVE_CATS_128, MSM8916_SLAVE_OCMEM_64);
DEFINE_QNODE(snoc_int_bimc, MSM8916_SNOC_INT_BIMC, 0, 8, 0, 101, 132, QCOM_QOS_MODE_FIXED, 1, MSM8916_SNOC_BIMC_0_MAS);
DEFINE_QNODE(snoc_pnoc_mas, MSM8916_SNOC_PNOC_MAS, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_SNOC_PNOC_SLV);
DEFINE_QNODE(snoc_pnoc_slv, MSM8916_SNOC_PNOC_SLV, 0, 8, 0, -1, -1, QCOM_QOS_MODE_FIXED, 1, MSM8916_PNOC_INT_0);

static struct qcom_icc_node *msm8916_snoc_nodes[] = {
	[BIMC_SNOC_SLV] = &bimc_snoc_slv,
	[MASTER_JPEG] = &mas_jpeg,
	[MASTER_MDP_PORT0] = &mas_mdp,
	[MASTER_QDSS_BAM] = &mas_qdss_bam,
	[MASTER_QDSS_ETR] = &mas_qdss_etr,
	[MASTER_SNOC_CFG] = &mas_snoc_cfg,
	[MASTER_VFE] = &mas_vfe,
	[MASTER_VIDEO_P0] = &mas_video,
	[SNOC_MM_INT_0] = &mm_int_0,
	[SNOC_MM_INT_1] = &mm_int_1,
	[SNOC_MM_INT_2] = &mm_int_2,
	[SNOC_MM_INT_BIMC] = &mm_int_bimc,
	[PNOC_SNOC_SLV] = &pnoc_snoc_slv,
	[SLAVE_APSS] = &slv_apss,
	[SLAVE_CATS_128] = &slv_cats_0,
	[SLAVE_OCMEM_64] = &slv_cats_1,
	[SLAVE_IMEM] = &slv_imem,
	[SLAVE_QDSS_STM] = &slv_qdss_stm,
	[SLAVE_SRVC_SNOC] = &slv_srvc_snoc,
	[SNOC_BIMC_0_MAS] = &snoc_bimc_0_mas,
	[SNOC_BIMC_1_MAS] = &snoc_bimc_1_mas,
	[SNOC_INT_0] = &snoc_int_0,
	[SNOC_INT_1] = &snoc_int_1,
	[SNOC_INT_BIMC] = &snoc_int_bimc,
	[SNOC_PNOC_MAS] = &snoc_pnoc_mas,
	[SNOC_QDSS_INT] = &qdss_int,
};

static struct qcom_icc_desc msm8916_snoc = {
	.nodes = msm8916_snoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8916_snoc_nodes),
};

static struct qcom_icc_node *msm8916_bimc_nodes[] = {
	[BIMC_SNOC_MAS] = &bimc_snoc_mas,
	[MASTER_AMPSS_M0] = &mas_apss,
	[MASTER_GRAPHICS_3D] = &mas_gfx,
	[MASTER_TCU0] = &mas_tcu0,
	[MASTER_TCU1] = &mas_tcu1,
	[SLAVE_AMPSS_L2] = &slv_apps_l2,
	[SLAVE_EBI_CH0] = &slv_ebi_ch0,
	[SNOC_BIMC_0_SLV] = &snoc_bimc_0_slv,
	[SNOC_BIMC_1_SLV] = &snoc_bimc_1_slv,
};

static struct qcom_icc_desc msm8916_bimc = {
	.nodes = msm8916_bimc_nodes,
	.num_nodes = ARRAY_SIZE(msm8916_bimc_nodes),
};

static struct qcom_icc_node *msm8916_pnoc_nodes[] = {
	[MASTER_BLSP_1] = &mas_blsp_1,
	[MASTER_DEHR] = &mas_dehr,
	[MASTER_LPASS] = &mas_audio,
	[MASTER_CRYPTO_CORE0] = &mas_pnoc_crypto_0,
	[MASTER_SDCC_1] = &mas_pnoc_sdcc_1,
	[MASTER_SDCC_2] = &mas_pnoc_sdcc_2,
	[MASTER_SPDM] = &mas_spdm,
	[MASTER_USB_HS] = &mas_usb_hs,
	[PNOC_INT_0] = &pnoc_int_0,
	[PNOC_INT_1] = &pnoc_int_1,
	[PNOC_MAS_0] = &pnoc_m_0,
	[PNOC_MAS_1] = &pnoc_m_1,
	[PNOC_SLV_0] = &pnoc_s_0,
	[PNOC_SLV_1] = &pnoc_s_1,
	[PNOC_SLV_2] = &pnoc_s_2,
	[PNOC_SLV_3] = &pnoc_s_3,
	[PNOC_SLV_4] = &pnoc_s_4,
	[PNOC_SLV_8] = &pnoc_s_8,
	[PNOC_SLV_9] = &pnoc_s_9,
	[PNOC_SNOC_MAS] = &pnoc_snoc_mas,
	[SLAVE_BIMC_CFG] = &slv_bimc_cfg,
	[SLAVE_BLSP_1] = &slv_blsp_1,
	[SLAVE_BOOT_ROM] = &slv_boot_rom,
	[SLAVE_CAMERA_CFG] = &slv_camera_cfg,
	[SLAVE_CLK_CTL] = &slv_clk_ctl,
	[SLAVE_CRYPTO_0_CFG] = &slv_crypto_0_cfg,
	[SLAVE_DEHR_CFG] = &slv_dehr_cfg,
	[SLAVE_DISPLAY_CFG] = &slv_display_cfg,
	[SLAVE_GRAPHICS_3D_CFG] = &slv_gfx_cfg,
	[SLAVE_IMEM_CFG] = &slv_imem_cfg,
	[SLAVE_LPASS] = &slv_audio,
	[SLAVE_MPM] = &slv_mpm,
	[SLAVE_MSG_RAM] = &slv_msg_ram,
	[SLAVE_MSS] = &slv_mss,
	[SLAVE_PDM] = &slv_pdm,
	[SLAVE_PMIC_ARB] = &slv_pmic_arb,
	[SLAVE_PNOC_CFG] = &slv_pnoc_cfg,
	[SLAVE_PRNG] = &slv_prng,
	[SLAVE_QDSS_CFG] = &slv_qdss_cfg,
	[SLAVE_RBCPR_CFG] = &slv_rbcpr_cfg,
	[SLAVE_SDCC_1] = &slv_sdcc_1,
	[SLAVE_SDCC_2] = &slv_sdcc_2,
	[SLAVE_SECURITY] = &slv_security,
	[SLAVE_SNOC_CFG] = &slv_snoc_cfg,
	[SLAVE_SPDM] = &slv_spdm,
	[SLAVE_TCSR] = &slv_tcsr,
	[SLAVE_TLMM] = &slv_tlmm,
	[SLAVE_USB_HS] = &slv_usb_hs,
	[SLAVE_VENUS_CFG] = &slv_venus_cfg,
	[SNOC_PNOC_SLV] = &snoc_pnoc_slv,
};

static struct qcom_icc_desc msm8916_pnoc = {
	.nodes = msm8916_pnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8916_pnoc_nodes),
};

static int qcom_icc_aggregate(struct icc_node *node, u8 tag, u32 avg_bw,
			      u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	*agg_avg += avg_bw;
	*agg_peak = max(*agg_peak, peak_bw);

	return 0;
}

static int qcom_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct qcom_icc_provider *qp;
	struct qcom_icc_node *qn;
	struct icc_provider *provider;
	struct icc_node *n;
	u64 sum_bw;
	u64 max_peak_bw;
	u64 rate;
	u32 agg_avg = 0;
	u32 agg_peak = 0;
	int ret = 0;

	qn = src->data;
	provider = src->provider;
	qp = to_qcom_provider(provider);

	list_for_each_entry(n, &provider->nodes, node_list)
		qcom_icc_aggregate(n, 0, n->avg_bw, n->peak_bw,
				   &agg_avg, &agg_peak);

	sum_bw = icc_units_to_bps(agg_avg);
	max_peak_bw = icc_units_to_bps(agg_peak);

	/* set bandwidth */
	if (qn->ap_owned) {
		/* TODO: set QoS */
	} else {
		/* send message to the RPM processor */
		if (qn->mas_rpm_id != -1) {
			ret = qcom_icc_rpm_smd_send(QCOM_SMD_RPM_ACTIVE_STATE,
						    RPM_BUS_MASTER_REQ,
						    qn->mas_rpm_id,
						    sum_bw);
			if (ret) {
				pr_err("qcom_icc_rpm_smd_send mas %d error %d\n", qn->mas_rpm_id, ret);
				return ret;
			}
		}

		if (qn->slv_rpm_id != -1) {
			ret = qcom_icc_rpm_smd_send(QCOM_SMD_RPM_ACTIVE_STATE,
						    RPM_BUS_SLAVE_REQ,
						    qn->slv_rpm_id,
						    sum_bw);
			if (ret) {
				pr_err("qcom_icc_rpm_smd_send slv error %d\n",
				       ret);
				return ret;
			}
		}
	}

	rate = max(sum_bw, max_peak_bw);

	do_div(rate, qn->buswidth);

	if (qn->rate != rate) {
		ret = clk_set_rate(qp->bus_clk, rate);
		if (ret) {
			pr_err("set clk rate %lld error %d\n", rate, ret);
			return ret;
		}

		ret = clk_set_rate(qp->bus_a_clk, rate);
		if (ret) {
			pr_err("set clk rate %lld error %d\n", rate, ret);
			return ret;
		}

		qn->rate = rate;
	}

	return ret;
}

static int qnoc_probe(struct platform_device *pdev)
{
	const struct qcom_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_node **qnodes;
	struct qcom_icc_provider *qp;
	struct icc_node *node;
	struct resource *res;
	size_t num_nodes, i;
	int ret;

	/* wait for RPM */
	if (!qcom_icc_rpm_smd_available())
		return -EPROBE_DEFER;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(&pdev->dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kcalloc(&pdev->dev, num_nodes, sizeof(*node), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	qp->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(qp->base))
		return PTR_ERR(qp->base);

	qp->bus_clk = devm_clk_get(&pdev->dev, "bus_clk");
	if (IS_ERR(qp->bus_clk))
		return PTR_ERR(qp->bus_clk);

	ret = clk_prepare_enable(qp->bus_clk);
	if (ret) {
		dev_err(&pdev->dev, "error enabling bus_clk: %d\n", ret);
		return ret;
	}

	qp->bus_a_clk = devm_clk_get(&pdev->dev, "bus_a_clk");
	if (IS_ERR(qp->bus_a_clk))
		return PTR_ERR(qp->bus_a_clk);

	ret = clk_prepare_enable(qp->bus_a_clk);
	if (ret) {
		dev_err(&pdev->dev, "error enabling bus_a_clk: %d\n", ret);
		clk_disable_unprepare(qp->bus_clk);
		return ret;
	}

	provider = &qp->provider;
	INIT_LIST_HEAD(&provider->nodes);
	provider->dev = &pdev->dev;
	provider->set = qcom_icc_set;
	provider->aggregate = qcom_icc_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	provider->data = data;

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(&pdev->dev, "error adding interconnect provider\n");
		clk_disable_unprepare(qp->bus_clk);
		clk_disable_unprepare(qp->bus_a_clk);
		return ret;
	}

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = qnodes[i]->name;
		node->data = qnodes[i];
		icc_node_add(node, provider);

		dev_dbg(&pdev->dev, "registered node %s\n", node->name);

		/* populate links */
		for (j = 0; j < qnodes[i]->num_links; j++)
			icc_link_create(node, qnodes[i]->links[j]);

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;

	platform_set_drvdata(pdev, qp);

	return ret;
err:
	list_for_each_entry(node, &provider->nodes, node_list) {
		icc_node_del(node);
		icc_node_destroy(node->id);
	}
	clk_disable_unprepare(qp->bus_clk);
	clk_disable_unprepare(qp->bus_a_clk);
	icc_provider_del(provider);

	return ret;
}

static int qnoc_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);
	struct icc_provider *provider = &qp->provider;
	struct icc_node *n;

	list_for_each_entry(n, &provider->nodes, node_list) {
		icc_node_del(n);
		icc_node_destroy(n->id);
	}
	clk_disable_unprepare(qp->bus_clk);
	clk_disable_unprepare(qp->bus_a_clk);

	return icc_provider_del(provider);
}

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,msm8916-pnoc", .data = &msm8916_pnoc },
	{ .compatible = "qcom,msm8916-snoc", .data = &msm8916_snoc },
	{ .compatible = "qcom,msm8916-bimc", .data = &msm8916_bimc },
	{ },
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-msm8916",
		.of_match_table = qnoc_of_match,
	},
};
module_platform_driver(qnoc_driver);
MODULE_AUTHOR("Georgi Djakov <georgi.djakov@linaro.org>");
MODULE_DESCRIPTION("Qualcomm msm8916 NoC driver");
MODULE_LICENSE("GPL v2");
