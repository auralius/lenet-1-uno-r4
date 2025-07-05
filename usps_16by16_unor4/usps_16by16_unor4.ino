/**
 * Auralius Manurung -- Universitas Telkom
 * auralius.manurung@ieee.org
 */
#include <SdFat.h>
#include <MCUFRIEND_kbv.h>
MCUFRIEND_kbv tft;  // hard-wiTFT_RED for UNO shields anyway.
#include <TouchScreen.h>

const int XP = 6, XM = A2, YP = A1, YM = 7;  //240x320 ID=0x9341
//const int XP = 8, YP = A3, XM = A2, YM = 9;
const int TS_LEFT = 154, TS_RT = 902, TS_TOP = 184, TS_BOT = 919;

TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
TSPoint tp;

SdFat SD;

#define SD_CS 10
#define MINPRESSURE 300
#define MAXPRESSURE 1000
#define PENRADIUS 1

const int16_t L = 96;
const int16_t L8 = 96 / 8;
const int16_t L16 = 96 / 16;

const int16_t W8 = 240 / 8;
const int16_t W16 = 240 / 16;
const int16_t W2 = 240 / 2;

byte ROI[8];  //xmin, ymin, xmax, ymax, width, length, center x, center y

// The discretized drawing area: 16x16 grids, max value of each grid is 255
float GRID[16][16];
float OUTPUT_GRID[16][16];
float y[10];

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
  for (int16_t i = 0; i < 16; i++){
    for (int16_t j = 0; j < 16; j++){
      GRID[i][j] = 0.0;
      OUTPUT_GRID[i][j] = 0.0;
    }
  }

  // Reset ROI
  for (int16_t i = 0; i < 8; i++)
    ROI[i] = 0;

  ROI[0] = 250;  // xmin
  ROI[1] = 250;  // ymin
}


float read_float(File &f) {
  char s[20];
  f.readBytesUntil('\n', (char *)s, sizeof(s));
  return atof(s);
}


void delete_file(char *fn) {
  File f = SD.open(fn, FILE_WRITE);
  f.remove();
}


float get_padded_x(int16_t i, int16_t j, int16_t W, int16_t P) {
  if ((i < P) || (j < P) || (i > (W - 1 + P)) || (j > (W - 1 + P)))
    return 0;

  return GRID[j - P][i - P];
}


void do_relu(uint16_t n){
  for (int16_t i = 0; i < n; i++) 
    for (int16_t j = 0; j < n; j++) 
      if (OUTPUT_GRID[i][j] < 0.0)
        OUTPUT_GRID[i][j] = 0.0;
} 


void do_pooling(uint16_t n){
  // Max Pooling
  float v = 0;
  for (int16_t i = 0; i < 8; i++) {
    for (int16_t j = 0; j < 8; j++) {
      v = 0.0;
      for (int16_t k = 0; k < 2; k++)
       for (int16_t l = 0; l < 2; l++)
         if (v < OUTPUT_GRID[i * 2 + k][j * 2 + l])
            v = OUTPUT_GRID[i * 2 + k][j * 2 + l];
      OUTPUT_GRID[i][j] = v;
    }
  }
}


void do_convolution(float kernel[5][5], float bias, uint16_t n) {
  // The kernel is always a 5x5-mtrix
  // Convolution
  for (int16_t i = 0; i < n; i++) {
    for (int16_t j = 0; j < n; j++) {
      float v = bias;
      for (int16_t k = 0; k < 5; k++)
        for (int16_t l = 0; l < 5; l++)
          v = v + kernel[k][l] *  get_padded_x(i + k, j + l, n, 2);
      OUTPUT_GRID[i][j] = OUTPUT_GRID[i][j] + v;
    }
  }
}


void grid_from_file(char* fn, uint16_t m, uint16_t n){
  File fi;
  fi = SD.open(fn, FILE_READ);
  
  for(uint16_t i=0; i<m; i++)
    for(uint16_t j=0; j<n; j++)
      GRID[j][i] = read_float(fi);

  fi.close();  
}


void reset_output_grid(){
  for(uint16_t i=0; i<16; i++)
    for(uint16_t j=0; j<16; j++)
      OUTPUT_GRID[i][j] = 0.0;
}


void C1() {
  float kernel[5][5];
  float bias[12];

  // 1 input, 12 outputs
  char w_fn[13] = "w1-a-x.txt\0\n";
  char b_fn[8] = "w2.txt";
  char o_fn[13] = "o1-x.txt\0\n";

  File fw, fb, fo;

  fb = SD.open(b_fn, FILE_READ);

  for (uint16_t k = 0; k < 12; k++) {
    reset_output_grid();

    w_fn[5] = k + 'a';
    o_fn[3] = k + 'a';

    // Get the bias
    bias[k] = read_float(fb);

    // Get the kernel matrix
    fw = SD.open(w_fn, FILE_READ);
    for (uint16_t i = 0; i < 5; i++)
      for (uint16_t j = 0; j < 5; j++)
        kernel[i][j] = read_float(fw);

    do_convolution(kernel, bias[k], 16);
    do_relu(16);
    do_pooling(8);

    // Write ouput to file  
    delete_file(o_fn);
    fo = SD.open(o_fn, FILE_WRITE);
    for (uint16_t i = 0; i < 8; i++) {
      for (uint16_t j = 0; j < 8; j++) {
        fo.print(OUTPUT_GRID[i][j], 9);
        fo.println('\0');
      }
    }

    fo.close();
    fw.close();
  }

  fb.close();
}


void C2() {
  float kernel[5][5];
  float bias[12];

  // 12 input, 12 outputs
  char i_fn[13] = "o1-x.txt\0\n";
  char w_fn[13] = "w3-x-x.txt\0\n";
  char b_fn[8]  = "w4.txt";
  char o_fn[13] = "o2-x.txt\0\n";

  File fw, fb, fo;

  fb = SD.open(b_fn, FILE_READ);
  for (uint16_t O = 0; O < 12; O++) {
    reset_output_grid();
    bias[O] = read_float(fb);
    
    for (uint16_t I = 0; I < 12; I++) {
      i_fn[3] = I + 'a'; 
      w_fn[3] = I + 'a';
      w_fn[5] = O + 'a';

      grid_from_file(i_fn, 8, 8);

      // Get the kernel matrix
      fw.open(w_fn, FILE_READ);
      for (uint16_t i = 0; i < 5; i++)
        for (uint16_t j = 0; j < 5; j++) {
          kernel[i][j] = read_float(fw);
        }
      fw.close();

      do_convolution(kernel, 0.0, 8);
    }


    for (uint16_t i = 0; i < 8; i++) 
      for (uint16_t j = 0; j < 8; j++) 
        OUTPUT_GRID[i][j] = OUTPUT_GRID[i][j] + bias[O];

    do_relu(8);
    do_pooling(4);

    // Write output to file
    o_fn[3] = O + 'a';
    delete_file(o_fn);
    fo = SD.open(o_fn, FILE_WRITE);
    for (uint16_t i = 0; i < 4; i++) 
      for (uint16_t j = 0; j < 4; j++) {
        fo.print(OUTPUT_GRID[i][j], 9);
        fo.println('\0');    
      }
    fo.close();
  }

  fb.close();
}


void NN0() {
  // Flattening
  char i_fn[13] = "o2-x.txt\0\n";
  File fi;

  reset_output_grid();
  for(uint16_t k=0; k<12; k++){
    i_fn[3] = k + 'a';
    fi = SD.open(i_fn, FILE_READ);
    for(uint16_t i=0; i<16; i++)
      OUTPUT_GRID[k][i] = read_float(fi);
    fi.close();
  }
}


void NN1() {
  File fw, fo, fb;
  fw = SD.open("w5.txt", FILE_READ);
  fb = SD.open("w6.txt", FILE_READ);
  delete_file("o3.txt");
  fo = SD.open("o3.txt", FILE_WRITE);
  
  for (uint16_t k=0; k<30; k++){
    float h = read_float(fb);
    for (uint16_t j=0; j<16; j++)
      for (uint16_t i=0; i<12; i++)
        h = h + OUTPUT_GRID[i][j] * read_float(fw);  
    
    if (h < 0.0)
      h = 0.0;
    
    fo.print(h, 9);
    fo.println('\0');  
  }

  fo.close();
  fw.close();
  fb.close();
}


void NN2() {
  File fi, fw, fb;
  
  fi = SD.open("o3.txt", FILE_READ);
  float x[30];
  for (uint16_t k=0; k<30; k++)
    x[k] = read_float(fi);
  fi.close();

  fw = SD.open("w7.txt", FILE_READ);
  fb = SD.open("w8.txt", FILE_READ);
  
  for (uint16_t j=0; j<10; j++){
    y[j] = read_float(fb);
    for (uint16_t k=0; k<30; k++)  
      y[j] = y[j] + x[k] * read_float(fw);  
  }

  fw.close();
  fb.close();
}


// Normalize the numbers in the grids such that they range from 0 to 16
void normalize_grid(int16_t n) {
  // find the maximum
  float maxval = 0;
  for (int16_t i = 0; i < n; i++)
    for (int16_t j = 0; j < n; j++)
      if (GRID[i][j] > maxval)
        maxval = GRID[i][j];

  // normalize such that the maximum is 255.0
  for (int16_t i = 0; i < n; i++)
    for (int16_t j = 0; j < n; j++)
      GRID[i][j] = GRID[i][j] / maxval * 255.0; 
}


// Draw the grids in the screen
// The fill-color or the gray-level of each grid is set based on its value
void area_setup() {
  tft.fillRect(0, 0, W2, 320 - W8, TFT_BLACK);

  for (int16_t i = 0; i < 16; i++)
    for (int16_t j = 0; j < 16; j++)
      tft.fillRect(i * L16, j * L16, L16, L16, GREYS[(uint16_t)GRID[i][j] / 16]);

  tft.drawRect(0, 0, L, L, TFT_GREEN);
}

void area_setup2() {
  for (int16_t i = 0; i < 8; i++)
    for (int16_t j = 0; j < 8; j++)
      tft.fillRect(i * L8, j * L8, L8, L8, GREYS[(uint16_t)OUTPUT_GRID[i][j] / 16]);

  tft.drawRect(0, 0, L, L, TFT_GREEN);
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
/*void draw_roi() {
  tft.drawRect(ROI[0], ROI[1], ROI[4], ROI[5], TFT_RED);
  tft.drawCircle(ROI[6], ROI[7], 2, TFT_RED);
}*/


// Draw the two button at the bottom of the screen
void draw_buttons(char *label1, char *label2) {
  tft.setTextSize(1);
  tft.setCursor(W2, 10);
  tft.print(F("LeNet-1 on UNO R4\n"));
  
  // Add 2 buttons in the bottom: CLEAR and PREDICT
  tft.fillRect(0, 320 - W8, W2, W2, TFT_BLUE);
  tft.fillRect(W2, 320 - W8, W2, W2, TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(6, 320 - W8 + 6);
  tft.print(label1);
  tft.setCursor(W2 + 8, 320 - W8 + 6);
  tft.print(label2);
}


// Put text at the bottom of the screen, right above the two buttons
/*void print_label(byte label) {
  tft.fillRect(L, 0, 240 - L, L, TFT_BLACK);
  tft.setCursor(W16 * 10, W16);
  tft.setTextSize(1);

  if (label == 255) {
    tft.print(F("Please wait!"));
  } else if (label == 254) {
    tft.print(F("Go ahead!"));
  } else {
    tft.print(F("Prediction:"));

    tft.setCursor(W16 * 11, W16 * 2);
    tft.setTextSize(4);

    char label_txt[8];
    itoa(label, label_txt, 10);
    tft.print(label);
  }
}*/


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

  if (!SD.begin(SD_CS)) {
    tft.println(F("cannot start SD"));
    while (1)
      ;
  }

  reset_grid();
  area_setup();
  //print_label(254);
  draw_buttons("CLEAR", "PREDICT");
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
      //print_label(254);
    }

    // PREDICT?
    if ((ypos > tft.height() - W8) && (xpos > W2)) {
      //print_label(255);
      //draw_roi();

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
                  tft.fillCircle(x_, y_, 1, TFT_GREEN);
                  GRID[x_ / L16][y_ / L16] = GRID[x_ / L16][y_ / L16] + 1;
                }
              }
            }
          }
        }
      }

      //delay(1000)
      normalize_grid(16);
      area_setup();
      
      tft.setTextSize(1);
      tft.setCursor(0, W16 * 7);
      
      unsigned long st = micros();  // timer starts
      tft.println(F("Conv layer 1..."));
      C1();
      
      tft.println(F("Conv layer 2..."));
      C2();

      tft.println(F("Flattening..."));
      NN0();

      tft.println(F("NN layer 1..."));
      NN1();

      tft.println(F("NN layer 2..."));
      NN2();
      
      float et = (float)(micros() - st) * 1e-6;  // timer stops
      
      summarize(et);
      reset_grid();
      
    }
  }
}

void summarize(float et){
  tft.setTextSize(1);
  tft.setCursor(0, W16 * 10);
  tft.print(F("\n-------------\n"));
  
  uint16_t label = 0;
  float max_y = 0;

  for (uint16_t i=0; i<10; i++){
    tft.print(i);
    tft.print(" : ");
    tft.println(y[i], 4);

    if(y[i] > max_y){
      max_y = y[i];
      label = i;
    }
  }
  
  tft.print(F("\n-------------\nPrediction: "));
  tft.print(label);

  // Display the elapsed time
  tft.print("\n\nIn ");
  tft.print(et, 2);
  tft.println(" s.");
}


