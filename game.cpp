#include <ESP8266WiFi.h>
#include <SPI.h>

#define CS1_PIN D1 // Top Display (CS1) - Enemy Viewport & Combat Log
#define CS2_PIN D2 // Bottom Display (CS2) - Hero Stats & Scrollable Menu

SPISettings bu6740Settings(250000, MSBFIRST, SPI_MODE0);

// --- Low-Level SPI & Display Drivers ---

uint16_t transfer16(uint8_t csPin, uint16_t data) {
  SPI.beginTransaction(bu6740Settings);
  digitalWrite(csPin, LOW);
  delayMicroseconds(5);
  uint16_t rx = SPI.transfer16(data);
  delayMicroseconds(5);
  digitalWrite(csPin, HIGH);
  SPI.endTransaction();
  return rx;
}

void sendCommand(uint8_t csPin, uint8_t cmd) {
  uint16_t highNibble = 0x2700 | (cmd & 0xF0);
  uint16_t lowNibble  = 0x2700 | ((cmd & 0x0F) << 4);
  transfer16(csPin, highNibble);
  delayMicroseconds(15);
  transfer16(csPin, lowNibble);
  delayMicroseconds(45);
}

void sendData(uint8_t csPin, uint8_t data) {
  uint16_t highNibble = 0x6700 | (data & 0xF0);
  uint16_t lowNibble  = 0x6700 | ((data & 0x0F) << 4);
  transfer16(csPin, highNibble);
  delayMicroseconds(15);
  transfer16(csPin, lowNibble);
  delayMicroseconds(45);
}

void setLEDs(uint8_t csPin, uint8_t state) {
  transfer16(csPin, 0x9000 | ((state & 0x03) << 4));
}

uint8_t readButtons(uint8_t csPin) {
  transfer16(csPin, 0x0700);
  delayMicroseconds(50);
  uint16_t rx = transfer16(csPin, 0x0700);
  return (rx & 0x000F);
}

void initBU6740(uint8_t csPin) {
  digitalWrite(csPin, HIGH);
  delay(10);
  uint16_t initSeq[] = {
    0x2730, 0x2730, 0x2730, 0x2720,
    0x2720, 0x2790, 0x2710, 0x2780,
    0x2750, 0x27e0, 0x2770, 0x2750,
    0x2760, 0x27c0
  };
  for (int i = 0; i < 14; i++) {
    transfer16(csPin, initSeq[i]);
    delayMicroseconds(15);
  }
  delay(10);
  sendCommand(csPin, 0x0C);
  delay(2);
  sendCommand(csPin, 0x28);
  delay(2);
  sendCommand(csPin, 0x01);
  delay(5);
}

void printToScreen(uint8_t csPin, uint8_t line, String text) {
  uint8_t addr = (line == 0) ? 0x80 : 0xC0;
  sendCommand(csPin, addr);
  for (int i = 0; i < 16; i++) {
    if (i < text.length()) {
      sendData(csPin, text[i]);
    } else {
      sendData(csPin, ' ');
    }
  }
}

// --- Game Engine Structures & State ---

enum MenuState {
  MENU_MAIN,
  MENU_FIGHT,
  MENU_BAG,
  MENU_SHOP
};

enum GameState {
  STATE_TITLE,
  STATE_PLAYER_TURN,
  STATE_ENEMY_TURN,
  STATE_LOOT_SCREEN,
  STATE_GAMEOVER
};

GameState gameState = STATE_TITLE;
MenuState currentMenu = MENU_MAIN;

// Hero Stats & Inventory
int heroHp = 30, heroMaxHp = 30;
int heroMp = 15, heroMaxMp = 15;
int gold = 20;
int hpPotions = 2;
int mpPotions = 1;
int weaponLvl = 0; // Adds +2 physical DMG per level
int armorLvl = 0;  // Reduces -1 incoming DMG per level
int dungeonFloor = 1;

// Enemy Database
struct Enemy {
  String name;
  int maxHp;
  int hp;
  int atkMin;
  int atkMax;
  int goldReward;
};

Enemy currentEnemy;
bool enemyFrozen = false;
String combatLog = "A dungeon awaits!";

// Scrollable Menu Indexing
int menuIndex = 0;

// Hardware Debouncing
unsigned long lastBtnCheck = 0;
uint8_t lastBtn1State = 0x0F;
uint8_t lastBtn2State = 0x0F;

// --- Bestiary Generator ---

void spawnEnemy() {
  enemyFrozen = false;
  int type = random(0, 6);
  if (dungeonFloor % 5 == 0) {
    currentEnemy = {"BOSS DRAGON", 80 + (dungeonFloor * 10), 0, 10, 18, 100};
  } else {
    switch (type) {
      case 0: currentEnemy = {"Giant Rat", 12 + (dungeonFloor * 2), 0, 2, 4, 8}; break;
      case 1: currentEnemy = {"Goblin", 18 + (dungeonFloor * 3), 0, 3, 6, 12}; break;
      case 2: currentEnemy = {"Skeleton", 25 + (dungeonFloor * 3), 0, 4, 8, 18}; break;
      case 3: currentEnemy = {"Orc", 35 + (dungeonFloor * 4), 0, 6, 11, 25}; break;
      case 4: currentEnemy = {"Ghost", 28 + (dungeonFloor * 3), 0, 5, 10, 22}; break;
      case 5: currentEnemy = {"Stone Golem", 50 + (dungeonFloor * 5), 0, 7, 13, 35}; break;
    }
  }
  currentEnemy.hp = currentEnemy.maxHp;
  combatLog = "F" + String(dungeonFloor) + ": Wild " + currentEnemy.name;
  gameState = STATE_PLAYER_TURN;
  currentMenu = MENU_MAIN;
  menuIndex = 0;
  
  setLEDs(CS1_PIN, 2);
  setLEDs(CS2_PIN, 2);
}

void startNewGame() {
  heroHp = 30; heroMaxHp = 30;
  heroMp = 15; heroMaxMp = 15;
  gold = 20;
  hpPotions = 2; mpPotions = 1;
  weaponLvl = 0; armorLvl = 0;
  dungeonFloor = 1;
  spawnEnemy();
}

// --- Combat & Enemy Mechanics ---

void triggerLoot() {
  gameState = STATE_LOOT_SCREEN;
  int gEarned = currentEnemy.goldReward + random(1, 6);
  gold += gEarned;
  
  String lootMsg = "+" + String(gEarned) + "G";
  
  // Random Loot Drop Chances
  int dropRoll = random(0, 100);
  if (dropRoll < 35) {
    hpPotions++;
    lootMsg += " +1 HP Pot";
  } else if (dropRoll < 60) {
    mpPotions++;
    lootMsg += " +1 MP Pot";
  } else if (dropRoll < 75) {
    weaponLvl++;
    lootMsg += " +1 WEAPON!";
  } else if (dropRoll < 85) {
    armorLvl++;
    lootMsg += " +1 ARMOR!";
  }
  
  combatLog = "Slain! Loot: " + lootMsg;
  setLEDs(CS1_PIN, 3);
  setLEDs(CS2_PIN, 3);
}

void processEnemyTurn() {
  if (currentEnemy.hp <= 0) {
    triggerLoot();
    return;
  }

  if (enemyFrozen) {
    enemyFrozen = false;
    combatLog = currentEnemy.name + " is frozen!";
    gameState = STATE_PLAYER_TURN;
    return;
  }

  // Calculate damage minus armor
  int rawDmg = random(currentEnemy.atkMin, currentEnemy.atkMax + 1);
  int finalDmg = rawDmg - armorLvl;
  if (finalDmg < 1) finalDmg = 1;

  heroHp -= finalDmg;
  if (heroHp < 0) heroHp = 0;

  combatLog = currentEnemy.name + " hit -" + String(finalDmg) + " HP";
  setLEDs(CS2_PIN, 1); // Yellow warning flash

  if (heroHp <= 0) {
    gameState = STATE_GAMEOVER;
    setLEDs(CS1_PIN, 1);
    setLEDs(CS2_PIN, 1);
  } else {
    gameState = STATE_PLAYER_TURN;
  }
}

// --- Interactive Menu Action Handlers ---

void handleMainSelection() {
  switch (menuIndex) {
    case 0: // FIGHT
      currentMenu = MENU_FIGHT;
      menuIndex = 0;
      break;
    case 1: // BAG
      currentMenu = MENU_BAG;
      menuIndex = 0;
      break;
    case 2: // SHOP
      currentMenu = MENU_SHOP;
      menuIndex = 0;
      break;
    case 3: // RUN
      if (random(0, 100) < 60) {
        combatLog = "Escaped safely!";
        dungeonFloor++;
        spawnEnemy();
      } else {
        combatLog = "Escape failed!";
        gameState = STATE_ENEMY_TURN;
      }
      break;
  }
}

void handleFightSelection() {
  int dmg = 0;
  switch (menuIndex) {
    case 0: // TACKLE (0 MP)
      dmg = random(4, 7) + (weaponLvl * 2);
      currentEnemy.hp = max(0, currentEnemy.hp - dmg);
      combatLog = "Tackle! Deals " + String(dmg) + "DMG";
      gameState = STATE_ENEMY_TURN;
      break;

    case 1: // SLASH (0 MP, Heavy Physical)
      dmg = random(7, 12) + (weaponLvl * 2);
      currentEnemy.hp = max(0, currentEnemy.hp - dmg);
      combatLog = "Slash! Deals " + String(dmg) + "DMG";
      gameState = STATE_ENEMY_TURN;
      break;

    case 2: // FIREBALL (4 MP)
      if (heroMp >= 4) {
        heroMp -= 4;
        dmg = random(14, 22);
        currentEnemy.hp = max(0, currentEnemy.hp - dmg);
        combatLog = "Fireball! " + String(dmg) + " DMG";
        gameState = STATE_ENEMY_TURN;
      } else {
        combatLog = "Not enough MP!";
      }
      break;

    case 3: // ICE BEAM (5 MP + Freeze)
      if (heroMp >= 5) {
        heroMp -= 5;
        dmg = random(10, 16);
        currentEnemy.hp = max(0, currentEnemy.hp - dmg);
        enemyFrozen = true;
        combatLog = "Ice Beam! " + String(dmg) + " (FREEZE)";
        gameState = STATE_ENEMY_TURN;
      } else {
        combatLog = "Not enough MP!";
      }
      break;
  }
}

void handleBagSelection() {
  switch (menuIndex) {
    case 0: // Use HP Potion
      if (hpPotions > 0 && heroHp < heroMaxHp) {
        hpPotions--;
        heroHp = min(heroMaxHp, heroHp + 20);
        combatLog = "Healed +20 HP!";
        gameState = STATE_ENEMY_TURN;
      } else {
        combatLog = (hpPotions == 0) ? "No HP Potions!" : "HP is full!";
      }
      break;

    case 1: // Use MP Potion
      if (mpPotions > 0 && heroMp < heroMaxMp) {
        mpPotions--;
        heroMp = min(heroMaxMp, heroMp + 12);
        combatLog = "Restored +12 MP!";
        gameState = STATE_ENEMY_TURN;
      } else {
        combatLog = (mpPotions == 0) ? "No MP Potions!" : "MP is full!";
      }
      break;

    case 2: // Equipment Info
      combatLog = "Wpn:+" + String(weaponLvl*2) + " Arm:-" + String(armorLvl);
      break;
  }
}

void handleShopSelection() {
  switch (menuIndex) {
    case 0: // Buy HP Potion (10 Gold)
      if (gold >= 10) {
        gold -= 10;
        hpPotions++;
        combatLog = "Bought HP Potion!";
      } else {
        combatLog = "Need 10 Gold!";
      }
      break;

    case 1: // Buy MP Potion (12 Gold)
      if (gold >= 12) {
        gold -= 12;
        mpPotions++;
        combatLog = "Bought MP Potion!";
      } else {
        combatLog = "Need 12 Gold!";
      }
      break;

    case 2: // Upgrade Weapon (25 Gold)
      if (gold >= 25) {
        gold -= 25;
        weaponLvl++;
        combatLog = "Weapon Upgraded!";
      } else {
        combatLog = "Need 25 Gold!";
      }
      break;

    case 3: // Upgrade Armor (25 Gold)
      if (gold >= 25) {
        gold -= 25;
        armorLvl++;
        combatLog = "Armor Upgraded!";
      } else {
        combatLog = "Need 25 Gold!";
      }
      break;
  }
}

// --- Screen Renderer ---

String getScrollableMenuString(int currentIdx, int totalItems, String itemName) {
  // Format: "1/4 [ FIGHT ]   " strictly formatted to 16 chars
  String prefix = String(currentIdx + 1) + "/" + String(totalItems) + " [";
  String full = prefix + itemName + "]";
  while (full.length() < 16) {
    full += " ";
  }
  return full.substring(0, 16);
}

void renderScreens() {
  if (gameState == STATE_TITLE) {
    printToScreen(CS1_PIN, 0, "=== VFD QUEST ==");
    printToScreen(CS1_PIN, 1, "PokemonRPG Demo");
    printToScreen(CS2_PIN, 0, "B2: START GAME  ");
    printToScreen(CS2_PIN, 1, "B1:[L] B3:[R] OK");
    return;
  }

  if (gameState == STATE_GAMEOVER) {
    printToScreen(CS1_PIN, 0, "  YOU DEFEATED  ");
    printToScreen(CS1_PIN, 1, "Reached Floor: " + String(dungeonFloor));
    printToScreen(CS2_PIN, 0, "Press B2/Start  ");
    printToScreen(CS2_PIN, 1, "To Try Again    ");
    return;
  }

  if (gameState == STATE_LOOT_SCREEN) {
    printToScreen(CS1_PIN, 0, "VICTORY!        ");
    printToScreen(CS1_PIN, 1, combatLog.substring(0, 16));
    printToScreen(CS2_PIN, 0, "HP:" + String(heroHp) + " G:" + String(gold) + " Pot:" + String(hpPotions));
    printToScreen(CS2_PIN, 1, "Press B2 Next FL");
    return;
  }

  // === COMBAT SCREEN RENDERING ===

  // TOP DISPLAY (CS1) - ENEMY VIEW
  String enemyBar = currentEnemy.name.substring(0, 8) + " " + String(currentEnemy.hp) + "/" + String(currentEnemy.maxHp);
  printToScreen(CS1_PIN, 0, enemyBar);
  printToScreen(CS1_PIN, 1, combatLog.substring(0, 16));

  // BOTTOM DISPLAY (CS2) - HERO STATS
  String heroStats = "H:" + String(heroHp) + " M:" + String(heroMp) + " G:" + String(gold);
  printToScreen(CS2_PIN, 0, heroStats);

  // BOTTOM DISPLAY LINE 1 - SCROLLABLE MENU CAROUSEL
  if (gameState == STATE_PLAYER_TURN) {
    switch (currentMenu) {
      case MENU_MAIN: {
        String items[] = {"FIGHT", "BAG", "SHOP", "RUN"};
        printToScreen(CS2_PIN, 1, getScrollableMenuString(menuIndex, 4, items[menuIndex]));
        break;
      }
      case MENU_FIGHT: {
        String items[] = {"Tackle (0M)", "Slash (0M)", "Fireball (4M)", "IceBeam (5M)"};
        printToScreen(CS2_PIN, 1, getScrollableMenuString(menuIndex, 4, items[menuIndex]));
        break;
      }
      case MENU_BAG: {
        String items[] = {"HP Pot (" + String(hpPotions) + ")", "MP Pot (" + String(mpPotions) + ")", "Equip Stat"};
        printToScreen(CS2_PIN, 1, getScrollableMenuString(menuIndex, 3, items[menuIndex]));
        break;
      }
      case MENU_SHOP: {
        String items[] = {"Buy HPPot 10G", "Buy MPPot 12G", "Upg Wpn 25G", "Upg Arm 25G"};
        printToScreen(CS2_PIN, 1, getScrollableMenuString(menuIndex, 4, items[menuIndex]));
        break;
      }
    }
  } else {
    printToScreen(CS2_PIN, 1, "Waiting...      ");
  }
}

// --- Main Arduino Loop ---

void setup() {
  Serial.begin(115200);
  randomSeed(ESP.getCycleCount());

  pinMode(CS1_PIN, OUTPUT);
  pinMode(CS2_PIN, OUTPUT);
  digitalWrite(CS1_PIN, HIGH);
  digitalWrite(CS2_PIN, HIGH);

  SPI.begin();
  delay(1000);

  initBU6740(CS1_PIN);
  initBU6740(CS2_PIN);

  setLEDs(CS1_PIN, 2);
  setLEDs(CS2_PIN, 2);

  renderScreens();
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Process Hardware Buttons (40ms Debounce Polling)
  if (currentMillis - lastBtnCheck >= 40) {
    lastBtnCheck = currentMillis;

    uint8_t btn1 = readButtons(CS1_PIN);
    uint8_t btn2 = readButtons(CS2_PIN);
    uint8_t combined = btn1 & btn2;
    uint8_t lastCombined = lastBtn1State & lastBtn2State;

    int totalMenuItems = (currentMenu == MENU_BAG) ? 3 : 4;

    // --- BUTTON 1: SCROLL LEFT [L] ---
    if ((combined & 0x01) == 0 && (lastCombined & 0x01) != 0) {
      if (gameState == STATE_PLAYER_TURN) {
        menuIndex = (menuIndex - 1 + totalMenuItems) % totalMenuItems;
        renderScreens();
      }
    }

    // --- BUTTON 3: SCROLL RIGHT [R] ---
    if ((combined & 0x04) == 0 && (lastCombined & 0x04) != 0) {
      if (gameState == STATE_PLAYER_TURN) {
        menuIndex = (menuIndex + 1) % totalMenuItems;
        renderScreens();
      }
    }

    // --- BUTTON 2: CONFIRM / EXECUTE ---
    if ((combined & 0x02) == 0 && (lastCombined & 0x02) != 0) {
      if (gameState == STATE_TITLE) {
        startNewGame();
      } else if (gameState == STATE_LOOT_SCREEN) {
        dungeonFloor++;
        heroMp = min(heroMaxMp, heroMp + 3);
        spawnEnemy();
      } else if (gameState == STATE_GAMEOVER) {
        gameState = STATE_TITLE;
      } else if (gameState == STATE_PLAYER_TURN) {
        switch (currentMenu) {
          case MENU_MAIN: handleMainSelection(); break;
          case MENU_FIGHT: handleFightSelection(); break;
          case MENU_BAG: handleBagSelection(); break;
          case MENU_SHOP: handleShopSelection(); break;
        }
      }
      renderScreens();
    }

    // --- BUTTON 4: CANCEL / BACK ---
    if ((combined & 0x08) == 0 && (lastCombined & 0x08) != 0) {
      if (gameState == STATE_PLAYER_TURN && currentMenu != MENU_MAIN) {
        currentMenu = MENU_MAIN; // Return to root menu
        menuIndex = 0;
      } else if (gameState != STATE_TITLE) {
        gameState = STATE_TITLE; // Exit to Title
      }
      renderScreens();
    }

    lastBtn1State = btn1;
    lastBtn2State = btn2;
  }

  // 2. Enemy Turn Delay Handler
  static unsigned long enemyTimer = 0;
  if (gameState == STATE_ENEMY_TURN) {
    if (enemyTimer == 0) enemyTimer = millis();
    if (millis() - enemyTimer >= 900) {
      enemyTimer = 0;
      processEnemyTurn();
      renderScreens();
    }
  }
}