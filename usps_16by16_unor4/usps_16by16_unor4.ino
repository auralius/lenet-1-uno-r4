/**
 * Auralius Manurung -- Universitas Telkom
 * auralius.manurung@ieee.org
 */
#include <SdFat.h>
SdFat SD;

#include <MCUFRIEND_kbv.h>
MCUFRIEND_kbv tft;  // hard-wiTFT_RED for UNO shields anyway.

#include <TouchScreen.h>
const uint16_t XP = 6, XM = A2, YP = A1, YM = 7;  //240x320 ID=0x9341
//const int XP = 8, YP = A3, XM = A2, YM = 9;
const int16_t TS_LEFT = 154, TS_RT = 902, TS_TOP = 184, TS_BOT = 919;
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
TSPoint tp;

#include "bmp.h"

#define SD_CS 10
#define MINPRESSURE 200
#define MAXPRESSURE 1000
#define PENRADIUS 1

const int16_t L = 96;
const int16_t L8 = 96 / 8;
const int16_t L16 = 96 / 16;

const int16_t W8 = 240 / 8;
const int16_t W16 = 240 / 16;
const int16_t W2 = 240 / 2;

// Writing area
byte ROI[8];  //xmin, ymin, xmax, ymax, width, length, center x, center y

// The discretized drawing area: 16x16 grids, max value of each grid is 255
byte GRID[16][16];
float *OUTPUT_BUFFER;

// Grayscale colors in 17 steps: 0 to 16
uint16_t GREYS[17];


// Grayscale of 16 steps from TFT_BLACK (0x0000) to TFT_GREEN(0xFFFF)
void create_greys() {
  for (int16_t k = 0; k < 16; k++)
    GREYS[k] = ((2 * k << 11) | (4 * k << 5) | (2 * k));
  GREYS[16] = 0xFFFF;
}


// Clear the grid data, set all to 0
void reset_grid() {
  for (int16_t i = 0; i < 16; i++) 
    for (int16_t j = 0; j < 16; j++) 
      GRID[i][j] = 0;

  // Reset ROI
  for (int16_t i = 0; i < 8; i++)
    ROI[i] = 0;

  ROI[0] = 250;  // xmin
  ROI[1] = 250;  // ymin
}


int16_t update_progress(int16_t p, uint16_t max = 240, uint16_t color = TFT_BLUE) {
  if (p == 0) {
    tft.fillRect(0, L + 1, 240, 3, TFT_BLACK);
    return 1;
  }

  p = p + 1;
  int16_t e = float(p) / (float)max * 240.0;
  tft.fillRect(0, L + 1, e, 3, color);
  return p;
}

float noodle_read_float(File &f) {
  char s[20];
  f.readBytesUntil('\n', (char *)s, sizeof(s));
  return atof(s);
}


void delete_file(char *fn) {
  //File f = SD.open(fn, FILE_WRITE);
  SD.remove(fn);
}


float * noodle_create_buffer(uint16_t size){
  return (float *)malloc(size);
}


void grid_to_file(char *fn, uint16_t n) {
  delete_file(fn);
  File fo = SD.open(fn, FILE_WRITE);
  for (int16_t i = 0; i < n; i++)
    for (int16_t j = 0; j < n; j++) {
      fo.print(GRID[j][i]); //transposed
      fo.println('\0');
    }
  fo.close();
}


float get_padded_x(int16_t i, int16_t j, int16_t W, int16_t P) {
  if ((i < P) || (j < P) || (i > (W - 1 + P)) || (j > (W - 1 + P)))
    return 0.0;

  return (float)GRID[j - P][i - P];
}


// Bias with ReLU
uint16_t noodle_do_bias(float * output, float bias, uint16_t n) {
  for (int16_t i = 0; i < n; i++) {
    for (int16_t j = 0; j < n; j++) {
      output[i * n + j] = output[i * n + j] + bias;
      if (output[i * n + j] < 0.0)
        output[i * n + j] = 0.0;
    }
  }
  return n;
}


// Do pooling and strore the result
uint16_t noodle_do_pooling(float *output, uint16_t W, uint16_t K, uint16_t S, char *fn) {
  uint16_t Wo = (W - K) / S + 1;
  delete_file(fn);
  File fo = SD.open(fn, FILE_WRITE);

  // Max Pooling
  for (int16_t i = 0; i < Wo; i++) {
    for (int16_t j = 0; j < Wo; j++) {
      float v = 0.0;
      for (int16_t k = 0; k < K; k++)
        for (int16_t l = 0; l < K; l++)
          if (v < output[(i * S + k) * W + (j * S + l)])
            v = output[(i * S + k) * W + (j * S + l)];
   
      output[i * Wo + j] = v;
   
      fo.print(output[i * Wo + j], 8);
      fo.println('\0');
    }
  }
  fo.close();

  return Wo;
}

// Input size is W x W.
// The kernel filter size is K x K.
// The padding is P (uniform and zero padding).
// The stride length is S
uint16_t noodle_do_convolution(float* kernel, uint16_t K, uint16_t W, float *output_buffer, uint16_t P, uint16_t S) {
  // Caclulate the output size
  uint16_t V = (W - K + 2 * P) / S + 1;

  for (int16_t i = 0; i < V; i++) {
    for (int16_t j = 0; j < V; j++) {
      float v = 0;
      for (int16_t k = 0; k < K; k++)
        for (int16_t l = 0; l < K; l++)
          v = v + kernel[k * K + l] * get_padded_x(i * S + k, j * S + l, W, P);
      output_buffer[i * V + j] = output_buffer[i * V + j] + v;
    }
  }

  return V;
}


// Load an (n x n)-grid from a file.
void noodle_grid_from_file(char *fn, uint16_t n, bool transposed=false) {
  File fi;
  fi = SD.open(fn, FILE_READ);

  for (uint16_t i = 0; i < n; i++)
    for (uint16_t j = 0; j < n; j++){
    if (transposed)
      GRID[j][i] = noodle_read_float(fi);
    else
      GRID[j][i] = noodle_read_float(fi);
    }

  fi.close();
}


// Load a square matrix from a file (K x K). 
// The matrix was previously stored linearly
void noodle_read_from_file(char *fn, float *buffer, uint16_t K, bool transposed=false) {
  File fi;
  fi = SD.open(fn, FILE_READ);

  for (uint16_t i = 0; i < K; i++)
    for (uint16_t j = 0; j < K; j++)
      if (!transposed)
        buffer[i * K + j] = noodle_read_float(fi);
      else
        buffer[j * K + i] = noodle_read_float(fi);
  fi.close();
}


void noodle_reset_buffer(float *buffer, uint16_t n) {
  for (uint16_t i = 0; i < n; i++)
      buffer[i] = 0.0;
}

uint16_t noodle_conv(float *output_buffer, uint16_t W, uint16_t n_inputs, uint16_t n_filters, char *in_fn, char *out_fn, char *weight_fn, char *bias_fn) {
  int16_t progress = 0;

  char i_fn[12];
  char o_fn[12];
  char w_fn[12];
  strcpy(i_fn, in_fn);
  strcpy(o_fn, out_fn);
  strcpy(w_fn, weight_fn);

  File fb, fo;
  float kernel[5][5];
  uint16_t V;

  fb = SD.open(bias_fn, FILE_READ);
  for (uint16_t O = 0; O < n_filters; O++) {
    noodle_reset_buffer(OUTPUT_BUFFER, 16*16);
    float bias = noodle_read_float(fb);
    for (uint16_t I = 0; I < n_inputs; I++) {
      i_fn[3] = I + 'a';
      w_fn[3] = I + 'a';
      w_fn[5] = O + 'a';
      noodle_grid_from_file(i_fn, W, true);
      noodle_read_from_file(w_fn, (float *)kernel, 5);
      V = noodle_do_convolution((float *)kernel, 5, W, output_buffer, 2, 1);

      progress = update_progress(progress, n_inputs * n_filters);
    }

    V = noodle_do_bias(output_buffer, bias, V);

    o_fn[3] = O + 'a';
    V = noodle_do_pooling(output_buffer, V, 2, 2, o_fn);
  }

  fb.close();

  return V;
}

// Flattening, from a several input files to output_buffer
uint16_t noodle_flat( float *output_buffer, char * in_fn, uint16_t V, uint16_t n_filters) {
  char i_fn[12];
  strcpy(i_fn, in_fn);
  
  File fi;

  noodle_reset_buffer(output_buffer, V * V * n_filters);
  for (uint16_t k = 0; k < n_filters; k++) {
    i_fn[3] = k + 'a';
    fi = SD.open(i_fn, FILE_READ);
    for (uint16_t i = 0; i < (V * V); i++)
      output_buffer[i * n_filters + k] = noodle_read_float(fi);
    fi.close();
  }
  return V * V * n_filters;
}

// From output_buffer to one out_fn
uint16_t noodle_fcn(float * output_buffer, uint16_t n_inputs, uint16_t n_outputs, char * out_fn, char *weight_fn, char * bias_fn) {
  int16_t progress = 0;

  File fw, fo, fb;
  fw = SD.open(weight_fn, FILE_READ);
  fb = SD.open(bias_fn, FILE_READ);

  delete_file(out_fn);
  fo = SD.open(out_fn, FILE_WRITE);

  for (uint16_t k = 0; k < n_outputs; k++) {
    float h = noodle_read_float(fb);
    for (uint16_t j = 0; j < n_inputs; j++)
      h = h + output_buffer[j] * noodle_read_float(fw);

    if (h < 0.0)
      h = 0.0;

    fo.print(h, 8);
    fo.println('\0');

    progress = update_progress(progress, n_outputs);
  }

  fo.close();
  fw.close();
  fb.close();
  Serial.println(n_outputs);
  return n_outputs;
}

// From one in_fn to output_buffer
uint16_t noodle_fcn(char * in_fn, uint16_t n_inputs, uint16_t n_outputs, float * output_buffer, char *weight_fn, char * bias_fn) {
  int16_t progress = 0;
  File fi, fw, fb;

  fw = SD.open(weight_fn, FILE_READ);
  fb = SD.open(bias_fn, FILE_READ);

  for (uint16_t j = 0; j < n_outputs; j++) {
    output_buffer[j] = noodle_read_float(fb);
    fi = SD.open(in_fn, FILE_READ);
    for (uint16_t k = 0; k < n_inputs; k++)
      output_buffer[j] = output_buffer[j] + noodle_read_float(fi) * noodle_read_float(fw);
    fi.close();

    progress = update_progress(progress, n_outputs);
  }

  fw.close();
  fb.close();
}


// Normalize the numbers in the grids such that they range from 0 to 16
void normalize_grid() {
  // find the maximum
  int16_t maxval = 0;
  for (int16_t i = 0; i < 16; i++)
    for (int16_t j = 0; j < 16; j++)
      if (GRID[i][j] > maxval)
        maxval = GRID[i][j];

  // normalize such that the maximum is 255.0
  for (int16_t i = 0; i < 16; i++)
    for (int16_t j = 0; j < 16; j++)
      GRID[i][j] = round((float)GRID[i][j] / (float)maxval * 255.0);  // round instead of floor!
}


// Draw the grids in the screen
// The fill-color or the gray-level of each grid is set based on its value
void area_setup() {
  tft.fillRect(0, 0, 240, 320 - W8, TFT_BLACK);

  for (int16_t i = 0; i < 16; i++)
    for (int16_t j = 0; j < 16; j++)
      tft.fillRect(i * L16, j * L16, L16, L16, GREYS[GRID[i][j] / 16]);

  tft.drawRect(0, 0, L, L, TFT_GREEN);

  tft.setTextSize(1);
  tft.setCursor(W2, 10);
  tft.print(F("LeNet-1 on UNO R4\n"));

  showBMP("logo.bmp", W2, 25);
}


// Track ROI, the specific area where the drawing occurs
void track_roi(int16_t xpos, int16_t ypos) {
  if (xpos - PENRADIUS < ROI[0])
    ROI[0] = xpos - PENRADIUS;
  if (xpos + PENRADIUS > ROI[2])
    ROI[2] = xpos + PENRADIUS;
  if (ypos - PENRADIUS < ROI[1])
    ROI[1] = ypos - PENRADIUS;
  if (ypos + PENRADIUS > ROI[3])
    ROI[3] = ypos + PENRADIUS;

  ROI[4] = ROI[2] - ROI[0];
  ROI[5] = ROI[3] - ROI[1];

  ROI[6] = ROI[0] + ROI[4] / 2;
  ROI[7] = ROI[1] + ROI[5] / 2;
}


// Draw the ROI as a TFT_RED rectangle
void draw_roi() {
  tft.drawRect(ROI[0], ROI[1], ROI[4], ROI[5], TFT_RED);
  tft.drawCircle(ROI[6], ROI[7], 2, TFT_RED);
}


// Draw the two button at the bottom of the screen
void draw_buttons(char *label1, char *label2) {
  // Add 2 buttons in the bottom: CLEAR and PREDICT
  tft.fillRect(0, 320 - W8, W2, W2, TFT_BLUE);
  tft.fillRect(W2, 320 - W8, W2, W2, TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(6, 320 - W8 + 6);
  tft.print(label1);
  tft.setCursor(W2 + 8, 320 - W8 + 6);
  tft.print(label2);
}


// Arduino setup function
void setup(void) {
  Serial.begin(9600);
  //while ( !Serial ) delay(2);

  create_greys();

  tft.reset();
  tft.begin(tft.readID());
  tft.fillScreen(TFT_BLACK);

  tft.setCursor(0, 10);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN);
  

  if (!SD.begin(SD_CS, 24000000)) {
    tft.println(F("cannot start SD"));
    while (1)
      ;
  }

  reset_grid();
  area_setup();
  draw_buttons("CLEAR", "PREDICT");

  OUTPUT_BUFFER = noodle_create_buffer(16*16);
}


void loop() {
  int16_t xpos, ypos;  //screen coordinates
  tp = ts.getPoint();  //tp.x, tp.y are ADC values

  // if sharing pins, you'll need to fix the directions of the touchscreen pins
  pinMode(XM, OUTPUT);
  pinMode(YP, OUTPUT);

  // we have some minimum pressure we consider 'valid'
  if (tp.z > MINPRESSURE && tp.z < MAXPRESSURE) {
    /// Map to your current pixel orientation
    xpos = map(tp.x, TS_LEFT, TS_RT, 0, tft.width());
    ypos = map(tp.y, TS_BOT, TS_TOP, 0, tft.height());

    // are we in drawing area ?
    if (((ypos - PENRADIUS) > 0) && ((ypos + PENRADIUS) < L) && ((xpos - PENRADIUS) > 0) && ((xpos + PENRADIUS) < L)) {
      tft.fillCircle(xpos, ypos, PENRADIUS, TFT_BLUE);
      track_roi(xpos, ypos);
    }

    // CLEAR?
    if ((ypos > tft.height() - W8) && (xpos < W2)) {
      reset_grid();
      area_setup();
    }

    // PREDICT?
    if ((ypos > tft.height() - W8) && (xpos > W2)) {
      draw_roi();

      for (byte i = 0; i < 16; i++) {
        for (byte j = 0; j < 16; j++) {
          for (byte k = 0; k < L16; k++) {
            for (byte l = 0; l < L16; l++) {
              int16_t x = i * L16 + k;
              int16_t y = j * L16 + l;

              uint16_t pixel = tft.readPixel(x, y);

              if (pixel == TFT_BLUE) {
                float a = (float)L;
                float s = a / (float)ROI[5];
                int16_t x_ = s * (float)(x - ROI[0]) + 0.5 * (a - s * (float)ROI[4]);  // Align to center (60,60)
                int16_t y_ = s * (float)(y - ROI[1]) + 0.5 * (a - s * (float)ROI[5]);

                if ((x_ >= 0) && (x_ < L) && (y_ >= 0) && (y_ < L)) {
                  tft.fillCircle(x, y, 1, TFT_RED);
                  GRID[x_ / L16][y_ / L16] = GRID[x_ / L16][y_ / L16] + 1;
                }
              }
            }
          }
        }
      }

      normalize_grid();
      area_setup();

      tft.setTextSize(1);
      tft.setCursor(0, W16 * 7);

      grid_to_file("i1-a.txt", 16);

      unsigned long st = micros();  // timer starts
      uint16_t V;

      tft.println(F("Conv layer 1 ..."));
      V = noodle_conv(OUTPUT_BUFFER, 16, 1, 12, "i1-x.txt", "o1-x.txt", "w1-x-x.txt", "w2.txt");

      tft.println(F("Conv layer 2 ..."));
      V = noodle_conv(OUTPUT_BUFFER, V, 12, 12, "o1-x.txt", "o2-x.txt", "w3-x-x.txt", "w4.txt");

      V = noodle_flat(OUTPUT_BUFFER, "o2-x.txt", V, 12);

      tft.println(F("NN layer 1 ..."));
      V = noodle_fcn(OUTPUT_BUFFER, V, 30, "o3.txt", "w5.txt", "w6.txt");

      tft.println(F("NN layer 2 ..."));
      V = noodle_fcn("o3.txt", V, 10, OUTPUT_BUFFER, "w7.txt", "w8.txt");



      float et = (float)(micros() - st) * 1e-6;  // timer stops

      summarize(et, OUTPUT_BUFFER);
      reset_grid();
    }
  }
}


void summarize(float et, float * output_buffer) {
  float *y = output_buffer;  // reuse global memory
  tft.setTextSize(1);
  tft.setCursor(0, W16 * 10);

  // Find the largest class
  uint16_t label = 0;
  float max_y = 0;

  for (uint16_t i = 0; i < 10; i++) {
    if (y[i] > max_y) {
      max_y = y[i];
      label = i;
    }
  }

  // Print the results
  for (uint16_t i = 0; i < 10; i++) {
    if (i == label)
      tft.setTextColor(TFT_RED);

    tft.print(i);
    tft.print(" : ");
    tft.println(y[i], 4);

    if (i == label)
      tft.setTextColor(TFT_GREEN);
  }

  tft.print(F("\n----------------------------------\nPrediction: "));
  tft.setTextColor(TFT_RED);
  tft.print(label);
  tft.setTextColor(TFT_GREEN);

  // Display the elapsed time
  tft.print(F(" -- in "));
  tft.print(et, 2);
  tft.println(F(" seconds."));
}
