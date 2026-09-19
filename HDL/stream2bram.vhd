----------------------------------------------------------------------------------
-- Module Name: stream2bram
--
-- Description:
--
--   One-shot AXI4-Stream video capture into BRAM.
--
--   Operation:
--
--      1. Wait for TUSER = 1 (Start Of Frame)
--      2. Capture BRAM_DEPTH AXI-stream words
--      3. Stop writing
--      4. BRAM contents remain unchanged until reset
--
--   MIPI CSI-2 RX:
--      TDATA = 24 bits
--
--   BRAM:
--      DATA  = 32 bits
--      DEPTH = 16384 words
--
--   The upper 8 bits of every BRAM word are padded with zero.
--
----------------------------------------------------------------------------------

library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;


entity stream2bram is

    generic (
        DATA_WIDTH : positive := 24;
        ADDR_WIDTH : positive := 14;
        BRAM_DEPTH : positive := 16384
    );

    port (

        ----------------------------------------------------------------------
        -- Clock / Reset
        ----------------------------------------------------------------------
        clk     : in std_logic;
        resetn  : in std_logic;


        ----------------------------------------------------------------------
        -- AXI4-Stream Video Input
        ----------------------------------------------------------------------
        video_out_tdata  : in  std_logic_vector(DATA_WIDTH-1 downto 0);
        video_out_tvalid : in  std_logic;
        video_out_tready : out std_logic;
        video_out_tuser  : in  std_logic_vector(0 downto 0);
        video_out_tlast  : in  std_logic;


        ----------------------------------------------------------------------
        -- BRAM Port A
        ----------------------------------------------------------------------
        bram_addr : out std_logic_vector(ADDR_WIDTH-1 downto 0);
        bram_din  : out std_logic_vector(31 downto 0);
        bram_we   : out std_logic_vector(0 downto 0);
        bram_en   : out std_logic

    );

end stream2bram;



architecture Behavioral of stream2bram is


    --------------------------------------------------------------------------
    -- Capture state machine
    --------------------------------------------------------------------------
    type state_type is (
        WAIT_SOF,
        CAPTURE,
        DONE
    );

    signal state : state_type := WAIT_SOF;


    --------------------------------------------------------------------------
    -- Address of next BRAM word
    --------------------------------------------------------------------------
    signal wr_addr : unsigned(ADDR_WIDTH-1 downto 0)
                  := (others => '0');


    --------------------------------------------------------------------------
    -- Registered BRAM address
    --------------------------------------------------------------------------
    signal bram_addr_reg : unsigned(ADDR_WIDTH-1 downto 0)
                         := (others => '0');


begin


    --------------------------------------------------------------------------
    -- Never apply backpressure to CSI-2 receiver.
    --
    -- Before capture and after capture, incoming camera data is simply
    -- accepted and discarded.
    --------------------------------------------------------------------------
    video_out_tready <= '1';


    --------------------------------------------------------------------------
    -- BRAM address output
    --------------------------------------------------------------------------
    bram_addr <= std_logic_vector(bram_addr_reg);



    --------------------------------------------------------------------------
    -- Capture process
    --------------------------------------------------------------------------
    process(clk)
    begin

        if rising_edge(clk) then


            ------------------------------------------------------------------
            -- Default:
            -- No BRAM write unless explicitly enabled below.
            ------------------------------------------------------------------
            bram_we(0) <= '0';
            bram_en    <= '0';


            ------------------------------------------------------------------
            -- Active-low synchronous reset
            ------------------------------------------------------------------
            if resetn = '0' then

                state <= WAIT_SOF;

                wr_addr <= (others => '0');

                bram_addr_reg <= (others => '0');

                bram_din <= (others => '0');

                bram_we(0) <= '0';

                bram_en <= '0';


            else


                ----------------------------------------------------------------
                -- State machine
                ----------------------------------------------------------------
                case state is


                    ------------------------------------------------------------
                    -- Wait for first AXI word of a new frame
                    ------------------------------------------------------------
                    when WAIT_SOF =>

                        wr_addr <= (others => '0');


                        if video_out_tvalid = '1' and
                           video_out_tuser(0) = '1' then


                            --------------------------------------------------
                            -- Capture first AXI word of frame
                            --------------------------------------------------
                            bram_addr_reg <= (others => '0');

                            bram_din <= x"00" & video_out_tdata;

                            bram_en    <= '1';
                            bram_we(0) <= '1';


                            --------------------------------------------------
                            -- Next location
                            --------------------------------------------------
                            wr_addr <= to_unsigned(1, ADDR_WIDTH);


                            --------------------------------------------------
                            -- Continue capturing
                            --------------------------------------------------
                            state <= CAPTURE;

                        end if;



                    ------------------------------------------------------------
                    -- Capture sequential AXI-stream words
                    ------------------------------------------------------------
                    when CAPTURE =>


                        if video_out_tvalid = '1' then


                            --------------------------------------------------
                            -- Write current AXI word
                            --------------------------------------------------
                            bram_addr_reg <= wr_addr;

                            bram_din <= x"00" & video_out_tdata;

                            bram_en    <= '1';
                            bram_we(0) <= '1';


                            --------------------------------------------------
                            -- Have we filled the entire BRAM?
                            --------------------------------------------------
                            if wr_addr =
                               to_unsigned(BRAM_DEPTH - 1, ADDR_WIDTH) then


                                ------------------------------------------------
                                -- This is the final BRAM word.
                                --
                                -- Stop after this write.
                                ------------------------------------------------
                                state <= DONE;


                            else


                                ------------------------------------------------
                                -- Next BRAM location
                                ------------------------------------------------
                                wr_addr <= wr_addr + 1;


                            end if;


                        end if;



                    ------------------------------------------------------------
                    -- Capture finished.
                    --
                    -- BRAM remains frozen until reset.
                    ------------------------------------------------------------
                    when DONE =>

                        bram_en    <= '0';
                        bram_we(0) <= '0';


                    ------------------------------------------------------------
                    -- Safety
                    ------------------------------------------------------------
                    when others =>

                        state <= WAIT_SOF;


                end case;

            end if;

        end if;

    end process;


end Behavioral;

------------------------------------------------------------------------------------
---- Module Name: stream2bram
----
---- Description:
----   Receives AXI4-Stream video data from the MIPI CSI-2 RX Subsystem
----   and writes each AXI-stream word into BRAM.
----
----   TUSER(0) = Start Of Frame
----   TLAST     = End Of Line
----
------------------------------------------------------------------------------------

--library IEEE;
--use IEEE.STD_LOGIC_1164.ALL;
--use IEEE.NUMERIC_STD.ALL;


--entity stream2bram is

--    generic (
--        DATA_WIDTH : positive := 24;
--        ADDR_WIDTH : positive := 16
--    );

--    port (

--        ----------------------------------------------------------------------
--        -- Clock / Reset
--        ----------------------------------------------------------------------
--        clk     : in  std_logic;
--        resetn  : in  std_logic;


--        ----------------------------------------------------------------------
--        -- AXI4-Stream Video Input
--        ----------------------------------------------------------------------
--        video_out_tdata  : in  std_logic_vector(DATA_WIDTH-1 downto 0);

--        video_out_tvalid : in  std_logic;

--        video_out_tready : out std_logic;

--        video_out_tuser  : in  std_logic_vector(0 downto 0);

--        video_out_tlast  : in  std_logic;


--        ----------------------------------------------------------------------
--        -- BRAM Port A
--        ----------------------------------------------------------------------
--        bram_addr : out std_logic_vector(ADDR_WIDTH-1 downto 0);

--        bram_din  : out std_logic_vector(31 downto 0);

--        bram_we   : out std_logic_vector(0 downto 0);

--        bram_en   : out std_logic

--    );

--end stream2bram;



--architecture Behavioral of stream2bram is


--    --------------------------------------------------------------------------
--    -- Address of next BRAM location
--    --------------------------------------------------------------------------
--    signal wr_addr : unsigned(ADDR_WIDTH-1 downto 0)
--                  := (others => '0');


--    --------------------------------------------------------------------------
--    -- Registered BRAM address
--    --------------------------------------------------------------------------
--    signal bram_addr_reg : unsigned(ADDR_WIDTH-1 downto 0)
--                         := (others => '0');


--begin


--    --------------------------------------------------------------------------
--    -- We currently never apply backpressure to the MIPI receiver.
--    --
--    -- Therefore every valid AXI word is accepted immediately.
--    --------------------------------------------------------------------------
--    video_out_tready <= '1';


--    --------------------------------------------------------------------------
--    -- BRAM address output
--    --------------------------------------------------------------------------
--    bram_addr <= std_logic_vector(bram_addr_reg);



--    --------------------------------------------------------------------------
--    -- AXI4-Stream -> BRAM write process
--    --------------------------------------------------------------------------
--    process(clk)
--    begin

--        if rising_edge(clk) then


--            ------------------------------------------------------------------
--            -- Default values
--            --
--            -- BRAM write is disabled unless a valid AXI transfer occurs.
--            ------------------------------------------------------------------
--            bram_we(0) <= '0';
--            bram_en    <= '0';



--            ------------------------------------------------------------------
--            -- Active-low synchronous reset
--            ------------------------------------------------------------------
--            if resetn = '0' then


--                wr_addr <= (others => '0');

--                bram_addr_reg <= (others => '0');

--                bram_din <= (others => '0');

--                bram_we(0) <= '0';

--                bram_en <= '0';



--            else


--                --------------------------------------------------------------
--                -- Because TREADY is permanently 1,
--                -- TVALID alone indicates an AXI transfer.
--                --------------------------------------------------------------
--                if video_out_tvalid = '1' then


--                    ----------------------------------------------------------
--                    -- Enable BRAM write
--                    ----------------------------------------------------------
--                    bram_en    <= '1';
--                    bram_we(0) <= '1';


--                    ----------------------------------------------------------
--                    -- Store incoming AXI-stream word
--                    ----------------------------------------------------------
--                    bram_din(DATA_WIDTH-1 downto 0) <= video_out_tdata;



--                    ----------------------------------------------------------
--                    -- Start Of Frame
--                    --
--                    -- TUSER(0) is asserted together with the first
--                    -- pixel/word of a frame.
--                    ----------------------------------------------------------
--                    if video_out_tuser(0) = '1' then


--                        ------------------------------------------------------
--                        -- First AXI word goes to BRAM address 0
--                        ------------------------------------------------------
--                        bram_addr_reg <= (others => '0');


--                        ------------------------------------------------------
--                        -- Next AXI word will go to address 1
--                        ------------------------------------------------------
--                        wr_addr <= to_unsigned(1, ADDR_WIDTH);



--                    else


--                        ------------------------------------------------------
--                        -- Current AXI word goes to wr_addr
--                        ------------------------------------------------------
--                        bram_addr_reg <= wr_addr;


--                        ------------------------------------------------------
--                        -- Prepare address for next AXI word
--                        ------------------------------------------------------
--                        wr_addr <= wr_addr + 1;


--                    end if;



--                    ----------------------------------------------------------
--                    -- TLAST indicates end of line.
--                    --
--                    -- No action is required yet.
--                    -- Later we can use this for:
--                    --
--                    --   x_pixel counter
--                    --   y_line counter
--                    --   image cropping
--                    --
--                    ----------------------------------------------------------
--                    if video_out_tlast = '1' then

--                        null;

--                    end if;


--                end if;

--            end if;

--        end if;

--    end process;


--end Behavioral;