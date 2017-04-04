/*********************************************************************************************
 * (c) Frank DD4WH 2017_04_04
 *  
 * "TEENSY CONVOLUTION SDR" 
 * 
 * SOFTWARE FOR A FAST CONVOLUTION-BASED RADIO
 * e
 * HARDWARE NEEDED:
 * - simple quadrature sampling detector board producing baseband IQ signals (Softrock, Elektor SDR etc.)
 * (IQ boards with up to 192kHz bandwidth supported)
 * - Teensy audio board
 * - Teensy 3.6 (No, Teensy 3.1/3.2/3.5 not supported)
 * HARDWARE OPTIONAL:
 * - Preselection: switchable RF lowpass or bandpass filter
 * - digital step attenuator: PE4306 used in my setup
 * 
 * 
 * SOFTWARE:
 * - FFT Fast Convolution = Digital Convolution
 * - with overlap - save = overlap-discard
 * - dynamically creates FIR coefficients  
 * 
 * - in floating point 32bit 
 * - tested on Teensy 3.6
 * - compile with 180MHz F_CPU, other speeds not supported
 * 
 * Part of the evolution of this project has been documented here:
 * https://forum.pjrc.com/threads/40188-Fast-Convolution-filtering-in-floating-point-with-Teensy-3-6/page2
 * 
 * FEATURES
 * - 12kHz to 30MHz Receive PLUS 76 - 108MHz: undersampling-by-5 with reduced sensitivity
 * - I & Q - correction in software
 * - efficient frequency translation without multiplication
 * - efficient spectrum display using a 256 point FFT on the first 256 samples of every 4096 sample-cycle 
 * - efficient AM demodulation with ARM functions
 * - efficient DC elimination after AM demodulation
 * - implemented nine different AM demodulation algorithms for comparison
 * - real SAM - synchronous AM demodulation with phase determination by atan2 implemented
 * - Stereo-SAM and sideband-selected SAM
 * - sample rate from 48k to 192k and decimation-by-8 for efficient realtime calculations
 * - spectrum Zoom function 1x, 2x, 4x, 8x, 16x
 * - Automatic gain control (high end algorithm by Warren Pratt, wdsp)
 * - plays MP3 and M4A (iTunes files) from SD card with the awesome lib by Frank Bösing
 * - automatic IQ amplitude and phase imbalance correction
 * - dynamic frequency indicator figures and graticules on spectrum display x-axis 
 * - kind of menu system now working with many variables that can be set by the two encoders 
 * - EEPROM save & load of important settings
 * - wideband FM demodulation with deemphasis
 * - automatic codec gain adjustment depending on the sample input level
 * - spectrum display AGC to allow display of very small signals
 * - spectrum display in WFM activated (alpha version . . .)
 * - optimized automatic test whether mirror rejection is working - if not, codec is restarted automatically until we have working mirror rejection
 * - display mirror rejection check ("IQtest" in red box)
 * - activated codec 5-band graphic equalizer
 * - added digital attenuator PE4306 bit-banging SPI control [0 -31dB attenuation possible]
 * - added backlight control for TFT in the menu
 * - added analog gain display
 * - fixed major bug associated with too small "string" variables for printing, leading to annoying audio clicks
 * - STEREO FM reception implemented
 *  
 * TODO:
 * - implement optional bandpass filtering
 * - implement manual notch filters in the frequency domain
 * - finetune AGC parameters and make AGC HANG TIME, AGC HANG THRESHOLD and AGC HANG DECAY user-adjustable
 * - record and playback IQ audio stream ;-)
 * - read stations´ frequencies from SD card and display station names when tuned to a frequency
 * - filter bandwidth limiting dependent on sample rate
 * 
 * some parts of the code modified from and/or inspired by
 * my good old Teensy SDR (rheslip & DD4WH): https://github.com/DD4WH/Teensy-SDR-Rx [GNU GPL]
 * mcHF (KA7OEI, DF8OE, DB4PLE, DD4WH): https://github.com/df8oe/mchf-github/ [GNU GPL]
 * libcsdr (András Retzler): https://github.com/simonyiszk/csdr [BSD / GPL]
 * wdsp (Warren Pratt): http://svn.tapr.org/repos_sdr_hpsdr/trunk/W5WC/PowerSDR_HPSDR_mRX_PS/Source/wdsp/ [GPL GNU]
 * Wheatley (2011): cuteSDR https://github.com/satrian/cutesdr-se [BSD] 
 * sample-rate-change-on-the-fly code by Frank Bösing [MIT]
 * GREAT THANKS FOR ALL THE HELP AND INPUT BY WALTER, WMXZ ! 
 * Audio queue optimized by Pete El Supremo 2016_10_27, thanks Pete!
 * An important hint on the implementation came from Alberto I2PHD, thanks for that!
 * Thanks to Brian, bmillier for helping with codec restart code for the SGTL 5000 codec in the Teensy audio board!
 * 
 * Audio processing in float32_t with the NEW ARM CMSIS lib, --> https://forum.pjrc.com/threads/40590-Teensy-Convolution-SDR-(Software-Defined-Radio)?p=129081&viewfull=1#post129081
 * 
 *********************************************************************************************************************************** 
 * 
 * GNU GPL LICENSE v3 
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 * 
 ************************************************************************************************************************************/

#include <Time.h>
#include <TimeLib.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Metro.h>
#include "font_Arial.h"
#include <ILI9341_t3.h>
#include <arm_math.h>
#include <arm_const_structs.h>
#include <si5351.h>
#include <Encoder.h>
#include <EEPROM.h>
#include <Bounce.h>
#include <play_sd_mp3.h> //mp3 decoder
#include <play_sd_aac.h> // AAC decoder

time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

// Settings for the hardware QSD
// Joris PCB uses a 27MHz crystal and CLOCK 2 output
// Elektor SDR PCB uses a 25MHz crystal and the CLOCK 1 output
//#define Si_5351_clock  SI5351_CLK1 
//#define Si_5351_crystal 25000000  
#define Si_5351_clock  SI5351_CLK2
#define Si_5351_crystal 27000000

unsigned long long calibration_factor = 1000000000 ;// 10002285;
long calibration_constant = -8000; // this is for the Joris PCB !
//long calibration_constant = 108000; // this is for the Elektor PCB !
unsigned long long hilfsf;

// Optical Encoder connections
Encoder tune      (16, 17);
Encoder filter    (1, 2);
Encoder encoder3  (5, 4); //(26, 28);

Si5351 si5351;
#define MASTER_CLK_MULT  4  // QSD frontend requires 4x clock

#define BACKLIGHT_PIN   3
#define TFT_DC          20
#define TFT_CS          21
#define TFT_RST         32  // 255 = unused. connect to 3.3V
#define TFT_MOSI        7
#define TFT_SCLK        14
#define TFT_MISO        12
// pins for digital attenuator board PE4306
#define ATT_LE          24
#define ATT_DATA        25
#define ATT_CLOCK       28

ILI9341_t3 tft = ILI9341_t3(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK, TFT_MISO);

// push-buttons
#define   BUTTON_1_PIN      33
#define   BUTTON_2_PIN      34
#define   BUTTON_3_PIN      35
#define   BUTTON_4_PIN      36
#define   BUTTON_5_PIN      38 // this is the pushbutton pin of the tune encoder
#define   BUTTON_6_PIN       0 // this is the pushbutton pin of the filter encoder
#define   BUTTON_7_PIN      37 // this is the menu button pin
#define   BUTTON_8_PIN       8  //27 // this is the pushbutton pin of encoder 3


Bounce button1 = Bounce(BUTTON_1_PIN, 50); //
Bounce button2 = Bounce(BUTTON_2_PIN, 50); //
Bounce button3 = Bounce(BUTTON_3_PIN, 50);//
Bounce button4 = Bounce(BUTTON_4_PIN, 50);//
Bounce button5 = Bounce(BUTTON_5_PIN, 50); //
Bounce button6 = Bounce(BUTTON_6_PIN, 50); // 
Bounce button7 = Bounce(BUTTON_7_PIN, 50); // 
Bounce button8 = Bounce(BUTTON_8_PIN, 50); // 

Metro five_sec=Metro(2000); // Set up a Metro
Metro ms_500 = Metro(500); // Set up a Metro
Metro encoder_check = Metro(100); // Set up a Metro
//Metro dbm_check = Metro(25); 
uint8_t wait_flag = 0;
const uint8_t Band1 = 31; // band selection pins for LPF relays, used with 2N7000: HIGH means LPF is activated
const uint8_t Band2 = 30; // always use only one LPF with HIGH, all others have to be LOW
const uint8_t Band3 = 27;
const uint8_t Band4 = 29; // 29: > 5.4MHz
const uint8_t Band5 = 26; // LW

// this audio comes from the codec by I2S2
AudioInputI2S            i2s_in; 
           
AudioRecordQueue         Q_in_L;    
AudioRecordQueue         Q_in_R;    

AudioPlaySdMp3           playMp3; 
AudioPlaySdAac           playAac; 
AudioMixer4              mixleft;
AudioMixer4              mixright;
AudioPlayQueue           Q_out_L; 
AudioPlayQueue           Q_out_R; 
AudioOutputI2S           i2s_out;           

AudioConnection          patchCord1(i2s_in, 0, Q_in_L, 0);
AudioConnection          patchCord2(i2s_in, 1, Q_in_R, 0);

//AudioConnection          patchCord3(Q_out_L, 0, i2s_out, 1);
//AudioConnection          patchCord4(Q_out_R, 0, i2s_out, 0);
AudioConnection          patchCord3(Q_out_L, 0, mixleft, 0);
AudioConnection          patchCord4(Q_out_R, 0, mixright, 0);
AudioConnection          patchCord5(playMp3, 0, mixleft, 1);
AudioConnection          patchCord6(playMp3, 1, mixright, 1);
AudioConnection          patchCord7(playAac, 0, mixleft, 2);
AudioConnection          patchCord8(playAac, 1, mixright, 2);
AudioConnection          patchCord9(mixleft, 0,  i2s_out, 1);
AudioConnection          patchCord10(mixright, 0, i2s_out, 0);

AudioControlSGTL5000     sgtl5000_1;     

int idx_t = 0;
int idx = 0;
int64_t sum;
float32_t mean;
int n_L;
int n_R;
long int n_clear;
float32_t notch_amp[1024];
//float32_t FFT_magn[4096];
float32_t FFT_spec[256];
float32_t FFT_spec_old[256];
int16_t pixelnew[256];
int16_t pixelold[256];
float32_t LPF_spectrum = 0.2;
float32_t spectrum_display_scale = 30.0; // 30.0
uint8_t show_spectrum_flag = 1;
int16_t spectrum_brightness = 256;
#define SPECTRUM_ZOOM_MIN         0
#define SPECTRUM_ZOOM_1           0
#define SPECTRUM_ZOOM_2           1
#define SPECTRUM_ZOOM_4           2
#define SPECTRUM_ZOOM_8           3
#define SPECTRUM_ZOOM_16          4
#define SPECTRUM_ZOOM_32          5
#define SPECTRUM_ZOOM_64          6
#define SPECTRUM_ZOOM_128         7
#define SPECTRUM_ZOOM_256         8
#define SPECTRUM_ZOOM_512         9
#define SPECTRUM_ZOOM_1024        10
#define SPECTRUM_ZOOM_2048        11
#define SPECTRUM_ZOOM_4096        12
#define SPECTRUM_ZOOM_MAX         12
     
int8_t spectrum_zoom = SPECTRUM_ZOOM_2;

// Text and position for the FFT spectrum display scale

#define SAMPLE_RATE_MIN               6
#define SAMPLE_RATE_8K                0
#define SAMPLE_RATE_11K               1
#define SAMPLE_RATE_16K               2  
#define SAMPLE_RATE_22K               3
#define SAMPLE_RATE_32K               4
#define SAMPLE_RATE_44K               5
#define SAMPLE_RATE_48K               6
#define SAMPLE_RATE_88K               7
#define SAMPLE_RATE_96K               8
#define SAMPLE_RATE_100K              9
#define SAMPLE_RATE_176K              10
#define SAMPLE_RATE_192K              11
#define SAMPLE_RATE_MAX               11

//uint8_t sr =                     SAMPLE_RATE_96K;
uint8_t SAMPLE_RATE =            SAMPLE_RATE_96K; 

typedef struct SR_Descriptor
{
    const uint8_t SR_n;
    const uint32_t rate;
    const char* const text;
    const char* const f1;
    const char* const f2;
    const char* const f3;
    const char* const f4;
    const float32_t x_factor;
    const uint8_t x_offset;
} SR_Desc;
const SR_Descriptor SR [12] =
{
    //   SR_n , rate, text, f1, f2, f3, f4, x_factor = pixels per f1 kHz in spectrum display
    {  SAMPLE_RATE_8K, 8000,  "  8k", " 1", " 2", " 3", " 4", 64.0, 11}, // not OK
    {  SAMPLE_RATE_11K, 11025, " 11k", " 1", " 2", " 3", " 4", 43.1, 17}, // not OK
    {  SAMPLE_RATE_16K, 16000, " 16k",  " 4", " 4", " 8", "12", 64.0, 1}, // OK
    {  SAMPLE_RATE_22K, 22050, " 22k",  " 5", " 5", "10", "15", 58.05, 6}, // OK 
    {  SAMPLE_RATE_32K, 32000,  " 32k", " 5", " 5", "10", "15", 40.0, 24}, // OK, one more indicator? 
    {  SAMPLE_RATE_44K, 44100,  " 44k", "10", "10", "20", "30", 58.05, 6}, // OK
    {  SAMPLE_RATE_48K, 48000,  " 48k", "10", "10", "20", "30", 53.33, 11}, // OK
    {  SAMPLE_RATE_88K, 88200,  " 88k", "20", "20", "40", "60", 58.05, 6}, // OK
    {  SAMPLE_RATE_96K, 96000,  " 96k", "20", "20", "40", "60", 53.33, 12}, // OK 
    {  SAMPLE_RATE_100K, 100000,  "100k", "20", "20", "40", "60", 53.33, 12}, // NOT OK 
    {  SAMPLE_RATE_176K, 176400,  "176k", "40", "40", "80", "120", 58.05, 6}, // OK
    {  SAMPLE_RATE_192K, 192000,  "192k", "40", "40", "80", "120", 53.33, 12} // not OK
};    
int32_t IF_FREQ = SR[SAMPLE_RATE].rate / 4;     // IF (intermediate) frequency
#define F_MAX 3600000000
#define F_MIN 1200000

ulong samp_ptr = 0;

const int myInput = AUDIO_INPUT_LINEIN;

float32_t IQ_amplitude_correction_factor = 1.0038;
float32_t IQ_phase_correction_factor =  0.0058;
// experiment: automatic IQ imbalance correction
// Chang, R. C.-H. & C.-H. Lin (2010): Implementation of carrier frequency offset and 
//     IQ imbalance copensation in OFDM systems. - Int J Electr Eng 17(4): 251-259.
float32_t K_est = 1.0;
float32_t K_est_old = 0.0;
float32_t K_est_mult = 1.0 / K_est;
float32_t P_est = 0.0;
float32_t P_est_old = 0.0;
float32_t P_est_mult = 1.0 / (sqrtf(1.0 - P_est * P_est));
float32_t Q_sum = 0.0;
float32_t I_sum = 0.0;
float32_t IQ_sum = 0.0;
uint8_t   IQ_state = 1;
uint32_t n_para = 512;
uint32_t IQ_counter = 0;
float32_t K_dirty = 0.868;
float32_t P_dirty = 1.0;
int8_t auto_IQ_correction = 1;
#define CHANG         0
#define MOSELEY       1
float32_t teta1 = 0.0;
float32_t teta2 = 0.0;
float32_t teta3 = 0.0;
float32_t teta1_old = 0.0;
float32_t teta2_old = 0.0;
float32_t teta3_old = 0.0;
float32_t M_c1 = 0.0;
float32_t M_c2 = 0.0;
uint8_t codec_restarts = 0;
uint32_t twinpeaks_counter = 0;
//uint8_t IQ_auto_counter = 0;
uint8_t twinpeaks_tested = 2; // initial value --> 2 !! 
//float32_t asin_sum = 0.0;
//uint16_t asin_N = 0;
uint8_t write_analog_gain = 0;

#define BUFFER_SIZE 128

// complex FFT with the new library CMSIS V4.5
const static arm_cfft_instance_f32 *S;

// create coefficients on the fly for custom filters
// and let the algorithm define which FFT size you need
// input variables by the user
// Fpass, Fstop, Astop (stopband attenuation)  
// fixed: sample rate, scale = 1.0 ???
// tested & working samplerates (processor load):
// 96k (46%), 88.2k, 48k, 44.1k (18%), 32k (13.6%),

float32_t LP_Fpass = 3500;
uint32_t LP_F_help = 3500;
float32_t LP_Fstop = 3600;
float32_t LP_Astop = 90;
//float32_t LP_Fpass_old = 0.0;

//int RF_gain = 0;
int audio_volume = 50;
//int8_t bass_gain_help = 0;
//int8_t midbass_gain_help = 30;
//int8_t mid_gain_help = 0;
//int8_t midtreble_gain_help = -10;
//int8_t treble_gain_help = -40;
float32_t bass = 0.0;
float32_t midbass = 0.0;
float32_t mid = 0.0;
float32_t midtreble = -0.1;
float32_t treble = - 0.4;
float32_t stereo_factor = 1000.0;
uint8_t half_clip = 0;
uint8_t quarter_clip = 0;
uint8_t auto_codec_gain = 1;
int8_t RF_attenuation = 0;

//float32_t FIR_Coef[2049];
float32_t FIR_Coef[513];
#define MAX_NUMCOEF 513
#define TPI           6.28318530717959f
#define PIH           1.57079632679490f
#define FOURPI        2.0 * TPI
#define SIXPI         3.0 * TPI

uint32_t m_NumTaps = 513;
//const uint32_t FFT_L = 4096;
const float32_t DF = 8.0; // decimation factor
const uint32_t FFT_L = 1024;
uint32_t FFT_length = FFT_L;
uint32_t N_BLOCKS = 32;
uint32_t BUF_N_8 = BUFFER_SIZE * N_BLOCKS / 8;
const uint32_t N_B = 32; //FFT_L / 2 / BUFFER_SIZE;
// decimation by 8 --> 32 / 8 = 4
const uint32_t N_DEC_B = 4; 
float32_t float_buffer_L [BUFFER_SIZE * N_B];
float32_t float_buffer_R [BUFFER_SIZE * N_B];

float32_t FFT_buffer [FFT_L * 2] __attribute__ ((aligned (4)));
float32_t last_sample_buffer_L [BUFFER_SIZE * N_DEC_B];
float32_t last_sample_buffer_R [BUFFER_SIZE * N_DEC_B];
uint8_t flagg = 0;
// complex iFFT with the new library CMSIS V4.5
const static arm_cfft_instance_f32 *iS;
float32_t iFFT_buffer [FFT_L * 2] __attribute__ ((aligned (4)));

// FFT instance for direct calculation of the filter mask
// from the impulse response of the FIR - the coefficients
const static arm_cfft_instance_f32 *maskS;
float32_t FIR_filter_mask [FFT_L * 2] __attribute__ ((aligned (4))); 

const static arm_cfft_instance_f32 *spec_FFT;
float32_t buffer_spec_FFT [512] __attribute__ ((aligned (4))); 

const static arm_cfft_instance_f32 *NR_FFT;
float32_t buffer_NR_FFT [512] __attribute__ ((aligned (4))); 

const static arm_cfft_instance_f32 *NR_iFFT;
float32_t buffer_NR_iFFT [512] __attribute__ ((aligned (4))); 

//////////////////////////////////////
// constants for display
//////////////////////////////////////
int spectrum_y = 120; // upper edge
int spectrum_x = 10;
int spectrum_height = 90;
int spectrum_pos_centre_f = 64;
#define pos_x_smeter 11 //5
#define pos_y_smeter (spectrum_y - 12) //94
#define s_w 10
uint8_t freq_flag[2] = {0,0};
uint8_t digits_old [2][10] =
{ {9,9,9,9,9,9,9,9,9,9},
 {9,9,9,9,9,9,9,9,9,9} };
uint8_t erase_flag = 0; 
uint8_t WFM_spectrum_flag = 4;
uint16_t leave_WFM = 4;
#define pos 50 // position of spectrum display, has to be < 124
#define pos_version 119 // position of version number printing
#define pos_x_tunestep 100
#define pos_y_tunestep 119 // = pos_y_menu 91
int pos_x_frequency = 12;// !!!5; //21 //100
int pos_y_frequency = 48; //52 //61  //119
#define notchpos 2
#define notchL 15
#define notchColour ILI9341_YELLOW
int pos_centre_f = 98; // 
int oldnotchF = 10000;
float32_t bin_BW = 0.0001220703125 * SR[SAMPLE_RATE].rate;
float32_t bin = 2000.0 / bin_BW;
float32_t notches[10] = {500.0, 1000.0, 1500.0, 2000.0,2500.0, 3000.0,3500.0, 4000.0,4500.0,5000.0};


uint8_t sch = 0;
float32_t dbm = -145.0;
float32_t dbmhz = -145.0;
float32_t m_AttackAvedbm = 0.0;
float32_t m_DecayAvedbm = 0.0;
float32_t m_AverageMagdbm = 0.0;
float32_t m_AttackAvedbmhz = 0.0;
float32_t m_DecayAvedbmhz = 0.0;
float32_t m_AverageMagdbmhz = 0.0;
// ALPHA = 1 - e^(-T/Tau), T = 0.02s (because dbm routine is called every 20ms!)
// Tau     ALPHA
//  10ms   0.8647
//  30ms   0.4866
//  50ms   0.3297
// 100ms   0.1812
// 500ms   0.0391
float32_t m_AttackAlpha = 0.2;
float32_t m_DecayAlpha  = 0.05;
int16_t pos_x_dbm = pos_x_smeter + 170;
int16_t pos_y_dbm = pos_y_smeter - 7;
#define DISPLAY_S_METER_DBM       0
#define DISPLAY_S_METER_DBMHZ     1
uint8_t display_dbm = DISPLAY_S_METER_DBM;
uint8_t dbm_state = 0;

// out of the nine implemented AM detectors, only
// two proved to be of acceptable quality:
// AM2 and AM_ME2
// however, SAM outperforms all demodulators ;-)
#define       DEMOD_MIN           0
#define       DEMOD_USB           0
#define       DEMOD_LSB           1
//#define       DEMOD_AM1           2
#define       DEMOD_AM2           2
//#define       DEMOD_AM3           4
//#define       DEMOD_AM_AE1        5
//#define       DEMOD_AM_AE2        6
//#define       DEMOD_AM_AE3        7
//#define       DEMOD_AM_ME1        8
#define       DEMOD_AM_ME2        3
//#define       DEMOD_AM_ME3       10
#define       DEMOD_SAM          4 // synchronous AM demodulation
#define       DEMOD_SAM_USB      5 // synchronous AM demodulation
#define       DEMOD_SAM_LSB      6 // synchronous AM demodulation
#define       DEMOD_SAM_STEREO   7 // SAM, with pseude-stereo effect
#define       DEMOD_WFM          8 
#define       DEMOD_DCF77        9 // set the clock with the time signal station DCF77
#define       DEMOD_AUTOTUNE     10 // AM demodulation with autotune function
#define       DEMOD_STEREO_DSB   17 // double sideband: SSB without eliminating the opposite sideband
#define       DEMOD_DSB          18 // double sideband: SSB without eliminating the opposite sideband
#define       DEMOD_STEREO_LSB   19 
#define       DEMOD_AM_USB       20 // AM demodulation with lower sideband suppressed
#define       DEMOD_AM_LSB       21 // AM demodulation with upper sideband suppressed
#define       DEMOD_STEREO_USB   22 
#define       DEMOD_MAX          8
#define BAND_LW     0   
#define BAND_MW     1
#define BAND_120M   2
#define BAND_90M    3
#define BAND_75M    4    
#define BAND_60M    5
#define BAND_49M    6
#define BAND_41M    7
#define BAND_31M    8
#define BAND_25M    9
#define BAND_22M   10
#define BAND_19M   11
#define BAND_16M   12
#define BAND_15M   13
#define BAND_13M   14
#define BAND_11M   15

#define FIRST_BAND BAND_LW
#define LAST_BAND  BAND_15M
#define NUM_BANDS  16
#define STARTUP_BAND BAND_MW    // 

uint8_t autotune_wait = 10;

struct band {
  unsigned long long freq; // frequency in Hz
  String name; // name of band
  int mode; 
  int bandwidthU;
  int bandwidthL;
  int RFgain; 
};
// f, band, mode, bandwidth, RFgain
struct band bands[NUM_BANDS] = {
//  7750000 ,"VLF", DEMOD_AM, 3600,3600,0,  
  22500000,"LW", DEMOD_SAM, 3600,3600,0,
  63900000,"MW",  DEMOD_SAM, 3600,3600,0,
  248500000,"120M",  DEMOD_SAM, 3600,3600,0,
  350000000,"90M",  DEMOD_LSB, 3600,3600,6,
  390500000,"75M",  DEMOD_SAM, 3600,3600,4,
  502500000,"60M",  DEMOD_SAM, 3600,3600,7,
  593200000,"49M",  DEMOD_SAM, 3600,3600,0,
  712000000,"41M",  DEMOD_SAM, 3600,3600,0,
  942000000,"31M",  DEMOD_SAM, 3600,3600,0,
  1173500000,"25M", DEMOD_SAM, 3600,3600,2,
  1357000000,"22M", DEMOD_SAM, 3600,3600,2,
  1514000000,"19M", DEMOD_SAM, 3600,3600,4,
  1748000000,"16M", DEMOD_SAM, 3600,3600,5,
  1890000000,"15M", DEMOD_SAM, 3600,3600,5,
  2145000000,"13M", DEMOD_SAM, 3600,3600,6,
  2567000000,"11M", DEMOD_SAM, 3600,3600,6
};
int band = STARTUP_BAND;

#define TUNE_STEP_MIN   0
#define TUNE_STEP1   0    // shortwave
#define TUNE_STEP2   1   // fine tuning
#define TUNE_STEP3   2    //
#define TUNE_STEP4   3    //
#define TUNE_STEP_MAX 3 
#define first_tunehelp 1
#define last_tunehelp 3
uint8_t tune_stepper = 0;
uint32_t tunestep = 5000; //TUNE_STEP1;
String tune_text = "Fast Tune";
uint8_t autotune_flag = 0;
 
int8_t first_block = 1;
uint32_t i = 0;
int32_t FFT_shift = 2048; // which means 1024 bins!

// used for AM demodulation
float32_t audiotmp = 0.0f;
float32_t w = 0.0f;
float32_t wold = 0.0f;
float32_t last_dc_level = 0.0f;
uint8_t audio_flag = 1;

typedef struct DEMOD_Descriptor
{   const uint8_t DEMOD_n;
    const char* const text;
} DEMOD_Desc;
const DEMOD_Descriptor DEMOD [15] =
{
    //   DEMOD_n, name
    {  DEMOD_USB, " USB  "}, 
    {  DEMOD_LSB, " LSB  "}, 
//    {  DEMOD_AM1,  " AM 1 "}, 
    {  DEMOD_AM2,  " AM 2 "}, 
//    {  DEMOD_AM3,  " AM 3 "}, 
//    {  DEMOD_AM_AE1,  "AM-AE1"}, 
//    {  DEMOD_AM_AE2,  "AM-AE2"}, 
//    {  DEMOD_AM_AE3,  "AM-AE3"}, 
//    {  DEMOD_AM_ME1,  "AM-ME1"}, 
    {  DEMOD_AM_ME2,  "AM-ME2"}, 
//    {  DEMOD_AM_ME3,  "AM-ME3"}, 
    {  DEMOD_SAM, " SAM  "},
    {  DEMOD_SAM_USB, "SAM-U "},
    {  DEMOD_SAM_LSB, "SAM-L "},
    {  DEMOD_SAM_STEREO, "SAM-St"},
    {  DEMOD_WFM, " WFM "},
    {  DEMOD_STEREO_LSB, "StLSB "}, 
    {  DEMOD_STEREO_USB, "StUSB "}, 
    {  DEMOD_DCF77, "DCF 77"}, 
    {  DEMOD_AUTOTUNE, "AUTO_T"}, 
    {  DEMOD_DSB, " DSB  "}, 
    {  DEMOD_STEREO_DSB, "StDSB "},
};

// Menus !
#define MENU_FILTER_BANDWIDTH             0
#define MENU_SPECTRUM_ZOOM                1
#define MENU_SAMPLE_RATE                  2
#define MENU_SAVE_EEPROM                  3
#define MENU_LOAD_EEPROM                  4
#define MENU_PLAYER                       5
#define MENU_LPF_SPECTRUM                 6
#define MENU_IQ_AUTO                      7
#define MENU_IQ_AMPLITUDE                 8
#define MENU_IQ_PHASE                     9 
#define MENU_CALIBRATION_FACTOR           10
#define MENU_CALIBRATION_CONSTANT         11
#define MENU_TIME_SET                     12
#define MENU_DATE_SET                     13
#define MENU_RESET_CODEC                  14
#define MENU_SPECTRUM_BRIGHTNESS          15
#define MENU_SHOW_SPECTRUM                16

#define first_menu                        0
#define last_menu                         16
#define start_menu                        0
int8_t Menu_pointer =                    start_menu;

#define MENU_VOLUME                       17
#define MENU_RF_GAIN                      18
#define MENU_RF_ATTENUATION               19
#define MENU_BASS                         20
#define MENU_MIDBASS                      21
#define MENU_MID                          22
#define MENU_MIDTREBLE                    23
#define MENU_TREBLE                       24
#define MENU_SPECTRUM_DISPLAY_SCALE       25
#define MENU_SAM_ZETA                     26
#define MENU_SAM_OMEGA                    27
#define MENU_SAM_CATCH_BW                 28
#define MENU_AGC_MODE                     29
#define MENU_AGC_THRESH                   30
#define MENU_AGC_DECAY                    31
#define MENU_AGC_SLOPE                    32
#define MENU_STEREO_FACTOR                33
#define MENU_AGC_HANG_ENABLE              34
#define MENU_AGC_HANG_TIME                35
#define MENU_AGC_HANG_THRESH              36
#define first_menu2                       17
#define last_menu2                        33  
int8_t Menu2 =                           MENU_VOLUME;
uint8_t which_menu = 1;

typedef struct Menu_Descriptor
{
    const uint8_t no; // Menu ID
    const char* const text1; // upper text
    const char* text2; // lower text
    const uint8_t menu2; // 0 = belongs to Menu, 1 = belongs to Menu2
} Menu_D;

Menu_D Menus [last_menu2 + 1] {
{ MENU_FILTER_BANDWIDTH, "  Filter", "   BW  ", 0 },
{ MENU_SPECTRUM_ZOOM, " Spectr", " Zoom ", 0 },
{ MENU_SAMPLE_RATE, "Sample", " Rate ", 0 },
{ MENU_SAVE_EEPROM, " Save ", "Eeprom", 0 },
{ MENU_LOAD_EEPROM, " Load ", "Eeprom", 0 },
{ MENU_PLAYER, "  MP3  ", " Player", 0 },
{ MENU_LPF_SPECTRUM, "Spectr", " LPF  ", 0 },
{ MENU_IQ_AUTO, "  IQ  ", " Auto ", 0 },
{ MENU_IQ_AMPLITUDE, "  IQ  ", " gain ", 0 },
{ MENU_IQ_PHASE, "   IQ  ", "  phase ", 0 },
{ MENU_CALIBRATION_FACTOR, "F-calib", "factor", 0 },
{ MENU_CALIBRATION_CONSTANT, "F-calib", "const", 0 },
{ MENU_TIME_SET, " Time ", " Set  ", 0},
{ MENU_DATE_SET, " Date ", " Set  ", 0},
{ MENU_RESET_CODEC, " Reset", " codec ", 0},
{ MENU_SPECTRUM_BRIGHTNESS, "Display", "  dim ", 0},
{ MENU_SHOW_SPECTRUM, " Show ", " spectr", 0},
{ MENU_VOLUME, "Volume", "      ", 1},
{ MENU_RF_GAIN, "   RF  ", "  gain ", 1},
{ MENU_RF_ATTENUATION, "   RF  ", " atten", 1},
{ MENU_BASS, "  Bass ", "  gain ", 1},
{ MENU_MIDBASS, "MidBas", "  gain ", 1},
{ MENU_MID, "  Mids ", "  gain ", 1},
{ MENU_MIDTREBLE, "Midtreb", "  gain ", 1},
{ MENU_TREBLE, "Treble", "  gain ", 1},
{ MENU_SPECTRUM_DISPLAY_SCALE, "spectr", " scale", 1},
{ MENU_SAM_ZETA, "  SAM  ", "  zeta ", 1},
{ MENU_SAM_OMEGA, "  SAM  ", " omega ", 1},
{ MENU_SAM_CATCH_BW, "  SAM  ", "catchB", 1},
{ MENU_AGC_MODE, "  AGC  ", "  mode  ", 1},
{ MENU_AGC_THRESH, "  AGC  ", " thresh ", 1},
{ MENU_AGC_DECAY, "  AGC  ", " decay ", 1},
{ MENU_AGC_SLOPE, "  AGC  ", " slope  ", 1},
{ MENU_STEREO_FACTOR, "Stereo", "factor", 1}
};
uint8_t eeprom_saved = 0;
uint8_t eeprom_loaded = 0;

// SD card & MP3 playing
int track;
int tracknum;
int trackext[255]; // 0= nothing, 1= mp3, 2= aac, 3= wav.
String tracklist[255];
File root;
char playthis[15];
boolean trackchange;
boolean paused;
int eeprom_adress = 1900;

uint8_t iFFT_flip = 0;

// AGC
#define MAX_SAMPLE_RATE     (12000.0)
#define MAX_N_TAU           (8)
#define MAX_TAU_ATTACK      (0.01)
#define RB_SIZE       (int) (MAX_SAMPLE_RATE * MAX_N_TAU * MAX_TAU_ATTACK + 1)

int8_t AGC_mode = 1;
int pmode = 1; // if 0, calculate magnitude by max(|I|, |Q|), if 1, calculate sqrtf(I*I+Q*Q)
float32_t out_sample[2];
float32_t abs_out_sample;
float32_t tau_attack;
float32_t tau_decay;
int n_tau;
float32_t max_gain;
float32_t var_gain;
float32_t fixed_gain = 1.0;
float32_t max_input;
float32_t out_targ;
float32_t tau_fast_backaverage;
float32_t tau_fast_decay;
float32_t pop_ratio;
uint8_t hang_enable;
float32_t tau_hang_backmult;
float32_t hangtime;
float32_t hang_thresh;
float32_t tau_hang_decay;
float32_t ring[RB_SIZE * 2];
float32_t abs_ring[RB_SIZE];
//assign constants
int ring_buffsize = RB_SIZE;
//do one-time initialization
int out_index = -1;
float32_t ring_max = 0.0;
float32_t volts = 0.0;
float32_t save_volts = 0.0;
float32_t fast_backaverage = 0.0;
float32_t hang_backaverage = 0.0;
int hang_counter = 0;
uint8_t decay_type = 0;
uint8_t state = 0;
int attack_buffsize;
uint32_t in_index;
float32_t attack_mult;
float32_t decay_mult;
float32_t fast_decay_mult;
float32_t fast_backmult;
float32_t onemfast_backmult;
float32_t out_target;
float32_t min_volts;
float32_t inv_out_target;
float32_t tmp;
float32_t slope_constant;
float32_t inv_max_input;
float32_t hang_level;
float32_t hang_backmult;
float32_t onemhang_backmult;
float32_t hang_decay_mult;
int agc_thresh = -10;
int agc_slope = 100;
int agc_decay = 100;
uint8_t agc_action = 0;
uint8_t agc_switch_mode = 0;

// new synchronous AM PLL & PHASE detector
// wdsp Warren Pratt, 2016
float32_t Sin = 0.0;
float32_t Cos = 0.0;
float32_t pll_fmax = +4000.0;
int zeta_help = 65;
float32_t zeta = (float32_t)zeta_help / 100.0; // PLL step response: smaller, slower response 1.0 - 0.1
float32_t omegaN = 200.0; // PLL bandwidth 50.0 - 1000.0
  
  //pll
float32_t omega_min = TPI * - pll_fmax * DF / SR[SAMPLE_RATE].rate;
float32_t omega_max = TPI * pll_fmax * DF / SR[SAMPLE_RATE].rate;
float32_t g1 = 1.0 - exp(-2.0 * omegaN * zeta * DF / SR[SAMPLE_RATE].rate);
float32_t g2 = - g1 + 2.0 * (1 - exp(- omegaN * zeta * DF / SR[SAMPLE_RATE].rate) * cosf(omegaN * DF / SR[SAMPLE_RATE].rate * sqrtf(1.0 - zeta * zeta)));
float32_t phzerror = 0.0;
float32_t det = 0.0;
float32_t fil_out = 0.0;
float32_t del_out = 0.0;
float32_t omega2 = 0.0;

  //fade leveler
float32_t tauR = 0.02; // original 0.02;
float32_t tauI = 1.4; // original 1.4;  
float32_t dc = 0.0;
float32_t dc_insert = 0.0;
float32_t dcu = 0.0;
float32_t dc_insertu = 0.0;
float32_t mtauR = exp(- DF / (SR[SAMPLE_RATE].rate * tauR)); 
float32_t onem_mtauR = 1.0 - mtauR;
float32_t mtauI = exp(- DF / (SR[SAMPLE_RATE].rate * tauI)); 
float32_t onem_mtauI = 1.0 - mtauI;  
uint8_t fade_leveler = 1;
uint8_t WDSP_SAM = 1;
#define SAM_PLL_HILBERT_STAGES 7
#define OUT_IDX   (3 * SAM_PLL_HILBERT_STAGES)
float32_t c0[SAM_PLL_HILBERT_STAGES];
float32_t c1[SAM_PLL_HILBERT_STAGES];
float32_t ai, bi, aq, bq;
float32_t ai_ps, bi_ps, aq_ps, bq_ps;
float32_t a[3 * SAM_PLL_HILBERT_STAGES + 3];     // Filter a variables
float32_t b[3 * SAM_PLL_HILBERT_STAGES + 3];     // Filter b variables
float32_t c[3 * SAM_PLL_HILBERT_STAGES + 3];     // Filter c variables
float32_t d[3 * SAM_PLL_HILBERT_STAGES + 3];     // Filter d variables
float32_t dsI;             // delayed sample, I path
float32_t dsQ;             // delayed sample, Q path
float32_t corr[2];
float32_t audio;
float32_t audiou;
int j,k;
float32_t SAM_carrier = 0.0;
float32_t SAM_lowpass = 0.0;
float32_t SAM_carrier_freq_offset = 0.0;
uint16_t  SAM_display_count = 0;

//***********************************************************************
bool timeflag = 0;
const int8_t pos_x_date = 14;
const int8_t pos_y_date = 68;
const int16_t pos_x_time = 225; // 14;
const int16_t pos_y_time = pos_y_frequency; //114;
int helpmin; // definitions for time and date adjust - Menu
int helphour;
int helpday;
int helpmonth;
int helpyear;
int helpsec;
uint8_t hour10_old;
uint8_t hour1_old;
uint8_t minute10_old;
uint8_t minute1_old;
uint8_t second10_old;
uint8_t second1_old;
uint8_t precision_flag = 0;
int8_t mesz = -1;
int8_t mesz_old = 0;

const float displayscale = 0.25;
float32_t display_offset = -10.0;

//const char* const Days[7] = { "Samstag", "Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag"};
const char* const Days[7] = { "Saturday", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday"};

/****************************************************************************************
 *  init decimation and interpolation
 ****************************************************************************************/

// decimation with FIR lowpass
// pState points to a state array of size numTaps + blockSize - 1
arm_fir_decimate_instance_f32 FIR_dec1_I;
float32_t FIR_dec1_I_state [50 + BUFFER_SIZE * N_B - 1];
arm_fir_decimate_instance_f32 FIR_dec1_Q;
float32_t FIR_dec1_Q_state [50 + BUFFER_SIZE * N_B - 1];
float32_t FIR_dec1_coeffs[50];

arm_fir_decimate_instance_f32 FIR_dec2_I;
float32_t FIR_dec2_I_state [88 + BUFFER_SIZE * N_B / 4 - 1];
arm_fir_decimate_instance_f32 FIR_dec2_Q;
float32_t FIR_dec2_Q_state [88 + BUFFER_SIZE * N_B / 4 - 1];
float32_t FIR_dec2_coeffs[88];

// interpolation with FIR lowpass
// pState is of length (numTaps/L)+blockSize-1 words where blockSize is the number of input samples processed by each call
arm_fir_interpolate_instance_f32 FIR_int1_I;
float32_t FIR_int1_I_state [(16 / 2) + BUFFER_SIZE * N_B / 8 - 1];
arm_fir_interpolate_instance_f32 FIR_int1_Q;
float32_t FIR_int1_Q_state [(16 / 2) + BUFFER_SIZE * N_B / 8 - 1];
float32_t FIR_int1_coeffs[16];

arm_fir_interpolate_instance_f32 FIR_int2_I;
float32_t FIR_int2_I_state [(16 / 4) + BUFFER_SIZE * N_B / 4 - 1];
arm_fir_interpolate_instance_f32 FIR_int2_Q;
float32_t FIR_int2_Q_state [(16 / 4) + BUFFER_SIZE * N_B / 4 - 1];
float32_t FIR_int2_coeffs[16];

// decimation with FIR lowpass for Zoom FFT
arm_fir_decimate_instance_f32 Fir_Zoom_FFT_Decimate_I;
arm_fir_decimate_instance_f32 Fir_Zoom_FFT_Decimate_Q;
float32_t Fir_Zoom_FFT_Decimate_I_state [4 + BUFFER_SIZE * N_B - 1];
float32_t Fir_Zoom_FFT_Decimate_Q_state [4 + BUFFER_SIZE * N_B - 1];

float32_t Fir_Zoom_FFT_Decimate_coeffs[4];

/****************************************************************************************
 *  init IIR filters
 ****************************************************************************************/
float32_t coefficient_set[5] = {0, 0, 0, 0, 0};
// 2-pole biquad IIR - definitions and Initialisation
const uint32_t N_stages_biquad_lowpass1 = 1;
float32_t biquad_lowpass1_state [N_stages_biquad_lowpass1 * 4];
float32_t biquad_lowpass1_coeffs[5 * N_stages_biquad_lowpass1] = {0,0,0,0,0}; 
arm_biquad_casd_df1_inst_f32 biquad_lowpass1;

const uint32_t N_stages_biquad_WFM = 4;
float32_t biquad_WFM_state [N_stages_biquad_WFM * 4];
float32_t biquad_WFM_coeffs[5 * N_stages_biquad_WFM] = {0,0,0,0,0,  0,0,0,0,0,  0,0,0,0,0,  0,0,0,0,0}; 
arm_biquad_casd_df1_inst_f32 biquad_WFM;

//biquad_WFM_19k
const uint32_t N_stages_biquad_WFM_19k = 1;
float32_t biquad_WFM_19k_state [N_stages_biquad_WFM_19k * 4];
float32_t biquad_WFM_19k_coeffs[5 * N_stages_biquad_WFM_19k] = {0,0,0,0,0}; 
arm_biquad_casd_df1_inst_f32 biquad_WFM_19k;

//biquad_WFM_38k
const uint32_t N_stages_biquad_WFM_38k = 1;
float32_t biquad_WFM_38k_state [N_stages_biquad_WFM_38k * 4];
float32_t biquad_WFM_38k_coeffs[5 * N_stages_biquad_WFM_38k] = {0,0,0,0,0}; 
arm_biquad_casd_df1_inst_f32 biquad_WFM_38k;

// 4-stage IIRs for Zoom FFT, one each for I & Q
const uint32_t IIR_biquad_Zoom_FFT_N_stages = 4;
float32_t IIR_biquad_Zoom_FFT_I_state [IIR_biquad_Zoom_FFT_N_stages * 4];
float32_t IIR_biquad_Zoom_FFT_Q_state [IIR_biquad_Zoom_FFT_N_stages * 4];
arm_biquad_casd_df1_inst_f32 IIR_biquad_Zoom_FFT_I;
arm_biquad_casd_df1_inst_f32 IIR_biquad_Zoom_FFT_Q;
int zoom_sample_ptr = 0;
uint8_t zoom_display = 0;

static float32_t* mag_coeffs[11] =
{
// for Index 0 [1xZoom == no zoom] the mag_coeffs will consist of  a NULL  ptr, since the filter is not going to be used in this  mode!
(float32_t*)NULL,

(float32_t*)(const float32_t[]){
      // 2x magnify - index 1
      // 12kHz, sample rate 48k, 60dB stopband, elliptic
      // a1 and a2 negated! order: b0, b1, b2, a1, a2
      // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
0.228454526413293696,
0.077639329099949764,
0.228454526413293696,
0.635534925142242080,
-0.170083307068779194,

0.436788292542003964,
0.232307972937606161,
0.436788292542003964,
0.365885230717786780,
-0.471769788739400842,

0.535974654742658707,
0.557035600464780845,
0.535974654742658707,
0.125740787233286133,
-0.754725697183384336,

0.501116342273565607,
0.914877831284765408,
0.501116342273565607,
0.013862536615004284,
-0.930973052446900984  },

(float32_t*)(const float32_t[]){
      // 4x magnify - index 2
      // 6kHz, sample rate 48k, 60dB stopband, elliptic
      // a1 and a2 negated! order: b0, b1, b2, a1, a2
      // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
0.182208761527446556,
-0.222492493114674145,
0.182208761527446556,
1.326111070880959810,
-0.468036100821178802,

0.337123762652097259,
-0.366352718812586853,
0.337123762652097259,
1.337053579516321200,
-0.644948386007929031,

0.336163175380826074,
-0.199246162162897811,
0.336163175380826074,
1.354952684569386670,
-0.828032873168141115,

0.178588201750411041,
0.207271695028067304,
0.178588201750411041,
1.386486967455699220,
-0.950935065984588657  },

(float32_t*)(const float32_t[]){
        // 8x magnify - index 3
        // 3kHz, sample rate 48k, 60dB stopband, elliptic
        // a1 and a2 negated! order: b0, b1, b2, a1, a2
        // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
0.185643392652478922,
-0.332064345389014803,
0.185643392652478922,
1.654637402827731090,
-0.693859842743674182,

0.327519300813245984,
-0.571358085216950418,
0.327519300813245984,
1.715375037176782860,
-0.799055553586324407,

0.283656142708241688,
-0.441088976843048652,
0.283656142708241688,
1.778230635987093860,
-0.904453944560528522,

0.079685368654848945,
-0.011231810140649204,
0.079685368654848945,
1.825046003243238070,
-0.973184930412286708  },

(float32_t*)(const float32_t[]){
        // 16x magnify - index 4
      // 1k5, sample rate 48k, 60dB stopband, elliptic
      // a1 and a2 negated! order: b0, b1, b2, a1, a2
      // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
 0.194769868656866380,
 -0.379098413160710079,
 0.194769868656866380,
 1.824436402073870810,
 -0.834877726226893380,

 0.333973874901496770,
-0.646106479315673776,
 0.333973874901496770,
 1.871892825636887640,
-0.893734096124207178,

 0.272903880596429671,
-0.513507745397738469,
 0.272903880596429671,
 1.918161772571113750,
-0.950461788366234739,

 0.053535383722369843,
-0.069683422367188122,
 0.053535383722369843,
 1.948900719896301760,
-0.986288064973853129 },

(float32_t*)(const float32_t[]){
        // 32x magnify - index 5
      // 750Hz, sample rate 48k, 60dB stopband, elliptic
        // a1 and a2 negated! order: b0, b1, b2, a1, a2
        // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
0.201507402588557594,
-0.400273615727755550,
0.201507402588557594,
1.910767558906650840,
-0.913508748356010480,

0.340295203367131205,
-0.674930558961690075,
0.340295203367131205,
1.939398230905991390,
-0.945058078678563840,

0.271859921641011359,
-0.535453706265515361,
0.271859921641011359,
1.966439529620203740,
-0.974705666636711099,

0.047026497485465592,
-0.084562104085501480,
0.047026497485465592,
1.983564238653704900,
-0.993055129539134551 },

(float32_t*)(const float32_t[]){
        // 32x magnify - index 6
      // 750Hz, sample rate 48k, 60dB stopband, elliptic
        // a1 and a2 negated! order: b0, b1, b2, a1, a2
        // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
0.201507402588557594,
-0.400273615727755550,
0.201507402588557594,
1.910767558906650840,
-0.913508748356010480,

0.340295203367131205,
-0.674930558961690075,
0.340295203367131205,
1.939398230905991390,
-0.945058078678563840,

0.271859921641011359,
-0.535453706265515361,
0.271859921641011359,
1.966439529620203740,
-0.974705666636711099,

0.047026497485465592,
-0.084562104085501480,
0.047026497485465592,
1.983564238653704900,
-0.993055129539134551 },

(float32_t*)(const float32_t[]){
        // 32x magnify - index 7
      // 750Hz, sample rate 48k, 60dB stopband, elliptic
        // a1 and a2 negated! order: b0, b1, b2, a1, a2
        // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
0.201507402588557594,
-0.400273615727755550,
0.201507402588557594,
1.910767558906650840,
-0.913508748356010480,

0.340295203367131205,
-0.674930558961690075,
0.340295203367131205,
1.939398230905991390,
-0.945058078678563840,

0.271859921641011359,
-0.535453706265515361,
0.271859921641011359,
1.966439529620203740,
-0.974705666636711099,

0.047026497485465592,
-0.084562104085501480,
0.047026497485465592,
1.983564238653704900,
-0.993055129539134551 },

(float32_t*)(const float32_t[]){
        // 32x magnify - index 8
      // 750Hz, sample rate 48k, 60dB stopband, elliptic
        // a1 and a2 negated! order: b0, b1, b2, a1, a2
        // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
0.201507402588557594,
-0.400273615727755550,
0.201507402588557594,
1.910767558906650840,
-0.913508748356010480,

0.340295203367131205,
-0.674930558961690075,
0.340295203367131205,
1.939398230905991390,
-0.945058078678563840,

0.271859921641011359,
-0.535453706265515361,
0.271859921641011359,
1.966439529620203740,
-0.974705666636711099,

0.047026497485465592,
-0.084562104085501480,
0.047026497485465592,
1.983564238653704900,
-0.993055129539134551 },

(float32_t*)(const float32_t[]){
        // 32x magnify - index 9
      // 750Hz, sample rate 48k, 60dB stopband, elliptic
        // a1 and a2 negated! order: b0, b1, b2, a1, a2
        // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
0.201507402588557594,
-0.400273615727755550,
0.201507402588557594,
1.910767558906650840,
-0.913508748356010480,

0.340295203367131205,
-0.674930558961690075,
0.340295203367131205,
1.939398230905991390,
-0.945058078678563840,

0.271859921641011359,
-0.535453706265515361,
0.271859921641011359,
1.966439529620203740,
-0.974705666636711099,

0.047026497485465592,
-0.084562104085501480,
0.047026497485465592,
1.983564238653704900,
-0.993055129539134551 },

(float32_t*)(const float32_t[]){
        // 32x magnify - index 10
      // 750Hz, sample rate 48k, 60dB stopband, elliptic
        // a1 and a2 negated! order: b0, b1, b2, a1, a2
        // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
0.201507402588557594,
-0.400273615727755550,
0.201507402588557594,
1.910767558906650840,
-0.913508748356010480,

0.340295203367131205,
-0.674930558961690075,
0.340295203367131205,
1.939398230905991390,
-0.945058078678563840,

0.271859921641011359,
-0.535453706265515361,
0.271859921641011359,
1.966439529620203740,
-0.974705666636711099,

0.047026497485465592,
-0.084562104085501480,
0.047026497485465592,
1.983564238653704900,
-0.993055129539134551 }
};


void setup() {
  Serial.begin(115200);
  delay(100);

  // for the large queue sizes at 192ksps sample rate we need a lot of buffers
  AudioMemory(130);
  delay(100);

  // get TIME from real time clock with 3V backup battery  
  setSyncProvider(getTeensy3Time);

// initialize SD card slot
  if (!(SD.begin(BUILTIN_SDCARD))) {
    // stop here, but print a message repetitively
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }
  //Starting to index the SD card for MP3/AAC.
  root = SD.open("/");

  // reads the last track what was playing.
  track = EEPROM.read(eeprom_adress); 


  while(true) {

    File files =  root.openNextFile();
    if (!files) {
      //If no more files, break out.
      break;
    }
    String curfile = files.name(); //put file in string
    //look for MP3 or AAC files
    int m = curfile.lastIndexOf(".MP3");
    int a = curfile.lastIndexOf(".AAC");
    int a1 = curfile.lastIndexOf(".MP4");
    int a2 = curfile.lastIndexOf(".M4A");
    //int w = curfile.lastIndexOf(".WAV");

    // if returned results is more then 0 add them to the list.
    if(m > 0 || a > 0 || a1 > 0 || a2 > 0 ){  

      tracklist[tracknum] = files.name();
      if(m > 0) trackext[tracknum] = 1;
      if(a > 0) trackext[tracknum] = 2;  
      if(a1 > 0) trackext[tracknum] = 2;
      if(a2 > 0) trackext[tracknum] = 2;
      //  if(w > 0) trackext[tracknum] = 3;
      tracknum++;  
    }
    // close 
    files.close();
  }
  //check if tracknum exist in tracklist from eeprom, like if you deleted some files or added.
  if(track > tracknum){
    //if it is too big, reset to 0
    EEPROM.write(eeprom_adress,0);
    track = 0;
  }
//      Serial.print("tracknum = "); Serial.println(tracknum);

  tracklist[track].toCharArray(playthis, sizeof(tracklist[track]));

/****************************************************************************************
 *  load saved settings from EEPROM
 ****************************************************************************************/
   // if loading the software for the very first time, comment out the "EEPROM_LOAD" --> then save settings in the menu --> load software with EEPROM_LOAD uncommented 
   EEPROM_LOAD();

  // Enable the audio shield. select input. and enable output
  sgtl5000_1.enable();
  sgtl5000_1.inputSelect(myInput);
  sgtl5000_1.adcHighPassFilterDisable(); // does not help too much!
  sgtl5000_1.lineInLevel(bands[band].RFgain);
  sgtl5000_1.lineOutLevel(31);
  
  sgtl5000_1.audioPostProcessorEnable(); // enables the DAP chain of the codec post audio processing before the headphone out
//  sgtl5000_1.eqSelect (2); // Tone Control
  sgtl5000_1.eqSelect (3); // five-band-graphic equalizer
  sgtl5000_1.eqBands (bass, midbass, mid, midtreble, treble); // (float bass, etc.) in % -100 to +100
//  sgtl5000_1.eqBands (bass, treble); // (float bass, float treble) in % -100 to +100
//  sgtl5000_1.enhanceBassEnable(); 
  sgtl5000_1.dacVolumeRamp();
  mixleft.gain(0,1.0);
  mixright.gain(0,1.0);
  sgtl5000_1.volume((float32_t)audio_volume / 100.0); // 

  pinMode(BACKLIGHT_PIN, OUTPUT );
  analogWriteResolution(8); // set resolution to 8 bit: well, that´s overkill for backlight, 4 bit would be enough :-)
  analogWriteFrequency(BACKLIGHT_PIN, 234375); // change PWM speed in order to prevent disturbance in the audio path
  // severe disturbance occurs (in the audio loudspeaker amp!) with the standard speed of 488.28Hz, which is well in the audible audio range
  analogWrite(BACKLIGHT_PIN, spectrum_brightness); // 0: dark, 256: bright 
  pinMode(BUTTON_1_PIN, INPUT_PULLUP);
  pinMode(BUTTON_2_PIN, INPUT_PULLUP);
  pinMode(BUTTON_3_PIN, INPUT_PULLUP);
  pinMode(BUTTON_4_PIN, INPUT_PULLUP);
  pinMode(BUTTON_5_PIN, INPUT_PULLUP);
  pinMode(BUTTON_6_PIN, INPUT_PULLUP);  
  pinMode(BUTTON_7_PIN, INPUT_PULLUP);  
  pinMode(BUTTON_8_PIN, INPUT_PULLUP);
  pinMode(Band1, OUTPUT);  // LPF switches
  pinMode(Band2, OUTPUT);  // 
  pinMode(Band3, OUTPUT);  // 
  pinMode(Band4, OUTPUT);  // 
  pinMode(Band5, OUTPUT);  // 
  pinMode(ATT_LE, OUTPUT);
  pinMode(ATT_CLOCK, OUTPUT);
  pinMode(ATT_DATA, OUTPUT);

  tft.begin();
  tft.setRotation( 3 );
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(10, 1);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_ORANGE);
  tft.setFont(Arial_14);
  tft.print("Teensy Convolution SDR ");
  tft.setFont(Arial_10);
  prepare_spectrum_display();

 /****************************************************************************************
 *  set filter bandwidth
 ****************************************************************************************/
   setup_mode(bands[band].mode);

  // this routine does all the magic of calculating the FIR coeffs (Bessel-Kaiser window)
    calc_FIR_coeffs (FIR_Coef, 513, (float32_t)LP_F_help, LP_Astop, 0, 0.0, (float)SR[SAMPLE_RATE].rate / DF);
    FFT_length = 1024;
    m_NumTaps = 513;
//    N_BLOCKS = FFT_length / 2 / BUFFER_SIZE;

/*  // set to zero all other coefficients in coefficient array    
  for(i = 0; i < MAX_NUMCOEF; i++)
  {
//    Serial.print (FIR_Coef[i]); Serial.print(", ");
      if (i >= m_NumTaps) FIR_Coef[i] = 0.0;
  }
*/    
 /****************************************************************************************
 *  init complex FFTs
 ****************************************************************************************/
      S = &arm_cfft_sR_f32_len1024;
      iS = &arm_cfft_sR_f32_len1024;
      maskS = &arm_cfft_sR_f32_len1024;
      spec_FFT = &arm_cfft_sR_f32_len256;
      NR_FFT = &arm_cfft_sR_f32_len256;
      NR_iFFT = &arm_cfft_sR_f32_len256;

 /****************************************************************************************
 *  Calculate the FFT of the FIR filter coefficients once to produce the FIR filter mask
 ****************************************************************************************/
    init_filter_mask();
 
 /****************************************************************************************
 *  show Filter response
 ****************************************************************************************/
/*    setI2SFreq (8000);
//    SAMPLE_RATE = 8000;
    delay(200);
    for(uint32_t y=0; y < FFT_length * 2; y++)
    FFT_buffer[y] = 180 * FIR_filter_mask[y];
//    calc_spectrum_mags(16,0.2);
//    show_spectrum();
//    delay(1000);
    SAMPLE_RATE = sr;
*/
 /****************************************************************************************
 *  Set sample rate
 ****************************************************************************************/
    setI2SFreq (SR[SAMPLE_RATE].rate);
    delay(200); // essential ?
    IF_FREQ = SR[SAMPLE_RATE].rate / 4; 
    
    biquad_lowpass1.numStages = N_stages_biquad_lowpass1; // set number of stages
    biquad_lowpass1.pCoeffs = biquad_lowpass1_coeffs; // set pointer to coefficients file
    for(i = 0; i < 4 * N_stages_biquad_lowpass1; i++)
    {
        biquad_lowpass1_state[i] = 0.0; // set state variables to zero   
    }
    biquad_lowpass1.pState = biquad_lowpass1_state; // set pointer to the state variables

    biquad_WFM.numStages = N_stages_biquad_WFM; // set number of stages
    biquad_WFM.pCoeffs = biquad_WFM_coeffs; // set pointer to coefficients file
    for(i = 0; i < 4 * N_stages_biquad_WFM; i++)
    {
        biquad_WFM_state[i] = 0.0; // set state variables to zero   
    }
    biquad_WFM.pState = biquad_WFM_state; // set pointer to the state variables

    biquad_WFM_19k.numStages = N_stages_biquad_WFM_19k; // set number of stages
    biquad_WFM_19k.pCoeffs = biquad_WFM_19k_coeffs; // set pointer to coefficients file
    for(i = 0; i < 4 * N_stages_biquad_WFM_19k; i++)
    {
        biquad_WFM_19k_state[i] = 0.0; // set state variables to zero   
    }
    biquad_WFM_19k.pState = biquad_WFM_19k_state; // set pointer to the state variables

    biquad_WFM_38k.numStages = N_stages_biquad_WFM_38k; // set number of stages
    biquad_WFM_38k.pCoeffs = biquad_WFM_38k_coeffs; // set pointer to coefficients file
    for(i = 0; i < 4 * N_stages_biquad_WFM_38k; i++)
    {
        biquad_WFM_38k_state[i] = 0.0; // set state variables to zero   
    }
    biquad_WFM_38k.pState = biquad_WFM_38k_state; // set pointer to the state variables


  /****************************************************************************************
 *  set filter bandwidth of IIR filter 
 ****************************************************************************************/
 // also adjust IIR AM filter
    // calculate IIR coeffs
    set_IIR_coeffs ((float32_t)LP_F_help, 1.3, (float32_t)SR[SAMPLE_RATE].rate / DF, 0); // 1st stage
    for(i = 0; i < 5; i++)
    {   // fill coefficients into the right file
        biquad_lowpass1_coeffs[i] = coefficient_set[i];
    }
  // IIR lowpass filter for wideband FM at 14k
   set_IIR_coeffs ((float32_t)15000, 0.54, (float32_t)192000, 0); // 1st stage
    for(i = 0; i < 5; i++)
    {   // fill coefficients into the right file
        biquad_WFM_coeffs[i] = coefficient_set[i];
        biquad_WFM_coeffs[i + 10] = coefficient_set[i];
    }
   set_IIR_coeffs ((float32_t)15000, 1.3, (float32_t)192000, 0); // 1st stage
    for(i = 0; i < 5; i++)
    {   // fill coefficients into the right file
        biquad_WFM_coeffs[i + 5] = coefficient_set[i];
        biquad_WFM_coeffs[i + 15] = coefficient_set[i];
    }

  // high Q IIR bandpass filter for wideband FM at 19k
   set_IIR_coeffs ((float32_t)19000, 100.0, (float32_t)192000, 2); // 1st stage
    for(i = 0; i < 5; i++)
    {   // fill coefficients into the right file
        biquad_WFM_19k_coeffs[i] = coefficient_set[i];
    }

  // high Q IIR bandpass filter for wideband FM at 38k
   set_IIR_coeffs ((float32_t)38000, 100.0, (float32_t)192000, 2); // 1st stage
    for(i = 0; i < 5; i++)
    {   // fill coefficients into the right file
        biquad_WFM_38k_coeffs[i] = coefficient_set[i];
    }
    
      set_tunestep();
      show_bandwidth (band[bands].mode, LP_F_help);

 /****************************************************************************************
 *  Initiate decimation and interpolation FIR filters
 ****************************************************************************************/

    // Decimation filter 1, M1 = 4
//    calc_FIR_coeffs (FIR_dec1_coeffs, 25, (float32_t)5100.0, 80, 0, 0.0, (float32_t)SR[SAMPLE_RATE].rate);
    calc_FIR_coeffs (FIR_dec1_coeffs, 50, (float32_t)5100.0, 80, 0, 0.0, (float32_t)SR[SAMPLE_RATE].rate);
    if(arm_fir_decimate_init_f32(&FIR_dec1_I, 50, 4, FIR_dec1_coeffs, FIR_dec1_I_state, BUFFER_SIZE * N_BLOCKS)) {
      Serial.println("Init of decimation failed");
      while(1);
    }
    if(arm_fir_decimate_init_f32(&FIR_dec1_Q, 50,  4, FIR_dec1_coeffs, FIR_dec1_Q_state, BUFFER_SIZE * N_BLOCKS)) {
      Serial.println("Init of decimation failed");
      while(1);
    }
    
    // Decimation filter 2, M2 = 2
    calc_FIR_coeffs (FIR_dec2_coeffs, 88, (float32_t)5100.0, 80, 0, 0.0, SR[SAMPLE_RATE].rate / 4.0);
    if(arm_fir_decimate_init_f32(&FIR_dec2_I, 88, 2, FIR_dec2_coeffs, FIR_dec2_I_state, BUFFER_SIZE * N_BLOCKS / 4)) {
      Serial.println("Init of decimation failed");
      while(1);
    }
    if(arm_fir_decimate_init_f32(&FIR_dec2_Q, 88, 2, FIR_dec2_coeffs, FIR_dec2_Q_state, BUFFER_SIZE * N_BLOCKS / 4)) {
      Serial.println("Init of decimation failed");
      while(1);
    }

    // Interpolation filter 1, L1 = 2
    // not sure whether I should design with the final sample rate ??
    // yes, because the interpolation filter is AFTER the upsampling, so it has to be in the target sample rate!
//    calc_FIR_coeffs (FIR_int1_coeffs, 8, (float32_t)5000.0, 80, 0, 0.0, 12000);
    calc_FIR_coeffs (FIR_int1_coeffs, 16, (float32_t)5100.0, 80, 0, 0.0, SR[SAMPLE_RATE].rate / 4.0);
    if(arm_fir_interpolate_init_f32(&FIR_int1_I, 2, 16, FIR_int1_coeffs, FIR_int1_I_state, BUFFER_SIZE * N_BLOCKS / 8)) {
      Serial.println("Init of interpolation failed");
      while(1);  
    }
    if(arm_fir_interpolate_init_f32(&FIR_int1_Q, 2, 16, FIR_int1_coeffs, FIR_int1_Q_state, BUFFER_SIZE * N_BLOCKS / 8)) {
      Serial.println("Init of interpolation failed");
      while(1);  
    }    
    
    // Interpolation filter 2, L2 = 4
    // not sure whether I should design with the final sample rate ??
    // yes, because the interpolation filter is AFTER the upsampling, so it has to be in the target sample rate!
//    calc_FIR_coeffs (FIR_int2_coeffs, 4, (float32_t)5000.0, 80, 0, 0.0, 24000);
    calc_FIR_coeffs (FIR_int2_coeffs, 16, (float32_t)5100.0, 80, 0, 0.0, (float32_t)SR[SAMPLE_RATE].rate);

    if(arm_fir_interpolate_init_f32(&FIR_int2_I, 4, 16, FIR_int2_coeffs, FIR_int2_I_state, BUFFER_SIZE * N_BLOCKS / 4)) {
      Serial.println("Init of interpolation failed");
      while(1);  
    }
    if(arm_fir_interpolate_init_f32(&FIR_int2_Q, 4, 16, FIR_int2_coeffs, FIR_int2_Q_state, BUFFER_SIZE * N_BLOCKS / 4)) {
      Serial.println("Init of interpolation failed");
      while(1);  
    }    
 /****************************************************************************************
 *  Coefficients for SAM sideband selection Hilbert filters
 *  Are these Hilbert transformers built from half-band filters??
 ****************************************************************************************/
    c0[0] = -0.328201924180698;
    c0[1] = -0.744171491539427;
    c0[2] = -0.923022915444215;
    c0[3] = -0.978490468768238;
    c0[4] = -0.994128272402075;
    c0[5] = -0.998458978159551;
    c0[6] = -0.999790306259206;

    c1[0] = -0.0991227952747244;
    c1[1] = -0.565619728761389;
    c1[2] = -0.857467122550052;
    c1[3] = -0.959123933111275;
    c1[4] = -0.988739372718090;
    c1[5] = -0.996959189310611;
    c1[6] = -0.999282492800792;

 /****************************************************************************************
 *  Zoom FFT: Initiate decimation and interpolation FIR filters AND IIR filters
 ****************************************************************************************/
    // Fstop = 0.5 * sample_rate / 2^spectrum_zoom 
    float32_t Fstop_Zoom = 0.5 * (float32_t) SR[SAMPLE_RATE].rate / (1<<spectrum_zoom); 
    calc_FIR_coeffs (Fir_Zoom_FFT_Decimate_coeffs, 4, Fstop_Zoom, 60, 0, 0.0, (float32_t)SR[SAMPLE_RATE].rate);
    if(arm_fir_decimate_init_f32(&Fir_Zoom_FFT_Decimate_I, 4, 1<<spectrum_zoom, Fir_Zoom_FFT_Decimate_coeffs, Fir_Zoom_FFT_Decimate_I_state, BUFFER_SIZE * N_BLOCKS)) {
      Serial.println("Init of decimation failed");
      while(1);
    }
    // same coefficients, but specific state variables
    if(arm_fir_decimate_init_f32(&Fir_Zoom_FFT_Decimate_Q, 4, 1<<spectrum_zoom, Fir_Zoom_FFT_Decimate_coeffs, Fir_Zoom_FFT_Decimate_Q_state, BUFFER_SIZE * N_BLOCKS)) {
      Serial.println("Init of decimation failed");
      while(1);
    }

    IIR_biquad_Zoom_FFT_I.numStages = IIR_biquad_Zoom_FFT_N_stages; // set number of stages
    IIR_biquad_Zoom_FFT_Q.numStages = IIR_biquad_Zoom_FFT_N_stages; // set number of stages
    for(i = 0; i < 4 * IIR_biquad_Zoom_FFT_N_stages; i++)
    {
        IIR_biquad_Zoom_FFT_I_state[i] = 0.0; // set state variables to zero   
        IIR_biquad_Zoom_FFT_Q_state[i] = 0.0; // set state variables to zero   
    }
    IIR_biquad_Zoom_FFT_I.pState = IIR_biquad_Zoom_FFT_I_state; // set pointer to the state variables
    IIR_biquad_Zoom_FFT_Q.pState = IIR_biquad_Zoom_FFT_Q_state; // set pointer to the state variables

    // this sets the coefficients for the ZoomFFT decimation filter
    // according to the desired magnification mode 
    // for 0 the mag_coeffs will a NULL  ptr, since the filter is not going to be used in this  mode!
    IIR_biquad_Zoom_FFT_I.pCoeffs = mag_coeffs[spectrum_zoom];
    IIR_biquad_Zoom_FFT_Q.pCoeffs = mag_coeffs[spectrum_zoom];

    Zoom_FFT_prep();

 /****************************************************************************************
 *  Initialize AGC variables
 ****************************************************************************************/
 
    AGC_prep();

 /****************************************************************************************
 *  IQ imbalance correction
 ****************************************************************************************/
        Serial.print("1 / K_est: "); Serial.println(1.0 / K_est);
        Serial.print("1 / sqrt(1 - P_est^2): "); Serial.println(P_est_mult);
        Serial.print("Phasenfehler in Grad: "); Serial.println(- asinf(P_est)); 
            
 /****************************************************************************************
 *  start local oscillator Si5351
 ****************************************************************************************/
  setAttenuator(RF_attenuation);
  si5351.init(SI5351_CRYSTAL_LOAD_10PF, Si_5351_crystal, calibration_constant);
  setfreq();
  delay(100); 
  show_frequency(bands[band].freq, 1);  

  /****************************************************************************************
 *  begin to queue the audio from the audio library
 ****************************************************************************************/
    delay(100);
    Q_in_L.begin();
    Q_in_R.begin();
//    delay(100);    
} // END SETUP

int16_t *sp_L;
int16_t *sp_R;
float32_t hh1 = 0.0;
float32_t hh2 = 0.0;
float32_t I_old = 0.0;
float32_t Q_old = 0.0;
float32_t rawFM_old_L = 0.0;
float32_t rawFM_old_R = 0.0;
const uint8_t WFM_BLOCKS = 6;
// T = 1.0/sample_rate;
// alpha = 1 - e^(-T/tau);
// tau = 50µsec in Europe --> alpha = 0.099
// tau = 75µsec in the US -->  
// 
float32_t dt = 1.0/192000.0;
float32_t deemp_alpha = dt/(50e-6+dt);
//float32_t m_alpha = 0.91;
//float32_t deemp_alpha = 0.099; 
float32_t onem_deemp_alpha = 1.0 - deemp_alpha;
uint16_t autotune_counter = 0;

void loop() {
//  asm(" wfi"); // does this save battery power ? https://forum.pjrc.com/threads/40315-Reducing-Power-Consumption
  elapsedMicros usec = 0;
/**********************************************************************************
 *  Get samples from queue buffers
 **********************************************************************************/
    // we have to ensure that we have enough audio samples: we need
    // N_BLOCKS = 32
    // decimate by 8
    // FFT1024 point --> = 1024 / 2 / 128 = 4 
    // when these buffers are available, read them in, decimate and perform
    // the FFT - cmplx-mult - iFFT
    //

    // WIDE FM BROADCAST RECEPTION 
    if(bands[band].mode == DEMOD_WFM)
    {
    if (Q_in_L.available() > 12 && Q_in_R.available() > 12 && Menu_pointer != MENU_PLAYER)
    {   
// get audio samples from the audio  buffers and convert them to float
    for (i = 0; i < WFM_BLOCKS; i++)
    {  
    sp_L = Q_in_L.readBuffer();
    sp_R = Q_in_R.readBuffer();

      // convert to float one buffer_size
      // float_buffer samples are now standardized from > -1.0 to < 1.0
     arm_q15_to_float (sp_L, &float_buffer_L[BUFFER_SIZE * i], BUFFER_SIZE); // convert int_buffer to float 32bit
     arm_q15_to_float (sp_R, &float_buffer_R[BUFFER_SIZE * i], BUFFER_SIZE); // convert int_buffer to float 32bit
     Q_in_L.freeBuffer();
     Q_in_R.freeBuffer();
    }

     FFT_buffer[0] = 0.025 * atan2f(I_old * float_buffer_R[0] - float_buffer_L[0] * Q_old, 
                     I_old * float_buffer_L[0] + float_buffer_R[0] * Q_old);
     for(i = 1; i < BUFFER_SIZE * WFM_BLOCKS; i++)
     { 
            FFT_buffer[i] = 0.025 * atan2f(float_buffer_L[i - 1] * float_buffer_R[i] - float_buffer_L[i] * float_buffer_R[i - 1], 
                    float_buffer_L[i - 1] * float_buffer_L[i] + float_buffer_R[i] * float_buffer_R[i - 1]);
     }

      // take care of last sample of each block 
            I_old = float_buffer_L[BUFFER_SIZE * WFM_BLOCKS -1];
            Q_old = float_buffer_R[BUFFER_SIZE * WFM_BLOCKS -1];

/*******************************************************************************************************
 * 
 * STEREO first trial
 * 39.2% in MONO
 *******************************************************************************************************/

// THIS IS REALLY A GREAT MESS WITH ALL THE COPYING INTO THOSE DIFFERENT BUFFERS
// BEWARE!

// audio of L + R channel is in FFT_buffer

// 1. BPF 19k for extracting the pilot tone
     arm_biquad_cascade_df1_f32 (&biquad_WFM_19k, FFT_buffer, float_buffer_L, BUFFER_SIZE * WFM_BLOCKS);
// float_buffer_L --> 19k pilot

// 2. if negative --> positive in order to rectify the wave
     for(i = 0; i < BUFFER_SIZE * WFM_BLOCKS; i++)
     {
          if(float_buffer_L[i] < 0.0) float_buffer_L[i] = - float_buffer_L[i];
     }
// float_buffer_L --> 19k pilot rectified

// 2.b) eliminate DC
     for(i = 0; i < BUFFER_SIZE * WFM_BLOCKS; i++)
     {  // DC removal filter ----------------------- 
          w = float_buffer_L[i] + wold * 0.9999f; // yes, I want a superb bass response ;-) 
          float_buffer_L[i] = w - wold; 
          wold = w;      
     }        
// float_buffer_L --> 19k pilot rectified & without DC

// 3. BPF 38kHz to extract the double f pilot tone
     arm_biquad_cascade_df1_f32 (&biquad_WFM_38k, float_buffer_L, float_buffer_R, BUFFER_SIZE * WFM_BLOCKS);
// float_buffer_R --> 38k pilot

// 4. L-R = multiply audio with 38k carrier in order to produce audio L - R
        for(i = 0; i < BUFFER_SIZE * WFM_BLOCKS; i++)
        {
//            float_buffer_L[i] = 1000.0 * float_buffer_R[i] * FFT_buffer[i];
            iFFT_buffer[i] = stereo_factor * float_buffer_R[i] * FFT_buffer[i];
        }
     arm_biquad_cascade_df1_f32 (&biquad_WFM, iFFT_buffer, float_buffer_L, BUFFER_SIZE * WFM_BLOCKS);
// float_buffer_L --> L-R

// 6. Right channel: 
        for(i = 0; i < BUFFER_SIZE * WFM_BLOCKS; i++)
        {
            iFFT_buffer[i] = FFT_buffer[i] - float_buffer_L[i];
        }
// iFFT_buffer --> RIGHT CHANNEL

// 7. Left channel
        for(i = 0; i < BUFFER_SIZE * WFM_BLOCKS; i++)
        {
            float_buffer_R[i] = FFT_buffer[i] + float_buffer_L[i];
        }
// float_buffer_R --> LEFT CHANNEL

// Right channel: lowpass filter with 15kHz Fstop & deemphasis
     rawFM_old_R = deemphasis_wfm_ff (iFFT_buffer, float_buffer_L, BUFFER_SIZE * WFM_BLOCKS, 192000, rawFM_old_R);
//     arm_biquad_cascade_df1_f32 (&biquad_WFM, FFT_buffer, float_buffer_L, BUFFER_SIZE * WFM_BLOCKS);

// FFT_buffer --> RIGHT CHANNEL PERFECT AUDIO

// Left channel: lowpass filter with 15kHz Fstop & deemphasis
//     arm_biquad_cascade_df1_f32 (&biquad_WFM, float_buffer_R, FFT_buffer, BUFFER_SIZE * WFM_BLOCKS);

       rawFM_old_L = deemphasis_wfm_ff (float_buffer_R, iFFT_buffer, BUFFER_SIZE * WFM_BLOCKS, 192000, rawFM_old_L);
     
//*****END of STEREO***********************************************************************************************************

/*        // lowpass filter with 15kHz Fstop
        arm_biquad_cascade_df1_f32 (&biquad_WFM, FFT_buffer, float_buffer_R, BUFFER_SIZE * WFM_BLOCKS);

         // de-emphasis with exponential averager
     float_buffer_L[0] = float_buffer_R[0] * 0.09 + rawFM_old * 0.91;
     for(i = 1; i < (BUFFER_SIZE * WFM_BLOCKS); i++)
     { 
              float_buffer_L[i] = float_buffer_R[i] * 0.09 + float_buffer_L[i-1] * 0.91;
     }
     rawFM_old = float_buffer_L[(BUFFER_SIZE * WFM_BLOCKS) - 1];
*/      


        if (Q_in_L.available() >  25) 
    {
//      AudioNoInterrupts();
      Q_in_L.clear();
      n_clear ++; // just for debugging to check how often this occurs [about once in an hour of playing . . .]
//      AudioInterrupts();
    }      
    if (Q_in_R.available() >  25)
    {
//      AudioNoInterrupts();
      Q_in_R.clear();
      n_clear ++; // just for debugging to check how often this occurs [about once in an hour of playing . . .]
//      AudioInterrupts();
    } 

    // decimation-by-2

    // frequency translate 38kHz

    // decimation-by-4

    // AM-demod

    // interpolation-by-8

    for (i = 0; i < WFM_BLOCKS; i++)
    {
      sp_L = Q_out_L.getBuffer();
      sp_R = Q_out_R.getBuffer();
      arm_float_to_q15 (&float_buffer_L[BUFFER_SIZE * i], sp_L, BUFFER_SIZE); 
//      arm_float_to_q15 (&float_buffer_L[BUFFER_SIZE * i], sp_R, BUFFER_SIZE); 
      arm_float_to_q15 (&iFFT_buffer[BUFFER_SIZE * i], sp_R, BUFFER_SIZE); 
      Q_out_L.playBuffer(); // play it !
      Q_out_R.playBuffer(); // play it !
    }  

     
      sum = sum + usec;
      idx_t++;

      if (idx_t > 1000) 
          //          if (five_sec.check() == 1)
      {
          tft.fillRect(260,5,60,20,ILI9341_BLACK);   
          tft.setCursor(260, 5);
          tft.setTextColor(ILI9341_GREEN);
          tft.setFont(Arial_9);
          mean = sum / idx_t;
          tft.print (mean/29.0 * SR[SAMPLE_RATE].rate / AUDIO_SAMPLE_RATE_EXACT / WFM_BLOCKS);tft.print("%");
          idx_t = 0;
          sum = 0;
          Serial.print(" n_clear = "); Serial.println(n_clear);
          Serial.print ("1 - Alpha = "); Serial.println(onem_deemp_alpha);
          Serial.print ("Alpha = "); Serial.println(deemp_alpha);

      } 
        WFM_spectrum_flag++;
        if(WFM_spectrum_flag == 2)
        {
//            spectrum_zoom == SPECTRUM_ZOOM_1;
            zoom_display = 1;
//            calc_256_magn();
            Zoom_FFT_prep();
            Zoom_FFT_exe(WFM_BLOCKS * BUFFER_SIZE);
        }
        else if (WFM_spectrum_flag >= 4 && show_spectrum_flag)
        {
            show_spectrum();
            WFM_spectrum_flag = 0;
        }
   }   
 
    }
    else    
    // are there at least N_BLOCKS buffers in each channel available ?
    if (Q_in_L.available() > N_BLOCKS && Q_in_R.available() > N_BLOCKS && Menu_pointer != MENU_PLAYER)
    {   
// get audio samples from the audio  buffers and convert them to float
// read in 32 blocks á 128 samples in I and Q
    for (i = 0; i < N_BLOCKS; i++)
    {  
    sp_L = Q_in_L.readBuffer();
    sp_R = Q_in_R.readBuffer();
    // set clip state flags for codec gain adjustment in codec_gain()
    for(int xx = 0; xx < 128; xx++)
    {
        if(sp_L[xx] > 4096)
        {
            quarter_clip = 1;
            if (sp_L[xx] > 8192)
            {
                  half_clip = 1;
            }
        }
    }
      // convert to float one buffer_size
      // float_buffer samples are now standardized from > -1.0 to < 1.0
     arm_q15_to_float (sp_L, &float_buffer_L[BUFFER_SIZE * i], BUFFER_SIZE); // convert int_buffer to float 32bit
     arm_q15_to_float (sp_R, &float_buffer_R[BUFFER_SIZE * i], BUFFER_SIZE); // convert int_buffer to float 32bit
     Q_in_L.freeBuffer();
     Q_in_R.freeBuffer();
//     blocks_read ++;
    }

    // this is supposed to prevent overfilled queue buffers
    // rarely the Teensy audio queue gets a hickup
    // in that case this keeps the whole audio chain running smoothly 
    if (Q_in_L.available() >  1) 
    {
      AudioNoInterrupts();
      Q_in_L.clear();
      n_clear ++; // just for debugging to check how often this occurs [about once in an hour of playing . . .]
      AudioInterrupts();
    }      
    if (Q_in_R.available() >  1)
    {
      AudioNoInterrupts();
      Q_in_R.clear();
      n_clear ++; // just for debugging to check how often this occurs [about once in an hour of playing . . .]
      AudioInterrupts();
    }

/***********************************************************************************************
 *  just for checking: plotting min/max and mean of the samples
 ***********************************************************************************************/
//    if (ms_500.check() == 1)
    if(0)
    {
    float32_t sample_min = 0.0;
    float32_t sample_max = 0.0;
    float32_t sample_mean = 0.0;
    uint32_t min_index, max_index;
    arm_mean_f32(float_buffer_L, BUFFER_SIZE * N_BLOCKS, &sample_mean);
    arm_max_f32(float_buffer_L, BUFFER_SIZE * N_BLOCKS, &sample_max, &max_index);
    arm_min_f32(float_buffer_L, BUFFER_SIZE * N_BLOCKS, &sample_min, &min_index);
    Serial.print("Sample min: "); Serial.println(sample_min);   
    Serial.print("Sample max: "); Serial.println(sample_max); 
    Serial.print("Max index: "); Serial.println(max_index); 
      
    Serial.print("Sample mean: "); Serial.println(sample_mean); 
    Serial.print("FFT_length: "); Serial.println(FFT_length); 
    Serial.print("N_BLOCKS: "); Serial.println(N_BLOCKS); 
    Serial.println(BUFFER_SIZE * N_BLOCKS / 8);      
    }


/***********************************************************************************************
 *  IQ amplitude and phase correction
 ***********************************************************************************************/

    if(!auto_IQ_correction)
    {
       // Manual IQ amplitude correction
        // to be honest: we only correct the amplitude of the I channel ;-)
        arm_scale_f32 (float_buffer_L, IQ_amplitude_correction_factor, float_buffer_L, BUFFER_SIZE * N_BLOCKS);
        // IQ phase correction
        IQ_phase_correction(float_buffer_L, float_buffer_R, IQ_phase_correction_factor, BUFFER_SIZE * N_BLOCKS);
//        Serial.println("Manual IQ correction");
    }
    else
/*    {
        // introduce artificial amplitude imbalance
        arm_scale_f32 (float_buffer_R, 0.97, float_buffer_R, BUFFER_SIZE * N_BLOCKS);
        // introduce artificial phase imbalance
        IQ_phase_correction(float_buffer_L, float_buffer_R, +0.05, BUFFER_SIZE * N_BLOCKS);
    }
*/
/*******************************************************************************************************
*
* algorithm by Moseley & Slump
********************************************************************************************************/

    if(MOSELEY)
    {   // Moseley, N.A. & C.H. Slump (2006): A low-complexity feed-forward I/Q imbalance compensation algorithm.
        // in 17th Annual Workshop on Circuits, Nov. 2006, pp. 158–164.
        // http://doc.utwente.nl/66726/1/moseley.pdf

        if(twinpeaks_tested == 3) // delete "IQ test"-display after a while, when everything is OK
        {
            twinpeaks_counter++;
            if(twinpeaks_counter >= 200)
            {
                tft.fillRect(spectrum_x + 256 + 2, pos_y_time + 20, 320 - spectrum_x - 258, 31, ILI9341_BLACK);
                twinpeaks_tested = 1;
            }
        }
        if (twinpeaks_tested == 2)
        {
            twinpeaks_counter++;
            Serial.print("twinpeaks counter = "); Serial.println(twinpeaks_counter);
            if(twinpeaks_counter == 1)
            {
                tft.fillRect(spectrum_x + 256 + 2, pos_y_time + 20, 320 - spectrum_x - 258, 31, ILI9341_RED);
                tft.drawRect(spectrum_x + 256 + 2, pos_y_time + 20, 320 - spectrum_x - 258, 31, ILI9341_MAROON);
            }
            tft.setCursor(spectrum_x + 256 + 6, pos_y_time + 22);
            tft.setFont(Arial_12);
            tft.setTextColor(ILI9341_WHITE);
            tft.print("IQtest");
            tft.setCursor(pos_x_time + 55, pos_y_time + 22 + 14);
            tft.setFont(Arial_12);
            if(twinpeaks_counter)
            {
                tft.setTextColor(ILI9341_RED);
                tft.print(800 - twinpeaks_counter + 1);
            }
            tft.setTextColor(ILI9341_WHITE);
            tft.setCursor(pos_x_time + 55, pos_y_time + 22 + 14);
            tft.print(800 - twinpeaks_counter);
        }
//        if(twinpeaks_counter >= 500) // wait 500 cycles for the system to settle: compare fig. 11 in Moseley & Slump (2006)
        if(twinpeaks_counter >= 800 && twinpeaks_tested == 2) // wait 800 cycles for the system to settle: compare fig. 11 in Moseley & Slump (2006)
        // it takes quite a while until the automatic IQ correction has really settled (because of the built-in lowpass filter in the algorithm):
        // we take only the first 256 of the 4096 samples to calculate the IQ correction factors 
        // 500 cycles x 4096 samples (@96ksps sample rate) = 21.33 sec 
        {
            twinpeaks_tested = 0;
            twinpeaks_counter = 0;
            Serial.println("twinpeaks_counter ready to test IQ balance !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1");
        }
        for(i = 0; i < n_para; i++)
        {
            teta1 += sign(float_buffer_L[i]) * float_buffer_R[i]; // eq (34)
            teta2 += sign(float_buffer_L[i]) * float_buffer_L[i]; // eq (35)
            teta3 += sign (float_buffer_R[i]) * float_buffer_R[i]; // eq (36)
        }
            teta1 = -0.01 * (teta1 / n_para) + 0.99 * teta1_old; // eq (34) and first order lowpass
            teta2 = 0.01 * (teta2 / n_para) + 0.99 * teta2_old; // eq (35) and first order lowpass
            teta3 = 0.01 * (teta3 / n_para) + 0.99 * teta3_old; // eq (36) and first order lowpass

            if(teta2 != 0.0)// prevent divide-by-zero
            {
                M_c1 = teta1 / teta2; // eq (30)
            }
            else
            {
                M_c1 = 0.0;
            }

            float32_t help = (teta2 * teta2);
            if(help > 0.0)// prevent divide-by-zero
            {
                help = (teta3 * teta3 - teta1 * teta1) / help; // eq (31)
            }
            if (help > 0.0)// prevent sqrtf of negative value
            {
                M_c2 = sqrtf(help); // eq (31)
            }
            else
            {
                M_c2 = 1.0;
            }
            // Test and fix of the "twinpeak syndrome"
            // which occurs sporadically and can -to our knowledge- only be fixed
            // by a reset of the codec
            // It can be identified by a totally non-existing mirror rejection,
            // so I & Q have essentially the same phase
            // We use this to identify the snydrome and reset the codec accordingly:
            // calculate phase between I & Q
            
            if(teta3 != 0.0 && twinpeaks_tested == 0) // prevent divide-by-zero
                // twinpeak_tested = 2 --> wait for system to warm up
                // twinpeak_tested = 0 --> go and test the IQ phase
                // twinpeak_tested = 1 --> tested, verified, go and have a nice day!
                {
                  Serial.println("HERE");
                  // Moseley & Slump (2006) eq. (33)
                    // this gives us the phase error between I & Q in radians
                    float32_t phase_IQ = asinf(teta1 / teta3);
                    Serial.print("asinf = "); Serial.println(phase_IQ);
                    if ((phase_IQ > 0.15 || phase_IQ < -0.15) && codec_restarts < 5)
                        // threshold lowered, so we can be really sure to have IQ phase balance OK
                        // threshold of 22.5 degrees phase shift == PI / 8 == 0.3926990817
                        // hopefully your hardware is not so bad, that its phase error is more than 22 degrees ;-)
                        // if it is that bad, adjust this threshold to maybe PI / 7 or PI / 6
                    {
                        reset_codec();
                        Serial.println("CODEC RESET");
                        twinpeaks_tested = 2;
                        codec_restarts++;
                        // TODO: we should set a maximum number of codec resets
                        // and print out a message, if twinpeaks remains after the
                        // 5th reset for example --> could then be a severe hardware error !
                        if(codec_restarts >= 4)
                        {
                            // PRINT OUT WARNING MESSAGE
                            Serial.println("Tried four times to reset your codec, but still IQ balance is very bad - hardware error ???");
                              twinpeaks_tested = 3; 
                            tft.fillRect(spectrum_x + 256 + 2, pos_y_time + 20, 320 - spectrum_x - 258, 31, ILI9341_RED);
                            tft.setCursor(pos_x_time + 55, pos_y_time + 22 + 14);
                            tft.setFont(Arial_12);
                            tft.print("reset!");
                        }
                    }
                    else
                    {
                        twinpeaks_tested = 3;
                        twinpeaks_counter = 0;
                        Serial.println("IQ phase balance is OK, so enjoy radio reception !");
                        tft.fillRect(spectrum_x + 256 + 2, pos_y_time + 20, 320 - spectrum_x - 258, 31, ILI9341_NAVY);
                        tft.drawRect(spectrum_x + 256 + 2, pos_y_time + 20, 320 - spectrum_x - 258, 31, ILI9341_MAROON);
                        tft.setCursor(spectrum_x + 256 + 6, pos_y_time + 22);
                        tft.setFont(Arial_12);
                        tft.setTextColor(ILI9341_WHITE);
                        tft.print("IQtest");
                        tft.setCursor(pos_x_time + 55, pos_y_time + 22 + 14);
                        tft.setFont(Arial_12);
                        tft.print("OK !");
                    }
                }

                
            teta1_old = teta1;
            teta2_old = teta2;
            teta3_old = teta3;

        // first correct Q and then correct I --> this order is crucially important!
        for(i = 0; i < BUFFER_SIZE * N_BLOCKS; i++)
        {   // see fig. 5
            float_buffer_R[i] += M_c1 * float_buffer_L[i];
        }
        // see fig. 5
        arm_scale_f32 (float_buffer_L, M_c2, float_buffer_L, BUFFER_SIZE * N_BLOCKS);
    }

/*******************************************************************************************************
*
* algorithm by Chang et al. 2010
*
*
********************************************************************************************************/

  else if(CHANG)
  {
// this is the IQ imbalance correction algorithm by Chang et al. 2010
// 1.) estimate K_est
// 2.) correct for K_est_mult
// 3.) estimate P_est
// 4.) correct for P_est_mult

// new experiment

    // calculate the coefficients for the imbalance correction
    // once at system start or when changing frequency band
    // IQ_state 0: do nothing --> automatic IQ imbalance correction switched off
    // IQ_state 1: estimate amplitude coefficient K_est
    // IQ_state 2: K_est estimated, wait for next stage
    // IQ_state 3: estimate phase coefficient P_est
    // IQ_state 4: everything calculated and corrected

/*
    switch(IQ_state) 
    {
      case 0:
        break;
      case 1: // Chang & Lin (2010): eq. (9)
          AudioNoInterrupts();
          Q_sum = 0.0;
          I_sum = 0.0;
          for (i = 0; i < n_para; i++)
          {
               Q_sum += float_buffer_R[i] * float_buffer_R[i + n_para];
               I_sum += float_buffer_L[i] * float_buffer_L[i + n_para];
          }
          K_est = sqrtf(Q_sum / I_sum);
          K_est_mult = 1.0 / K_est;
          IQ_state++;
          Serial.print("New 1 / K_est: "); Serial.println(1.0 / K_est);
          AudioInterrupts();
        break;    
      case 2: // Chang & Lin (2010): eq. (10)
          AudioNoInterrupts();
          IQ_sum = 0.0;
          I_sum = 0.0;
          for (i = 0; i < n_para; i++)
          {
               IQ_sum += float_buffer_L[i] * float_buffer_R[i + n_para];
               I_sum += float_buffer_L[i] * float_buffer_L[i + n_para];
          }
          P_est = IQ_sum / I_sum;
          P_est_mult = 1.0 / (sqrtf(1.0 - P_est * P_est));
          IQ_state = 1;
          Serial.print("1 / sqrt(1 - P_est^2): "); Serial.println(P_est_mult);
          if(P_est > -1.0 && P_est < 1.0) {
            Serial.print("New: Phasenfehler in Grad: "); Serial.println(- asinf(P_est)); 
          }
          AudioInterrupts();
        break;
    }
*/
  
      // only correct, if signal strength is above a threshold
      // 
      if(IQ_counter >= 0 && 1 )
      {
// 1.)  
    // K_est estimation
          Q_sum = 0.0;
          I_sum = 0.0;
          for (i = 0; i < n_para; i++)
          {
               Q_sum += float_buffer_R[i] * float_buffer_R[i + n_para];
               I_sum += float_buffer_L[i] * float_buffer_L[i + n_para];
          }
          if(I_sum != 0.0)
          {
            if(Q_sum / I_sum < 0) {
            Serial.println("ACHTUNG WURZEL AUS NEGATIVER ZAHL");
            Serial.println("ACHTUNG WURZEL AUS NEGATIVER ZAHL");
            Serial.println("ACHTUNG WURZEL AUS NEGATIVER ZAHL");
            Serial.println("ACHTUNG WURZEL AUS NEGATIVER ZAHL");
            Serial.println("ACHTUNG WURZEL AUS NEGATIVER ZAHL");
            Serial.println("ACHTUNG WURZEL AUS NEGATIVER ZAHL");
            Serial.println("ACHTUNG WURZEL AUS NEGATIVER ZAHL");
            Serial.println("ACHTUNG WURZEL AUS NEGATIVER ZAHL");
            Serial.println("ACHTUNG WURZEL AUS NEGATIVER ZAHL");
            K_est = K_est_old;
            }
            else 
            {
                if(IQ_counter != 0) K_est = 0.001 * sqrtf(Q_sum / I_sum) + 0.999 * K_est_old;
                else 
                {
                    K_est = sqrtf(Q_sum / I_sum);
                }
                K_est_old = K_est;
            }
          }
          else K_est = K_est_old;
          Serial.print("New 1 / K_est: "); Serial.println(100.0 / K_est);

// 3.)
          // phase estimation 
          IQ_sum = 0.0;
          for (i = 0; i < n_para; i++)
          {    // amplitude correction inside the formula  --> K_est_mult !
               IQ_sum += float_buffer_L[i] * float_buffer_R[i + n_para];// * K_est_mult;
          }
          if(I_sum == 0.0) I_sum = IQ_sum;
          if(IQ_counter != 0) P_est = 0.001 * (IQ_sum / I_sum) + 0.999 * P_est_old;
          else P_est = (IQ_sum / I_sum);
          P_est_old = P_est;
          if(P_est > -1.0 && P_est < 1.0) P_est_mult = 1.0 / (sqrtf(1.0 - P_est * P_est));
          else P_est_mult = 1.0;
          // dirty fix !!!

          Serial.print("1 / sqrt(1 - P_est^2): "); Serial.println(P_est_mult * 100.0);
          if(P_est > -1.0 && P_est < 1.0) {
          Serial.print("New: Phasenfehler in Grad: "); Serial.println(- asinf(P_est) * 100.0); 
          }

// 4.)
    // Chang & Lin (2010): eq. 12; phase correction
    for(i = 0; i < BUFFER_SIZE * N_BLOCKS; i++)
    {
        float_buffer_R[i] = P_est_mult * float_buffer_R[i] - P_est * float_buffer_L[i];
    }
      }
       IQ_counter++;
    if(IQ_counter >= 1000) IQ_counter = 1;
  
  } // end if (Chang)

 
/***********************************************************************************************
 *  Perform a 256 point FFT for the spectrum display on the basis of the first 256 complex values
 *  of the raw IQ input data
 *  this saves about 3% of processor power compared to calculating the magnitudes and means of 
 *  the 4096 point FFT for the display [41.8% vs. 44.53%]
 ***********************************************************************************************/
    // only go there from here, if magnification == 1
    if (spectrum_zoom == SPECTRUM_ZOOM_1)
    {
//        Zoom_FFT_exe(BUFFER_SIZE * N_BLOCKS);
          zoom_display = 1;
          calc_256_magn();
    }
   
/**********************************************************************************
 *  Frequency translation by Fs/4 without multiplication
 *  Lyons (2011): chapter 13.1.2 page 646
 *  together with the savings of not having to shift/rotate the FFT_buffer, this saves
 *  about 1% of processor use (40.78% vs. 41.70% [AM demodulation])
 **********************************************************************************/
      // this is for +Fs/4 [moves receive frequency to the left in the spectrum display]
        for(i = 0; i < BUFFER_SIZE * N_BLOCKS; i += 4)
    {   // float_buffer_L contains I = real values
        // float_buffer_R contains Q = imaginary values
        // xnew(0) =  xreal(0) + jximag(0)
        // leave as it is!
        // xnew(1) =  - ximag(1) + jxreal(1)
        hh1 = - float_buffer_R[i + 1];
        hh2 =   float_buffer_L[i + 1];
            float_buffer_L[i + 1] = hh1;
            float_buffer_R[i + 1] = hh2;
        // xnew(2) = -xreal(2) - jximag(2)
        hh1 = - float_buffer_L[i + 2];
        hh2 = - float_buffer_R[i + 2];
            float_buffer_L[i + 2] = hh1;
            float_buffer_R[i + 2] = hh2;
        // xnew(3) = + ximag(3) - jxreal(3)
        hh1 =   float_buffer_R[i + 3];
        hh2 = - float_buffer_L[i + 3];
            float_buffer_L[i + 3] = hh1;
            float_buffer_R[i + 3] = hh2;
    }

      // this is for -Fs/4 [moves receive frequency to the right in the spectrumdisplay]
/*    for(i = 0; i < BUFFER_SIZE * N_BLOCKS; i += 4)
    {   // float_buffer_L contains I = real values
        // float_buffer_R contains Q = imaginary values
        // xnew(0) =  xreal(0) + jximag(0)
        // leave as it is!
        // xnew(1) =  ximag(1) - jxreal(1)
        hh1 = float_buffer_R[i + 1];
        hh2 = - float_buffer_L[i + 1];
        float_buffer_L[i + 1] = hh1;
        float_buffer_R[i + 1] = hh2;
        // xnew(2) = -xreal(2) - jximag(2)
        hh1 = - float_buffer_L[i + 2];
        hh2 = - float_buffer_R[i + 2];
        float_buffer_L[i + 2] = hh1;
        float_buffer_R[i + 2] = hh2;
        // xnew(3) = -ximag(3) + jxreal(3)
        hh1 = - float_buffer_R[i + 3];
        hh2 = float_buffer_L[i + 3];
        float_buffer_L[i + 3] = hh1;
        float_buffer_R[i + 3] = hh2;
    }
*/  
/***********************************************************************************************
 *  just for checking: plotting min/max and mean of the samples
 ***********************************************************************************************/
//    if (ms_500.check() == 1)
    if(0)
    {
    float32_t sample_min = 0.0;
    float32_t sample_max = 0.0;
    float32_t sample_mean = 0.0;
    flagg = 1;
    uint32_t min_index, max_index;
    arm_mean_f32(float_buffer_L, BUFFER_SIZE * N_BLOCKS, &sample_mean);
    arm_max_f32(float_buffer_L, BUFFER_SIZE * N_BLOCKS, &sample_max, &max_index);
    arm_min_f32(float_buffer_L, BUFFER_SIZE * N_BLOCKS, &sample_min, &min_index);
    Serial.print("VOR DECIMATION: ");
    Serial.print("Sample min: "); Serial.println(sample_min);   
    Serial.print("Sample max: "); Serial.println(sample_max); 
    Serial.print("Max index: "); Serial.println(max_index); 
      
    Serial.print("Sample mean: "); Serial.println(sample_mean); 
//    Serial.print("FFT_length: "); Serial.println(FFT_length); 
//    Serial.print("N_BLOCKS: "); Serial.println(N_BLOCKS); 
//Serial.println(BUFFER_SIZE * N_BLOCKS / 8);      
    }

      // SPECTRUM_ZOOM_2 and larger here after frequency conversion!
      if(spectrum_zoom != SPECTRUM_ZOOM_1)
      {
          Zoom_FFT_exe (BUFFER_SIZE * N_BLOCKS);
      }
          if(zoom_display)
          {
            if(show_spectrum_flag)
            {
                show_spectrum();
            }
            zoom_display = 0;
            zoom_sample_ptr = 0;
          }

/**********************************************************************************
 *  S-Meter & dBm-display
 **********************************************************************************/
        Calculatedbm();
        Display_dbm();

/**********************************************************************************
 *  Decimation
 **********************************************************************************/
      // lowpass filter 5kHz, 80dB stopband att, fstop = 19k, 25 taps
      // decimation-by-4 in-place!
      arm_fir_decimate_f32(&FIR_dec1_I, float_buffer_L, float_buffer_L, BUFFER_SIZE * N_BLOCKS);
      arm_fir_decimate_f32(&FIR_dec1_Q, float_buffer_R, float_buffer_R, BUFFER_SIZE * N_BLOCKS);

      // lowpass filter 5kHz, 80dB stopband att, fstop = 7k, 44 taps
      // decimation-by-2 in-place
      arm_fir_decimate_f32(&FIR_dec2_I, float_buffer_L, float_buffer_L, BUFFER_SIZE * N_BLOCKS / 4);
      arm_fir_decimate_f32(&FIR_dec2_Q, float_buffer_R, float_buffer_R, BUFFER_SIZE * N_BLOCKS / 4);

/***********************************************************************************************
 *  just for checking: plotting min/max and mean of the samples
 ***********************************************************************************************/
//    if (flagg == 1)
    if(0)
    {
    flagg = 0;  
    float32_t sample_min = 0.0;
    float32_t sample_max = 0.0;
    float32_t sample_mean = 0.0;
    uint32_t min_index, max_index;
    arm_mean_f32(float_buffer_L, BUFFER_SIZE * N_BLOCKS / 8, &sample_mean);
    arm_max_f32(float_buffer_L, BUFFER_SIZE * N_BLOCKS / 8, &sample_max, &max_index);
    arm_min_f32(float_buffer_L, BUFFER_SIZE * N_BLOCKS/ 8, &sample_min, &min_index);
    Serial.print("NACH DECIMATION: ");
    Serial.print("Sample min: "); Serial.println(sample_min);   
    Serial.print("Sample max: "); Serial.println(sample_max); 
    Serial.print("Max index: "); Serial.println(max_index); 
      
    Serial.print("Sample mean: "); Serial.println(sample_mean); 
//    Serial.print("FFT_length: "); Serial.println(FFT_length); 
//    Serial.print("N_BLOCKS: "); Serial.println(N_BLOCKS); 
//Serial.println(BUFFER_SIZE * N_BLOCKS / 8);      
    }

/**********************************************************************************
 *  Digital convolution
 **********************************************************************************/
//  basis for this was Lyons, R. (2011): Understanding Digital Processing.
//  "Fast FIR Filtering using the FFT", pages 688 - 694
//  numbers for the steps taken from that source
//  Method used here: overlap-and-save

// 4.) ONLY FOR the VERY FIRST FFT: fill first samples with zeros 
      if(first_block) // fill real & imaginaries with zeros for the first BLOCKSIZE samples
      {
        for(i = 0; i < BUFFER_SIZE * N_BLOCKS / 4; i++)
        {
          FFT_buffer[i] = 0.0;
        }
        first_block = 0;
      }
      else
      
// HERE IT STARTS for all other instances
// 6 a.) fill FFT_buffer with last events audio samples
      for(i = 0; i < BUFFER_SIZE * N_BLOCKS / 8; i++)
      {
        FFT_buffer[i * 2] = last_sample_buffer_L[i]; // real
        FFT_buffer[i * 2 + 1] = last_sample_buffer_R[i]; // imaginary
      }

   
    // copy recent samples to last_sample_buffer for next time!
      for(i = 0; i < BUFFER_SIZE * N_BLOCKS / 8; i++)
      {
         last_sample_buffer_L [i] = float_buffer_L[i]; 
         last_sample_buffer_R [i] = float_buffer_R[i]; 
      }
    
// 6. b) now fill recent audio samples into FFT_buffer (left channel: re, right channel: im)
      for(i = 0; i < BUFFER_SIZE * N_BLOCKS / 8; i++)
      {   
          FFT_buffer[FFT_length + i * 2] = float_buffer_L[i]; // real  
          FFT_buffer[FFT_length + i * 2 + 1] = float_buffer_R[i]; // imaginary
      }

// 6. c) and do windowing: Nuttall window
// window does not work properly, I think
// --> stuttering with the frequency of the FFT update depending on the sample size
/*
      for(i = 0; i < FFT_length; i++)
//      for(i = FFT_length/2; i < FFT_length; i++)
      { // SIN^2 window
          float32_t SINF = (sinf(PI * (float32_t)i / 1023.0f));
          SINF = SINF * SINF;
          FFT_buffer[i * 2] = SINF * FFT_buffer[i * 2];
          FFT_buffer[i * 2 + 1] = SINF * FFT_buffer[i * 2 + 1];
// Nuttal            
//          FFT_buffer[i * 2] = (0.355768 - (0.487396*arm_cos_f32((TPI*(float32_t)i)/(float32_t)2047)) + (0.144232*arm_cos_f32((FOURPI*(float32_t)i)/(float32_t)2047)) - (0.012604*arm_cos_f32((SIXPI*(float32_t)i)/(float32_t)2047))) * FFT_buffer[i * 2];
//          FFT_buffer[i * 2 + 1] = (0.355768 - (0.487396*arm_cos_f32((TPI*(float32_t)i)/(float32_t)2047)) + (0.144232*arm_cos_f32((FOURPI*(float32_t)i)/(float32_t)2047)) - (0.012604*arm_cos_f32((SIXPI*(float32_t)i)/(float32_t)2047))) * FFT_buffer[i * 2 + 1];
      }
*/

// perform complex FFT
// calculation is performed in-place the FFT_buffer [re, im, re, im, re, im . . .]
      arm_cfft_f32(S, FFT_buffer, 0, 1);

// AUTOTUNE, slow down process in order for Si5351 to settle 
      if(autotune_flag != 0)
      {
          if(autotune_counter < autotune_wait) autotune_counter++;
          else 
          {
              autotune_counter = 0;
              autotune();
          }
      }

/*
// frequency shift & choice of FFT_bins for "demodulation"
    switch (demod_mode) 
    {
      case DEMOD_AM:
////////////////////////////////////////////////////////////////////////////////
// AM demodulation - shift everything FFT_length / 4 bins around IF 
// leave both: USB & LSB
////////////////////////////////////////////////////////////////////////////////
// shift USB part by FFT_shift
    for(i = 0; i < FFT_shift; i++)
    {
        FFT_buffer[i] = FFT_buffer[i + FFT_length * 2 - FFT_shift];
    }
    
// now shift LSB part by FFT_shift
    for(i = (FFT_length * 2) - FFT_shift - 2; i > FFT_length - FFT_shift;i--)
     {
        FFT_buffer[i + FFT_shift] = FFT_buffer[i]; // shift LSB part by FFT_shift bins
     }
     break;
///// END AM-demodulation, seems to be OK (at least sounds ok)     
////////////////////////////////////////////////////////////////////////////////
// USB DEMODULATION
////////////////////////////////////////////////////////////////////////////////
    case DEMOD_USB:
    case DEMOD_STEREO_USB:
    for(i = FFT_length / 2; i < FFT_length ; i++) // maybe this is not necessary? yes, it is!
     {
        FFT_buffer[i] = FFT_buffer [i - FFT_shift]; // shift USB, 2nd part, to the right
     }

    // frequency shift of the USB 1st part - but this is essential 
    for(i = FFT_length * 2 - FFT_shift; i < FFT_length * 2; i++)
    {
        FFT_buffer[i - FFT_length * 2 + FFT_shift] = FFT_buffer[i];
    }
    // fill LSB with zeros
    for(i = FFT_length; i < FFT_length *2; i++)
     {
        FFT_buffer[i] = 0.0; // fill rest with zero
     }
    break;
// END USB: audio seems to be OK 
////////////////////////////////////////////////////////////////////////////////    
    case DEMOD_LSB:
    case DEMOD_STEREO_LSB:    
    case DEMOD_DCF77:
////////////////////////////////////////////////////////////////////////////////
// LSB demodulation
////////////////////////////////////////////////////////////////////////////////
    for(i = 0; i < FFT_length ; i++)
     {
        FFT_buffer[i] = 0.0; // fill USB with zero
     }
    // frequency shift of the LSB part 
    ////////////////////////////////////////////////////////////////////////////////
    for(i = (FFT_length * 2) - FFT_shift - 2; i > FFT_length - FFT_shift;i--)
     {
        FFT_buffer[i + FFT_shift] = FFT_buffer[i]; // shift LSB part by FFT_shift bins
     }
// fill USB with zeros
    for(i = FFT_length; i < FFT_length + FFT_shift; i++)
     {
        FFT_buffer[i] = 0.0; // fill rest with zero
     }
    break;
// END LSB: audio seems to be OK 
////////////////////////////////////////////////////////////////////////////////    
    } // END switch demod_mode
*/     

// choice of FFT_bins for "demodulation" WITHOUT frequency translation
// (because frequency translation has already been done in time domain with
// "frequency translation without multiplication" - DSP trick R. Lyons (2011))
    switch (band[bands].mode) 
    {
//      case DEMOD_AM1:
      case DEMOD_AUTOTUNE:
      case DEMOD_DSB:
      case DEMOD_SAM_USB:
      case DEMOD_SAM_LSB:

////////////////////////////////////////////////////////////////////////////////
// AM & SAM demodulation 
// DO NOTHING !
// leave both: USB & LSB
////////////////////////////////////////////////////////////////////////////////
     break;
////////////////////////////////////////////////////////////////////////////////
// USB DEMODULATION
////////////////////////////////////////////////////////////////////////////////
    case DEMOD_USB:
    case DEMOD_STEREO_USB:
    // fill LSB with zeros
    for(i = FFT_length; i < FFT_length * 2; i++)
     {
        FFT_buffer[i] = 0.0;
     }
    break;
////////////////////////////////////////////////////////////////////////////////    
    case DEMOD_LSB:
    case DEMOD_STEREO_LSB:    
    case DEMOD_DCF77:
////////////////////////////////////////////////////////////////////////////////
// LSB demodulation
////////////////////////////////////////////////////////////////////////////////
    for(i = 4; i < FFT_length ; i++) 
    // when I delete all the FFT_buffer products (from i = 0 to FFT_length), LSB is much louder! --> effect of the AGC !!1
    // so, I leave indices 0 to 3 in the buffer 
     {
        FFT_buffer[i] = 0.0; // fill USB with zero
     }
    break;
////////////////////////////////////////////////////////////////////////////////    
    } // END switch demod_mode

// complex multiply with filter mask
     arm_cmplx_mult_cmplx_f32 (FFT_buffer, FIR_filter_mask, iFFT_buffer, FFT_length);
//    arm_copy_f32(FFT_buffer, iFFT_buffer, FFT_length * 2);


// filter by just deleting bins - principle of Linrad
// only works properly when we have the right window function!?

// (automatic) notch filter = Tone killer --> the name is stolen from SNR ;-)
// first test, we set a notch filter at 1kHz
// which bin is that?
// positive & negative frequency -1kHz and +1kHz --> delete 2 bins
// we are not deleting one bin, but five bins for the test
// 1024 bins in 12ksps = 11.71Hz per bin
// SR[SAMPLE_RATE].rate / 8.0 / 1024 = bin BW

// 1000Hz / 11.71Hz = bin 85.333
//
// set ten notches
/*  for(int n = 0; n < 10; n++)
  {
    bin = notches[n] * 2.0 / bin_BW;
    for(int zz = bin; zz < bin+4; zz++)
    {
        iFFT_buffer[zz] = 0.0; 
        iFFT_buffer[2048 - zz] = 0.0;
    }
  }
*/
// lowpass 2.1kHz
//  float32_t bin = bands[band].bandwidthU * 2.0 / bin_BW;
//  float32_t bin = LP_F_help * 2.0 / bin_BW;
/*
  for(int zz = bin; zz < FFT_length; zz++)
  {
      iFFT_buffer[zz] = 0.0; 
      iFFT_buffer[2048 - zz] = 0.0;
  }
*/

// highpass 234Hz
// never delete the carrier !!! ;-)
// always preserve bin 0 ()
/*  for(int zz = 5; zz < 40; zz++)
  {
      iFFT_buffer[zz] = 0.0; 
      iFFT_buffer[2047 - zz] = 0.0;
  }

// lowpass
  for(int zz = bin; zz < FFT_length; zz++)
  {
      iFFT_buffer[zz] = 0.0; 
      iFFT_buffer[2047 - zz] = 0.0;
  }
*/
// automatic notch:
// detect bins where the power level remains the same for at least 200ms
// set these bins to zero
/*  for(i = 0; i < 1024; i++)
  {
      float32_t bin_p = sqrtf((FFT_buffer[i * 2] * FFT_buffer[i * 2]) + (FFT_buffer[i * 2 + 1] * FFT_buffer[i * 2 + 1]));
      notch_amp[i] = bin_p; 
  }
*/

// spectral noise reduction
// if sample rate = 96kHz, we are in 12ksps now, because we decimated by 8

// perform iFFT (in-place)
     arm_cfft_f32(iS, iFFT_buffer, 1, 1);
/*
// apply window the other way round !
      for(i = 0; i < FFT_length; i++)
//      for(i = FFT_length / 2; i < FFT_length; i++)
      { // SIN^2 window
          float32_t SINF = (sinf(PI * (float32_t)i / 1023.0));
          SINF = (SINF * SINF);
          SINF = 1.0f / SINF;
          iFFT_buffer[i * 2] = SINF * iFFT_buffer[i * 2];
          if(iFFT_flip == 1)
          {
              iFFT_buffer[i * 2 + 1] = SINF * iFFT_buffer[i * 2 + 1];
          }
          else
          {
              iFFT_buffer[i * 2 + 1] = - SINF * iFFT_buffer[i * 2 + 1];
          }
//          Serial.print(iFFT_buffer[i*2]); Serial.print("  ");
      }
      if(iFFT_flip == 1) iFFT_flip = 0;
      else iFFT_flip = 1;
*/          

/**********************************************************************************
 *  AGC - automatic gain control
 *  
 *  we´re back in time domain
 *  AGC acts upon I & Q before demodulation on the decimated audio data in iFFT_buffer
 **********************************************************************************/

    AGC();

/**********************************************************************************
 *  Demodulation
 **********************************************************************************/

// our desired output is a combination of the real part (left channel) AND the imaginary part (right channel) of the second half of the FFT_buffer
// which one and how they are combined is dependent upon the demod_mode . . .


      if(band[bands].mode == DEMOD_SAM || band[bands].mode == DEMOD_SAM_LSB ||band[bands].mode == DEMOD_SAM_USB ||band[bands].mode == DEMOD_SAM_STEREO)
      {   // taken from Warren Pratt´s WDSP, 2016
          // http://svn.tapr.org/repos_sdr_hpsdr/trunk/W5WC/PowerSDR_HPSDR_mRX_PS/Source/wdsp/

        for(i = 0; i < FFT_length / 2; i++)
        {
            sincosf(phzerror,&Sin,&Cos);
            ai = Cos * iFFT_buffer[FFT_length + i * 2];
            bi = Sin * iFFT_buffer[FFT_length + i * 2];
            aq = Cos * iFFT_buffer[FFT_length + i * 2 + 1];
            bq = Sin * iFFT_buffer[FFT_length + i * 2 + 1];
 
            if (band[bands].mode != DEMOD_SAM)
            {
              a[0] = dsI;
              b[0] = bi;
              c[0] = dsQ;
              d[0] = aq;
              dsI = ai;
              dsQ = bq;

              for (int j = 0; j < SAM_PLL_HILBERT_STAGES; j++)
              {
                k = 3 * j;
                a[k + 3] = c0[j] * (a[k] - a[k + 5]) + a[k + 2];
                b[k + 3] = c1[j] * (b[k] - b[k + 5]) + b[k + 2];
                c[k + 3] = c0[j] * (c[k] - c[k + 5]) + c[k + 2];
                d[k + 3] = c1[j] * (d[k] - d[k + 5]) + d[k + 2];
              }
              ai_ps = a[OUT_IDX];
              bi_ps = b[OUT_IDX];
              bq_ps = c[OUT_IDX];
              aq_ps = d[OUT_IDX];

              for (j = OUT_IDX + 2; j > 0; j--)
              {
                a[j] = a[j - 1];
                b[j] = b[j - 1];
                c[j] = c[j - 1];
                d[j] = d[j - 1];
              }
            }

            corr[0] = +ai + bq;
            corr[1] = -bi + aq;

            switch(band[bands].mode)
            {
            case DEMOD_SAM:
              {
                audio = corr[0];
                break;
              }
            case DEMOD_SAM_USB:
              {
                audio = (ai_ps - bi_ps) + (aq_ps + bq_ps);
                break;
              }
            case DEMOD_SAM_LSB:
              {
                audio = (ai_ps + bi_ps) - (aq_ps - bq_ps);
                break;
              }
            case DEMOD_SAM_STEREO:
              {
                audio = (ai_ps + bi_ps) - (aq_ps - bq_ps);
                audiou = (ai_ps - bi_ps) + (aq_ps + bq_ps);
                break;
              }
            }
            if(fade_leveler)
            {
            dc = mtauR * dc + onem_mtauR * audio;
            dc_insert = mtauI * dc_insert + onem_mtauI * corr[0];
            audio = audio + dc_insert - dc;
            }
            float_buffer_L[i] = audio;
            if(band[bands].mode == DEMOD_SAM_STEREO)
            {            
              if(fade_leveler)
              {
                  dcu = mtauR * dcu + onem_mtauR * audiou;
                  dc_insertu = mtauI * dc_insertu + onem_mtauI * corr[0];
                  audiou = audiou + dc_insertu - dcu;
              }
              float_buffer_R[i] = audiou;
            }
            
            det = atan2f(corr[1], corr[0]);
//            Serial.println(corr[1] * 100000);
//            Serial.println(corr[0] * 100000);
              // is not at all faster than atan2f !
//            det = atan2_fast(corr[1], corr[0]);

                del_out = fil_out;
                omega2 = omega2 + g2 * det;
                if (omega2 < omega_min) omega2 = omega_min;
                else if (omega2 > omega_max) omega2 = omega_max;
                fil_out = g1 * det + omega2;
                phzerror = phzerror + del_out;
           
            // wrap round 2PI, modulus
            while (phzerror >= TPI) phzerror -= TPI;
            while (phzerror < 0.0) phzerror += TPI;
        } 
        if(band[bands].mode != DEMOD_SAM_STEREO) 
        {
            arm_copy_f32(float_buffer_L, float_buffer_R, FFT_length/2);
        }
//        SAM_display_count++;
//        if(SAM_display_count > 50) // to display the exact carrier frequency that the PLL is tuned to
//        if(0)
        // in the small frequency display
            // we calculate carrier offset here and the display function is
            // then called in main loop every 100ms
        { // to make this smoother, a simple lowpass/exponential averager here . . .
            SAM_carrier = 0.08 * (omega2 * SR[SAMPLE_RATE].rate) / (DF * TPI);
            SAM_carrier = SAM_carrier + 0.92 * SAM_lowpass;
            SAM_carrier_freq_offset =  (int)SAM_carrier;
//            SAM_display_count = 0;
            SAM_lowpass = SAM_carrier;
            show_frequency(bands[band].freq, 0);
        }
      }
      else
/*      if(demod_mode == DEMOD_AM1)
      { // E(t) = sqrt(I*I + Q*Q) with arm function --> moving average DC removal --> lowpass IIR 2nd order
        // this AM demodulator with arm_cmplx_mag_f32 saves 1.23% of processor load at 96ksps compared to using arm_sqrt_f32
        arm_cmplx_mag_f32(&iFFT_buffer[FFT_length], float_buffer_R, FFT_length/2);
        // DC removal
        last_dc_level = fastdcblock_ff(float_buffer_R, float_buffer_L, FFT_length / 2, last_dc_level);
        // lowpass-filter the AM output to reduce high frequency noise produced by the envelope demodulator
        // see Whiteley 2011 for an explanation of this problem
        // 45.18% with IIR; 43.29% without IIR 
        arm_biquad_cascade_df1_f32 (&biquad_lowpass1, float_buffer_L, float_buffer_R, FFT_length / 2);
        arm_copy_f32(float_buffer_R, float_buffer_L, FFT_length/2);
      }
      else */
      if(band[bands].mode == DEMOD_AM2)
      { // // E(t) = sqrtf(I*I + Q*Q) --> highpass IIR 1st order for DC removal --> lowpass IIR 2nd order
          for(i = 0; i < FFT_length / 2; i++)
          { // 
                audiotmp = sqrtf(iFFT_buffer[FFT_length + (i * 2)] * iFFT_buffer[FFT_length + (i * 2)] 
                                    + iFFT_buffer[FFT_length + (i * 2) + 1] * iFFT_buffer[FFT_length + (i * 2) + 1]); 
                // DC removal filter ----------------------- 
                w = audiotmp + wold * 0.9999f; // yes, I want a superb bass response ;-) 
                float_buffer_L[i] = w - wold; 
                wold = w; 
          }
          arm_biquad_cascade_df1_f32 (&biquad_lowpass1, float_buffer_L, float_buffer_R, FFT_length / 2);
          arm_copy_f32(float_buffer_R, float_buffer_L, FFT_length/2);
      }
      else 
/*      if(band[bands].mode == DEMOD_AM3)
      { // // E(t) = sqrt(I*I + Q*Q) --> highpass IIR 1st order for DC removal --> no lowpass 
          for(i = 0; i < FFT_length / 2; i++)
          { // 
                audiotmp = sqrtf(iFFT_buffer[FFT_length + (i * 2)] * iFFT_buffer[FFT_length + (i * 2)] 
                                    + iFFT_buffer[FFT_length + (i * 2) + 1] * iFFT_buffer[FFT_length + (i * 2) + 1]); 
                // DC removal filter ----------------------- 
                w = audiotmp + wold * 0.9999f; // yes, I want a superb bass response ;-) 
                float_buffer_L[i] = w - wold; 
                wold = w; 
          }
          arm_copy_f32(float_buffer_L, float_buffer_R, FFT_length/2);
      }
      else 
      if(band[bands].mode == DEMOD_AM_AE1)
      {  // E(n) = |I| + |Q| --> moving average DC removal --> lowpass 2nd order IIR
        // Approximate envelope detection, Lyons (2011): page 756
         // E(n) = |I| + |Q| --> lowpass (here I use a 2nd order IIR instead of the 1st order IIR of the original publication)
          for(i = 0; i < FFT_length / 2; i++)
          {
                float_buffer_R[i] = abs(iFFT_buffer[FFT_length + (i * 2)]) + abs(iFFT_buffer[FFT_length + (i * 2) + 1]); 
          }
        // DC removal
        last_dc_level = fastdcblock_ff(float_buffer_R, float_buffer_L, FFT_length / 2, last_dc_level);
        arm_biquad_cascade_df1_f32 (&biquad_lowpass1, float_buffer_L, float_buffer_R, FFT_length / 2);
        arm_copy_f32(float_buffer_R, float_buffer_L, FFT_length/2);
      }
      else 
      if(band[bands].mode == DEMOD_AM_AE2)
      {  // E(n) = |I| + |Q| --> highpass IIR 1st order for DC removal --> lowpass 2nd order IIR 
         // Approximate envelope detection, Lyons (2011): page 756
         // E(n) = |I| + |Q| --> lowpass (here I use a 2nd order IIR instead of the 1st order IIR of the original publication)
          for(i = 0; i < FFT_length / 2; i++)
          {
            audiotmp = abs(iFFT_buffer[FFT_length + (i * 2)]) + abs(iFFT_buffer[FFT_length + (i * 2) + 1]); 
            // DC removal filter ----------------------- 
            w = audiotmp + wold * 0.9999f; // yes, I want a superb bass response ;-) 
            float_buffer_L[i] = w - wold; 
            wold = w; 
          }
        arm_biquad_cascade_df1_f32 (&biquad_lowpass1, float_buffer_L, float_buffer_R, FFT_length / 2);
        arm_copy_f32(float_buffer_R, float_buffer_L, FFT_length/2);
      }
      else 
      if(band[bands].mode == DEMOD_AM_AE3)
      {  // E(n) = |I| + |Q| --> highpass IIR 1st order for DC removal --> NO lowpass
         // Approximate envelope detection, Lyons (2011): page 756
         // E(n) = |I| + |Q| --> lowpass 1st order IIR
          for(i = 0; i < FFT_length / 2; i++)
          {
            audiotmp = abs(iFFT_buffer[FFT_length + (i * 2)]) + abs(iFFT_buffer[FFT_length + (i * 2) + 1]); 
            // DC removal filter ----------------------- 
            w = audiotmp + wold * 0.9999f; // yes, I want a superb bass response ;-) 
            float_buffer_L[i] = w - wold; 
            wold = w; 
          }
        arm_copy_f32(float_buffer_L, float_buffer_R, FFT_length/2);
      }
      else
      if(band[bands].mode == DEMOD_AM_ME1)
      {  // E(n) = alpha * max [I, Q] + beta * min [I, Q] --> moving average DC removal --> lowpass 2nd order IIR
         // Magnitude estimation Lyons (2011): page 652 / libcsdr
          for(i = 0; i < FFT_length / 2; i++)
          {
                float_buffer_R[i] = alpha_beta_mag(iFFT_buffer[FFT_length + (i * 2)], iFFT_buffer[FFT_length + (i * 2) + 1]);
          }
        // DC removal
        last_dc_level = fastdcblock_ff(float_buffer_R, float_buffer_L, FFT_length / 2, last_dc_level);
        arm_biquad_cascade_df1_f32 (&biquad_lowpass1, float_buffer_L, float_buffer_R, FFT_length / 2);
        arm_copy_f32(float_buffer_R, float_buffer_L, FFT_length/2);
      }
      else */
      if(band[bands].mode == DEMOD_AM_ME2)
      {  // E(n) = alpha * max [I, Q] + beta * min [I, Q] --> highpass 1st order DC removal --> lowpass 2nd order IIR
         // Magnitude estimation Lyons (2011): page 652 / libcsdr
          for(i = 0; i < FFT_length / 2; i++)
          {
                audiotmp = alpha_beta_mag(iFFT_buffer[FFT_length + (i * 2)], iFFT_buffer[FFT_length + (i * 2) + 1]);
                // DC removal filter ----------------------- 
                w = audiotmp + wold * 0.9999f; // yes, I want a superb bass response ;-) 
                float_buffer_L[i] = w - wold; 
                wold = w; 
          }
        arm_biquad_cascade_df1_f32 (&biquad_lowpass1, float_buffer_L, float_buffer_R, FFT_length / 2);
        arm_copy_f32(float_buffer_R, float_buffer_L, FFT_length/2);
      }
      else
/*      if(band[bands].mode == DEMOD_AM_ME3)
      {  // E(n) = alpha * max [I, Q] + beta * min [I, Q] --> highpass 1st order DC removal --> NO lowpass
         // Magnitude estimation Lyons (2011): page 652 / libcsdr
          for(i = 0; i < FFT_length / 2; i++)
          {
                audiotmp = alpha_beta_mag(iFFT_buffer[FFT_length + (i * 2)], iFFT_buffer[FFT_length + (i * 2) + 1]);
                // DC removal filter ----------------------- 
                w = audiotmp + wold * 0.9999f; // yes, I want a superb bass response ;-) 
                float_buffer_L[i] = w - wold; 
                wold = w; 
          }
        arm_copy_f32(float_buffer_L, float_buffer_R, FFT_length/2);
      }
      else */
      for(i = 0; i < FFT_length / 2; i++)
      {
        if(band[bands].mode == DEMOD_USB || band[bands].mode == DEMOD_LSB || band[bands].mode == DEMOD_DCF77 || band[bands].mode == DEMOD_AUTOTUNE)
        {
            float_buffer_L[i] = iFFT_buffer[FFT_length + (i * 2)]; 
            // for SSB copy real part in both outputs
            float_buffer_R[i] = float_buffer_L[i];
        }
        else if(band[bands].mode == DEMOD_STEREO_LSB || band[bands].mode == DEMOD_STEREO_USB) // creates a pseudo-stereo effect
            // could be good for copying faint CW signals
        {
            float_buffer_L[i] = iFFT_buffer[FFT_length + (i * 2)]; 
            float_buffer_R[i] = iFFT_buffer[FFT_length + (i * 2) + 1];
        }
      }
 /**********************************************************************************
 *  EXPERIMENTAL STATION FOR SPECTRAL NOISE REDUCTION
 *  only one channel --> float_buffer_L
 **********************************************************************************/
/*// we have 1024 real samples in float_buffer_L
// so we have to perform FFT 256 four times
      for (k = 0; k < 4; k++)
      {
      if(first_block) // fill real & imaginaries with zeros for the first BLOCKSIZE samples
      {
        for(i = 0; i < 256; i++)
        {
          FFT_buffer[i] = 0.0;
        }
        first_block = 0;
      }
      else
      
// HERE IT STARTS for all other instances
// 6 a.) fill FFT_buffer with last events audio samples
      for(i = 0; i < BUFFER_SIZE * N_BLOCKS / 8; i++)
      {
        FFT_buffer[i * 2] = last_sample_buffer_L[i]; // real
        FFT_buffer[i * 2 + 1] = last_sample_buffer_R[i]; // imaginary
      }

   
    // copy recent samples to last_sample_buffer for next time!
      for(i = 0; i < BUFFER_SIZE * N_BLOCKS / 8; i++)
      {
         last_sample_buffer_L [i] = float_buffer_L[i]; 
         last_sample_buffer_R [i] = float_buffer_R[i]; 
      }
    
// 6. b) now fill recent audio samples into FFT_buffer (left channel: re, right channel: im)
      for(i = 0; i < BUFFER_SIZE * N_BLOCKS / 8; i++)
      {
          FFT_buffer[FFT_length + i * 2] = float_buffer_L[i]; // real  
          FFT_buffer[FFT_length + i * 2 + 1] = float_buffer_R[i]; // imaginary
      }
      }

// iFFT 256
// overlap & save

*/      
/**********************************************************************************
 *  INTERPOLATION
 **********************************************************************************/
// re-uses iFFT_buffer[2048] and FFT_buffer !!!

// receives 512 samples and makes 4096 samples out of it
// interpolation-by-2
// interpolation-in-place does not work
      arm_fir_interpolate_f32(&FIR_int1_I, float_buffer_L, iFFT_buffer, BUFFER_SIZE * N_BLOCKS / 8);
      arm_fir_interpolate_f32(&FIR_int1_Q, float_buffer_R, FFT_buffer, BUFFER_SIZE * N_BLOCKS / 8);

// interpolation-by-4
      arm_fir_interpolate_f32(&FIR_int2_I, iFFT_buffer, float_buffer_L, BUFFER_SIZE * N_BLOCKS / 4);
      arm_fir_interpolate_f32(&FIR_int2_Q, FFT_buffer, float_buffer_R, BUFFER_SIZE * N_BLOCKS / 4);

// scale after interpolation
      float32_t interpol_scale = 8.0;
      if(band[bands].mode == DEMOD_LSB || band[bands].mode == DEMOD_USB) interpol_scale = 16.0;
//      if(band[bands].mode == DEMOD_USB) interpol_scale = 16.0;
      arm_scale_f32(float_buffer_L, interpol_scale, float_buffer_L, BUFFER_SIZE * N_BLOCKS);
      arm_scale_f32(float_buffer_R, interpol_scale, float_buffer_R, BUFFER_SIZE * N_BLOCKS);

/**********************************************************************************
 *  CONVERT TO INTEGER AND PLAY AUDIO
 **********************************************************************************/
    for (i = 0; i < N_BLOCKS; i++)
    {
      sp_L = Q_out_L.getBuffer();
      sp_R = Q_out_R.getBuffer();
      arm_float_to_q15 (&float_buffer_L[BUFFER_SIZE * i], sp_L, BUFFER_SIZE); 
      arm_float_to_q15 (&float_buffer_R[BUFFER_SIZE * i], sp_R, BUFFER_SIZE); 
      Q_out_L.playBuffer(); // play it !
      Q_out_R.playBuffer(); // play it !
    }  

/**********************************************************************************
 *  PRINT ROUTINE FOR ELAPSED MICROSECONDS
 **********************************************************************************/
 
      sum = sum + usec;
      idx_t++;
      if (idx_t > 40) {
          tft.fillRect(260,5,60,20,ILI9341_BLACK);   
          tft.setCursor(260, 5);
          tft.setTextColor(ILI9341_GREEN);
          tft.setFont(Arial_9);
          mean = sum / idx_t;
          tft.print (mean/29.00/N_BLOCKS * SR[SAMPLE_RATE].rate / AUDIO_SAMPLE_RATE_EXACT);tft.print("%");
          Serial.print (mean);
          Serial.print (" microsec for ");
          Serial.print (N_BLOCKS);
          Serial.print ("  stereo blocks    ");
          Serial.println();
          Serial.print (" n_clear    ");
          Serial.println(n_clear);
          idx_t = 0;
          sum = 0;
          tft.setTextColor(ILI9341_WHITE);
      }
      /*
          if(zoom_display)
          {
            show_spectrum();
            zoom_display = 0;
          zoom_sample_ptr = 0;
          }
          */
          if(band[bands].mode == DEMOD_DCF77)
          { 
          }
    if(auto_codec_gain == 1)
    {
          codec_gain();
    }
     } // end of if(audio blocks available)
 
/**********************************************************************************
 *  PRINT ROUTINE FOR AUDIO LIBRARY PROCESSOR AND MEMORY USAGE
 **********************************************************************************/
          if (0) //(five_sec.check() == 1)
    {
      Serial.print("Proc = ");
      Serial.print(AudioProcessorUsage());
      Serial.print(" (");    
      Serial.print(AudioProcessorUsageMax());
      Serial.print("),  Mem = ");
      Serial.print(AudioMemoryUsage());
      Serial.print(" (");    
      Serial.print(AudioMemoryUsageMax());
      Serial.println(")");
      AudioProcessorUsageMaxReset();
      AudioMemoryUsageMaxReset();
//      eeprom_saved = 0;
//      eeprom_loaded = 0;
    }
//    show_spectrum();

    if(encoder_check.check() == 1)
    {
        encoders();
        buttons();
        displayClock();
        show_analog_gain();
//        Serial.print("SAM carrier frequency = "); Serial.println(SAM_carrier_freq_offset);
    }

    if (ms_500.check() == 1)
        wait_flag = 0;

//    if(dbm_check.check() == 1) Calculatedbm();

    if(Menu_pointer == MENU_PLAYER)
    {
        if(trackext[track] == 1)
        {
          Serial.println("MP3" );
          playFileMP3(playthis);
        }
        else if(trackext[track] == 2)
        {
          Serial.println("aac");
          playFileAAC(playthis);
        }
        if(trackchange == true)
        { //when your track is finished, go to the next one. when using buttons, it will not skip twice.
            nexttrack();
        }
    }
    
} // end loop

void IQ_phase_correction (float32_t *I_buffer, float32_t *Q_buffer, float32_t factor, uint32_t blocksize) 
{
  float32_t temp_buffer[blocksize];
  if(factor < 0.0)
  { // mix a bit of I into Q
      arm_scale_f32 (I_buffer, factor, temp_buffer, blocksize);
      arm_add_f32 (Q_buffer, temp_buffer, Q_buffer, blocksize);
  }
  else
  { // // mix a bit of Q into I
      arm_scale_f32 (Q_buffer, factor, temp_buffer, blocksize);
      arm_add_f32 (I_buffer, temp_buffer, I_buffer, blocksize);
  }
} // end IQ_phase_correction

void AGC_prep()
{
  float32_t tmp;
  float32_t sample_rate = (float32_t)SR[SAMPLE_RATE].rate / DF;
// Start variables taken from wdsp
// RXA.c !!!!
/*
    0.001,                      // tau_attack
    0.250,                      // tau_decay
    4,                        // n_tau
    10000.0,                    // max_gain
    1.5,                      // var_gain
    1000.0,                     // fixed_gain
    1.0,                      // max_input
    1.0,                      // out_target
    0.250,                      // tau_fast_backaverage
    0.005,                      // tau_fast_decay
    5.0,                      // pop_ratio
    1,                        // hang_enable
    0.500,                      // tau_hang_backmult
    0.250,                      // hangtime
    0.250,                      // hang_thresh
    0.100);                     // tau_hang_decay
 */
/*  GOOD WORKING VARIABLES
    max_gain = 1.0;                    // max_gain
    var_gain = 0.0015; // 1.5                      // var_gain
    fixed_gain = 1.0;                     // fixed_gain
    max_input = 1.0;                 // max_input
    out_target = 0.00005; //0.0001; // 1.0                // out_target

 */
    tau_attack = 0.001;               // tau_attack
//    tau_decay = 0.250;                // tau_decay
    n_tau = 1;                        // n_tau
 
//    max_gain = 1000.0; // 1000.0; max gain to be applied??? or is this AGC threshold = knee level?
    fixed_gain = 0.7; // if AGC == OFF
    max_input = 2.0; //
    out_targ = 0.3; // target value of audio after AGC
//    var_gain = 32.0;  // slope of the AGC --> this is 10 * 10^(slope / 20) --> for 10dB slope, this is 30
    var_gain = powf (10.0, (float32_t)agc_slope / 200.0); // 10 * 10^(slope / 20)

    tau_fast_backaverage = 0.250;    // tau_fast_backaverage
    tau_fast_decay = 0.005;          // tau_fast_decay
    pop_ratio = 5.0;                 // pop_ratio
    hang_enable = 0;                 // hang_enable
    tau_hang_backmult = 0.500;       // tau_hang_backmult
    hangtime = 0.250;                // hangtime
    hang_thresh = 0.250;             // hang_thresh
    tau_hang_decay = 0.100;          // tau_hang_decay

  //calculate internal parameters
    if(agc_switch_mode)
    {
    switch (AGC_mode)
      {
        case 0: //agcOFF
          break;
        case 2: //agcLONG
          hangtime = 2.000;
          agc_decay = 2000;
          break;
        case 3: //agcSLOW
          hangtime = 1.000;
          agc_decay = 500;
          break;
        case 4: //agcMED
          hang_thresh = 1.0;
          hangtime = 0.000;
          agc_decay = 250;
          break;
        case 5: //agcFAST
          hang_thresh = 1.0;
          hangtime = 0.000;
          agc_decay = 50;
          break;
        case 1: //agcFrank
          hang_enable = 0;
          hang_thresh = 0.100; // from which level on should hang be enabled
          hangtime = 2.000; // hang time, if enabled
          tau_hang_backmult = 0.500; // time constant exponential averager
          
          agc_decay = 4000; // time constant decay long
          tau_fast_decay = 0.05;          // tau_fast_decay
          tau_fast_backaverage = 0.250; // time constant exponential averager
    //      max_gain = 1000.0; // max gain to be applied??? or is this AGC threshold = knee level?
    //      fixed_gain = 1.0; // if AGC == OFF
    //      max_input = 1.0; //
    //      out_targ = 0.2; // target value of audio after AGC
    //      var_gain = 30.0;  // slope of the AGC --> 
    
    /*    // sehr gut!
     *     hang_thresh = 0.100;
          hangtime = 2.000;
          tau_decay = 2.000;
          tau_hang_backmult = 0.500;
          tau_fast_backaverage = 0.250;
          out_targ = 0.0004;
          var_gain = 0.001; */
          break;
        default:
          break;
        }
    agc_switch_mode = 0;
    }
  tau_decay = (float32_t)agc_decay / 1000.0;
  max_gain = powf (10.0, (float32_t)agc_thresh / 20.0);

  attack_buffsize = (int)ceil(sample_rate * n_tau * tau_attack);
  Serial.println(attack_buffsize);
  in_index = attack_buffsize + out_index;
  attack_mult = 1.0 - expf(-1.0 / (sample_rate * tau_attack));
  Serial.print("attack_mult = ");
  Serial.println(attack_mult * 1000);
  decay_mult = 1.0 - expf(-1.0 / (sample_rate * tau_decay));
  Serial.print("decay_mult = ");
  Serial.println(decay_mult * 1000);
  fast_decay_mult = 1.0 - expf(-1.0 / (sample_rate * tau_fast_decay));
  Serial.print("fast_decay_mult = ");
  Serial.println(fast_decay_mult * 1000);
  fast_backmult = 1.0 - expf(-1.0 / (sample_rate * tau_fast_backaverage));
  Serial.print("fast_backmult = ");
  Serial.println(fast_backmult * 1000);

  onemfast_backmult = 1.0 - fast_backmult;

  out_target = out_targ * (1.0 - expf(-(float32_t)n_tau)) * 0.9999;
//  out_target = out_target * (1.0 - expf(-(float32_t)n_tau)) * 0.9999;
  Serial.print("out_target = ");
  Serial.println(out_target * 1000);
  min_volts = out_target / (var_gain * max_gain);
  inv_out_target = 1.0 / out_target;

  tmp = log10f(out_target / (max_input * var_gain * max_gain));
  if (tmp == 0.0)
    tmp = 1e-16;
  slope_constant = (out_target * (1.0 - 1.0 / var_gain)) / tmp;
  Serial.print("slope_constant = ");
  Serial.println(slope_constant * 1000);

  inv_max_input = 1.0 / max_input;

  tmp = powf (10.0, (hang_thresh - 1.0) / 0.125);
  hang_level = (max_input * tmp + (out_target / 
    (var_gain * max_gain)) * (1.0 - tmp)) * 0.637;

  hang_backmult = 1.0 - expf(-1.0 / (sample_rate * tau_hang_backmult));
  onemhang_backmult = 1.0 - hang_backmult;

  hang_decay_mult = 1.0 - expf(-1.0 / (sample_rate * tau_hang_decay));
}

   
void AGC()
{
  int i, j, k;
  float32_t mult;

    if (AGC_mode == 0)  // AGC OFF
    {
      for (i = 0; i < FFT_length / 2; i++)
      {
        iFFT_buffer[FFT_length + 2 * i + 0] = fixed_gain * iFFT_buffer[FFT_length + 2 * i + 0];
        iFFT_buffer[FFT_length + 2 * i + 1] = fixed_gain * iFFT_buffer[FFT_length + 2 * i + 1];
      }
      return;
    }
  
    for (i = 0; i < FFT_length / 2; i++)
    {
      if (++out_index >= ring_buffsize)
        out_index -= ring_buffsize;
      if (++in_index >= ring_buffsize)
        in_index -= ring_buffsize;
  
      out_sample[0] = ring[2 * out_index + 0];
      out_sample[1] = ring[2 * out_index + 1];
      abs_out_sample = abs_ring[out_index];
      ring[2 * in_index + 0] = iFFT_buffer[FFT_length + 2 * i + 0];
      ring[2 * in_index + 1] = iFFT_buffer[FFT_length + 2 * i + 1];
      if (pmode == 0) // MAGNITUDE CALCULATION
        abs_ring[in_index] = max(fabs(ring[2 * in_index + 0]), fabs(ring[2 * in_index + 1]));
      else
        abs_ring[in_index] = sqrtf(ring[2 * in_index + 0] * ring[2 * in_index + 0] + ring[2 * in_index + 1] * ring[2 * in_index + 1]);

      fast_backaverage = fast_backmult * abs_out_sample + onemfast_backmult * fast_backaverage;
      hang_backaverage = hang_backmult * abs_out_sample + onemhang_backmult * hang_backaverage;

      if ((abs_out_sample >= ring_max) && (abs_out_sample > 0.0))
      {
        ring_max = 0.0;
        k = out_index;
        for (j = 0; j < attack_buffsize; j++)
        {
          if (++k == ring_buffsize)
            k = 0;
          if (abs_ring[k] > ring_max)
            ring_max = abs_ring[k];
        }
      }
      if (abs_ring[in_index] > ring_max)
        ring_max = abs_ring[in_index];

      if (hang_counter > 0)
        --hang_counter;
//      Serial.println(ring_max);
//      Serial.println(volts);
      
      switch (state)
      {
      case 0:
        {
          if (ring_max >= volts)
          {
            volts += (ring_max - volts) * attack_mult;
          }
          else
          {
            if (volts > pop_ratio * fast_backaverage)
            {
              state = 1;
              volts += (ring_max - volts) * fast_decay_mult;
            }
            else
            {
              if (hang_enable && (hang_backaverage > hang_level))
              {
                state = 2;
                hang_counter = (int)(hangtime * SR[SAMPLE_RATE].rate / DF);
                decay_type = 1;
              }
              else
              {
                state = 3;
                volts += (ring_max - volts) * decay_mult;
                decay_type = 0;
              }
            }
          }
          break;
        }
      case 1:
        {
          if (ring_max >= volts)
          {
            state = 0;
            volts += (ring_max - volts) * attack_mult;
          }
          else
          {
            if (volts > save_volts)
            {
              volts += (ring_max - volts) * fast_decay_mult;
            }
            else
            {
              if (hang_counter > 0)
              {
                state = 2;
              }
              else
              {
                if (decay_type == 0)
                {
                  state = 3;
                  volts += (ring_max - volts) * decay_mult;
                }
                else
                {
                  state = 4;
                  volts += (ring_max - volts) * hang_decay_mult;
                }
              }
            }
          }
          break;
        }
      case 2:
        {
          if (ring_max >= volts)
          {
            state = 0;
            save_volts = volts;
            volts += (ring_max - volts) * attack_mult;
          }
          else
          {
            if (hang_counter == 0)
            {
              state = 4;
              volts += (ring_max - volts) * hang_decay_mult;
            }
          }
          break;
        }
      case 3:
        {
          if (ring_max >= volts)
          {
            state = 0;
            save_volts = volts;
            volts += (ring_max - volts) * attack_mult;
          }
          else
          {
            volts += (ring_max - volts) * decay_mult;
          }
          break;
        }
      case 4:
        {
          if (ring_max >= volts)
          {
            state = 0;
            save_volts = volts;
            volts += (ring_max - volts) * attack_mult;
          }
          else
          {
            volts += (ring_max - volts) * hang_decay_mult;
          }
          break;
        }
      }
      if (volts < min_volts)
          {
                  volts = min_volts; // no AGC action is taking place
                  agc_action = 0;
          }
      else
          {
          // LED indicator for AGC action
                  agc_action = 1;
          } 
//      Serial.println(volts * inv_out_target);
      mult = (out_target - slope_constant * min (0.0, log10f(inv_max_input * volts))) / volts;
//    Serial.println(mult * 1000);
//      Serial.println(volts * 1000);
      iFFT_buffer[FFT_length + 2 * i + 0] = out_sample[0] * mult;
      iFFT_buffer[FFT_length + 2 * i + 1] = out_sample[1] * mult;
    }
  }

void filter_bandwidth() 
{
//    LP_F_help = (int)((float)LP_F_help * 0.3 + 0.7 * (float)LP_Fpass_old);
//    LP_F_help = (int)(LP_F_help/100 * 100);
//    LP_F_help += 100;
//    if(LP_F_help != LP_Fpass_old)
//    { //audio_flag_counter = 1000;

    AudioNoInterrupts();
    sgtl5000_1.dacVolume(0.0);
    //LP_F_help = max(bands[band].bandwidthU, bands[band].bandwidthL);
    calc_FIR_coeffs (FIR_Coef, 513, (float32_t)LP_F_help, LP_Astop, 0, 0.0, SR[SAMPLE_RATE].rate / DF);
    init_filter_mask();

    // also adjust IIR AM filter
    set_IIR_coeffs ((float32_t)LP_F_help, 1.3, (float32_t)SR[SAMPLE_RATE].rate / DF, 0); // 1st stage
//    set_IIR_coeffs ((float32_t)LP_F_help, 1.3, (float32_t)SR[SAMPLE_RATE].rate / DF, 0); // 1st stage
    for(i = 0; i < 5; i++)
    {
        biquad_lowpass1_coeffs[i] = coefficient_set[i];
    }
    
    show_bandwidth (band[bands].mode, LP_F_help);
//    }      
//    LP_Fpass_old = LP_F_help; 
    sgtl5000_1.dacVolume(1.0);
    delay(1);
    AudioInterrupts();

} // end filter_bandwidth

void calc_FIR_coeffs (float * coeffs, int numCoeffs, float32_t fc, float32_t Astop, int type, float dfc, float Fsamprate)
    // pointer to coefficients variable, no. of coefficients to calculate, frequency where it happens, stopband attenuation in dB, 
    // filter type, half-filter bandwidth (only for bandpass and notch) 
 {  // modified by WMXZ and DD4WH after
    // Wheatley, M. (2011): CuteSDR Technical Manual. www.metronix.com, pages 118 - 120, FIR with Kaiser-Bessel Window
    // assess required number of coefficients by
    //     numCoeffs = (Astop - 8.0) / (2.285 * TPI * normFtrans);
    // selecting high-pass, numCoeffs is forced to an even number for better frequency response

     int ii,jj;
     float32_t Beta;
     float32_t izb;
     float fcf = fc;
     int nc = numCoeffs;
     fc = fc / Fsamprate;
     dfc = dfc / Fsamprate;
     // calculate Kaiser-Bessel window shape factor beta from stop-band attenuation
     if (Astop < 20.96)
       Beta = 0.0;
     else if (Astop >= 50.0)
       Beta = 0.1102 * (Astop - 8.71);
     else
       Beta = 0.5842 * powf((Astop - 20.96), 0.4) + 0.07886 * (Astop - 20.96);

     izb = Izero (Beta);
     if(type == 0) // low pass filter
//     {  fcf = fc;
     {  fcf = fc * 2.0;
      nc =  numCoeffs;
     }
     else if(type == 1) // high-pass filter
     {  fcf = -fc;
      nc =  2*(numCoeffs/2);
     }
     else if ((type == 2) || (type==3)) // band-pass filter
     {
       fcf = dfc;
       nc =  2*(numCoeffs/2); // maybe not needed
     }
     else if (type==4)  // Hilbert transform
   {
         nc =  2*(numCoeffs/2);
       // clear coefficients
       for(ii=0; ii< 2*(nc-1); ii++) coeffs[ii]=0;
       // set real delay
       coeffs[nc]=1;

       // set imaginary Hilbert coefficients
       for(ii=1; ii< (nc+1); ii+=2)
       {
         if(2*ii==nc) continue;
       float x =(float)(2*ii - nc)/(float)nc;
       float w = Izero(Beta*sqrtf(1.0f - x*x))/izb; // Kaiser window
       coeffs[2*ii+1] = 1.0f/(PIH*(float)(ii-nc/2)) * w ;
       }
       return;
   }

     for(ii= - nc, jj=0; ii< nc; ii+=2,jj++)
     {
       float x =(float)ii/(float)nc;
       float w = Izero(Beta*sqrtf(1.0f - x*x))/izb; // Kaiser window
       coeffs[jj] = fcf * m_sinc(ii,fcf) * w;
     }

     if(type==1)
     {
       coeffs[nc/2] += 1;
     }
     else if (type==2)
     {
         for(jj=0; jj< nc+1; jj++) coeffs[jj] *= 2.0f*cosf(PIH*(2*jj-nc)*fc);
     }
     else if (type==3)
     {
         for(jj=0; jj< nc+1; jj++) coeffs[jj] *= -2.0f*cosf(PIH*(2*jj-nc)*fc);
       coeffs[nc/2] += 1;
     }

} // END calc_FIR_coeffs

float m_sinc(int m, float fc)
{  // fc is f_cut/(Fsamp/2)
  // m is between -M and M step 2
  //
  float x = m*PIH;
  if(m == 0)
    return 1.0f;
  else
    return sinf(x*fc)/(fc*x);
}
/*
void calc_FIR_lowpass_coeffs (float32_t Scale, float32_t Astop, float32_t Fpass, float32_t Fstop, float32_t Fsamprate) 
{ // Wheatley, M. (2011): CuteSDR Technical Manual. www.metronix.com, pages 118 - 120, FIR with Kaiser-Bessel Window
    int n;
    float32_t Beta;
    float32_t izb;
    // normalized F parameters
    float32_t normFpass = Fpass / Fsamprate;
    float32_t normFstop = Fstop / Fsamprate;
    float32_t normFcut = (normFstop + normFpass) / 2.0; // lowpass filter 6dB cutoff
    // calculate Kaiser-Bessel window shape factor beta from stopband attenuation
    if (Astop < 20.96)
    {
      Beta = 0.0;    
    }
    else if (Astop >= 50.0)
    {
      Beta = 0.1102 * (Astop - 8.71);
    }
    else
    {
      Beta = 0.5842 * powf((Astop - 20.96), 0.4) + 0.07886 * (Astop - 20.96);
    }
    // estimate number of taps
    m_NumTaps = (int32_t)((Astop - 8.0) / (2.285 * K_2PI * (normFstop - normFpass)) + 1.0);
    if (m_NumTaps > MAX_NUMCOEF) 
    { 
      m_NumTaps = MAX_NUMCOEF;
    }
    if (m_NumTaps < 3)
    {
      m_NumTaps = 3;
    }
    float32_t fCenter = 0.5 * (float32_t) (m_NumTaps - 1);
    izb = Izero (Beta);
    for (n = 0; n < m_NumTaps; n++)
    {
      float32_t x = (float32_t) n - fCenter;
      float32_t c;
      // create ideal Sinc() LP filter with normFcut
        if ( abs((float)n - fCenter) < 0.01)
      { // deal with odd size filter singularity where sin(0) / 0 == 1
        c = 2.0 * normFcut;
//        Serial.println("Hello, here at odd FIR filter size");
      }
      else
      {
        c = (float32_t) sinf(K_2PI * x * normFcut) / (K_PI * x);
//        c = (float32_t) sinf(K_PI * x * normFcut) / (K_PI * x);
      }
      x = ((float32_t) n - ((float32_t) m_NumTaps - 1.0) / 2.0) / (((float32_t) m_NumTaps - 1.0) / 2.0);
      FIR_Coef[n] = Scale * c * Izero (Beta * sqrt(1 - (x * x) )) / izb;
    }
} // END calc_lowpass_coeffs
*/
float32_t Izero (float32_t x) 
{
    float32_t x2 = x / 2.0;
    float32_t summe = 1.0;
    float32_t ds = 1.0;
    float32_t di = 1.0;
    float32_t errorlimit = 1e-9;
    float32_t tmp;
    do
    {
        tmp = x2 / di;
        tmp *= tmp;
        ds *= tmp;
        summe += ds;
        di += 1.0; 
    }   while (ds >= errorlimit * summe);
    return (summe);
}  // END Izero

// set samplerate code by Frank Boesing 
void setI2SFreq(int freq) {
  typedef struct {
    uint8_t mult;
    uint16_t div;
  } tmclk;

  const int numfreqs = 15;
  const int samplefreqs[numfreqs] = { 8000, 11025, 16000, 22050, 32000, 44100, (int)44117.64706 , 48000, 88200, (int)44117.64706 * 2, 96000, 100000, 176400, (int)44117.64706 * 4, 192000};

#if (F_PLL==16000000)
  const tmclk clkArr[numfreqs] = {{16, 125}, {148, 839}, {32, 125}, {145, 411}, {64, 125}, {151, 214}, {12, 17}, {96, 125}, {151, 107}, {24, 17}, {192, 125}, {1,1}, {127, 45}, {48, 17}, {255, 83} };
#elif (F_PLL==72000000)
  const tmclk clkArr[numfreqs] = {{32, 1125}, {49, 1250}, {64, 1125}, {49, 625}, {128, 1125}, {98, 625}, {8, 51}, {64, 375}, {196, 625}, {16, 51}, {128, 375}, {1,1}, {249, 397}, {32, 51}, {185, 271} };
#elif (F_PLL==96000000)
  const tmclk clkArr[numfreqs] = {{8, 375}, {73, 2483}, {16, 375}, {147, 2500}, {32, 375}, {147, 1250}, {2, 17}, {16, 125}, {147, 625}, {4, 17}, {32, 125}, {1,1}, {151, 321}, {8, 17}, {64, 125} };
#elif (F_PLL==120000000)
  const tmclk clkArr[numfreqs] = {{32, 1875}, {89, 3784}, {64, 1875}, {147, 3125}, {128, 1875}, {205, 2179}, {8, 85}, {64, 625}, {89, 473}, {16, 85}, {128, 625}, {1,1}, {178, 473}, {32, 85}, {145, 354} };
#elif (F_PLL==144000000)
  const tmclk clkArr[numfreqs] = {{16, 1125}, {49, 2500}, {32, 1125}, {49, 1250}, {64, 1125}, {49, 625}, {4, 51}, {32, 375}, {98, 625}, {8, 51}, {64, 375}, {1,1}, {196, 625}, {16, 51}, {128, 375} };
#elif (F_PLL==168000000)
  const tmclk clkArr[numfreqs] = {{32, 2625}, {21, 1250}, {64, 2625}, {21, 625}, {128, 2625}, {42, 625}, {8, 119}, {64, 875}, {84, 625}, {16, 119}, {128, 875}, {1,1}, {168, 625}, {32, 119}, {189, 646} };
#elif (F_PLL==180000000)
  const tmclk clkArr[numfreqs] = {{46, 4043}, {49, 3125}, {73, 3208}, {98, 3125}, {183, 4021}, {196, 3125}, {16, 255}, {128, 1875}, {107, 853}, {32, 255}, {219, 1604}, {224,1575}, {214, 853}, {64, 255}, {219, 802} };
#elif (F_PLL==192000000)
  const tmclk clkArr[numfreqs] = {{4, 375}, {37, 2517}, {8, 375}, {73, 2483}, {16, 375}, {147, 2500}, {1, 17}, {8, 125}, {147, 1250}, {2, 17}, {16, 125}, {1,1}, {147, 625}, {4, 17}, {32, 125} };
#elif (F_PLL==216000000)
  const tmclk clkArr[numfreqs] = {{32, 3375}, {49, 3750}, {64, 3375}, {49, 1875}, {128, 3375}, {98, 1875}, {8, 153}, {64, 1125}, {196, 1875}, {16, 153}, {128, 1125}, {1,1}, {226, 1081}, {32, 153}, {147, 646} };
#elif (F_PLL==240000000)
  const tmclk clkArr[numfreqs] = {{16, 1875}, {29, 2466}, {32, 1875}, {89, 3784}, {64, 1875}, {147, 3125}, {4, 85}, {32, 625}, {205, 2179}, {8, 85}, {64, 625}, {1,1}, {89, 473}, {16, 85}, {128, 625} };
#endif

  for (int f = 0; f < numfreqs; f++) {
    if ( freq == samplefreqs[f] ) {
      while (I2S0_MCR & I2S_MCR_DUF) ;
      I2S0_MDR = I2S_MDR_FRACT((clkArr[f].mult - 1)) | I2S_MDR_DIVIDE((clkArr[f].div - 1));
      return;
    }
  }
} // end set_I2S

void init_filter_mask() 
{
   /****************************************************************************************
 *  Calculate the FFT of the FIR filter coefficients once to produce the FIR filter mask
 ****************************************************************************************/
// the FIR has exactly m_NumTaps and a maximum of (FFT_length / 2) + 1 taps = coefficients, so we have to add (FFT_length / 2) -1 zeros before the FFT
// in order to produce a FFT_length point input buffer for the FFT 
    // copy coefficients into real values of first part of buffer, rest is zero

    for (i = 0; i < (FFT_length / 2) + 1; i++)
    {
        FIR_filter_mask[i * 2] = FIR_Coef [i];
        FIR_filter_mask[i * 2 + 1] = 0.0; 
    }
    
    for (i = FFT_length; i < FFT_length * 2; i++)
    {
        FIR_filter_mask[i] = 0.0;
    }

/*
// pass-thru
// b0=1
// all others zero

    FIR_filter_mask[0] = 1;
    for (i = 1; i < FFT_length * 2; i++)
    {
        FIR_filter_mask[i] = 0.0;
    }

*/

// FFT of FIR_filter_mask
// perform FFT (in-place), needs only to be done once (or every time the filter coeffs change)
    arm_cfft_f32(maskS, FIR_filter_mask, 0, 1);    
    
///////////////////////////////////////////////////////////////77
// PASS-THRU only for TESTING
/////////////////////////////////////////////////////////////77
/*
// hmmm, unclear, whether [1,1] or [1,0] or [0,1] are pass-thru filter-mask-multipliers??
// empirically, [1,0] sounds most natural = pass-thru
  for(i = 0; i < FFT_length * 2; i+=2)
  {
        FIR_filter_mask [i] = 1.0; // real
        FIR_filter_mask [i + 1] = 0.0; // imaginary
  }
*/
} // end init_filter_mask


void Zoom_FFT_prep()
{ // take value of spectrum_zoom and initialize IIR lowpass and FIR decimation filters for the right values

    float32_t Fstop_Zoom = 0.5 * (float32_t) SR[SAMPLE_RATE].rate / (1<<spectrum_zoom); 
//    Serial.print("Fstop =  "); Serial.println(Fstop_Zoom);
    calc_FIR_coeffs (Fir_Zoom_FFT_Decimate_coeffs, 4, Fstop_Zoom, 60, 0, 0.0, (float32_t)SR[SAMPLE_RATE].rate);

    if(spectrum_zoom < 7)
    {   
        Fir_Zoom_FFT_Decimate_I.M = (1<<spectrum_zoom);
        Fir_Zoom_FFT_Decimate_Q.M = (1<<spectrum_zoom);
        IIR_biquad_Zoom_FFT_I.pCoeffs = mag_coeffs[spectrum_zoom];
        IIR_biquad_Zoom_FFT_Q.pCoeffs = mag_coeffs[spectrum_zoom];
    }
    else
    {   // we have to decimate by 128 for all higher magnifications, arm routine does not allow for higher decimations
        Fir_Zoom_FFT_Decimate_I.M = 128;
        Fir_Zoom_FFT_Decimate_Q.M = 128;
        IIR_biquad_Zoom_FFT_I.pCoeffs = mag_coeffs[7];
        IIR_biquad_Zoom_FFT_Q.pCoeffs = mag_coeffs[7];
    }
    
    zoom_sample_ptr = 0;
}


void Zoom_FFT_exe (uint32_t blockSize)
{
  /*********************************************************************************************
   * see Lyons 2011 for a general description of the ZOOM FFT
   *********************************************************************************************/
      float32_t x_buffer[4096];
      float32_t y_buffer[4096];
//      static float32_t display_offset = 0.0;
      int sample_no = 256;
      if(spectrum_zoom >= SPECTRUM_ZOOM_32)
      {
          sample_no = 4096 / (1 << spectrum_zoom);  
      }
      
      if (spectrum_zoom != SPECTRUM_ZOOM_1)
      {
           // lowpass filter
           if(band[bands].mode == DEMOD_WFM)
           {
              arm_biquad_cascade_df1_f32 (&IIR_biquad_Zoom_FFT_I, float_buffer_R,x_buffer, blockSize);
              arm_biquad_cascade_df1_f32 (&IIR_biquad_Zoom_FFT_Q, iFFT_buffer,y_buffer, blockSize);
           }
           else
           {
              arm_biquad_cascade_df1_f32 (&IIR_biquad_Zoom_FFT_I, float_buffer_L,x_buffer, blockSize);
              arm_biquad_cascade_df1_f32 (&IIR_biquad_Zoom_FFT_Q, float_buffer_R,y_buffer, blockSize);
           }
            // decimation
            arm_fir_decimate_f32(&Fir_Zoom_FFT_Decimate_I, x_buffer, x_buffer, blockSize);
            arm_fir_decimate_f32(&Fir_Zoom_FFT_Decimate_Q, y_buffer, y_buffer, blockSize);
//            arm_fir_decimate_f32(&Fir_Zoom_FFT_Decimate_I, float_buffer_L, x_buffer, blockSize);
//            arm_fir_decimate_f32(&Fir_Zoom_FFT_Decimate_Q, float_buffer_R, y_buffer, blockSize);
      // put samples into buffer and apply windowing
      for(i = 0; i < sample_no; i++) 
      { // interleave real and imaginary input values [real, imag, real, imag . . .]
        // apply Hann window 
        // Nuttall window
          uint16_t index = zoom_sample_ptr / 2; // only for correct windowing !
          buffer_spec_FFT[zoom_sample_ptr] = (1<<spectrum_zoom) * (0.355768 - (0.487396*cosf((TPI*(float32_t)index)/(float32_t)512 - 1)) +
                   (0.144232*cosf((FOURPI*(float32_t)index)/(float32_t)512 - 1)) - (0.012604*cosf((SIXPI*(float32_t)index)/(float32_t)512 - 1))) * x_buffer[i];
          zoom_sample_ptr++;
          buffer_spec_FFT[zoom_sample_ptr] = (1<<spectrum_zoom) *  (0.355768 - (0.487396*cosf((TPI*(float32_t)index)/(float32_t)512 - 1)) +
                   (0.144232*cosf((FOURPI*(float32_t)index)/(float32_t)512 - 1)) - (0.012604*cosf((SIXPI*(float32_t)index)/(float32_t)512 - 1))) * y_buffer[i];
          zoom_sample_ptr++;

/*          // without windowing  
            buffer_spec_FFT[zoom_sample_ptr] = (1<<spectrum_zoom) * x_buffer[i];
            zoom_sample_ptr++;
            buffer_spec_FFT[zoom_sample_ptr] = (1<<spectrum_zoom) * y_buffer[i];
            zoom_sample_ptr++;
            */
       }
     }
      else
      { 
      
      // put samples into buffer and apply windowing
      for(i = 0; i < sample_no; i++) 
      { // interleave real and imaginary input values [real, imag, real, imag . . .]
        // apply Hann window 
        // Nuttall window
          buffer_spec_FFT[zoom_sample_ptr] = (0.355768 - (0.487396*cosf((TPI*(float32_t)i)/(float32_t)512 - 1)) +
                   (0.144232*cosf((FOURPI*(float32_t)i)/(float32_t)512 - 1)) - (0.012604*cosf((SIXPI*(float32_t)i)/(float32_t)512 - 1))) * float_buffer_L[i];
          zoom_sample_ptr++;
          buffer_spec_FFT[zoom_sample_ptr] = (0.355768 - (0.487396*cosf((TPI*(float32_t)i)/(float32_t)512 - 1)) +
                   (0.144232*cosf((FOURPI*(float32_t)i)/(float32_t)512 - 1)) - (0.012604*cosf((SIXPI*(float32_t)i)/(float32_t)512 - 1))) * float_buffer_R[i];
          zoom_sample_ptr++;
     }
      }
     if(zoom_sample_ptr >= 511)
     {
          zoom_display = 1;
          zoom_sample_ptr = 0;
//***************
      float32_t help = 0.0;
      // adjust lowpass filter coefficient, so that
      // "spectrum display smoothness" is the same across the different sample rates
      // and the same across different magnify modes . . .
      float32_t LPFcoeff = LPF_spectrum * (AUDIO_SAMPLE_RATE_EXACT / SR[SAMPLE_RATE].rate);
      
      float32_t onem_LPFcoeff = 1.0 - LPFcoeff;
      onem_LPFcoeff *= (float32_t)(1 << spectrum_zoom) / 4096.0;
      LPFcoeff += onem_LPFcoeff;
      if(spectrum_zoom > 10) LPFcoeff = 0.90;
      if(LPFcoeff > 1.0) LPFcoeff = 1.0;
      if(LPFcoeff < 0.001) LPFcoeff = 0.001;
//      if(spectrum_zoom >= 7) LPFcoeff = 1.0; // FIXME 
      // save old pixels for lowpass filter
      for(i = 0; i < 256; i++)
      {
          pixelold[i] = pixelnew[i];
      }
      // perform complex FFT
      // calculation is performed in-place the FFT_buffer [re, im, re, im, re, im . . .]
      arm_cfft_f32(spec_FFT, buffer_spec_FFT, 0, 1);
       // calculate mag = I*I + Q*Q, 
      // and simultaneously put them into the right order
      for(i = 0; i < 128; i++)
      {
          FFT_spec[i + 128] = sqrtf(buffer_spec_FFT[i * 2] * buffer_spec_FFT[i * 2] + buffer_spec_FFT[i * 2 + 1] * buffer_spec_FFT[i * 2 + 1]);
          FFT_spec[i] = sqrtf(buffer_spec_FFT[(i + 128) * 2] * buffer_spec_FFT[(i + 128)  * 2] + buffer_spec_FFT[(i + 128)  * 2 + 1] * buffer_spec_FFT[(i + 128)  * 2 + 1]);
      }
     // apply low pass filter and scale the magnitude values and convert to int for spectrum display
     // apply spectrum AGC
     // 
      for(int16_t x = 0; x < 256; x++)
      {
           FFT_spec[x] = LPFcoeff * FFT_spec[x] + (1.0 - LPFcoeff) * FFT_spec_old[x];
           FFT_spec_old[x] = FFT_spec[x]; 
      }
      float32_t min_spec = 10000.0;
      for(int16_t x = 0; x < 256; x++)
      {
           help = 10.0 * log10f(FFT_spec[x] + 1.0) * spectrum_display_scale; 
           help = help + display_offset;
           if(help < min_spec) min_spec = help;
           if(help < 1) help = 1.0;
              // insert display offset, AGC etc. here
           pixelnew[x] = (int16_t) (help);
      }
           display_offset -= min_spec * 0.03;
     }
}

void codec_gain()
{
      static uint32_t timer = 0;

//      sgtl5000_1.lineInLevel(bands[band].RFgain);
      timer ++;
      if (timer > 10000) timer = 10000;
      if(half_clip == 1)       // did clipping almost occur?
      {
              if(timer >= 5)      // has enough time passed since the last gain decrease?
              {
                  if(bands[band].RFgain != 0)        // yes - is this NOT zero?
                  {
                      bands[band].RFgain -= 1;    // decrease gain one step, 1.5dB 
                      if(bands[band].RFgain < 0) 
                      {
                          bands[band].RFgain = 0;
                      }
                      timer = 0;  // reset the adjustment timer
                      sgtl5000_1.lineInLevel(bands[band].RFgain);
                      if(Menu2 == MENU_RF_GAIN) show_menu();
                  }
              }
       }
       else if(quarter_clip == 0)      // no clipping occurred
       {
              if(timer >= 25)        // has it been long enough since the last increase?
              {
                  bands[band].RFgain += 1;    // increase gain by one step, 1.5dB 
                  timer = 0;  // reset the timer to prevent this from executing too often
                  if(bands[band].RFgain > 15)
                  {
                      bands[band].RFgain = 15;
                  }
                      sgtl5000_1.lineInLevel(bands[band].RFgain);
                      if(Menu2 == MENU_RF_GAIN) show_menu();
              }
       }
        half_clip = 0;      // clear "half clip" indicator that tells us that we should decrease gain
        quarter_clip = 0;   // clear indicator that, if not triggered, indicates that we can increase gain
}


void calc_256_magn() 
{
      float32_t help = 0.0;
      // adjust lowpass filter coefficient, so that
      // "spectrum display smoothness" is the same across the different sample rates
      float32_t LPFcoeff = LPF_spectrum * (AUDIO_SAMPLE_RATE_EXACT / SR[SAMPLE_RATE].rate);
      if(LPFcoeff > 1.0) LPFcoeff = 1.0;

      for(i = 0; i < 256; i++)
      {
          pixelold[i] = pixelnew[i];
      }

      // put samples into buffer and apply windowing
      for(i = 0; i < 256; i++) 
      { // interleave real and imaginary input values [real, imag, real, imag . . .]
        // apply Hann window 
        // cosf is much much faster than arm_cos_f32 !
//          buffer_spec_FFT[i * 2] = 0.5 * (float32_t)((1 - (cosf(PI*2 * (float32_t)i / (float32_t)(512-1)))) * float_buffer_L[i]);
//          buffer_spec_FFT[i * 2 + 1] = 0.5 * (float32_t)((1 - (cosf(PI*2 * (float32_t)i / (float32_t)(512-1)))) * float_buffer_R[i]);        
        // Nuttall window
          buffer_spec_FFT[i * 2] = (0.355768 - (0.487396*cosf((TPI*(float32_t)i)/(float32_t)512 - 1)) +
                   (0.144232*cosf((FOURPI*(float32_t)i)/(float32_t)512 - 1)) - (0.012604*cosf((SIXPI*(float32_t)i)/(float32_t)512 - 1))) * float_buffer_L[i];
          buffer_spec_FFT[i * 2 + 1] = (0.355768 - (0.487396*cosf((TPI*(float32_t)i)/(float32_t)512 - 1)) +
                   (0.144232*cosf((FOURPI*(float32_t)i)/(float32_t)512 - 1)) - (0.012604*cosf((SIXPI*(float32_t)i)/(float32_t)512 - 1))) * float_buffer_R[i];

      }
      
        //  Hanning 1.36
        //sc.FFT_Windat[i] = 0.5 * (float32_t)((1 - (arm_cos_f32(PI*2 * (float32_t)i / (float32_t)(FFT_IQ_BUFF_LEN2-1)))) * sc.FFT_Samples[i]);
        // Hamming 1.22
        //sc.FFT_Windat[i] = (float32_t)((0.53836 - (0.46164 * arm_cos_f32(PI*2 * (float32_t)i / (float32_t)(FFT_IQ_BUFF_LEN2-1)))) * sc.FFT_Samples[i]);
        // Blackman 1.75
        // float32_t help_sample = (0.42659 - (0.49656*arm_cos_f32((2.0*PI*(float32_t)i)/(buff_len-1.0))) + (0.076849*arm_cos_f32((4.0*PI*(float32_t)i)/(buff_len-1.0)))) * sc.FFT_Samples[i];
        // Nuttall
//             s = (0.355768 - (0.487396*arm_cos_f32((2*PI*(float32_t)i)/(float32_t)FFT_IQ_BUFF_LEN-1)) + (0.144232*arm_cos_f32((4*PI*(float32_t)i)/(float32_t)FFT_IQ_BUFF_LEN-1)) - (0.012604*arm_cos_f32((6*PI*(float32_t)i)/(float32_t)FFT_IQ_BUFF_LEN-1))) * sd.FFT_Samples[i];
     
      // perform complex FFT
      // calculation is performed in-place the FFT_buffer [re, im, re, im, re, im . . .]
      arm_cfft_f32(spec_FFT, buffer_spec_FFT, 0, 1);
      // calculate magnitudes and put into FFT_spec
      // we do not need to calculate magnitudes with square roots, it would seem to be sufficient to
      // calculate mag = I*I + Q*Q, because we are doing a log10-transformation later anyway
      // and simultaneously put them into the right order
      // 38.50%, saves 0.05% of processor power and 1kbyte RAM ;-)
      for(i = 0; i < 128; i++)
      {
          FFT_spec[i + 128] = sqrtf(buffer_spec_FFT[i * 2] * buffer_spec_FFT[i * 2] + buffer_spec_FFT[i * 2 + 1] * buffer_spec_FFT[i * 2 + 1]);
          FFT_spec[i] = sqrtf(buffer_spec_FFT[(i + 128) * 2] * buffer_spec_FFT[(i + 128)  * 2] + buffer_spec_FFT[(i + 128)  * 2 + 1] * buffer_spec_FFT[(i + 128)  * 2 + 1]);
      }

            
      // apply low pass filter and scale the magnitude values and convert to int for spectrum display
      for(int16_t x = 0; x < 256; x++)
      {
           help = LPFcoeff * FFT_spec[x] + (1.0 - LPFcoeff) * FFT_spec_old[x];
           FFT_spec_old[x] = help; 
              // insert display offset, AGC etc. here
           help = 10.0 * log10f(help + 1.0); 
           pixelnew[x] = (int16_t) (help * spectrum_display_scale);
      }
} // end calc_256_magn

/*
 // leave this here for REFERENCE !
void calc_spectrum_mags(uint32_t zoom, float32_t LPFcoeff) {
      float32_t help, help2;
      LPFcoeff = LPFcoeff * (AUDIO_SAMPLE_RATE_EXACT / SR[SAMPLE_RATE].rate);
      if(LPFcoeff > 1.0) LPFcoeff = 1.0;
      for(i = 0; i < 256; i++)
      {
          pixelold[i] = pixelnew[i];
      }
      // this saves absolutely NO processor power at 96ksps in comparison to arm_cmplx_mag
//      for(i = 0; i < FFT_length; i++)
//      {
//          FFT_magn[i] = alpha_beta_mag(FFT_buffer[(i * 2)], FFT_buffer[(i*2)+1]);
//      }
    
      // this is slower than arm_cmplx_mag_f32 (43.3% vs. 45.5% MCU usage)
//      for(i = 0; i < FFT_length; i++)
//      {
//          FFT_magn[i] = sqrtf(FFT_buffer[(i*2)] * FFT_buffer[(i*2)] + FFT_buffer[(i*2) + 1] * FFT_buffer[(i*2) + 1]);  
//      } 

      arm_cmplx_mag_f32(FFT_buffer, FFT_magn, FFT_length);  // calculates sqrt(I*I + Q*Q) for each frequency bin of the FFT
      
      ////////////////////////////////////////////////////////////////////////
      // now calculate average of the bin values according to spectrum zoom
      // this assumes FFT_shift of 1024, ie. 5515Hz with an FFT_length of 4096
      ////////////////////////////////////////////////////////////////////////
      
      // TODO: adapt to different FFT lengths
      for(i = 0; i < FFT_length / 2; i+=zoom)
      {
              arm_mean_f32(&FFT_magn[i], zoom, &FFT_spec[i / zoom + 128]);
      }
      for(i = FFT_length / 2; i < FFT_length; i+=zoom)
      {
              arm_mean_f32(&FFT_magn[i], zoom, &FFT_spec[i / zoom - 128]);
      }

      // low pass filtering of the spectrum pixels to smooth/slow down spectrum in the time domain
      // ynew = LPCoeff * x + (1-LPCoeff) * yprevious; 
      for(int16_t x = 0; x < 256; x++)
      {
           help = LPFcoeff * FFT_spec[x] + (1.0 - LPFcoeff) * FFT_spec_old[x];
           FFT_spec_old[x] = help; 
              // insert display offset, AGC etc. here
              // a single log10 here needs another 22% of processor use on a Teensy 3.6 (96k sample rate)!
//           help = 50.0 * log10(help+1.0); 
             // sqrtf is a little bit faster than arm_sqrt_f32 ! 
             help2 = sqrtf(help); 
//           arm_sqrt_f32(help, &help2);          
           pixelnew[x] = (int16_t) (help2 * 10);
      }
      
} // end calc_spectrum_mags
*/

void show_spectrum() 
{
      int16_t y_old, y_new, y1_new, y1_old;
      int16_t y1_old_minus = 0;
      int16_t y1_new_minus = 0;
      leave_WFM++;
      if(leave_WFM == 2)
      {
          // clear spectrum display
          tft.fillRect(0,spectrum_y + 4,320,240 - spectrum_y + 4,ILI9341_BLACK);
          prepare_spectrum_display();
          show_bandwidth(band[bands].mode, LP_F_help);
      }
      if(leave_WFM == 1000) leave_WFM = 1000; 

      // Draw spectrum display
      for (int16_t x = 0; x < 254; x++) 
      {
                if ((x > 1) && (x < 255)) 
                // moving window - weighted average of 5 points of the spectrum to smooth spectrum in the frequency domain
                // weights:  x: 50% , x-1/x+1: 36%, x+2/x-2: 14% 
                {    
                  y_new = pixelnew[x] * 0.5 + pixelnew[x - 1] * 0.18 + pixelnew[x + 1] * 0.18 + pixelnew[x - 2] * 0.07 + pixelnew[x + 2] * 0.07;
                  y_old = pixelold[x] * 0.5 + pixelold[x - 1] * 0.18 + pixelold[x + 1] * 0.18 + pixelold[x - 2] * 0.07 + pixelold[x + 2] * 0.07;
                }
                else 
                {
                  y_new = pixelnew[x];
                  y_old = pixelold[x];
                 }
               if(y_old > (spectrum_height - 7))
               {
                  y_old = (spectrum_height - 7);
               }

               if(y_new > (spectrum_height - 7))
               {
                  y_new = (spectrum_height - 7);
               }
               y1_old  = (spectrum_y + spectrum_height - 1) - y_old;
               y1_new  = (spectrum_y + spectrum_height - 1) - y_new; 

               if(x == 0)
               {
                   y1_old_minus = y1_old;
                   y1_new_minus = y1_new;
               }
               if(x == 254)
               {
                   y1_old_minus = y1_old;
                   y1_new_minus = y1_new;
               }

//             if(x != spectrum_pos_centre_f) 
             {
          // DELETE OLD LINE/POINT
              if(y1_old - y1_old_minus > 1) 
               { // plot line upwards
                tft.drawFastVLine(x + spectrum_x, y1_old_minus + 1, y1_old - y1_old_minus,ILI9341_BLACK);
               }
              else if (y1_old - y1_old_minus < -1) 
               { // plot line downwards
                tft.drawFastVLine(x + spectrum_x, y1_old, y1_old_minus - y1_old,ILI9341_BLACK);
               }
              else
               {
                  tft.drawPixel(x + spectrum_x, y1_old, ILI9341_BLACK); // delete old pixel
               }

          // DRAW NEW LINE/POINT
              if(y1_new - y1_new_minus > 1) 
               { // plot line upwards
                tft.drawFastVLine(x + spectrum_x, y1_new_minus + 1, y1_new - y1_new_minus,ILI9341_WHITE);
               }
              else if (y1_new - y1_new_minus < -1) 
               { // plot line downwards
                tft.drawFastVLine(x + spectrum_x, y1_new, y1_new_minus - y1_new,ILI9341_WHITE);
               }
               else
               {
                  tft.drawPixel(x + spectrum_x, y1_new, ILI9341_WHITE); // write new pixel
               }

            y1_new_minus = y1_new;
            y1_old_minus = y1_old;

        }
      } // end for loop

} // END show_spectrum


void show_bandwidth (int M, uint32_t filter_f)  
{  
//  AudioNoInterrupts();
  // M = demod_mode, FU & FL upper & lower frequency
  // this routine prints the frequency bars under the spectrum display
  // and displays the bandwidth bar indicating demodulation bandwidth
   if (spectrum_zoom != SPECTRUM_ZOOM_1) spectrum_pos_centre_f = 128;
    else spectrum_pos_centre_f = 64;
   float32_t pixel_per_khz = (1<<spectrum_zoom) * 256.0 / SR[SAMPLE_RATE].rate * 1000.0;
   float32_t leU = filter_f / 1000.0 * pixel_per_khz;
   float32_t leL = filter_f / 1000.0 * pixel_per_khz;
//   char string[4];
   char string[10];
   int pos_y = spectrum_y + spectrum_height + 2; 
   tft.drawFastHLine(spectrum_x - 1, pos_y, 258, ILI9341_BLACK); // erase old indicator
   tft.drawFastHLine(spectrum_x - 1, pos_y + 1, 258, ILI9341_BLACK); // erase old indicator 
   tft.drawFastHLine(spectrum_x - 1, pos_y + 2, 258, ILI9341_BLACK); // erase old indicator
   tft.drawFastHLine(spectrum_x - 1, pos_y + 3, 258, ILI9341_BLACK); // erase old indicator

    switch(M) {
//      case DEMOD_AM1:
      case DEMOD_AM2:
/*      case DEMOD_AM3:
      case DEMOD_AM_AE1:
      case DEMOD_AM_AE2:
      case DEMOD_AM_AE3:
      case DEMOD_AM_ME1: */
      case DEMOD_AM_ME2:
//      case DEMOD_AM_ME3:
      case DEMOD_SAM:
      case DEMOD_SAM_STEREO:
          leU = filter_f / 1000.0 * pixel_per_khz;
          leL = filter_f / 1000.0 * pixel_per_khz;
          break;
      case DEMOD_LSB:
      case DEMOD_STEREO_LSB:
      case DEMOD_SAM_LSB:
          leU = 0.0;
          leL = filter_f / 1000.0 * pixel_per_khz;
          break;
      case DEMOD_USB:
      case DEMOD_SAM_USB:
      case DEMOD_STEREO_USB:
          leU = filter_f / 1000.0 * pixel_per_khz;
          leL = 0.0;
    }
      if(leL > spectrum_pos_centre_f + 1) leL = spectrum_pos_centre_f + 2;
      if((leU + spectrum_pos_centre_f) > 255) leU = 256 - spectrum_pos_centre_f;
     
      // draw upper sideband indicator
      tft.drawFastHLine(spectrum_pos_centre_f + spectrum_x + 1, pos_y, leU, ILI9341_YELLOW);
      tft.drawFastHLine(spectrum_pos_centre_f + spectrum_x + 1, pos_y + 1, leU, ILI9341_YELLOW);
      tft.drawFastHLine(spectrum_pos_centre_f + spectrum_x + 1, pos_y + 2, leU, ILI9341_YELLOW);
      tft.drawFastHLine(spectrum_pos_centre_f + spectrum_x + 1, pos_y + 3, leU, ILI9341_YELLOW);
      // draw lower sideband indicator   
      tft.drawFastHLine(spectrum_pos_centre_f  + spectrum_x - leL + 1, pos_y, leL + 1, ILI9341_YELLOW);
      tft.drawFastHLine(spectrum_pos_centre_f  + spectrum_x - leL + 1, pos_y + 1, leL + 1, ILI9341_YELLOW);
      tft.drawFastHLine(spectrum_pos_centre_f  + spectrum_x - leL + 1, pos_y + 2, leL + 1, ILI9341_YELLOW);
      tft.drawFastHLine(spectrum_pos_centre_f  + spectrum_x - leL + 1, pos_y + 3, leL + 1, ILI9341_YELLOW);
      //print bandwidth !
          tft.fillRect(10,24,310,21,ILI9341_BLACK);   
          tft.setCursor(10, 25);
          tft.setFont(Arial_9);
          tft.setTextColor(ILI9341_WHITE);
          tft.print(DEMOD[band[bands].mode].text);
          sprintf(string,"%02.1f kHz", (float)(filter_f/1000.0));//kHz);
          tft.setCursor(70, 25);
          tft.print(string);
          tft.setCursor(140, 25);
          tft.print("   SR:  ");
          tft.print(SR[SAMPLE_RATE].text);
          show_tunestep();
          tft.setTextColor(ILI9341_WHITE); // set text color to white for other print routines not to get confused ;-)
//  AudioInterrupts();
}  // end show_bandwidth

void prepare_spectrum_display() 
{
    uint16_t base_y = spectrum_y + spectrum_height + 4;
//    uint16_t b_x = spectrum_x + SR[SAMPLE_RATE].x_offset;
//    float32_t x_f = SR[SAMPLE_RATE].x_factor;
  
    tft.fillRect(0,base_y,320,240 - base_y,ILI9341_BLACK);
    tft.drawRect(spectrum_x - 2, spectrum_y + 2, 257, spectrum_height, ILI9341_MAROON); 
    // receive freq indicator line
//    tft.drawFastVLine(spectrum_x + spectrum_pos_centre_f, spectrum_y + spectrum_height - 18, 20, ILI9341_GREEN); 
/*
    // vertical lines
    tft.drawFastVLine(b_x - 4, base_y + 1, 10, ILI9341_YELLOW);  
    tft.drawFastVLine(b_x - 3, base_y + 1, 10, ILI9341_YELLOW);  
    tft.drawFastVLine( x_f * 2.0 + b_x,  base_y + 1, 10, ILI9341_YELLOW);  
    tft.drawFastVLine( x_f * 2.0 + 1.0 + b_x,  base_y + 1, 10, ILI9341_YELLOW);  
    if(x_f * 3.0 + b_x < 256+b_x) {
    tft.drawFastVLine( x_f * 3.0 + b_x,  base_y + 1, 10, ILI9341_YELLOW);  
    tft.drawFastVLine( x_f * 3.0 + 1.0 + b_x,  base_y + 1, 10, ILI9341_YELLOW);  
    }
    if(x_f * 4.0 + b_x < 256+b_x) {
    tft.drawFastVLine( x_f * 4.0 + b_x,  base_y + 1, 10, ILI9341_YELLOW);  
    tft.drawFastVLine( x_f * 4.0 + 1.0 + b_x,  base_y + 1, 10, ILI9341_YELLOW);  
    }
    if(x_f * 5.0 + b_x < 256+b_x) {
    tft.drawFastVLine( x_f * 5.0 + b_x,  base_y + 1, 10, ILI9341_YELLOW);  
    tft.drawFastVLine( x_f * 5.0 + 1.0 + b_x,  base_y + 1, 10, ILI9341_YELLOW);  
    }
    tft.drawFastVLine( x_f * 0.5 + b_x,  base_y + 1, 6, ILI9341_YELLOW);  
    tft.drawFastVLine( x_f * 1.5 + b_x,  base_y + 1, 6, ILI9341_YELLOW);  
    tft.drawFastVLine( x_f * 2.5 + b_x,  base_y + 1, 6, ILI9341_YELLOW);  
    if(x_f * 3.5 + b_x < 256+b_x) {
    tft.drawFastVLine( x_f * 3.5 + b_x,  base_y + 1, 6, ILI9341_YELLOW);  
    }
    if(x_f * 4.5 + b_x < 256+b_x) {
        tft.drawFastVLine( x_f * 4.5 + b_x,  base_y + 1, 6, ILI9341_YELLOW);  
    }
    // text
    tft.setTextColor(ILI9341_WHITE);
    tft.setFont(Arial_8);
    int text_y_offset = 15;
    int text_x_offset = - 5;
    // zero
    tft.setCursor (b_x + text_x_offset - 1, base_y + text_y_offset);
    tft.print(SR[SAMPLE_RATE].f1);
    tft.setCursor (b_x + x_f * 2 + text_x_offset, base_y + text_y_offset);
    tft.print(SR[SAMPLE_RATE].f2);
    tft.setCursor (b_x + x_f *3 + text_x_offset, base_y + text_y_offset);
    tft.print(SR[SAMPLE_RATE].f3);
    tft.setCursor (b_x + x_f *4 + text_x_offset, base_y + text_y_offset);
    tft.print(SR[SAMPLE_RATE].f4);
//    tft.setCursor (b_x + text_x_offset + 256, base_y + text_y_offset);
    tft.print(" kHz");
    tft.setFont(Arial_10);
*/
// draw S-Meter layout
  tft.drawFastHLine (pos_x_smeter, pos_y_smeter-1, 9*s_w, ILI9341_WHITE);
  tft.drawFastHLine (pos_x_smeter, pos_y_smeter+6, 9*s_w, ILI9341_WHITE);
  tft.fillRect(pos_x_smeter, pos_y_smeter-3, 2, 2, ILI9341_WHITE);
  tft.fillRect(pos_x_smeter+8*s_w, pos_y_smeter-3, 2, 2, ILI9341_WHITE);
  tft.fillRect(pos_x_smeter+2*s_w, pos_y_smeter-3, 2, 2, ILI9341_WHITE);
  tft.fillRect(pos_x_smeter+4*s_w, pos_y_smeter-3, 2, 2, ILI9341_WHITE);
  tft.fillRect(pos_x_smeter+6*s_w, pos_y_smeter-3, 2, 2, ILI9341_WHITE);
  tft.fillRect(pos_x_smeter+7*s_w, pos_y_smeter-4, 2, 3, ILI9341_WHITE);
  tft.fillRect(pos_x_smeter+3*s_w, pos_y_smeter-4, 2, 3, ILI9341_WHITE);
  tft.fillRect(pos_x_smeter+5*s_w, pos_y_smeter-4, 2, 3, ILI9341_WHITE);
  tft.fillRect(pos_x_smeter+s_w, pos_y_smeter-4, 2, 3, ILI9341_WHITE);
  tft.fillRect(pos_x_smeter+9*s_w, pos_y_smeter-4, 2, 3, ILI9341_WHITE);
  tft.drawFastHLine (pos_x_smeter+9*s_w, pos_y_smeter-1, 3*s_w*2+2, ILI9341_GREEN);

  tft.drawFastHLine (pos_x_smeter+9*s_w, pos_y_smeter+6, 3*s_w*2+2, ILI9341_GREEN);
  tft.fillRect(pos_x_smeter+11*s_w, pos_y_smeter-4, 2, 3, ILI9341_GREEN);
  tft.fillRect(pos_x_smeter+13*s_w, pos_y_smeter-4, 2, 3, ILI9341_GREEN);
  tft.fillRect(pos_x_smeter+15*s_w, pos_y_smeter-4, 2, 3, ILI9341_GREEN);

  tft.drawFastVLine (pos_x_smeter-1, pos_y_smeter-1, 8, ILI9341_WHITE); 
  tft.drawFastVLine (pos_x_smeter+15*s_w+2, pos_y_smeter-1, 8, ILI9341_GREEN);

  tft.setCursor(pos_x_smeter - 1, pos_y_smeter - 15);
  tft.setTextColor(ILI9341_WHITE);
  tft.setFont(Arial_8);
  tft.print("S 1");
  tft.setCursor(pos_x_smeter + 28, pos_y_smeter - 15);
  tft.print("3");
  tft.setCursor(pos_x_smeter + 48, pos_y_smeter - 15);
  tft.print("5");
  tft.setCursor(pos_x_smeter + 68, pos_y_smeter - 15);
  tft.print("7");
  tft.setCursor(pos_x_smeter + 88, pos_y_smeter - 15);
  tft.print("9");
  tft.setCursor(pos_x_smeter + 120, pos_y_smeter - 15);
  tft.print("+20dB");
    FrequencyBarText();
    show_menu();
} // END prepare_spectrum_display

void FrequencyBarText()
{    // This function draws the frequency bar at the bottom of the spectrum scope, putting markers at every graticule and the full frequency
    // (rounded to the nearest kHz) in the "center".  (by KA7OEI, 20140913) modified from the mcHF source code
    float   freq_calc;
    ulong   i;
    char    txt[16], *c;
    float   grat;
    int centerIdx;
    const int pos_grat_y = 20;
    grat = (float)(SR[SAMPLE_RATE].rate / 8000.0) / (float)(1 << spectrum_zoom); // 1, 2, 4, 8, 16, 32, 64 . . . 4096

/*    if(spectrum_zoom == SPECTRUM_SUPER_ZOOM)
    {
        grat = (float)((SR[SAMPLE_RATE].rate / 8000.0) / 4096); // 
    } */

    tft.setTextColor(ILI9341_WHITE);
    tft.setFont(Arial_8);
    // clear print area for frequency text
//    tft.fillRect(0, spectrum_y + spectrum_height + pos_grat_y, 320, 8, ILI9341_BLACK);
    tft.fillRect(0,spectrum_y + spectrum_height + 5,320,240 - spectrum_y - spectrum_height - 5,ILI9341_BLACK);

    freq_calc = (float)(bands[band].freq / SI5351_FREQ_MULT);      // get current frequency in Hz
    if(band[bands].mode == DEMOD_WFM)
    { // undersampling mode with 5x undersampling
        grat *= 5.0;
        freq_calc = 5.0 * freq_calc + 1.25 * SR[SAMPLE_RATE].rate;;
    }

    if(spectrum_zoom == 0)         // 
    {
        freq_calc += (float32_t)SR[SAMPLE_RATE].rate / 4.0;
    }

    if(spectrum_zoom < 3)
    {
        freq_calc = roundf(freq_calc/1000); // round graticule frequency to the nearest kHz
    }
    else if(spectrum_zoom < 5)
    {
        freq_calc = roundf(freq_calc/100) / 10; // round graticule frequency to the nearest 100Hz
    }
    else if(spectrum_zoom == 5) // 32x
    {
        freq_calc = roundf(freq_calc/50) / 20; // round graticule frequency to the nearest 50Hz
    }
    else if(spectrum_zoom < 8) // 
    {
        freq_calc = roundf(freq_calc/10) / 100 ; // round graticule frequency to the nearest 10Hz
    }
    else  
    {
        freq_calc = roundf(freq_calc) / 1000; // round graticule frequency to the nearest 1Hz
    }

    if(spectrum_zoom != 0)     centerIdx = 0;
      else centerIdx = -2;
    
    {
        // remainder of frequency/graticule markings
//        const static int idx2pos[2][9] = {{0,26,58,90,122,154,186,218, 242},{0,26,58,90,122,154,186,209, 229} };
        // positions for graticules: first for spectrum_zoom < 3, then for spectrum_zoom > 2
        const static int idx2pos[2][9] = {{-10,21,52,83,123,151,186,218, 252},{-10,21,50,86,123,154,178,218, 252} };
//        const static int centerIdx2pos[] = {62,94,130,160,192};
        const static int centerIdx2pos[] = {62,94,145,160,192};

/**************************************************************************************************
 * CENTER FREQUENCY PRINT 
 **************************************************************************************************/ 
        if(spectrum_zoom < 3)
        {
            snprintf(txt,16, "  %lu  ", (ulong)(freq_calc+(centerIdx*grat))); // build string for center frequency precision 1khz
        }
        else
        {
            float disp_freq = freq_calc + (centerIdx*grat);
            int bignum = (int)disp_freq;
            if(spectrum_zoom < 8)
            {
                int smallnum = (int)roundf((disp_freq-bignum)*100);
                snprintf(txt,16, "  %u.%02u  ", bignum,smallnum); // build string for center frequency precision 100Hz/10Hz/1Hz
            }
            else 
            {
                int smallnum = (int)roundf((disp_freq-bignum)*1000);
                snprintf(txt,16, "  %u.%03u  ", bignum,smallnum); // build string for center frequency precision 100Hz/10Hz/1Hz
            }
        }

        i = centerIdx2pos[centerIdx+2] -((strlen(txt)- 4) * 4);    // calculate position of center frequency text

        tft.setCursor(spectrum_x + i, spectrum_y + spectrum_height + pos_grat_y);
        tft.print(txt);

/**************************************************************************************************
 * PRINT ALL OTHER FREQUENCIES (NON-CENTER)
 **************************************************************************************************/ 

        for (int idx = -4; idx < 5; idx++)
//        for (int idx = -3; idx < 4; idx++)
        {
            int pos_help = idx2pos[spectrum_zoom < 3? 0 : 1][idx+4];
            if (idx != centerIdx)
            {
                if(spectrum_zoom < 3)
                {
                    snprintf(txt,16, " %lu ", (ulong)(freq_calc+(idx*grat)));   // build string for middle-left frequency (1khz precision)
                    c = &txt[strlen(txt)-3];  // point at 2nd character from the end
                }
                else
                {
                    float disp_freq = freq_calc+(idx*grat);
                    int bignum = (int)disp_freq;
                    if(spectrum_zoom < 8)
                    {
                        int smallnum = (int)roundf((disp_freq-bignum)*100);
                        snprintf(txt,16, "  %u.%02u  ", bignum,smallnum); // build string for center frequency precision 100Hz/10Hz/1Hz
                    }
                    else 
                    {
                        int smallnum = (int)roundf((disp_freq-bignum)*1000);
                        snprintf(txt,16, "  %u.%03u  ", bignum,smallnum); // build string for center frequency precision 100Hz/10Hz/1Hz
                    }
                    c = &txt[strlen(txt)-5];  // point at 5th character from the end
                }

            tft.setCursor(spectrum_x + pos_help, spectrum_y + spectrum_height + pos_grat_y);
            tft.print(txt);
            // insert draw vertical bar HERE:


            
            }
            if(spectrum_zoom > 2 || freq_calc > 1000)
            {
                idx++;
            }
        }
    }
    tft.setFont(Arial_10);

//**************************************************************************
    uint16_t base_y = spectrum_y + spectrum_height + 4;

//    float32_t pixel_per_khz = (1<<spectrum_zoom) * 256.0 / SR[SAMPLE_RATE].rate * 1000.0;

    // center line
    if(spectrum_zoom == 0)
    {
            tft.drawFastVLine(spectrum_x + 62, base_y + 1, 10, ILI9341_RED);  
            tft.drawFastVLine(spectrum_x + 65, base_y + 1, 10, ILI9341_RED);  
            tft.drawFastVLine(spectrum_x + 63, base_y + 1, 10, ILI9341_RED);  
            tft.drawFastVLine(spectrum_x + 64, base_y + 1, 10, ILI9341_RED);  
            tft.drawFastVLine(spectrum_x + 127, base_y + 1, 10, ILI9341_YELLOW);  
    }
    else
    {
            tft.drawFastVLine(spectrum_x + 126, base_y + 1, 10, ILI9341_RED);  
            tft.drawFastVLine(spectrum_x + 129, base_y + 1, 10, ILI9341_RED);  
            tft.drawFastVLine(spectrum_x + 127, base_y + 1, 10, ILI9341_RED);  
            tft.drawFastVLine(spectrum_x + 128, base_y + 1, 10, ILI9341_RED);  
            tft.drawFastVLine(spectrum_x + 63, base_y + 1, 10, ILI9341_YELLOW);  
    }
    // vertical lines
    tft.drawFastVLine(spectrum_x, base_y + 1, 10, ILI9341_YELLOW);  
    tft.drawFastVLine(spectrum_x - 1, base_y + 1, 10, ILI9341_YELLOW);  
    tft.drawFastVLine(spectrum_x + 255, base_y + 1, 10, ILI9341_YELLOW);  
    tft.drawFastVLine(spectrum_x + 256, base_y + 1, 10, ILI9341_YELLOW);  
    tft.drawFastVLine(spectrum_x + 191, base_y + 1, 10, ILI9341_YELLOW);  

    if(spectrum_zoom < 3 && freq_calc <= 1000)
    {
        tft.drawFastVLine(spectrum_x + 31, base_y + 1, 10, ILI9341_YELLOW);  
        tft.drawFastVLine(spectrum_x + 95, base_y + 1, 10, ILI9341_YELLOW);  
        tft.drawFastVLine(spectrum_x + 159, base_y + 1, 10, ILI9341_YELLOW);  
        tft.drawFastVLine(spectrum_x + 223, base_y + 1, 10, ILI9341_YELLOW);  
    }
}

float32_t alpha_beta_mag(float32_t  inphase, float32_t  quadrature) 
// (c) András Retzler
// taken from libcsdr: https://github.com/simonyiszk/csdr
{
  // Min RMS Err      0.947543636291 0.392485425092
  // Min Peak Err     0.960433870103 0.397824734759
  // Min RMS w/ Avg=0 0.948059448969 0.392699081699
  const float32_t alpha = 0.960433870103; // 1.0; //0.947543636291;
  const float32_t beta =  0.397824734759;
   /* magnitude ~= alpha * max(|I|, |Q|) + beta * min(|I|, |Q|) */
   float32_t abs_inphase = fabs(inphase);
   float32_t abs_quadrature = fabs(quadrature);
   if (abs_inphase > abs_quadrature) {
      return alpha * abs_inphase + beta * abs_quadrature;
   } else {
      return alpha * abs_quadrature + beta * abs_inphase;
   }
}
/*
void amdemod_estimator_cf(complexf* input, float *output, int input_size, float alpha, float beta)
{ //  (c) András Retzler
  //  taken from libcsdr: https://github.com/simonyiszk/csdr
  //concept is explained here:
  //http://www.dspguru.com/dsp/tricks/magnitude-estimator

  //default: optimize for min RMS error
  if(alpha==0)
  {
    alpha=0.947543636291;
    beta=0.392485425092;
  }

  //@amdemod_estimator
  for (int i=0; i<input_size; i++)
  {
    float abs_i=iof(input,i);
    if(abs_i<0) abs_i=-abs_i;
    float abs_q=qof(input,i);
    if(abs_q<0) abs_q=-abs_q;
    float max_iq=abs_i;
    if(abs_q>max_iq) max_iq=abs_q;
    float min_iq=abs_i;
    if(abs_q<min_iq) min_iq=abs_q;

    output[i]=alpha*max_iq+beta*min_iq;
  }
}

*/

float32_t fastdcblock_ff(float32_t* input, float32_t* output, int input_size, float32_t last_dc_level)
{ //  (c) András Retzler
  //  taken from libcsdr: https://github.com/simonyiszk/csdr
  //this DC block filter does moving average block-by-block.
  //this is the most computationally efficient
  //input and output buffer is allowed to be the same
  //http://www.digitalsignallabs.com/dcblock.pdf
  float32_t avg=0.0;
  for(int i=0;i<input_size;i++) //@fastdcblock_ff: calculate block average
  {
    avg+=input[i];
  }
  avg/=input_size;

  float32_t avgdiff=avg-last_dc_level;
  //DC removal level will change lineraly from last_dc_level to avg.
  for(int i=0;i<input_size;i++) //@fastdcblock_ff: remove DC component
  {
    float32_t dc_removal_level=last_dc_level+avgdiff*((float32_t)i/input_size);
    output[i]=input[i]-dc_removal_level;
  }
  return avg;
}

void set_IIR_coeffs (float32_t f0, float32_t Q, float32_t sample_rate, uint8_t filter_type)
{      
 //     set_IIR_coeffs ((float32_t)2400.0, 1.3, (float32_t)SR[SAMPLE_RATE].rate, 0);
     /*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
     * Cascaded biquad (notch, peak, lowShelf, highShelf) [DD4WH, april 2016]
     ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
    // DSP Audio-EQ-cookbook for generating the coeffs of the filters on the fly
    // www.musicdsp.org/files/Audio-EQ-Cookbook.txt  [by Robert Bristow-Johnson]
    //
    // the ARM algorithm assumes the biquad form
    // y[n] = b0 * x[n] + b1 * x[n-1] + b2 * x[n-2] + a1 * y[n-1] + a2 * y[n-2]
    //
    // However, the cookbook formulae by Robert Bristow-Johnson AND the Iowa Hills IIR Filter designer
    // use this formula:
    //
    // y[n] = b0 * x[n] + b1 * x[n-1] + b2 * x[n-2] - a1 * y[n-1] - a2 * y[n-2]
    //
    // Therefore, we have to use negated a1 and a2 for use with the ARM function
    if(f0 > sample_rate / 2.0) f0 = sample_rate / 2.0;
    float32_t w0 = f0 * (TPI / sample_rate);
    float32_t sinW0 = sinf(w0);
    float32_t alpha = sinW0 / (Q * 2.0);
    float32_t cosW0 = cosf(w0);
    float32_t scale = 1.0 / (1.0 + alpha);

    if(filter_type == 0)
    { // lowpass coeffs
    
    /* b0 */ coefficient_set[0] = ((1.0 - cosW0) / 2.0) * scale;
    /* b1 */ coefficient_set[1] = (1.0 - cosW0) * scale;
    /* b2 */ coefficient_set[2] = coefficient_set[0];
    /* a1 */ coefficient_set[3] = (2.0 * cosW0) * scale; // negated
/* a2 */     coefficient_set[4] = (-1.0 + alpha) * scale; // negated
    }
    else 
    if (filter_type == 2)
    {
//BPF:        H(s) = (s/Q) / (s^2 + s/Q + 1)      (constant 0 dB peak gain)

    /* b0 */ coefficient_set[0] =  alpha * scale;
    /* b1 */ coefficient_set[1] =  0.0;
    /* b2 */ coefficient_set[2] =  - alpha * scale;
       //     a0 =   1 + alpha
    /* a1 */ coefficient_set[3] =  2.0 * cosW0 * scale; // negated
    /* a2 */ coefficient_set[4] =  alpha - 1.0; // negated
    }
   
}

int ExtractDigit(long int n, int k) {
        switch (k) {
              case 0: return n%10;
              case 1: return n/10%10;
              case 2: return n/100%10;
              case 3: return n/1000%10;
              case 4: return n/10000%10;
              case 5: return n/100000%10;
              case 6: return n/1000000%10;
              case 7: return n/10000000%10;
              case 8: return n/100000000%10;              
              case 9: return n/1000000000%10;
              default: return 0;
        }
}



// show frequency
void show_frequency(unsigned long long freq, uint8_t text_size) { 
    // text_size 0 --> small display
    // text_size 1 --> large main display
    int color = ILI9341_WHITE;
    int8_t font_width = 8;
    int8_t sch_help = 0;
    freq = freq / SI5351_FREQ_MULT;
    if(bands[band].mode == DEMOD_WFM) 
    {
        freq = freq * 5 + 1.25 * SR[SAMPLE_RATE].rate; // undersampling of f/5 and correction, because no IF is used in WFM mode
        erase_flag = 1;
    }
    if(text_size == 0) // small SAM carrier display 
    {
        if(freq_flag[0] == 0)
        { // print text first time we´re here
            tft.setCursor(pos_x_frequency + 10, pos_y_frequency + 26);
            tft.setFont(Arial_8);
            tft.setTextColor(ILI9341_ORANGE);
            tft.print("SAM carrier ");
        }
        sch_help = 9;
        freq += SAM_carrier_freq_offset;
        tft.setFont(Arial_10);
        pos_x_frequency = pos_x_frequency + 68;
        pos_y_frequency = pos_y_frequency + 24;
        color = ILI9341_GREEN;
    }
    else // large main frequency display
    {
        sch_help = 9;
        tft.setFont(Arial_18);
        font_width = 16;
    }
    tft.setTextColor(color);
    uint8_t zaehler;
    uint8_t digits[10];
    zaehler = 9; //8;

          while (zaehler--) {
              digits[zaehler] = ExtractDigit (freq, zaehler);
//              Serial.print(digits[zaehler]);
//              Serial.print(".");
// 7: 10Mhz, 6: 1Mhz, 5: 100khz, 4: 10khz, 3: 1khz, 2: 100Hz, 1: 10Hz, 0: 1Hz
        }
            //  Serial.print("xxxxxxxxxxxxx");

    zaehler = 9; //8;
        while (zaehler--) { // counts from 8 to 0
              if (zaehler < 6) sch = sch_help; // (khz)
              if (zaehler < 3) sch = sch_help * 2; //18; // (Hz)
          if (digits[zaehler] != digits_old[text_size][zaehler] || !freq_flag[text_size]) { // digit has changed (or frequency is displayed for the first time after power on)
              if (zaehler == 8) {
                     sch = 0;
                     tft.setCursor(pos_x_frequency + font_width * (8-zaehler) + sch,pos_y_frequency); // set print position
                     tft.fillRect(pos_x_frequency + font_width * (8-zaehler) + sch,pos_y_frequency, font_width,18,ILI9341_BLACK); // delete old digit
                     if (digits[8] != 0) tft.print(digits[zaehler]); // write new digit in white
              }
              if (zaehler == 7) {
                     sch = 0;
                     tft.setCursor(pos_x_frequency + font_width * (8-zaehler) + sch,pos_y_frequency); // set print position
                     tft.fillRect(pos_x_frequency + font_width * (8-zaehler) + sch,pos_y_frequency, font_width,18,ILI9341_BLACK); // delete old digit
                     if (digits[7] != 0 || digits[8] != 0) tft.print(digits[zaehler]); // write new digit in white
              }
              if (zaehler == 6) {
                            sch = 0;
                            tft.setCursor(pos_x_frequency + font_width * (8-zaehler) + sch,pos_y_frequency); // set print position
                            tft.fillRect(pos_x_frequency + font_width * (8-zaehler) + sch,pos_y_frequency, font_width,18,ILI9341_BLACK); // delete old digit
                            if (digits[6]!=0 || digits[7] != 0 || digits[8] != 0) tft.print(digits[zaehler]); // write new digit in white
              }
               if (zaehler == 5) {
                            sch = 9;
                            tft.setCursor(pos_x_frequency + font_width * (8-zaehler) + sch,pos_y_frequency); // set print position
                            tft.fillRect(pos_x_frequency + font_width * (8-zaehler) + sch,pos_y_frequency, font_width,18,ILI9341_BLACK); // delete old digit
                            if (digits[5] != 0 || digits[6]!=0 || digits[7] != 0 || digits[8] != 0) tft.print(digits[zaehler]); // write new digit in white
              }
              
              if (zaehler < 5) { 
              // print the digit
              tft.setCursor(pos_x_frequency + font_width * (8-zaehler) + sch,pos_y_frequency); // set print position
              tft.fillRect(pos_x_frequency + font_width * (8-zaehler) + sch,pos_y_frequency, font_width,18,ILI9341_BLACK); // delete old digit
              tft.print(digits[zaehler]); // write new digit in white
              }
              digits_old[text_size][zaehler] = digits[zaehler]; 
        }
        }

      // reset to previous values!
      if(text_size == 0)
      {
          if (digits[7] == 0 && digits[6] == 0 && digits[8] == 0)
              tft.fillRect(pos_x_frequency + font_width * 3 + 2,pos_y_frequency + 8, 2, 2, ILI9341_BLACK);
          else    tft.fillRect(pos_x_frequency + font_width * 3 + 2,pos_y_frequency + 8, 2, 2, ILI9341_YELLOW);
          tft.fillRect(pos_x_frequency + font_width * 8 - 4, pos_y_frequency + 8, 2, 2, ILI9341_YELLOW);
          pos_y_frequency -= 24;
          pos_x_frequency -= 68;      
      }
      else
      {
      tft.setFont(Arial_10);
      if (digits[7] == 0 && digits[6] == 0 && digits[8] == 0)
              tft.fillRect(pos_x_frequency + font_width * 3 + 2 ,pos_y_frequency + 15, 3, 3, ILI9341_BLACK);
      else    tft.fillRect(pos_x_frequency + font_width * 3 + 2,pos_y_frequency + 15, 3, 3, ILI9341_YELLOW);
      tft.fillRect(pos_x_frequency + font_width * 7 - 6, pos_y_frequency + 15, 3, 3, ILI9341_YELLOW);
      if (!freq_flag[text_size]) {
            tft.setCursor(pos_x_frequency + font_width * 9 + 21,pos_y_frequency + 7); // set print position
            tft.setTextColor(ILI9341_GREEN);
            tft.print("Hz");
      }

      }
      freq_flag[text_size] = 1;
//      Serial.print("SAM carrier frequency = "); Serial.println((int)freq);

} // END VOID SHOW-FREQUENCY

void setfreq () {
// NEVER USE AUDIONOINTERRUPTS HERE: that introduces annoying clicking noise with every frequency change
//   hilfsf = (bands[band].freq +  IF_FREQ) * 10000000 * MASTER_CLK_MULT * SI5351_FREQ_MULT;
   hilfsf = (bands[band].freq +  IF_FREQ * SI5351_FREQ_MULT) * 1000000000 * MASTER_CLK_MULT; // SI5351_FREQ_MULT is 100ULL;
   hilfsf = hilfsf / calibration_factor;
   si5351.set_freq(hilfsf, Si_5351_clock); 
   if(band[bands].mode == DEMOD_AUTOTUNE)
   {
        autotune_flag = 1;
   }
   FrequencyBarText();

// LPF switching follows here
// Five filter banks there:
// longwave LPF 295kHz, mediumwave I LPF 955kHz, mediumwave II LPF 2MHz, tropical bands LPF 5.4MHz, others LPF LPF 30MHz
// LW: Band5
// MW: Band3 (up to 955kHz)
// MW: Band1 (up tp 1996kHz)
// SW: Band2 (up to 5400kHz)
// SW: Band4 (up up up)
//
// LOWPASS 955KHZ
     if (((bands[band].freq + IF_FREQ * SI5351_FREQ_MULT) < 955001 * SI5351_FREQ_MULT) && ((bands[band].freq + IF_FREQ * SI5351_FREQ_MULT) > 300001 * SI5351_FREQ_MULT)) { 
     digitalWrite (Band3, HIGH); Serial.println ("Band3");
     digitalWrite (Band1, LOW); digitalWrite (Band2, LOW); digitalWrite (Band4, LOW); digitalWrite (Band5, LOW);
     } // end if
  
// LOWPASS 2MHZ
    if (((bands[band].freq + IF_FREQ * SI5351_FREQ_MULT) > 955000 * SI5351_FREQ_MULT) && ((bands[band].freq + IF_FREQ * SI5351_FREQ_MULT) < 1996001 * SI5351_FREQ_MULT)) {
     digitalWrite (Band1, HIGH);Serial.println ("Band1");
     digitalWrite (Band5, LOW); digitalWrite (Band3, LOW); digitalWrite (Band4, LOW); digitalWrite (Band2, LOW);
     } // end if

//LOWPASS 5.4MHZ
    if (((bands[band].freq + IF_FREQ * SI5351_FREQ_MULT) > 1996000 * SI5351_FREQ_MULT) && ((bands[band].freq + IF_FREQ * SI5351_FREQ_MULT) < 5400001 * SI5351_FREQ_MULT)) {
     digitalWrite (Band2, HIGH);Serial.println ("Band2");
     digitalWrite (Band4, LOW); digitalWrite (Band3, LOW); digitalWrite (Band1, LOW); digitalWrite (Band5, LOW);
     } // end if
    
// LOWPASS 30MHZ --> OK
   if ((bands[band].freq + IF_FREQ * SI5351_FREQ_MULT) > 5400000 * SI5351_FREQ_MULT) { 
    // && ((bands[band].freq + IF_FREQ) < 12500001)) {
     digitalWrite (Band4, HIGH);Serial.println ("Band4");
     digitalWrite (Band1, LOW); digitalWrite (Band3, LOW); digitalWrite (Band2, LOW); digitalWrite (Band5, LOW);
     } // end if
      // I took out the 12.5MHz lowpass and inserted the 30MHz instead - I have to live with 3rd harmonic images in the range 5.4 - 12Mhz now
      // maybe this is more important than the 5.4 - 2Mhz filter ?? Maybe swap them sometime, because I only got five filter relays . . .

// this is the brandnew longwave LPF (cutoff ca. 295kHz) --> OK
   if ((bands[band].freq - IF_FREQ * SI5351_FREQ_MULT) < 300000 * SI5351_FREQ_MULT) {
     digitalWrite (Band5, HIGH);Serial.println ("Band5");
     digitalWrite (Band2, LOW); digitalWrite (Band3, LOW); digitalWrite (Band4, LOW); digitalWrite (Band1, LOW);
     } // end if
}   

void buttons() {
      button1.update(); // BAND --
      button2.update(); // BAND ++
      button3.update(); // change band[bands].mode
      button4.update(); // MENU --
      button5.update(); // tune encoder button
      button6.update(); // filter encoder button
      button7.update(); // MENU ++
      button8.update(); // menu2 button
      eeprom_saved = 0;
      eeprom_loaded = 0;

            if ( button1.fallingEdge()) {
              if(Menu_pointer == MENU_PLAYER)
              {
                 prevtrack();
              }
              else
              { 
                if(--band < FIRST_BAND) band=LAST_BAND; // cycle thru radio bands 
                // set frequency_print flag to 0
                AudioNoInterrupts();
                sgtl5000_1.dacVolume(0.0);
                setup_mode(band[bands].mode);
                freq_flag[1] = 0;
                set_band();
                set_tunestep();
                show_menu();
                prepare_spectrum_display();
                leave_WFM = 0;
/*                sgtl5000_1.disable();
                delay(20);
                sgtl5000_1.enable();
//                sgtl5000_1.setADCStereo();
                sgtl5000_1.volume((float32_t)audio_volume / 100);
                delay(20);
*/
                delay(1);
                sgtl5000_1.dacVolume(1.0);
                AudioInterrupts();
              }       
            }
            if ( button2.fallingEdge()) { 
              if(Menu_pointer == MENU_PLAYER)
              {
                 pausetrack();
              }
              else  
              {
                if(++band > LAST_BAND) band=FIRST_BAND; // cycle thru radio bands
                // set frequency_print flag to 0
                AudioNoInterrupts();
                sgtl5000_1.dacVolume(0.0);
                setup_mode(band[bands].mode);
                freq_flag[1] = 0;
                set_band(); 
                set_tunestep();
                show_menu();
                prepare_spectrum_display();
                leave_WFM = 0;
                sgtl5000_1.dacVolume(1.0);
                delay(1);
                AudioInterrupts();
              }
            }
            if ( button3.fallingEdge()) {  // cycle through DEMOD modes
              if(Menu_pointer == MENU_PLAYER)
              {
                 nexttrack();
              }
              else
              {
                if(++band[bands].mode > DEMOD_MAX) band[bands].mode = DEMOD_MIN; // cycle thru demod modes
                AudioNoInterrupts();
                sgtl5000_1.dacVolume(0.0);
                setup_mode(band[bands].mode);
                show_frequency(bands[band].freq, 1);
                leave_WFM = 0;
                prepare_spectrum_display();
                if(twinpeaks_tested == 3 && twinpeaks_counter >= 200) write_analog_gain = 1;
                show_analog_gain(); 
                delay(10);
                AudioInterrupts();
                sgtl5000_1.dacVolume(1.0);
             }
            }
            if ( button4.fallingEdge()) { 
               // toggle thru menu
               if(which_menu == 1)
               {
                    if(--Menu_pointer < first_menu) Menu_pointer = last_menu;
               }
               else 
               {
                    if(--Menu2 < first_menu2) Menu2 = last_menu2;
               }
               if(Menu_pointer == MENU_PLAYER)
               {
                  Menu2 = MENU_VOLUME;
                  setI2SFreq (AUDIO_SAMPLE_RATE_EXACT);
                  delay(200); // essential ?
//                  audio_flag = 0;
                  Q_in_L.end();
                  Q_in_R.end();
                  Q_in_L.clear();
                  Q_in_R.clear();
                  mixleft.gain(0,0.0);
                  mixright.gain(0,0.0);
                  mixleft.gain(1,0.1);
                  mixright.gain(1,0.1);
                  mixleft.gain(2,0.1);
                  mixright.gain(2,0.1);
               }
               if(Menu_pointer == (MENU_PLAYER - 1) || (Menu_pointer == last_menu && MENU_PLAYER == first_menu))
               {
                  // stop all playing
                  playMp3.stop();
                  playAac.stop();
                  delay(200);
                  setI2SFreq (SR[SAMPLE_RATE].rate);
                  delay(200); // essential ?
                  mixleft.gain(0,1.0);
                  mixright.gain(0,1.0);
                  mixleft.gain(1,0.0);
                  mixright.gain(1,0.0);
                  mixleft.gain(2,0.0);
                  mixright.gain(2,0.0);
                  prepare_spectrum_display();
                  Q_in_L.clear();
                  Q_in_R.clear();
                  Q_in_L.begin();
                  Q_in_R.begin();
               }
               show_menu();

            }
            if ( button5.fallingEdge()) { // cycle thru tune steps
                if(++tune_stepper > TUNE_STEP_MAX) tune_stepper = TUNE_STEP_MIN; 
                set_tunestep();
            }
            if (button6.fallingEdge()) { 
                if(Menu_pointer == MENU_SAVE_EEPROM)
                {
                    EEPROM_SAVE();
                    eeprom_saved = 1;
                    show_menu();
                }
                else if(Menu_pointer == MENU_LOAD_EEPROM)
                {
                    EEPROM_LOAD();
                    eeprom_loaded = 1;
                    show_menu();
                }
                else if(Menu_pointer == MENU_IQ_AUTO)
                {
                    if(auto_IQ_correction == 0)
                        auto_IQ_correction = 1;
                        else auto_IQ_correction = 0;
                    show_menu();
                }
                else if (Menu_pointer == MENU_RESET_CODEC)
                {
                    reset_codec();
                } // END RESET_CODEC
                else if(Menu_pointer == MENU_SHOW_SPECTRUM)
                {
                    if(show_spectrum_flag == 0)
                        show_spectrum_flag = 1;
                        else show_spectrum_flag = 0;
                    show_menu();
                }
                else autotune_flag = 1;
                Serial.println("Flag gesetzt!");
                
            }            
            if (button7.fallingEdge()) { 
               // toggle thru menu
               if(which_menu == 1)
               {
                    if(++Menu_pointer > last_menu) Menu_pointer = first_menu;
               }
               else 
               {
                    if(++Menu2 > last_menu2) Menu2 = first_menu2;
               }
               Serial.println("MENU BUTTON pressed");
               if(Menu_pointer == MENU_PLAYER)
               {
                  Menu2 = MENU_VOLUME;
                  setI2SFreq (AUDIO_SAMPLE_RATE_EXACT);
                  delay(200); // essential ?
//                  audio_flag = 0;
                  Q_in_L.end();
                  Q_in_R.end();
                  Q_in_L.clear();
                  Q_in_R.clear();
                  mixleft.gain(0,0.0);
                  mixright.gain(0,0.0);
                  mixleft.gain(1,0.1);
                  mixright.gain(1,0.1);
                  mixleft.gain(2,0.1);
                  mixright.gain(2,0.1);
               }
               if(Menu_pointer == (MENU_PLAYER + 1) || (Menu_pointer == 0 && MENU_PLAYER == last_menu))
               {
                  // stop all playing
                  playMp3.stop();
                  playAac.stop();
                  delay(200);
                  setI2SFreq (SR[SAMPLE_RATE].rate);
                  delay(200); // essential ?
                  mixleft.gain(0,1.0);
                  mixright.gain(0,1.0);
                  mixleft.gain(1,0.0);
                  mixright.gain(1,0.0);
                  mixleft.gain(2,0.0);
                  mixright.gain(2,0.0);
                  prepare_spectrum_display();
                  Q_in_L.clear();
                  Q_in_R.clear();
                  Q_in_L.begin();
                  Q_in_R.begin();
               }
               show_menu();
            }            
            if (button8.fallingEdge()) { 
               // toggle thru menu2
               if(++Menu2 > last_menu2) Menu2 = first_menu2;
               which_menu = 2;
               Serial.println("MENU2 BUTTON pressed");
               show_menu();
            }            
}

void show_menu()
{
    // two blue boxes show the setting for each encoder
    // menu  = filter    --> encoder under display
    // menu2 = encoder3  --> left encoder
    // define txt-string for display
    // position constant: spectrum_y
    // Menus[Menu_pointer].no
    // upper text menu
    char string[8];
    int color1 = ILI9341_WHITE;
    int color2 = ILI9341_WHITE;
    if(which_menu == 1) color1 = ILI9341_DARKGREY;
      else color2 = ILI9341_DARKGREY;
    spectrum_y +=2;
    tft.fillRect(spectrum_x + 256 + 2, spectrum_y, 320 - spectrum_x - 258, 31, ILI9341_NAVY);
    tft.drawRect(spectrum_x + 256 + 2, spectrum_y, 320 - spectrum_x - 258, 31, ILI9341_MAROON);
    tft.setCursor(spectrum_x + 256 + 5, spectrum_y + 4);
    tft.setTextColor(color2);
    tft.setFont(Arial_10);
    tft.print(Menus[Menu_pointer].text1);
    // lower text menu
    tft.setCursor(spectrum_x + 256 + 5, spectrum_y + 16);
    tft.print(Menus[Menu_pointer].text2);
    // upper text menu2
    tft.setTextColor(color1);
    tft.fillRect(spectrum_x + 256 + 2, spectrum_y + 30, 320 - spectrum_x - 258, 31, ILI9341_NAVY);
    tft.drawRect(spectrum_x + 256 + 2, spectrum_y + 30, 320 - spectrum_x - 258, 31, ILI9341_MAROON);
    tft.setCursor(spectrum_x + 256 + 5, spectrum_y + 30 + 4);
    tft.print(Menus[Menu2].text1);
    // lower text menu2
    tft.setCursor(spectrum_x + 256 + 5, spectrum_y + 30 + 16);
    tft.print(Menus[Menu2].text2);
    tft.setTextColor(ILI9341_WHITE);
    // third box: shows the value of the parameter being changed
    tft.fillRect(spectrum_x + 256 + 2, spectrum_y + 60, 320 - spectrum_x - 258, 31, ILI9341_NAVY);
    tft.drawRect(spectrum_x + 256 + 2, spectrum_y + 60, 320 - spectrum_x - 258, 31, ILI9341_MAROON);
    // print value
    tft.setFont(Arial_14);
    tft.setCursor(spectrum_x + 256 + 12, spectrum_y + 31 + 31 + 7);
    if(which_menu == 1) // print value of Menu parameter
    {
        switch(Menu_pointer)
        {
            case MENU_SPECTRUM_ZOOM:
                tft.setFont(Arial_11);
                tft.setCursor(spectrum_x + 256 + 8, spectrum_y + 31 + 31 + 7);
                tft.print(1 << spectrum_zoom);
            break;
            case MENU_IQ_AMPLITUDE:
                tft.setFont(Arial_11);
                tft.setCursor(spectrum_x + 256 + 8, spectrum_y + 31 + 31 + 7);
                sprintf(string,"%01.3f", IQ_amplitude_correction_factor);
                tft.print(string);
            break;
            case MENU_IQ_PHASE:
                tft.setFont(Arial_11);
                tft.setCursor(spectrum_x + 256 + 8, spectrum_y + 31 + 31 + 7);
                sprintf(string,"%01.3f", IQ_phase_correction_factor);
                tft.print(string);
//                tft.print(IQ_phase_correction_factor);
            break;
            case MENU_CALIBRATION_FACTOR:
                tft.print(1 << spectrum_zoom);
            break;
            case MENU_CALIBRATION_CONSTANT:
                tft.print(1 << spectrum_zoom);
            break;
            case MENU_LPF_SPECTRUM:
                tft.setFont(Arial_11);
                tft.setCursor(spectrum_x + 256 + 8, spectrum_y + 31 + 31 + 7);
                sprintf(string,"%01.5f", LPF_spectrum);
                tft.print(string);
            break;
            case MENU_SAVE_EEPROM:
                if(eeprom_saved)
                {
                tft.setFont(Arial_11);
                tft.setCursor(spectrum_x + 256 + 8, spectrum_y + 31 + 31 + 7);
                tft.print("saved!");
                }
                else
                {
                tft.setFont(Arial_11);
                tft.setCursor(spectrum_x + 256 + 8, spectrum_y + 31 + 31 + 7);
                tft.print("press!");
                }
            break;
            case MENU_LOAD_EEPROM:
                if(eeprom_loaded)
                {
                tft.setFont(Arial_11);
                tft.setCursor(spectrum_x + 256 + 8, spectrum_y + 31 + 31 + 7);
                tft.print("loaded!");
                }
                else
                {
                tft.setFont(Arial_11);
                tft.setCursor(spectrum_x + 256 + 8, spectrum_y + 31 + 31 + 7);
                tft.print("press!");
                }
            break;
            case MENU_IQ_AUTO:
                if(auto_IQ_correction)
                {
                    tft.print("ON ");
                }
                else
                {
                    tft.print("OFF ");
                }
            break;
            case MENU_RESET_CODEC:
                    tft.setFont(Arial_11);
                    tft.print("DO IT");
            break;
            case MENU_SPECTRUM_BRIGHTNESS:
                tft.setFont(Arial_11);
                sprintf(string,"%3d", spectrum_brightness);
                tft.print(string);
            break;
            case MENU_SHOW_SPECTRUM:
                if(show_spectrum_flag)
                {
                    tft.print("YES");
                }
                else
                {
                    tft.print("NO");
                }
            break;
            case MENU_TIME_SET:
            break;
            case MENU_DATE_SET:
            break;            
        }
    }
    else
    { // print value of Menu2 parameter
        switch(Menu2)
        {
            case MENU_RF_GAIN:
                tft.setFont(Arial_11);
                tft.setCursor(spectrum_x + 256 + 6, spectrum_y + 31 + 31 + 7);
                sprintf(string,"%02.1fdB", (float)(bands[band].RFgain * 1.5));
                tft.print(string);
            break;
            case MENU_RF_ATTENUATION:
                tft.setFont(Arial_11);
                sprintf(string,"%2ddB", RF_attenuation);
                tft.print(string);
            break;
            case MENU_VOLUME:
                tft.print(audio_volume);
            break;
            case MENU_SAM_ZETA:
                tft.print(zeta);
            break;
            case MENU_TREBLE:
                sprintf(string,"%2.0f", treble * 100.0);
                tft.print(string);
            break;
            case MENU_MIDTREBLE:
                sprintf(string,"%2.0f", midtreble * 100.0);
                tft.print(string);
            break;
            case MENU_BASS:
                sprintf(string,"%2.0f", bass * 100.0);
                tft.print(string);
            break;
            case MENU_MIDBASS:
                sprintf(string,"%2.0f", midbass * 100.0);
                tft.print(string);
            break;
            case MENU_MID:
                sprintf(string,"%2.0f", mid * 100.0);
                tft.print(string);
            break;
            case MENU_SPECTRUM_DISPLAY_SCALE:
                tft.setFont(Arial_11);
                tft.setCursor(spectrum_x + 256 + 6, spectrum_y + 31 + 31 + 7);
                sprintf(string,"%4.0f", spectrum_display_scale);
                tft.print(string);
            break;            
            case MENU_SAM_OMEGA:
//                tft.setFont(Arial_11);
//                tft.setCursor(spectrum_x + 256 + 6, spectrum_y + 31 + 31 + 7);
                sprintf(string,"%3.0f", omegaN);
                tft.print(string);
            break;
            case MENU_SAM_CATCH_BW:
                tft.setFont(Arial_12);
                tft.setCursor(spectrum_x + 256 + 8, spectrum_y + 31 + 31 + 7);
                sprintf(string,"%4.0f", pll_fmax);
                tft.print(string);
            break;
            case MENU_AGC_MODE:
                tft.setFont(Arial_10);
                tft.setCursor(spectrum_x + 256 + 8, spectrum_y + 31 + 31 + 7);
                switch(AGC_mode)
                {
                  case 0:
                    tft.print("OFF");
                  break;
                  case 1:
                    tft.print("LON+");
                  break;
                  case 2:
                    tft.print("LONG");
                  break;
                  case 3:
                    tft.print("SLOW");
                  break;
                  case 4:
                    tft.print("MED");
                  break;
                  case 5:
                    tft.print("FAST");
                  break;
                }
            break; 
            case MENU_AGC_THRESH:
                sprintf(string,"%3d", agc_thresh);
                tft.print(string);
            break;
            case MENU_AGC_DECAY:
                sprintf(string,"%3d", agc_decay / 10);
                tft.print(string);
            break;
            case MENU_AGC_SLOPE:
                sprintf(string,"%3d", agc_slope);
                tft.print(string);
            break;
            case MENU_STEREO_FACTOR:
                tft.setFont(Arial_10);
                tft.setCursor(spectrum_x + 256 + 8, spectrum_y + 31 + 31 + 7);
                sprintf(string,"%5.0f", stereo_factor);
                tft.print(string);
            break;
   
        }
    }
    
    spectrum_y -= 2;
}


void set_tunestep()
{
                if(tune_stepper == 0) 
                if(band == BAND_MW || band == BAND_LW) tunestep = 9000; else tunestep = 5000;
                else if (tune_stepper == 1) tunestep = 100;
                else if (tune_stepper == 2) tunestep = 1000;
                else if (tune_stepper == 3) tunestep = 1;
                else tunestep = 5000;
                show_tunestep(); 

}

void autotune() {
    // Lyons (2011): chapter 13.15 page 702
    // this uses the FFT_buffer DIRECTLY after the 1024 point FFT
    // and calculates the magnitudes 
    // after that, for finetuning, a quadratic interpolation is performed
    // 1.) determine bins that are inside the filterbandwidth,
    //     depending on filter bandwidth AND band[bands].mode
    // 2.) calculate magnitudes from the real & imaginary values in the FFT buffer
    //     and bring them in the right order and put them into
    //     iFFT_buffer [that is recycled for this function and filled with other values afterwards]
    // 3.) perform carrier frequency estimation
    // 4.) tune to the estimated carrier frequency with an accuracy of 0.01Hz ;-) 
    // --> in reality, we achieve about 0.2Hz accuracy, not bad
    
    const float32_t buff_len = FFT_length * 2.0;
    const float32_t bin_BW = (float32_t) (SR[SAMPLE_RATE].rate * 2.0 / DF / (buff_len));
//    const int buff_len_int = FFT_length * 2;
    float32_t bw_LSB = 0.0;
    float32_t bw_USB = 0.0;
//    float32_t help_freq = (float32_t)(bands[band].freq +  IF_FREQ * SI5351_FREQ_MULT);

    //  determine posbin (where we receive at the moment) 
    // FFT_lengh is 1024 
    // FFT_buffer is already frequency-translated !
    // so we do not need to worry about that IF stuff
    const int posbin = FFT_length / 2; //
    bw_LSB = (float32_t)bands[band].bandwidthL;
    bw_USB = (float32_t)bands[band].bandwidthU;
    // include 500Hz of the other sideband into the search bandwidth
    if(bw_LSB < 1.0) bw_LSB = 500.0;
    if(bw_USB < 1.0) bw_USB = 500.0;
    
//    Serial.print("bw_LSB = "); Serial.println(bw_LSB);
//    Serial.print("bw_USB = "); Serial.println(bw_USB);
    
    // calculate upper and lower limit for determination of maximum magnitude
     const float32_t Lbin = (float32_t)posbin - round(bw_LSB / bin_BW);
     const float32_t Ubin = (float32_t)posbin + round(bw_USB / bin_BW); // the bin on the upper sideband side

      // put into second half of iFFT_buffer
      arm_cmplx_mag_f32(FFT_buffer, &iFFT_buffer[FFT_length], FFT_length);  // calculates sqrt(I*I + Q*Q) for each frequency bin of the FFT
      
      ////////////////////////////////////////////////////////////////////////
      // now bring into right order and copy in first half of iFFT_buffer
      ////////////////////////////////////////////////////////////////////////
      
      for(i = 0; i < FFT_length / 2; i++)
      {
          iFFT_buffer[i] = iFFT_buffer[i + FFT_length + FFT_length / 2];
      }
      for(i = FFT_length / 2; i < FFT_length; i++)
      {
          iFFT_buffer[i] = iFFT_buffer[i + FFT_length / 2];
      }

    //####################################################################
    if (autotune_flag == 1)
    {
        // look for maximum value and save the bin # for frequency delta calculation
        float32_t maximum = 0.0;
        float32_t maxbin = 1.0;
        float32_t delta = 0.0;

        for (int c = (int)Lbin; c <= (int)Ubin; c++)   // search for FFT bin with highest value = carrier and save the no. of the bin in maxbin
        {
            if (maximum < iFFT_buffer[c])
            {
                maximum = iFFT_buffer[c];
                maxbin = c;
            }
        }

        // ok, we have found the maximum, now save first delta frequency
        delta = (maxbin - (float32_t)posbin) * bin_BW;
//        Serial.print("maxbin = ");
//        Serial.println(maxbin);
//        Serial.print("posbin = ");
//        Serial.println(posbin);
        
        bands[band].freq = bands[band].freq  + (long long)(delta * SI5351_FREQ_MULT);
        setfreq();
        show_frequency(bands[band].freq, 1);
//        Serial.print("delta = ");
//        Serial.println(delta);
        autotune_flag = 2;
    }
    else
    {
        // ######################################################

        // and now: fine-tuning:
        //  get amplitude values of the three bins around the carrier

        float32_t bin1 = iFFT_buffer[posbin-1];
        float32_t bin2 = iFFT_buffer[posbin];
        float32_t bin3 = iFFT_buffer[posbin+1];

        if (bin1+bin2+bin3 == 0.0) bin1= 0.00000001; // prevent divide by 0

        // estimate frequency of carrier by three-point-interpolation of bins around maxbin
        // formula by (Jacobsen & Kootsookos 2007) equation (4) P=1.36 for Hanning window FFT function
        // but we have unwindowed data here !
        // float32_t delta = (bin_BW * (1.75 * (bin3 - bin1)) / (bin1 + bin2 + bin3));
        // maybe this is the right equation for unwindowed magnitude data ?
        // performance is not too bad ;-)
        float32_t delta = (bin_BW * ((bin3 - bin1)) / (2 * bin2 - bin1 - bin3));
        if(delta > bin_BW) delta = 0.0; // just in case something went wrong

        bands[band].freq = bands[band].freq  + (long long)(delta * SI5351_FREQ_MULT);
        setfreq();
        show_frequency(bands[band].freq, 1);

        if(band[bands].mode == DEMOD_AUTOTUNE)
        {
          autotune_flag = 0;
        }
        else
        {
        // empirically derived: it seems good to perform the whole tuning some 5 to 10 times
        // in order to be perfect on the carrier frequency 
        if(autotune_flag < 6)
        {
            autotune_flag++;
        } 
        else 
        {
            autotune_flag = 0;
            AudioNoInterrupts();
            Q_in_L.clear();
            Q_in_R.clear();
            AudioInterrupts();
        }
        }
//        Serial.print("DELTA 2 = ");
//        Serial.println(delta);
    }  

} // end function autotune


void show_tunestep() {
          tft.fillRect(240, 25, 80, 21, ILI9341_BLACK);
          tft.setCursor(240, 25);
          tft.setFont(Arial_9);
          tft.setTextColor(ILI9341_GREEN);
          tft.print("Step: ");
          tft.print(tunestep);
}

void set_band () {
//         show_band(bands[band].name); // show new band
         setup_mode(bands[band].mode);
         sgtl5000_1.lineInLevel(bands[band].RFgain, bands[band].RFgain);
//         setup_RX(bands[band].mode, bands[band].bandwidthU, bands[band].bandwidthL);  // set up the audio chain for new mode
         setfreq();
         show_frequency(bands[band].freq, 1);
         filter_bandwidth();
}

/* #########################################################################
 *  
 *  void setup_mode
 *  
 *  set up radio for RX modes - USB, LSB
 * ######################################################################### 
 */

void setup_mode(int MO) {
/*  switch (MO)  {

    case DEMOD_LSB:
    case DEMOD_STEREO_LSB:
    case DEMOD_SAM_LSB:
        LP_F_help = bands[band].bandwidthL;
//        if (bands[band].bandwidthU != 0) bands[band].bandwidthL = bands[band].bandwidthU;
//        bands[band].bandwidthU = 0;
    break;
    case DEMOD_USB:
    case DEMOD_STEREO_USB:
    case DEMOD_SAM_USB:
//        if (bands[band].bandwidthL != 0) bands[band].bandwidthU = bands[band].bandwidthL;
//        bands[band].bandwidthL = 0;
        LP_F_help = bands[band].bandwidthU;
    break;
//    case DEMOD_AM1:
    case DEMOD_AM2:
/*    case DEMOD_AM3:
    case DEMOD_AM_AE1:
    case DEMOD_AM_AE2:
    case DEMOD_AM_AE3:
    case DEMOD_AM_ME1: */
/*    case DEMOD_AM_ME2:
//    case DEMOD_AM_ME3:
    case DEMOD_SAM:
    case DEMOD_SAM_STEREO:
    case DEMOD_AUTOTUNE:
        LP_F_help = bands[band].bandwidthU;
        if(band[bands].mode == DEMOD_AUTOTUNE) autotune_wait = 40;
    break;
/*    case modeDSB:
        if (bands[band].bandwidthU >= bands[band].bandwidthL) 
            bands[band].bandwidthL = bands[band].bandwidthU;
            else bands[band].bandwidthU = bands[band].bandwidthL;
    break;
    case modeStereoAM:
        if (bands[band].bandwidthU >= bands[band].bandwidthL) 
            bands[band].bandwidthL = bands[band].bandwidthU;
            else bands[band].bandwidthU = bands[band].bandwidthL;
    break;
    
  }*/
    LP_F_help = bands[band].bandwidthL;
    if(LP_F_help == 0) LP_F_help = bands[band].bandwidthU;
    show_bandwidth(band[bands].mode, LP_F_help);
    if(band[bands].mode == DEMOD_WFM)
    {
          tft.fillRect(spectrum_x + 256 + 2, pos_y_time + 20, 320 - spectrum_x - 258, 31, ILI9341_BLACK);
    }
    Q_in_L.clear();
    Q_in_R.clear();
    tft.fillRect(pos_x_frequency + 10, pos_y_frequency + 24, 210, 16, ILI9341_BLACK);
    freq_flag[0] = 0; 
} // end void setup_mode


void encoders () {
 static long encoder_pos=0, last_encoder_pos=0;
  long encoder_change;
  static long encoder2_pos=0, last_encoder2_pos=0;
  long encoder2_change;
  static long encoder3_pos=0, last_encoder3_pos=0;
  long encoder3_change;
  unsigned long long old_freq; 
  
  // tune radio and adjust settings in menus using encoder switch  
  encoder_pos = tune.read();
  encoder2_pos = filter.read();
  encoder3_pos = encoder3.read();
   
  if (encoder_pos != last_encoder_pos){
      encoder_change=(encoder_pos-last_encoder_pos);
      last_encoder_pos=encoder_pos;
 
    if (((band == BAND_LW) || (band == BAND_MW)) && (tune_stepper == 5000))
    { 
        tunestep = 9000;
        show_tunestep();
    }
    if (((band != BAND_LW) && (band != BAND_MW)) && (tunestep == 9000))
    {
        tunestep = 5000;
        show_tunestep();
    }
    if(tunestep == 1)
    {
      if (encoder_change <= 4 && encoder_change > 0) encoder_change = 4;
      else if(encoder_change >= -4 && encoder_change < 0) encoder_change = - 4;
    } 
    long long tune_help1 = tunestep  * SI5351_FREQ_MULT * roundf((float32_t)encoder_change / 4.0);
//    long long tune_help1 = tunestep  * SI5351_FREQ_MULT * encoder_change;
    old_freq = bands[band].freq;
    bands[band].freq += (long long)tune_help1;  // tune the master vfo 
    if (bands[band].freq > F_MAX) bands[band].freq = F_MAX;
    if (bands[band].freq < F_MIN) bands[band].freq = F_MIN;
    if(bands[band].freq != old_freq) 
    {
        Q_in_L.clear();
        Q_in_R.clear();
        setfreq();
        show_frequency(bands[band].freq, 1);
        return;
    }
    show_menu();
  }
////////////////////////////////////////////////
  if (encoder2_pos != last_encoder2_pos)
  {
      encoder2_change=(encoder2_pos-last_encoder2_pos);
      last_encoder2_pos=encoder2_pos;
      which_menu = 1;
    if(Menu_pointer == MENU_FILTER_BANDWIDTH)
    {
/////////////////////////////////////7        
/*{
        LP_F_help = LP_F_help + encoder2_change * 25.0;
        float32_t sam = (SR[SAMPLE_RATE].rate / (DF * 2.0));
        if(LP_F_help > (uint32_t)(sam)) LP_F_help = (uint32_t)(sam);
        else
        {
            if(LP_F_help < 100) LP_F_help = 100;
            else 
            {
                filter_bandwidth();
                setfreq();
                show_frequency(bands[band].freq);
            }
        }
}*/        
/////////////////////////////////////7        
        LP_F_help = LP_F_help + encoder2_change * 25.0;
        float32_t sam = (SR[SAMPLE_RATE].rate / (DF * 2.0)) - 100.0;
        if(LP_F_help > (uint32_t)(sam)) LP_F_help = (uint32_t)(sam);
        if(LP_F_help < 100) LP_F_help = 100;
            {
                filter_bandwidth();
                setfreq();
                show_frequency(bands[band].freq, 1);
                // store bandwidth info in array
                switch (bands[band].mode)
                {
                  case DEMOD_LSB:
                  case DEMOD_STEREO_LSB:
                  case DEMOD_SAM_LSB:
                      bands[band].bandwidthU = 0;
                      bands[band].bandwidthL = LP_F_help;
                  break;
                  case DEMOD_USB:
                  case DEMOD_STEREO_USB:
                  case DEMOD_SAM_USB:
                      bands[band].bandwidthL = 0;
                      bands[band].bandwidthU = LP_F_help;
                  break;
                  default:
                      bands[band].bandwidthL = LP_F_help;
                      bands[band].bandwidthU = LP_F_help;
                  break;    
                }           
            }
        
    }
    else
    if (Menu_pointer == MENU_SPECTRUM_ZOOM) 
    {
//       if(encoder2_change < 0) spectrum_zoom--;
//            else spectrum_zoom++;
        spectrum_zoom += (int8_t)((float)encoder2_change / 4.0);
        Serial.println(encoder2_change);
        Serial.println((int8_t)((float)encoder2_change / 4.0));
        if(spectrum_zoom > SPECTRUM_ZOOM_MAX) spectrum_zoom = SPECTRUM_ZOOM_MAX;

        if(spectrum_zoom < SPECTRUM_ZOOM_MIN) spectrum_zoom = SPECTRUM_ZOOM_MIN;
        Zoom_FFT_prep();
//        Serial.print("ZOOM factor:  "); Serial.println(1<<spectrum_zoom);
        show_bandwidth (band[bands].mode, LP_F_help);
        FrequencyBarText();
    } // END Spectrum_zoom

    else
    if (Menu_pointer == MENU_IQ_AMPLITUDE) 
    {
//          K_dirty += (float32_t)encoder2_change / 1000.0;
//          Serial.print("IQ Ampl corr factor:  "); Serial.println(K_dirty * 1000);
//          Serial.print("encoder_change:  "); Serial.println(encoder2_change);
          
          IQ_amplitude_correction_factor += encoder2_change / 4000.0;
//          Serial.print("IQ Ampl corr factor:  "); Serial.println(IQ_amplitude_correction_factor * 1000000);
//          Serial.print("encoder_change:  "); Serial.println(encoder2_change);
    } // END IQadjust
    else 
    if (Menu_pointer == MENU_SPECTRUM_BRIGHTNESS) 
    {
          spectrum_brightness += encoder2_change / 4 * 10;
          if(spectrum_brightness > 256) spectrum_brightness = 256;
          if(spectrum_brightness < 20) spectrum_brightness = 20;
          analogWrite(BACKLIGHT_PIN, spectrum_brightness);
    } // 
    else 
    if (Menu_pointer == MENU_SAMPLE_RATE) 
    {
//          if(encoder2_change < 0) SAMPLE_RATE--;
//            else SAMPLE_RATE++;
          SAMPLE_RATE += (long)((float)encoder2_change / 4.0);  
            wait_flag = 1;    
          AudioNoInterrupts();
          if(SAMPLE_RATE > SAMPLE_RATE_MAX) SAMPLE_RATE = SAMPLE_RATE_MAX;
          if(SAMPLE_RATE < SAMPLE_RATE_MIN) SAMPLE_RATE = SAMPLE_RATE_MIN;
          setI2SFreq (SR[SAMPLE_RATE].rate);
          delay(500);
          IF_FREQ = SR[SAMPLE_RATE].rate / 4;
          // this sets the frequency, but without knowing the IF!
          setfreq();
          prepare_spectrum_display(); // show new frequency scale
//          LP_Fpass_old = 0; // cheat the filter_bandwidth function ;-)
          // before calculating the filter, we have to assure, that the filter bandwidth is not larger than
          // sample rate / 19.0
// TODO: change bands[band].bandwidthU and L !!!
          
          if(LP_F_help > SR[SAMPLE_RATE].rate / 19.0) LP_F_help = SR[SAMPLE_RATE].rate / 19.0;
          filter_bandwidth(); // calculate new FIR & IIR coefficients according to the new sample rate
          show_bandwidth(band[bands].mode, LP_F_help);
          set_SAM_PLL();
/****************************************************************************************
*  Recalculate decimation and interpolation FIR filters
****************************************************************************************/
          // Decimation filter 1, M1 = 4
          calc_FIR_coeffs (FIR_dec1_coeffs, 50, (float32_t)SR[SAMPLE_RATE].rate / 19.0, 80, 0, 0.0, SR[SAMPLE_RATE].rate);
          // Decimation filter 2, M2 = 2
          calc_FIR_coeffs (FIR_dec2_coeffs, 88, (float32_t)SR[SAMPLE_RATE].rate / 19.0, 80, 0, 0.0, SR[SAMPLE_RATE].rate / 4);
          // Interpolation filter 1, L1 = 2
          calc_FIR_coeffs (FIR_int1_coeffs, 16, (float32_t)SR[SAMPLE_RATE].rate / 19.0, 80, 0, 0.0, SR[SAMPLE_RATE].rate / 4);
          // Interpolation filter 2, L2 = 4
          calc_FIR_coeffs (FIR_int2_coeffs, 16, (float32_t)SR[SAMPLE_RATE].rate / 19.0, 80, 0, 0.0, SR[SAMPLE_RATE].rate);
          AudioInterrupts();
     } 
    else
    if (Menu_pointer == MENU_LPF_SPECTRUM) 
    {
          LPF_spectrum += encoder2_change / 400.0;
          if(LPF_spectrum < 0.00001) LPF_spectrum = 0.00001;
          if(LPF_spectrum > 1.0) LPF_spectrum = 1.0;
    } // END LPFSPECTRUM
    else 
    if (Menu_pointer == MENU_IQ_PHASE) 
    {
//          P_dirty += (float32_t)encoder2_change / 1000.0;
//          Serial.print("IQ Phase corr factor:  "); Serial.println(P_dirty * 1000);
//          Serial.print("encoder_change:  "); Serial.println(encoder2_change);
          IQ_phase_correction_factor = IQ_phase_correction_factor + (float32_t)encoder2_change / 4000.0;
//          Serial.print("IQ Phase corr factor"); Serial.println(IQ_phase_correction_factor * 1000000);

    } // END IQadjust
    else if (Menu_pointer == MENU_TIME_SET) {
        helpmin = minute(); helphour = hour();
        helpmin = helpmin + encoder2_change / 4;
        if (helpmin > 59) { 
          helpmin = 0; helphour = helphour +1;}
         if (helpmin < 0) {
          helpmin = 59; helphour = helphour -1; }
         if (helphour < 0) helphour = 23; 
        if (helphour > 23) helphour = 0;
        helpmonth = month(); helpyear = year(); helpday = day();
        setTime (helphour, helpmin, 0, helpday, helpmonth, helpyear);      
        Teensy3Clock.set(now()); // set the RTC  
      } // end TIMEADJUST
    else 
    if (Menu_pointer == MENU_DATE_SET) {
      helpyear = year(); 
      helpmonth = month();
      helpday = day();
      helpday = helpday + encoder2_change / 4;
      if (helpday < 1) {helpday=31; helpmonth=helpmonth-1;}
      if (helpday > 31) {helpmonth = helpmonth +1; helpday=1;}
      if (helpmonth < 1) {helpmonth = 12; helpyear = helpyear-1;}
      if (helpmonth > 12) {helpmonth = 1; helpyear = helpyear+1;}
      helphour=hour(); helpmin=minute(); helpsec=second(); 
      setTime (helphour, helpmin, helpsec, helpday, helpmonth, helpyear);      
      Teensy3Clock.set(now()); // set the RTC
      displayDate();
          } // end DATEADJUST

    
        show_menu();
//        tune.write(0);
  } // end encoder2 was turned

    if (encoder3_pos != last_encoder3_pos)
    {
      encoder3_change=(encoder3_pos-last_encoder3_pos);
      last_encoder3_pos=encoder3_pos;
      which_menu = 2;
    if(Menu2 == MENU_RF_GAIN)
    {
      if(auto_codec_gain == 1)
      {
          auto_codec_gain = 0;
          Menus[MENU_RF_GAIN].text2 = "  gain  ";
          Serial.println ("auto = 0");
      }
      bands[band].RFgain = bands[band].RFgain + encoder3_change / 4;
      if(bands[band].RFgain < 0)
      {
            auto_codec_gain = 1; Serial.println ("auto = 1");
            Menus[MENU_RF_GAIN].text2 = " AUTO  ";
            bands[band].RFgain = 0;
      }
      if(bands[band].RFgain > 15) 
      {
          bands[band].RFgain = 15;
      }
      sgtl5000_1.lineInLevel(bands[band].RFgain);
    }
    else if(Menu2 == MENU_VOLUME)
    {
      audio_volume = audio_volume + encoder3_change;
      if(audio_volume < 0) audio_volume = 0;
      else if(audio_volume > 100) audio_volume = 100;
//      AudioNoInterrupts();
      sgtl5000_1.volume((float32_t)audio_volume / 100.0);
      
    }
    else if(Menu2 == MENU_RF_ATTENUATION)
    {
      RF_attenuation = RF_attenuation + encoder3_change / 4;
      if(RF_attenuation < 0) RF_attenuation = 0;
      else if(RF_attenuation > 31) RF_attenuation = 31;
      setAttenuator(RF_attenuation);
    }
    else if(Menu2 == MENU_SAM_ZETA)
    {
      zeta_help = zeta_help + (float32_t)encoder3_change / 4.0;
      if(zeta_help < 15) zeta_help = 15;
      else if(zeta_help > 99) zeta_help = 99;
      set_SAM_PLL();
    }
    else if(Menu2 == MENU_SAM_OMEGA)
    {
      omegaN = omegaN + (float32_t)encoder3_change * 10 / 4.0;
      if(omegaN < 20.0) omegaN = 20.0;
      else if(omegaN > 1000.0) omegaN = 1000.0;
      set_SAM_PLL();
    }
    else if(Menu2 == MENU_SAM_CATCH_BW)
    {
      pll_fmax = pll_fmax + (float32_t)encoder3_change * 100.0 / 4.0;
      if(pll_fmax < 200.0) pll_fmax = 200.0;
      else if(pll_fmax > 6000.0) pll_fmax = 6000.0;
      set_SAM_PLL();
    }
    else if(Menu2 == MENU_AGC_MODE)
    {
      AGC_mode = AGC_mode + (float32_t)encoder3_change / 4.0;
      if(AGC_mode > 5) AGC_mode = 5;
        else if (AGC_mode < 0) AGC_mode = 0;
      agc_switch_mode = 1;
      AGC_prep();  
    }
    else if(Menu2 == MENU_AGC_THRESH)
    {
      agc_thresh = agc_thresh + encoder3_change / 4.0;
      if(agc_thresh < -20) agc_thresh = -20;
      else if(agc_thresh > 120) agc_thresh = 120;
      AGC_prep();
    }
    else if(Menu2 == MENU_AGC_DECAY)
    {
      agc_decay = agc_decay + encoder3_change * 100.0 / 4.0;
      if(agc_decay < 100) agc_decay = 100;
      else if(agc_decay > 5000) agc_decay = 5000;
      AGC_prep();
    }
    else if(Menu2 == MENU_AGC_SLOPE)
    {
      agc_slope = agc_slope + encoder3_change * 10.0 / 4.0;
      if(agc_slope < 0) agc_slope = 0;
      else if(agc_slope > 200) agc_slope = 200;
      AGC_prep();
    }
    else if(Menu2 == MENU_STEREO_FACTOR)
    {
      stereo_factor = stereo_factor + encoder3_change * 10.0 / 4.0;
      if(stereo_factor < 0.0) stereo_factor = 0.0;
      else if(stereo_factor > 20000.0) stereo_factor = 20000.0;
    }
    else if(Menu2 == MENU_BASS)
    {
      bass = bass + (float32_t)encoder3_change / 80.0;
      if(bass > 1.0) bass = 1.0;
        else if (bass < -1.0) bass = -1.0;
      sgtl5000_1.eqBands (bass, midbass, mid, midtreble, treble); // (float bass, etc.) in % -100 to +100
//      sgtl5000_1.eqBands (bass, treble); // (float bass, float treble) in % -100 to +100
    }    
    else if(Menu2 == MENU_MIDBASS)
    {
      midbass = midbass + (float32_t)encoder3_change / 80.0;
      if(midbass > 1.0) midbass = 1.0;
        else if (midbass < -1.0) midbass = -1.0;
      sgtl5000_1.eqBands (bass, midbass, mid, midtreble, treble); // (float bass, etc.) in % -100 to +100
    }
        else if(Menu2 == MENU_MID)
    {
      mid = mid + (float32_t)encoder3_change / 80.0;
      if(mid > 1.0) mid = 1.0;
        else if (mid < -1.0) mid = -1.0;
      sgtl5000_1.eqBands (bass, midbass, mid, midtreble, treble); // (float bass, etc.) in % -100 to +100
    }
        else if(Menu2 == MENU_MIDTREBLE)
    {
      midtreble = midtreble + (float32_t)encoder3_change / 80.0;
      if(midtreble > 1.0) midtreble = 1.0;
        else if (midtreble < -1.0) midtreble = -1.0;
      sgtl5000_1.eqBands (bass, midbass, mid, midtreble, treble); // (float bass, etc.) in % -100 to +100
    }
    else if(Menu2 == MENU_TREBLE)
    {
      treble = treble + (float32_t)encoder3_change / 80.0;
      if(treble > 1.0) treble = 1.0;
        else if (treble < -1.0) treble =  -1.0;
      sgtl5000_1.eqBands (bass, midbass, mid, midtreble, treble); // (float bass, etc.) in % -100 to +100
//      sgtl5000_1.eqBands (bass, treble); // (float bass, float treble) in % -100 to +100
    }
    else if(Menu2 == MENU_SPECTRUM_DISPLAY_SCALE)
    {
      if(spectrum_display_scale < 100.0) spectrum_display_scale = spectrum_display_scale + (float32_t)encoder3_change / 4.0;
      else spectrum_display_scale = spectrum_display_scale + (float32_t)encoder3_change * 5.0;
      if(spectrum_display_scale > 2000.0) spectrum_display_scale = 2000.0;
        else if (spectrum_display_scale < 1.0) spectrum_display_scale =  1.0;
    }
    
    show_menu();
//    encoder3.write(0);

    }
}

void displayClock() {

  uint8_t hour10 = hour() / 10 % 10;
  uint8_t hour1 = hour() % 10;
  uint8_t minute10 = minute() / 10 % 10;
  uint8_t minute1 = minute() % 10;
  uint8_t second10 = second() / 10 % 10;
  uint8_t second1 = second() % 10;
  uint8_t time_pos_shift = 12; // distance between figures
  uint8_t dp = 7; // distance between ":" and figures

/*  if (mesz != mesz_old && mesz >= 0) {
    tft.setTextColor(ILI9341_ORANGE);
    tft.setFont(Arial_16);    
    tft.setCursor(pos_x_date, pos_y_date+20);
    tft.fillRect(pos_x_date, pos_y_date+20, 150-pos_x_date, 20, ILI9341_BLACK);
    tft.printf((mesz==0)?"(CET)":"(CEST)");
  }
*/
  tft.setFont(Arial_14);
  tft.setTextColor(ILI9341_WHITE);

  // set up ":" for time display
  if (!timeflag) {
    tft.setCursor(pos_x_time + 2 * time_pos_shift, pos_y_time);
    tft.print(":");
    tft.setCursor(pos_x_time + 4 * time_pos_shift + dp, pos_y_time);
    tft.print(":");
    tft.setCursor(pos_x_time + 7 * time_pos_shift + 2 * dp, pos_y_time);
    //      tft.print("UTC");
  }

  if (hour10 != hour10_old || !timeflag) {
    tft.setCursor(pos_x_time, pos_y_time);
    tft.fillRect(pos_x_time, pos_y_time, time_pos_shift, time_pos_shift + 2, ILI9341_BLACK);
    if (hour10) tft.print(hour10);  // do not display, if zero
  }
  if (hour1 != hour1_old || !timeflag) {
    tft.setCursor(pos_x_time + time_pos_shift, pos_y_time);
    tft.fillRect(pos_x_time  + time_pos_shift, pos_y_time, time_pos_shift, time_pos_shift + 2, ILI9341_BLACK);
    tft.print(hour1);  // always display
  }
  if (minute1 != minute1_old || !timeflag) {
    tft.setCursor(pos_x_time + 3 * time_pos_shift + dp, pos_y_time);
    tft.fillRect(pos_x_time  + 3 * time_pos_shift + dp, pos_y_time, time_pos_shift, time_pos_shift + 2, ILI9341_BLACK);
    tft.print(minute1);  // always display
  }
  if (minute10 != minute10_old || !timeflag) {
    tft.setCursor(pos_x_time + 2 * time_pos_shift + dp, pos_y_time);
    tft.fillRect(pos_x_time  + 2 * time_pos_shift + dp, pos_y_time, time_pos_shift, time_pos_shift + 2, ILI9341_BLACK);
    tft.print(minute10);  // always display
  }
  if (second10 != second10_old || !timeflag) {
    tft.setCursor(pos_x_time + 4 * time_pos_shift + 2 * dp, pos_y_time);
    tft.fillRect(pos_x_time  + 4 * time_pos_shift + 2 * dp, pos_y_time, time_pos_shift, time_pos_shift + 2, ILI9341_BLACK);
    tft.print(second10);  // always display
  }
  if (second1 != second1_old || !timeflag) {
    tft.setCursor(pos_x_time + 5 * time_pos_shift + 2 * dp, pos_y_time);
    tft.fillRect(pos_x_time  + 5 * time_pos_shift + 2 * dp, pos_y_time, time_pos_shift, time_pos_shift + 2, ILI9341_BLACK);
    tft.print(second1);  // always display
  }

  hour1_old = hour1;
  hour10_old = hour10;
  minute1_old = minute1;
  minute10_old = minute10;
  second1_old = second1;
  second10_old = second10;
  mesz_old = mesz;
  timeflag = 1;
  tft.setFont(Arial_10);
} // end function displayTime

void displayDate() {
  char string99 [20];
  tft.fillRect(pos_x_date, pos_y_date, 320 - pos_x_date, 20, ILI9341_BLACK); // erase old string
  tft.setTextColor(ILI9341_ORANGE);
  tft.setFont(Arial_12);
  tft.setCursor(pos_x_date, pos_y_date);
  //  Date: %s, %d.%d.20%d P:%d %d", Days[weekday-1], day, month, year
  sprintf(string99, "%s, %02d.%02d.%04d", Days[weekday()], day(), month(), year());
  tft.print(string99);
} // end function displayDate

void set_SAM_PLL() {
// DX adjustments: zeta = 0.15, omegaN = 100.0
// very stable, but does not lock very fast
// standard settings: zeta = 1.0, omegaN = 250.0
// maybe user can choose between slow (DX), medium, fast SAM PLL
// zeta / omegaN
// DX = 0.2, 70
// medium 0.6, 200
// fast 1.2, 500
//float32_t zeta = 0.8; // PLL step response: smaller, slower response 1.0 - 0.1
//float32_t omegaN = 250.0; // PLL bandwidth 50.0 - 1000.0

omega_min = TPI * (-pll_fmax) * DF / SR[SAMPLE_RATE].rate;
omega_max = TPI * pll_fmax * DF / SR[SAMPLE_RATE].rate;
zeta = (float32_t)zeta_help / 100.0; 
g1 = 1.0 - expf(-2.0 * omegaN * zeta * DF / SR[SAMPLE_RATE].rate);
g2 = - g1 + 2.0 * (1 - expf(- omegaN * zeta * DF / SR[SAMPLE_RATE].rate) * cosf(omegaN * DF / SR[SAMPLE_RATE].rate * sqrtf(1.0 - zeta * zeta)));

mtauR = expf(- DF / (SR[SAMPLE_RATE].rate * tauR)); 
onem_mtauR = 1.0 - mtauR;
mtauI = expf(- DF / (SR[SAMPLE_RATE].rate * tauI)); 
onem_mtauI = 1.0 - mtauI;  
}


// Unfortunately, this does not work for SAM PLL phase determination
// AND it is not less computation-intense compared to atan2f
float32_t atan2_fast(float32_t Im, float32_t Re)
{
  float32_t tan_1;
//  float32_t Q_div_I;
  Im = Im + 1e-9;
  Re = Re + 1e-9;
  if(fabs(Im) - fabs(Re) < 0)
  {  
//    Q_div_I = Im / Re;
    // Eq 13-108 in Lyons 2011 
    tan_1 = (Re * Im) / (Re * Re + (0.25 + 0.03125) * Im * Im);
    int8_t sign = Re > 0;
    if(!sign) sign = -1;
    if(Re > 0)
    {
      // 1st/8th octant
      return tan_1;
    }
    else
    {
      // 4th/5th octant
      return (PI * sign + tan_1);      
    }
  }
  else 
  {
    // Eq 13-108 in Lyons 2011 
    tan_1 = (Re * Im) / (Im * Im + (0.25 + 0.03125) * Re * Re);

    if(Im > 0)
    {
      // 2nd/3rd octant
      return (PIH - tan_1);
    }
    else
    {
      // 6th/7th octant
      return (- PIH - tan_1);
    }
  }
}

void playFileMP3(const char *filename)
{
  trackchange = true; //auto track change is allowed.
  // Start playing the file.  This sketch continues to
  // run while the file plays.
  printTrack();
  EEPROM.write(eeprom_adress,track); //meanwhile write the track position to eeprom address 0
  Serial.println("After EEPROM.write");
  playMp3.play(filename);
  // Simply wait for the file to finish playing.
  while (playMp3.isPlaying()) {
  show_load();
  buttons();
  encoders();
  displayClock();
  }
}

void playFileAAC(const char *filename)
{
  trackchange = true; //auto track change is allowed.
  // Start playing the file.  This sketch continues to
  // run while the file plays.
  // print track no & trackname
  printTrack ();
  EEPROM.write(eeprom_adress,track); //meanwhile write the track position to eeprom address 0
  playAac.play(filename);
  // Simply wait for the file to finish playing.
  while (playAac.isPlaying()) {
    // update controls!
  show_load();  
  buttons();
  encoders();
  displayClock();  
  }
}
void nexttrack(){
  Serial.println("Next track!");
  trackchange=false; // we are doing a track change here, so the auto trackchange will not skip another one.
  playMp3.stop();
  playAac.stop();
  track++;
  if(track >= tracknum){ // keeps in tracklist.
    track = 0;
  }  
  tracklist[track].toCharArray(playthis, sizeof(tracklist[track])); //since we have to convert String to Char will do this    
}

void prevtrack(){
  Serial.println("Previous track! ");
  trackchange=false; // we are doing a track change here, so the auto trackchange will not skip another one.
  playMp3.stop();
  playAac.stop();
  track--;
  if(track <0){ // keeps in tracklist.
    track = tracknum-1;
  }  
  tracklist[track].toCharArray(playthis, sizeof(tracklist[track])); //since we have to convert String to Char will do this    
}

void pausetrack(){
  paused = !paused;
    playMp3.pause(paused);
    playAac.pause(paused);
}


void randomtrack(){
  Serial.println("Random track!");
  trackchange=false; // we are doing a track change here, so the auto trackchange will not skip another one.
  if(trackext[track] == 1) playMp3.stop();
  if(trackext[track] == 2) playAac.stop();

  track= random(tracknum);

  tracklist[track].toCharArray(playthis, sizeof(tracklist[track])); //since we have to convert String to Char will do this    
}

void printTrack () {
  tft.fillRect(0,222,320,17,ILI9341_BLACK);
  tft.setCursor(0, 222);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextWrap(true);
  tft.setTextSize(2);
  tft.print("Track: ");
  tft.print (track); 
  tft.print (" "); tft.print (playthis);
 } //end printTrack

void show_load(){
            if (five_sec.check() == 1)
    {
      Serial.print("Proc = ");
      Serial.print(AudioProcessorUsage());
      Serial.print(" (");    
      Serial.print(AudioProcessorUsageMax());
      Serial.print("),  Mem = ");
      Serial.print(AudioMemoryUsage());
      Serial.print(" (");    
      Serial.print(AudioMemoryUsageMax());
      Serial.println(")");
      AudioProcessorUsageMaxReset();
      AudioMemoryUsageMaxReset();
    }
}

float32_t sign(float32_t x) {
  if(x < 0)
    return -1.0f;
    else
      if(x > 0)
        return 1.0f;
          else return 0.0f;
}

void Calculatedbm()
{
// calculation of the signal level inside the filter bandwidth
// taken from the spectrum display FFT
// taking into account the analog gain before the ADC
// analog gain is adjusted in steps of 1.5dB
// bands[band].RFgain = 0 --> 0dB gain
// bands[band].RFgain = 15 --> 22.5dB gain
            
            float32_t slope = 19.8; // 
            float32_t cons = -92; // 
            float32_t Lbin, Ubin;
            float32_t bw_LSB = 0.0;
            float32_t bw_USB = 0.0;
//            float64_t sum_db = 0.0; // FIXME: mabye this slows down the FPU, because the FPU does only process 32bit floats ???
            float32_t sum_db = 0.0; // FIXME: mabye this slows down the FPU, because the FPU does only process 32bit floats ???
            int posbin = 0;
            float32_t bin_BW = (float32_t) (SR[SAMPLE_RATE].rate / 256.0);
            // width of a 256 tap FFT bin @ 96ksps = 375Hz
            // we have to take into account the magnify mode
            // --> recalculation of bin_BW
            bin_BW = bin_BW / (1 << spectrum_zoom); // correct bin bandwidth is determined by the Zoom FFT display setting
//            Serial.print("bin_BW = "); Serial.println(bin_BW);

            // in all magnify cases (2x up to 16x) the posbin is in the centre of the spectrum display
            if(spectrum_zoom != 0)
            {
                posbin = 128; // right in the middle!
            } 
            else 
            {
                posbin = 64;
            }

            //  determine Lbin and Ubin from ts.dmod_mode and FilterInfo.width
            //  = determine bandwith separately for lower and upper sideband

            switch(band[bands].mode)
            {
            case DEMOD_LSB:
            case DEMOD_SAM_LSB:
                bw_USB = 0.0;
                bw_LSB = (float32_t)bands[band].bandwidthL;
                break;
            case DEMOD_USB:
            case DEMOD_SAM_USB:
                bw_LSB = 0.0;
                bw_USB = (float32_t)bands[band].bandwidthU;
                break;
            default:
                bw_LSB = (float32_t)bands[band].bandwidthL;
                bw_USB = (float32_t)bands[band].bandwidthU;
            }
            // calculate upper and lower limit for determination of signal strength
            // = filter passband is between the lower bin Lbin and the upper bin Ubin
            Lbin = (float32_t)posbin - roundf(bw_LSB / bin_BW);
            Ubin = (float32_t)posbin + roundf(bw_USB / bin_BW); // the bin on the upper sideband side

            // take care of filter bandwidths that are larger than the displayed FFT bins
            if(Lbin < 0)
            {
                Lbin = 0;
            }
            if (Ubin > 255)
            {
                Ubin = 255;
            }
            //Serial.print("Lbin = "); Serial.println(Lbin);
            //Serial.print("Ubin = "); Serial.println(Ubin);
            if((int)Lbin == (int)Ubin) 
            {
                Ubin = 1.0 + Lbin;
            }
            // determine the sum of all the bin values in the passband
            for (int c = (int)Lbin; c <= (int)Ubin; c++)   // sum up all the values of all the bins in the passband
            {
//                sum_db = sum_db + FFT_spec[c];
                sum_db = sum_db + FFT_spec_old[c];
            }

            if (sum_db > 0)
            {
                dbm = (float32_t)RF_attenuation + slope * log10f (sum_db) + cons - (float32_t)bands[band].RFgain * 1.5;
                dbmhz = (float32_t)RF_attenuation +  - (float32_t)bands[band].RFgain * 1.5 + slope * log10f (sum_db) -  10 * log10f ((float32_t)(((int)Ubin-(int)Lbin) * bin_BW)) + cons;
            }
            else
            {
                dbm = -145.0;
                dbmhz = -145.0;
            }

            // lowpass IIR filter
            // Wheatley 2011: two averagers with two time constants
            // IIR filter with one element analog to 1st order RC filter
            // but uses two different time constants (ALPHA = 1 - e^(-T/Tau)) depending on
            // whether the signal is increasing (attack) or decreasing (decay)
            // m_AttackAlpha = 0.8647; //  ALPHA = 1 - e^(-T/Tau), T = 0.02s (because dbm routine is called every 20ms!)
            // Tau = 10ms = 0.01s attack time
            // m_DecayAlpha = 0.0392; // 500ms decay time
            //
            m_AttackAvedbm = (1.0 - m_AttackAlpha) * m_AttackAvedbm + m_AttackAlpha * dbm;
            m_DecayAvedbm = (1.0 - m_DecayAlpha) * m_DecayAvedbm + m_DecayAlpha * dbm;
            m_AttackAvedbmhz = (1.0 - m_AttackAlpha) * m_AttackAvedbmhz + m_AttackAlpha * dbmhz;
            m_DecayAvedbmhz = (1.0 - m_DecayAlpha) * m_DecayAvedbmhz + m_DecayAlpha * dbmhz;

            if (m_AttackAvedbm > m_DecayAvedbm)
            { // if attack average is larger then it must be an increasing signal
                m_AverageMagdbm = m_AttackAvedbm; // use attack average value for output
                m_DecayAvedbm = m_AttackAvedbm; // set decay average to attack average value for next time
            }
            else
            { // signal is decreasing, so use decay average value
                m_AverageMagdbm = m_DecayAvedbm;
            }

            if (m_AttackAvedbmhz > m_DecayAvedbmhz)
            { // if attack average is larger then it must be an increasing signal
                m_AverageMagdbmhz = m_AttackAvedbmhz; // use attack average value for output
                m_DecayAvedbmhz = m_AttackAvedbmhz; // set decay average to attack average value for next time
            }
            else
            { // signal is decreasing, so use decay average value
                m_AverageMagdbmhz = m_DecayAvedbmhz;
            }

            dbm = m_AverageMagdbm; // write average into variable for S-meter display
            dbmhz = m_AverageMagdbmhz; // write average into variable for S-meter display
 
}

void Display_dbm()
{
    static int dbm_old = (int)dbm;
    bool display_something = false;
        long val;
        const char* unit_label;
        float32_t dbuv;

        switch(display_dbm)
        {
        case DISPLAY_S_METER_DBM:
            display_something = true;
            val = dbm;
            unit_label = "dBm   ";
            break;
        case DISPLAY_S_METER_DBMHZ:
            display_something = true;
            val = dbmhz;
            unit_label = "dBm/Hz";
            break;
        }
        if((int) dbm == dbm_old) display_something = false;
        
        if (display_something == true) 
        {        
            tft.fillRect(pos_x_dbm, pos_y_dbm, 100, 16, ILI9341_BLACK);
            char txt[12];
            snprintf(txt,12,"%4ld      ", val);
            tft.setFont(Arial_14);
            tft.setCursor(pos_x_dbm, pos_y_dbm);
            tft.setTextColor(ILI9341_WHITE);
            tft.print(txt);
            tft.setFont(Arial_9);
            tft.setCursor(pos_x_dbm + 42, pos_y_dbm + 5);
            tft.setTextColor(ILI9341_GREEN);
            tft.print(unit_label);
            dbm_old = (int)dbm;
            
            float32_t s = 9.0 + ((dbm + 73.0) / 6.0);
            if (s <0.0) s=0.0;
            if ( s > 9.0)
            {
              dbuv = dbm + 73.0;
              s = 9.0;
            }
            else dbuv = 0.0;
           uint8_t pos_sxs_w = pos_x_smeter + s * s_w + 2;
           uint8_t sxs_w = s * s_w + 2;
           uint8_t nine_sw_minus = (9*s_w + 1) - s * s_w; 
//            tft.drawFastHLine(pos_x_smeter, pos_y_smeter, s*s_w+1, ILI9341_BLUE);
//            tft.drawFastHLine(pos_x_smeter+s*s_w+1, pos_y_smeter, nine_sw_minus, ILI9341_BLACK);

            tft.drawFastHLine(pos_x_smeter + 1, pos_y_smeter + 1, sxs_w, ILI9341_BLUE);
            tft.drawFastHLine(pos_sxs_w, pos_y_smeter+1, nine_sw_minus, ILI9341_BLACK);
            tft.drawFastHLine(pos_x_smeter + 1, pos_y_smeter + 2, sxs_w, ILI9341_WHITE);
            tft.drawFastHLine(pos_sxs_w, pos_y_smeter+2, nine_sw_minus, ILI9341_BLACK);
            tft.drawFastHLine(pos_x_smeter + 1, pos_y_smeter + 3, sxs_w, ILI9341_WHITE);
            tft.drawFastHLine(pos_sxs_w, pos_y_smeter+3, nine_sw_minus, ILI9341_BLACK);
            tft.drawFastHLine(pos_x_smeter + 1, pos_y_smeter + 4, sxs_w, ILI9341_BLUE);
            tft.drawFastHLine(pos_sxs_w, pos_y_smeter+4, nine_sw_minus, ILI9341_BLACK);
//            tft.drawFastHLine(pos_x_smeter, pos_y_smeter+5, sxs_w, ILI9341_BLUE);
//            tft.drawFastHLine(pos_x_smeter+s*s_w+1, pos_y_smeter+5, nine_sw_minus, ILI9341_BLACK);
      
            if(dbuv>30) dbuv=30;
            if(dbuv > 0)
            {
                uint8_t dbuv_div_x = (dbuv/5)*s_w+1;
                uint8_t six_sw_minus = (6*s_w+1)-(dbuv/5)*s_w;
                uint8_t nine_sw_plus_x = pos_x_smeter + 9*s_w + (dbuv/5) * s_w + 1;
                uint8_t nine_sw_plus = pos_x_smeter + 9*s_w + 1;
                tft.drawFastHLine(nine_sw_plus, pos_y_smeter, dbuv_div_x, ILI9341_RED);
                tft.drawFastHLine(nine_sw_plus_x, pos_y_smeter, six_sw_minus, ILI9341_BLACK);
                tft.drawFastHLine(nine_sw_plus, pos_y_smeter + 1, dbuv_div_x, ILI9341_RED);
                tft.drawFastHLine(nine_sw_plus_x, pos_y_smeter + 1, six_sw_minus, ILI9341_BLACK);
                tft.drawFastHLine(nine_sw_plus, pos_y_smeter + 2, dbuv_div_x, ILI9341_RED);
                tft.drawFastHLine(nine_sw_plus_x, pos_y_smeter + 2, six_sw_minus, ILI9341_BLACK);
                tft.drawFastHLine(nine_sw_plus, pos_y_smeter + 3, dbuv_div_x, ILI9341_RED);
                tft.drawFastHLine(nine_sw_plus_x, pos_y_smeter + 3, six_sw_minus, ILI9341_BLACK);
                tft.drawFastHLine(nine_sw_plus, pos_y_smeter + 4, dbuv_div_x, ILI9341_RED);
                tft.drawFastHLine(nine_sw_plus_x, pos_y_smeter + 4, six_sw_minus, ILI9341_BLACK);
                tft.drawFastHLine(nine_sw_plus, pos_y_smeter + 5, dbuv_div_x, ILI9341_RED);
                tft.drawFastHLine(nine_sw_plus_x, pos_y_smeter + 5, six_sw_minus, ILI9341_BLACK);
            }
            }
            // AGC box
            if(agc_action == 1)
            {
                tft.setCursor(pos_x_dbm + 98, pos_y_dbm + 4);
                tft.setFont(Arial_11);
                tft.setTextColor(ILI9341_WHITE);
                tft.print("AGC");
            }
            else
            {
                tft.fillRect(pos_x_dbm + 98, pos_y_dbm + 4, 100, 16, ILI9341_BLACK);
            }
    
}

void EEPROM_LOAD() {

   struct config_t {
       unsigned long long calibration_factor;
       long calibration_constant;
       unsigned long long freq[NUM_BANDS];
       int mode[NUM_BANDS];
       int bwu[NUM_BANDS];
       int bwl[NUM_BANDS];
       int rfg[NUM_BANDS];
       int band;
       float32_t LPFcoeff;
       int audio_volume;
       int8_t AGC_mode;
       float32_t pll_fmax;
       float32_t omegaN;
       int zeta_help;
       uint8_t rate;
       float32_t bass;
       float32_t treble;
       int agc_thresh;
       int agc_decay;
       int agc_slope;
       uint8_t auto_IQ_correction;
       float32_t midbass;
       float32_t mid;
       float32_t midtreble;
       int8_t RF_attenuation;
 } E; 

    eeprom_read_block(&E,0,sizeof(E));
    calibration_factor = E.calibration_factor;
    calibration_constant = E.calibration_constant;
    for (int i=0; i< (NUM_BANDS); i++) 
        bands[i].freq = E.freq[i];
    for (int i=0; i< (NUM_BANDS); i++)
        bands[i].mode = E.mode[i];
    for (int i=0; i< (NUM_BANDS); i++)
        bands[i].bandwidthU = E.bwu[i];
    for (int i=0; i< (NUM_BANDS); i++)
        bands[i].bandwidthL = E.bwl[i];
    for (int i=0; i< (NUM_BANDS); i++)
        bands[i].RFgain = E.rfg[i];
    band = E.band;
    //I_help = E.I_ampl;
    //Q_in_I_help = E.Q_in_I;
    //I_in_Q_help = E.I_in_Q;
    //Window_FFT = E.Window_FFT;
     LPF_spectrum = E.LPFcoeff;
     audio_volume = E.audio_volume;
     AGC_mode = E.AGC_mode;
     pll_fmax = E.pll_fmax;
     omegaN = E.omegaN;
     zeta_help = E.zeta_help;
     zeta = (float32_t) zeta_help / 100.0;
     SAMPLE_RATE = E.rate;
     bass = E.bass;
     treble = E.treble;
     agc_thresh = E.agc_thresh;
     agc_decay = E.agc_decay;
     agc_slope = E.agc_slope;
     auto_IQ_correction = E.auto_IQ_correction;
     midbass = E.midbass;
     mid = E.mid;
     midtreble = E.midtreble;
     RF_attenuation = E.RF_attenuation;
} // end void eeProm LOAD 

void EEPROM_SAVE() {

  struct config_t {
       unsigned long long calibration_factor;
       long calibration_constant;
       unsigned long long freq[NUM_BANDS];
       int mode[NUM_BANDS];
       int bwu[NUM_BANDS];
       int bwl[NUM_BANDS];
       int rfg[NUM_BANDS];
       int band;
       float32_t LPFcoeff;
       int audio_volume;
       int8_t AGC_mode;
       float32_t pll_fmax;
       float32_t omegaN;
       int zeta_help;
       uint8_t rate;
       float32_t bass;
       float32_t treble;
       int agc_thresh;
       int agc_decay;
       int agc_slope;
       uint8_t auto_IQ_correction;
       float32_t midbass;
       float32_t mid;
       float32_t midtreble;
       int8_t RF_attenuation;
 } E; 

      E.calibration_factor = calibration_factor;
      E.band = band;
      E.calibration_constant = calibration_constant;
      for (int i=0; i< (NUM_BANDS); i++) 
        E.freq[i] = bands[i].freq;
      for (int i=0; i< (NUM_BANDS); i++)
        E.mode[i] = bands[i].mode;
      for (int i=0; i< (NUM_BANDS); i++)
        E.bwu[i] = bands[i].bandwidthU;
      for (int i=0; i< (NUM_BANDS); i++)
        E.bwl[i] = bands[i].bandwidthL;
      for (int i=0; i< (NUM_BANDS); i++)
        E.rfg[i] = bands[i].RFgain;
//      E.I_ampl = I_help;
//      E.Q_in_I = Q_in_I_help;
//      E.I_in_Q = I_in_Q_help;
//      E.Window_FFT = Window_FFT;
//      E.LPFcoeff = LPFcoeff;
     E.LPFcoeff = LPF_spectrum;
     E.audio_volume = audio_volume;
     E.AGC_mode = AGC_mode;
     E.pll_fmax = pll_fmax;
     E.omegaN = omegaN;
     E.zeta_help = zeta_help;
     E.rate = SAMPLE_RATE;
     E.bass = bass;
     E.treble = treble;
     E.agc_thresh = agc_thresh;
     E.agc_decay = agc_decay;
     E.agc_slope = agc_slope;
     E.auto_IQ_correction = auto_IQ_correction;
     E.midbass = midbass;
     E.mid = mid;
     E.midtreble = midtreble;
     E.RF_attenuation = RF_attenuation;
     eeprom_write_block (&E,0,sizeof(E));
} // end void eeProm SAVE 

/*
void set_freq_conv2(float32_t NCO_FREQ) {
//  float32_t NCO_FREQ = AUDIO_SAMPLE_RATE_EXACT / 16; // + 20;
float32_t NCO_INC = 2 * PI * NCO_FREQ / AUDIO_SAMPLE_RATE_EXACT;
float32_t OSC_COS = cos (NCO_INC);
float32_t OSC_SIN = sin (NCO_INC);
float32_t Osc_Vect_Q = 1.0;
float32_t Osc_Vect_I = 0.0;
float32_t Osc_Gain = 0.0;
float32_t Osc_Q = 0.0;
float32_t Osc_I = 0.0;
float32_t i_temp = 0.0;
float32_t q_temp = 0.0;
}
*/
/*
void freq_conv2()
{
  uint     i;
      for(i = 0; i < BUFFER_SIZE; i++) {
        // generate local oscillator on-the-fly:  This takes a lot of processor time!
        Osc_Q = (Osc_Vect_Q * OSC_COS) - (Osc_Vect_I * OSC_SIN);  // Q channel of oscillator
        Osc_I = (Osc_Vect_I * OSC_COS) + (Osc_Vect_Q * OSC_SIN);  // I channel of oscillator
        Osc_Gain = 1.95 - ((Osc_Vect_Q * Osc_Vect_Q) + (Osc_Vect_I * Osc_Vect_I));  // Amplitude control of oscillator
        // rotate vectors while maintaining constant oscillator amplitude
        Osc_Vect_Q = Osc_Gain * Osc_Q;
        Osc_Vect_I = Osc_Gain * Osc_I;
        //
        // do actual frequency conversion
        float_buffer_L[i] = (float_buffer_L_3[i] * Osc_Q) + (float_buffer_R_3[i] * Osc_I);  // multiply I/Q data by sine/cosine data to do translation
        float_buffer_R[i] = (float_buffer_R_3[i] * Osc_Q) - (float_buffer_L_3[i] * Osc_I);
        //
      }
} // end freq_conv2()

*/


void reset_codec ()
{
                      AudioNoInterrupts();
                    sgtl5000_1.disable();
                    delay(10);
                    sgtl5000_1.enable();
                    delay(10);
                    sgtl5000_1.inputSelect(myInput);
                    sgtl5000_1.adcHighPassFilterDisable(); // does not help too much!
                    sgtl5000_1.lineInLevel(bands[band].RFgain);
                    sgtl5000_1.lineOutLevel(31);
                    sgtl5000_1.audioPostProcessorEnable(); // enables the DAP chain of the codec post audio processing before the headphone out
                    sgtl5000_1.eqSelect (3); // Tone Control
                    sgtl5000_1.eqBands (bass, midbass, mid, midtreble, treble); // in % -100 to +100
                    sgtl5000_1.enhanceBassEnable();
                    sgtl5000_1.dacVolumeRamp();
                    sgtl5000_1.volume((float32_t)audio_volume / 100.0); // 
                    AudioInterrupts();

}

void setAttenuator(int value)
{ 
  // bit-banging of the digital step attenuator chip PE4306
  // allows 0 to 31dB RF attenuation in 1dB steps
  // inspired by https://github.com/jefftranter/Arduino/blob/master/pe4306/pe4306.ino
  int level; // Holds level of DATA line when shifting
  
  if (value < 0) value = 0;
  if (value > 31) value = 31;

  digitalWrite(ATT_LE, LOW);
  digitalWrite(ATT_CLOCK,LOW);
  
  for (int bit = 5; bit >= 0; bit--) {
    if (bit == 0) 
    {
        level = 0; // B0 has to be set to zero
    }
    else 
    { // left shift of 1, because B0 has to be zero and value starts at B1
      // then right shift by the "bit"-variable value to write the specific bit
      // what does &0x01 do? --> sets the specific bit to a binary 1 
        level = ((value << 1) >> bit) & 0x01; // Level is value of bit
    }

    digitalWrite(ATT_DATA, level); // Write data value
    digitalWrite(ATT_CLOCK, HIGH); // Toggle clock high and then low
    digitalWrite(ATT_CLOCK, LOW);
  }
  digitalWrite(ATT_LE, HIGH); // Toggle LE high to enable latch
  digitalWrite(ATT_LE, LOW);  // and then low again to hold it.
}

void show_analog_gain()
{
  static uint8_t RF_gain_old = 0;
  static uint8_t RF_att_old = 0;
  char string[16];
  const uint16_t col = ILI9341_GREEN;
  // automatic RF gain indicated by different colors??
            if((((bands[band].RFgain != RF_gain_old) || (RF_attenuation != RF_att_old)) && twinpeaks_tested == 1) || write_analog_gain)
            {
                tft.setCursor(pos_x_time - 40, pos_y_time + 26);
                tft.setFont(Arial_8);
                tft.setTextColor(ILI9341_BLACK);
                sprintf(string,"%02.1fdB -", (float)(RF_gain_old * 1.5));
                tft.print(string);
                tft.setCursor(pos_x_time - 40, pos_y_time + 26);
                tft.setTextColor(col);
                sprintf(string,"%02.1fdB -", (float)(bands[band].RFgain * 1.5));
                tft.print(string);
                //tft.print(" - ");
                tft.setCursor(pos_x_time, pos_y_time + 26);
                tft.setTextColor(ILI9341_BLACK);
                sprintf(string," %2ddB", RF_att_old);
                tft.print(string);
                tft.setCursor(pos_x_time, pos_y_time + 26);
                tft.setTextColor(col);
                sprintf(string," %2ddB", RF_attenuation);
                tft.print(string);
                tft.print(" = ");
                tft.setCursor(pos_x_time + 40, pos_y_time + 24);
                tft.setFont(Arial_9);
                tft.setTextColor(ILI9341_BLACK);
                sprintf(string,"%02.1fdB", (float)(RF_gain_old * 1.5) - (float)RF_att_old);
                tft.print(string);
                tft.setCursor(pos_x_time + 40, pos_y_time + 24);
                tft.setTextColor(ILI9341_WHITE);
                sprintf(string,"%02.1fdB", (float)(bands[band].RFgain * 1.5) - (float)RF_attenuation);
                tft.print(string);
                RF_gain_old = bands[band].RFgain;
                RF_att_old = RF_attenuation;
                write_analog_gain = 0;
           }
}


void show_notch(int notchF, int MODE) {
  // pos_centre_f is the x position of the Rx centre
  // pos is the y position of the spectrum display 
  // notch display should be at x = pos_centre_f +- notch frequency and y = 20 
  //  LSB: 
  float32_t spectrum_span = SR[SAMPLE_RATE].rate / 1000.0 / (1<<spectrum_zoom);
  pos_centre_f+=1; // = pos_centre_f + 1;
          // delete old indicator
          tft.drawFastVLine(pos_centre_f + 1 + 160/spectrum_span * oldnotchF / 1000, notchpos, notchL, ILI9341_BLACK);
          tft.drawFastVLine(pos_centre_f + 160/spectrum_span * oldnotchF / 1000, notchpos, notchL, ILI9341_BLACK);
          tft.drawFastVLine(pos_centre_f -1 + 160/spectrum_span * oldnotchF / 1000, notchpos, notchL, ILI9341_BLACK);
          tft.drawFastHLine(pos_centre_f -4 + 160/spectrum_span * oldnotchF / 1000, notchpos+notchL, 9, ILI9341_BLACK);
          tft.drawFastHLine(pos_centre_f -3 + 160/spectrum_span * oldnotchF / 1000, notchpos+notchL + 1, 7, ILI9341_BLACK);
          tft.drawFastHLine(pos_centre_f -2 + 160/spectrum_span * oldnotchF / 1000, notchpos+notchL + 2, 5, ILI9341_BLACK);
          tft.drawFastHLine(pos_centre_f -1 + 160/spectrum_span * oldnotchF / 1000, notchpos+notchL + 3, 3, ILI9341_BLACK);
          tft.drawFastVLine(pos_centre_f + 160/spectrum_span * oldnotchF / 1000, notchpos+notchL + 4, 2, ILI9341_BLACK);

          tft.drawFastVLine(pos_centre_f +1 - 160/spectrum_span * oldnotchF / 1000, notchpos, notchL, ILI9341_BLACK);
          tft.drawFastVLine(pos_centre_f - 160/spectrum_span * oldnotchF / 1000, notchpos, notchL, ILI9341_BLACK);
          tft.drawFastVLine(pos_centre_f -1 - 160/spectrum_span * oldnotchF / 1000, notchpos, notchL, ILI9341_BLACK);
          tft.drawFastHLine(pos_centre_f -4 - 160/spectrum_span * oldnotchF / 1000, notchpos+notchL, 9, ILI9341_BLACK);
          tft.drawFastHLine(pos_centre_f -3 - 160/spectrum_span * oldnotchF / 1000, notchpos+notchL + 1, 7, ILI9341_BLACK);
          tft.drawFastHLine(pos_centre_f -2 - 160/spectrum_span * oldnotchF / 1000, notchpos+notchL + 2, 5, ILI9341_BLACK);
          tft.drawFastHLine(pos_centre_f -1 - 160/spectrum_span * oldnotchF / 1000, notchpos+notchL + 3, 3, ILI9341_BLACK);
          tft.drawFastVLine(pos_centre_f - 160/spectrum_span * oldnotchF / 1000, notchpos+notchL + 4, 2, ILI9341_BLACK);
          // Show mid screen tune position
          tft.drawFastVLine(pos_centre_f - 1, 0,pos+1, ILI9341_RED); //WHITE);

      if (notchF >= 400 || notchF <= -400) {
          // draw new indicator according to mode
      switch (MODE)  {
          case DEMOD_LSB: //modeLSB:
          tft.drawFastVLine(pos_centre_f + 1 - 160/spectrum_span * notchF / -1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f - 160/spectrum_span * notchF / -1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f -1 - 160/spectrum_span * notchF / -1000, notchpos, notchL, notchColour);
          tft.drawFastHLine(pos_centre_f -4 - 160/spectrum_span * notchF / -1000, notchpos+notchL, 9, notchColour);
          tft.drawFastHLine(pos_centre_f -3 - 160/spectrum_span * notchF / -1000, notchpos+notchL + 1, 7, notchColour);
          tft.drawFastHLine(pos_centre_f -2 - 160/spectrum_span * notchF / -1000, notchpos+notchL + 2, 5, notchColour);
          tft.drawFastHLine(pos_centre_f -1 - 160/spectrum_span * notchF / -1000, notchpos+notchL + 3, 3, notchColour);
          tft.drawFastVLine(pos_centre_f - 160/spectrum_span * notchF / -1000, notchpos+notchL + 4, 2, notchColour);
          break;
          case DEMOD_USB: //modeUSB:
          tft.drawFastVLine(pos_centre_f +1 + 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f + 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f -1 + 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastHLine(pos_centre_f -4 + 160/spectrum_span * notchF / 1000, notchpos+notchL, 9, notchColour);
          tft.drawFastHLine(pos_centre_f -3 + 160/spectrum_span * notchF / 1000, notchpos+notchL + 1, 7, notchColour);
          tft.drawFastHLine(pos_centre_f -2 + 160/spectrum_span * notchF / 1000, notchpos+notchL + 2, 5, notchColour);
          tft.drawFastHLine(pos_centre_f -1 + 160/spectrum_span * notchF / 1000, notchpos+notchL + 3, 3, notchColour);
          tft.drawFastVLine(pos_centre_f + 160/spectrum_span * notchF / 1000, notchpos+notchL + 4, 2, notchColour);
          break;
          case DEMOD_AM2: // modeAM:
          case DEMOD_SAM:
          tft.drawFastVLine(pos_centre_f + 1 + 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f + 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f - 1 + 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastHLine(pos_centre_f -4 + 160/spectrum_span * notchF / 1000, notchpos+notchL, 9, notchColour);
          tft.drawFastHLine(pos_centre_f -3 + 160/spectrum_span * notchF / 1000, notchpos+notchL + 1, 7, notchColour);
          tft.drawFastHLine(pos_centre_f -2 + 160/spectrum_span * notchF / 1000, notchpos+notchL + 2, 5, notchColour);
          tft.drawFastHLine(pos_centre_f -1 + 160/spectrum_span * notchF / 1000, notchpos+notchL + 3, 3, notchColour);
          tft.drawFastVLine(pos_centre_f + 160/spectrum_span * notchF / 1000, notchpos+notchL + 4, 2, notchColour);

          tft.drawFastVLine(pos_centre_f + 1 - 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f - 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f - 1 - 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastHLine(pos_centre_f -4 - 160/spectrum_span * notchF / 1000, notchpos+notchL, 9, notchColour);
          tft.drawFastHLine(pos_centre_f -3 - 160/spectrum_span * notchF / 1000, notchpos+notchL + 1, 7, notchColour);
          tft.drawFastHLine(pos_centre_f -2 - 160/spectrum_span * notchF / 1000, notchpos+notchL + 2, 5, notchColour);
          tft.drawFastHLine(pos_centre_f -1 - 160/spectrum_span * notchF / 1000, notchpos+notchL + 3, 3, notchColour);
          tft.drawFastVLine(pos_centre_f - 160/spectrum_span * notchF / 1000, notchpos+notchL + 4, 2, notchColour);
          break;
          case DEMOD_DSB: //modeDSB:
//          case DEMOD_STEREO_AM: //modeStereoAM:
          if (notchF <=-400) {
          tft.drawFastVLine(pos_centre_f + 1 - 160/spectrum_span * notchF / -1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f - 160/spectrum_span * notchF / -1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f - 1 - 160/spectrum_span * notchF / -1000, notchpos, notchL, notchColour);
          tft.drawFastHLine(pos_centre_f -4 - 160/spectrum_span * notchF / -1000, notchpos+notchL, 9, notchColour);
          tft.drawFastHLine(pos_centre_f -3 - 160/spectrum_span * notchF / -1000, notchpos+notchL + 1, 7, notchColour);
          tft.drawFastHLine(pos_centre_f -2 - 160/spectrum_span * notchF / -1000, notchpos+notchL + 2, 5, notchColour);
          tft.drawFastHLine(pos_centre_f -1 - 160/spectrum_span * notchF / -1000, notchpos+notchL + 3, 3, notchColour);
          tft.drawFastVLine(pos_centre_f - 160/spectrum_span * notchF / -1000, notchpos+notchL + 4, 2, notchColour);
          }
          if (notchF >=400) {
          tft.drawFastVLine(pos_centre_f + 1 + 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f + 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastVLine(pos_centre_f - 1 + 160/spectrum_span * notchF / 1000, notchpos, notchL, notchColour);
          tft.drawFastHLine(pos_centre_f -4 + 160/spectrum_span * notchF / 1000, notchpos+notchL, 9, notchColour);
          tft.drawFastHLine(pos_centre_f -3 + 160/spectrum_span * notchF / 1000, notchpos+notchL + 1, 7, notchColour);
          tft.drawFastHLine(pos_centre_f -2 + 160/spectrum_span * notchF / 1000, notchpos+notchL + 2, 5, notchColour);
          tft.drawFastHLine(pos_centre_f -1 + 160/spectrum_span * notchF / 1000, notchpos+notchL + 3, 3, notchColour);
          tft.drawFastVLine(pos_centre_f + 160/spectrum_span * notchF / 1000, notchpos+notchL + 4, 2, notchColour);
          }
          break;
      }
      }
      oldnotchF = notchF;
      pos_centre_f-=1; // = pos_centre_f - 1;
  } // end void show_notch

float deemphasis_wfm_ff (float* input, float* output, int input_size, int sample_rate, float last_output)
{
  /* taken from libcsdr
    typical time constant (tau) values:
    WFM transmission in USA: 75 us -> tau = 75e-6
    WFM transmission in EU:  50 us -> tau = 50e-6
    More info at: http://www.cliftonlaboratories.com/fm_receivers_and_de-emphasis.htm
    Simulate in octave: tau=75e-6; dt=1/48000; alpha = dt/(tau+dt); freqz([alpha],[1 -(1-alpha)])
  */
//  if(is_nan(last_output)) last_output=0.0; //if last_output is NaN
  output[0]=deemp_alpha*input[0]+(onem_deemp_alpha)*last_output;
  for (int i=1;i<input_size;i++) //@deemphasis_wfm_ff
       output[i]=deemp_alpha*input[i]+(onem_deemp_alpha)*output[i-1]; //this is the simplest IIR LPF
    return output[input_size-1];
}
