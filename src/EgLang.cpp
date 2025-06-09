#include "EgLang.h"

// Конфигурация пинов в PROGMEM (экономия SRAM)
const byte inputs[] PROGMEM = {3, 5, 7, 9, 11, 13};
const byte outputs[] PROGMEM = {2, 4, 6, 8, 10, 12};

// Глобальный контроллер
EgLangController _eglang;

// Конструктор Rule
Rule::Rule() : done(false) {
    ruleText[0] = '\0';
    memset(&parsed, 0, sizeof(parsed));
    parsed.valid = false;
}

void Rule::setRule(const char* text) {
    if (!text || !text[0]) return; // БАГ-ФИХ: Проверка пустой строки
    
    strncpy(ruleText, text, MAX_RULE_LENGTH - 1);
    ruleText[MAX_RULE_LENGTH - 1] = '\0';
    parseRule();
}

void EgLangController::init() {
    if (initialized) return;
    
    // БАГ-ФИХ: Инициализация Serial для минимальной отладки
    Serial.begin(9600);
    
    // Настройка пинов (читаем из PROGMEM)
    for (byte i = 0; i < 6; i++) {
        pinMode(pgm_read_byte(&inputs[i]), INPUT_PULLUP);
        pinMode(pgm_read_byte(&outputs[i]), INPUT); // HIGH-Z
        
        // Инициализация состояний OUTPUT пинов
        pinStates[i].pin = pgm_read_byte(&outputs[i]);
        pinStates[i].state = 0;
        pinStates[i].isOutput = 0;
    }
    
    count = 0;
    currentRule = 0;
    initialized = true;
    
    // БАГ-ФИХ: Небольшая задержка для стабилизации INPUT_PULLUP
    delay(10);
}

bool EgLangController::add(const char* rule) {
    init();
    
    if (!rule || count >= MAX_RULES) return false;
    
    rules[count].setRule(rule);
    if (rules[count].parsed.valid) {
        count++;
        return true;
    }
    return false;
}

void EgLangController::run() {
    if (!initialized || count == 0) return;
    
    // ИЗМЕНЕНО: Проверяем все правила каждый цикл для непрерывных условий
    bool anyRuleExecuted = false;
    
    for (byte i = 0; i < count; i++) {
        if (rules[i].check()) {
            anyRuleExecuted = true;
        }
    }
    
    // Переходим к следующему правилу только для простых команд
    if (currentRule < count && rules[currentRule].parsed.isSimpleCommand && rules[currentRule].done) {
        currentRule++;
        if (currentRule >= count) {
            currentRule = 0;
            
            // Сброс простых команд
            for (byte i = 0; i < count; i++) {
                if (rules[i].parsed.isSimpleCommand) {
                    rules[i].reset();
                }
            }
        }
    }
}

void EgLangController::resetPinsToHighZ() {
    for (byte i = 0; i < 6; i++) {
        pinMode(pgm_read_byte(&outputs[i]), INPUT);
    }
}

void EgLangController::reset() {
    currentRule = 0;
    for (byte i = 0; i < count; i++) {
        rules[i].reset();
    }
    // Пины сохраняют свое состояние при reset()
}

// НОВЫЙ МЕТОД: Завершение работы с полным сбросом пинов
void EgLangController::shutdown() {
    // Сбрасываем все правила
    reset();
    
    // Сбрасываем все OUTPUT пины в HIGH-Z
    resetPinsToHighZ();
    
    // Очищаем состояния пинов
    for (byte i = 0; i < 6; i++) {
        pinStates[i].state = 0;
        pinStates[i].isOutput = 0;
    }
    
    Serial.println("EgLang shutdown complete");
}

// НОВЫЕ МЕТОДЫ: Управление состояниями пинов
void EgLangController::updatePinState(byte pin, byte state) {
    for (byte i = 0; i < 6; i++) {
        if (pinStates[i].pin == pin) {
            pinStates[i].state = state;
            pinStates[i].isOutput = 1;
            break;
        }
    }
}

void EgLangController::setPinOutput(byte pin, byte state) {
    // ИСПРАВЛЕНИЕ: Строгая проверка - избегаем ЛЮБЫХ повторных вызовов
    for (byte i = 0; i < 6; i++) {
        if (pinStates[i].pin == pin) {
            if (pinStates[i].isOutput && pinStates[i].state == state) {
                // ПОЛНОСТЬЮ ИГНОРИРУЕМ повторную установку того же состояния
                return;
            }
            break;
        }
    }
    
    // Устанавливаем пин только если состояние ДЕЙСТВИТЕЛЬНО изменилось
    pinMode(pin, OUTPUT);
    digitalWrite(pin, state);
    updatePinState(pin, state);
    
    // ОТЛАДКА: Показываем только реальные изменения
    Serial.print("CHANGE Pin "); Serial.print(pin); 
    Serial.print(" -> "); Serial.println(state);
}

// НОВЫЙ МЕТОД: Проверка и сброс неактивных пинов
void EgLangController::checkAndResetInactivePins() {
    // Проверяем каждый активный пин
    for (byte i = 0; i < 6; i++) {
        if (pinStates[i].isOutput) {
            byte pin = pinStates[i].pin;
            bool shouldBeActive = false;
            
            // Проверяем есть ли активные условные правила для этого пина
            for (byte j = 0; j < count; j++) {
                if (rules[j].parsed.valid && !rules[j].parsed.isSimpleCommand && 
                    !rules[j].parsed.isLoop && rules[j].parsed.action == pin) {
                    
                    // Проверяем условие
                    bool condition1 = (readPinStable(rules[j].parsed.trigger1) == (rules[j].parsed.tState1 == 1));
                    bool condition2 = true;
                    
                    if (rules[j].parsed.useAND) {
                        condition2 = (readPinStable(rules[j].parsed.trigger2) == (rules[j].parsed.tState2 == 1));
                    }
                    
                    if (condition1 && condition2) {
                        shouldBeActive = true;
                        break;
                    }
                }
            }
            
            // Если пин не должен быть активным, сбрасываем его
            if (!shouldBeActive && pinStates[i].state == 1) {
                setPinOutput(pin, 0);
            }
        }
    }
}

// Вспомогательная функция для стабильного чтения (перенесена из Rule)
bool EgLangController::readPinStable(byte pin) {
    byte readings = 0;
    for (byte i = 0; i < 3; i++) {
        if (digitalRead(pin) == LOW) readings++;
        delayMicroseconds(100);
    }
    return (readings >= 2);
}

// Глобальные функции-обёртки
void addRule(const char* rule) {
    _eglang.add(rule);
}

void processRules() {
    _eglang.run();
}

// НОВАЯ ФУНКЦИЯ: Завершение работы EgLang
void shutdownEgLang() {
    _eglang.shutdown();
}

// Методы Rule с исправленными багами
void Rule::parseRule() {
    memset(&parsed, 0, sizeof(parsed));
    parsed.valid = false;
    
    if (!ruleText[0]) return;
    
    byte len = strlen(ruleText);
    if (len < 3) return; // Минимум "2,1"
    
    if (ruleText[0] == '[' && ruleText[len-1] == ']') {
        parseLoop();
    } else if (ruleText[0] == '?') {
        parseConditionalRule();
    } else {
        parseSimpleCommand();
    }
}

void Rule::parseLoop() {
    // БАГ-ФИХ: Более строгая проверка формата [pin:commands]
    byte len = strlen(ruleText);
    if (len < 5) return; // Минимум "[3:4]"
    
    // Ищем двоеточие
    char* colon = strchr(ruleText + 1, ':');
    if (!colon || colon >= ruleText + len - 2) return; // Должно быть место для команд
    
    // Извлекаем пин (максимум 2 цифры)
    int pinLen = colon - (ruleText + 1);
    if (pinLen <= 0 || pinLen > 2) return;
    
    char pinStr[4];
    strncpy(pinStr, ruleText + 1, pinLen);
    pinStr[pinLen] = '\0';
    
    int pin = atoi(pinStr);
    if (!isPinValid(pin)) return;
    
    // БАГ-ФИХ: Проверяем что пин является INPUT пином
    if (!isInputPin(pin)) return;
    
    // Извлекаем команды (между : и ])
    int cmdLen = (ruleText + len - 1) - (colon + 1);
    if (cmdLen <= 0 || cmdLen >= MAX_LOOP_COMMANDS) return;
    
    strncpy(parsed.loopCommands, colon + 1, cmdLen);
    parsed.loopCommands[cmdLen] = '\0';
    
    // БАГ-ФИХ: Валидируем каждую команду в цикле
    if (!validateLoopCommands(parsed.loopCommands)) return;
    
    parsed.loopPin = pin;
    parsed.isLoop = true;
    parsed.valid = true;
}

void Rule::parseSimpleCommand() {
    char* comma = strchr(ruleText, ',');
    if (!comma || comma == ruleText) return;
    
    // БАГ-ФИХ: Проверяем что есть символы после запятой
    if (!*(comma + 1) || *(comma + 2) != '\0') return; // Только один символ после запятой
    
    // Извлекаем пин
    int pinLen = comma - ruleText;
    if (pinLen <= 0 || pinLen > 2) return;
    
    char pinStr[4];
    strncpy(pinStr, ruleText, pinLen);
    pinStr[pinLen] = '\0';
    
    int pin = atoi(pinStr);
    if (!isPinValid(pin)) return;
    
    // БАГ-ФИХ: Проверяем что пин является OUTPUT пином
    if (!isOutputPin(pin)) return;
    
    // БАГ-ФИХ: Проверяем состояние (только '0' или '1')
    char stateChar = *(comma + 1);
    if (stateChar != '0' && stateChar != '1') return;
    
    parsed.action = pin;
    parsed.aState = (stateChar == '1') ? 1 : 0;
    parsed.isSimpleCommand = true;
    parsed.valid = true;
}

void Rule::parseConditionalRule() {
    char* exclamation = strchr(ruleText, '!');
    if (!exclamation || exclamation <= ruleText + 1) return;
    
    // Парсим действие после !
    char* actionStart = exclamation + 1;
    char* actionComma = strchr(actionStart, ',');
    if (!actionComma || actionComma == actionStart) return;
    
    // БАГ-ФИХ: Проверяем что после запятой только один символ (состояние)
    if (!*(actionComma + 1) || *(actionComma + 2) != '\0') return;
    
    // Извлекаем пин действия
    int actionPinLen = actionComma - actionStart;
    if (actionPinLen <= 0 || actionPinLen > 2) return;
    
    char actionPinStr[4];
    strncpy(actionPinStr, actionStart, actionPinLen);
    actionPinStr[actionPinLen] = '\0';
    
    int actionPin = atoi(actionPinStr);
    if (!isPinValid(actionPin) || !isOutputPin(actionPin)) return;
    
    char actionStateChar = *(actionComma + 1);
    if (actionStateChar != '0' && actionStateChar != '1') return;
    
    parsed.action = actionPin;
    parsed.aState = (actionStateChar == '1') ? 1 : 0;
    
    // НОВОЕ: Условные правила теперь непрерывные
    parsed.isContinuous = true;
    
    // Парсим условие (от ? до !)
    int conditionLen = exclamation - (ruleText + 1);
    if (conditionLen <= 0 || conditionLen > 15) return; // Ограничиваем длину условия
    
    char condition[16];
    strncpy(condition, ruleText + 1, conditionLen);
    condition[conditionLen] = '\0';
    
    // Проверяем наличие &
    char* ampersand = strchr(condition, '&');
    if (ampersand) {
        // AND условие
        if (!parseAndCondition(condition, ampersand)) return;
    } else {
        // Простое условие
        if (!parseSimpleCondition(condition)) return;
    }
    
    parsed.valid = true;
}

// БАГ-ФИХ: Новая функция для валидации INPUT пинов
bool Rule::isInputPin(byte pin) {
    for (byte i = 0; i < 6; i++) {
        if (pgm_read_byte(&inputs[i]) == pin) return true;
    }
    return false;
}

// БАГ-ФИХ: Новая функция для валидации OUTPUT пинов
bool Rule::isOutputPin(byte pin) {
    for (byte i = 0; i < 6; i++) {
        if (pgm_read_byte(&outputs[i]) == pin) return true;
    }
    return false;
}

bool Rule::isPinValid(byte pin) {
    return (pin >= 2 && pin <= 13);
}

// БАГ-ФИХ: Новая функция для валидации команд в цикле
bool Rule::validateLoopCommands(const char* commands) {
    if (!commands || !*commands) return false;
    
    char temp[MAX_LOOP_COMMANDS];
    strncpy(temp, commands, MAX_LOOP_COMMANDS - 1);
    temp[MAX_LOOP_COMMANDS - 1] = '\0';
    
    char* cmd = temp;
    char* start = cmd;
    
    while (*cmd) {
        if (*cmd == ';') {
            *cmd = '\0';
            if (!validateSingleLoopCommand(start)) return false;
            start = cmd + 1;
        }
        cmd++;
    }
    
    // Проверяем последнюю команду
    if (start < cmd && *start) {
        if (!validateSingleLoopCommand(start)) return false;
    }
    
    return true;
}

// БАГ-ФИХ: Валидация одной команды цикла
bool Rule::validateSingleLoopCommand(const char* command) {
    if (!command || !*command) return false;
    
    char* comma = strchr((char*)command, ',');
    if (!comma || comma == command) return false;
    
    // Проверяем что после запятой только один символ
    if (!*(comma + 1) || *(comma + 2) != '\0') return false;
    
    // Проверяем пин
    int pinLen = comma - command;
    if (pinLen <= 0 || pinLen > 2) return false;
    
    char pinStr[4];
    strncpy(pinStr, command, pinLen);
    pinStr[pinLen] = '\0';
    
    int pin = atoi(pinStr);
    if (!isPinValid(pin) || !isOutputPin(pin)) return false;
    
    // Проверяем состояние
    char stateChar = *(comma + 1);
    if (stateChar != '0' && stateChar != '1') return false;
    
    return true;
}

// БАГ-ФИХ: Парсинг AND условия
bool Rule::parseAndCondition(const char* condition, char* ampersand) {
    char* comma1 = strchr((char*)condition, ',');
    char* comma2 = strchr(ampersand + 1, ',');
    
    if (!comma1 || !comma2 || comma1 >= ampersand) return false;
    
    // Первое условие
    int pin1Len = comma1 - condition;
    if (pin1Len <= 0 || pin1Len > 2) return false;
    
    char pin1Str[4];
    strncpy(pin1Str, condition, pin1Len);
    pin1Str[pin1Len] = '\0';
    
    int pin1 = atoi(pin1Str);
    if (!isPinValid(pin1) || !isInputPin(pin1)) return false;
    
    // БАГ-ФИХ: Проверяем что состояние только один символ
    if (comma1 + 2 != ampersand) return false; // Только один символ между , и &
    char state1Char = *(comma1 + 1);
    if (state1Char != '0' && state1Char != '1') return false;
    
    // Второе условие
    int pin2Len = comma2 - (ampersand + 1);
    if (pin2Len <= 0 || pin2Len > 2) return false;
    
    char pin2Str[4];
    strncpy(pin2Str, ampersand + 1, pin2Len);
    pin2Str[pin2Len] = '\0';
    
    int pin2 = atoi(pin2Str);
    if (!isPinValid(pin2) || !isInputPin(pin2)) return false;
    
    // БАГ-ФИХ: Проверяем что после запятой только один символ
    if (*(comma2 + 2) != '\0') return false;
    char state2Char = *(comma2 + 1);
    if (state2Char != '0' && state2Char != '1') return false;
    
    parsed.trigger1 = pin1;
    parsed.tState1 = (state1Char == '1') ? 1 : 0;
    parsed.trigger2 = pin2;
    parsed.tState2 = (state2Char == '1') ? 1 : 0;
    parsed.useAND = true;
    
    return true;
}

// БАГ-ФИХ: Парсинг простого условия
bool Rule::parseSimpleCondition(const char* condition) {
    char* comma = strchr((char*)condition, ',');
    if (!comma || comma == condition) return false;
    
    // БАГ-ФИХ: Проверяем что после запятой только один символ
    if (!*(comma + 1) || *(comma + 2) != '\0') return false;
    
    int pinLen = comma - condition;
    if (pinLen <= 0 || pinLen > 2) return false;
    
    char pinStr[4];
    strncpy(pinStr, condition, pinLen);
    pinStr[pinLen] = '\0';
    
    int pin = atoi(pinStr);
    if (!isPinValid(pin) || !isInputPin(pin)) return false;
    
    char stateChar = *(comma + 1);
    if (stateChar != '0' && stateChar != '1') return false;
    
    parsed.trigger1 = pin;
    parsed.tState1 = (stateChar == '1') ? 1 : 0;
    
    return true;
}

bool Rule::executeLoopCommand(const char* command) {
    if (!command || !*command) return true;
    
    char* comma = strchr((char*)command, ',');
    if (!comma || comma == command) return false;
    
    int pinLen = comma - command;
    if (pinLen <= 0 || pinLen > 2) return false;
    
    char pinStr[4];
    strncpy(pinStr, command, pinLen);
    pinStr[pinLen] = '\0';
    
    int pin = atoi(pinStr);
    if (!isPinValid(pin) || !isOutputPin(pin)) return false;
    
    char stateChar = *(comma + 1);
    if (stateChar != '0' && stateChar != '1') return false;
    
    byte state = (stateChar == '1') ? 1 : 0;
    
    // УБРАНО: Проверка состояния пина
    // Теперь команды выполняются каждый раз, как и должно быть в цикле
    _eglang.setPinOutput(pin, state);
    
    return true;
}

void Rule::executeLoopCommands() {
    if (!parsed.loopCommands[0]) return;
    
    char temp[MAX_LOOP_COMMANDS];
    strncpy(temp, parsed.loopCommands, MAX_LOOP_COMMANDS - 1);
    temp[MAX_LOOP_COMMANDS - 1] = '\0';
    
    char* cmd = temp;
    char* start = cmd;
    
    while (*cmd) {
        if (*cmd == ';') {
            *cmd = '\0';
            executeLoopCommand(start);
            start = cmd + 1;
        }
        cmd++;
    }
    
    if (start < cmd && *start) {
        executeLoopCommand(start);
    }
}

// ИСПРАВЛЕННЫЙ МЕТОД: Полный анализ всех команд в цикле
bool Rule::hasAlternatingCommands() {
    if (!parsed.loopCommands[0]) return false;
    
    char temp[MAX_LOOP_COMMANDS];
    strncpy(temp, parsed.loopCommands, MAX_LOOP_COMMANDS - 1);
    temp[MAX_LOOP_COMMANDS - 1] = '\0';
    
    // Собираем все команды в массив
    char* commands[10]; // Максимум 10 команд
    int commandCount = 0;
    
    char* cmd = temp;
    char* start = cmd;
    
    // Разбиваем строку по ';'
    while (*cmd && commandCount < 10) {
        if (*cmd == ';') {
            *cmd = '\0';
            // Удаляем пробелы в начале команды
            while (*start == ' ') start++;
            if (*start) {
                commands[commandCount++] = start;
            }
            start = cmd + 1;
        }
        cmd++;
    }
    
    // Добавляем последнюю команду
    if (start < cmd && *start && commandCount < 10) {
        while (*start == ' ') start++;
        commands[commandCount++] = start;
    }
    
    // Если только одна команда - не чередующаяся
    if (commandCount <= 1) {
        Serial.println("Single command - NOT alternating");
        return false;
    }
    
    // Анализируем все команды
    Serial.print("Found commands: ");
    for (int i = 0; i < commandCount; i++) {
        Serial.print("'"); Serial.print(commands[i]); Serial.print("' ");
    }
    Serial.println();
    
    // Проверяем есть ли хотя бы две разные команды
    bool hasAlternation = false;
    for (int i = 0; i < commandCount - 1; i++) {
        for (int j = i + 1; j < commandCount; j++) {
            if (strcmp(commands[i], commands[j]) != 0) {
                Serial.print("Different commands found: '");
                Serial.print(commands[i]); Serial.print("' != '");
                Serial.print(commands[j]); Serial.println("'");
                hasAlternation = true;
                break;
            }
        }
        if (hasAlternation) break;
    }
    
    if (!hasAlternation) {
        Serial.println("All commands are SAME - NOT alternating");
    }
    
    return hasAlternation;
}

// НОВЫЙ МЕТОД: Выключение всех пинов цикла при выходе
void Rule::executeLoopCommandsOff() {
    if (!parsed.loopCommands[0]) return;
    
    Serial.println("Exiting loop - turning OFF pins");
    
    char temp[MAX_LOOP_COMMANDS];
    strncpy(temp, parsed.loopCommands, MAX_LOOP_COMMANDS - 1);
    temp[MAX_LOOP_COMMANDS - 1] = '\0';
    
    char* cmd = temp;
    char* start = cmd;
    
    while (*cmd) {
        if (*cmd == ';') {
            *cmd = '\0';
            executeLoopCommandOff(start);
            start = cmd + 1;
        }
        cmd++;
    }
    
    if (start < cmd && *start) {
        executeLoopCommandOff(start);
    }
}

// НОВЫЙ МЕТОД: Выключение одной команды цикла
void Rule::executeLoopCommandOff(const char* command) {
    if (!command || !*command) return;
    
    char* comma = strchr((char*)command, ',');
    if (!comma || comma == command) return;
    
    int pinLen = comma - command;
    if (pinLen <= 0 || pinLen > 2) return;
    
    char pinStr[4];
    strncpy(pinStr, command, pinLen);
    pinStr[pinLen] = '\0';
    
    int pin = atoi(pinStr);
    if (!isPinValid(pin) || !isOutputPin(pin)) return;
    
    // Устанавливаем пин в LOW при выходе из цикла
    _eglang.setPinOutput(pin, 0);
}

// БАГ-ФИХ: Улучшенное чтение пинов с защитой от наводок (перенесено в Controller)
bool Rule::readPinStable(byte pin) {
    return _eglang.readPinStable(pin);
}

bool Rule::check() {
    if (!parsed.valid) return false;
    
    // Обработка циклов - ИСПРАВЛЕННАЯ ЛОГИКА
    if (parsed.isLoop) {
        if (!parsed.inLoop) {
            if (_eglang.readPinStable(parsed.loopPin)) {
                parsed.inLoop = true;
                executeLoopCommands(); // Выполняем команды при входе в цикл
            }
            return false;
        } else {
            if (_eglang.readPinStable(parsed.loopPin)) {
                // ИСПРАВЛЕНИЕ: Проверяем есть ли разные команды в цикле
                if (hasAlternatingCommands()) {
                    // Для команд типа [3:8,1;8,0] - выполняем постоянно
                    executeLoopCommands();
                }
                // Для команд типа [3:8,1] - НЕ выполняем повторно
                return false;
            } else {
                parsed.inLoop = false;
                executeLoopCommandsOff(); // Выключаем при выходе
                done = true;
                return true;
            }
        }
    }
    
    // Простые команды (выполняются один раз)
    if (parsed.isSimpleCommand) {
        if (done) return false;
        done = true;
        _eglang.setPinOutput(parsed.action, parsed.aState);
        return true;
    }
    
    // Условные правила
    if (parsed.isContinuous) {
        bool condition1 = (_eglang.readPinStable(parsed.trigger1) == (parsed.tState1 == 1));
        bool condition2 = true;
        
        if (parsed.useAND) {
            condition2 = (_eglang.readPinStable(parsed.trigger2) == (parsed.tState2 == 1));
        }
        
        if (condition1 && condition2) {
            _eglang.setPinOutput(parsed.action, parsed.aState);
            return true;
        } else {
            if (parsed.aState == 1) {
                _eglang.setPinOutput(parsed.action, 0);
            }
            return false;
        }
    }
    
    return false;
}

void Rule::reset() {
    done = false;
    parsed.inLoop = false;
}
