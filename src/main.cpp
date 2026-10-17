#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <string.h>

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen touch(33, 36);
bool touchWasDown = false;
constexpr uint8_t BUZZER_PIN = 26;
constexpr uint8_t BUZZER_CHANNEL = 7;
uint8_t volumePercent = 50;
uint32_t speakerStopAt = 0;
bool speakerReady = false;
int16_t greenX;
int16_t greenY;
int16_t targetX;
int16_t targetY;
uint32_t lastGreenMove = 0;
uint8_t pandaAnimationFrame = 0;
uint32_t lastPandaAnimationAt = 0;
bool caught = false;
bool awaitingRestart = false;
bool gameWon = false;
enum class GameMode : uint8_t { Classic, Endless };
GameMode gameMode = GameMode::Classic;
bool inMainMenu = true;
uint32_t crumbsCollected = 0;
uint32_t endlessHighScore = 0;
uint32_t greenBoostUntil = 0;
bool greenFacingRight = true;
bool pandaBoostVisualActive = false;
enum class EnemyType : uint8_t { Bee, Fox, Skunk, Snake, Caveman };
constexpr uint8_t MAX_ENEMIES = 5;
constexpr int16_t ENEMY_MIN_DX = 36;
constexpr int16_t ENEMY_MIN_DY = 30;
constexpr int8_t ENEMY_TARGET_OFFSET_X[MAX_ENEMIES] = {0, -16, 14, 0, -10};
constexpr int8_t ENEMY_TARGET_OFFSET_Y[MAX_ENEMIES] = {0, 12, -14, 20, 0};
struct Enemy {
  int16_t x;
  int16_t y;
  uint32_t lastMove;
  uint32_t lastAnimationAt;
  uint32_t boostUntil;
  uint32_t specialUntil;
  uint32_t nextSpecialAt;
  uint8_t animationFrame;
  EnemyType type;
  bool specialActive;
  bool facingRight;
};
Enemy enemies[MAX_ENEMIES];
uint8_t enemyCount = 1;
constexpr uint8_t CRUMB_COUNT = 3;
struct Crumb {
  int16_t x;
  int16_t y;
  bool active;
};
Crumb crumbs[CRUMB_COUNT];
void placeCrumb(Crumb &crumb);
void drawMeadowPatch(int16_t x, int16_t y, int16_t width, int16_t height);

struct Wall {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
};
constexpr uint8_t WALL_COUNT = 3;
Wall walls[WALL_COUNT] = {
    {86, 42, 14, 66},
    {157, 116, 14, 74},
    {231, 38, 14, 62},
};
void randomizeWalls();
const uint16_t melodyNotes[] = {659, 784, 988, 784, 880, 698, 587, 523, 392};
const uint16_t melodyDurations[] = {95, 95, 170, 95, 130, 130, 150, 180, 340};
uint8_t melodyIndex = 0;
bool melodyNotePlaying = false;
uint32_t melodyNextAt = 0;
uint32_t lastWinDance = 0;
uint8_t winDanceFrame = 0;

struct TouchCalibration {
  uint16_t xLeft;
  uint16_t xRight;
  uint16_t yTop;
  uint16_t yBottom;
};

TouchCalibration touchCalibration;
Preferences preferences;

void stopSpeaker() {
  if (!speakerReady) {
    return;
  }
  ledcWriteTone(BUZZER_CHANNEL, 0);
  speakerStopAt = 0;
}

void playSpeakerTone(uint16_t frequency, uint16_t durationMs = 0) {
  if (!speakerReady) {
    return;
  }
  if (volumePercent == 0) {
    stopSpeaker();
    return;
  }

  if (ledcWriteTone(BUZZER_CHANNEL, frequency) == 0) {
    Serial.println("ERROR: Could not start speaker tone.");
    return;
  }

  const uint32_t duty = (511UL * volumePercent * volumePercent) / 10000;
  ledcWrite(BUZZER_CHANNEL, duty);
  speakerStopAt = durationMs ? millis() + durationMs : 0;
}

void updateSpeaker() {
  if (speakerStopAt != 0 && static_cast<int32_t>(millis() - speakerStopAt) >= 0) {
    stopSpeaker();
  }
}

int16_t spriteX(int16_t x, int16_t offset, bool facingRight) {
  return x + (facingRight ? offset : -offset);
}

void drawVolumeControls() {
  drawMeadowPatch(tft.width() - 126, 0, 126, 24);
  tft.drawRect(tft.width() - 126, 0, 126, 24, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(tft.width() - 116, 8);
  tft.print("-");
  tft.setCursor(tft.width() - 96, 8);
  tft.printf("%u%%", volumePercent);
  tft.setCursor(tft.width() - 40, 8);
  tft.print("+");
}

bool handleVolumeTouch(int16_t x, int16_t y) {
  if (y > 24) {
    return false;
  }

  if (x >= tft.width() - 126 && x <= tft.width() - 100) {
    volumePercent = volumePercent >= 10 ? volumePercent - 10 : 0;
  } else if (x >= tft.width() - 50 && x <= tft.width() - 24) {
    volumePercent = volumePercent <= 90 ? volumePercent + 10 : 100;
  } else {
    return false;
  }

  drawVolumeControls();
  preferences.putUChar("volume", volumePercent);
  Serial.printf("Volume: %u%%\n", volumePercent);
  playSpeakerTone(1000, 90);
  return true;
}

void drawPanda(int16_t x, int16_t y, uint8_t frame, bool facingRight,
               bool boosted) {
  const uint16_t black = tft.color565(20, 20, 24);
  const uint16_t white = TFT_WHITE;
  const uint16_t outline = tft.color565(155, 155, 165);
  const int8_t stride = frame % 2 == 0 ? -2 : 2;

  if (boosted) {
    const uint16_t glow = tft.color565(80, 230, 255);
    tft.drawCircle(x, y - 4, 17, glow);
    tft.drawFastHLine(spriteX(x, -25, facingRight), y - 7, 7, glow);
    tft.drawFastHLine(spriteX(x, -29, facingRight), y, 9, glow);
    tft.drawFastHLine(spriteX(x, -24, facingRight), y + 7, 6, glow);
    const int8_t sparkleOffset = frame % 2 == 0 ? 1 : 3;
    tft.fillCircle(spriteX(x, 13, facingRight), y - 16 - sparkleOffset, 2,
                   TFT_YELLOW);
  }

  tft.fillEllipse(x, y + 6, 9, 7, black);
  tft.fillEllipse(x, y + 5, 7, 6, white);
  tft.fillEllipse(x - 8, y + 3, 4, 5, black);
  tft.fillEllipse(x + 8, y + 3, 4, 5, black);
  tft.fillEllipse(x - 7, y - 12, 4, 4, black);
  tft.fillEllipse(x + 7, y - 12, 4, 4, black);
  tft.fillCircle(x - 7, y - 12, 2, tft.color565(245, 150, 160));
  tft.fillCircle(x + 7, y - 12, 2, tft.color565(245, 150, 160));
  tft.fillCircle(x, y - 5, 10, outline);
  tft.fillCircle(x, y - 5, 9, white);
  tft.fillEllipse(x - 4, y - 6, 3, 5, black);
  tft.fillEllipse(x + 4, y - 6, 3, 5, black);
  const int8_t gazeOffset = facingRight ? 1 : -1;
  tft.fillCircle(x - 4 + gazeOffset, y - 6, 1, white);
  tft.fillCircle(x + 4 + gazeOffset, y - 6, 1, white);
  tft.fillEllipse(x, y - 1, 4, 3, white);
  tft.fillCircle(x, y - 2, 2, black);
  tft.drawLine(x, y, x - 2, y + 1, black);
  tft.drawLine(x, y, x + 2, y + 1, black);
  const int16_t leftFootY = y + (frame % 2 == 0 ? 11 : 15);
  const int16_t rightFootY = y + (frame % 2 == 0 ? 15 : 11);
  tft.drawLine(spriteX(x, -6, facingRight), y + 8,
               spriteX(x, -9 + stride, facingRight), leftFootY, black);
  tft.drawLine(spriteX(x, 6, facingRight), y + 8,
               spriteX(x, 9 - stride, facingRight), rightFootY, black);
  tft.fillEllipse(spriteX(x, -9 + stride, facingRight), leftFootY, 5, 4, black);
  tft.fillEllipse(spriteX(x, 9 - stride, facingRight), rightFootY, 5, 4, black);
}

void drawBee(int16_t x, int16_t y, uint8_t frame, bool facingRight) {
  const uint16_t yellow = tft.color565(255, 205, 35);
  const uint16_t wing = tft.color565(190, 225, 255);
  const uint16_t black = TFT_BLACK;

  tft.fillEllipse(x - 2, y - 7 + (frame % 2), 5, 4, wing);
  tft.fillEllipse(x + 5, y - 7 - (frame % 2), 5, 4, wing);
  tft.drawLine(spriteX(x, -8, facingRight), y + 5,
               spriteX(x, -11, facingRight), y + 10, black);
  tft.drawLine(x, y + 6, spriteX(x, -1, facingRight), y + 11, black);
  tft.drawLine(spriteX(x, 7, facingRight), y + 3,
               spriteX(x, 9, facingRight), y + 9, black);
  tft.fillEllipse(x, y, 11, 7, yellow);
  tft.drawFastVLine(spriteX(x, -5, facingRight), y - 6, 13, black);
  tft.drawFastVLine(spriteX(x, 1, facingRight), y - 7, 15, black);
  tft.drawFastVLine(spriteX(x, 7, facingRight), y - 5, 11, black);
  tft.fillCircle(spriteX(x, 10, facingRight), y - 1, 5, yellow);
  tft.drawLine(spriteX(x, 9, facingRight), y - 5,
               spriteX(x, 7, facingRight), y - 10, black);
  tft.drawLine(spriteX(x, 12, facingRight), y - 5,
               spriteX(x, 14, facingRight), y - 10, black);
  tft.fillCircle(spriteX(x, 11, facingRight), y - 2, 1, black);
}

void drawFox(int16_t x, int16_t y, uint8_t frame, bool sprinting, bool facingRight) {
  const uint16_t orange = tft.color565(230, 105, 35);
  const uint16_t cream = tft.color565(255, 225, 175);
  const uint16_t black = tft.color565(45, 35, 30);
  const int8_t wag = frame % 2 == 0 ? -3 : 3;
  tft.fillEllipse(spriteX(x, -11, facingRight), y + wag, 9, 5, orange);
  tft.fillCircle(spriteX(x, -18, facingRight), y + wag - 1, 3, cream);
  tft.fillEllipse(x, y + 3, 10, 7, orange);
  tft.fillCircle(spriteX(x, 7, facingRight), y - 5, 8, orange);
  tft.fillTriangle(spriteX(x, 1, facingRight), y - 9,
                   spriteX(x, 3, facingRight), y - 19,
                   spriteX(x, 9, facingRight), y - 9, black);
  tft.fillTriangle(spriteX(x, 9, facingRight), y - 10,
                   spriteX(x, 15, facingRight), y - 18,
                   spriteX(x, 15, facingRight), y - 5, black);
  tft.fillCircle(spriteX(x, 12, facingRight), y - 4, 2, cream);
  tft.fillCircle(spriteX(x, 9, facingRight), y - 6, 1, black);
  tft.fillEllipse(spriteX(x, 13, facingRight), y - 1, 5, 3, cream);
  tft.fillCircle(spriteX(x, 17, facingRight), y - 2, 2, black);
  const int8_t stride = frame % 2 == 0 ? -2 : 2;
  tft.drawLine(spriteX(x, -5, facingRight), y + 7,
               spriteX(x, -7 + stride, facingRight), y + 13, black);
  tft.drawLine(spriteX(x, 4, facingRight), y + 7,
               spriteX(x, 6 - stride, facingRight), y + 13, black);
  if (sprinting) {
    tft.drawFastHLine(spriteX(x, -30, facingRight), y - 5, 7, cream);
    tft.drawFastHLine(spriteX(x, -33, facingRight), y + 1, 8, cream);
  }
}

void drawSkunk(int16_t x, int16_t y, uint8_t frame, bool cloudActive, bool facingRight) {
  const uint16_t black = tft.color565(35, 38, 45);
  const uint16_t white = tft.color565(240, 240, 225);
  const uint16_t pink = tft.color565(240, 135, 145);
  const int8_t tailLift = frame % 2 == 0 ? 0 : 3;
  tft.fillEllipse(spriteX(x, -8, facingRight), y - 6 - tailLift, 5, 12, black);
  tft.drawLine(spriteX(x, -9, facingRight), y - 15 - tailLift,
               spriteX(x, -7, facingRight), y - 3, white);
  tft.fillEllipse(x, y + 3, 11, 7, black);
  tft.fillCircle(spriteX(x, 7, facingRight), y - 5, 7, black);
  tft.drawLine(spriteX(x, 6, facingRight), y - 11,
               spriteX(x, 4, facingRight), y - 2, white);
  tft.fillTriangle(spriteX(x, 3, facingRight), y - 10,
                   spriteX(x, 1, facingRight), y - 17,
                   spriteX(x, 8, facingRight), y - 11, black);
  tft.fillTriangle(spriteX(x, 9, facingRight), y - 10,
                   spriteX(x, 13, facingRight), y - 16,
                   spriteX(x, 13, facingRight), y - 8, black);
  tft.fillCircle(spriteX(x, 13, facingRight), y - 3, 2, pink);
  tft.fillCircle(spriteX(x, 9, facingRight), y - 6, 1, white);
  tft.drawLine(spriteX(x, -5, facingRight), y + 8,
               spriteX(x, -7, facingRight), y + 13, black);
  tft.drawLine(spriteX(x, 5, facingRight), y + 8,
               spriteX(x, 7, facingRight), y + 13, black);
  if (cloudActive) {
    const uint16_t cloud = tft.color565(155, 185, 105);
    tft.fillCircle(spriteX(x, 16, facingRight), y - 17, 5, cloud);
    tft.fillCircle(spriteX(x, 22, facingRight), y - 20, 6, cloud);
    tft.fillCircle(spriteX(x, 27, facingRight), y - 14, 5, cloud);
  }
}

void drawSnake(int16_t x, int16_t y, uint8_t frame, bool lunging, bool facingRight) {
  const uint16_t green = tft.color565(65, 175, 90);
  const uint16_t lightGreen = tft.color565(145, 215, 115);
  const uint16_t darkGreen = tft.color565(35, 105, 60);
  const int8_t wiggle = frame % 2 == 0 ? -4 : 4;
  tft.fillCircle(spriteX(x, -12, facingRight), y + wiggle, 5, green);
  tft.fillCircle(spriteX(x, -5, facingRight), y - wiggle, 5, green);
  tft.fillCircle(spriteX(x, 2, facingRight), y + wiggle, 5, green);
  tft.fillCircle(spriteX(x, 9, facingRight), y - wiggle, 5, green);
  tft.fillCircle(spriteX(x, 15, facingRight), y - 2, 7, green);
  tft.fillCircle(spriteX(x, 13, facingRight), y - 4, 2, lightGreen);
  tft.fillCircle(spriteX(x, 14, facingRight), y - 4, 1, darkGreen);
  if (lunging || frame % 3 == 0) {
    tft.drawLine(spriteX(x, 20, facingRight), y,
                 spriteX(x, 24, facingRight), y + 2, tft.color565(240, 70, 90));
    tft.drawLine(spriteX(x, 24, facingRight), y + 2,
                 spriteX(x, 26, facingRight), y, tft.color565(240, 70, 90));
  }
}

void drawCaveman(int16_t x, int16_t y, uint8_t frame, bool facingRight) {
  const uint16_t skin = tft.color565(210, 155, 105);
  const uint16_t hide = tft.color565(115, 75, 45);
  const uint16_t hair = tft.color565(65, 45, 35);
  const uint16_t bone = tft.color565(230, 220, 185);
  const int8_t stride = frame % 2 == 0 ? -2 : 2;
  tft.fillEllipse(x, y + 3, 9, 8, hide);
  tft.fillCircle(x, y - 8, 8, skin);
  tft.fillEllipse(x, y - 13, 9, 5, hair);
  tft.fillCircle(x - 6, y - 9, 3, hair);
  tft.fillCircle(x + 6, y - 9, 3, hair);
  tft.fillCircle(x - 3, y - 8, 1, hair);
  tft.fillCircle(x + 3, y - 8, 1, hair);
  tft.fillCircle(spriteX(x, 4, facingRight), y - 8, 1, hair);
  tft.drawLine(spriteX(x, -4, facingRight), y + 7,
               spriteX(x, -6 + stride, facingRight), y + 14, hair);
  tft.drawLine(spriteX(x, 4, facingRight), y + 7,
               spriteX(x, 6 - stride, facingRight), y + 14, hair);
  tft.drawLine(spriteX(x, 7, facingRight), y - 1,
               spriteX(x, 13, facingRight), y - 8, skin);
  tft.drawLine(spriteX(x, 13, facingRight), y - 8,
               spriteX(x, 17, facingRight), y - 14, hair);
  tft.fillCircle(spriteX(x, 17, facingRight), y - 14, 3, bone);
  tft.drawLine(spriteX(x, -7, facingRight), y - 1,
               spriteX(x, -13, facingRight), y + 4, skin);
}

void drawEnemy(const Enemy &enemy) {
  const bool skunkCloudActive =
      enemy.type == EnemyType::Skunk &&
      static_cast<int32_t>(enemy.specialUntil - millis()) > 0;
  switch (enemy.type) {
  case EnemyType::Bee:
    drawBee(enemy.x, enemy.y, enemy.animationFrame, enemy.facingRight);
    break;
  case EnemyType::Fox:
    drawFox(enemy.x, enemy.y, enemy.animationFrame, enemy.specialActive,
            enemy.facingRight);
    break;
  case EnemyType::Skunk:
    drawSkunk(enemy.x, enemy.y, enemy.animationFrame, skunkCloudActive,
              enemy.facingRight);
    break;
  case EnemyType::Snake:
    drawSnake(enemy.x, enemy.y, enemy.animationFrame, enemy.specialActive,
              enemy.facingRight);
    break;
  case EnemyType::Caveman:
    drawCaveman(enemy.x, enemy.y, enemy.animationFrame, enemy.facingRight);
    break;
  }
}

void drawTarget() {
  tft.drawCircle(targetX, targetY, 7, TFT_YELLOW);
  tft.drawFastHLine(targetX - 10, targetY, 21, TFT_YELLOW);
  tft.drawFastVLine(targetX, targetY - 10, 21, TFT_YELLOW);
}

void drawWalls() {
  const uint16_t wallColor = tft.color565(110, 115, 130);
  const uint16_t edgeColor = tft.color565(180, 185, 200);
  for (const Wall &wall : walls) {
    tft.fillRect(wall.x, wall.y, wall.width, wall.height, wallColor);
    tft.drawRect(wall.x, wall.y, wall.width, wall.height, edgeColor);
  }
}

void drawCrumbs() {
  const uint16_t crust = tft.color565(205, 145, 65);
  const uint16_t crumbColor = tft.color565(255, 220, 135);
  for (const Crumb &crumb : crumbs) {
    if (!crumb.active) {
      continue;
    }
    tft.fillCircle(crumb.x, crumb.y, 5, crust);
    tft.fillCircle(crumb.x - 2, crumb.y - 1, 2, crumbColor);
    tft.fillCircle(crumb.x + 2, crumb.y + 1, 2, crumbColor);
  }
}

void drawCounter() {
  drawMeadowPatch(0, 0, 108, 23);
  tft.drawRect(0, 0, 108, 23, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(4, 7);
  if (gameMode == GameMode::Endless) {
    tft.printf("Bread: %lu", static_cast<unsigned long>(crumbsCollected));
  } else {
    tft.printf("Bread: %lu/21", static_cast<unsigned long>(crumbsCollected));
  }
  drawVolumeControls();
}

void drawBugs() {
  const bool boosted =
      static_cast<int32_t>(greenBoostUntil - millis()) > 0;
  drawPanda(greenX, greenY, pandaAnimationFrame, greenFacingRight, boosted);
  for (uint8_t i = 0; i < enemyCount; ++i) {
    drawEnemy(enemies[i]);
  }
}

void drawMeadowPatch(int16_t x, int16_t y, int16_t width, int16_t height) {
  const uint16_t grass = tft.color565(72, 150, 58);
  const uint16_t darkGrass = tft.color565(48, 126, 45);
  const uint16_t lightGrass = tft.color565(104, 178, 70);
  const int16_t left = max<int16_t>(0, x);
  const int16_t top = max<int16_t>(0, y);
  const int16_t right = min<int16_t>(tft.width(), x + width);
  const int16_t bottom = min<int16_t>(tft.height(), y + height);
  if (left >= right || top >= bottom) {
    return;
  }
  tft.fillRect(left, top, right - left, bottom - top, grass);

  const int16_t firstX = (left / 13) * 13;
  const int16_t firstY = (top / 17) * 17;
  for (int16_t bladeX = firstX; bladeX < right; bladeX += 13) {
    for (int16_t bladeY = firstY; bladeY < bottom; bladeY += 17) {
      if ((bladeX * 7 + bladeY * 11) % 3 == 0) {
        continue;
      }
      const int16_t offsetX = (bladeX + bladeY) % 5;
      const uint16_t color = (bladeX + bladeY) % 2 == 0 ? darkGrass : lightGrass;
      const int16_t stemX = bladeX + offsetX;
      if (stemX >= left && stemX < right && bladeY + 4 >= top &&
          bladeY + 4 < bottom && stemX - 2 >= left && bladeY >= top &&
          stemX + 2 < right && bladeY + 1 < bottom) {
        tft.drawLine(stemX, bladeY + 4, stemX - 2, bladeY, color);
        tft.drawLine(stemX, bladeY + 4, stemX + 2, bladeY + 1, color);
      }
    }
  }
}

void drawMenuOption(int16_t y, const char *label, uint16_t color) {
  tft.fillRoundRect(55, y, 210, 42, 8, color);
  tft.drawRoundRect(55, y, 210, 42, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, color);
  tft.setTextSize(2);
  const int16_t textWidth = strlen(label) * 12;
  tft.setCursor((tft.width() - textWidth) / 2, y + 13);
  tft.print(label);
}

void showMainMenu() {
  inMainMenu = true;
  caught = false;
  awaitingRestart = false;
  gameWon = false;
  stopSpeaker();
  drawMeadowPatch(0, 0, tft.width(), tft.height());
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(73, 34);
  tft.print("PANDA GAME");
  tft.setTextSize(1);
  tft.setCursor(113, 72);
  tft.print("Choose a mode");
  drawMenuOption(92, "CLASSIC", TFT_DARKGREEN);
  drawMenuOption(142, "ENDLESS", tft.color565(45, 95, 145));
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(91, 197);
  tft.printf("Endless best: %lu", static_cast<unsigned long>(endlessHighScore));
  drawVolumeControls();
}

void drawScene() {
  drawMeadowPatch(0, 0, tft.width(), tft.height());
  drawWalls();
  drawCrumbs();
  drawTarget();
  drawBugs();
  drawCounter();
}

void eraseBugAt(int16_t x, int16_t y) {
  drawMeadowPatch(x - 36, y - 32, 72, 64);
}

void resetGame(GameMode mode) {
  gameMode = mode;
  inMainMenu = false;
  greenX = 38;
  greenY = tft.height() / 2;
  enemyCount = 1;
  enemies[0] = {static_cast<int16_t>(tft.width() - 38),
                static_cast<int16_t>(tft.height() / 2), 0, 0, 0, 0, 0, 0,
                EnemyType::Bee, false, false};
  randomizeWalls();
  targetX = greenX;
  targetY = greenY;
  caught = false;
  awaitingRestart = false;
  gameWon = false;
  crumbsCollected = 0;
  greenBoostUntil = 0;
  greenFacingRight = true;
  pandaBoostVisualActive = false;
  pandaAnimationFrame = 0;
  lastPandaAnimationAt = 0;
  melodyIndex = 0;
  melodyNotePlaying = false;
  stopSpeaker();
  for (Crumb &crumb : crumbs) {
    crumb.active = false;
  }
  for (Crumb &crumb : crumbs) placeCrumb(crumb);
  drawScene();
}

void startCaughtMelody() {
  caught = true;
  inMainMenu = false;
  awaitingRestart = false;
  if (gameMode == GameMode::Endless && crumbsCollected > endlessHighScore) {
    endlessHighScore = crumbsCollected;
    preferences.putULong("endlessBest", endlessHighScore);
  }
  melodyIndex = 0;
  melodyNotePlaying = false;
  melodyNextAt = millis();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(65, tft.height() / 2 - 12);
  tft.print("Got You!");
}

void drawMenuButton() {
  tft.fillRoundRect(55, 155, 210, 60, 10, TFT_DARKGREEN);
  tft.drawRoundRect(55, 155, 210, 60, 10, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setTextSize(2);
  tft.setCursor(104, 176);
  tft.print("Menu");
}

void drawWinDance() {
  constexpr int16_t danceY = 105;
  const int8_t offsets[] = {0, -8, 0, 8};
  const uint8_t frame = winDanceFrame % 4;
  tft.fillRect(0, 70, tft.width(), 68, TFT_BLACK);

  const uint8_t totalCharacters = enemyCount + 1;
  for (uint8_t i = 0; i < totalCharacters; ++i) {
    const int16_t x = 28 + (tft.width() - 56) * i / (totalCharacters - 1);
    const int16_t y = danceY + offsets[(frame + i) % 4];
    if (i == 0) {
      drawPanda(x, y, frame, true, false);
    } else {
      Enemy dancer = enemies[i - 1];
      dancer.x = x;
      dancer.y = y;
      dancer.animationFrame = frame + i;
      dancer.specialUntil = 0;
      dancer.facingRight = x < tft.width() / 2;
      drawEnemy(dancer);
    }
  }
}

void updateWinDance() {
  const uint32_t now = millis();
  if (now - lastWinDance < 180) {
    return;
  }
  lastWinDance = now;
  ++winDanceFrame;
  drawWinDance();
}

void updateCaughtMelody() {
  if (awaitingRestart) {
    return;
  }

  const uint32_t now = millis();
  if (now < melodyNextAt) {
    return;
  }

  if (melodyNotePlaying) {
    stopSpeaker();
    melodyNotePlaying = false;
    ++melodyIndex;
    melodyNextAt = now + 35;
    return;
  }

  if (melodyIndex >= sizeof(melodyNotes) / sizeof(melodyNotes[0])) {
    stopSpeaker();
    awaitingRestart = true;
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(20, 20);
    tft.print("Got You!");
    if (gameMode == GameMode::Endless) {
      tft.setTextSize(2);
      tft.setCursor(65, 90);
      tft.printf("Score: %lu", static_cast<unsigned long>(crumbsCollected));
      tft.setCursor(48, 116);
      tft.printf("Best: %lu", static_cast<unsigned long>(endlessHighScore));
    }
    drawMenuButton();
    drawVolumeControls();
    return;
  }

  playSpeakerTone(melodyNotes[melodyIndex]);
  melodyNotePlaying = true;
  melodyNextAt = now + melodyDurations[melodyIndex];
}

int16_t stepToward(int16_t current, int16_t target) {
  return current + (target > current) - (target < current);
}

bool hitsWall(int16_t x, int16_t y) {
  constexpr int16_t bugRadiusX = 13;
  constexpr int16_t bugRadiusY = 10;
  for (const Wall &wall : walls) {
    if (x + bugRadiusX > wall.x && x - bugRadiusX < wall.x + wall.width &&
        y + bugRadiusY > wall.y && y - bugRadiusY < wall.y + wall.height) {
      return true;
    }
  }
  return false;
}

bool moveBugToward(int16_t &x, int16_t &y, int16_t destinationX, int16_t destinationY) {
  const int16_t nextX = stepToward(x, destinationX);
  const int16_t nextY = stepToward(y, destinationY);
  if ((nextX != x || nextY != y) && !hitsWall(nextX, nextY)) {
    x = nextX;
    y = nextY;
    return true;
  }

  if (nextX != x && !hitsWall(nextX, y)) {
    x = nextX;
    return true;
  }
  if (nextY != y && !hitsWall(x, nextY)) {
    y = nextY;
    return true;
  }

  const int16_t offsetX[] = {0, 0, -1, 1};
  const int16_t offsetY[] = {-1, 1, 0, 0};
  int32_t bestDistance = INT32_MAX;
  int16_t bestX = x;
  int16_t bestY = y;
  bool foundStep = false;
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t candidateX = x + offsetX[i];
    const int16_t candidateY = y + offsetY[i];
    if (hitsWall(candidateX, candidateY)) {
      continue;
    }
    const int32_t distance = abs(destinationX - candidateX) + abs(destinationY - candidateY);
    if (!foundStep || distance < bestDistance) {
      foundStep = true;
      bestDistance = distance;
      bestX = candidateX;
      bestY = candidateY;
    }
  }
  if (foundStep) {
    x = bestX;
    y = bestY;
    return true;
  }
  return false;
}

bool enemyWouldOverlap(uint8_t enemyIndex, int16_t x, int16_t y) {
  for (uint8_t i = 0; i < enemyCount; ++i) {
    if (i == enemyIndex) {
      continue;
    }
    if (abs(x - enemies[i].x) < ENEMY_MIN_DX &&
        abs(y - enemies[i].y) < ENEMY_MIN_DY) {
      return true;
    }
  }
  return false;
}

bool moveEnemyToward(uint8_t enemyIndex, int16_t destinationX, int16_t destinationY) {
  Enemy &enemy = enemies[enemyIndex];
  const int16_t nextX = stepToward(enemy.x, destinationX);
  const int16_t nextY = stepToward(enemy.y, destinationY);
  if ((nextX != enemy.x || nextY != enemy.y) && !hitsWall(nextX, nextY) &&
      !enemyWouldOverlap(enemyIndex, nextX, nextY)) {
    enemy.x = nextX;
    enemy.y = nextY;
    return true;
  }

  if (nextX != enemy.x && !hitsWall(nextX, enemy.y) &&
      !enemyWouldOverlap(enemyIndex, nextX, enemy.y)) {
    enemy.x = nextX;
    return true;
  }
  if (nextY != enemy.y && !hitsWall(enemy.x, nextY) &&
      !enemyWouldOverlap(enemyIndex, enemy.x, nextY)) {
    enemy.y = nextY;
    return true;
  }

  const int16_t offsetX[] = {0, 0, -1, 1};
  const int16_t offsetY[] = {-1, 1, 0, 0};
  int32_t bestDistance = INT32_MAX;
  int16_t bestX = enemy.x;
  int16_t bestY = enemy.y;
  bool foundStep = false;
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t candidateX = enemy.x + offsetX[i];
    const int16_t candidateY = enemy.y + offsetY[i];
    if (hitsWall(candidateX, candidateY) ||
        enemyWouldOverlap(enemyIndex, candidateX, candidateY)) {
      continue;
    }
    const int32_t distance =
        abs(destinationX - candidateX) + abs(destinationY - candidateY);
    if (!foundStep || distance < bestDistance) {
      foundStep = true;
      bestDistance = distance;
      bestX = candidateX;
      bestY = candidateY;
    }
  }
  if (foundStep) {
    enemy.x = bestX;
    enemy.y = bestY;
    return true;
  }
  return false;
}

bool findEnemyWaypoint(uint8_t enemyIndex, int16_t targetX, int16_t targetY,
                        int16_t &waypointX, int16_t &waypointY) {
  constexpr uint8_t columns = 36;
  constexpr uint8_t rows = 27;
  constexpr uint16_t nodeCount = columns * rows;
  static uint16_t queue[nodeCount];
  static int16_t parent[nodeCount];
  const Enemy &enemy = enemies[enemyIndex];
  const auto nodeX = [](uint16_t node) {
    return static_cast<int16_t>(20 + (node % columns) * 8);
  };
  const auto nodeY = [](uint16_t node) {
    return static_cast<int16_t>(20 + (node / columns) * 8);
  };
  const auto nearestNode = [](int16_t x, int16_t y) {
    const int16_t column = constrain((x - 20 + 4) / 8, 0, columns - 1);
    const int16_t row = constrain((y - 20 + 4) / 8, 0, rows - 1);
    return static_cast<uint16_t>(row * columns + column);
  };

  for (uint16_t i = 0; i < nodeCount; ++i) {
    parent[i] = -1;
  }

  uint16_t start = nearestNode(enemy.x, enemy.y);
  if (hitsWall(nodeX(start), nodeY(start))) {
    int32_t nearestDistance = INT32_MAX;
    for (uint16_t i = 0; i < nodeCount; ++i) {
      if (hitsWall(nodeX(i), nodeY(i))) continue;
      const int32_t dx = enemy.x - nodeX(i);
      const int32_t dy = enemy.y - nodeY(i);
      const int32_t distance = dx * dx + dy * dy;
      if (distance < nearestDistance) {
        nearestDistance = distance;
        start = i;
      }
    }
  }

  uint16_t goal = 0;
  int32_t nearestGoalDistance = INT32_MAX;
  for (uint16_t i = 0; i < nodeCount; ++i) {
    if (hitsWall(nodeX(i), nodeY(i))) continue;
    const int32_t dx = targetX - nodeX(i);
    const int32_t dy = targetY - nodeY(i);
    const int32_t distance = dx * dx + dy * dy;
    if (distance < nearestGoalDistance) {
      nearestGoalDistance = distance;
      goal = i;
    }
  }

  uint16_t readIndex = 0;
  uint16_t writeIndex = 0;
  queue[writeIndex++] = start;
  parent[start] = start;
  const int8_t columnOffsets[] = {0, 0, -1, 1};
  const int8_t rowOffsets[] = {
      static_cast<int8_t>(enemyIndex % 2 == 0 ? -1 : 1),
      static_cast<int8_t>(enemyIndex % 2 == 0 ? 1 : -1),
      0,
      0,
  };
  while (readIndex < writeIndex && parent[goal] == -1) {
    const uint16_t current = queue[readIndex++];
    const int16_t column = current % columns;
    const int16_t row = current / columns;
    for (uint8_t direction = 0; direction < 4; ++direction) {
      const int16_t nextColumn = column + columnOffsets[direction];
      const int16_t nextRow = row + rowOffsets[direction];
      if (nextColumn < 0 || nextColumn >= columns ||
          nextRow < 0 || nextRow >= rows) {
        continue;
      }
      const uint16_t next = nextRow * columns + nextColumn;
      if (parent[next] != -1 || hitsWall(nodeX(next), nodeY(next))) continue;
      parent[next] = current;
      queue[writeIndex++] = next;
    }
  }
  if (parent[goal] == -1) {
    return false;
  }

  uint16_t waypoint = goal;
  while (parent[waypoint] != start && waypoint != start) {
    waypoint = parent[waypoint];
  }
  waypointX = nodeX(waypoint);
  waypointY = nodeY(waypoint);
  return true;
}

bool enemiesCollided() {
  for (uint8_t i = 0; i < enemyCount; ++i) {
    const int16_t dx = greenX - enemies[i].x;
    const int16_t dy = greenY - enemies[i].y;
    if (dx * dx + dy * dy <= 25 * 25) return true;
  }
  return false;
}

bool pointHitsWall(int16_t x, int16_t y) {
  for (const Wall &wall : walls) {
    if (x >= wall.x - 16 && x <= wall.x + wall.width + 16 &&
        y >= wall.y - 16 && y <= wall.y + wall.height + 16) {
      return true;
    }
  }
  return false;
}

void randomizeWalls() {
  const Wall fallback[WALL_COUNT] = {
      {86, 42, 14, 66},
      {157, 116, 14, 74},
      {231, 38, 14, 62},
  };
  for (uint8_t index = 0; index < WALL_COUNT; ++index) {
    bool placed = false;
    for (uint8_t attempt = 0; attempt < 100; ++attempt) {
      const int16_t height = random(48, 76);
      const Wall candidate = {
          static_cast<int16_t>(random(50, tft.width() - 48)),
          static_cast<int16_t>(random(36, tft.height() - height - 18)),
          14,
          height,
      };
      if ((abs(candidate.x - greenX) < 48 &&
           abs(candidate.y + candidate.height / 2 - greenY) < 48) ||
          (abs(candidate.x - enemies[0].x) < 48 &&
           abs(candidate.y + candidate.height / 2 - enemies[0].y) < 48)) {
        continue;
      }

      bool overlapsExisting = false;
      for (uint8_t previous = 0; previous < index; ++previous) {
        const Wall &wall = walls[previous];
        if (candidate.x < wall.x + wall.width + 24 &&
            candidate.x + candidate.width + 24 > wall.x &&
            candidate.y < wall.y + wall.height + 24 &&
            candidate.y + candidate.height + 24 > wall.y) {
          overlapsExisting = true;
          break;
        }
      }
      if (overlapsExisting) continue;

      walls[index] = candidate;
      placed = true;
      break;
    }
    if (!placed) {
      walls[index] = fallback[index];
    }
  }
}

void spawnEnemy() {
  if (enemyCount >= MAX_ENEMIES) {
    return;
  }

  Enemy &enemy = enemies[enemyCount];
  bool placed = false;
  for (uint16_t attempt = 0; attempt < 160; ++attempt) {
    const int16_t x = random(24, tft.width() - 24);
    const int16_t y = random(34, tft.height() - 18);
    if (pointHitsWall(x, y) || abs(x - greenX) < 50 || abs(y - greenY) < 35) {
      continue;
    }
    bool overlapsBug = false;
    for (uint8_t i = 0; i < enemyCount; ++i) {
      if (abs(x - enemies[i].x) < ENEMY_MIN_DX &&
          abs(y - enemies[i].y) < ENEMY_MIN_DY) {
        overlapsBug = true;
        break;
      }
    }
    if (!overlapsBug) {
      enemy.x = x;
      enemy.y = y;
      placed = true;
      break;
    }
  }

  if (!placed) {
    Serial.println("No free space for a new bug.");
    return;
  }
  enemy.lastMove = millis();
  enemy.lastAnimationAt = enemy.lastMove;
  enemy.boostUntil = 0;
  enemy.specialUntil = 0;
  const uint32_t now = millis();
  switch (static_cast<EnemyType>(enemyCount)) {
  case EnemyType::Fox:
    enemy.nextSpecialAt = now + 1800;
    break;
  case EnemyType::Skunk:
    enemy.nextSpecialAt = now + 3200;
    break;
  case EnemyType::Snake:
    enemy.nextSpecialAt = now + 2400;
    break;
  default:
    enemy.nextSpecialAt = 0;
    break;
  }
  enemy.animationFrame = 0;
  enemy.type = static_cast<EnemyType>(enemyCount);
  enemy.specialActive = false;
  enemy.facingRight = enemy.x < greenX;
  ++enemyCount;
  Serial.printf("A new enemy joined the chase (%u).\n", enemyCount);
}

void placeCrumb(Crumb &crumb) {
  for (uint16_t attempt = 0; attempt < 160; ++attempt) {
    const int16_t x = random(18, tft.width() - 18);
    const int16_t y = random(34, tft.height() - 18);
    if (pointHitsWall(x, y) || abs(x - greenX) < 30 || abs(y - greenY) < 24) continue;
    bool tooCloseToRed = false;
    for (uint8_t i = 0; i < enemyCount; ++i) {
      if (abs(x - enemies[i].x) < 30 || abs(y - enemies[i].y) < 24) {
        tooCloseToRed = true;
        break;
      }
    }
    if (tooCloseToRed) continue;

    bool overlapsCrumb = false;
    for (const Crumb &other : crumbs) {
      if (&other != &crumb && other.active &&
          abs(x - other.x) < 18 && abs(y - other.y) < 18) {
        overlapsCrumb = true;
        break;
      }
    }
    if (!overlapsCrumb) {
      crumb.x = x;
      crumb.y = y;
      crumb.active = true;
      return;
    }
  }

  crumb.active = false;
}

void showWinScreen() {
  caught = true;
  gameWon = true;
  awaitingRestart = true;
  stopSpeaker();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(25, 20);
  tft.print("You Win!");
  drawWinDance();
  drawMenuButton();
  drawVolumeControls();
  lastWinDance = millis();
}

void playCrumbSound() {
  playSpeakerTone(180, 65);
}

void collectCrumbs(uint32_t now) {
  for (Crumb &crumb : crumbs) {
    if (!crumb.active) {
      continue;
    }

    if (abs(greenX - crumb.x) > 14 || abs(greenY - crumb.y) > 12) {
      continue;
    }

    crumb.active = false;
    const bool boostStillActive =
        static_cast<int32_t>(greenBoostUntil - now) > 0;
    greenBoostUntil =
        (boostStillActive ? greenBoostUntil : now) + 4000;
    if (crumbsCollected < UINT32_MAX) {
      ++crumbsCollected;
    }
    playCrumbSound();
    if (gameMode == GameMode::Endless) {
      Serial.printf("Bread collected: %lu\n",
                    static_cast<unsigned long>(crumbsCollected));
    } else {
      Serial.printf("Bread collected: %lu/21\n",
                    static_cast<unsigned long>(crumbsCollected));
    }

    if (gameMode == GameMode::Classic && crumbsCollected >= 21) {
      showWinScreen();
      return;
    }

    if (crumbsCollected % 4 == 0) {
      spawnEnemy();
    }

    placeCrumb(crumb);
    drawCrumbs();
    drawCounter();
    drawBugs();
  }
}

bool readTouchPoint(uint16_t &x, uint16_t &y) {
  if (!touch.touched()) {
    return false;
  }

  uint32_t sumX = 0;
  uint32_t sumY = 0;
  uint8_t samples = 0;
  for (uint8_t i = 0; i < 12; ++i) {
    if (!touch.touched()) {
      continue;
    }
    const TS_Point point = touch.getPoint();
    sumX += point.x;
    sumY += point.y;
    ++samples;
    delay(5);
  }

  if (samples == 0) {
    return false;
  }

  x = sumX / samples;
  y = sumY / samples;
  return true;
}

void waitForTouchRelease() {
  while (touch.touched()) {
    delay(10);
  }
  delay(150);
}

void drawCalibrationTarget(uint8_t index, int16_t x, int16_t y) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.printf("Touch-Calibration %u/4", index + 1);
  tft.drawCircle(x, y, 12, TFT_MAGENTA);
  tft.drawFastHLine(x - 18, y, 37, TFT_MAGENTA);
  tft.drawFastVLine(x, y - 18, 37, TFT_MAGENTA);
}

void calibrateTouch() {
  const int16_t targetX[] = {20, static_cast<int16_t>(tft.width() - 21), 20,
                             static_cast<int16_t>(tft.width() - 21)};
  const int16_t targetY[] = {20, 20, static_cast<int16_t>(tft.height() - 21),
                             static_cast<int16_t>(tft.height() - 21)};
  uint16_t measuredX[4];
  uint16_t measuredY[4];

  for (uint8_t i = 0; i < 4; ++i) {
    drawCalibrationTarget(i, targetX[i], targetY[i]);
    Serial.printf("Calibration %u/4: Touch the screen target and release.\n", i + 1);
    waitForTouchRelease();
    while (!readTouchPoint(measuredX[i], measuredY[i])) {
      delay(10);
    }
    waitForTouchRelease();
  }

  touchCalibration.xLeft = (measuredX[0] + measuredX[2]) / 2;
  touchCalibration.xRight = (measuredX[1] + measuredX[3]) / 2;
  touchCalibration.yTop = (measuredY[0] + measuredY[1]) / 2;
  touchCalibration.yBottom = (measuredY[2] + measuredY[3]) / 2;

  if (abs((int)touchCalibration.xRight - touchCalibration.xLeft) < 500 ||
      abs((int)touchCalibration.yBottom - touchCalibration.yTop) < 500) {
    Serial.println("Touch-Calibration failed: invalid measurement range. Please restart and touch targets again.");
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 20);
    tft.println("Calibration");
    tft.println("failed");
    while (true) {
      delay(1000);
    }
  }

  preferences.putBytes("touch", &touchCalibration, sizeof(touchCalibration));
  Serial.println("Touch-Calibration saved.");
}

int16_t mapTouchCoordinate(uint16_t raw, uint16_t rawStart, uint16_t rawEnd, int16_t screenSize) {
  const int32_t mapped = (static_cast<int32_t>(raw) - rawStart) * (screenSize - 1) /
                         (static_cast<int32_t>(rawEnd) - rawStart);
  return constrain(mapped, 0, screenSize - 1);
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  touchSPI.begin(25, 39, 32, 33);
  if (!touch.begin(touchSPI)) {
    Serial.println("Issue: XPT2046-Controller could not be initialized. Check wiring.");
  }
  touch.setRotation(1);
  preferences.begin("cyd-touch", false);
  volumePercent = constrain(preferences.getUChar("volume", 50), 0, 100);
  endlessHighScore = preferences.getULong("endlessBest", 0);
  if (ledcSetup(BUZZER_CHANNEL, 2000, 10) == 0) {
    Serial.println("Issue: Buzzer PWM could not be initialized.");
  } else {
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
    speakerReady = true;
    stopSpeaker();
  }

  if (preferences.getBytesLength("touch") == sizeof(touchCalibration)) {
    preferences.getBytes("touch", &touchCalibration, sizeof(touchCalibration));
    Serial.println("Previously saved touch calibration loaded. Send 'c' to perform new calibration.");
  } else {
    calibrateTouch();
  }

  Serial.println("Game ready: Touch the screen to control the green bug.");
  randomSeed(esp_random());
  showMainMenu();
}

void loop() {
  updateSpeaker();

  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 'c' || command == 'C') {
      calibrateTouch();
      if (inMainMenu) {
        showMainMenu();
      } else {
        drawScene();
      }
    }
  }

  if (inMainMenu) {
    const bool isTouching = touch.touched();
    if (isTouching && !touchWasDown) {
      const TS_Point point = touch.getPoint();
      const int16_t x = mapTouchCoordinate(point.x, touchCalibration.xLeft,
                                          touchCalibration.xRight, tft.width());
      const int16_t y = mapTouchCoordinate(point.y, touchCalibration.yTop,
                                          touchCalibration.yBottom, tft.height());
      if (!handleVolumeTouch(x, y) && x >= 55 && x <= 265) {
        if (y >= 92 && y <= 134) {
          resetGame(GameMode::Classic);
        } else if (y >= 142 && y <= 184) {
          resetGame(GameMode::Endless);
        }
      }
    }
    touchWasDown = isTouching;
    delay(5);
    return;
  }

  if (caught) {
    if (gameWon) {
      updateWinDance();
    } else {
      updateCaughtMelody();
    }
    const bool isTouching = touch.touched();
    if (awaitingRestart && isTouching && !touchWasDown) {
      const TS_Point point = touch.getPoint();
      const int16_t x = mapTouchCoordinate(point.x, touchCalibration.xLeft,
                                          touchCalibration.xRight, tft.width());
      const int16_t y = mapTouchCoordinate(point.y, touchCalibration.yTop,
                                          touchCalibration.yBottom, tft.height());
      if (!handleVolumeTouch(x, y) && x >= 55 && x <= 265 && y >= 155 && y <= 215) {
        showMainMenu();
      }
    }
    touchWasDown = isTouching;
    delay(5);
    return;
  }

  const bool isTouching = touch.touched();
  if (isTouching && !touchWasDown) {
    const TS_Point point = touch.getPoint();
    const int16_t x = mapTouchCoordinate(point.x, touchCalibration.xLeft,
                                        touchCalibration.xRight, tft.width());
    const int16_t y = mapTouchCoordinate(point.y, touchCalibration.yTop,
                                        touchCalibration.yBottom, tft.height());
    if (handleVolumeTouch(x, y)) {
      touchWasDown = isTouching;
      delay(5);
      return;
    }
    targetX = constrain(x, 20, tft.width() - 20);
    targetY = constrain(y, 20, tft.height() - 20);
    Serial.printf("Green Target: X=%d Y=%d Pressure=%d\n", targetX, targetY, point.z);
    drawScene();
  }
  touchWasDown = isTouching;

  const uint32_t now = millis();
  const int16_t oldGreenX = greenX;
  const int16_t oldGreenY = greenY;
  const uint8_t oldEnemyCount = enemyCount;
  int16_t oldEnemyX[MAX_ENEMIES];
  int16_t oldEnemyY[MAX_ENEMIES];
  for (uint8_t i = 0; i < oldEnemyCount; ++i) {
    oldEnemyX[i] = enemies[i].x;
    oldEnemyY[i] = enemies[i].y;
  }
  bool moved = false;
  const bool pandaBoostActive =
      static_cast<int32_t>(greenBoostUntil - now) > 0;
  if (pandaBoostActive != pandaBoostVisualActive) {
    pandaBoostVisualActive = pandaBoostActive;
    moved = true;
  }
  bool pandaInStinkCloud = false;
  for (uint8_t i = 0; i < enemyCount; ++i) {
    Enemy &enemy = enemies[i];
    const bool active = enemy.specialUntil != 0 &&
                        static_cast<int32_t>(enemy.specialUntil - now) > 0;
    if (enemy.specialActive != active) {
      enemy.specialActive = active;
      moved = true;
    }
    if (!active) {
      enemy.specialUntil = 0;
    }

    const int16_t dx = greenX - enemy.x;
    const int16_t dy = greenY - enemy.y;
    const int32_t distanceSquared = dx * dx + dy * dy;
    if (enemy.type == EnemyType::Skunk && active &&
        distanceSquared <= 48 * 48) {
      pandaInStinkCloud = true;
    }

    if (enemy.type == EnemyType::Fox || enemy.type == EnemyType::Skunk ||
        enemy.type == EnemyType::Snake) {
      const int16_t triggerDistance =
          enemy.type == EnemyType::Skunk ? 82 : 125;
      if (!active &&
          static_cast<int32_t>(now - enemy.nextSpecialAt) >= 0 &&
          distanceSquared <= triggerDistance * triggerDistance) {
        const uint32_t duration =
            enemy.type == EnemyType::Fox ? 650 :
            enemy.type == EnemyType::Skunk ? 1300 : 500;
        const uint32_t cooldown =
            enemy.type == EnemyType::Fox ? 3600 :
            enemy.type == EnemyType::Skunk ? 5200 : 3000;
        enemy.specialUntil = now + duration;
        enemy.nextSpecialAt = now + cooldown;
        enemy.specialActive = true;
        moved = true;
        if (enemy.type == EnemyType::Skunk && distanceSquared <= 48 * 48) {
          pandaInStinkCloud = true;
        }
      }
    }
  }

  const uint32_t greenInterval =
      pandaInStinkCloud ? 30 :
      static_cast<int32_t>(greenBoostUntil - now) > 0 ? 4 : 13;
  if (now - lastGreenMove >= greenInterval && (greenX != targetX || greenY != targetY)) {
    lastGreenMove = now;
    const int16_t previousGreenX = greenX;
    if (moveBugToward(greenX, greenY, targetX, targetY)) {
      if (greenX != previousGreenX) {
        greenFacingRight = greenX > previousGreenX;
      }
      if (now - lastPandaAnimationAt >= 110) {
        ++pandaAnimationFrame;
        lastPandaAnimationAt = now;
      }
      moved = true;
    }
  }
  for (uint8_t i = 0; i < enemyCount; ++i) {
    Enemy &enemy = enemies[i];
    uint32_t enemyInterval = 10 + i;
    switch (enemy.type) {
    case EnemyType::Bee:
      break;
    case EnemyType::Fox:
      enemyInterval = enemy.specialActive ? 7 : 10;
      break;
    case EnemyType::Skunk:
      enemyInterval = 13;
      break;
    case EnemyType::Snake:
      enemyInterval = enemy.specialActive ? 8 : 11;
      break;
    case EnemyType::Caveman:
      enemyInterval = 12;
      break;
    }
    enemyInterval = (enemyInterval * 125 + 32) / 64;
    if (static_cast<int32_t>(enemy.boostUntil - now) > 0) {
      enemyInterval = (enemyInterval * 4 + 2) / 5;
    }
    const int16_t enemyTargetX = constrain(
        greenX + ENEMY_TARGET_OFFSET_X[i], 20, tft.width() - 20);
    const int16_t enemyTargetY = constrain(
        greenY + ENEMY_TARGET_OFFSET_Y[i], 20, tft.height() - 20);
    if (now - enemy.lastMove >= enemyInterval &&
        (enemy.x != enemyTargetX || enemy.y != enemyTargetY)) {
      enemy.lastMove = now;
      const int16_t previousEnemyX = enemy.x;
      int16_t waypointX;
      int16_t waypointY;
      if (findEnemyWaypoint(i, enemyTargetX, enemyTargetY, waypointX, waypointY) &&
          moveEnemyToward(i, waypointX, waypointY)) {
        if (enemy.x != previousEnemyX) {
          enemy.facingRight = enemy.x > previousEnemyX;
        }
        if (now - enemy.lastAnimationAt >= 100) {
          ++enemy.animationFrame;
          enemy.lastAnimationAt = now;
        }
        moved = true;
      }
    }
  }

  collectCrumbs(now);
  if (caught) {
    delay(5);
    return;
  }

  if (moved) {
    eraseBugAt(oldGreenX, oldGreenY);
    for (uint8_t i = 0; i < oldEnemyCount; ++i) {
      eraseBugAt(oldEnemyX[i], oldEnemyY[i]);
    }
    if (enemyCount != oldEnemyCount) {
      drawScene();
    } else {
    drawWalls();
    drawCrumbs();
    drawTarget();
    drawBugs();
    drawCounter();
    }
    if (enemiesCollided()) {
      startCaughtMelody();
    }
  }
  delay(5);
}
