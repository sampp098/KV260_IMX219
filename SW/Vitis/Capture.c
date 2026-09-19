/******************************************************************************
 * IMX219 bare-metal capture test for KV260
 *
 * Hardware:
 *
 *   IMX219
 *      |
 *      | 2-lane MIPI CSI-2 RAW10
 *      v
 *   MIPI CSI-2 RX Subsystem
 *      |
 *      | AXI4-Stream
 *      v
 *   stream2bram
 *      |
 *      v
 *   BRAM Port A
 *
 *   PS -> AXI BRAM Controller -> BRAM Port B
 *
 *
 * stream2bram behaviour:
 *
 *   WAIT_SOF
 *      |
 *      | TUSER = 1
 *      v
 *   CAPTURE 16384 words
 *      |
 *      v
 *   DONE
 *
 * BRAM remains frozen until FPGA/resetn reset.
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdint.h>

#include "platform.h"
#include "xil_printf.h"
#include "xil_io.h"
#include "xstatus.h"
#include "xiic.h"
#include "sleep.h"


/*****************************************************************************
 * Hardware addresses
 *
 * From your Vivado Address Editor:
 *
 * AXI IIC     : 0xA0000000
 * CSI-2 RX    : 0xA0010000
 * AXI BRAM    : 0xA0020000
 *****************************************************************************/

#define IIC_BASEADDR       0xA0000000U
#define CSI_BASEADDR       0xA0010000U
#define BRAM_BASEADDR      0xA0200000U

#define BRAM_SIZE_BYTES    0x00010000U

#define BRAM_WORDS         16384U


/*****************************************************************************
 * IMX219
 *****************************************************************************/

#define IMX219_I2C_ADDR        0x10U

#define IMX219_REG_CHIP_ID     0x0000U
#define IMX219_CHIP_ID         0x0219U

#define IMX219_MODE_SELECT     0x0100U

#define IMX219_MODE_STANDBY    0x00U
#define IMX219_MODE_STREAMING  0x01U

#define I2C_MUX_ADDR        0x74U
#define I2C_MUX_CAM_CH2     0x04U
/*****************************************************************************
 * CSI-2 RX register offsets
 *****************************************************************************/

#define CSI_CORE_CONFIG        0x0000U

/*
 * Important:
 *
 * Core status is offset 0x10, NOT 0x04.
 */
#define CSI_CORE_STATUS        0x0010U

#define CSI_INTERRUPT_STATUS   0x0024U


/*****************************************************************************
 * Low-level IMX219 I2C functions
 *****************************************************************************/

static int imx219_write_reg8(uint16_t reg, uint8_t value)
{
    uint8_t buf[3];

    buf[0] = (uint8_t)((reg >> 8) & 0xFF);
    buf[1] = (uint8_t)(reg & 0xFF);
    buf[2] = value;

    unsigned sent = XIic_Send(
        IIC_BASEADDR,
        IMX219_I2C_ADDR,
        buf,
        3,
        XIIC_STOP
    );

    if (sent != 3) {
        xil_printf(
            "I2C WRITE8 failed: reg=0x%04X sent=%d\r\n",
            reg,
            sent
        );

        return XST_FAILURE;
    }

    return XST_SUCCESS;
}


/*
 * IMX219 uses big-endian ordering for its 16-bit register values.
 *
 * Example:
 *
 * register 0x016C = 0x0280
 *
 * sends:
 *
 * 01 6C 02 80
 */
static int imx219_write_reg16(uint16_t reg, uint16_t value)
{
    uint8_t buf[4];

    buf[0] = (uint8_t)((reg >> 8) & 0xFF);
    buf[1] = (uint8_t)(reg & 0xFF);

    buf[2] = (uint8_t)((value >> 8) & 0xFF);
    buf[3] = (uint8_t)(value & 0xFF);

    unsigned sent = XIic_Send(
        IIC_BASEADDR,
        IMX219_I2C_ADDR,
        buf,
        4,
        XIIC_STOP
    );

    if (sent != 4) {
        xil_printf(
            "I2C WRITE16 failed: reg=0x%04X sent=%d\r\n",
            reg,
            sent
        );

        return XST_FAILURE;
    }

    return XST_SUCCESS;
}


static int imx219_read_reg8(uint16_t reg, uint8_t *value)
{
    uint8_t addr[2];

    addr[0] = (uint8_t)((reg >> 8) & 0xFF);
    addr[1] = (uint8_t)(reg & 0xFF);

    unsigned sent = XIic_Send(
        IIC_BASEADDR,
        IMX219_I2C_ADDR,
        addr,
        2,
        XIIC_REPEATED_START
    );

    if (sent != 2) {
        xil_printf(
            "I2C register-address write failed: 0x%04X\r\n",
            reg
        );

        return XST_FAILURE;
    }

    unsigned recv = XIic_Recv(
        IIC_BASEADDR,
        IMX219_I2C_ADDR,
        value,
        1,
        XIIC_STOP
    );

    if (recv != 1) {
        xil_printf(
            "I2C read failed: reg=0x%04X\r\n",
            reg
        );

        return XST_FAILURE;
    }

    return XST_SUCCESS;
}


/*****************************************************************************
 * IMX219 ID check
 *****************************************************************************/

static int imx219_check_id(void)
{
    uint8_t msb;
    uint8_t lsb;

    if (imx219_read_reg8(0x0000, &msb) != XST_SUCCESS)
        return XST_FAILURE;

    if (imx219_read_reg8(0x0001, &lsb) != XST_SUCCESS)
        return XST_FAILURE;

    uint16_t id =
        ((uint16_t)msb << 8) |
        ((uint16_t)lsb);

    xil_printf("IMX219 chip ID = 0x%04X\r\n", id);

    if (id != IMX219_CHIP_ID) {

        xil_printf(
            "ERROR: Expected IMX219 ID 0x0219\r\n"
        );

        return XST_FAILURE;
    }

    xil_printf("IMX219 detected successfully.\r\n");

    return XST_SUCCESS;
}

static int i2c_mux_select_camera(void)
{
    uint8_t value = I2C_MUX_CAM_CH2;

    unsigned sent = XIic_Send(
        IIC_BASEADDR,
        I2C_MUX_ADDR,
        &value,
        1,
        XIIC_STOP
    );

    if (sent != 1) {
        xil_printf("ERROR: failed to select I2C mux camera channel\r\n");
        return XST_FAILURE;
    }

    xil_printf("I2C mux camera channel selected\r\n");

    usleep(1000);

    return XST_SUCCESS;
}

/*****************************************************************************
 * IMX219 common initialization
 *
 * Based on the current Linux IMX219 driver common register sequence.
 *****************************************************************************/

static int imx219_common_init(void)
{
#define WR8(REG, VAL) \
    do { \
        if (imx219_write_reg8((REG), (VAL)) != XST_SUCCESS) \
            return XST_FAILURE; \
    } while (0)

#define WR16(REG, VAL) \
    do { \
        if (imx219_write_reg16((REG), (VAL)) != XST_SUCCESS) \
            return XST_FAILURE; \
    } while (0)


    /*
     * Standby during configuration.
     */
    WR8(0x0100, 0x00);


    /*
     * Access manufacturer register area.
     */
    WR8(0x30EB, 0x05);
    WR8(0x30EB, 0x0C);

    WR8(0x300A, 0xFF);
    WR8(0x300B, 0xFF);

    WR8(0x30EB, 0x05);
    WR8(0x30EB, 0x09);


    /*
     * Common undocumented initialization registers.
     */
    WR8(0x455E, 0x00);
    WR8(0x471E, 0x4B);
    WR8(0x4767, 0x0F);
    WR8(0x4750, 0x14);
    WR8(0x4540, 0x00);
    WR8(0x47B4, 0x14);
    WR8(0x4713, 0x30);
    WR8(0x478B, 0x10);
    WR8(0x478F, 0x10);
    WR8(0x4793, 0x10);
    WR8(0x4797, 0x0E);
    WR8(0x479B, 0x0E);


    /*
     * Odd increment.
     */
    WR8(0x0170, 0x01);
    WR8(0x0171, 0x01);


    /*
     * Automatic D-PHY timing.
     */
    WR8(0x0128, 0x00);


    /*
     * External clock = 24 MHz.
     *
     * IMX219 represents this as:
     *
     *      MHz * 256
     *
     * 24 * 256 = 0x1800
     */
    WR16(0x012A, 0x1800);


    return XST_SUCCESS;
}


/*****************************************************************************
 * IMX219 2-lane PLL configuration
 *
 * This gives the standard 456-MHz link frequency:
 *
 *      456 MHz DDR
 *      -> 912 Mb/s per lane
 *****************************************************************************/

static int imx219_configure_2lane(void)
{
    /*
     * PLL clock configuration
     */
    WR8(0x0301, 0x05);
    WR8(0x0303, 0x01);

    WR8(0x0304, 0x03);
    WR8(0x0305, 0x03);

    /*
     * VT PLL multiplier = 57
     */
    WR16(0x0306, 57);

    WR8(0x030B, 0x01);

    /*
     * OP PLL multiplier = 114
     */
    WR16(0x030C, 114);


    /*
     * 2 CSI lanes
     */
    WR8(0x0114, 0x01);


    return XST_SUCCESS;
}


/*****************************************************************************
 * Configure 640 x 480 RAW10
 *
 * Linux's current IMX219 driver uses a centered 1280 x 960 crop with
 * 2x2 analog binning to produce 640 x 480.
 *****************************************************************************/

static int imx219_configure_640x480_raw10(void)
{
    /*
     * Sensor crop coordinates, relative to the active-area origin.
     *
     * Source crop:
     *
     *     1280 x 960
     *
     * Output:
     *
     *     640 x 480
     *
     * via 2x2 analog binning.
     */

    WR16(0x0164, 1000);     /* X start */
    WR16(0x0166, 2279);     /* X end   */

    WR16(0x0168, 752);      /* Y start */
    WR16(0x016A, 1711);     /* Y end   */


    /*
     * Output image size.
     */
    WR16(0x016C, 640);
    WR16(0x016E, 480);


    /*
     * 2x2 analog binning.
     */
    WR8(0x0174, 0x03);
    WR8(0x0175, 0x03);


    /*
     * No horizontal/vertical flip.
     */
    WR8(0x0172, 0x00);


    /*
     * CSI RAW10:
     *
     * MSB byte = 10 bits
     * LSB byte = 10 bits
     */
    WR16(0x018C, 0x0A0A);


    /*
     * OP pixel clock divider follows bits per pixel.
     */
    WR8(0x0309, 10);


    /*
     * Timing.
     *
     * For the current Linux 640x480 analog-binned mode:
     *
     * frame length:
     *     1707 / 2 = 853
     *
     * line length:
     *     3560
     */
    WR16(0x0160, 853);
    WR16(0x0162, 3560);


    /*
     * Exposure.
     *
     * Linux default exposure is 0x640.
     * Analog-binned mode has rate factor 2.
     */
    WR16(0x015A, 0x0320);


    /*
     * Analog gain = 1x/default.
     */
    WR8(0x0157, 0x00);


    /*
     * Digital gain = 1.0
     */
    WR16(0x0158, 0x0100);


    /*
     * Disable test pattern.
     *
     * Change this to 2 if later you want IMX219 color bars
     * for debugging the capture chain.
     */
    WR16(0x0600, 0x0000);


    /*
     * Test-pattern window.
     * Harmless when the pattern generator is disabled.
     */
    WR16(0x0624, 640);
    WR16(0x0626, 480);


    return XST_SUCCESS;
}

static int imx219_configure_160x120_raw10(void)
{
    /*
     * Sensor crop coordinates, relative to the active-area origin.
     *
     * Source crop:
     *
     *     1280 x 960
     *
     * Output:
     *
     *     640 x 480
     *
     * via 2x2 analog binning.
     */

    WR16(0x0164, 1000);     /* X start */
    WR16(0x0166, 2279);     /* X end   */

    WR16(0x0168, 752);      /* Y start */
    WR16(0x016A, 1711);     /* Y end   */


    /*
     * Output image size.
     */
    //WR16(0x016C, 640);
    //WR16(0x016E, 480);
    /* output width  = 160 */
    WR16(0x016C, 160);

    /* output height = 120 */
    WR16(0x016E, 120);

    /*
     * 2x2 analog binning.
     */
    WR8(0x0174, 0x03);
    WR8(0x0175, 0x03);


    /*
     * No horizontal/vertical flip.
     */
    WR8(0x0172, 0x00);


    /*
     * CSI RAW10:
     *
     * MSB byte = 10 bits
     * LSB byte = 10 bits
     */
    WR16(0x018C, 0x0A0A);


    /*
     * OP pixel clock divider follows bits per pixel.
     */
    WR8(0x0309, 10);


    /*
     * Timing.
     *
     * For the current Linux 640x480 analog-binned mode:
     *
     * frame length:
     *     1707 / 2 = 853
     *
     * line length:
     *     3560
     */
    WR16(0x0160, 853);
    WR16(0x0162, 3560);


    /*
     * Exposure.
     *
     * Linux default exposure is 0x640.
     * Analog-binned mode has rate factor 2.
     */
    WR16(0x015A, 0x0320);


    /*
     * Analog gain = 1x/default.
     */
    WR8(0x0157, 0x00);


    /*
     * Digital gain = 1.0
     */
    WR16(0x0158, 0x0100);


    /*
     * Disable test pattern.
     *
     * Change this to 2 if later you want IMX219 color bars
     * for debugging the capture chain.
     */
    WR16(0x0600, 0x0000);


    /*
     * Test-pattern window.
     * Harmless when the pattern generator is disabled.
     */
    WR16(0x0624, 640);
    WR16(0x0626, 480);


    return XST_SUCCESS;
}

/*****************************************************************************
 * Complete camera initialization
 *****************************************************************************/
static int imx219_init(void)
{
    xil_printf("\r\nInitializing IMX219...\r\n");

    usleep(10000);

    /*
     * KV260 RPi camera I2C is behind PCA9546 mux.
     */
    if (i2c_mux_select_camera() != XST_SUCCESS) {
        xil_printf("ERROR: I2C mux setup failed\r\n");
        return XST_FAILURE;
    }
    /*
	 * Verify I2C communication first.
	 */
    if (imx219_check_id() != XST_SUCCESS) {
        xil_printf("ERROR: IMX219 I2C communication failed.\r\n");
        return XST_FAILURE;
    }

    /*
     * Common sensor initialization.
     */
    if (imx219_common_init() != XST_SUCCESS) {

        xil_printf(
            "ERROR: IMX219 common initialization failed.\r\n"
        );

        return XST_FAILURE;
    }


    /*
     * Configure the two-lane PLL.
     */
    if (imx219_configure_2lane() != XST_SUCCESS) {

        xil_printf(
            "ERROR: IMX219 lane configuration failed.\r\n"
        );

        return XST_FAILURE;
    }


    /*
     * Configure 640 x 480 RAW10.
     */

    if (imx219_configure_640x480_raw10() != XST_SUCCESS) {

        xil_printf(
            "ERROR: IMX219 format configuration failed.\r\n"
        );

        return XST_FAILURE;
    }

//    /*
//	 * Configure 160 x 120 RAW10.
//	 */
//	if (imx219_configure_160x120_raw10() != XST_SUCCESS) {
//
//		xil_printf(
//			"ERROR: IMX219 format configuration failed.\r\n"
//		);
//
//		return XST_FAILURE;
//	}

    xil_printf(
        "IMX219 configured: 640x480 RAW10, 2 lanes\r\n"
    );

    return XST_SUCCESS;
}


/*****************************************************************************
 * Start / stop streaming
 *****************************************************************************/

static int imx219_start_stream(void)
{
    xil_printf("Starting IMX219 stream...\r\n");

    if (imx219_write_reg8(
            IMX219_MODE_SELECT,
            IMX219_MODE_STREAMING
        ) != XST_SUCCESS) {

        return XST_FAILURE;
    }

    return XST_SUCCESS;
}


static int imx219_stop_stream(void)
{
    xil_printf("Stopping IMX219 stream...\r\n");

    return imx219_write_reg8(
        IMX219_MODE_SELECT,
        IMX219_MODE_STANDBY
    );
}


/*****************************************************************************
 * CSI RX
 *****************************************************************************/

static void csi_rx_enable(void)
{
    uint32_t reg;

    reg = Xil_In32(
        CSI_BASEADDR + CSI_CORE_CONFIG
    );

    /*
     * Bit 0 = Core Enable
     */
    reg |= 0x00000001U;

    Xil_Out32(
        CSI_BASEADDR + CSI_CORE_CONFIG,
        reg
    );


    xil_printf(
        "CSI configuration = 0x%08X\r\n",
        Xil_In32(CSI_BASEADDR + CSI_CORE_CONFIG)
    );
}


/*****************************************************************************
 * Wait for CSI packets
 *
 * Core Status:
 *
 * bits 31:16 = packet count
 *****************************************************************************/

static int csi_wait_for_packets(void)
{
    unsigned timeout;

    xil_printf("Waiting for CSI-2 packets...\r\n");


    for (timeout = 0; timeout < 5000; timeout++) {

        uint32_t status =
            Xil_In32(
                CSI_BASEADDR + CSI_CORE_STATUS
            );

        uint16_t packet_count =
            (uint16_t)((status >> 16) & 0xFFFF);


        if (packet_count != 0) {

            xil_printf(
                "CSI packets detected. Count = %d\r\n",
                packet_count
            );

            xil_printf(
                "CSI status = 0x%08X\r\n",
                status
            );

            return XST_SUCCESS;
        }


        usleep(1000);
    }


    xil_printf(
        "ERROR: No CSI packets received.\r\n"
    );


    xil_printf(
        "CSI status = 0x%08X\r\n",
        Xil_In32(CSI_BASEADDR + CSI_CORE_STATUS)
    );


    xil_printf(
        "CSI interrupt status = 0x%08X\r\n",
        Xil_In32(CSI_BASEADDR + CSI_INTERRUPT_STATUS)
    );


    return XST_FAILURE;
}


/*****************************************************************************
 * Dump BRAM
 *
 * stream2bram stores:
 *
 *     BRAM[31:24] = 0
 *     BRAM[23:0]  = CSI TDATA
 *****************************************************************************/

static void read_bram(void)
{
    volatile uint32_t *bram =
        (volatile uint32_t *)BRAM_BASEADDR;


    xil_printf("\r\nReading BRAM...\r\n");


    for (unsigned i = 0; i < BRAM_WORDS; i++) {

        uint32_t word = bram[i];

        uint32_t camera_data =
            word & 0x00FFFFFFU;


        xil_printf(
            "%05d : 0x%08X  data24=0x%06X\r\n",
            i,
            word,
            camera_data
        );
    }


    xil_printf("BRAM read complete.\r\n");
}


/*****************************************************************************
 * Small BRAM sanity check
 *
 * Prints only the first words before dumping everything.
 *****************************************************************************/

static void print_first_bram_words(void)
{
    volatile uint32_t *bram =
        (volatile uint32_t *)BRAM_BASEADDR;


    xil_printf("\r\nFirst 32 captured BRAM words:\r\n");


    for (unsigned i = 0; i < 32; i++) {

        xil_printf(
            "%02d : 0x%08X\r\n",
            i,
            bram[i]
        );
    }
}


/*****************************************************************************
 * Main
 *****************************************************************************/

int main(void)
{
    int status;


    init_platform();


    xil_printf(
        "\r\n"
        "----------------------------------------\r\n"
        " KV260 IMX219 bare-metal capture test\r\n"
        "----------------------------------------\r\n"
    );


    /*************************************************************************
     * 1. Initialize IMX219
     *************************************************************************/

    status = imx219_init();

    if (status != XST_SUCCESS) {

        xil_printf(
            "Camera initialization FAILED.\r\n"
        );

        cleanup_platform();

        return -1;
    }


    /*************************************************************************
     * 2. Enable MIPI CSI-2 RX
     *************************************************************************/

    csi_rx_enable();


    /*************************************************************************
     * 3. Start camera
     *
     * Your stream2bram is already in WAIT_SOF after FPGA reset.
     *************************************************************************/

    status = imx219_start_stream();

    if (status != XST_SUCCESS) {

        xil_printf(
            "Failed to start IMX219 stream.\r\n"
        );

        cleanup_platform();

        return -1;
    }


    /*************************************************************************
     * 4. Verify CSI packets are arriving
     *************************************************************************/

    status = csi_wait_for_packets();

    if (status != XST_SUCCESS) {

        xil_printf(
            "CSI receive test FAILED.\r\n"
        );

        imx219_stop_stream();

        cleanup_platform();

        return -1;
    }


    /*************************************************************************
     * 5. Wait long enough for stream2bram to fill 16384 locations.
     *
     * stream2bram automatically:
     *
     *     waits for TUSER
     *     captures 16384 words
     *     enters DONE
     *     freezes BRAM
     *
     * No capture control register is required.
     *************************************************************************/

    xil_printf(
        "Waiting for one-shot BRAM capture...\r\n"
    );


    usleep(500000);


    /*************************************************************************
     * 6. Stop IMX219
     *************************************************************************/

    status = imx219_stop_stream();

    if (status != XST_SUCCESS) {

        xil_printf(
            "WARNING: Failed to stop IMX219.\r\n"
        );
    }


    xil_printf(
        "Camera stopped. BRAM should now be frozen.\r\n"
    );


    /*************************************************************************
     * 7. Inspect captured data
     *************************************************************************/

    print_first_bram_words();


    /*
     * Uncomment when you want to print all 16384 words.
     *
     * Warning:
     * This produces a LOT of UART output.
     */

    /* read_bram(); */


    xil_printf(
        "\r\nCapture test complete.\r\n"
    );


    cleanup_platform();


    return 0;
}
//Capture using
//mrd -size w -bin -file D:/KV260_Camera/MOHSEN_Camera/capture.bin 0xA0020000 16384
