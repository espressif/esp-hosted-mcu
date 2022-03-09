# ESP-Hosted

# Index
* [**1. Introduction**](#1-introduction)  
	* [Connectivity Feature](#11-connectivity-features)  
	* [Supported ESP boards](#12-supported-esp-boards)  
	* [Supported Hosts](#13-supported-hosts)  
	* [Supported Transports](#14-supported-transports)  
	* [Feature Matrix](#15-feature-matrix)  
* [**2. Hardware and Software setup**](#2-hardware-and-software-setup)  
	* [Setup With Linux Host](#21-setup-with-linux-host)  
	* [Setup With MCU host](#22-setup-with-mcu-host)  
* [**3. Control Path**](#3-control-path)
	* [Control Path APIs](#31-control-path-apis)
	* [Demo Apps](#32-demo-apps)
* [**4. Design**](#4-design)  
	* [System Architecture](#41-system-architecture)  
	* [Transport layer communication protocol](#42-transport-layer-communication-protocol)  
	* [Integration Guide](#43-integration-guide)  
* [**5. Throughput performance**](#5-throughput-performance)

---


# 1. Introduction

ESP-Hosted solution provides a way to use ESP board as a communication processor *i.e.* host for Wi-Fi and Bluetooth/BLE connectivity. It basically adds a network interface and a HCI interface to host, allowing it to communicate with other devices. It leverages Wi-Fi and Bluetooth capabilities of ESP chipset to host helping it to be smart device.

Following features are provided as a part of this solution:
* A standard 802.3 network interface is provided to host for transmitting and receiving 802.3 frames
* A standard HCI interface is provided to host over which Bluetooth/BLE is supported
* A control interface to configure and control Wi-Fi on ESP board

ESP-Hosted solution makes use of existing host's `TCP/IP and/or Bluetooth/BLE software stack` and `hardware peripheral like SPI/SDIO/UART` to conect to ESP firmware with very thin layer of software.

Although the project doesn't provide a standard 802.11 interface to the host, it provides a easy way, *i.e.* [control path](docs/common/contrl_path.md), to configure WiFi. For control path between the host and ESP board, ESP-Hosted makes use of [Protobuf](https://developers.google.com/protocol-buffers) which is language independent data serialization mechanism.

### 1.1 Connectivity Features

ESP-Hosted solution provides following WLAN and BT/BLE features to host:
- WLAN Features:
	- 802.11b/g/n
	- WLAN Station
	- WLAN Soft AP
- BT/BLE Features:
	- ESP32 supports BR/EDR and BLE with v4.2
	- ESP32-C3 supports BLE v4.2 and v5.0

### 1.2 Supported ESP boards

ESP-Hosted solution is supported on following ESP boards:
- ESP32
- ESP32S2
- ESP32C3

### 1.3 Supported Hosts

* ESP-Hosted solution showcase examples for following Linux based and MCU based hosts out of the box.
	* Linux Based Hosts
		* Raspberry-Pi 3 Model B
		* Raspberry-Pi 3 Model B+
		* Raspberry-Pi 4 Model B
	* MCU Based Hosts
		* STM32 Discovery Board (STM32F469I-DISCO)
* It is relatively easy to port this solution to other Linux and MCU platforms.

### 1.4 Supported Transports

ESP-Hosted uses SDIO or SPI bus for interfacing ESP boards and host platform. Not all host platforms support both these interfaces. Further section depicts supported host platforms and corresponding transport interface, ESP boards and feature set.

### 1.5 Feature Matrix
##### 1.5.1 Linux Host
Below table explains which feature is supported on which transport interface for Linux based host.

| ESP device | Transport Interface | WLAN support | Virtual serial interface | Bluetooth support |
|:---------:|:-------:|:---------:|:--------:|:--------:|
| ESP32 | SDIO | Yes | Yes | BT/BLE 4.2 |
| ESP32 | SPI | Yes | Yes | BT/BLE 4.2 |
| ESP32 | UART | No | No | BT/BLE 4.2 |
| ESP32-S2 | SDIO | NA | NA | NA |
| ESP32-S2 | SPI | Yes | Yes | NA |
| ESP32-S2 | UART | No | No | NA |
| ESP32-C3 | SDIO | NA | NA | NA |
| ESP32-C3 | SPI | Yes | Yes | BLE 5.0 |
| ESP32-C3 | UART | No | No | BLE 5.0 |

Note:
* BT stands for Bluetooth BR/EDR and BLE stands for Bluetooth Low Energy specifications.
* ESP-Hosted related BR/EDR 4.2 and BLE 4.2 functionalities are tested with bluez 5.43+. Whereas BLE 5.0 functionalities are tested with bluez 5.45+.
* We suggest latest stable bluez version to be used. Any other bluetooth stack instead of bluez also could be used.
* bluez 5.45 on-wards BLE 5.0 HCI commands are supported.
* BLE 5.0 has backward compability of BLE 4.2.

##### 1.5.2 MCU Host
Below table explains which feature is supported on which transport interface for MCU based host.

| ESP device | Transport Interface | WLAN support | Virtual serial interface | Bluetooth support |
|:------------:|:-------:|:---------:|:--------:|:--------:|
| ESP32 | SDIO | No | No | No |
| ESP32 | SPI | Yes | Yes | BT/BLE 4.2\* |
| ESP32 | UART | No | No | BT/BLE 4.2\*\* |
| ESP32-S2 | SDIO | NA | NA | NA |
| ESP32-S2 | SPI | Yes | Yes | NA |
| ESP32-S2 | UART | No | No | NA |
| ESP32-C3 | SDIO | NA | NA | NA |
| ESP32-C3 | SPI | Yes | Yes | BLE 5.0\* |
| ESP32-C3 | UART | No | No | BLE 5.0\*\* |

Note: BT stands for Bluetooth BR/EDR and BLE stands for Bluetooth Low Energy specifications.

\* BT/BLE over SPI
> BT/BLE support over SPI is not readily available. In order to implement it, one needs to:
> 
> Port BT/BLE stack to MCU, \
> Add a new virtual serial interface using the serial interface API's provided in host driver of ESP-Hosted solution.
> HCI implementation in Linux Driver `host/linux/host_driver/esp32` could be used as reference. Search keyword: `ESP_HCI_IF`
> Register this serial interface with BT/BLE stack as a HCI interface.

\*\* BT/BLE over UART
> BT/BLE support over UART is not readily available. In order to implement this, one needs to:
>
> Port BT/BLE stack to MCU, \
> Register the UART serial interface as a HCI interface with BT/BLE stack
> With the help of this UART interface, BT/BLE stack can directly interact with BT controller present on ESP32 bypassing host driver and firmware
> ESP Hosted host driver and a firmware plays no role in this communication

* Linux hosts support OTA update (Over The Air ESP32 firmware update) in C and python. MCU hosts can refer the same for their development. For detailed documentation please read
[ota_update.md](docs/Linux_based_host/ota_update.md).

---

# 2. Hardware and Software setup
This section describes how to setup and use ESP-Hosted solution. Since ESP-Hosted solution supports two distinct platforms, procedure to use it vastly differs.

### 2.1 Setup With Linux Host
Please refer [Setup Guide](docs/Linux_based_host/Linux_based_readme.md) guide for Linux host.

### 2.2 Setup With MCU Host
Please refer [Setup Guide](docs/MCU_based_host/MCU_based_readme.md) guide for MCU host.
In addition to [Control path APIs](#31-control-path-apis) listed below, APIs for MCU specific solution are explained [here](docs/MCU_based_host/mcu_api.md)

---

# 3. Control Path
- Once ESP-Hosted transport is setup, getting control path working is the first step to verify if the transport is setup correctly
- Control path works over ESP-Hosted transport *i.e.* SPI or SDIO and leverages way to control and manage ESP from host
- Detailed design and procedure to get control path working is explained [here](docs/common/contrl_path.md)
- Impatient to evaluate? Please follow our beutiful and intuitive cli [here](<TDB>) to setup control path

### 3.1 Control Path APIs
- As [control path design](docs/common/contrl_path.md#3-design) details, demo application works over control path libary using control path APIs.
- These APIs are exhaustive list. User can use specific APIs which are of interest.
- Each API is explained in detailed way to kickstart its use.
- These Control APIs are common for **MPU** and **MCU** based solution.
- User can easily integrate ESP-Hosted solution with own project using these APIs and demo application, which implements these APIs.
- Control APIs are detailed below.
  - [Control path APIs](docs/common/ctrl_apis.md)

### 3.2 Demo apps
Demo applications are provided in python and C. Python app makes use of [ctypes](https://docs.python.org/3/library/ctypes.html) to implement control APIs.

* [C demo app](docs/common/c_demo.md)
* [Python demo app](docs/common/python_demo.md)

---

# 4. Design
This section describes the overall design of ESP-Hosted solution. There are 3 aspects to it:
* System Architecture
* Transport layer communication protocol
* Integration Guide

### 4.1 System Architecture

This section discusses building blocks of the ESP-Hosted solution for the supported host platforms.

These building blocks can be broadly classified as:
* ESP Host Software  
This includes ESP Host driver and control interface implementation.

* ESP Firmware  
This includes ESP peripheral driver and implementation of control commands.

* Third party components  
This includes components that are essential for end to end working of entire system but are not maintained or implemented as a part of this project.


##### 4.1.1 ESP Host Software

The components of ESP host software are dependent on host platform that is being used. Please refer following documents:

1. [System Architecture: Linux based host](docs/Linux_based_host/Linux_based_architecture.md)
2. [System Architecture: MCU based host](docs/MCU_based_host/MCU_based_architecture.md)


##### 4.1.2 ESP Firmware
This implements ESP-Hosted solution part that runs on ESP boards. ESP firmware is agnostic of the host platform. It consists of following.

* ESP Application  
This implements:
	* SDIO transport layer
	* SPI transport layer
	* Virtual serial interface driver
	* Control interface command implementation
	* Bridges data path between Wi-Fi, HCI controller driver of ESP and Host platform
* ESP-IDF Components  
ESP firmware mainly uses following components from ESP-IDF. Please check [ESP-IDF documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) for more details.
	* SDIO Slave driver
	* SPI Slave driver
	* Wi-Fi driver
	* HCI controller driver
	* Protocomm Layer


##### 4.1.3 Third Party Components
Third components such as following are essential for end to end working of ESP-Hosted Solution. Implementation or porting of these third party compoenent is not in scope of this project.
* TCP/IP and TLS stack
* BT/BLE stack
* UART driver
* Protobuf


### 4.2 Transport layer communication protocol
This section describes the data communication protocol used at the transport layer. This protocol is agnostic of host platforms that is being used. Please refer following links to get more information on communication protocol per transport interface type.
* [SDIO Communication Protocol](docs/sdio_protocol.md)
* [SPI Communication Protocol](docs/spi_protocol.md)

##### 4.2.1 Payload Format
This section explains the payload format used for data transfer on SDIO and SPI interfaces.

* Host and peripheral makes use of 12 byte payload header which preceeds every data packet.
* This payload header provides additional information about the data packet. Based on this header, host/peripheral consumes transmitted data packet.
* Payload format is as below

| Field | Length | Description |
|:-------:|:---------:|:--------|
| Interface type | 4 bits | Possible values: STA(0), SoftAP(1), Serial interface(2), HCI (3), Priv interface(4). Rest all values are reserved |
| Interface number | 4 bits | Unused |
| Flags | 1 byte | Additional flags like `MORE_FRAGMENT` in fragmentation |
| Packet length | 2 bytes | Actual length of data packet |
| Offset to packet | 2 bytes | Offset of actual data packet |
| Checksum | 2 bytes | checksum for complete packet (Includes header and payload) |
| Reserved2 | 1 byte | Not in use |
| seq_num | 2 bytes | Sequence number for serial inerface |
| Packet type | 1 byte | reserved when interface type is 0, 1 and 2. Applicable only for interface type 3 and 4 |

### 4.3 Integration Guide

##### 4.3.1 Porting
Porting for MPU *.i.e.* Linux based hosts is explained [here](docs/Linux_based_host/porting_guide.md)

##### 4.3.3 APIs for MCU Based ESP-Hosted Solution
Below document explains the APIs provided for MCU based ESP-Hosted solution
* [API's for MCU based host](docs/MCU_based_host/mcu_api.md)

# 5. Throughput performance
* Wi-Fi Performance in shielded environment
Following performance numbers are taken on Linux based ESP-Hosted solution.
These numbers are tested with older release, [Release 0.3](releases/tag/release%2Fv0.3)

![alt text](esp_hosted_performance.png "ESP Hosted performance matrix")
