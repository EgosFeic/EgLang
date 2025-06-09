#ifndef EGLANG_H
#define EGLANG_H

#include <Arduino.h>

// Максимальное количество правил (уменьшено для экономии памяти)
#define MAX_RULES 20
#define MAX_RULE_LENGTH 32
#define MAX_LOOP_COMMANDS 24

// Конфигурация пинов (в PROGMEM для экономии SRAM)
extern const byte inputs[] PROGMEM;
extern const byte outputs[] PROGMEM;

// Компактная структура правила
struct Rule {
    char ruleText[MAX_RULE_LENGTH];  // Фиксированный размер вместо String
    bool done : 1;                   // Битовое поле
    
    struct ParsedRule {
        byte trigger1 : 4;           // 4 бита для пина (0-15)
        byte tState1 : 1;            // 1 бит для состояния
        byte trigger2 : 4;           // 4 бита для пина
        byte tState2 : 1;            // 1 бит для состояния
        byte action : 4;             // 4 бита для пина
        byte aState : 1;             // 1 бит для состояния
        byte useAND : 1;             // 1 бит
        byte isSimpleCommand : 1;    // 1 бит
        byte isLoop : 1;             // 1 бит
        byte isContinuous : 1;       // 1 бит - для непрерывных условий
        byte loopPin : 4;            // 4 бита для пина
        byte inLoop : 1;             // 1 бит
        byte valid : 1;              // 1 бит
        char loopCommands[MAX_LOOP_COMMANDS]; // Фиксированный размер
    } parsed;
    
    Rule();
    void setRule(const char* text);
    void parseRule();
    bool check();
    void reset();
    
private:
    void parseLoop();
    void parseSimpleCommand();
    void parseConditionalRule();
    bool isPinValid(byte pin);
    bool isInputPin(byte pin);
    bool isOutputPin(byte pin);
    bool validateLoopCommands(const char* commands);
    bool validateSingleLoopCommand(const char* command);
    bool parseAndCondition(const char* condition, char* ampersand);
    bool parseSimpleCondition(const char* condition);
    bool executeLoopCommand(const char* command);
    void executeLoopCommands();
    void executeLoopCommandsOff();       // Новый метод для выключения пинов цикла
    void executeLoopCommandOff(const char* command); // Выключение одной команды
    bool hasAlternatingCommands();       // Проверка наличия чередующихся команд
    bool readPinStable(byte pin);
};

// Компактный контроллер
class EgLangController {
public:
    Rule rules[MAX_RULES];
    byte count : 6;              // 6 бит для счетчика (до 63)
    byte currentRule : 6;        // 6 бит для текущего правила
    bool initialized : 1;        // 1 бит
    
    // Хранение последних состояний OUTPUT пинов
    struct PinState {
        byte pin : 4;            // 4 бита для номера пина
        byte state : 1;          // 1 бит для состояния (0/1)
        byte isOutput : 1;       // 1 бит - настроен ли как OUTPUT
    };
    PinState pinStates[6];       // Состояния для 6 OUTPUT пинов
    
    void init();
    bool add(const char* rule);
    void run();
    void reset();
    void shutdown();             // Новый метод для завершения программы
    bool readPinStable(byte pin); // Перенесено из Rule для общего использования
    void setPinOutput(byte pin, byte state); // ПЕРЕНЕСЕНО В PUBLIC
    
private:
    void resetPinsToHighZ();
    void updatePinState(byte pin, byte state);
    void checkAndResetInactivePins(); // Новый метод для сброса неактивных пинов
};

extern EgLangController _eglang;

// Макросы (без изменений)
#define AUTO_START \
    void _user_rules(); \
    void setup() { \
        _eglang.init(); \
        _user_rules(); \
    } \
    void loop() { \
        _eglang.run(); \
        delay(50); \
    } \
    void _user_rules() {

#define AUTO_END }

// Оптимизированный макрос R
#define R(rule) _eglang.add(rule);

// Компактные глобальные функции
void addRule(const char* rule);
void processRules();
void shutdownEgLang();           // Новая функция для завершения

// Убираны избыточные функции для экономии памяти:
// - showRules() 
// - printHelp()
// Они занимают много Flash памяти строковыми константами

#endif
