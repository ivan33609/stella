//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2017 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#include "bspf.hxx"

#include "OSystem.hxx"
#include "Joystick.hxx"
#include "Paddles.hxx"
#include "PointingDevice.hxx"
#include "Settings.hxx"
#include "EventMappingWidget.hxx"
#include "EditTextWidget.hxx"
#include "PopUpWidget.hxx"
#include "TabWidget.hxx"
#include "Widget.hxx"

#include "InputDialog.hxx"


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
InputDialog::InputDialog(OSystem& osystem, DialogContainer& parent,
                         const GUI::Font& font, int max_w, int max_h)
  : Dialog(osystem, parent)
{
  const int lineHeight   = font.getLineHeight(),
            fontWidth    = font.getMaxCharWidth(),
            buttonWidth  = font.getStringWidth("Defaults") + 20,
            buttonHeight = font.getLineHeight() + 4;
  const int vBorder = 4;
  int xpos, ypos, tabID;
  StringList actions;

  // Set real dimensions
  _w = std::min(50 * fontWidth + 10, max_w);
  _h = std::min(16 * (lineHeight + 4) + 14, max_h);

  // The tab widget
  xpos = 2; ypos = vBorder;
  myTab = new TabWidget(this, font, xpos, ypos, _w - 2*xpos, _h - buttonHeight - 20);
  addTabWidget(myTab);

  // 1) Event mapper for emulation actions
  tabID = myTab->addTab("Emul. Events");
  actions = instance().eventHandler().getActionList(kEmulationMode);
  myEmulEventMapper = new EventMappingWidget(myTab, font, 2, 2,
                                             myTab->getWidth(),
                                             myTab->getHeight() - ypos,
                                             actions, kEmulationMode);
  myTab->setParentWidget(tabID, myEmulEventMapper);
  addToFocusList(myEmulEventMapper->getFocusList(), myTab, tabID);

  // 2) Event mapper for UI actions
  tabID = myTab->addTab("UI Events");
  actions = instance().eventHandler().getActionList(kMenuMode);
  myMenuEventMapper = new EventMappingWidget(myTab, font, 2, 2,
                                             myTab->getWidth(),
                                             myTab->getHeight() - ypos,
                                             actions, kMenuMode);
  myTab->setParentWidget(tabID, myMenuEventMapper);
  addToFocusList(myMenuEventMapper->getFocusList(), myTab, tabID);

  // 3) Devices & ports
  addDevicePortTab(font);

  // Finalize the tabs, and activate the first tab
  myTab->activateTabs();
  myTab->setActiveTab(0);

  // Add Defaults, OK and Cancel buttons
  WidgetArray wid;
  ButtonWidget* b;
  b = new ButtonWidget(this, font, 10, _h - buttonHeight - 10,
                       buttonWidth, buttonHeight, "Defaults", kDefaultsCmd);
  wid.push_back(b);
  addOKCancelBGroup(wid, font);
  addBGroupToFocusList(wid);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void InputDialog::addDevicePortTab(const GUI::Font& font)
{
  const int lineHeight = font.getLineHeight(),
            fontWidth  = font.getMaxCharWidth(),
            fontHeight = font.getFontHeight();
  int xpos, ypos, lwidth, pwidth, tabID;
  WidgetArray wid;
  VariantList items;

  // Devices/ports
  tabID = myTab->addTab("Devices & Ports");

  // Stelladaptor mappings
  xpos = 5;  ypos = 5;
  lwidth = font.getStringWidth("Use mouse as a controller ");
  pwidth = font.getStringWidth("-UI, -Emulation");

  VarList::push_back(items, "Left / Right", "lr");
  VarList::push_back(items, "Right / Left", "rl");
  mySAPort = new PopUpWidget(myTab, font, xpos, ypos, pwidth, lineHeight, items,
                             "Stelladaptor port order ", lwidth);
  wid.push_back(mySAPort);

  // Use mouse as controller
  ypos += lineHeight + 5;
  items.clear();
  VarList::push_back(items, "Always", "always");
  VarList::push_back(items, "Analog devices", "analog");
  VarList::push_back(items, "Never", "never");
  myMouseControl = new PopUpWidget(myTab, font, xpos, ypos, pwidth, lineHeight, items,
                             "Use mouse as a controller ", lwidth);
  wid.push_back(myMouseControl);

  // Mouse cursor state
  ypos += lineHeight + 5;
  items.clear();
  VarList::push_back(items, "-UI, -Emulation", "0");
  VarList::push_back(items, "-UI, +Emulation", "1");
  VarList::push_back(items, "+UI, -Emulation", "2");
  VarList::push_back(items, "+UI, +Emulation", "3");
  myCursorState = new PopUpWidget(myTab, font, xpos, ypos, pwidth, lineHeight, items,
                             "Mouse cursor visibility ", lwidth);
  wid.push_back(myCursorState);
#ifndef WINDOWED_SUPPORT
  myCursorState->clearFlags(WIDGET_ENABLED);
#endif

  // Add AtariVox serial port
  ypos += lineHeight + 5;
  lwidth = font.getStringWidth("AVox serial port ");
  int fwidth = _w - xpos - lwidth - 20;
  new StaticTextWidget(myTab, font, xpos, ypos, lwidth, fontHeight,
                       "AVox serial port ", kTextAlignLeft);
  myAVoxPort = new EditTextWidget(myTab, font, xpos+lwidth, ypos,
                                  fwidth, fontHeight, "");
  wid.push_back(myAVoxPort);

  lwidth = font.getStringWidth("Digital paddle sensitivity ");
  pwidth = font.getMaxCharWidth() * 8;

  // Add joystick deadzone setting
  ypos += lineHeight + 8;
  myDeadzone = new SliderWidget(myTab, font, xpos, ypos, pwidth, lineHeight,
                                "Joystick deadzone size ", lwidth, kDeadzoneChanged);
  myDeadzone->setMinValue(0); myDeadzone->setMaxValue(29);
  xpos += myDeadzone->getWidth() + 5;
  myDeadzoneLabel = new StaticTextWidget(myTab, font, xpos, ypos+1, 5*fontWidth,
                                         lineHeight, "", kTextAlignLeft);
  myDeadzoneLabel->setFlags(WIDGET_CLEARBG);
  wid.push_back(myDeadzone);

  // Add paddle speed (digital emulation)
  xpos = 5;  ypos += lineHeight + 4;
  myDPaddleSpeed = new SliderWidget(myTab, font, xpos, ypos, pwidth, lineHeight,
                                    "Digital paddle sensitivity ",
                                    lwidth, kDPSpeedChanged);
  myDPaddleSpeed->setMinValue(1); myDPaddleSpeed->setMaxValue(20);
  xpos += myDPaddleSpeed->getWidth() + 5;
  myDPaddleLabel = new StaticTextWidget(myTab, font, xpos, ypos+1, 24, lineHeight,
                                        "", kTextAlignLeft);
  myDPaddleLabel->setFlags(WIDGET_CLEARBG);
  wid.push_back(myDPaddleSpeed);

  // Add paddle speed (mouse emulation)
  xpos = 5;  ypos += lineHeight + 4;
  myMPaddleSpeed = new SliderWidget(myTab, font, xpos, ypos, pwidth, lineHeight,
                                    "Mouse paddle sensitivity ",
                                    lwidth, kMPSpeedChanged);
  myMPaddleSpeed->setMinValue(1); myMPaddleSpeed->setMaxValue(20);
  xpos += myMPaddleSpeed->getWidth() + 5;
  myMPaddleLabel = new StaticTextWidget(myTab, font, xpos, ypos+1, 24, lineHeight,
                                        "", kTextAlignLeft);
  myMPaddleSpeed->setFlags(WIDGET_CLEARBG);
  wid.push_back(myMPaddleSpeed);

  // Add trackball speed
  xpos = 5;  ypos += lineHeight + 4;
  myTrackBallSpeed = new SliderWidget(myTab, font, xpos, ypos, pwidth, lineHeight,
                                      "Trackball sensitivity ",
                                      lwidth, kTBSpeedChanged);
  myTrackBallSpeed->setMinValue(1); myTrackBallSpeed->setMaxValue(20);
  xpos += myTrackBallSpeed->getWidth() + 5;
  myTrackBallLabel = new StaticTextWidget(myTab, font, xpos, ypos+1, 24, lineHeight,
                                          "", kTextAlignLeft);
  myTrackBallSpeed->setFlags(WIDGET_CLEARBG);
  wid.push_back(myTrackBallSpeed);

  // Add 'allow all 4 directions' for joystick
  xpos = 10;  ypos += lineHeight + 12;
  myAllowAll4 = new CheckboxWidget(myTab, font, xpos, ypos,
                  "Allow all 4 directions on joystick");
  wid.push_back(myAllowAll4);

  // Grab mouse (in windowed mode)
  ypos += lineHeight + 4;
  myGrabMouse = new CheckboxWidget(myTab, font, xpos, ypos,
	                "Grab mouse in emulation mode");
  wid.push_back(myGrabMouse);
#ifndef WINDOWED_SUPPORT
  myGrabMouse->clearFlags(WIDGET_ENABLED);
#endif

  // Enable/disable control key-combos
  ypos += lineHeight + 4;
  myCtrlCombo = new CheckboxWidget(myTab, font, xpos, ypos,
	                "Use Control key combos");
  wid.push_back(myCtrlCombo);

  // Show joystick database
  xpos += 20;  ypos += lineHeight + 8;
  myJoyDlgButton = new ButtonWidget(myTab, font, xpos, ypos,
    font.getStringWidth("Show Joystick Database") + 20, font.getLineHeight() + 4,
    "Show Joystick Database", kDBButtonPressed);
  wid.push_back(myJoyDlgButton);

  // Add items for virtual device ports
  addToFocusList(wid, myTab, tabID);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void InputDialog::loadConfig()
{
  // Left & right ports
  mySAPort->setSelected(instance().settings().getString("saport"), "lr");

  // Use mouse as a controller
  myMouseControl->setSelected(
    instance().settings().getString("usemouse"), "analog");

  // Mouse cursor state
  myCursorState->setSelected(instance().settings().getString("cursor"), "2");

  // Joystick deadzone
  myDeadzone->setValue(instance().settings().getInt("joydeadzone"));
  myDeadzoneLabel->setValue(Joystick::deadzone());

  // Paddle speed (digital and mouse)
  myDPaddleSpeed->setValue(instance().settings().getInt("dsense"));
  myDPaddleLabel->setLabel(instance().settings().getString("dsense"));
  myMPaddleSpeed->setValue(instance().settings().getInt("msense"));
  myMPaddleLabel->setLabel(instance().settings().getString("msense"));

  // Trackball speed
  myTrackBallSpeed->setValue(instance().settings().getInt("tsense"));
  myTrackBallLabel->setLabel(instance().settings().getString("tsense"));

  // AtariVox serial port
  myAVoxPort->setText(instance().settings().getString("avoxport"));

  // Allow all 4 joystick directions
  myAllowAll4->setState(instance().settings().getBool("joyallow4"));

  // Grab mouse
  myGrabMouse->setState(instance().settings().getBool("grabmouse"));

  // Enable/disable control key-combos
  myCtrlCombo->setState(instance().settings().getBool("ctrlcombo"));

  myTab->loadConfig();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void InputDialog::saveConfig()
{
  // Left & right ports
  instance().eventHandler().mapStelladaptors(mySAPort->getSelectedTag().toString());

  // Use mouse as a controller
  const string& usemouse = myMouseControl->getSelectedTag().toString();
  instance().settings().setValue("usemouse", usemouse);
  instance().eventHandler().setMouseControllerMode(usemouse);

  // Joystick deadzone
  int deadzone = myDeadzone->getValue();
  instance().settings().setValue("joydeadzone", deadzone);
  Joystick::setDeadZone(deadzone);

  // Paddle speed (digital and mouse)
  int sensitivity = myDPaddleSpeed->getValue();
  instance().settings().setValue("dsense", sensitivity);
  Paddles::setDigitalSensitivity(sensitivity);
  sensitivity = myMPaddleSpeed->getValue();
  instance().settings().setValue("msense", sensitivity);
  Paddles::setMouseSensitivity(sensitivity);

  // Trackball speed
  sensitivity = myTrackBallSpeed->getValue();
  instance().settings().setValue("tsense", sensitivity);
  PointingDevice::setSensitivity(sensitivity);

  // AtariVox serial port
  instance().settings().setValue("avoxport", myAVoxPort->getText());

  // Allow all 4 joystick directions
  bool allowall4 = myAllowAll4->getState();
  instance().settings().setValue("joyallow4", allowall4);
  instance().eventHandler().allowAllDirections(allowall4);

  // Grab mouse and hide cursor
  const string& cursor = myCursorState->getSelectedTag().toString();
  instance().settings().setValue("cursor", cursor);
  instance().settings().setValue("grabmouse", myGrabMouse->getState());
  instance().frameBuffer().setCursorState();

  // Enable/disable control key-combos
  instance().settings().setValue("ctrlcombo", myCtrlCombo->getState());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void InputDialog::setDefaults()
{
  switch(myTab->getActiveTab())
  {
    case 0:  // Emulation events
      myEmulEventMapper->setDefaults();
      break;

    case 1:  // UI events
      myMenuEventMapper->setDefaults();
      break;

    case 2:  // Virtual devices
    {
      // Left & right ports
      mySAPort->setSelected("lr");

      // Use mouse as a controller
      myMouseControl->setSelected("analog");

      // Mouse cursor state
      myCursorState->setSelected("2");

      // Joystick deadzone
      myDeadzone->setValue(0);
      myDeadzoneLabel->setValue(3200);

      // Paddle speed (digital and mouse)
      myDPaddleSpeed->setValue(10);
      myDPaddleLabel->setLabel("10");
      myMPaddleSpeed->setValue(10);
      myMPaddleLabel->setLabel("10");
      myTrackBallSpeed->setValue(10);
      myTrackBallLabel->setLabel("10");

      // AtariVox serial port
      myAVoxPort->setText("");

      // Allow all 4 joystick directions
      myAllowAll4->setState(false);

      // Grab mouse
      myGrabMouse->setState(true);

      // Enable/disable control key-combos
      myCtrlCombo->setState(true);

      break;
    }

    default:
      break;
  }

  _dirty = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void InputDialog::handleKeyDown(StellaKey key, StellaMod mod)
{
  // Remap key events in remap mode, otherwise pass to parent dialog
  if(myEmulEventMapper->remapMode())
    myEmulEventMapper->handleKeyDown(key, mod);
  else if(myMenuEventMapper->remapMode())
    myMenuEventMapper->handleKeyDown(key, mod);
  else
    Dialog::handleKeyDown(key, mod);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void InputDialog::handleJoyDown(int stick, int button)
{
  // Remap joystick buttons in remap mode, otherwise pass to parent dialog
  if(myEmulEventMapper->remapMode())
    myEmulEventMapper->handleJoyDown(stick, button);
  else if(myMenuEventMapper->remapMode())
    myMenuEventMapper->handleJoyDown(stick, button);
  else
    Dialog::handleJoyDown(stick, button);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void InputDialog::handleJoyAxis(int stick, int axis, int value)
{
  // Remap joystick axis in remap mode, otherwise pass to parent dialog
  if(myEmulEventMapper->remapMode())
    myEmulEventMapper->handleJoyAxis(stick, axis, value);
  else if(myMenuEventMapper->remapMode())
    myMenuEventMapper->handleJoyAxis(stick, axis, value);
  else
    Dialog::handleJoyAxis(stick, axis, value);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool InputDialog::handleJoyHat(int stick, int hat, int value)
{
  // Remap joystick hat in remap mode, otherwise pass to parent dialog
  if(myEmulEventMapper->remapMode())
    return myEmulEventMapper->handleJoyHat(stick, hat, value);
  else if(myMenuEventMapper->remapMode())
    return myMenuEventMapper->handleJoyHat(stick, hat, value);
  else
    return Dialog::handleJoyHat(stick, hat, value);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void InputDialog::handleCommand(CommandSender* sender, int cmd,
                                int data, int id)
{
  switch(cmd)
  {
    case kOKCmd:
      saveConfig();
      close();
      break;

    case kCloseCmd:
      // Revert changes made to event mapping
      close();
      break;

    case kDefaultsCmd:
      setDefaults();
      break;

    case kDeadzoneChanged:
      myDeadzoneLabel->setValue(3200 + 1000*myDeadzone->getValue());
      break;

    case kDPSpeedChanged:
      myDPaddleLabel->setValue(myDPaddleSpeed->getValue());
      break;

    case kMPSpeedChanged:
      myMPaddleLabel->setValue(myMPaddleSpeed->getValue());
      break;

    case kTBSpeedChanged:
      myTrackBallLabel->setValue(myTrackBallSpeed->getValue());
      break;

    case kDBButtonPressed:
      if(!myJoyDialog)
        myJoyDialog = make_unique<JoystickDialog>
                          (this, instance().frameBuffer().font(), _w-60, _h-60);
      myJoyDialog->show();
      break;

    default:
      Dialog::handleCommand(sender, cmd, data, 0);
  }
}
