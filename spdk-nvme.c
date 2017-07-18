/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "nvme-ioctl.h"
#include "spdk-nvme.h"

#include "spdk/env.h"
#include "spdk/log.h"
#include "spdk/nvme.h"
#include "spdk/nvme_intel.h"
#include "spdk/nvmf_spec.h"
#include "spdk/pci_ids.h"

struct spdk_nvme_dev g_spdk_dev[NUM_MAX_NVMES] = {};

bool g_spdk_enabled = false;
unsigned int g_num_ctrlr = 0;
bool g_list_all = false;
static int outstanding_commands;

struct spdk_nvme_passthru_cmd {
	struct nvme_passthru_cmd	*cmd;
	bool				failed;
};

static bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{
	if (g_list_all == true) {
		return true;
	}

	if (strcmp(g_spdk_dev[g_num_ctrlr].dbdf, trid->traddr) == 0) {
		return true;
	}

	return false;
}

static void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
	unsigned int num_ns = spdk_nvme_ctrlr_get_num_ns(ctrlr);
	struct spdk_nvme_ns *ns = NULL;

	for (int i = 1; i <= num_ns; i ++) {
		ns = spdk_nvme_ctrlr_get_ns(ctrlr, i);
		if (ns != NULL && spdk_nvme_ns_is_active(ns) == true) {
			if (g_num_ctrlr == NUM_MAX_NVMES) {
				fprintf(stderr, "no resource to manage ctrlr %s\n", trid->traddr);
				return;
			}

			g_spdk_dev[g_num_ctrlr].ctrlr = ctrlr;
			g_spdk_dev[g_num_ctrlr].fd = socket(AF_UNIX, SOCK_RAW, 0);
			g_spdk_dev[g_num_ctrlr].ns_id = i;
			snprintf(g_spdk_dev[g_num_ctrlr].dbdf, PCI_DBDF_LEN, "%s", trid->traddr);

			g_num_ctrlr++;
		}
	}
}

static int
spdk_parse_args(int argc, char **argv)
{
	if (argc == 1) {
		if (strcmp(argv[0], "list") == 0) {
			g_list_all = true;
			return 0;
		} else {
			fprintf(stderr, "device information needed, example: 0000:07:00.0\n");
			return 1;
		}
	}

	snprintf(g_spdk_dev[g_num_ctrlr].dbdf, PCI_DBDF_LEN, "%s", argv[1]);

	return 0;
}

int
spdk_main(int argc, char **argv)
{
	int ret;
	struct spdk_env_opts opts;
	char spdk_conf[512], *str;
	char core_mask[16] = "0x1";
	FILE *spdkf;

	spdkf = fopen("spdk.conf", "r");
	if (!spdkf) {
		fprintf(stderr, "could not open spdk.conf\n");
		return 1;
	}

	spdk_env_opts_init(&opts);
	opts.name = "nvme_cli";
	opts.shm_id = -1;
	opts.mem_size = 512;

	while (fscanf(spdkf, "%s", spdk_conf) == 1) {
		str = strtok(spdk_conf, "=");

		if (strcmp(str, "spdk") == 0) {
			g_spdk_enabled = atoi(strtok(NULL, "="));
		}

		if (strcmp(str, "core_mask") == 0) {
			snprintf(core_mask, sizeof(core_mask), "%s", strtok(NULL, "="));
			opts.core_mask = core_mask;
		}

		if (strcmp(str, "shm_id") == 0) {
			opts.shm_id = atoi(strtok(NULL, "="));
		}

		if (strcmp(str, "mem_size") == 0) {
			opts.mem_size = atoi(strtok(NULL, "="));
		}
	}

	fclose(spdkf);

	if (g_spdk_enabled == false) {
		return 0;
	}

	ret = spdk_parse_args(argc, argv);
	if (ret != 0) {
		return 1;
	}

	spdk_env_init(&opts);

	if (spdk_nvme_probe(NULL, NULL, probe_cb, attach_cb, NULL) != 0) {
		fprintf(stderr, "spdk_nvme_probe() failed\n");
		return 1;
	}

	return 0;
}

static inline int
nvme_spdk_get_error_code(const struct spdk_nvme_cpl *cpl)
{
	return (cpl->status.sct << 8) | cpl->status.sc;
}

static void
nvme_spdk_get_admin_completion(void *cb_arg, const struct spdk_nvme_cpl *cpl)
{
	struct spdk_nvme_passthru_cmd *spdk_cmd = (struct spdk_nvme_passthru_cmd *)cb_arg;

	if (spdk_nvme_cpl_is_error(cpl)) {
		fprintf(stderr, "command error: SC %x SCT %x\n", cpl->status.sc, cpl->status.sct);

		spdk_cmd->cmd->result = nvme_spdk_get_error_code(cpl);

		spdk_cmd->failed = true;
	} else {
		spdk_cmd->cmd->result = cpl->cdw0;
	}

	outstanding_commands--;
}

struct spdk_nvme_ctrlr *
nvme_spdk_get_ctrlr_by_fd(unsigned int fd)
{
	for (int i = 0; i < g_num_ctrlr; i++) {
		if (g_spdk_dev[i].fd == fd) {
			return g_spdk_dev[i].ctrlr;
		}
	}

	return NULL;
}

int
nvme_spdk_submit_admin_passthru(unsigned int fd, struct nvme_passthru_cmd *cmd)
{
	int rc = 0;

	struct spdk_nvme_cmd *spdk_cmd = (struct spdk_nvme_cmd *)cmd;

	struct spdk_nvme_passthru_cmd spdk_nvme_cmd = {};

	void *contig_buffer = NULL;

	struct spdk_nvme_ctrlr *ctrlr = nvme_spdk_get_ctrlr_by_fd(fd);

	enum spdk_nvme_data_transfer xfer = spdk_nvme_opc_get_data_transfer(cmd->opcode);

	if (cmd->data_len != 0) {
		contig_buffer = spdk_dma_zmalloc(cmd->data_len, 0, NULL);
		if (!contig_buffer) {
			return 1;
		}
	}

	if (xfer == SPDK_NVME_DATA_HOST_TO_CONTROLLER) {
		if (contig_buffer) {
			memcpy(contig_buffer, (void *)cmd->addr, cmd->data_len);
		}
	}

	spdk_nvme_cmd.cmd = cmd;
	spdk_nvme_cmd.failed = false;

	outstanding_commands = 0;

	rc = spdk_nvme_ctrlr_cmd_admin_raw(ctrlr, spdk_cmd, contig_buffer, cmd->data_len,
					   nvme_spdk_get_admin_completion, &spdk_nvme_cmd);
	if (rc != 0) {
		fprintf(stderr, "send command failed 0x%x\n", rc);
		return rc;
	}

	outstanding_commands++;

	while (outstanding_commands) {
		spdk_nvme_ctrlr_process_admin_completions(ctrlr);
	}

	if (spdk_nvme_cmd.failed == true) {
		rc = cmd->result;
	} else if (xfer == SPDK_NVME_DATA_CONTROLLER_TO_HOST) {
		if (contig_buffer) {
			memcpy((void *)cmd->addr, contig_buffer, cmd->data_len);
		}
	}

	spdk_dma_free(contig_buffer);

	return rc;
}

unsigned int
nvme_spdk_get_fd_by_dev(const char *dev)
{
	for (int i = 0; i < g_num_ctrlr; i++) {
		if (strcmp(g_spdk_dev[i].dbdf, dev) == 0) {
			return g_spdk_dev[i].fd;
		}
	}

	return 0;
}

void
nvme_spdk_cleanup(void)
{
	struct spdk_nvme_ctrlr *ctrlr = NULL;

	for (int i = 0; i < g_num_ctrlr; i++) {
		if (g_spdk_dev[i].ctrlr && ctrlr != g_spdk_dev[i].ctrlr) {
			spdk_nvme_detach(g_spdk_dev[i].ctrlr);
		}

		/* Same controller with multi-namespaces support */
		ctrlr = g_spdk_dev[i].ctrlr;
	}
}

int
nvme_spdk_get_nsid(unsigned int fd)
{
	for (int i = 0; i < g_num_ctrlr; i++) {
		if (g_spdk_dev[i].fd == fd) {
			return g_spdk_dev[i].ns_id;
		}
	}

	return 1;
}

int
nvme_spdk_is_valid_fd(unsigned int fd)
{
	for (int i = 0; i < g_num_ctrlr; i++) {
		if (g_spdk_dev[i].fd == fd) {
			return 0;
		}
	}

	return -1;
}
