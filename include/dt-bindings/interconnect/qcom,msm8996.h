/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Qualcomm interconnect IDs
 *
 * Copyright (c) 2018, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_MSM8996_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_MSM8996_H

#define A0NOC_SNOC_MAS			0
#define A0NOC_SNOC_SLV			1
#define A1NOC_SNOC_MAS			2
#define A1NOC_SNOC_SLV			3
#define A2NOC_SNOC_MAS			4
#define A2NOC_SNOC_SLV			5
#define BIMC_SNOC_1_MAS			6
#define BIMC_SNOC_MAS			7
#define MASTER_HMSS			8
#define MASTER_QDSS_BAM			9
#define MASTER_QDSS_ETR			10
#define MASTER_SNOC_CFG			11
#define SLAVE_APPSS			12
#define SLAVE_LPASS			13
#define SLAVE_OCIMEM			14
#define SLAVE_PCIE_0			15
#define SLAVE_PCIE_1			16
#define SLAVE_PCIE_2			17
#define SLAVE_PIMEM			18
#define SLAVE_QDSS_STM			19
#define SLAVE_SERVICE_SNOC		20
#define SLAVE_SNOC_VMEM			21
#define SLAVE_USB3			22
#define SNOC_BIMC_SLV			23
#define SNOC_CNOC_SLV			24
#define SNOC_PNOC_SLV			25

#define BIMC_SNOC_SLV			0
#define BIMC_SNOC_1_SLV			1
#define MASTER_AMPSS_M0			2
#define MASTER_GRAPHICS_3D		3
#define MNOC_BIMC_MAS			4
#define SLAVE_EBI_CH0			5
#define SLAVE_HMSS_L3			6
#define SNOC_BIMC_MAS			7

#define MASTER_BLSP_1			0
#define MASTER_BLSP_2			1
#define MASTER_SDCC_1			2
#define MASTER_SDCC_2			3
#define MASTER_SDCC_4			4
#define MASTER_TSIF			5
#define MASTER_USB_HS			6
#define PNOC_A1NOC_SLV			7
#define SLAVE_AHB2PHY			8
#define SLAVE_BLSP_1			9
#define SLAVE_BLSP_2			10
#define SLAVE_PDM			11
#define SLAVE_SDCC_1			12
#define SLAVE_SDCC_2			13
#define SLAVE_SDCC_4			14
#define SLAVE_TSIF			15
#define SLAVE_USB_HS			16
#define SNOC_PNOC_MAS			17

#define CNOC_SNOC_SLV			0
#define MASTER_QDSS_DAP			1
#define SLAVE_A0NOC_CFG			2
#define SLAVE_A0NOC_MPU_CFG		3
#define SLAVE_A0NOC_SMMU_CFG		4
#define SLAVE_A1NOC_CFG			5
#define SLAVE_A1NOC_MPU_CFG		6
#define SLAVE_A1NOC_SMMU_CFG		7
#define SLAVE_A2NOC_CFG			8
#define SLAVE_A2NOC_MPU_CFG		9
#define SLAVE_A2NOC_SMMU_CFG		10
#define SLAVE_BIMC_CFG			11
#define SLAVE_CLK_CTL			12
#define SLAVE_CNOC_MNOC_CFG		13
#define SLAVE_CNOC_MNOC_MMSS_CFG	14
#define SLAVE_CRYPTO_0_CFG		15
#define SLAVE_DCC_CFG			16
#define SLAVE_EBI1_PHY_CFG		17
#define SLAVE_IMEM_CFG			18
#define SLAVE_LPASS_SMMU_CFG		19
#define SLAVE_MESSAGE_RAM		20
#define SLAVE_MPM			21
#define SLAVE_PCIE_0_CFG		22
#define SLAVE_PCIE_1_CFG		23
#define SLAVE_PCIE20_AHB2PHY		24
#define SLAVE_PCIE_2_CFG		25
#define SLAVE_PIMEM_CFG			26
#define SLAVE_PMIC_ARB			27
#define SLAVE_PRNG			28
#define SLAVE_QDSS_CFG			29
#define SLAVE_QDSS_RBCPR_APU_CFG	30
#define SLAVE_RBCPR_CX			31
#define SLAVE_RBCPR_MX			32
#define SLAVE_SNOC_CFG			33
#define SLAVE_SNOC_MPU_CFG		34
#define SLAVE_SSC_CFG			35
#define SLAVE_TCSR			36
#define SLAVE_TLMM			37
#define SLAVE_UFS_CFG			38
#define SNOC_CNOC_MAS			39

#define MASTER_CNOC_MNOC_CFG		0
#define MASTER_CNOC_MNOC_MMSS_CFG	1
#define MASTER_CPP			2
#define MASTER_JPEG			3
#define MASTER_MDP_PORT0		4
#define MASTER_MDP_PORT1		5
#define MASTER_ROTATOR			6
#define MASTER_SNOC_VMEM		7
#define MASTER_VFE			8
#define MASTER_VIDEO_P0			9
#define MASTER_VIDEO_P0_OCMEM		10
#define MNOC_BIMC_SLV			11
#define SLAVE_CAMERA_CFG		12
#define SLAVE_CAMERA_THROTTLE_CFG	13
#define SLAVE_CPR_CFG			14
#define SLAVE_DISPLAY_CFG		15
#define SLAVE_DISPLAY_THROTTLE_CFG	16
#define SLAVE_DSA_CFG			17
#define SLAVE_DSA_MPU_CFG		18
#define SLAVE_GRAPHICS_3D_CFG		19
#define SLAVE_MISC_CFG			20
#define SLAVE_MMAGIC_CFG		21
#define SLAVE_MMSS_CLK_CFG		22
#define SLAVE_MNOC_MPU_CFG		23
#define SLAVE_SERVICE_MNOC		24
#define SLAVE_SMMU_CPP_CFG		25
#define SLAVE_SMMU_JPEG_CFG		26
#define SLAVE_SMMU_MDP_CFG		27
#define SLAVE_SMMU_ROTATOR_CFG		28
#define SLAVE_SMMU_VENUS_CFG		29
#define SLAVE_SMMU_VFE_CFG		30
#define SLAVE_VENUS_CFG			31
#define SLAVE_VENUS_THROTTLE_CFG	32
#define SLAVE_VMEM_CFG			33
#define SLAVE_VMEM			34

#define MASTER_PCIE			0
#define MASTER_PCIE_1			1
#define MASTER_PCIE_2			2

#define CNOC_A1NOC_MAS			0
#define MASTER_CRYPTO_CORE0		1
#define PNOC_A1NOC_MAS			2

#define MASTER_IPA			0
#define MASTER_UFS			1
#define MASTER_USB3			2

#endif
