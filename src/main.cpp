#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>

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
uint16_t crumbsCollected = 0;
uint32_t greenBoostUntil = 0;
constexpr uint8_t MAX_RED_BUGS = 4;
constexpr int16_t RED_BUG_MIN_DX = 36;
constexpr int16_t RED_BUG_MIN_DY = 30;
constexpr int8_t RED_BUG_TARGET_OFFSET_X[MAX_RED_BUGS] = {0, -16, 14, 0};
constexpr int8_t RED_BUG_TARGET_OFFSET_Y[MAX_RED_BUGS] = {0, 12, -14, 20};
struct RedBug {
  int16_t x;
  int16_t y;
  uint32_t lastMove;
  uint32_t boostUntil;
};
RedBug redBugs[MAX_RED_BUGS];
uint8_t redBugCount = 1;
constexpr uint8_t CRUMB_COUNT = 3;
struct Crumb {
  int16_t x;
  int16_t y;
  bool active;
};
Crumb crumbs[CRUMB_COUNT];
void placeCrumb(Crumb &crumb);

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
    Serial.println("FEHLER: Lautsprecherton konnte nicht gestartet werden.");
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

void drawVolumeControls() {
  tft.fillRect(tft.width() - 126, 0, 126, 24, TFT_BLACK);
  tft.drawRoundRect(tft.width() - 124, 2, 24, 20, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(tft.width() - 116, 8);
  tft.print("-");
  tft.setCursor(tft.width() - 96, 8);
  tft.printf("%u%%", volumePercent);
  tft.drawRoundRect(tft.width() - 48, 2, 24, 20, 4, TFT_WHITE);
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
  Serial.printf("Lautstaerke: %u%%\n", volumePercent);
  playSpeakerTone(1000, 90);
  return true;
}

void drawPanda(int16_t x, int16_t y, uint8_t frame = 0) {
  const uint16_t black = tft.color565(20, 20, 24);
  const uint16_t white = TFT_WHITE;
  const uint16_t outline = tft.color565(155, 155, 165);
  const int8_t stride = frame % 2 == 0 ? -2 : 2;

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
  tft.fillCircle(x - 4, y - 6, 1, white);
  tft.fillCircle(x + 4, y - 6, 1, white);
  tft.fillEllipse(x, y - 1, 4, 3, white);
  tft.fillCircle(x, y - 2, 2, black);
  tft.drawLine(x, y, x - 2, y + 1, black);
  tft.drawLine(x, y, x + 2, y + 1, black);
  const int16_t leftFootY = y + (frame % 2 == 0 ? 11 : 15);
  const int16_t rightFootY = y + (frame % 2 == 0 ? 15 : 11);
  tft.drawLine(x - 6, y + 8, x - 9 + stride, leftFootY, black);
  tft.drawLine(x + 6, y + 8, x + 9 - stride, rightFootY, black);
  tft.fillEllipse(x - 9 + stride, leftFootY, 5, 4, black);
  tft.fillEllipse(x + 9 - stride, rightFootY, 5, 4, black);
}

void drawBee(int16_t x, int16_t y) {
  const uint16_t yellow = tft.color565(255, 205, 35);
  const uint16_t wing = tft.color565(190, 225, 255);
  const uint16_t black = TFT_BLACK;

  tft.fillEllipse(x - 2, y - 7, 5, 4, wing);
  tft.fillEllipse(x + 5, y - 7, 5, 4, wing);
  tft.drawLine(x - 8, y + 5, x - 11, y + 10, black);
  tft.drawLine(x, y + 6, x - 1, y + 11, black);
  tft.drawLine(x + 7, y + 3, x + 9, y + 9, black);
  tft.fillEllipse(x, y, 11, 7, yellow);
  tft.drawFastVLine(x - 5, y - 6, 13, black);
  tft.drawFastVLine(x + 1, y - 7, 15, black);
  tft.drawFastVLine(x + 7, y - 5, 11, black);
  tft.fillCircle(x + 10, y - 1, 5, yellow);
  tft.drawLine(x + 9, y - 5, x + 7, y - 10, black);
  tft.drawLine(x + 12, y - 5, x + 14, y - 10, black);
  tft.fillCircle(x + 11, y - 2, 1, black);
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
  tft.fillRect(0, 0, 90, 23, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(4, 7);
  tft.printf("Brot: %u/21", crumbsCollected);
  drawVolumeControls();
}

void drawBugs() {
  drawPanda(greenX, greenY, pandaAnimationFrame);
  for (uint8_t i = 0; i < redBugCount; ++i) {
    drawBee(redBugs[i].x, redBugs[i].y);
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

void drawScene() {
  drawMeadowPatch(0, 0, tft.width(), tft.height());
  drawWalls();
  drawCrumbs();
  drawTarget();
  drawBugs();
  drawCounter();
}

void eraseBugAt(int16_t x, int16_t y) {
  drawMeadowPatch(x - 23, y - 22, 47, 45);
}

void resetGame() {
  greenX = 38;
  greenY = tft.height() / 2;
  redBugCount = 1;
  redBugs[0] = {static_cast<int16_t>(tft.width() - 38),
                static_cast<int16_t>(tft.height() / 2), 0, 0};
  randomizeWalls();
  targetX = greenX;
  targetY = greenY;
  caught = false;
  awaitingRestart = false;
  gameWon = false;
  crumbsCollected = 0;
  greenBoostUntil = 0;
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
  awaitingRestart = false;
  melodyIndex = 0;
  melodyNotePlaying = false;
  melodyNextAt = millis();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(65, tft.height() / 2 - 12);
  tft.print("Got You!");
}

void drawRestartButton() {
  tft.fillRoundRect(55, 155, 210, 60, 10, TFT_DARKGREEN);
  tft.drawRoundRect(55, 155, 210, 60, 10, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setTextSize(2);
  tft.setCursor(76, 176);
  tft.print("New Game");
}

void drawWinDance() {
  constexpr int16_t danceY = 105;
  const int8_t offsets[] = {0, -8, 0, 8};
  const uint8_t frame = winDanceFrame % 4;
  tft.fillRect(0, 70, tft.width(), 68, TFT_BLACK);

  const uint8_t totalBugs = redBugCount + 1;
  for (uint8_t i = 0; i < totalBugs; ++i) {
    const int16_t x = 28 + (tft.width() - 56) * i / (totalBugs - 1);
    const int16_t y = danceY + offsets[(frame + i) % 4];
    if (i == 0) {
      drawPanda(x, y, frame);
    } else {
      drawBee(x, y);
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
    drawRestartButton();
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

bool redBugWouldOverlap(uint8_t bugIndex, int16_t x, int16_t y) {
  for (uint8_t i = 0; i < redBugCount; ++i) {
    if (i == bugIndex) {
      continue;
    }
    if (abs(x - redBugs[i].x) < RED_BUG_MIN_DX &&
        abs(y - redBugs[i].y) < RED_BUG_MIN_DY) {
      return true;
    }
  }
  return false;
}

bool moveRedBugToward(uint8_t bugIndex, int16_t destinationX, int16_t destinationY) {
  RedBug &bug = redBugs[bugIndex];
  const int16_t nextX = stepToward(bug.x, destinationX);
  const int16_t nextY = stepToward(bug.y, destinationY);
  if ((nextX != bug.x || nextY != bug.y) && !hitsWall(nextX, nextY) &&
      !redBugWouldOverlap(bugIndex, nextX, nextY)) {
    bug.x = nextX;
    bug.y = nextY;
    return true;
  }

  if (nextX != bug.x && !hitsWall(nextX, bug.y) &&
      !redBugWouldOverlap(bugIndex, nextX, bug.y)) {
    bug.x = nextX;
    return true;
  }
  if (nextY != bug.y && !hitsWall(bug.x, nextY) &&
      !redBugWouldOverlap(bugIndex, bug.x, nextY)) {
    bug.y = nextY;
    return true;
  }

  const int16_t offsetX[] = {0, 0, -1, 1};
  const int16_t offsetY[] = {-1, 1, 0, 0};
  int32_t bestDistance = INT32_MAX;
  int16_t bestX = bug.x;
  int16_t bestY = bug.y;
  bool foundStep = false;
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t candidateX = bug.x + offsetX[i];
    const int16_t candidateY = bug.y + offsetY[i];
    if (hitsWall(candidateX, candidateY) ||
        redBugWouldOverlap(bugIndex, candidateX, candidateY)) {
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
    bug.x = bestX;
    bug.y = bestY;
    return true;
  }
  return false;
}

bool findRedBugWaypoint(uint8_t bugIndex, int16_t targetX, int16_t targetY,
                        int16_t &waypointX, int16_t &waypointY) {
  constexpr uint8_t columns = 36;
  constexpr uint8_t rows = 27;
  constexpr uint16_t nodeCount = columns * rows;
  static uint16_t queue[nodeCount];
  static int16_t parent[nodeCount];
  const RedBug &bug = redBugs[bugIndex];
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

  uint16_t start = nearestNode(bug.x, bug.y);
  if (hitsWall(nodeX(start), nodeY(start))) {
    int32_t nearestDistance = INT32_MAX;
    for (uint16_t i = 0; i < nodeCount; ++i) {
      if (hitsWall(nodeX(i), nodeY(i))) continue;
      const int32_t dx = bug.x - nodeX(i);
      const int32_t dy = bug.y - nodeY(i);
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
      static_cast<int8_t>(bugIndex % 2 == 0 ? -1 : 1),
      static_cast<int8_t>(bugIndex % 2 == 0 ? 1 : -1),
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

bool bugsCollided() {
  for (uint8_t i = 0; i < redBugCount; ++i) {
    const int16_t dx = greenX - redBugs[i].x;
    const int16_t dy = greenY - redBugs[i].y;
    if (dx * dx + dy * dy <= 24 * 24) return true;
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
          (abs(candidate.x - redBugs[0].x) < 48 &&
           abs(candidate.y + candidate.height / 2 - redBugs[0].y) < 48)) {
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

void spawnRedBug() {
  if (redBugCount >= MAX_RED_BUGS) {
    return;
  }

  RedBug &bug = redBugs[redBugCount];
  bool placed = false;
  for (uint16_t attempt = 0; attempt < 160; ++attempt) {
    const int16_t x = random(24, tft.width() - 24);
    const int16_t y = random(34, tft.height() - 18);
    if (pointHitsWall(x, y) || abs(x - greenX) < 50 || abs(y - greenY) < 35) {
      continue;
    }
    bool overlapsBug = false;
    for (uint8_t i = 0; i < redBugCount; ++i) {
      if (abs(x - redBugs[i].x) < RED_BUG_MIN_DX &&
          abs(y - redBugs[i].y) < RED_BUG_MIN_DY) {
        overlapsBug = true;
        break;
      }
    }
    if (!overlapsBug) {
      bug.x = x;
      bug.y = y;
      placed = true;
      break;
    }
  }

  if (!placed) {
    Serial.println("No free space for a new bug.");
    return;
  }
  bug.lastMove = millis();
  bug.boostUntil = 0;
  ++redBugCount;
  Serial.printf("A new red bug is following you (%u).\n", redBugCount);
}

void placeCrumb(Crumb &crumb) {
  for (uint16_t attempt = 0; attempt < 160; ++attempt) {
    const int16_t x = random(18, tft.width() - 18);
    const int16_t y = random(34, tft.height() - 18);
    if (pointHitsWall(x, y) || abs(x - greenX) < 30 || abs(y - greenY) < 24) continue;
    bool tooCloseToRed = false;
    for (uint8_t i = 0; i < redBugCount; ++i) {
      if (abs(x - redBugs[i].x) < 30 || abs(y - redBugs[i].y) < 24) {
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
  drawRestartButton();
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

    uint32_t *boostUntil = nullptr;
    if (abs(greenX - crumb.x) <= 14 && abs(greenY - crumb.y) <= 12) {
      boostUntil = &greenBoostUntil;
    } else {
      for (uint8_t i = 0; i < redBugCount; ++i) {
        if (abs(redBugs[i].x - crumb.x) <= 14 && abs(redBugs[i].y - crumb.y) <= 12) {
          boostUntil = &redBugs[i].boostUntil;
          break;
        }
      }
      if (boostUntil == nullptr) continue;
    }

    crumb.active = false;
    *boostUntil = now + 2000;
    ++crumbsCollected;
    playCrumbSound();
    Serial.printf("Bread collected: %u/21\n", crumbsCollected);

    if (crumbsCollected >= 21) {
      showWinScreen();
      return;
    }

    if (crumbsCollected % 5 == 0) {
      spawnRedBug();
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
  resetGame();
}

void loop() {
  updateSpeaker();

  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 'c' || command == 'C') {
      calibrateTouch();
      drawScene();
    }
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
        resetGame();
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
  const uint8_t oldRedCount = redBugCount;
  int16_t oldRedX[MAX_RED_BUGS];
  int16_t oldRedY[MAX_RED_BUGS];
  for (uint8_t i = 0; i < oldRedCount; ++i) {
    oldRedX[i] = redBugs[i].x;
    oldRedY[i] = redBugs[i].y;
  }
  bool moved = false;
  const uint32_t greenInterval = static_cast<int32_t>(greenBoostUntil - now) > 0 ? 9 : 15;
  if (now - lastGreenMove >= greenInterval && (greenX != targetX || greenY != targetY)) {
    lastGreenMove = now;
    if (moveBugToward(greenX, greenY, targetX, targetY)) {
      if (now - lastPandaAnimationAt >= 110) {
        ++pandaAnimationFrame;
        lastPandaAnimationAt = now;
      }
      moved = true;
    }
  }
  for (uint8_t i = 0; i < redBugCount; ++i) {
    const uint32_t redInterval =
        static_cast<int32_t>(redBugs[i].boostUntil - now) > 0
            ? 18 + i * 2
            : 40 + i * 4;
    const int16_t redTargetX = constrain(
        greenX + RED_BUG_TARGET_OFFSET_X[i], 20, tft.width() - 20);
    const int16_t redTargetY = constrain(
        greenY + RED_BUG_TARGET_OFFSET_Y[i], 20, tft.height() - 20);
    if (now - redBugs[i].lastMove >= redInterval &&
        (redBugs[i].x != redTargetX || redBugs[i].y != redTargetY)) {
      redBugs[i].lastMove = now;
      int16_t waypointX;
      int16_t waypointY;
      if (findRedBugWaypoint(i, redTargetX, redTargetY, waypointX, waypointY)) {
        moved = moveRedBugToward(i, waypointX, waypointY) || moved;
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
    for (uint8_t i = 0; i < oldRedCount; ++i) {
      eraseBugAt(oldRedX[i], oldRedY[i]);
    }
    if (redBugCount != oldRedCount) {
      drawScene();
    } else {
    drawWalls();
    drawCrumbs();
    drawTarget();
    drawBugs();
    drawCounter();
    }
    if (bugsCollided()) {
      startCaughtMelody();
    }
  }
  delay(5);
}
