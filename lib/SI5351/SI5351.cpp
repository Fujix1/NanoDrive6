#include "SI5351.hpp"

#include <Wire.h>
#include <stdlib.h>
// constructor
SI5351_cls::SI5351_cls(void) {
  this->currentFreq0 = SI5351_UNDEFINED;
  this->currentFreq1 = SI5351_UNDEFINED;
}

/*
 *  Write a register and an 8-bit value over I2C
 */
void SI5351_cls::write8(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(SI5351_ADDRESS);
  Wire.write(reg);
  Wire.write(value & 0xFF);
  Wire.endTransmission();
}

void SI5351_cls::begin(void) {
  /* Initialise I2C */
  Wire.begin(18, 8, 100000);

  /* Disable all outputs setting CLKx_DIS high */
  write8(SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL, 0xFF);

  /* Set the load capacitance for the XTAL */
  // Bit 7:6 Crystal Load Capacitance Selection.
  // These 2 bits determine the internal load capacitance value for the crystal.
  // See the Crystal Inputs section in the Si5351 data sheet. 00: Reserved. Do
  // not select this option. 01: Internal CL = 6 pF. 10: Internal CL = 8 pF. 11:
  // Internal CL = 10 pF (default).
  //
  // 5:0 Reserved Bits 5:0 should be written to 010010b.
  write8(SI5351_REGISTER_183_CRYSTAL_INTERNAL_LOAD_CAPACITANCE,
         0b10010010);  // 8 pF

  write8(16, 0x80);  // Disable CLK0
  write8(17, 0x80);  // Disable CLK1
  write8(18, 0x80);  // Disable CLK2
}

/**************************************************************************/
/*!
  @brief  Sets the multiplier for the specified PLL using integer values
  @param  pll   The PLL to configure, which must be one of the following:
                - SI5351_PLL_A
                - SI5351_PLL_B
  @param  mult  The PLL integer multiplier (must be between 15 and 90)
*/
/**************************************************************************/
void SI5351_cls::setupPLLInt(si5351PLL_t pll, uint8_t mult) { setupPLL(pll, mult, 0, 1); }

/**************************************************************************/
/*!
    @brief  Sets the multiplier for the specified PLL
    @param  pll   The PLL to configure, which must be one of the following:
                  - SI5351_PLL_A
                  - SI5351_PLL_B
    @param  mult  The PLL integer multiplier (must be between 15 and 90)
    @param  num   The 20-bit numerator for fractional output (0..1,048,575).
                  Set this to '0' for integer output.
    @param  denom The 20-bit denominator for fractional output (1..1,048,575).
                  Set this to '1' or higher to avoid divider by zero errors.
    @section PLL Configuration
    fVCO is the PLL output, and must be between 600..900MHz, where:
        fVCO = fXTAL * (a+(b/c))
    fXTAL = the crystal input frequency
    a     = an integer between 15 and 90
    b     = the fractional numerator (0..1,048,575)
    c     = the fractional denominator (1..1,048,575)
    NOTE: Try to use integers whenever possible to avoid clock jitter
    (only use the a part, setting b to '0' and c to '1').
    See: http://www.silabs.com/Support%20Documents/TechnicalDocs/AN619.pdf
*/
/**************************************************************************/
void SI5351_cls::setupPLL(si5351PLL_t pll, uint8_t mult, uint32_t num, uint32_t denom) {
  uint32_t P1; /* PLL config register P1 */
  uint32_t P2; /* PLL config register P2 */
  uint32_t P3; /* PLL config register P3 */

  /* Set the main PLL config registers */
  if (num == 0) {
    /* Integer mode */
    P1 = 128 * mult - 512;
    P2 = num;
    P3 = denom;
  } else {
    /* Fractional mode */
    P1 = (uint32_t)(128 * mult + floor(128 * ((float)num / (float)denom)) - 512);
    P2 = (uint32_t)(128 * num - denom * floor(128 * ((float)num / (float)denom)));
    P3 = denom;
  }

  /* Get the appropriate starting point for the PLL registers */
  uint8_t baseaddr = (pll == SI5351_PLL_A ? 26 : 34);

  /* The datasheet is a nightmare of typos and inconsistencies here! */
  write8(baseaddr, (P3 & 0x0000FF00) >> 8);
  write8(baseaddr + 1, (P3 & 0x000000FF));
  write8(baseaddr + 2, (P1 & 0x00030000) >> 16);
  write8(baseaddr + 3, (P1 & 0x0000FF00) >> 8);
  write8(baseaddr + 4, (P1 & 0x000000FF));
  write8(baseaddr + 5, ((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16));
  write8(baseaddr + 6, (P2 & 0x0000FF00) >> 8);
  write8(baseaddr + 7, (P2 & 0x000000FF));

  /* Reset both PLLs */
  write8(SI5351_REGISTER_177_PLL_RESET, (1 << 7) | (1 << 5));
}

/**************************************************************************/
/*!
    @brief  Configures the Multisynth divider using integer output.
    @param  output    The output channel to use (0..2)
    @param  pllSource  The PLL input source to use, which must be one of:
                      - SI5351_PLL_A
                      - SI5351_PLL_B
    @param  div       The integer divider for the Multisynth output,
                      which must be one of the following values:
                      - SI5351_MULTISYNTH_DIV_4
                      - SI5351_MULTISYNTH_DIV_6
                      - SI5351_MULTISYNTH_DIV_8
*/
/**************************************************************************/
void SI5351_cls::setupMultisynthInt(uint8_t output, si5351PLL_t pllSource, si5351MultisynthDiv_t div) {
  return setupMultisynth(output, pllSource, div, 0, 1);
}

/**************************************************************************/
/*!
    @brief  Configures the Multisynth divider, which determines the
            output clock frequency based on the specified PLL input.
    @param  output    The output channel to use (0..2)
    @param  pllSource  The PLL input source to use, which must be one of:
                      - SI5351_PLL_A
                      - SI5351_PLL_B
    @param  div       The integer divider for the Multisynth output.
                      If pure integer values are used, this value must
                      be one of:
                      - SI5351_MULTISYNTH_DIV_4
                      - SI5351_MULTISYNTH_DIV_6
                      - SI5351_MULTISYNTH_DIV_8
                      If fractional output is used, this value must be
                      between 8 and 900.
    @param  num       The 20-bit numerator for fractional output
                      (0..1,048,575). Set this to '0' for integer output.
    @param  denom     The 20-bit denominator for fractional output
                      (1..1,048,575). Set this to '1' or higher to
                      avoid divide by zero errors.
    @section Output Clock Configuration
    The multisynth dividers are applied to the specified PLL output,
    and are used to reduce the PLL output to a valid range (500kHz
    to 160MHz). The relationship can be seen in this formula, where
    fVCO is the PLL output frequency and MSx is the multisynth
    divider:
        fOUT = fVCO / MSx
    Valid multisynth dividers are 4, 6, or 8 when using integers,
    or any fractional values between 8 + 1/1,048,575 and 900 + 0/1
    The following formula is used for the fractional mode divider:
        a + b / c
    a = The integer value, which must be 4, 6 or 8 in integer mode (MSx_INT=1)
        or 8..900 in fractional mode (MSx_INT=0).
    b = The fractional numerator (0..1,048,575)
    c = The fractional denominator (1..1,048,575)
    @note   Try to use integers whenever possible to avoid clock jitter
    @note   For output frequencies > 150MHz, you must set the divider
            to 4 and adjust to PLL to generate the frequency (for example
            a PLL of 640 to generate a 160MHz output clock). This is not
            yet supported in the driver, which limits frequencies to
            500kHz .. 150MHz.
    @note   For frequencies below 500kHz (down to 8kHz) Rx_DIV must be
            used, but this isn't currently implemented in the driver.
*/
/**************************************************************************/
void SI5351_cls::setupMultisynth(uint8_t output, si5351PLL_t pllSource, uint32_t div, uint32_t num, uint32_t denom) {
  uint32_t P1; /* Multisynth config register P1 */
  uint32_t P2; /* Multisynth config register P2 */
  uint32_t P3; /* Multisynth config register P3 */

  /* Set the main PLL config registers */
  if (num == 0) {
    /* Integer mode */
    P1 = 128 * div - 512;
    P2 = num;
    P3 = denom;
  } else {
    /* Fractional mode */
    P1 = (uint32_t)(128 * div + floor(128 * ((float)num / (float)denom)) - 512);
    P2 = (uint32_t)(128 * num - denom * floor(128 * ((float)num / (float)denom)));
    P3 = denom;
  }

  /* Get the appropriate starting point for the PLL registers */
  uint8_t baseaddr = 0;
  switch (output) {
    case 0:
      baseaddr = SI5351_REGISTER_42_MULTISYNTH0_PARAMETERS_1;
      break;
    case 1:
      baseaddr = SI5351_REGISTER_50_MULTISYNTH1_PARAMETERS_1;
      break;
    case 2:
      baseaddr = SI5351_REGISTER_58_MULTISYNTH2_PARAMETERS_1;
      break;
  }

  /* Set the MSx config registers */
  write8(baseaddr, (P3 & 0x0000FF00) >> 8);
  write8(baseaddr + 1, (P3 & 0x000000FF));
  write8(baseaddr + 2, (P1 & 0x00030000) >> 16);
  write8(baseaddr + 3, (P1 & 0x0000FF00) >> 8);
  write8(baseaddr + 4, (P1 & 0x000000FF));
  write8(baseaddr + 5, ((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16));
  write8(baseaddr + 6, (P2 & 0x0000FF00) >> 8);
  write8(baseaddr + 7, (P2 & 0x000000FF));

  /* Configure the clk control and enable the output */
  uint8_t clkControlReg = 0x0F;                             /* 8mA drive strength, MS0 as CLK0 source, Clock
                                                               not inverted, powered up */
  if (pllSource == SI5351_PLL_B) clkControlReg |= (1 << 5); /* Uses PLLB */
  if (num == 0) clkControlReg |= (1 << 6);                  /* Integer mode */
  switch (output) {
    case 0:
      write8(SI5351_REGISTER_16_CLK0_CONTROL, clkControlReg);
      break;
    case 1:
      write8(SI5351_REGISTER_17_CLK1_CONTROL, clkControlReg);
      break;
    case 2:
      write8(SI5351_REGISTER_18_CLK2_CONTROL, clkControlReg);
      break;
  }
}

/*
 * Switch all clock outputs
 */
void SI5351_cls::enableOutputs(bool enabled) { write8(SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL, enabled ? 0x00 : 0xFF); }

// new frequency
void SI5351_cls::setFreq(si5351Freq_t newFreq, uint8_t output) {
  switch (output) {
    case 0:
      if (this->currentFreq0 == newFreq) return;
      break;
    case 1:
      if (this->currentFreq1 == newFreq) return;
      break;
  }

  si5351PLL_t targetPLL;

  if (output == 0) {
    targetPLL = SI5351_PLL_A;
  } else {
    targetPLL = SI5351_PLL_B;
  }

  switch (newFreq) {
    case SI5351_1250:  // 1.250MHz
      setupPLLInt(targetPLL, 31);
      setupMultisynth(output, targetPLL, 620, 0, 1);
      break;
    case SI5351_1500:  // 1.5MHz
      setupPLL(targetPLL, 30, 0, 1);
      setupMultisynth(output, targetPLL, 500, 0, 1);
      break;
    case SI5351_1536:  // 1.536MHz
      setupPLL(targetPLL, 28, 52, 3125);
      setupMultisynth(output, targetPLL, 456, 0, 1);
      break;
    case SI5351_1789:  // 1.789772MHz
      setupPLL(targetPLL, 27, 575277, 625000);
      setupMultisynth(output, targetPLL, 390, 0, 1);
      break;
    case SI5351_2000:              // 2MHz
      setupPLLInt(targetPLL, 32);  // 25MHz * 32 = 800
      setupMultisynth(output, targetPLL, 400, 0, 1);
      break;
    case SI5351_2578:  // 2.578 MHz
      setupPLL(targetPLL, 28, 3868, 78125);
      setupMultisynth(0, targetPLL, 272, 0, 1);
      break;
    case SI5351_3000:  // 3MHz
      setupPLL(targetPLL, 30, 0, 1);
      setupMultisynth(output, targetPLL, 250, 0, 1);
      break;
    case SI5351_3072:  // 3.072MHz
      setupPLL(targetPLL, 27, 2409, 3125);
      setupMultisynth(output, targetPLL, 226, 0, 1);
      break;
    case SI5351_3332:  // 3.332 MHz
      setupPLL(targetPLL, 27, 15534, 15625);
      setupMultisynth(output, targetPLL, 210, 0, 1);
      break;
    case SI5351_3375:                                 // 3.375 MHz
      setupPLLInt(targetPLL, 27);                     // 25MHz * 27 = 675
      setupMultisynth(output, targetPLL, 200, 0, 1);  // 675/200 = 3.375
      break;
    case SI5351_3500:  // 3.5 MHz
      setupPLL(targetPLL, 35, 0, 1);
      setupMultisynth(output, targetPLL, 250, 0, 1);
      break;
    case SI5351_3579:  // 3.57954545 MHz
      setupPLL(targetPLL, 28, 15909, 250000);
      setupMultisynth(output, targetPLL, 196, 0, 1);
      break;
    case SI5351_4000:              // 4MHz
      setupPLLInt(targetPLL, 32);  // 25MHz * 32 = 800
      setupMultisynth(output, targetPLL, 200, 0, 1);
      break;
    case SI5351_4500:  // 4.5 MHz
      setupPLL(targetPLL, 27, 0, 1);
      setupMultisynth(output, targetPLL, 150, 0, 1);
      break;
    case SI5351_5000:
      setupPLLInt(targetPLL, 30);                     // 25MHz * 30 = 750
      setupMultisynth(output, targetPLL, 150, 0, 1);  // 750 / 150 = 5 MHz
      break;
    case SI5351_6000:
      setupPLLInt(targetPLL, 24);                     // 25MHz * 24 = 600
      setupMultisynth(output, targetPLL, 100, 0, 1);  // 600 / 100 = 6 MHz
      break;
    case SI5351_6144:
      setupPLL(targetPLL, 28, 52, 3125);
      setupMultisynth(output, targetPLL, 114, 0, 1);
      break;
    case SI5351_7159:
      setupPLL(targetPLL, 27, 153409, 312500);
      setupMultisynth(output, targetPLL, 96, 0, 1);
      break;
    case SI5351_7670:  // For Mega Drive 7.670453 MHz
      // setupPLL(SI5351_PLL_A, 25, 1, 5);  // 3.579545 MHz
      // setupMultisynth(0, SI5351_PLL_A, 175, 1, 1);
      setupPLL(targetPLL, 27, 0, 1);
      setupMultisynth(output, targetPLL, 88, 0, 1);
      break;
    case SI5351_7987:
      setupPLL(targetPLL, 28, 357, 3125);
      setupMultisynth(output, targetPLL, 88, 0, 1);
      break;
    case SI5351_8000:                                 // 8 MHz
      setupPLLInt(targetPLL, 32);                     // 25MHz * 32 = 800
      setupMultisynth(output, targetPLL, 100, 0, 1);  // 800 / 100 = 8 MHz
      break;
    case SI5351_9000:                                 // 9 MHz
      setupPLLInt(targetPLL, 36);                     // 25MHz * 36 = 900
      setupMultisynth(output, targetPLL, 100, 0, 1);  // 900 / 100 = 9 MHz
      break;
    case SI5351_14318:                               // 14.318180 MHz
      setupPLL(targetPLL, 27, 38352, 78125);         // 25MHz * 27 38352/78125 = 687.272640000
      setupMultisynth(output, targetPLL, 48, 0, 1);  // 687.27264000 / 48 = 14.318180 MHz
      break;
    default:                       // 4MHz
      setupPLLInt(targetPLL, 32);  // 25MHz * 32 = 800
      setupMultisynth(output, targetPLL, 200, 0, 1);
      break;
  }
  switch (output) {
    case 0:
      this->currentFreq0 = newFreq;
      break;
    case 1:
      this->currentFreq1 = newFreq;
      break;
  }
}

SI5351_cls SI5351;
