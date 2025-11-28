#include "ButtonLayoutScreen.h"
#include "buttonlayouts.h"
#include "drivermanager.h"
#include "drivers/ps4/PS4Driver.h"
#include "drivers/xbone/XBOneDriver.h"
#include "drivers/xinput/XInputDriver.h"
#include "drivers/p5general/P5GeneralDriver.h"  // si tu repo oficial lo trae
//#include "gp_link.h"

void ButtonLayoutScreen::init() {
    isInputHistoryEnabled = Storage::getInstance().getDisplayOptions().inputHistoryEnabled;
    inputHistoryX = Storage::getInstance().getDisplayOptions().inputHistoryRow;
    inputHistoryY = Storage::getInstance().getDisplayOptions().inputHistoryCol;
    inputHistoryLength = Storage::getInstance().getDisplayOptions().inputHistoryLength;
    bannerDelayStart = getMillis();
    gamepad = Storage::getInstance().GetGamepad();
    inputMode = DriverManager::getInstance().getInputMode();

    EventManager::getInstance().registerEventHandler(GP_EVENT_PROFILE_CHANGE, GPEVENT_CALLBACK(this->handleProfileChange(event)));
    EventManager::getInstance().registerEventHandler(GP_EVENT_USBHOST_MOUNT, GPEVENT_CALLBACK(this->handleUSB(event)));
    EventManager::getInstance().registerEventHandler(GP_EVENT_USBHOST_UNMOUNT, GPEVENT_CALLBACK(this->handleUSB(event)));
    
    footer = "";
    historyString = "";
    inputHistory.clear();

    setViewport((isInputHistoryEnabled ? 8 : 0), 0, (isInputHistoryEnabled ? 56 : getRenderer()->getDriver()->getMetrics()->height), getRenderer()->getDriver()->getMetrics()->width);

	// load layout (drawElement pushes element to the display list)
    uint16_t elementCtr = 0;
    LayoutManager::LayoutList currLayoutLeft = LayoutManager::getInstance().getLayoutA();
    LayoutManager::LayoutList currLayoutRight = LayoutManager::getInstance().getLayoutB();
    for (elementCtr = 0; elementCtr < currLayoutLeft.size(); elementCtr++) {
        pushElement(currLayoutLeft[elementCtr]);
    }
    for (elementCtr = 0; elementCtr < currLayoutRight.size(); elementCtr++) {
        pushElement(currLayoutRight[elementCtr]);
    }

	// start with profile mode displayed
	bannerDisplay = true;
    prevProfileNumber = -1;

    prevLayoutLeft = Storage::getInstance().getDisplayOptions().buttonLayout;
    prevLayoutRight = Storage::getInstance().getDisplayOptions().buttonLayoutRight;
    prevLeftOptions = Storage::getInstance().getDisplayOptions().buttonLayoutCustomOptions.paramsLeft;
    prevRightOptions = Storage::getInstance().getDisplayOptions().buttonLayoutCustomOptions.paramsRight;
    prevOrientation = Storage::getInstance().getDisplayOptions().buttonLayoutOrientation;

    // we cannot look at macro options enabled, pull the pins
    
    // macro display now uses our pin functions, so we need to check if pins are enabled...
    macroEnabled = false;
    hasTurboAssigned = false;
    // Macro Button initialized by void Gamepad::setup()
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch( pinMappings[pin].action ) {
            case GpioAction::BUTTON_PRESS_MACRO:
            case GpioAction::BUTTON_PRESS_MACRO_1:
            case GpioAction::BUTTON_PRESS_MACRO_2:
            case GpioAction::BUTTON_PRESS_MACRO_3:
            case GpioAction::BUTTON_PRESS_MACRO_4:
            case GpioAction::BUTTON_PRESS_MACRO_5:
            case GpioAction::BUTTON_PRESS_MACRO_6:
                macroEnabled = true;
                break;
            case GpioAction::BUTTON_PRESS_TURBO:
                hasTurboAssigned = true;
                break;
            default:
                break;
        }
    }

    // determine which fields will be displayed on the status bar
    showInputMode = Storage::getInstance().getDisplayOptions().inputMode;
    showTurboMode = Storage::getInstance().getDisplayOptions().turboMode && hasTurboAssigned;
    showDpadMode = Storage::getInstance().getDisplayOptions().dpadMode;
    showSocdMode = Storage::getInstance().getDisplayOptions().socdMode;
    showMacroMode = Storage::getInstance().getDisplayOptions().macroMode;
    showProfileMode = Storage::getInstance().getDisplayOptions().profileMode;

    getRenderer()->clearScreen();
}

void ButtonLayoutScreen::shutdown() {
    clearElements();

    EventManager::getInstance().unregisterEventHandler(GP_EVENT_PROFILE_CHANGE, GPEVENT_CALLBACK(this->handleProfileChange(event)));
    EventManager::getInstance().unregisterEventHandler(GP_EVENT_USBHOST_MOUNT, GPEVENT_CALLBACK(this->handleUSB(event)));
    EventManager::getInstance().unregisterEventHandler(GP_EVENT_USBHOST_UNMOUNT, GPEVENT_CALLBACK(this->handleUSB(event)));
}

int8_t ButtonLayoutScreen::update() {
        // DEBUG: ver si esta pantalla se ejecuta
    // static uint32_t lastDebug = 0;
    // uint32_t now = getMillis();
    // if (now - lastDebug > 500) { // cada 500 ms aprox
    //     const char* dbg = "UPDT";
    //     GpLink_SendHeader(dbg, 4); // A7 "UPDT"
    //     lastDebug = now;
    // }

    bool configMode = DriverManager::getInstance().isConfigMode();
    uint8_t profileNumber = getGamepad()->getOptions().profileNumber;
    
    // Check if we've updated button layouts while in config mode
    if (configMode) {
        uint8_t layoutLeft = Storage::getInstance().getDisplayOptions().buttonLayout;
        uint8_t layoutRight = Storage::getInstance().getDisplayOptions().buttonLayoutRight;
        uint8_t buttonLayoutOrientation = Storage::getInstance().getDisplayOptions().buttonLayoutOrientation;
        bool inputHistoryEnabled = Storage::getInstance().getDisplayOptions().inputHistoryEnabled;
        if ((prevLayoutLeft != layoutLeft) || (prevLayoutRight != layoutRight) || (isInputHistoryEnabled != inputHistoryEnabled) || compareCustomLayouts() || (prevOrientation != buttonLayoutOrientation)) {
            shutdown();
            init();
        }
    }

    // main logic loop
    if (prevProfileNumber != profileNumber) {
        bannerDelayStart = getMillis();
        prevProfileNumber = profileNumber;
        bannerDisplay = true;
    }

    // main logic loop
	generateHeader();
    if (isInputHistoryEnabled)
		processInputHistory();

    // check for exit/screen change
    if (DriverManager::getInstance().isConfigMode()) {
        uint16_t buttonState = getGamepad()->state.buttons;
        if (prevButtonState && !buttonState) {
            if (prevButtonState == GAMEPAD_MASK_B1) {
                prevButtonState = 0;
                return DisplayMode::CONFIG_INSTRUCTION;
            }
        }
        prevButtonState = buttonState;
    }

	return -1;
}

void ButtonLayoutScreen::generateHeader() {
    statusBar.clear();
    Storage& storage = Storage::getInstance();

    bool useBanner = false;

    // --- LÓGICA DE BANNER SIN RETURN TEMPRANO ---
    if (bannerDisplay) {
        if (((getMillis() - bannerDelayStart) / 1000) < bannerDelay) {
            // Mientras dure el banner, usamos statusBar para el banner
            useBanner = true;

            if (bannerMessage.empty()) {
                statusBar.assign(storage.currentProfileLabel(), strlen(storage.currentProfileLabel()));
                if (statusBar.empty()) {
                    statusBar = "     Profile #";
                    statusBar += std::to_string(getGamepad()->getOptions().profileNumber);
                } else {
                    statusBar.insert(statusBar.begin(), (21 - statusBar.length()) / 2, ' ');
                }
            } else {
                statusBar = bannerMessage;
            }
        } else {
            // Se acabó el banner, volvemos al header normal
            bannerDisplay = false;
            bannerMessage.clear();
        }
    }

    // --- HEADER NORMAL SOLO SI NO ESTAMOS USANDO BANNER ---
    if (!useBanner) {
        if (showInputMode) {
            switch (inputMode)
            {
                case INPUT_MODE_PS3:          statusBar += "PS3";    break;
                case INPUT_MODE_GENERIC:      statusBar += "USBHID"; break;
                case INPUT_MODE_SWITCH:       statusBar += "SWITCH"; break;
                case INPUT_MODE_MDMINI:       statusBar += "GEN/MD"; break;
                case INPUT_MODE_NEOGEO:       statusBar += "NGMINI"; break;
                case INPUT_MODE_PCEMINI:      statusBar += "PCE/TG"; break;
                case INPUT_MODE_EGRET:        statusBar += "EGRET";  break;
                case INPUT_MODE_ASTRO:        statusBar += "ASTRO";  break;
                case INPUT_MODE_PSCLASSIC:    statusBar += "PSC";    break;
                case INPUT_MODE_XBOXORIGINAL: statusBar += "OGXBOX"; break;
                case INPUT_MODE_SWITCH_PRO:   statusBar += "SWPRO";  break;

                case INPUT_MODE_PS4: {
                    statusBar += "PS4";
                    if (((PS4Driver*)DriverManager::getInstance().getDriver())->getAuthSent())
                        statusBar += ":AS";
                    else
                        statusBar += "   ";
                    break;
                }

                case INPUT_MODE_PS5: {
                    statusBar += "PS5";
                    if (((PS4Driver*)DriverManager::getInstance().getDriver())->getAuthSent())
                        statusBar += ":AS";
                    else
                        statusBar += "   ";
                    break;
                }

#ifdef INPUT_MODE_P5GENERAL
                case INPUT_MODE_P5GENERAL: {
                    statusBar += "P5G";
                    if (((P5GeneralDriver*)DriverManager::getInstance().getDriver())->getAuthSent())
                        statusBar += ":AS";
                    else
                        statusBar += "   ";
                    break;
                }
#endif

                case INPUT_MODE_XBONE: {
                    statusBar += "XBON";
                    if (((XBOneDriver*)DriverManager::getInstance().getDriver())->getAuthSent())
                        statusBar += "E";
                    else
                        statusBar += "*";
                    break;
                }

                case INPUT_MODE_XINPUT: {
                    statusBar += "X";
                    if (((XInputDriver*)DriverManager::getInstance().getDriver())->getAuthSent())
                        statusBar += "B360";
                    else
                        statusBar += "INPUT";
                    break;
                }

                case INPUT_MODE_KEYBOARD:     statusBar += "HID-KB"; break;
                case INPUT_MODE_CONFIG:       statusBar += "CONFIG"; break;
            }
        }

        // TURBO
        if (showTurboMode) {
            const TurboOptions& turboOptions = storage.getAddonOptions().turboOptions;
            if (turboOptions.enabled) {
                statusBar += " T";
                if (turboOptions.shotCount < 10)
                    statusBar += "0";
                statusBar += std::to_string(turboOptions.shotCount);
            } else {
                statusBar += "    ";
            }
        }

        const GamepadOptions& options = gamepad->getOptions();

        // DPAD MODE
        if (showDpadMode) {
            switch (gamepad->getActiveDpadMode())
            {
                case DPAD_MODE_DIGITAL:      statusBar += " D"; break;
                case DPAD_MODE_LEFT_ANALOG:  statusBar += " L"; break;
                case DPAD_MODE_RIGHT_ANALOG: statusBar += " R"; break;
            }
        }

        // SOCD MODE
        if (showSocdMode) {
            switch (Gamepad::resolveSOCDMode(options))
            {
                case SOCD_MODE_NEUTRAL:               statusBar += " SOCD-N"; break;
                case SOCD_MODE_UP_PRIORITY:           statusBar += " SOCD-U"; break;
                case SOCD_MODE_SECOND_INPUT_PRIORITY: statusBar += " SOCD-L"; break;
                case SOCD_MODE_FIRST_INPUT_PRIORITY:  statusBar += " SOCD-F"; break;
                case SOCD_MODE_BYPASS:                statusBar += " SOCD-X"; break;
            }
        }

        // MACRO
        if (showMacroMode && macroEnabled)
            statusBar += " M";

        // PROFILE
        if (showProfileMode) {
            statusBar += " ";

            std::string profile;
            profile.assign(storage.currentProfileLabel(), strlen(storage.currentProfileLabel()));
            if (profile.empty()) {
                statusBar += std::to_string(options.profileNumber);
            } else {
                statusBar += profile;
            }
        }
    }

    trim(statusBar);

    // ------------------------------------------------------------------
    // SIEMPRE llegamos acá ⇒ siempre se envía STATUS + HEADER por UART
    // ------------------------------------------------------------------

//     const GamepadOptions& options = gamepad->getOptions();
//     uint8_t socd = (uint8_t)Gamepad::resolveSOCDMode(options);

//     uint8_t authMode = 0;
//     switch (inputMode)
//     {
//         case INPUT_MODE_PS4:
//         case INPUT_MODE_PS5:
//             if (((PS4Driver*)DriverManager::getInstance().getDriver())->getAuthSent())
//                 authMode = 1;
//             break;

//         case INPUT_MODE_XBONE:
//             if (((XBOneDriver*)DriverManager::getInstance().getDriver())->getAuthSent())
//                 authMode = 1;
//             break;

//         case INPUT_MODE_XINPUT:
//             if (((XInputDriver*)DriverManager::getInstance().getDriver())->getAuthSent())
//                 authMode = 1;
//             break;

// #ifdef INPUT_MODE_P5GENERAL
//         case INPUT_MODE_P5GENERAL:
//             if (((P5GeneralDriver*)DriverManager::getInstance().getDriver())->getAuthSent())
//                 authMode = 1;
//             break;
// #endif

//         default:
//             authMode = 0;
//             break;
//     }

//     // STATUS (A5)
//     GpLink_UpdateStatus(
//         static_cast<uint8_t>(inputMode),
//         socd,
//         authMode
//     );

//     // HEADER (A7): aunque sea banner, lo mandamos tal cual está en statusBar
//     if (!statusBar.empty()) {
//         GpLink_SendHeader(
//             statusBar.c_str(),
//             static_cast<uint8_t>(statusBar.size())
//         );
//    }
}


void ButtonLayoutScreen::drawScreen() {
    if (bannerDisplay) {
        getRenderer()->drawRectangle(0, 0, 128, 7, true, true);
    	getRenderer()->drawText(0, 0, statusBar, true);
    } else {
		getRenderer()->drawText(0, 0, statusBar);
	}
    getRenderer()->drawText(0, 7, footer);
}

GPLever* ButtonLayoutScreen::addLever(uint16_t startX, uint16_t startY, uint16_t sizeX, uint16_t sizeY, uint16_t strokeColor, uint16_t fillColor, uint16_t inputType) {
    GPLever* lever = new GPLever();
    lever->setRenderer(getRenderer());
    lever->setPosition(startX, startY);
    lever->setStrokeColor(strokeColor);
    lever->setFillColor(fillColor);
    lever->setRadius(sizeX);
    lever->setInputType(inputType);
    lever->setViewport(this->getViewport());
    return (GPLever*)addElement(lever);
}

GPButton* ButtonLayoutScreen::addButton(uint16_t startX, uint16_t startY, uint16_t sizeX, uint16_t sizeY, uint16_t strokeColor, uint16_t fillColor, int16_t inputMask) {
    GPButton* button = new GPButton();
    button->setRenderer(getRenderer());
    button->setPosition(startX, startY);
    button->setStrokeColor(strokeColor);
    button->setFillColor(fillColor);
    button->setSize(sizeX, sizeY);
    button->setInputMask(inputMask);
    button->setViewport(this->getViewport());
    return (GPButton*)addElement(button);
}

GPShape* ButtonLayoutScreen::addShape(uint16_t startX, uint16_t startY, uint16_t sizeX, uint16_t sizeY, uint16_t strokeColor, uint16_t fillColor) {
    GPShape* shape = new GPShape();
    shape->setRenderer(getRenderer());
    shape->setPosition(startX, startY);
    shape->setStrokeColor(strokeColor);
    shape->setFillColor(fillColor);
    shape->setSize(sizeX,sizeY);
    shape->setViewport(this->getViewport());
    return (GPShape*)addElement(shape);
}

GPSprite* ButtonLayoutScreen::addSprite(uint16_t startX, uint16_t startY, uint16_t sizeX, uint16_t sizeY) {
    GPSprite* sprite = new GPSprite();
    sprite->setRenderer(getRenderer());
    sprite->setPosition(startX, startY);
    sprite->setSize(sizeX,sizeY);
    sprite->setViewport(this->getViewport());
    return (GPSprite*)addElement(sprite);
}

GPWidget* ButtonLayoutScreen::pushElement(GPButtonLayout element) {
    if (element.elementType == GP_ELEMENT_LEVER) {
        return addLever(element.parameters.x1, element.parameters.y1, element.parameters.x2, element.parameters.y2, element.parameters.stroke, element.parameters.fill, element.parameters.value);
    } else if ((element.elementType == GP_ELEMENT_BTN_BUTTON) || (element.elementType == GP_ELEMENT_DIR_BUTTON) || (element.elementType == GP_ELEMENT_PIN_BUTTON)) {
        GPButton* button = addButton(element.parameters.x1, element.parameters.y1, element.parameters.x2, element.parameters.y2, element.parameters.stroke, element.parameters.fill, element.parameters.value);

        // set type of button
        button->setInputType(element.elementType);
        button->setInputDirection(false);
        button->setShape((GPShape_Type)element.parameters.shape);
        button->setAngle(element.parameters.angleStart);
        button->setAngleEnd(element.parameters.angleEnd);
        button->setClosed(element.parameters.closed);

        if (element.elementType == GP_ELEMENT_DIR_BUTTON) button->setInputDirection(true);

        return (GPWidget*)button;
    } else if (element.elementType == GP_ELEMENT_SPRITE) {
        return addSprite(element.parameters.x1, element.parameters.y1, element.parameters.x2, element.parameters.y2);
    } else if (element.elementType == GP_ELEMENT_SHAPE) {
        GPShape* shape = addShape(element.parameters.x1, element.parameters.y1, element.parameters.x2, element.parameters.y2, element.parameters.stroke, element.parameters.fill);
        shape->setShape((GPShape_Type)element.parameters.shape);
        shape->setAngle(element.parameters.angleStart);
        shape->setAngleEnd(element.parameters.angleEnd);
        shape->setClosed(element.parameters.closed);
        return shape;
    }
    return NULL;
}

void ButtonLayoutScreen::processInputHistory() {
    std::deque<std::string> pressed;

    // Obtener estados de teclas
    std::array<bool, INPUT_HISTORY_MAX_INPUTS> currentInput = {

        pressedUp(),
        pressedDown(),
        pressedLeft(),
        pressedRight(),

        pressedUpLeft(),
        pressedUpRight(),
        pressedDownLeft(),
        pressedDownRight(),

        getProcessedGamepad()->pressedB1(),
        getProcessedGamepad()->pressedB2(),
        getProcessedGamepad()->pressedB3(),
        getProcessedGamepad()->pressedB4(),
        getProcessedGamepad()->pressedL1(),
        getProcessedGamepad()->pressedR1(),
        getProcessedGamepad()->pressedL2(),
        getProcessedGamepad()->pressedR2(),
        getProcessedGamepad()->pressedS1(),
        getProcessedGamepad()->pressedS2(),
        getProcessedGamepad()->pressedL3(),
        getProcessedGamepad()->pressedR3(),
        getProcessedGamepad()->pressedA1(),
        getProcessedGamepad()->pressedA2(),
    };

    uint8_t mode = ((displayModeLookup.count(inputMode) > 0) ? displayModeLookup.at(inputMode) : 0);

    // Ver si hubo nuevas teclas presionadas
    if (lastInput != currentInput) {
        for (uint8_t x = 0; x < INPUT_HISTORY_MAX_INPUTS; x++) {
            std::string inputChar(displayNames[mode][x]);
            if (currentInput[x] && (inputChar != "")) {
                pressed.push_back(inputChar);
            }
        }
        lastInput = currentInput;
    }

    if (pressed.size() > 0) {
        std::string newInput;
        for (const auto& s : pressed) {
            if (!newInput.empty())
                newInput += "+";
            newInput += s;
        }

        inputHistory.push_back(newInput);
    }

    if (inputHistory.size() > (inputHistoryLength / 2) + 1) {
        inputHistory.pop_front();
    }

    std::string ret;

    for (auto it = inputHistory.crbegin(); it != inputHistory.crend(); ++it) {
        std::string newRet = ret;
        if (!newRet.empty())
            newRet = " " + newRet;

        newRet = *it + newRet;
        ret = newRet;

        if (ret.size() >= inputHistoryLength) {
            break;
        }
    }

    if (ret.size() >= inputHistoryLength) {
        historyString = ret.substr(ret.size() - inputHistoryLength);
    } else {
        historyString = ret;
    }

    footer = historyString;

    // Enviar input history al RP2040 (A6)
    // if (!historyString.empty()) {
    //     GpLink_SendHistory(
    //         historyString.c_str(),
    //         static_cast<uint8_t>(historyString.size())
    //     );
    // }
}


bool ButtonLayoutScreen::compareCustomLayouts()
{
    ButtonLayoutParamsLeft leftOptions = Storage::getInstance().getDisplayOptions().buttonLayoutCustomOptions.paramsLeft;
    ButtonLayoutParamsRight rightOptions = Storage::getInstance().getDisplayOptions().buttonLayoutCustomOptions.paramsRight;

    bool leftChanged = ((leftOptions.layout != prevLeftOptions.layout) || (leftOptions.common.startX != prevLeftOptions.common.startX) || (leftOptions.common.startY != prevLeftOptions.common.startY) || (leftOptions.common.buttonPadding != prevLeftOptions.common.buttonPadding) || (leftOptions.common.buttonRadius != prevLeftOptions.common.buttonRadius));
    bool rightChanged = ((rightOptions.layout != prevRightOptions.layout) || (rightOptions.common.startX != prevRightOptions.common.startX) || (rightOptions.common.startY != prevRightOptions.common.startY) || (rightOptions.common.buttonPadding != prevRightOptions.common.buttonPadding) || (rightOptions.common.buttonRadius != prevRightOptions.common.buttonRadius));
    
    return (leftChanged || rightChanged);
}

bool ButtonLayoutScreen::pressedUp()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == GAMEPAD_MASK_UP);
        case DPAD_MODE_LEFT_ANALOG:  return getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MIN;
        case DPAD_MODE_RIGHT_ANALOG: return getProcessedGamepad()->state.ry == GAMEPAD_JOYSTICK_MIN;
    }

    return false;
}

bool ButtonLayoutScreen::pressedDown()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == GAMEPAD_MASK_DOWN);
        case DPAD_MODE_LEFT_ANALOG:  return getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MAX;
        case DPAD_MODE_RIGHT_ANALOG: return getProcessedGamepad()->state.ry == GAMEPAD_JOYSTICK_MAX;
    }

    return false;
}

bool ButtonLayoutScreen::pressedLeft()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == GAMEPAD_MASK_LEFT);
        case DPAD_MODE_LEFT_ANALOG:  return getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MIN;
        case DPAD_MODE_RIGHT_ANALOG: return getProcessedGamepad()->state.rx == GAMEPAD_JOYSTICK_MIN;
    }

    return false;
}

bool ButtonLayoutScreen::pressedRight()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == GAMEPAD_MASK_RIGHT);
        case DPAD_MODE_LEFT_ANALOG:  return getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MAX;
        case DPAD_MODE_RIGHT_ANALOG: return getProcessedGamepad()->state.rx == GAMEPAD_JOYSTICK_MAX;
    }

    return false;
}

bool ButtonLayoutScreen::pressedUpLeft()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == (GAMEPAD_MASK_UP | GAMEPAD_MASK_LEFT));
        case DPAD_MODE_LEFT_ANALOG:  return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MIN) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MIN);
        case DPAD_MODE_RIGHT_ANALOG: return (getProcessedGamepad()->state.rx == GAMEPAD_JOYSTICK_MIN) && (getProcessedGamepad()->state.ry == GAMEPAD_JOYSTICK_MIN);
    }

    return false;
}

bool ButtonLayoutScreen::pressedUpRight()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == (GAMEPAD_MASK_UP | GAMEPAD_MASK_RIGHT));
        case DPAD_MODE_LEFT_ANALOG:  return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MAX) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MIN);
        case DPAD_MODE_RIGHT_ANALOG: return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MAX) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MIN);
    }

    return false;
}

bool ButtonLayoutScreen::pressedDownLeft()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == (GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT));
        case DPAD_MODE_LEFT_ANALOG:  return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MIN) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MAX);
        case DPAD_MODE_RIGHT_ANALOG: return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MIN) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MAX);
    }

    return false;
}

bool ButtonLayoutScreen::pressedDownRight()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == (GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT));
        case DPAD_MODE_LEFT_ANALOG:  return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MAX) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MAX);
        case DPAD_MODE_RIGHT_ANALOG: return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MAX) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MAX);
    }

    return false;
}

void ButtonLayoutScreen::handleProfileChange(GPEvent* e) {
    GPProfileChangeEvent* event = (GPProfileChangeEvent*)e;

    profileNumber = event->currentValue;
    prevProfileNumber = event->previousValue;
}

void ButtonLayoutScreen::handleUSB(GPEvent* e) {
    GPUSBHostEvent* event = (GPUSBHostEvent*)e;
    bannerDelayStart = getMillis();
    prevProfileNumber = profileNumber;

    if (e->eventType() == GP_EVENT_USBHOST_MOUNT) {
        bannerMessage = "    USB Connected";
    } else if (e->eventType() == GP_EVENT_USBHOST_UNMOUNT) {
        bannerMessage = "  USB Disconnnected";
    }
    bannerDisplay = true;
}

void ButtonLayoutScreen::trim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            std::not1(std::ptr_fun<int, int>(std::isspace))));
}