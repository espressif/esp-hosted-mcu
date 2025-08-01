// SPDX-License-Identifier: Apache-2.0
// Copyright 2015-2021 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef __NETWORK_ADAPTER_PRIV__H
#define __NETWORK_ADAPTER_PRIV__H

#include "esp_hosted_transport.h"
#include "esp_hosted_header.h"
#include "esp_hosted_interface.h"
#include "esp_hosted_transport_init.h"

typedef struct {
	interface_context_t *context;
} adapter;

esp_err_t esp_hosted_coprocessor_init(void);
#endif
