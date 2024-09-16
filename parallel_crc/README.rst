Includes: 

The includes can be found in the Vitis Examples Repository in the common folder. https://github.com/Xilinx/Vitis_Accel_Examples/tree/main/common

Make: 

make all TARGET=sw_emu PLATFORM=/opt/xilinx/platforms/xilinx_u280_gen3x16_xdma_1_202211_1/xilinx_u280_gen3x16_xdma_1_202211_1.xpfm

make all TARGET=hw_emu PLATFORM=/opt/xilinx/platforms/xilinx_u280_gen3x16_xdma_1_202211_1/xilinx_u280_gen3x16_xdma_1_202211_1.xpfm

make all TARGET=hw PLATFORM=/opt/xilinx/platforms/xilinx_u280_gen3x16_xdma_1_202211_1/xilinx_u280_gen3x16_xdma_1_202211_1.xpfm

Run:

./host_xrt -x ./build_dir.sw_emu.xilinx_u280_gen3x16_xdma_1_202211_1/kernel.xclbin

./host_xrt -x ./build_dir.hw_emu.xilinx_u280_gen3x16_xdma_1_202211_1/kernel.xclbin

// Hardware runs on OCT?

./host_xrt -x ./build_dir.hw.xilinx_u280_gen3x16_xdma_1_202211_1/kernel.xclbin
