#include <LiquidCrystal.h>
#define lbttn 8
#define rbttn 7
#define bbttn 6
#define timeEntry 0
#define bonusTimeEntry 1
#define timerAwaitingStart 2
#define timerInAction 3
#define timerExpired 4
#define timerPaused 5
#define coinFlip 6
// Time needed to hold to count as a long hold.
#define timeToHold 200
#define timeToHoldFast 100
#define maxSeconds 144000
#define milliUpdateInterval 150
#define unconnectedPin 10

// Turns the given value into a string ready to be printed directly to the lcd.
String LongToFormattedString(long seconds, int secondToMinuteTransitionPoint) {
  String result;
  
  if (seconds >= secondToMinuteTransitionPoint) {
    result = String(float(seconds) / 60.0, 1);
    result += " minutes";
  }
  else {
    result = String(seconds);
    result += " seconds";
  }

  while(result.length() < 16) {
    result += " ";
  }

  return result;
}

class Button {
private:
  byte pin;
  bool previousPressState;
  bool currentPressState;
  bool justPressedConsumed;

public:
  Button(byte pin) {
    this->pin = pin;
    pinMode(pin, INPUT);
  }

  void UpdateButtonState() {
    previousPressState = currentPressState;
    currentPressState = digitalRead(pin) == HIGH;
    justPressedConsumed = false;
  }

  bool IsJustPressed() {
    return currentPressState && !previousPressState && !justPressedConsumed;
  }

  bool IsPressed() {
    return currentPressState;
  }

  void ConsumeJustPressed() {
    justPressedConsumed = true;
  }
};

// Max chars that can be displayed: 16

// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
int timerState = 0;
bool isWhiteTurn = false;

long whiteTime = 0; // In miliseconds.
long blackTime = 0; // In miliseconds.
long bonusTime = 0; // In seconds.

unsigned long cachedTime = 0; // In miliseconds.

unsigned long timeOfLastPageChange = 0;
int pageChangeInterval = 2000;
bool showFirstPage = true;

Button leftButton(lbttn);
Button rightButton(rbttn);
Button middleButton(bbttn);

class TimeSelector {
protected:
  long time = 0;
  int lowChange = 0;
  int mediumChange = 0;
  int bigChange = 0;
  int massiveChange = 0;
  int timesAutoIncremented = 0;
  unsigned long timeButtonStartedHold = 0;

  virtual void UpdateDisplay() {
    lcd.print("Not set");
  }

  virtual void InitialDisplay() {
    lcd.print("Not set");
  }

  virtual void GoToNextState() {
    lcd.print("Confirm not defined.");
  }

  void IncrementTime() {
    if (time < 120) {
      time += lowChange;
    }
    else if (time < 600) {
      time += mediumChange;
    }
    else if (time < 3600) {
      time += bigChange;
    }
    else {
      time += massiveChange;
    }

    if (time > maxSeconds) {
      time = 0;
    }
  }

  void DecrementTime() {
    if (time <= 120) {
      time -= lowChange;
    }
    else if (time <= 600) {
      time -= mediumChange;
    }
    else if (time <= 3600) {
      time -= bigChange;
    }
    else {
      time -= massiveChange;
    }

    if (time < 0) {
      time = maxSeconds;
    }
  }

public:
  TimeSelector(int lowChange, int medChange, int bigChange, int massiveChange) {
    time = 0;
    this->lowChange = lowChange;
    this->mediumChange = medChange;
    this->bigChange = bigChange;
    this->massiveChange = massiveChange;

    InitialDisplay();
  }

  void Update() {
    if (middleButton.IsJustPressed()) {
      middleButton.ConsumeJustPressed();
      GoToNextState();
      return;
    }

    // Choose if auto increment should increment fast or slow.
    int timeToHoldMili;
    if (timesAutoIncremented > 10) {
      timeToHoldMili = timeToHoldFast;
    }
    else {
      timeToHoldMili = timeToHold;
    }

    if (leftButton.IsPressed() == rightButton.IsPressed()) {
      timeButtonStartedHold = millis();
      timesAutoIncremented = 0;
    }
    else if ((millis() - timeButtonStartedHold) > timeToHoldMili) {
      timesAutoIncremented++;
      timeButtonStartedHold = millis();

      if (leftButton.IsPressed()) {
        DecrementTime();
      }
      else {
        IncrementTime();
      }
    }
    else if (leftButton.IsJustPressed()) {
      timesAutoIncremented = 0;
      DecrementTime();
    }
    else if (rightButton.IsJustPressed()) {
      timesAutoIncremented = 0;
      IncrementTime();
    }
    
    UpdateDisplay();
  }

  long GetSelectedTime() {
    return time;
  }
};

class StartingTimeSelector : public TimeSelector {
protected:
  virtual void UpdateDisplay() {
    lcd.setCursor(0, 0);
    lcd.print("Set Start Time: ");
    lcd.setCursor(0, 1);
    lcd.print(LongToFormattedString(time, 120));
  }

  virtual void InitialDisplay() {
    lcd.setCursor(0, 0);
    lcd.print("Set Start Time: ");
    lcd.setCursor(0, 1);
    lcd.print(LongToFormattedString(time, 120));
  }

  virtual void GoToNextState() {
    timerState = bonusTimeEntry;
    whiteTime = time * 1000;
    blackTime = time * 1000;
  }

public:
  StartingTimeSelector() : 
  TimeSelector(10, 30, 60, 300) {}
};

class BonusTimeSelector : public TimeSelector {
protected:
  virtual void UpdateDisplay() {
    lcd.setCursor(0, 0);
    lcd.print("Set Bonus Time: ");
    lcd.setCursor(0, 1);
    lcd.print(LongToFormattedString(time, 120));
  }

  virtual void InitialDisplay() {
    lcd.setCursor(0, 0);
    lcd.print("Set Bonus Time: ");
    lcd.setCursor(0, 1);
    lcd.print(LongToFormattedString(time, 120));
  }

  virtual void GoToNextState() {
    timerState = coinFlip;
    bonusTime = time;
  }

public:
  BonusTimeSelector() : 
  TimeSelector(1, 1, 1, 1) {}
};

StartingTimeSelector startTimeHandler;
BonusTimeSelector bonusTimeHandler;

class CoinFlipper {
private:
bool displayInitialScreen = true;

public:
  CoinFlipper() {
    randomSeed(analogRead(10));
  }

  Update() {
    if (middleButton.IsJustPressed()) {
      timerState = timerAwaitingStart;
      middleButton.ConsumeJustPressed();
      displayInitialScreen = true;
      return;
    }
    else if (leftButton.IsJustPressed() || rightButton.IsJustPressed()) {
      displayInitialScreen = false;
      lcd.setCursor(0, 0);
      lcd.print("Coin Flip Result");
      lcd.setCursor(0, 1);

      if (random() % 2 == 0) {
        lcd.print("     Heads      ");
      }
      else {
        lcd.print("     Tails      ");
      }
    }

    if (displayInitialScreen) {
      lcd.setCursor(0, 0);
      lcd.print("Press L or R btn");
      lcd.setCursor(0, 1);
      lcd.print("to flip a coin. ");
    }
  }
};

CoinFlipper coinFlipHandler;

void setup() {
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  randomSeed(analogRead(unconnectedPin));
}

void loop() {
  leftButton.UpdateButtonState();
  rightButton.UpdateButtonState();
  middleButton.UpdateButtonState();
  
  lcd.setCursor(0, 0);
  
  // Each function is effectively a page that the LCD displays.
  switch(timerState) {
    case timeEntry:
      startTimeHandler.Update();
      break;
    case bonusTimeEntry:
      bonusTimeHandler.Update();
      break;
    case timerAwaitingStart:
      TimerAwaitingStart();
      break;
    case timerInAction:
      TimerInAction();
      break;
    case timerExpired:
      DisplayWinner();
      break;
    case timerPaused:
      DisplayPauseScreen();
      break;
    case coinFlip:
      coinFlipHandler.Update();
      break;
  }
}

void TimerAwaitingStart() {
  lcd.print("Press left      ");
  lcd.setCursor(0, 1);
  lcd.print("button to begin.");

  if (leftButton.IsJustPressed()) {
    isWhiteTurn = true;
    timerState++;
    cachedTime = millis();
  }
}

String FormattedTime(long miliseconds) {
  String toPrint;

  if (miliseconds < 15000) {
    toPrint = String((float)miliseconds / 1000, 2) + "s";
  }
  else if (miliseconds < 60000) {
    toPrint = String(miliseconds / 1000) + "s";
  }
  else {
    toPrint = String(miliseconds / 1000 / 60) + ":";
    if (miliseconds / 1000 % 60 < 10) {
      toPrint += "0" + String(miliseconds / 1000 % 60);
    }
    else {
      toPrint += String(miliseconds / 1000 % 60);
    }
  }

  return toPrint;
}

void TimerInAction() {
  // When pause button is pressed.
  if (middleButton.IsJustPressed()) {
    middleButton.ConsumeJustPressed();
    timerState = timerPaused;
    timeOfLastPageChange = millis();
    return;
  }

  // Update remaining time.
  if (isWhiteTurn) {
    whiteTime -= millis() - cachedTime;
    if (whiteTime <= 0) {
      timerState++;
      return;
    }

    if (leftButton.IsJustPressed()) {
      isWhiteTurn = false;
      whiteTime += bonusTime * 1000;
    }
  }
  else {
    blackTime -= millis() - cachedTime;
    if (blackTime <= 0) {
      timerState++;
      return;
    }

    if (rightButton.IsJustPressed()) {
      isWhiteTurn = true;
      blackTime += bonusTime * 1000;
    }
  }
  cachedTime = millis();

  DisplayTime();
}

void DisplayTime() {
  String whitePrint;
  String blackPrint;

  whitePrint = "W: " + FormattedTime(whiteTime);
  blackPrint = "B: " + FormattedTime(blackTime);

  if (isWhiteTurn) {
    whitePrint = ">" + whitePrint;
    blackPrint = " " + blackPrint;
  }
  else {
    whitePrint = " " + whitePrint;
    blackPrint = ">" + blackPrint;
  }

  while(whitePrint.length() < 16) {
    whitePrint += " ";
  }
  while(blackPrint.length() < 16) {
    blackPrint += " ";
  }

  lcd.setCursor(0, 0);
  lcd.print(whitePrint);
  lcd.setCursor(0, 1);
  lcd.print(blackPrint);
}

void DisplayWinner() {
  if (whiteTime <= 0) {
    lcd.print("  Black Won On  ");
  }
  else {
    lcd.print("  White Won On  ");
  }
  lcd.setCursor(0, 1);
  lcd.print("     Time!      ");

  if (middleButton.IsJustPressed()) {
    timerState = timeEntry;
    middleButton.ConsumeJustPressed();
  }
}

void DisplayPauseScreen() {
  if (millis() - timeOfLastPageChange >= pageChangeInterval) {
    showFirstPage = !showFirstPage;
    timeOfLastPageChange = millis();
  }

  String toPrint;

  if (showFirstPage) {
    if (isWhiteTurn) {
      toPrint = " White To Play  ";
    }
    else {
      toPrint = " Black To Play  ";
    }

    lcd.print(toPrint);
    lcd.setCursor(0, 1);
    lcd.print(" Timer Stopped  ");
  }
  else {
    DisplayTime();
  }

  if (middleButton.IsJustPressed()) {
    middleButton.ConsumeJustPressed();
    timerState = timerInAction;
    showFirstPage = true;
    cachedTime = millis();
    return;
  }
}