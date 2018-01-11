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
#include <regex.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "nvme-ioctl.h"
#include "fabrics.h"
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
struct spdk_nvme_transport_id g_trid;
static struct spdk_nvme_ctrlr *g_discovery_ctrlr;

struct spdk_nvme_passthru_cmd {
	struct nvme_passthru_cmd	*cmd;
	bool				failed;
};

static struct config *g_nvmf_conf;

static void
nvme_spdk_nvmf_setup_opts(struct spdk_nvme_ctrlr_opts *opts, struct config *conf)
{
	if (conf->hostnqn) {
		snprintf(opts->hostnqn, sizeof(opts->hostnqn), "%s", conf->hostnqn);
	}

	if (conf->nr_io_queues) {
		opts->num_io_queues = atoi(conf->nr_io_queues);
	}

	if (conf->keep_alive_tmo) {
		opts->keep_alive_timeout_ms = atoi(conf->keep_alive_tmo) * 1000;
	}
}

static bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{
	if (g_list_all == true) {
		return true;
	}

	if (strcmp(g_spdk_dev[g_num_ctrlr].traddr, trid->traddr) == 0) {
		if (trid->trtype == SPDK_NVME_TRANSPORT_RDMA) {
			if ((strcmp(g_spdk_dev[g_num_ctrlr].trsvcid, trid->trsvcid) == 0) &&
			    (strcmp(g_spdk_dev[g_num_ctrlr].subnqn, trid->subnqn) == 0)) {
				nvme_spdk_nvmf_setup_opts(opts, g_nvmf_conf);
				return true;
			}
		} else {
			return true;
		}
	}

	return false;
}

static void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
	unsigned int num_ns = spdk_nvme_ctrlr_get_num_ns(ctrlr);
	struct spdk_nvme_ns *ns = NULL;
	struct spdk_nvme_qpair *io_qpair = NULL;

	if (strcmp(trid->subnqn, NVME_DISC_SUBSYS_NAME) == 0) {
		g_discovery_ctrlr = ctrlr;
		return;
	}

	io_qpair = spdk_nvme_ctrlr_alloc_io_qpair(ctrlr, NULL, 0);

	for (int i = 1; i <= num_ns; i ++) {
		ns = spdk_nvme_ctrlr_get_ns(ctrlr, i);
		if (ns != NULL && spdk_nvme_ns_is_active(ns) == true) {
			if (g_num_ctrlr == NUM_MAX_NVMES) {
				fprintf(stderr, "no resource to manage ctrlr %s\n", trid->traddr);
				return;
			}

			g_spdk_dev[g_num_ctrlr].ctrlr = ctrlr;
			g_spdk_dev[g_num_ctrlr].fd = socket(AF_UNIX, SOCK_RAW, 0);
			g_spdk_dev[g_num_ctrlr].io_qpair = io_qpair;
			g_spdk_dev[g_num_ctrlr].ns_id = i;
			snprintf(g_spdk_dev[g_num_ctrlr].traddr, sizeof(trid->traddr), "%s", trid->traddr);
			if (trid->trtype == SPDK_NVME_TRANSPORT_RDMA) {
				snprintf(g_spdk_dev[g_num_ctrlr].subnqn, sizeof(trid->subnqn), "%s", trid->subnqn);
			}

			g_num_ctrlr++;
		}
	}
}

static bool
spdk_nvmf_command_probe(char *cmd_str)
{
	if (strcmp(cmd_str, "discover") == 0) {
		return false;
	}

	if (strcmp(cmd_str, "connect") == 0) {
		return false;
	}

	if (strcmp(cmd_str, "disconnect") == 0) {
		return false;
	}

	if (strcmp(cmd_str, "connect-all") == 0) {
		return false;
	}

	return true;
}

bool
spdk_bypass_cmd(char *cmd_str)
{
	if (strcmp(cmd_str, "help") == 0) {
		return true;
	}

	if (strcmp(cmd_str, "version") == 0) {
		return true;
	}

	if (strcmp(cmd_str, "subsystem-reset") == 0) {
		return true;
	}

	if (strcmp(cmd_str, "gen-hostnqn") == 0) {
		return true;
	}

	return false;
}

static bool
spdk_unsupported_cmd(char *cmd_str)
{
	if (strcmp(cmd_str, "show-regs") == 0) {
		return true;
	}

	return false;
}

static int
nvme_spdk_ereg(char *pattern, char *value)
{
	int r, cflags = 0;
	regmatch_t pm[10];
	const size_t nmatch = 10;
	regex_t reg;

	r = regcomp(&reg, pattern, cflags);
	if (r == 0) {
		r = regexec(&reg, value, nmatch, pm, cflags);
	}

	regfree(&reg);

	return r;
}

static bool
nvme_spdk_is_bdf_dev(char *str)
{
	/* The format is DDDD:BB:DD.F */
	char *reg = "^[0-9]\\{4\\}:[0-9a-fA-F]\\{2\\}:[0-9a-fA-F]\\{2\\}\\.[0-9a-fA-F]$";

	if (nvme_spdk_ereg(reg, str) == 0) {
		return true;
	}
	else {
		return false;
	}
}

static int
spdk_parse_args(int argc, char **argv, bool *probe)
{
	*probe = true;

	if (argc == 1) {
		if (strcmp(argv[0], "list") == 0) {
			g_list_all = true;
			return 0;
		} else if (spdk_unsupported_cmd(argv[0]) == true) {
			fprintf(stderr, "SPDK unsupported command: %s\n", argv[0]);
			return 1;
		} else {
			fprintf(stderr, "device information needed, example: 0000:07:00.0\n");
			return 1;
		}
	}

	*probe = spdk_nvmf_command_probe(argv[0]);
	if (*probe == false) {
		return 0;
	}

	for (int i = 1; i < argc; i++) {
		if (nvme_spdk_is_bdf_dev(argv[i])) {
			snprintf(g_spdk_dev[g_num_ctrlr].traddr, SPDK_TRADDR_MAX_LEN, "%s", argv[i]);
			return 0;
		}
	}

	fprintf(stderr, "device information needed, example: 0000:07:00.0\n");
	return 1;
}

static int
spdk_parse_intel_args(int argc, char **argv)
{
	if (argc == 1) {
		return 0;
	} else if (spdk_bypass_cmd(argv[1]) == true) {
		return 0;
	}

	return 1;
}

int
spdk_main(int argc, char **argv)
{
	int ret;
	struct spdk_env_opts opts;
	char spdk_conf[512], *str;
	char core_mask[16] = "0x1";
	FILE *spdkf;
	bool probe = true;

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

	if (strcmp(argv[0], "intel") == 0) {
		if (spdk_parse_intel_args(argc, argv) == 0) {
			return 0;
		} else {
			ret = spdk_parse_args(argc - 1, &argv[1], &probe);
		}
	} else {
		ret = spdk_parse_args(argc, argv, &probe);
	}

	if (ret != 0) {
		return 1;
	}

	spdk_env_init(&opts);

	if (probe == true) {
		if (spdk_nvme_probe(NULL, NULL, probe_cb, attach_cb, NULL) != 0) {
			fprintf(stderr, "spdk_nvme_probe() failed\n");
			return 1;
		}
	}

	return 0;
}

int
nvme_spdk_nvmf_connect(void *conf)
{
	struct spdk_nvme_ctrlr *ctrlr;

	g_nvmf_conf = (struct config *)conf;

	g_trid.trtype = SPDK_NVMF_TRTYPE_RDMA;
	g_trid.adrfam = SPDK_NVMF_ADRFAM_IPV4;
	snprintf(g_trid.traddr, sizeof(g_trid.traddr), "%s", g_nvmf_conf->traddr);
	snprintf(g_trid.trsvcid, sizeof(g_trid.trsvcid), "%s", g_nvmf_conf->trsvcid);
	snprintf(g_trid.subnqn, sizeof(g_trid.subnqn), "%s", g_nvmf_conf->nqn);
	snprintf(g_spdk_dev[g_num_ctrlr].traddr, sizeof(g_trid.traddr), "%s", g_trid.traddr);
	snprintf(g_spdk_dev[g_num_ctrlr].trsvcid, sizeof(g_trid.trsvcid), "%s", g_trid.trsvcid);
	snprintf(g_spdk_dev[g_num_ctrlr].subnqn, sizeof(g_trid.subnqn), "%s", g_trid.subnqn);

	ctrlr = spdk_nvme_connect(&g_trid, NULL, 0);
	if (ctrlr == NULL) {
		fprintf(stderr, "spdk_nvme_connect() failed\n");
		return 1;
	}

	/* Explicitly call the attach_cb to setup the connected ctrlr. */
	attach_cb(NULL, &g_trid, ctrlr, NULL);

	return 0;
}

static inline int
nvme_spdk_get_error_code(const struct spdk_nvme_cpl *cpl)
{
	return (cpl->status.sct << 8) | cpl->status.sc;
}

static void
nvme_spdk_get_cmd_completion(void *cb_arg, const struct spdk_nvme_cpl *cpl)
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
	if (g_discovery_ctrlr) {
		return g_discovery_ctrlr;
	}

	for (int i = 0; i < g_num_ctrlr; i++) {
		if (g_spdk_dev[i].fd == fd) {
			return g_spdk_dev[i].ctrlr;
		}
	}

	return NULL;
}

static struct spdk_nvme_qpair *
nvme_spdk_get_io_qpair_by_fd(unsigned int fd)
{
	for (int i = 0; i < g_num_ctrlr; i++) {
		if (g_spdk_dev[i].fd == fd) {
			return g_spdk_dev[i].io_qpair;
		}
	}

	return NULL;
}

int
nvme_spdk_submit_cmd_passthru(unsigned int fd, struct nvme_passthru_cmd *cmd, bool admin)
{
	int rc = 0;

	struct spdk_nvme_cmd *spdk_cmd = (struct spdk_nvme_cmd *)cmd;

	struct spdk_nvme_passthru_cmd spdk_nvme_cmd = {};

	void *contig_buffer = NULL;

	struct spdk_nvme_ctrlr *ctrlr = nvme_spdk_get_ctrlr_by_fd(fd);

	struct spdk_nvme_qpair *io_qpair = NULL;

	enum spdk_nvme_data_transfer xfer = spdk_nvme_opc_get_data_transfer(cmd->opcode);

	if (cmd->data_len != 0) {
		contig_buffer = spdk_dma_zmalloc(cmd->data_len, 128, NULL);
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

	if (admin == true) {
		rc = spdk_nvme_ctrlr_cmd_admin_raw(ctrlr, spdk_cmd, contig_buffer, cmd->data_len,
						   nvme_spdk_get_cmd_completion, &spdk_nvme_cmd);
	} else {
		io_qpair = nvme_spdk_get_io_qpair_by_fd(fd);

		rc = spdk_nvme_ctrlr_cmd_io_raw(ctrlr, io_qpair, spdk_cmd, contig_buffer, cmd->data_len,
						nvme_spdk_get_cmd_completion, &spdk_nvme_cmd);
	}

	if (rc != 0) {
		fprintf(stderr, "send command failed 0x%x\n", rc);
		return rc;
	}

	outstanding_commands++;

	while (outstanding_commands) {
		if (admin == true) {
			/* This function is lock protected. */
			spdk_nvme_ctrlr_process_admin_completions(ctrlr);
		} else {
			spdk_nvme_qpair_process_completions(io_qpair, 0);
		}
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
		if (strcmp(g_spdk_dev[i].traddr, dev) == 0) {
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
			spdk_nvme_ctrlr_free_io_qpair(g_spdk_dev[i].io_qpair);

			spdk_nvme_detach(g_spdk_dev[i].ctrlr);
		}

		/* Same controller with multi-namespaces support */
		ctrlr = g_spdk_dev[i].ctrlr;
	}

	if (g_discovery_ctrlr) {
		spdk_nvme_detach(g_discovery_ctrlr);
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

void
nvme_spdk_ns_sector_size(unsigned int fd, int *sector_size)
{
	int ns_id = nvme_spdk_get_nsid(fd);

	struct spdk_nvme_ctrlr *ctrlr = nvme_spdk_get_ctrlr_by_fd(fd);

	struct spdk_nvme_ns *ns = spdk_nvme_ctrlr_get_ns(ctrlr, ns_id);

	*sector_size = spdk_nvme_ns_get_sector_size(ns);
}

static void
nvme_spdk_io_completion(void *cb_arg, const struct spdk_nvme_cpl *cpl)
{
	if (spdk_nvme_cpl_is_error(cpl)) {
		fprintf(stderr, "command error: SC %x SCT %x\n", cpl->status.sc, cpl->status.sct);

		*(int *)cb_arg = nvme_spdk_get_error_code(cpl);
	}

	outstanding_commands--;
}

int
nvme_spdk_io(unsigned int fd, struct nvme_user_io *io)
{
	int rc = 0;

	int io_status = 0;

	int ns_id = nvme_spdk_get_nsid(fd);

	void *payload = NULL;

	struct spdk_nvme_ctrlr *ctrlr = nvme_spdk_get_ctrlr_by_fd(fd);

	struct spdk_nvme_qpair *io_qpair = nvme_spdk_get_io_qpair_by_fd(fd);

	struct spdk_nvme_ns *ns = spdk_nvme_ctrlr_get_ns(ctrlr, ns_id);

	int sector_size = spdk_nvme_ns_get_sector_size(ns);

	/* nblocks is zero based and add 1 for SPDK which is one based */
	io->nblocks++;

	payload = spdk_dma_zmalloc(io->nblocks * sector_size, 0, NULL);
	if (!payload) {
		rc = ENOMEM;

		goto exit;
	}

	outstanding_commands = 0;

	switch (io->opcode) {
		case nvme_cmd_read:
			outstanding_commands++;

			rc = spdk_nvme_ns_cmd_read(ns, io_qpair, payload, io->slba,
						   io->nblocks, nvme_spdk_io_completion, &io_status, 0);

			break;
		case nvme_cmd_write:
			outstanding_commands++;

			memcpy(payload, (void *)io->addr, io->nblocks * sector_size);

			rc = spdk_nvme_ns_cmd_write(ns, io_qpair, payload, io->slba,
						    io->nblocks, nvme_spdk_io_completion, &io_status, 0);

			break;
		case nvme_cmd_compare:
			outstanding_commands++;

			rc = spdk_nvme_ns_cmd_compare(ns, io_qpair, payload, io->slba,
						      io->nblocks, nvme_spdk_io_completion, &io_status, 0);

			break;
		default:
			break;
	}

	if (rc != 0) {
		goto exit;
	}

	while (outstanding_commands) {
		spdk_nvme_qpair_process_completions(io_qpair, 0);
	}

	if (io->opcode == nvme_cmd_read) {
		memcpy((void *)io->addr, payload, io->nblocks * sector_size);
	}

exit:
	spdk_dma_free(payload);

	if (rc == 0) {
		rc = io_status;
	}

	return rc;
}
