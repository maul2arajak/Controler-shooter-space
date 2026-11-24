/*
 * ============================================================================
 *  ESP32 SPACE SHOOTER CONSOLE ("PERANG ANGKASA" - FULL PIN SISI KIRI)
 * ============================================================================
 *    - OLED Display 4-Pin (SSD1306 128x64):
 *        * VCC -> VIN (5V, kiri bawah)
 *        * GND -> GND (kiri bawah)
 *        * SDA -> D27 (GPIO 27)
 *        * SCL -> D25 (GPIO 25)
 *    - 3x Push Button (INPUT_PULLUP, aktif LOW ke GND):
 *        * Kiri   -> D33 (GPIO 33)
 *        * Kanan  -> D32 (GPIO 32)
 *        * Tembak -> D14 (GPIO 14)  [juga untuk Mulai & Main Lagi]
 *    - 2x LED + 2x Resistor 220 - 330 Ohm (ke GND):
 *        * LED Hijau (Bermain)   -> D13 (GPIO 13)
 *        * LED Merah (Game Over) -> D12 (GPIO 12)
 *    - 1x Buzzer Alarm:
 *        * Pin (+) -> D26 
 *        * Pin (-) -> GND
 *
 * ============================================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===================== KONFIGURASI LAYAR OLED =====================
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDR      0x3C   // Sudah terkonfirmasi 0x3C
#define OLED_SDA       27     // Pin D27 (sisi kiri)
#define OLED_SCL       25     // Pin D25 (sisi kiri)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===================== KONFIGURASI PIN ESP32 (SISI KIRI) ==========
// 3 Tombol Kontrol (1 kaki ke pin ESP32, 1 kaki lagi ke GND)
#define BTN_LEFT       33     // Pin D33 (GPIO 33) - Gerak pesawat ke kiri
#define BTN_RIGHT      32     // Pin D32 (GPIO 32) - Gerak pesawat ke kanan
#define BTN_FIRE       14     // Pin D14 (GPIO 14) - Tembak / Mulai / Restart

// 2 LED Indikator Status (pakai resistor 220 - 330 Ohm seri ke GND)
#define LED_GREEN      13     // Pin D13 (GPIO 13) - Nyala saat mulai & bermain
#define LED_RED        12     // Pin D12 (GPIO 12) - Nyala saat kalah (Game Over)

// Buzzer Alarm
#define BUZZER_PIN     26     // Pin D26 (GPIO 26) - Pin positif (+) buzzer

// Konfigurasi PWM Buzzer (kompatibel ESP32 Core 2.x dan 3.x)
#define BUZZER_CH      0
#define BUZZER_RES     8
#define BUZZER_DUTY    100    // Volume tetap yang jelas (maks piezo = 128)

// ===================== PARAMETER GAME =============================
#define FRAME_MS           25     // ~40 FPS
#define PLAYER_Y           53
#define PLAYER_W           11
#define PLAYER_H           8
#define PLAYER_SPEED       3.2f
#define MAX_HP             3

#define MAX_PLAYER_BULLETS 6
#define MAX_ENEMIES        7
#define MAX_ENEMY_BULLETS  6
#define MAX_STARS          14

// ===================== TIPE DATA & STATE ==========================
enum GameState {
  STATE_TITLE,
  STATE_PLAYING,
  STATE_GAMEOVER
};

struct Bullet {
  float x, y;
  bool active;
};

struct Enemy {
  float x, y;
  float speed;
  uint8_t type;        // 0 = Pesawat Musuh (menembak), 1 = Meteor
  unsigned long nextShot;
  bool active;
};

struct Star {
  uint8_t x;
  float y;
  float speed;
};

// ===================== VARIABEL GLOBAL ============================
GameState gameState = STATE_TITLE;

float playerX = 58.0f;
int playerHP = MAX_HP;
int score = 0;
int highScore = 0;
unsigned long invulnUntil = 0;
unsigned long gameOverTime = 0;
unsigned long lastShotTime = 0;
unsigned long lastSpawnTime = 0;
unsigned long lastFrameTime = 0;

bool lastFireBtn = HIGH;

Bullet playerBullets[MAX_PLAYER_BULLETS];
Bullet enemyBullets[MAX_ENEMY_BULLETS];
Enemy enemies[MAX_ENEMIES];
Star stars[MAX_STARS];

// ===================== ENGINE SUARA NON-BLOCKING ==================
struct NoteStep {
  uint16_t freq;
  uint16_t durationMs;
};

const NoteStep* currentSound = nullptr;
uint8_t soundLen = 0;
uint8_t soundIdx = 0;
unsigned long noteEndTime = 0;

// Pola suara alarm & efek
const NoteStep SFX_HIT[] = {
  {1400, 90}, {0, 30}, {850, 90}, {0, 30}, {1400, 110}
};
const NoteStep SFX_GAMEOVER[] = {
  {950, 140}, {0, 40}, {650, 160}, {0, 40}, {380, 320}
};
const NoteStep SFX_SHOOT[] = {
  {1750, 32}
};
const NoteStep SFX_EXPLODE[] = {
  {460, 50}
};
const NoteStep SFX_START[] = {
  {800, 70}, {1250, 100}
};

// ===================== FUNGSI LED STATUS ==========================
void setLedPlaying() {
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_RED, LOW);
}

void setLedGameOver() {
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, HIGH);
}

// ===================== FUNGSI BUZZER ==============================
void buzzerInit() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcAttach(BUZZER_PIN, 2000, BUZZER_RES);
  ledcWrite(BUZZER_PIN, 0);
#else
  ledcSetup(BUZZER_CH, 2000, BUZZER_RES);
  ledcAttachPin(BUZZER_PIN, BUZZER_CH);
  ledcWrite(BUZZER_CH, 0);
#endif
}

void buzzerPlayTone(uint16_t freq) {
  if (freq == 0) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    ledcWrite(BUZZER_PIN, 0);
#else
    ledcWrite(BUZZER_CH, 0);
#endif
    return;
  }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWriteTone(BUZZER_PIN, freq);
  ledcWrite(BUZZER_PIN, BUZZER_DUTY);
#else
  ledcWriteTone(BUZZER_CH, freq);
  ledcWrite(BUZZER_CH, BUZZER_DUTY);
#endif
}

void triggerSound(const NoteStep* seq, uint8_t len, bool highPriority = false) {
  if (!highPriority && currentSound != nullptr &&
      (currentSound == SFX_HIT || currentSound == SFX_GAMEOVER)) {
    return;
  }
  currentSound = seq;
  soundLen = len;
  soundIdx = 0;
  noteEndTime = millis() + currentSound[0].durationMs;
  buzzerPlayTone(currentSound[0].freq);
}

void updateBuzzer() {
  if (currentSound == nullptr) return;

  unsigned long now = millis();
  if (now >= noteEndTime) {
    soundIdx++;
    if (soundIdx >= soundLen) {
      currentSound = nullptr;
      buzzerPlayTone(0);
    } else {
      noteEndTime = now + currentSound[soundIdx].durationMs;
      buzzerPlayTone(currentSound[soundIdx].freq);
    }
  }
}

// ===================== INISIALISASI GAME ==========================
void initStars() {
  for (int i = 0; i < MAX_STARS; i++) {
    stars[i].x = random(0, SCREEN_WIDTH);
    stars[i].y = random(11, SCREEN_HEIGHT);
    stars[i].speed = (i % 3 == 0) ? 1.3f : 0.6f;
  }
}

void resetGame() {
  playerX = (SCREEN_WIDTH - PLAYER_W) / 2.0f;
  playerHP = MAX_HP;
  score = 0;
  invulnUntil = 0;
  lastSpawnTime = millis();

  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) playerBullets[i].active = false;
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++)  enemyBullets[i].active = false;
  for (int i = 0; i < MAX_ENEMIES; i++)        enemies[i].active = false;

  gameState = STATE_PLAYING;
  setLedPlaying();  // LED HIJAU nyala, LED MERAH mati
  triggerSound(SFX_START, 2, true);
}

// ===================== LOGIKA GAMEPLAY ============================
void spawnPlayerBullet() {
  unsigned long now = millis();
  if (now - lastShotTime < 180) return;

  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (!playerBullets[i].active) {
      playerBullets[i].x = playerX + (PLAYER_W / 2.0f);
      playerBullets[i].y = PLAYER_Y - 2;
      playerBullets[i].active = true;
      lastShotTime = now;
      triggerSound(SFX_SHOOT, 1);
      break;
    }
  }
}

void spawnEnemyBullet(float ex, float ey) {
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!enemyBullets[i].active) {
      enemyBullets[i].x = ex + 4;
      enemyBullets[i].y = ey + 8;
      enemyBullets[i].active = true;
      break;
    }
  }
}

void spawnEnemy() {
  unsigned long now = millis();
  unsigned long spawnInterval = max(420UL, 1150UL - (unsigned long)(score * 5));
  if (now - lastSpawnTime < spawnInterval) return;

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) {
      enemies[i].x = random(4, SCREEN_WIDTH - 13);
      enemies[i].y = 10.0f;
      enemies[i].type = (random(0, 10) < 7) ? 0 : 1;  // 70% pesawat, 30% meteor
      float extraSpeed = min(1.4f, score * 0.01f);
      enemies[i].speed = (enemies[i].type == 0 ? 0.75f : 1.25f) + extraSpeed;
      enemies[i].nextShot = now + random(600, 1500);
      enemies[i].active = true;
      lastSpawnTime = now;
      break;
    }
  }
}

void damagePlayer() {
  unsigned long now = millis();
  if (now < invulnUntil) return;

  playerHP--;
  invulnUntil = now + 1200;  // Kebal sesaat 1.2 detik

  if (playerHP <= 0) {
    if (score > highScore) highScore = score;
    gameState = STATE_GAMEOVER;
    gameOverTime = now;
    setLedGameOver();  // Kalah -> LED MERAH nyala, LED HIJAU mati
    triggerSound(SFX_GAMEOVER, 5, true);
  } else {
    // Kapal ketembak / tertabrak -> bunyikan alarm buzzer!
    triggerSound(SFX_HIT, 5, true);
  }
}

void updateStars() {
  for (int i = 0; i < MAX_STARS; i++) {
    stars[i].y += stars[i].speed;
    if (stars[i].y >= SCREEN_HEIGHT) {
      stars[i].y = 11;
      stars[i].x = random(0, SCREEN_WIDTH);
    }
  }
}

void updateGameplay() {
  unsigned long now = millis();

  // Gerak pesawat ke kiri & kanan
  if (digitalRead(BTN_LEFT) == LOW)  playerX -= PLAYER_SPEED;
  if (digitalRead(BTN_RIGHT) == LOW) playerX += PLAYER_SPEED;
  if (playerX < 1) playerX = 1;
  if (playerX > SCREEN_WIDTH - PLAYER_W - 1) playerX = SCREEN_WIDTH - PLAYER_W - 1;

  // Tembak laser ke atas
  if (digitalRead(BTN_FIRE) == LOW) {
    spawnPlayerBullet();
  }

  updateStars();
  spawnEnemy();

  // Update peluru pemain (bergerak ke atas)
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (!playerBullets[i].active) continue;
    playerBullets[i].y -= 3.8f;
    if (playerBullets[i].y < 10) {
      playerBullets[i].active = false;
    }
  }

  // Update peluru musuh (bergerak ke bawah)
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!enemyBullets[i].active) continue;
    enemyBullets[i].y += 2.4f;
    if (enemyBullets[i].y > SCREEN_HEIGHT) {
      enemyBullets[i].active = false;
      continue;
    }

    // Cek kena pesawat pemain
    if (enemyBullets[i].x >= playerX && enemyBullets[i].x <= playerX + PLAYER_W &&
        enemyBullets[i].y >= PLAYER_Y && enemyBullets[i].y <= PLAYER_Y + PLAYER_H) {
      enemyBullets[i].active = false;
      damagePlayer();
      if (gameState == STATE_GAMEOVER) return;
    }
  }

  // Update musuh (turun dari atas ke bawah)
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    enemies[i].y += enemies[i].speed;

    // Pesawat musuh menembak ke bawah
    if (enemies[i].type == 0 && enemies[i].y > 14 &&
        enemies[i].y < PLAYER_Y - 14 && now >= enemies[i].nextShot) {
      spawnEnemyBullet(enemies[i].x, enemies[i].y);
      enemies[i].nextShot = now + random(1000, 2100);
    }

    if (enemies[i].y > SCREEN_HEIGHT) {
      enemies[i].active = false;
      continue;
    }

    // Cek kena peluru pemain
    bool hitByBullet = false;
    for (int b = 0; b < MAX_PLAYER_BULLETS; b++) {
      if (!playerBullets[b].active) continue;
      if (playerBullets[b].x >= enemies[i].x - 1 && playerBullets[b].x <= enemies[i].x + 9 &&
          playerBullets[b].y >= enemies[i].y && playerBullets[b].y <= enemies[i].y + 8) {
        playerBullets[b].active = false;
        enemies[i].active = false;
        score += (enemies[i].type == 0) ? 10 : 15;
        triggerSound(SFX_EXPLODE, 1);
        hitByBullet = true;
        break;
      }
    }
    if (hitByBullet) continue;

    // Cek tabrakan langsung musuh dengan pesawat pemain
    if (enemies[i].x < playerX + PLAYER_W && enemies[i].x + 8 > playerX &&
        enemies[i].y < PLAYER_Y + PLAYER_H && enemies[i].y + 8 > PLAYER_Y) {
      enemies[i].active = false;
      damagePlayer();
      if (gameState == STATE_GAMEOVER) return;
    }
  }
}

// ===================== RENDER OLED ================================
void drawPlayerShip(int x, int y) {
  if (millis() < invulnUntil && ((millis() / 90) % 2 == 0)) return;

  // Pesawat menghadap ke atas
  display.drawTriangle(x + 5, y, x, y + 7, x + 10, y + 7, SSD1306_WHITE);
  display.drawLine(x + 5, y + 2, x + 5, y + 6, SSD1306_WHITE);
  // Api roket di bawah
  if ((millis() / 80) % 2 == 0) {
    display.drawPixel(x + 5, y + 9, SSD1306_WHITE);
  }
}

void drawEnemyShip(int x, int y, uint8_t type) {
  if (type == 0) {
    // Pesawat musuh menghadap ke bawah
    display.fillTriangle(x, y, x + 8, y, x + 4, y + 7, SSD1306_WHITE);
    display.drawPixel(x + 4, y + 2, SSD1306_BLACK);
  } else {
    // Meteor / asteroid
    display.drawCircle(x + 4, y + 4, 4, SSD1306_WHITE);
    display.drawPixel(x + 3, y + 3, SSD1306_WHITE);
    display.drawPixel(x + 5, y + 5, SSD1306_WHITE);
  }
}

void drawHUD() {
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  // Skor di kiri atas
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 1);
  display.print("SKOR:");
  display.print(score);

  // Nyawa (HP) di kanan atas
  for (int i = 0; i < MAX_HP; i++) {
    int hx = 98 + (i * 9);
    if (i < playerHP) {
      display.fillRect(hx, 2, 6, 5, SSD1306_WHITE);
    } else {
      display.drawRect(hx, 2, 6, 5, SSD1306_WHITE);
    }
  }
}

void renderScreen() {
  display.clearDisplay();

  if (gameState == STATE_TITLE) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(18, 8);
    display.println("PERANG ANGKASA");
    display.drawFastHLine(16, 18, 96, SSD1306_WHITE);
    display.setCursor(10, 26);
    display.println("KIRI/KANAN: Gerak");
    display.setCursor(10, 36);
    display.println("TEMBAK    : Laser");
    display.setCursor(12, 52);
    display.println("Tekan TEMBAK mulai!");
  }
  else if (gameState == STATE_PLAYING) {
    drawHUD();

    for (int i = 0; i < MAX_STARS; i++) {
      display.drawPixel(stars[i].x, (int)stars[i].y, SSD1306_WHITE);
    }

    drawPlayerShip((int)playerX, PLAYER_Y);

    for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
      if (playerBullets[i].active) {
        display.drawFastVLine((int)playerBullets[i].x, (int)playerBullets[i].y, 4, SSD1306_WHITE);
      }
    }

    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
      if (enemyBullets[i].active) {
        display.fillCircle((int)enemyBullets[i].x, (int)enemyBullets[i].y, 1, SSD1306_WHITE);
      }
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (enemies[i].active) {
        drawEnemyShip((int)enemies[i].x, (int)enemies[i].y, enemies[i].type);
      }
    }
  }
  else if (gameState == STATE_GAMEOVER) {
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 6);
    display.println("GAME OVER");

    display.setTextSize(1);
    display.setCursor(28, 28);
    display.print("Skor : ");
    display.println(score);
    display.setCursor(28, 38);
    display.print("Best : ");
    display.println(highScore);
    display.setCursor(10, 52);
    display.println("Tekan TEMBAK lagi!");
  }

  display.display();
}

// ===================== SETUP & LOOP ===============================
void setup() {
  Serial.begin(115200);

  // Setup 3 tombol (tanpa resistor eksternal)
  pinMode(BTN_LEFT,  INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_FIRE,  INPUT_PULLUP);

  // Setup LED Hijau & Merah
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED,   OUTPUT);
  setLedPlaying();  // Awal siap bermain -> LED HIJAU menyala

  // Setup Buzzer
  buzzerInit();

  // Setup OLED I2C pada pin sisi kiri (SDA=D27, SCL=D25)
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED SSD1306 tidak terdeteksi! Cek SDA(D27)/SCL(D25) & VCC(VIN)."));
    while (true) {
      digitalWrite(LED_RED, HIGH);
      delay(250);
      digitalWrite(LED_RED, LOW);
      delay(250);
    }
  }

  randomSeed(micros());
  initStars();
  renderScreen();
}

void loop() {
  updateBuzzer();

  bool fireNow = digitalRead(BTN_FIRE);
  bool firePressed = (lastFireBtn == HIGH && fireNow == LOW);
  lastFireBtn = fireNow;

  if (gameState == STATE_TITLE) {
    if (firePressed) {
      resetGame();  // Mulai -> LED Hijau nyala
    }
  } else if (gameState == STATE_GAMEOVER) {
    // Jeda 600ms setelah kalah supaya tidak tidak sengaja langsung restart
    if (firePressed && (millis() - gameOverTime > 600)) {
      resetGame();  // Kembali bermain -> LED Merah mati, LED Hijau nyala
    }
  }

  unsigned long now = millis();
  if (now - lastFrameTime >= FRAME_MS) {
    lastFrameTime = now;
    if (gameState == STATE_PLAYING) {
      updateGameplay();
    }
    renderScreen();
  }
}