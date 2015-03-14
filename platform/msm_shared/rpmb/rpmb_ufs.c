/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <ufs.h>
#include <ucs.h>
#include <upiu.h>
#include <rpmb.h>
#include <debug.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <arch/defines.h>

static struct rpmb_frame read_result_reg =
{
	.requestresponse[1] = READ_RESULT_FLAG,
};

int rpmb_read_ufs(struct ufs_dev *dev, uint32_t *req_buf, uint32_t blk_cnt, uint32_t *resp_buf, uint32_t *resp_len)
{
	// validate input parameters
	ASSERT(req_buf);
	ASSERT(resp_buf);
	ASSERT(resp_len);

	STACKBUF_DMA_ALIGN(cdb, sizeof(struct scsi_sec_protocol_cdb));
	struct scsi_req_build_type   req_upiu;
	struct scsi_sec_protocol_cdb *cdb_out_param, *cdb_in_param;
	uint32_t                     blks_remaining;
	uint32_t                     blks_to_transfer;
	uint64_t                     bytes_to_transfer;
	uint64_t                     max_size;
	blks_remaining    = blk_cnt;
	blks_to_transfer  = blks_remaining;
	bytes_to_transfer = blks_to_transfer * RPMB_FRAME_SIZE;

#ifdef DEBUG_RPMB
	dump_rpmb_frame((uint8_t *)req_buf, "request");
#endif

	// check if total bytes to transfer exceed max supported size
	max_size = dev->rpmb_rw_size * RPMB_FRAME_SIZE * blk_cnt;
	if (bytes_to_transfer > max_size)
	{
		dprintf(CRITICAL, "RPMB request transfer size %llu greater than max transfer size %llu\n", bytes_to_transfer, max_size);
		return -UFS_FAILURE;
	}
#ifdef DEBUG_RPMB
	dprintf(SPEW, "rpmb_read: req_buf: 0x%x blk_count: 0x%x\n", *req_buf, blk_cnt);
	dprintf(INFO, "rpmb_read: bytes_to_transfer: 0x%x blks_to_transfer: 0x%x\n",
                   bytes_to_transfer, blks_to_transfer);
#endif
	// send the request
	cdb_out_param = (struct scsi_sec_protocol_cdb*) cdb;
	memset(cdb_out_param, 0, sizeof(struct scsi_sec_protocol_cdb));

	cdb_out_param->opcode                = SCSI_CMD_SECPROT_OUT;
	cdb_out_param->cdb1                  = SCSI_SEC_PROT;
	cdb_out_param->sec_protocol_specific = BE16(SCSI_SEC_UFS_PROT_ID);
	cdb_out_param->alloc_tlen            = BE32(bytes_to_transfer);

	// Flush CDB to memory
	dsb();
	arch_clean_invalidate_cache_range((addr_t) cdb_out_param, sizeof(struct scsi_sec_protocol_cdb));

	memset(&req_upiu, 0, sizeof(struct scsi_req_build_type));

	req_upiu.cdb              = (addr_t) cdb_out_param;
	req_upiu.data_buffer_addr = (addr_t) req_buf;
	req_upiu.data_len         = bytes_to_transfer;
	req_upiu.flags            = UPIU_FLAGS_WRITE;
	req_upiu.lun              = UFS_WLUN_RPMB;
	req_upiu.dd               = UTRD_TARGET_TO_SYSTEM;

#ifdef DEBUG_RPMB
	dprintf(INFO, "Sending RPMB Read request\n");
#endif
	if (ucs_do_scsi_cmd(dev, &req_upiu))
	{
		dprintf(CRITICAL, "%s:%d ucs_do_scsi_rpmb_read: failed\n", __func__, __LINE__);
		return -UFS_FAILURE;
	}
#ifdef DEBUG_RPMB
	dprintf(INFO, "Sending RPMB Read request complete\n");
#endif
	// read the response
	cdb_in_param = (struct scsi_sec_protocol_cdb*) cdb;
	memset(cdb_in_param, 0, sizeof(struct scsi_sec_protocol_cdb));

	cdb_in_param->opcode                = SCSI_CMD_SECPROT_IN;
	cdb_in_param->cdb1                  = SCSI_SEC_PROT;
	cdb_in_param->sec_protocol_specific = BE16(SCSI_SEC_UFS_PROT_ID);
	cdb_in_param->alloc_tlen            = BE32(bytes_to_transfer);

	// Flush CDB to memory
	dsb();
	arch_clean_invalidate_cache_range((addr_t) cdb_in_param, sizeof(struct scsi_sec_protocol_cdb));

	memset(&req_upiu, 0, sizeof(struct scsi_req_build_type));

	req_upiu.cdb              = (addr_t) cdb_in_param;
	req_upiu.data_buffer_addr = (addr_t) resp_buf;
	req_upiu.data_len         = bytes_to_transfer;
	req_upiu.flags            = UPIU_FLAGS_READ;
	req_upiu.lun              = UFS_WLUN_RPMB;
	req_upiu.dd               = UTRD_SYSTEM_TO_TARGET;

#ifdef DEBUG_RPMB
	dprintf(INFO, "Sending RPMB Read response\n");
#endif
	if (ucs_do_scsi_cmd(dev, &req_upiu))
	{
		dprintf(CRITICAL, "%s:%d ucs_do_scsi_rpmb_read: failed\n", __func__, __LINE__);
		return -UFS_FAILURE;
	}
#ifdef DEBUG_RPMB
	dprintf(SPEW, "Sending RPMB Read response complete\n");
	dump_rpmb_frame((uint8_t *)resp_buf, "response");
#endif
	*resp_len = bytes_to_transfer;
	return UFS_SUCCESS;
}

int rpmb_write_ufs(struct ufs_dev *dev, uint32_t *req_buf, uint32_t blk_cnt, uint32_t *resp_buf, uint32_t *resp_len)
{
	// validate input parameters
	ASSERT(req_buf);
	ASSERT(resp_buf);
	ASSERT(resp_len);

	STACKBUF_DMA_ALIGN(cdb, sizeof(struct scsi_sec_protocol_cdb));
	struct scsi_req_build_type   req_upiu;
	struct scsi_sec_protocol_cdb *cdb_out_param, *cdb_in_param;
	uint32_t                     blks_remaining;
	uint32_t                     blks_to_transfer;
	uint64_t                     bytes_to_transfer;
	uint64_t                     max_size;
	uint32_t                     result_frame_bytes = RPMB_FRAME_SIZE;
	uint32_t                     i;

	blks_remaining    = blk_cnt;
	blks_to_transfer  = blks_remaining;
	bytes_to_transfer = blks_to_transfer * RPMB_FRAME_SIZE;

	// check if total bytes to transfer exceed max supported size
	max_size = dev->rpmb_rw_size * RPMB_FRAME_SIZE * blk_cnt;
	if (bytes_to_transfer > max_size)
	{
		dprintf(CRITICAL, "RPMB request transfer size %llu greater than max transfer size %llu\n", bytes_to_transfer, max_size);
		return -UFS_FAILURE;
	}
#ifdef DEBUG_RPMB
	dprintf(INFO, "%s: req_buf: 0x%x blk_count: 0x%x\n", __func__,*req_buf, blk_cnt);
	dprintf(INFO, "%s: bytes_to_transfer: 0x%x blks_to_transfer: 0x%x\n", __func__
                   bytes_to_transfer, blk_cnt);
	dump_rpmb_frame((uint8_t *)req_buf, "request");
#endif

	for (i = 0; i < blk_cnt; i++)
	{
		// send the request
		cdb_out_param = (struct scsi_sec_protocol_cdb*) cdb;
		memset(cdb_out_param, 0, sizeof(struct scsi_sec_protocol_cdb));

		cdb_out_param->opcode                = SCSI_CMD_SECPROT_OUT;
		cdb_out_param->cdb1                  = SCSI_SEC_PROT;
		cdb_out_param->sec_protocol_specific = BE16(SCSI_SEC_UFS_PROT_ID);
		cdb_out_param->alloc_tlen            = BE32(RPMB_FRAME_SIZE);

		// Flush CDB to memory
		dsb();
		arch_clean_invalidate_cache_range((addr_t) cdb_out_param, sizeof(struct scsi_sec_protocol_cdb));

		memset(&req_upiu, 0, sizeof(struct scsi_req_build_type));

		req_upiu.cdb              = (addr_t) cdb_out_param;
		req_upiu.data_buffer_addr = (addr_t) req_buf;
		req_upiu.data_len         = RPMB_FRAME_SIZE;
		req_upiu.flags            = UPIU_FLAGS_WRITE;
		req_upiu.lun              = UFS_WLUN_RPMB;
		req_upiu.dd               = UTRD_TARGET_TO_SYSTEM;

#ifdef DEBUG_RPMB
		dprintf(INFO, "Sending RPMB write request: Start\n");
#endif
		if (ucs_do_scsi_cmd(dev, &req_upiu))
		{
			dprintf(CRITICAL, "%s:%d ucs_do_scsi_rpmb_read: failed\n", __func__, __LINE__);
			return -UFS_FAILURE;
		}
#ifdef DEBUG_RPMB
		dprintf(INFO, "Sending RPMB write request: Done\n");
#endif
		// Result Read
		cdb_in_param = (struct scsi_sec_protocol_cdb*) cdb;
		memset(cdb_in_param, 0, sizeof(struct scsi_sec_protocol_cdb));

		cdb_in_param->opcode                = SCSI_CMD_SECPROT_OUT;
		cdb_in_param->cdb1                  = SCSI_SEC_PROT;
		cdb_in_param->sec_protocol_specific = BE16(SCSI_SEC_UFS_PROT_ID);
		cdb_in_param->alloc_tlen            = BE32(RPMB_FRAME_SIZE);

		// Flush CDB to memory
		dsb();
		arch_clean_invalidate_cache_range((addr_t) cdb_in_param, sizeof(struct scsi_sec_protocol_cdb));

		memset(&req_upiu, 0, sizeof(struct scsi_req_build_type));

		req_upiu.cdb              = (addr_t) cdb_in_param;
		req_upiu.data_buffer_addr = (addr_t) &read_result_reg ;
		req_upiu.data_len         = result_frame_bytes;
		req_upiu.flags            = UPIU_FLAGS_WRITE;
		req_upiu.lun              = UFS_WLUN_RPMB;
		req_upiu.dd               = UTRD_TARGET_TO_SYSTEM;

#ifdef DEBUG_RPMB
		dprintf(INFO, "Sending RPMB Result Read Register: Start \n");
#endif
		if (ucs_do_scsi_cmd(dev, &req_upiu))
		{
			dprintf(CRITICAL, "%s:%d ucs_do_scsi_rpmb_read: failed\n", __func__, __LINE__);
			return -UFS_FAILURE;
		}
#ifdef DEBUG_RPMB
		dprintf(SPEW, "Sending RPMB Result Read Register: Done\n");
#endif

		// Retrieve verification result
		cdb_in_param = (struct scsi_sec_protocol_cdb*) cdb;
		memset(cdb_in_param, 0, sizeof(struct scsi_sec_protocol_cdb));

		cdb_in_param->opcode                = SCSI_CMD_SECPROT_IN;
		cdb_in_param->cdb1                  = SCSI_SEC_PROT;
		cdb_in_param->sec_protocol_specific = BE16(SCSI_SEC_UFS_PROT_ID);
		cdb_in_param->alloc_tlen            = BE32(RPMB_FRAME_SIZE);

		// Flush CDB to memory
		dsb();
		arch_clean_invalidate_cache_range((addr_t) cdb_in_param, sizeof(struct scsi_sec_protocol_cdb));

		memset(&req_upiu, 0, sizeof(struct scsi_req_build_type));

		req_upiu.cdb              = (addr_t) cdb_in_param;
		req_upiu.data_buffer_addr = (addr_t) resp_buf;
		req_upiu.data_len         = result_frame_bytes;
		req_upiu.flags            = UPIU_FLAGS_READ;
		req_upiu.lun              = UFS_WLUN_RPMB;
		req_upiu.dd               = UTRD_SYSTEM_TO_TARGET;

#ifdef DEBUG_RPMB
		dprintf(INFO, "Sending RPMB Response for Result Read Register : Start\n");
#endif
		if (ucs_do_scsi_cmd(dev, &req_upiu))
		{
			dprintf(CRITICAL, "%s:%d ucs_do_scsi_rpmb_read: failed\n", __func__, __LINE__);
			return -UFS_FAILURE;
		}
#ifdef DEBUG_RPMB
		dprintf(SPEW, "Sending RPMB Response for Result Read Register: Done\n");
		dump_rpmb_frame((uint8_t *)resp_buf, "response");
#endif

		req_buf = (uint32_t*) ((uint8_t*)req_buf + RPMB_BLK_SIZE);
	}

	*resp_len = RPMB_MIN_BLK_CNT * RPMB_BLK_SIZE;

	return 0;
}
