# FPGA Parallel CRC Bitstream Generator

This repository contains the source code for generating FPGA bitstreams that perform Cyclic Redundancy Check (CRC) calculations in parallel. By leveraging FPGA's inherent parallelism, this project enables faster and more efficient CRC calculations compared to traditional serial approaches.

## Features

- **Parallel CRC Calculation**: Implement CRC algorithms on FPGA, significantly speeding up the process compared to CPU-based serial implementations.
- **Configurable CRC Polynomial**: The design allows for the use of different CRC polynomials to support various protocols (e.g., CRC-32, CRC-16, etc.).
- **High Throughput**: Optimized for parallelism, this design increases throughput and reduces latency for CRC calculations.
- **FPGA Platform Support**: Supports Xilinx FPGA families for use in OCT.

## Getting Started

### Prerequisites

Before you begin, ensure you have the following installed:

- FPGA development tools Xilinx Vivado
- Basic knowledge of CRC algorithms
- Git for cloning the repository
