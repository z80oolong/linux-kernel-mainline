// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Linaro Ltd
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#include <dt-bindings/interconnect/qcom,msm8996.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "msm8996_ids.h"
#include "smd-rpm.h"

#define RPM_MASTER_FIELD_BW	0x00007762
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

#define MSM8996_MAX_LINKS       38

/**
 * struct qcom_icc_node - Qualcomm specific interconnect nodes
 * @name: the node name used in debugfs
 * @id: a unique node identifier
 * @links: an array of nodes where we can go next while traversing
 * @num_links: the total number of @links
 * @port: the offset index into the masters QoS register space
 * @agg_ports: the number of aggregation ports on the bus
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
	u16 links[MSM8996_MAX_LINKS];
	u16 num_links;
	u16 port;
	u16 agg_ports;
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

#define DEFINE_QNODE(_name, _id, _port, _agg_ports, _buswidth,		\
			_qos_mode, _ap_owned, _mas_rpm_id, _slv_rpm_id, \
			_numlinks, ...)					\
		static struct qcom_icc_node _name = {			\
		.name = #_name,						\
		.id = _id,						\
		.port = _port,						\
		.agg_ports = _agg_ports,				\
		.buswidth = _buswidth,					\
		.qos_mode = _qos_mode,					\
		.ap_owned = _ap_owned,					\
		.mas_rpm_id = _mas_rpm_id,				\
		.slv_rpm_id = _slv_rpm_id,				\
		.num_links = _numlinks,					\
		.links = { __VA_ARGS__ },				\
	}

DEFINE_QNODE(mas_a0noc_snoc, MSM8996_A0NOC_SNOC_MAS, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, 110, -1, 5, MSM8996_SNOC_PNOC_SLV, MSM8996_SLAVE_OCIMEM, MSM8996_SLAVE_APPSS, MSM8996_SNOC_BIMC_SLV, MSM8996_SLAVE_PIMEM);
DEFINE_QNODE(mas_a1noc_snoc, MSM8996_A1NOC_SNOC_MAS, 0, 1, 16, QCOM_QOS_MODE_FIXED, 0, 111, -1, 13, MSM8996_SLAVE_SNOC_VMEM, MSM8996_SLAVE_USB3, MSM8996_SLAVE_PCIE_0, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_PCIE_2, MSM8996_SLAVE_LPASS, MSM8996_SLAVE_PCIE_1, MSM8996_SLAVE_APPSS, MSM8996_SNOC_BIMC_SLV, MSM8996_SNOC_CNOC_SLV, MSM8996_SNOC_PNOC_SLV, MSM8996_SLAVE_OCIMEM, MSM8996_SLAVE_QDSS_STM);
DEFINE_QNODE(mas_a2noc_snoc, MSM8996_A2NOC_SNOC_MAS, 0, 1, 16, QCOM_QOS_MODE_FIXED, 0, 112, -1, 12, MSM8996_SLAVE_SNOC_VMEM, MSM8996_SLAVE_USB3, MSM8996_SLAVE_PCIE_1, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_PCIE_2, MSM8996_SLAVE_QDSS_STM, MSM8996_SLAVE_LPASS, MSM8996_SNOC_BIMC_SLV, MSM8996_SNOC_CNOC_SLV, MSM8996_SNOC_PNOC_SLV, MSM8996_SLAVE_OCIMEM, MSM8996_SLAVE_PCIE_0);
DEFINE_QNODE(mas_apps_proc, MSM8996_MASTER_AMPSS_M0, 0, 2, 8, QCOM_QOS_MODE_FIXED, 1, 0, -1, 3, MSM8996_BIMC_SNOC_1_SLV, MSM8996_SLAVE_EBI_CH0, MSM8996_BIMC_SNOC_SLV);
DEFINE_QNODE(mas_bimc_snoc_0, MSM8996_BIMC_SNOC_MAS, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, 21, -1, 9, MSM8996_SLAVE_SNOC_VMEM, MSM8996_SLAVE_USB3, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_LPASS, MSM8996_SLAVE_APPSS, MSM8996_SNOC_CNOC_SLV, MSM8996_SNOC_PNOC_SLV, MSM8996_SLAVE_OCIMEM, MSM8996_SLAVE_QDSS_STM);
DEFINE_QNODE(mas_bimc_snoc_1, MSM8996_BIMC_SNOC_1_MAS, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, 109, -1, 3, MSM8996_SLAVE_PCIE_2, MSM8996_SLAVE_PCIE_1, MSM8996_SLAVE_PCIE_0);
DEFINE_QNODE(mas_blsp_1, MSM8996_MASTER_BLSP_1, 0, 1, 4, QCOM_QOS_MODE_BYPASS, 0, 41, -1, 1, MSM8996_PNOC_A1NOC_SLV);
DEFINE_QNODE(mas_blsp_2, MSM8996_MASTER_BLSP_2, 0, 1, 4, QCOM_QOS_MODE_BYPASS, 0, 39, -1, 1, MSM8996_PNOC_A1NOC_SLV);
DEFINE_QNODE(mas_cnoc_a1noc, MSM8996_CNOC_A1NOC_MAS, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, 116, -1, 1, MSM8996_A1NOC_SNOC_SLV);
DEFINE_QNODE(mas_cnoc_mnoc_cfg, MSM8996_MASTER_CNOC_MNOC_CFG, 0, 1, 8, QCOM_QOS_MODE_BYPASS, 1, 5, -1, 1, MSM8996_SLAVE_SERVICE_MNOC);
DEFINE_QNODE(mas_cnoc_mnoc_mmss_cfg, MSM8996_MASTER_CNOC_MNOC_MMSS_CFG, 0, 1, 8, QCOM_QOS_MODE_BYPASS, 1, 4, -1, 21, MSM8996_SLAVE_MMAGIC_CFG, MSM8996_SLAVE_DSA_MPU_CFG, MSM8996_SLAVE_MMSS_CLK_CFG, MSM8996_SLAVE_CAMERA_THROTTLE_CFG, MSM8996_SLAVE_VENUS_CFG, MSM8996_SLAVE_SMMU_VFE_CFG, MSM8996_SLAVE_MISC_CFG, MSM8996_SLAVE_SMMU_CPP_CFG, MSM8996_SLAVE_GRAPHICS_3D_CFG, MSM8996_SLAVE_DISPLAY_THROTTLE_CFG, MSM8996_SLAVE_VENUS_THROTTLE_CFG, MSM8996_SLAVE_CAMERA_CFG, MSM8996_SLAVE_DISPLAY_CFG, MSM8996_SLAVE_CPR_CFG, MSM8996_SLAVE_SMMU_ROTATOR_CFG, MSM8996_SLAVE_DSA_CFG, MSM8996_SLAVE_SMMU_VENUS_CFG, MSM8996_SLAVE_VMEM_CFG, MSM8996_SLAVE_SMMU_JPEG_CFG, MSM8996_SLAVE_SMMU_MDP_CFG, MSM8996_SLAVE_MNOC_MPU_CFG);
DEFINE_QNODE(mas_cpp, MSM8996_MASTER_CPP, 5, 1, 32, QCOM_QOS_MODE_BYPASS, 1, 115, -1, 1, MSM8996_MNOC_BIMC_SLV);
DEFINE_QNODE(mas_crypto_c0, MSM8996_MASTER_CRYPTO_CORE0, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, 23, -1, 1, MSM8996_A1NOC_SNOC_SLV);
DEFINE_QNODE(mas_hmss, MSM8996_MASTER_HMSS, 4, 1, 8, QCOM_QOS_MODE_FIXED, 1, 118, -1, 3, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_OCIMEM, MSM8996_SNOC_BIMC_SLV);
DEFINE_QNODE(mas_ipa, MSM8996_MASTER_IPA, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, 59, -1, 1, MSM8996_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_jpeg, MSM8996_MASTER_JPEG, 7, 1, 32, QCOM_QOS_MODE_BYPASS, 1, 7, -1, 1, MSM8996_MNOC_BIMC_SLV);
DEFINE_QNODE(mas_mdp_p0, MSM8996_MASTER_MDP_PORT0, 1, 1, 32, QCOM_QOS_MODE_BYPASS, 1, 8, -1, 1, MSM8996_MNOC_BIMC_SLV);
DEFINE_QNODE(mas_mdp_p1, MSM8996_MASTER_MDP_PORT1, 2, 1, 32, QCOM_QOS_MODE_BYPASS, 1, 61, -1, 1, MSM8996_MNOC_BIMC_SLV);
DEFINE_QNODE(mas_mnoc_bimc, MSM8996_MNOC_BIMC_MAS, 2, 2, 8, QCOM_QOS_MODE_BYPASS, 1, 2, -1, 4, MSM8996_BIMC_SNOC_1_SLV, MSM8996_SLAVE_HMSS_L3, MSM8996_SLAVE_EBI_CH0, MSM8996_BIMC_SNOC_SLV);
DEFINE_QNODE(mas_oxili, MSM8996_MASTER_GRAPHICS_3D, 1, 2, 8, QCOM_QOS_MODE_BYPASS, 1, 6, -1, 4, MSM8996_BIMC_SNOC_1_SLV, MSM8996_SLAVE_HMSS_L3, MSM8996_SLAVE_EBI_CH0, MSM8996_BIMC_SNOC_SLV);
DEFINE_QNODE(mas_pcie_0, MSM8996_MASTER_PCIE, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, 65, -1, 1, MSM8996_A0NOC_SNOC_SLV);
DEFINE_QNODE(mas_pcie_1, MSM8996_MASTER_PCIE_1, 1, 1, 8, QCOM_QOS_MODE_FIXED, 1, 66, -1, 1, MSM8996_A0NOC_SNOC_SLV);
DEFINE_QNODE(mas_pcie_2, MSM8996_MASTER_PCIE_2, 2, 1, 8, QCOM_QOS_MODE_FIXED, 1, 119, -1, 1, MSM8996_A0NOC_SNOC_SLV);
DEFINE_QNODE(mas_pnoc_a1noc, MSM8996_PNOC_A1NOC_MAS, 1, 1, 8, QCOM_QOS_MODE_FIXED, 0, 117, -1, 1, MSM8996_A1NOC_SNOC_SLV);
DEFINE_QNODE(mas_qdss_bam, MSM8996_MASTER_QDSS_BAM, 2, 1, 16, QCOM_QOS_MODE_FIXED, 1, 19, -1, 5, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_USB3, MSM8996_SLAVE_OCIMEM, MSM8996_SNOC_BIMC_SLV, MSM8996_SNOC_PNOC_SLV);
DEFINE_QNODE(mas_qdss_dap, MSM8996_MASTER_QDSS_DAP, 0, 1, 8, QCOM_QOS_MODE_BYPASS, 1, 49, -1, 38, MSM8996_SLAVE_QDSS_RBCPR_APU_CFG, MSM8996_SLAVE_RBCPR_CX, MSM8996_SLAVE_A2NOC_SMMU_CFG, MSM8996_SLAVE_A0NOC_MPU_CFG, MSM8996_SLAVE_MESSAGE_RAM, MSM8996_SLAVE_PCIE_0_CFG, MSM8996_SLAVE_TLMM, MSM8996_SLAVE_MPM, MSM8996_SLAVE_A0NOC_SMMU_CFG, MSM8996_SLAVE_EBI1_PHY_CFG, MSM8996_SLAVE_BIMC_CFG, MSM8996_SLAVE_PIMEM_CFG, MSM8996_SLAVE_RBCPR_MX, MSM8996_SLAVE_CLK_CTL, MSM8996_SLAVE_PRNG, MSM8996_SLAVE_PCIE20_AHB2PHY, MSM8996_SLAVE_A2NOC_MPU_CFG, MSM8996_SLAVE_QDSS_CFG, MSM8996_SLAVE_A2NOC_CFG, MSM8996_SLAVE_A0NOC_CFG, MSM8996_SLAVE_UFS_CFG, MSM8996_SLAVE_CRYPTO_0_CFG, MSM8996_CNOC_SNOC_SLV, MSM8996_SLAVE_PCIE_1_CFG, MSM8996_SLAVE_SNOC_CFG, MSM8996_SLAVE_SNOC_MPU_CFG, MSM8996_SLAVE_A1NOC_MPU_CFG, MSM8996_SLAVE_A1NOC_SMMU_CFG, MSM8996_SLAVE_PCIE_2_CFG, MSM8996_SLAVE_CNOC_MNOC_CFG, MSM8996_SLAVE_CNOC_MNOC_MMSS_CFG, MSM8996_SLAVE_PMIC_ARB, MSM8996_SLAVE_IMEM_CFG, MSM8996_SLAVE_A1NOC_CFG, MSM8996_SLAVE_SSC_CFG, MSM8996_SLAVE_TCSR, MSM8996_SLAVE_LPASS_SMMU_CFG, MSM8996_SLAVE_DCC_CFG);
DEFINE_QNODE(mas_qdss_etr, MSM8996_MASTER_QDSS_ETR, 3, 1, 16, QCOM_QOS_MODE_FIXED, 1, 31, -1, 5, MSM8996_SLAVE_PIMEM, MSM8996_SLAVE_USB3, MSM8996_SLAVE_OCIMEM, MSM8996_SNOC_BIMC_SLV, MSM8996_SNOC_PNOC_SLV);
DEFINE_QNODE(mas_rotator, MSM8996_MASTER_ROTATOR, 0, 1, 32, QCOM_QOS_MODE_BYPASS, 1, 120, -1, 1, MSM8996_MNOC_BIMC_SLV);
DEFINE_QNODE(mas_sdcc_1, MSM8996_MASTER_SDCC_1, 0, 1, 8, QCOM_QOS_MODE_BYPASS, 0, 33, -1, 1, MSM8996_PNOC_A1NOC_SLV);
DEFINE_QNODE(mas_sdcc_2, MSM8996_MASTER_SDCC_2, 0, 1, 8, QCOM_QOS_MODE_BYPASS, 0, 35, -1, 1, MSM8996_PNOC_A1NOC_SLV);
DEFINE_QNODE(mas_sdcc_4, MSM8996_MASTER_SDCC_4, 0, 1, 8, QCOM_QOS_MODE_BYPASS, 0, 36, -1, 1, MSM8996_PNOC_A1NOC_SLV);
DEFINE_QNODE(mas_snoc_bimc, MSM8996_SNOC_BIMC_MAS, 0, 2, 8, QCOM_QOS_MODE_BYPASS, 0, 3, -1, 2, MSM8996_SLAVE_HMSS_L3, MSM8996_SLAVE_EBI_CH0);
DEFINE_QNODE(mas_snoc_cfg, MSM8996_MASTER_SNOC_CFG, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, 20, -1, 1, MSM8996_SLAVE_SERVICE_SNOC);
DEFINE_QNODE(mas_snoc_cnoc, MSM8996_SNOC_CNOC_MAS, 0, 1, 8, QCOM_QOS_MODE_BYPASS, 0, 52, -1, 37, MSM8996_SLAVE_CLK_CTL, MSM8996_SLAVE_RBCPR_CX, MSM8996_SLAVE_A2NOC_SMMU_CFG, MSM8996_SLAVE_A0NOC_MPU_CFG, MSM8996_SLAVE_MESSAGE_RAM, MSM8996_SLAVE_CNOC_MNOC_MMSS_CFG, MSM8996_SLAVE_PCIE_0_CFG, MSM8996_SLAVE_TLMM, MSM8996_SLAVE_MPM, MSM8996_SLAVE_A0NOC_SMMU_CFG, MSM8996_SLAVE_EBI1_PHY_CFG, MSM8996_SLAVE_BIMC_CFG, MSM8996_SLAVE_PIMEM_CFG, MSM8996_SLAVE_RBCPR_MX, MSM8996_SLAVE_PRNG, MSM8996_SLAVE_PCIE20_AHB2PHY, MSM8996_SLAVE_A2NOC_MPU_CFG, MSM8996_SLAVE_QDSS_CFG, MSM8996_SLAVE_A2NOC_CFG, MSM8996_SLAVE_A0NOC_CFG, MSM8996_SLAVE_UFS_CFG, MSM8996_SLAVE_CRYPTO_0_CFG, MSM8996_SLAVE_PCIE_1_CFG, MSM8996_SLAVE_SNOC_CFG, MSM8996_SLAVE_SNOC_MPU_CFG, MSM8996_SLAVE_A1NOC_MPU_CFG, MSM8996_SLAVE_A1NOC_SMMU_CFG, MSM8996_SLAVE_PCIE_2_CFG, MSM8996_SLAVE_CNOC_MNOC_CFG, MSM8996_SLAVE_QDSS_RBCPR_APU_CFG, MSM8996_SLAVE_PMIC_ARB, MSM8996_SLAVE_IMEM_CFG, MSM8996_SLAVE_A1NOC_CFG, MSM8996_SLAVE_SSC_CFG, MSM8996_SLAVE_TCSR, MSM8996_SLAVE_LPASS_SMMU_CFG, MSM8996_SLAVE_DCC_CFG);
DEFINE_QNODE(mas_snoc_pnoc, MSM8996_SNOC_PNOC_MAS, 0, 1, 8, QCOM_QOS_MODE_BYPASS, 0, 44, -1, 9, MSM8996_SLAVE_BLSP_1, MSM8996_SLAVE_BLSP_2, MSM8996_SLAVE_USB_HS, MSM8996_SLAVE_SDCC_1, MSM8996_SLAVE_SDCC_2, MSM8996_SLAVE_SDCC_4, MSM8996_SLAVE_TSIF, MSM8996_SLAVE_PDM, MSM8996_SLAVE_AHB2PHY);
DEFINE_QNODE(mas_snoc_vmem, MSM8996_MASTER_SNOC_VMEM, 0, 1, 32, QCOM_QOS_MODE_BYPASS, 1, 114, -1, 1, MSM8996_SLAVE_VMEM_CFG);
DEFINE_QNODE(mas_tsif, MSM8996_MASTER_TSIF, 0, 1, 4, QCOM_QOS_MODE_BYPASS, 0, 37, -1, 1, MSM8996_PNOC_A1NOC_SLV);
DEFINE_QNODE(mas_ufs, MSM8996_MASTER_UFS, 2, 1, 8, QCOM_QOS_MODE_FIXED, 1, 68, -1, 1, MSM8996_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_usb3, MSM8996_MASTER_USB3, 3, 1, 8, QCOM_QOS_MODE_FIXED, 1, 32, -1, 1, MSM8996_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_usb_hs, MSM8996_MASTER_USB_HS, 0, 1, 8, QCOM_QOS_MODE_BYPASS, 0, 42, -1, 1, MSM8996_PNOC_A1NOC_SLV);
DEFINE_QNODE(mas_venus, MSM8996_MASTER_VIDEO_P0, 3, 2, 32, QCOM_QOS_MODE_BYPASS, 1, 9, -1, 1, MSM8996_MNOC_BIMC_SLV);
DEFINE_QNODE(mas_venus_vmem, MSM8996_MASTER_VIDEO_P0_OCMEM, 0, 1, 32, QCOM_QOS_MODE_BYPASS, 1, 121, -1, 1, MSM8996_SLAVE_VMEM_CFG);
DEFINE_QNODE(mas_vfe, MSM8996_MASTER_VFE, 6, 1, 32, QCOM_QOS_MODE_BYPASS, 1, 11, -1, 1, MSM8996_MNOC_BIMC_SLV);
DEFINE_QNODE(slv_a0noc_cfg, MSM8996_SLAVE_A0NOC_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 144, 0, 0);
DEFINE_QNODE(slv_a0noc_mpu_cfg, MSM8996_SLAVE_A0NOC_MPU_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 145, 0, 0);
DEFINE_QNODE(slv_a0noc_smmu_cfg, MSM8996_SLAVE_A0NOC_SMMU_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 146, 0, 0);
DEFINE_QNODE(slv_a0noc_snoc, MSM8996_A0NOC_SNOC_SLV, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 141, 1, MSM8996_A0NOC_SNOC_MAS);
DEFINE_QNODE(slv_a1noc_cfg, MSM8996_SLAVE_A1NOC_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 147, 0, 0);
DEFINE_QNODE(slv_a1noc_mpu_cfg, MSM8996_SLAVE_A1NOC_MPU_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 148, 0, 0);
DEFINE_QNODE(slv_a1noc_smmu_cfg, MSM8996_SLAVE_A1NOC_SMMU_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 149, 0, 0);
DEFINE_QNODE(slv_a1noc_snoc, MSM8996_A1NOC_SNOC_SLV, 0, 1, 8, QCOM_QOS_MODE_FIXED, 0, -1, 142, 1, MSM8996_A1NOC_SNOC_MAS);
DEFINE_QNODE(slv_a2noc_cfg, MSM8996_SLAVE_A2NOC_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 150, 0, 0);
DEFINE_QNODE(slv_a2noc_mpu_cfg, MSM8996_SLAVE_A2NOC_MPU_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 151, 0, 0);
DEFINE_QNODE(slv_a2noc_smmu_cfg, MSM8996_SLAVE_A2NOC_SMMU_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 152, 0, 0);
DEFINE_QNODE(slv_a2noc_snoc, MSM8996_A2NOC_SNOC_SLV, 0, 1, 8, QCOM_QOS_MODE_FIXED, 0, -1, 143, 1, MSM8996_A2NOC_SNOC_MAS);
DEFINE_QNODE(slv_ahb2phy, MSM8996_SLAVE_AHB2PHY, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 153, 0, 0);
DEFINE_QNODE(slv_bimc_cfg, MSM8996_SLAVE_BIMC_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 56, 0, 0);
DEFINE_QNODE(slv_bimc_snoc_0, MSM8996_BIMC_SNOC_SLV, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 2, 1, MSM8996_BIMC_SNOC_MAS);
DEFINE_QNODE(slv_bimc_snoc_1, MSM8996_BIMC_SNOC_1_SLV, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 138, 1, MSM8996_BIMC_SNOC_1_MAS);
DEFINE_QNODE(slv_blsp_1, MSM8996_SLAVE_BLSP_1, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 39, 0, 0);
DEFINE_QNODE(slv_blsp_2, MSM8996_SLAVE_BLSP_2, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 37, 0, 0);
DEFINE_QNODE(slv_camera_cfg, MSM8996_SLAVE_CAMERA_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 3, 0, 0);
DEFINE_QNODE(slv_camera_throttle_cfg, MSM8996_SLAVE_CAMERA_THROTTLE_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 154, 0, 0);
DEFINE_QNODE(slv_clk_ctl, MSM8996_SLAVE_CLK_CTL, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 47, 0, 0);
DEFINE_QNODE(slv_cnoc_a1noc, MSM8996_CNOC_SNOC_SLV, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 75, 1, MSM8996_CNOC_A1NOC_MAS);
DEFINE_QNODE(slv_cnoc_mnoc_cfg, MSM8996_SLAVE_CNOC_MNOC_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 66, 1, MSM8996_MASTER_CNOC_MNOC_CFG);
DEFINE_QNODE(slv_cnoc_mnoc_mmss_cfg, MSM8996_SLAVE_CNOC_MNOC_MMSS_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 58, 1, MSM8996_MASTER_CNOC_MNOC_MMSS_CFG);
DEFINE_QNODE(slv_cpr_apu_cfg, MSM8996_SLAVE_QDSS_RBCPR_APU_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 168, 0, 0);
DEFINE_QNODE(slv_cpr_cfg, MSM8996_SLAVE_CPR_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 6, 0, 0);
DEFINE_QNODE(slv_crypto0_cfg, MSM8996_SLAVE_CRYPTO_0_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 52, 0, 0);
DEFINE_QNODE(slv_dcc_cfg, MSM8996_SLAVE_DCC_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 155, 0, 0);
DEFINE_QNODE(slv_display_cfg, MSM8996_SLAVE_DISPLAY_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 4, 0, 0);
DEFINE_QNODE(slv_display_throttle_cfg, MSM8996_SLAVE_DISPLAY_THROTTLE_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 156, 0, 0);
DEFINE_QNODE(slv_dsa_cfg, MSM8996_SLAVE_DSA_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 157, 0, 0);
DEFINE_QNODE(slv_dsa_mpu_cfg, MSM8996_SLAVE_DSA_MPU_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 158, 0, 0);
DEFINE_QNODE(slv_ebi1_phy_cfg, MSM8996_SLAVE_EBI1_PHY_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 73, 0, 0);
DEFINE_QNODE(slv_ebi, MSM8996_SLAVE_EBI_CH0, 0, 2, 8, QCOM_QOS_MODE_FIXED, 0, -1, 0, 0, 0);
DEFINE_QNODE(slv_hmss_l3, MSM8996_SLAVE_HMSS_L3, 0, 1, 8, QCOM_QOS_MODE_FIXED, 0, -1, 160, 0, 0);
DEFINE_QNODE(slv_hmss, MSM8996_SLAVE_APPSS, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, -1, 20, 0, 0);
DEFINE_QNODE(slv_imem_cfg, MSM8996_SLAVE_IMEM_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 54, 0, 0);
DEFINE_QNODE(slv_imem, MSM8996_SLAVE_OCIMEM, 0, 1, 16, QCOM_QOS_MODE_FIXED, 0, -1, 26, 0, 0);
DEFINE_QNODE(slv_lpass, MSM8996_SLAVE_LPASS, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, -1, 21, 0, 0);
DEFINE_QNODE(slv_lpass_smmu_cfg, MSM8996_SLAVE_LPASS_SMMU_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 161, 0, 0);
DEFINE_QNODE(slv_message_ram, MSM8996_SLAVE_MESSAGE_RAM, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 55, 0, 0);
DEFINE_QNODE(slv_misc_cfg, MSM8996_SLAVE_MISC_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 8, 0, 0);
DEFINE_QNODE(slv_mmagic_cfg, MSM8996_SLAVE_MMAGIC_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 162, 0, 0);
DEFINE_QNODE(slv_mnoc_bimc, MSM8996_MNOC_BIMC_SLV, 0, 2, 32, QCOM_QOS_MODE_FIXED, 1, -1, 16, 1, MSM8996_MNOC_BIMC_MAS);
DEFINE_QNODE(slv_mnoc_clocks_cfg, MSM8996_SLAVE_MMSS_CLK_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 12, 0, 0);
DEFINE_QNODE(slv_mnoc_mpu_cfg, MSM8996_SLAVE_MNOC_MPU_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 14, 0, 0);
DEFINE_QNODE(slv_mpm, MSM8996_SLAVE_MPM, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 62, 0, 0);
DEFINE_QNODE(slv_oxili_cfg, MSM8996_SLAVE_GRAPHICS_3D_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 11, 0, 0);
DEFINE_QNODE(slv_pcie_0_cfg, MSM8996_SLAVE_PCIE_0_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 88, 0, 0);
DEFINE_QNODE(slv_pcie_0, MSM8996_SLAVE_PCIE_0, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, -1, 84, 0, 0);
DEFINE_QNODE(slv_pcie_1_cfg, MSM8996_SLAVE_PCIE_1_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 89, 0, 0);
DEFINE_QNODE(slv_pcie_1, MSM8996_SLAVE_PCIE_1, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, -1, 85, 0, 0);
DEFINE_QNODE(slv_pcie20_ahb2phy, MSM8996_SLAVE_PCIE20_AHB2PHY, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 163, 0, 0);
DEFINE_QNODE(slv_pcie_2_cfg, MSM8996_SLAVE_PCIE_2_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 165, 0, 0);
DEFINE_QNODE(slv_pcie_2, MSM8996_SLAVE_PCIE_2, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, -1, 164, 0, 0);
DEFINE_QNODE(slv_pdm, MSM8996_SLAVE_PDM, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 41, 0, 0);
DEFINE_QNODE(slv_pimem_cfg, MSM8996_SLAVE_PIMEM_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 167, 0, 0);
DEFINE_QNODE(slv_pimem, MSM8996_SLAVE_PIMEM, 0, 1, 16, QCOM_QOS_MODE_FIXED, 0, -1, 166, 0, 0);
DEFINE_QNODE(slv_pmic_arb, MSM8996_SLAVE_PMIC_ARB, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 59, 0, 0);
DEFINE_QNODE(slv_pnoc_a1noc, MSM8996_PNOC_A1NOC_SLV, 0, 1, 8, QCOM_QOS_MODE_FIXED, 0, -1, 139, 1, MSM8996_PNOC_A1NOC_MAS);
DEFINE_QNODE(slv_prng, MSM8996_SLAVE_PRNG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 44, 0, 0);
DEFINE_QNODE(slv_qdss_cfg, MSM8996_SLAVE_QDSS_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 63, 0, 0);
DEFINE_QNODE(slv_qdss_stm, MSM8996_SLAVE_QDSS_STM, 0, 1, 16, QCOM_QOS_MODE_FIXED, 0, -1, 30, 0, 0);
DEFINE_QNODE(slv_rbcpr_cx, MSM8996_SLAVE_RBCPR_CX, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 169, 0, 0);
DEFINE_QNODE(slv_rbcpr_mx, MSM8996_SLAVE_RBCPR_MX, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 170, 0, 0);
DEFINE_QNODE(slv_sdcc_1, MSM8996_SLAVE_SDCC_1, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 31, 0, 0);
DEFINE_QNODE(slv_sdcc_2, MSM8996_SLAVE_SDCC_2, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 33, 0, 0);
DEFINE_QNODE(slv_sdcc_4, MSM8996_SLAVE_SDCC_4, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 34, 0, 0);
DEFINE_QNODE(slv_smmu_cpp_cfg, MSM8996_SLAVE_SMMU_CPP_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 171, 0, 0);
DEFINE_QNODE(slv_smmu_jpeg_cfg, MSM8996_SLAVE_SMMU_JPEG_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 172, 0, 0);
DEFINE_QNODE(slv_smmu_mdp_cfg, MSM8996_SLAVE_SMMU_MDP_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 173, 0, 0);
DEFINE_QNODE(slv_smmu_rot_cfg, MSM8996_SLAVE_SMMU_ROTATOR_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 174, 0, 0);
DEFINE_QNODE(slv_smmu_venus_cfg, MSM8996_SLAVE_SMMU_VENUS_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 175, 0, 0);
DEFINE_QNODE(slv_smmu_vfe_cfg, MSM8996_SLAVE_SMMU_VFE_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 176, 0, 0);
DEFINE_QNODE(slv_snoc_bimc, MSM8996_SNOC_BIMC_SLV, 0, 2, 32, QCOM_QOS_MODE_FIXED, 0, -1, 24, 1, MSM8996_SNOC_BIMC_MAS);
DEFINE_QNODE(slv_snoc_cfg, MSM8996_SLAVE_SNOC_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 70, 0, 0);
DEFINE_QNODE(slv_snoc_cnoc, MSM8996_SNOC_CNOC_SLV, 0, 1, 16, QCOM_QOS_MODE_FIXED, 0, -1, 25, 1, MSM8996_SNOC_CNOC_MAS);
DEFINE_QNODE(slv_snoc_mpu_cfg, MSM8996_SLAVE_SNOC_MPU_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 67, 0, 0);
DEFINE_QNODE(slv_snoc_pnoc, MSM8996_SNOC_PNOC_SLV, 0, 1, 16, QCOM_QOS_MODE_FIXED, 0, -1, 28, 1, MSM8996_SNOC_PNOC_MAS);
DEFINE_QNODE(slv_snoc_vmem, MSM8996_SLAVE_SNOC_VMEM, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, -1, 140, 1, MSM8996_MASTER_SNOC_VMEM);
DEFINE_QNODE(slv_srvc_mnoc, MSM8996_SLAVE_SERVICE_MNOC, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 17, 0, 0);
DEFINE_QNODE(slv_srvc_snoc, MSM8996_SLAVE_SERVICE_SNOC, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, -1, 29, 0, 0);
DEFINE_QNODE(slv_ssc_cfg, MSM8996_SLAVE_SSC_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 177, 0, 0);
DEFINE_QNODE(slv_tcsr, MSM8996_SLAVE_TCSR, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 50, 0, 0);
DEFINE_QNODE(slv_tlmm, MSM8996_SLAVE_TLMM, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 51, 0, 0);
DEFINE_QNODE(slv_tsif, MSM8996_SLAVE_TSIF, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 35, 0, 0);
DEFINE_QNODE(slv_ufs_cfg, MSM8996_SLAVE_UFS_CFG, 0, 1, 4, QCOM_QOS_MODE_FIXED, 1, -1, 92, 0, 0);
DEFINE_QNODE(slv_usb3, MSM8996_SLAVE_USB3, 0, 1, 16, QCOM_QOS_MODE_FIXED, 1, -1, 22, 0, 0);
DEFINE_QNODE(slv_usb_hs, MSM8996_SLAVE_USB_HS, 0, 1, 4, QCOM_QOS_MODE_FIXED, 0, -1, 40, 0, 0);
DEFINE_QNODE(slv_venus_cfg, MSM8996_SLAVE_VENUS_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 10, 0, 0);
DEFINE_QNODE(slv_venus_throttle_cfg, MSM8996_SLAVE_VENUS_THROTTLE_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 178, 0, 0);
DEFINE_QNODE(slv_vmem_cfg, MSM8996_SLAVE_VMEM_CFG, 0, 1, 8, QCOM_QOS_MODE_FIXED, 1, -1, 180, 0, 0);
DEFINE_QNODE(slv_vmem, MSM8996_SLAVE_VMEM, 0, 1, 32, QCOM_QOS_MODE_FIXED, 1, -1, 179, 0, 0);

static struct qcom_icc_node *msm8996_snoc_nodes[] = {
	[A0NOC_SNOC_MAS] = &mas_a0noc_snoc,
	[A0NOC_SNOC_SLV] = &slv_a0noc_snoc,
	[A1NOC_SNOC_MAS] = &mas_a1noc_snoc,
	[A1NOC_SNOC_SLV] = &slv_a1noc_snoc,
	[A2NOC_SNOC_MAS] = &mas_a2noc_snoc,
	[A2NOC_SNOC_SLV] = &slv_a2noc_snoc,
	[BIMC_SNOC_1_MAS] = &mas_bimc_snoc_1,
	[BIMC_SNOC_MAS] = &mas_bimc_snoc_0,
	[MASTER_HMSS] = &mas_hmss,
	[MASTER_QDSS_BAM] = &mas_qdss_bam,
	[MASTER_QDSS_ETR] = &mas_qdss_etr,
	[MASTER_SNOC_CFG] = &mas_snoc_cfg,
	[SLAVE_APPSS] = &slv_hmss,
	[SLAVE_LPASS] = &slv_lpass,
	[SLAVE_OCIMEM] = &slv_imem,
	[SLAVE_PCIE_0] = &slv_pcie_0,
	[SLAVE_PCIE_1] = &slv_pcie_1,
	[SLAVE_PCIE_2] = &slv_pcie_2,
	[SLAVE_PIMEM] = &slv_pimem,
	[SLAVE_QDSS_STM] = &slv_qdss_stm,
	[SLAVE_SERVICE_SNOC] = &slv_srvc_snoc,
	[SLAVE_SNOC_VMEM] = &slv_snoc_vmem,
	[SLAVE_USB3] = &slv_usb3,
	[SNOC_BIMC_SLV] = &slv_snoc_bimc,
	[SNOC_CNOC_SLV] = &slv_snoc_cnoc,
	[SNOC_PNOC_SLV] = &slv_snoc_pnoc,
};

static struct qcom_icc_desc msm8996_snoc = {
	.nodes = msm8996_snoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_snoc_nodes),
};

static struct qcom_icc_node *msm8996_bimc_nodes[] = {
	[BIMC_SNOC_SLV] = &slv_bimc_snoc_0,
	[BIMC_SNOC_1_SLV] = &slv_bimc_snoc_1,
	[MASTER_AMPSS_M0] = &mas_apps_proc,
	[MASTER_GRAPHICS_3D] = &mas_oxili,
	[MNOC_BIMC_MAS] = &mas_mnoc_bimc,
	[SLAVE_EBI_CH0] = &slv_ebi,
	[SLAVE_HMSS_L3] = &slv_hmss_l3,
	[SNOC_BIMC_MAS] = &mas_snoc_bimc,
};

static struct qcom_icc_desc msm8996_bimc = {
	.nodes = msm8996_bimc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_bimc_nodes),
};

static struct qcom_icc_node *msm8996_pnoc_nodes[] = {
	[MASTER_BLSP_1] = &mas_blsp_1,
	[MASTER_BLSP_2] = &mas_blsp_2,
	[MASTER_SDCC_1] = &mas_sdcc_1,
	[MASTER_SDCC_2] = &mas_sdcc_2,
	[MASTER_SDCC_4] = &mas_sdcc_4,
	[MASTER_TSIF] = &mas_tsif,
	[MASTER_USB_HS] = &mas_usb_hs,
	[PNOC_A1NOC_SLV] = &slv_pnoc_a1noc,
	[SLAVE_AHB2PHY] = &slv_ahb2phy,
	[SLAVE_BLSP_1] = &slv_blsp_1,
	[SLAVE_BLSP_2] = &slv_blsp_2,
	[SLAVE_PDM] = &slv_pdm,
	[SLAVE_SDCC_1] = &slv_sdcc_1,
	[SLAVE_SDCC_2] = &slv_sdcc_2,
	[SLAVE_SDCC_4] = &slv_sdcc_4,
	[SLAVE_TSIF] = &slv_tsif,
	[SLAVE_USB_HS] = &slv_usb_hs,
	[SNOC_PNOC_MAS] = &mas_snoc_pnoc,
};

static struct qcom_icc_desc msm8996_pnoc = {
	.nodes = msm8996_pnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_pnoc_nodes),
};

static struct qcom_icc_node *msm8996_cnoc_nodes[] = {
	[CNOC_SNOC_SLV] = &slv_cnoc_a1noc,
	[MASTER_QDSS_DAP] = &mas_qdss_dap,
	[SLAVE_A0NOC_CFG] = &slv_a0noc_cfg,
	[SLAVE_A0NOC_MPU_CFG] = &slv_a0noc_mpu_cfg,
	[SLAVE_A0NOC_SMMU_CFG] = &slv_a0noc_smmu_cfg,
	[SLAVE_A1NOC_CFG] = &slv_a1noc_cfg,
	[SLAVE_A1NOC_MPU_CFG] = &slv_a1noc_mpu_cfg,
	[SLAVE_A1NOC_SMMU_CFG] = &slv_a1noc_smmu_cfg,
	[SLAVE_A2NOC_CFG] = &slv_a2noc_cfg,
	[SLAVE_A2NOC_MPU_CFG] = &slv_a2noc_mpu_cfg,
	[SLAVE_A2NOC_SMMU_CFG] = &slv_a2noc_smmu_cfg,
	[SLAVE_BIMC_CFG] = &slv_bimc_cfg,
	[SLAVE_CLK_CTL] = &slv_clk_ctl,
	[SLAVE_CNOC_MNOC_CFG] = &slv_cnoc_mnoc_cfg,
	[SLAVE_CNOC_MNOC_MMSS_CFG] &slv_cnoc_mnoc_mmss_cfg,
	[SLAVE_CRYPTO_0_CFG] = &slv_crypto0_cfg,
	[SLAVE_DCC_CFG] = &slv_dcc_cfg,
	[SLAVE_EBI1_PHY_CFG] = &slv_ebi1_phy_cfg,
	[SLAVE_IMEM_CFG] = &slv_imem_cfg,
	[SLAVE_LPASS_SMMU_CFG] = &slv_lpass_smmu_cfg,
	[SLAVE_MESSAGE_RAM] = &slv_message_ram,
	[SLAVE_MPM] = &slv_mpm,
	[SLAVE_PCIE_0_CFG] = &slv_pcie_0_cfg,
	[SLAVE_PCIE_1_CFG] = &slv_pcie_1_cfg,
	[SLAVE_PCIE20_AHB2PHY] = &slv_pcie20_ahb2phy,
	[SLAVE_PCIE_2_CFG] = &slv_pcie_2_cfg,
	[SLAVE_PIMEM_CFG] = &slv_pimem_cfg,
	[SLAVE_PMIC_ARB] = &slv_pmic_arb,
	[SLAVE_PRNG] = &slv_prng,
	[SLAVE_QDSS_CFG] = &slv_qdss_cfg,
	[SLAVE_QDSS_RBCPR_APU_CFG] = &slv_cpr_apu_cfg,
	[SLAVE_RBCPR_CX] = &slv_rbcpr_cx,
	[SLAVE_RBCPR_MX] = &slv_rbcpr_mx,
	[SLAVE_SNOC_CFG] = &slv_snoc_cfg,
	[SLAVE_SNOC_MPU_CFG] = &slv_snoc_mpu_cfg,
	[SLAVE_SSC_CFG] = &slv_ssc_cfg,
	[SLAVE_TCSR] = &slv_tcsr,
	[SLAVE_TLMM] = &slv_tlmm,
	[SLAVE_UFS_CFG] = &slv_ufs_cfg,
	[SNOC_CNOC_MAS] = &mas_snoc_cnoc,
};

static struct qcom_icc_desc msm8996_cnoc = {
	.nodes = msm8996_cnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_cnoc_nodes),
};

static struct qcom_icc_node *msm8996_mnoc_nodes[] = {
	[MASTER_CNOC_MNOC_CFG] = &mas_cnoc_mnoc_cfg,
	[MASTER_CNOC_MNOC_MMSS_CFG] = &mas_cnoc_mnoc_mmss_cfg,
	[MASTER_CPP] = &mas_cpp,
	[MASTER_JPEG] = &mas_jpeg,
	[MASTER_MDP_PORT0] = &mas_mdp_p0,
	[MASTER_MDP_PORT1] = &mas_mdp_p1,
	[MASTER_ROTATOR] = &mas_rotator,
	[MASTER_SNOC_VMEM] = &mas_snoc_vmem,
	[MASTER_VFE] = &mas_vfe,
	[MASTER_VIDEO_P0] = &mas_venus,
	[MASTER_VIDEO_P0_OCMEM] = &mas_venus_vmem,
	[MNOC_BIMC_SLV] = &slv_mnoc_bimc,
	[SLAVE_CAMERA_CFG] = &slv_camera_cfg,
	[SLAVE_CAMERA_THROTTLE_CFG] = &slv_camera_throttle_cfg,
	[SLAVE_CPR_CFG] = &slv_cpr_cfg,
	[SLAVE_DISPLAY_CFG] = &slv_display_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &slv_display_throttle_cfg,
	[SLAVE_DSA_CFG] = &slv_dsa_cfg,
	[SLAVE_DSA_MPU_CFG] = &slv_dsa_mpu_cfg,
	[SLAVE_GRAPHICS_3D_CFG] = &slv_oxili_cfg,
	[SLAVE_MISC_CFG] = &slv_misc_cfg,
	[SLAVE_MMAGIC_CFG] = &slv_mmagic_cfg,
	[SLAVE_MMSS_CLK_CFG] = &slv_mnoc_clocks_cfg,
	[SLAVE_MNOC_MPU_CFG] = &slv_mnoc_mpu_cfg,
	[SLAVE_SERVICE_MNOC] = &slv_srvc_mnoc,
	[SLAVE_SMMU_CPP_CFG] = &slv_smmu_cpp_cfg,
	[SLAVE_SMMU_JPEG_CFG] = &slv_smmu_jpeg_cfg,
	[SLAVE_SMMU_MDP_CFG] = &slv_smmu_mdp_cfg,
	[SLAVE_SMMU_ROTATOR_CFG] = &slv_smmu_rot_cfg,
	[SLAVE_SMMU_VENUS_CFG] = &slv_smmu_venus_cfg,
	[SLAVE_SMMU_VFE_CFG] = &slv_smmu_vfe_cfg,
	[SLAVE_VENUS_CFG] = &slv_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &slv_venus_throttle_cfg,
	[SLAVE_VMEM_CFG] = &slv_vmem_cfg,
	[SLAVE_VMEM] = &slv_vmem,
};

static struct qcom_icc_desc msm8996_mnoc = {
	.nodes = msm8996_mnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_mnoc_nodes),
};

static struct qcom_icc_node *msm8996_a0noc_nodes[] = {
	[MASTER_PCIE] = &mas_pcie_0,
	[MASTER_PCIE_1] = &mas_pcie_1,
	[MASTER_PCIE_2] = &mas_pcie_2,
};

static struct qcom_icc_desc msm8996_a0noc = {
	.nodes = msm8996_a0noc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_a0noc_nodes),
};

static struct qcom_icc_node *msm8996_a1noc_nodes[] = {
	[CNOC_A1NOC_MAS] = &mas_cnoc_a1noc,
	[MASTER_CRYPTO_CORE0] = &mas_crypto_c0,
	[PNOC_A1NOC_MAS] = &mas_pnoc_a1noc,
};

static struct qcom_icc_desc msm8996_a1noc = {
	.nodes = msm8996_a1noc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_a1noc_nodes),
};

static struct qcom_icc_node *msm8996_a2noc_nodes[] = {
	[MASTER_IPA] = &mas_ipa,
	[MASTER_UFS] = &mas_ufs,
	[MASTER_USB3] = &mas_usb3,
};

static struct qcom_icc_desc msm8996_a2noc = {
	.nodes = msm8996_a2noc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_a2noc_nodes),
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
	{ .compatible = "qcom,msm8996-bimc", .data = &msm8996_bimc },
	{ .compatible = "qcom,msm8996-cnoc", .data = &msm8996_cnoc },
	{ .compatible = "qcom,msm8996-snoc", .data = &msm8996_snoc },
	{ .compatible = "qcom,msm8996-a0noc", .data = &msm8996_a0noc },
	{ .compatible = "qcom,msm8996-a1noc", .data = &msm8996_a1noc },
	{ .compatible = "qcom,msm8996-a2noc", .data = &msm8996_a2noc },
	{ .compatible = "qcom,msm8996-mmnoc", .data = &msm8996_mnoc },
	{ .compatible = "qcom,msm8996-pnoc", .data = &msm8996_pnoc },
	{ },
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-msm8996",
		.of_match_table = qnoc_of_match,
	},
};
module_platform_driver(qnoc_driver);
MODULE_AUTHOR("Georgi Djakov <georgi.djakov@linaro.org>");
MODULE_DESCRIPTION("Qualcomm msm8996 NoC driver");
MODULE_LICENSE("GPL v2");
