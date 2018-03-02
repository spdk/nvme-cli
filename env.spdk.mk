#
#  BSD LICENSE
#
#  Copyright (c) Intel Corporation.
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in
#      the documentation and/or other materials provided with the
#      distribution.
#    * Neither the name of Intel Corporation nor the names of its
#      contributors may be used to endorse or promote products derived
#      from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

SPDK_ROOT_DIR ?= $(abspath $(CURDIR)/spdk)
SPDK_BUILD_DIR ?=  $(SPDK_ROOT_DIR)/build/lib
DPDK_BUILD_DIR ?= $(SPDK_ROOT_DIR)/dpdk/build

DPDK_LIB = $(DPDK_BUILD_DIR)/lib/librte_eal.a \
	$(DPDK_BUILD_DIR)/lib/librte_mempool.a \
	$(DPDK_BUILD_DIR)/lib/librte_ring.a \
	$(DPDK_BUILD_DIR)/lib/librte_pci.a \
	$(DPDK_BUILD_DIR)/lib/librte_bus_pci.a

SPDK_LIB += $(SPDK_BUILD_DIR)/libspdk_log.a \
	$(SPDK_BUILD_DIR)/libspdk_nvme.a \
	$(SPDK_BUILD_DIR)/libspdk_env_dpdk.a \
	$(SPDK_BUILD_DIR)/libspdk_util.a \
	$(DPDK_LIB)

override CFLAGS += -I$(SPDK_ROOT_DIR)/include
override LDFLAGS += -ldl -pthread -lrt -lrdmacm -lnuma -libverbs -Wl,--whole-archive $(SPDK_LIB) -Wl,--no-whole-archive
