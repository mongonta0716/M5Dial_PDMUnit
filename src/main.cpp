#include <Arduino.h>
#include <M5Unified.h>
#include <SPIFFS.h>

#define SAMPLE_RATE 16000
#define CHANNELS 1
#define BUFFER_SIZE 1024
#define BIT_DEPTH 16

// WAVファイルヘッダー構造体
struct WAVHeader {
  // RIFFチャンク
  char riffHeader[4] = {'R', 'I', 'F', 'F'};
  uint32_t wavSize;        // ファイルサイズ - 8
  char waveHeader[4] = {'W', 'A', 'V', 'E'};
  
  // fmtチャンク
  char fmtHeader[4] = {'f', 'm', 't', ' '};
  uint32_t fmtChunkSize = 16;
  uint16_t audioFormat = 1;  // PCM = 1
  uint16_t numChannels;      // チャンネル数
  uint32_t sampleRate;       // サンプルレート
  uint32_t byteRate;         // サンプルレート * チャンネル数 * ビット深度 / 8
  uint16_t blockAlign;       // チャンネル数 * ビット深度 / 8
  uint16_t bitsPerSample;    // ビット深度
  
  // dataチャンク
  char dataHeader[4] = {'d', 'a', 't', 'a'};
  uint32_t dataSize;         // 音声データのサイズ
};

// 録音関連の変数
File wavFile;
char wavFileName[32];
int fileCounter = 0;
uint32_t recordStartTime = 0;
bool isRecording = false;
WAVHeader wavHeader;
uint32_t bytesWritten = 0;
int16_t audioBuffer[BUFFER_SIZE];

// WAVファイルヘッダーを初期化する関数
void initWAVHeader() {
  wavHeader.numChannels = CHANNELS;
  wavHeader.sampleRate = SAMPLE_RATE;
  wavHeader.bitsPerSample = BIT_DEPTH;
  wavHeader.byteRate = SAMPLE_RATE * CHANNELS * (wavHeader.bitsPerSample / 8);
  wavHeader.blockAlign = CHANNELS * (wavHeader.bitsPerSample / 8);
  wavHeader.dataSize = 0; // 初期化時は0
  wavHeader.wavSize = 36; // 初期化時はヘッダーサイズのみ
}

// 録音ファイル名を生成する関数
void getNextFileName() {
  do {
    sprintf(wavFileName, "/recording_%03d.wav", fileCounter++);
  } while (SPIFFS.exists(wavFileName));
}

// 録音を開始する関数
void startRecording() {
  getNextFileName();
  
  // WAVファイルを作成
  wavFile = SPIFFS.open(wavFileName, FILE_WRITE);
  if (!wavFile) {
    M5.Display.println("ファイル作成に失敗しました");
    return;
  }
  
  // WAVヘッダーを初期化して書き込む
  initWAVHeader();
  wavFile.write((uint8_t*)&wavHeader, sizeof(WAVHeader));
  bytesWritten = 0;
  
  // M5.Micを開始
  M5.Mic.begin();
  
  isRecording = true;
  recordStartTime = millis();
  
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(0, 0);
  M5.Display.println("録音中...");
  M5.Display.println(wavFileName);
  M5.Display.println("\n停止するにはボタンを押してください");
}

// 録音を停止する関数
void stopRecording() {
  if (isRecording) {
    // 録音停止
    M5.Mic.end();
    isRecording = false;
    
    // WAVファイルのヘッダーを更新
    wavHeader.dataSize = bytesWritten;
    wavHeader.wavSize = 36 + bytesWritten;
    
    wavFile.seek(0);
    wavFile.write((uint8_t*)&wavHeader, sizeof(WAVHeader));
    wavFile.close();
    
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("録音完了!");
    M5.Display.println("録音時間: " + String((millis() - recordStartTime) / 1000) + "秒");
    M5.Display.println("ファイル: " + String(wavFileName));
    M5.Display.println("\n再度録音するにはボタンを押してください");
  }
}

// M5.Micからデータを取得してWAVファイルに書き込む関数
void processAudio() {
  if (!isRecording) return;
  
  // M5.Micからデータを読み取る
  size_t len = M5.Mic.record(audioBuffer, BUFFER_SIZE);
  
  if (len > 0) {
    // WAVファイルにデータを書き込む
    size_t bytesPerSample = sizeof(int16_t);
    wavFile.write((uint8_t*)audioBuffer, len * bytesPerSample);
    bytesWritten += len * bytesPerSample;
    
    // 録音中の視覚的フィードバック（録音レベルメーター）
    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate > 100) { // 100msごとに更新
      lastUpdate = millis();
      
      // 録音時間の表示を更新
      M5.Display.fillRect(0, 50, 320, 20, TFT_BLACK);
      M5.Display.setCursor(0, 50);
      M5.Display.print("時間: " + String((millis() - recordStartTime) / 1000) + "秒");
      
      // オーディオレベルの簡易表示
      int16_t maxLevel = 0;
      for (int i = 0; i < len; i++) {
        int16_t absValue = abs(audioBuffer[i]);
        if (absValue > maxLevel) maxLevel = absValue;
      }
      int levelWidth = map(maxLevel, 0, 32768, 0, M5.Display.width());
      M5.Display.fillRect(0, 80, M5.Display.width(), 20, TFT_BLACK);
      M5.Display.fillRect(0, 80, levelWidth, 20, TFT_GREEN);
    }
  }
}

void setup() {
  // M5Stackの初期化
  auto cfg = M5.config();
  cfg.internal_mic = true;  // 内蔵マイクを有効化
  cfg.internal_spk = true;  // 内蔵スピーカーを有効化
  M5.begin(cfg);
  
  M5.Display.setTextSize(2);
  M5.Display.println("M5.Mic録音アプリ");
  M5.Display.println("初期化中...");

  // M5.Micの設定
  auto mic_cfg = M5.Mic.config();
  mic_cfg.sample_rate = SAMPLE_RATE;
  mic_cfg.stereo = (CHANNELS == 2);
  mic_cfg.pin_ws = 15;
  mic_cfg.pin_bck = 13;
  M5.Mic.config(mic_cfg);

  // SPIFFSの初期化
  if (!SPIFFS.begin(true)) {
    M5.Display.println("SPIFFSの初期化に失敗しました");
    M5.Display.println("リセットしてください");
    while (1) { delay(100); }
  }

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("準備完了!");
  M5.Display.println("録音を開始するには");
  M5.Display.println("ボタンを押してください");
}

void loop() {
  M5.update();

  // ボタンが押されたら録音開始/停止
  if (M5.BtnA.wasPressed() || (M5.Touch.getCount() && M5.Touch.getDetail(0).wasPressed())) {
    if (isRecording) {
      stopRecording();
    } else {
      startRecording();
    }
    delay(100); // デバウンス
  }

  // 録音中の処理
  processAudio();
}
