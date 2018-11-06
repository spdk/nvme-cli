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
SPDK_LIB_DIR ?= $(SPDK_ROOT_DIR)/build/lib
DPDK_LIB_DIR ?= $(SPDK_ROOT_DIR)/dpdk/build/lib

-include $(SPDK_ROOT_DIR)/CONFIG.local
include $(SPDK_ROOT_DIR)/CONFIG

override CFLAGS += -I$(SPDK_ROOT_DIR)/include
override LDFLAGS += \
	-Wl,--whole-archive \
	-L$(SPDK_LIB_DIR) -lspdk_log -lspdk_sock -lspdk_nvme -lspdk_env_dpdk -lspdk_util \
	-L$(DPDK_LIB_DIR) -lrte_eal -lrte_mempool -lrte_ring -lrte_pci -lrte_bus_pci -lrte_power \
	-Wl,--no-whole-archive \
	-ldl -pthread -lrt -lrdmacm -lnuma -libverbs

ifeq ($(CONFIG_ASAN),y)
override CFLAGS += -fsanitize=address
override LDFLAGS += -fsanitize=address
endif

ifeq ($(CONFIG_UBSAN),y)
override CFLAGS += -fsanitize=undefined
override LDFLAGS += -fsanitize=undefined
endif

ifeq ($(CONFIG_TSAN),y)
override CFLAGS += -fsanitize=thread
override LDFLAGS += -fsanitize=thread
endif

ifeq ($(CONFIG_COVERAGE), y)
override CFLAGS += -fprofile-arcs -ftest-coverage
override LDFLAGS += -fprofile-arcs -ftest-coverage
endif
