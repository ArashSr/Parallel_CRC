Includes: 

The includes can be found in the Vitis Examples Repository in the common folder. https://github.com/Xilinx/Vitis_Accel_Examples/tree/main/common


**Software Emulation**

make all TARGET=sw_emu PLATFORM=/opt/xilinx/platforms/xilinx_u280_gen3x16_xdma_1_202211_1/xilinx_u280_gen3x16_xdma_1_202211_1.xpfm

./host_xrt -x ./build_dir.sw_emu.xilinx_u280_gen3x16_xdma_1_202211_1/kernel.xclbin

**Hardware Emulation**

make all TARGET=hw_emu PLATFORM=/opt/xilinx/platforms/xilinx_u280_gen3x16_xdma_1_202211_1/xilinx_u280_gen3x16_xdma_1_202211_1.xpfm

./host_xrt -x ./build_dir.hw_emu.xilinx_u280_gen3x16_xdma_1_202211_1/kernel.xclbin

**Hardware (Run on on CloudLab OCT)**

make all TARGET=hw PLATFORM=/opt/xilinx/platforms/xilinx_u280_gen3x16_xdma_1_202211_1/xilinx_u280_gen3x16_xdma_1_202211_1.xpfm

./host_xrt -x ./build_dir.hw.xilinx_u280_gen3x16_xdma_1_202211_1/kernel.xclbin
